#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/ProfileData/InstrProf.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include "llvm/Transforms/HeapAllocOptimizer/HeapAllocOptimizer.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/ProfileData/InstrProfReader.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include <fstream>
#include <sstream>
#include <string>
using namespace llvm;

PreservedAnalyses HeapAllocOptimizer::run(Module &M, ModuleAnalysisManager &AM) {
  if (!loadProfileDataFromTxt()) {
    errs()<<"Load Proffile falied, skip cxl place optimize ";
    return PreservedAnalyses::none();
  }
  allocStrategy();
  std::vector<CallBase*> ToReplace;
  for (Function &F : M) {
    for (BasicBlock &BB : F) {
      for (Instruction &I : BB) {
        if (auto *CI = dyn_cast<CallBase>(&I)) {
          if (Function *Callee = CI->getCalledFunction()) {
            StringRef Name = Callee->getName();
            if (Name.contains("malloc") || Name.contains("calloc") || Name.contains("realloc")) {
              ToReplace.push_back(CI);
            }
          }
        }
      }
    }
  }
  for (CallBase* CI : ToReplace) {
    replaceAllocator(CI, *CI->getModule());
  }
  return PreservedAnalyses::all();
}
void HeapAllocOptimizer::allocStrategy()
{
  MemoryParams params;
  uint64_t ddrCapacity;
  auto sortedVars = std::make_unique<std::vector<std::pair<uint64_t, SHeapVar*>>>();
  sortedVars->reserve(m_pAccessMap->size());
  for (auto &[id, var] : *m_pAccessMap) {
    var->cost = var->read_count * (params.cxl_read_lat - params.ddr_read_lat) +
                var->write_count * (params.cxl_write_lat - params.ddr_write_lat);
    var->byte_cost = (var->size > 0) ? var->cost / var->size : 0;
    sortedVars->emplace_back(id, var.get());
  }

  std::sort(sortedVars->begin(), sortedVars->end(),
    [](const auto &a, const auto &b) {
      return a.second->byte_cost > b.second->byte_cost;
    });

  
  uint64_t usedCapacity = 0;
  for (auto &[id, var] : *sortedVars) {
    if (usedCapacity + var->size <= ddrCapacity*0.8) {
      (*m_pAccessMap)[id]->bLocal = true;
      usedCapacity += var->size;
    } else {
      (*m_pAccessMap)[id]->bLocal = false;
    }
  }
}
bool HeapAllocOptimizer::loadProfileDataFromTxt() {
  if(nullptr == m_pAccessMap){
    m_pAccessMap = std::make_unique<llvm::DenseMap<uint64_t, std::unique_ptr<SHeapVar>>>();
  }
  m_pAccessMap->clear();

  std::ifstream infile("heap.prof");
  if (!infile.is_open()) return false;
  std::string line;
  std::getline(infile, line);
  std::getline(infile, line);
  while (std::getline(infile, line)) {
    std::istringstream iss(line);
    SHeapVar heapVar;
    if (!(iss >> heapVar.id >> heapVar.ptr >> heapVar.location >> heapVar.size >> heapVar.read_count >> heapVar.write_count)) continue;
    (*m_pAccessMap)[heapVar.id] = std::make_unique<SHeapVar>(heapVar);
  }
  return true;
}

StringRef HeapAllocOptimizer::selectAllocator(CallBase *CI) const {
  errs()<<"hmalooc~~~~~~~~~~~";
  return "hmalloc";
  uint64_t ID = generateAllocID(CI->getDebugLoc());
  //todo add alloc support
  if((*m_pAccessMap)[ID]->bLocal){
    return "malloc";
  }else{
    return "hmalloc";
  }
}

void HeapAllocOptimizer::replaceAllocator(CallBase *CI, Module &M) const {
  StringRef NewAlloc = selectAllocator(CI);
  if(NewAlloc.empty()){
    return ;
  }
  IRBuilder<> Builder(CI);
  FunctionType *FTy = CI->getFunctionType();
  Function *NewFunc = M.getFunction(NewAlloc);
  if (!NewFunc) {
    NewFunc = Function::Create(FTy, GlobalValue::ExternalLinkage, NewAlloc, M);
    NewFunc->setAttributes(CI->getCalledFunction()->getAttributes());
  }
  
  SmallVector<Value *, 4> Args;
  unsigned numArgs = CI->arg_size();
  for (unsigned i = 0; i < numArgs; ++i) {
    Args.push_back(CI->getArgOperand(i));
  }
  CallInst *NewCall = Builder.CreateCall(FTy, NewFunc, Args);
  CI->replaceAllUsesWith(NewCall);
  CI->eraseFromParent();
}

uint64_t HeapAllocOptimizer::generateAllocID(const DebugLoc &Loc) const {
  static uint64_t next_id = 1;
  int id = 0;
  if (Loc) {
    id = hash_combine(  
        hash_value(Loc->getFilename()),
        Loc.getLine(),
        Loc.getCol());
  }
  return id ? id : next_id++;
}

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {
      LLVM_PLUGIN_API_VERSION, "HeapAllocOptimizer", "v0.1",
      [](PassBuilder &PB) {
        PB.registerPipelineParsingCallback(
            [](StringRef Name, ModulePassManager &MPM,
               ArrayRef<PassBuilder::PipelineElement>) {
              char *env = getenv("CLANG_MODE");
              if(!env && 0 == strcmp(env, "OPTIMIZE")){
                MPM.addPass(HeapAllocOptimizer());
                return true;
              }else if(env || (!env && strcmp(env, "AOTU"))){
                if (Name == "heap-optimizer") {
                    errs()<<"addPass(HeapAllocOptimizer())";
                    MPM.addPass(HeapAllocOptimizer());
                    return true;
                }
              }
              return false;
            });
      }};
}