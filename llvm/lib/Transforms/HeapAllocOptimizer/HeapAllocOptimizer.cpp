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
  AccessMap.clear();
  if (!loadProfileDataFromTxt(AccessMap)) {
    return PreservedAnalyses::none();
  }
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

bool HeapAllocOptimizer::loadProfileDataFromTxt(DenseMap<uint64_t, uint64_t> &AccessMap) {
  std::ifstream infile("heap.prof");
  if (!infile.is_open()) return false;
  std::string line;
  std::getline(infile, line);
  std::getline(infile, line);
  while (std::getline(infile, line)) {
    std::istringstream iss(line);
    uint64_t id, size, read_count, write_count;
    std::string ptr, location;
    if (!(iss >> id >> ptr >> location >> size >> read_count >> write_count)) continue;
    AccessMap[id] = read_count + write_count;
  }
  return true;
}

StringRef HeapAllocOptimizer::selectAllocator(CallBase *CI) const {
  uint64_t ID = generateAllocID(CI->getDebugLoc());
  uint64_t hotness = 0;
  auto it = AccessMap.find(ID);
  if (it != AccessMap.end()) hotness = it->second;
  const uint64_t CXLThreshold = 1; 
  //todo add alloc support
  return hotness > CXLThreshold ? "hmalloc" : "malloc";
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
              if (Name == "heap-optimizer") {
                errs()<<"Name == heap-optimizer";
                MPM.addPass(HeapAllocOptimizer());
                return true;
              }
              return false;
            });
      }};
}