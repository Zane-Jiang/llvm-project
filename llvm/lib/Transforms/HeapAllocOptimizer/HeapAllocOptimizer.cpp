#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/ProfileData/InstrProf.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include "llvm/Transforms/HeapProfiler/My_hash.h"
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
static inline bool read_memory_params(const std::string& config_path,MemoryParams& params) {
    std::ifstream infile(config_path);
    if (!infile.is_open()) {
        errs()<<"Failed to open memory parameters file: " + config_path+"\n";
        return false;
    }
    std::map<std::string, double*> param_map = {
        {"ddr_read_lat", &params.ddr_read_lat},
        {"ddr_write_lat", &params.ddr_write_lat},
        {"cxl_read_lat", &params.cxl_read_lat},
        {"cxl_write_lat", &params.cxl_write_lat},
        {"ddr_capacity_MB",&params.ddr_capacity_MB}
    };
    
    std::string line;
    while (std::getline(infile, line)) {
        size_t eq_pos = line.find('=');
        if (eq_pos == std::string::npos) continue;
        
        std::string key = line.substr(0, eq_pos);
        key.erase(key.find_last_not_of(" \t") + 1);
        
        std::string value_str = line.substr(eq_pos + 1);
        double value = std::stod(value_str);
            
        auto it = param_map.find(key);
        if (it != param_map.end()) {
              *(it->second) = value;
        }

    }
    
    for (const auto& [key, ptr] : param_map) {
        if (*ptr == 0) {
            errs()<<("Missing parameter in config file: " + key);
            return false;
        }
    }
    return true;
}

PreservedAnalyses HeapAllocOptimizer::run(Module &M, ModuleAnalysisManager &AM) {
  if (!loadProfileDataFromTxt()) {
    errs()<<"Load Proffile falied, skip cxl place optimize \n";
    return PreservedAnalyses::all();
  }
  if(!allocStrategy()){
    return PreservedAnalyses::all();
  }
  m_AllocatorReplaceMap = {
    {"malloc", "hmalloc"},
    {"calloc", "hcalloc"},
    {"realloc", "hrealloc"},
    {"aligned_alloc","haligned_alloc"},
    {"posix_memalign", "hposix_memalign"},
    {"mmap", "hmmap"},
    {"_Znwm", "hmalloc"},
    {"_Znam", "hmalloc"},
  };

  m_HelperReplaceMap = {
    {"free", "hfree"},
    {"munmap", "hmunmap"},
    {"malloc_usable_size", "hmalloc_usable_size"},
    {"_ZdlPv", "hfree"},
    {"_ZdaPv", "hfree"},
  };
  std::vector<CallBase*> ToReplaceAllocator;
  for (Function &F : M) {
    for (BasicBlock &BB : F) {
      for (Instruction &I : BB) {
        if (auto *CI = dyn_cast<CallBase>(&I)) {
          if (Function *Callee = CI->getCalledFunction()) {
            StringRef Name = Callee->getName();
            if(m_AllocatorReplaceMap.count(Name)){
              ToReplaceAllocator.push_back(CI);
            }
          }
        }
      }
    }
  }

  for (CallBase* CI : ToReplaceAllocator) {
    replaceAllocator(CI, *CI->getModule());
  }
  return PreservedAnalyses::none();
}

bool HeapAllocOptimizer::allocStrategy()
{
  MemoryParams params;
  const char* conf_env = getenv("CXL_MEM_PARAMS_CONF");
  std::string conf_path = conf_env ? std::string(conf_env) : (std::string(getenv("HOME")) + "/.cxl_mem_params.conf");
  if(!read_memory_params(conf_path, params)){
    return false;
  }

  auto sortedVars = std::make_unique<std::vector<std::pair<uint64_t, SHeapVar*>>>();
  sortedVars->reserve(m_pAccessMap->size());
  for (auto &[id, var] : *m_pAccessMap) {
    var->cost = 0.0001 *  var->read_count * (params.cxl_read_lat - params.ddr_read_lat) +
                0.0001 *  var->write_count * (params.cxl_write_lat - params.ddr_write_lat);
    var->byte_cost = (var->size > 0) ? var->cost / var->size : 0;
    sortedVars->emplace_back(id, var.get());
  }

  std::sort(sortedVars->begin(), sortedVars->end(),
    [](const auto &a, const auto &b) {
      return a.second->byte_cost > b.second->byte_cost;
    });
  uint64_t usedCapacity = 0;
  // errs()<<"location     "<<"size   " <<"read count  "<<"write count    "<<"byte cost     is local\n" ;
  uint64_t ddr_capacity_B = params.ddr_capacity_MB * 1024;
  for (auto &[id, var] : *sortedVars) {
    if (usedCapacity + var->size <= ddr_capacity_B*0.8) {
      (*m_pAccessMap)[id]->bLocal = true;
      usedCapacity += var->size;
    } else {
      (*m_pAccessMap)[id]->bLocal = false;
    }
    // errs()<<var->location<<"  "<<var->size <<"   " <<var->read_count<<"  "<<var->write_count<<"  "<<var->byte_cost<<"   "<<(*m_pAccessMap)[id]->bLocal<<"\n" ;
  }
  return true;
}
bool HeapAllocOptimizer::loadProfileDataFromTxt() {
  if(nullptr == m_pAccessMap){
    m_pAccessMap = std::make_unique<llvm::DenseMap<uint64_t, std::unique_ptr<SHeapVar>>>();
  }
  m_pAccessMap->clear();

  const char* prof_env = getenv("HEAP_PROF_PATH");
  std::string prof_path = prof_env ? std::string(prof_env) : "heap.prof";
  std::ifstream infile(prof_path);
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
  std::string  localStr;
  uint64_t ID = generateAllocID(CI->getDebugLoc(),localStr);
  Function *Callee = nullptr;
  Callee = CI->getCalledFunction();
  if (Callee == nullptr){
    return "";
  }
  StringRef Name = Callee->getName();
  //todo add alloc support
  if(m_pAccessMap == nullptr || m_pAccessMap->find(ID) == m_pAccessMap->end())
  {
    if(m_pAccessMap == nullptr){
        errs()<<"m_pAccessMap == nullptr\n";
    }else{
      // errs()<<"select Allocator not find "<<ID <<"\n";
    }
    return "";
  }

  if(!(*m_pAccessMap)[ID]->bLocal){
    return  m_AllocatorReplaceMap.at(Name);
  }else{
    return "";
  }
}

void HeapAllocOptimizer::replaceAllocator(CallBase *CI, Module &M) const {
  StringRef NewAlloc = selectAllocator(CI);
  if(NewAlloc.empty()){
    return ;
  }else{
      std::string  localStr;
      generateAllocID(CI->getDebugLoc(),localStr);
      errs()<<"[releace]"<<localStr<<"  : "<<NewAlloc<<"\n";
  }
  replaceFreeForAllocation(CI, M, NewAlloc.str());
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

void HeapAllocOptimizer::replaceFreeForAllocation(Value *Allocation, Module &M, const std::string &AllocType) const {
    SmallVector<Value*, 16> Worklist;
    SmallPtrSet<Value*, 32> Visited;
    
    Worklist.push_back(Allocation);
    Visited.insert(Allocation);
    while (!Worklist.empty()) {
        Value *Current = Worklist.pop_back_val();
        for (User *U : Current->users()) {
            if (Visited.count(U)) continue;
            Visited.insert(U);

            if (auto *FreeCall = dyn_cast<CallBase>(U)) {
                if (Function *Callee = FreeCall->getCalledFunction()) {
                    if (Callee->getName() == "free" && FreeCall->getArgOperand(0) == Current && (AllocType == "hmalloc" || AllocType == "hcalloc" || AllocType == "hrealloc" || AllocType == "haligned_alloc" || AllocType == "hposix_memalign")) {
                        IRBuilder<> Builder(FreeCall);
                        Function *HFreeFunc = M.getFunction("hfree");
                        if (!HFreeFunc) {
                          Type* Int8PtrTy = PointerType::get(Type::getInt8Ty(M.getContext()), 0);
                          FunctionType *FreeFTy = FunctionType::get(
                            Type::getVoidTy(M.getContext()), 
                            {Int8PtrTy}, 
                            false
                          );
                          HFreeFunc = Function::Create(FreeFTy, GlobalValue::ExternalLinkage, "hfree", M);
                          HFreeFunc->setDoesNotThrow();
                        }
                        Value *Ptr = FreeCall->getArgOperand(0);
                        CallInst *NewCall = Builder.CreateCall(HFreeFunc, {Ptr});
                        NewCall->setDebugLoc(FreeCall->getDebugLoc());
                        FreeCall->eraseFromParent();
                        break;
                    } else if (Callee->getName() == "free" && FreeCall->getArgOperand(0) == Current && AllocType == "hmmap") {
                        IRBuilder<> Builder(FreeCall);
                        Function *HUnmapFunc = M.getFunction("hmunmap");
                        if (!HUnmapFunc) {
                          Type* Int8PtrTy = PointerType::get(Type::getInt8Ty(M.getContext()), 0);
                          FunctionType *UnmapFTy = FunctionType::get(
                            Type::getVoidTy(M.getContext()), 
                            {Int8PtrTy}, 
                            false
                          );
                          HUnmapFunc = Function::Create(UnmapFTy, GlobalValue::ExternalLinkage, "hmunmap", M);
                          HUnmapFunc->setDoesNotThrow();
                        }
                        Value *Ptr = FreeCall->getArgOperand(0);
                        CallInst *NewCall = Builder.CreateCall(HUnmapFunc, {Ptr});
                        NewCall->setDebugLoc(FreeCall->getDebugLoc());
                        FreeCall->eraseFromParent();
                        break;
                    }
                }
            } else if (isa<BitCastInst>(U) || isa<AddrSpaceCastInst>(U)) {
                Worklist.push_back(U);
            } else if (auto *GEP = dyn_cast<GetElementPtrInst>(U)) {
                if (all_of(GEP->indices(), [](Value *Idx) {
                        return isa<ConstantInt>(Idx) && 
                               cast<ConstantInt>(Idx)->isZero();
                    })) {
                    Worklist.push_back(GEP);
                }
            } else if (auto *Store = dyn_cast<StoreInst>(U)) {
                Worklist.push_back(Store->getPointerOperand());
            } else if (auto *Load = dyn_cast<LoadInst>(U)) {
                Worklist.push_back(Load);
            } else if (auto *PN = dyn_cast<PHINode>(U)) {
                Worklist.push_back(PN);
            } else if (auto *Sel = dyn_cast<SelectInst>(U)) {
                Worklist.push_back(Sel);
            } else if (isa<ICmpInst>(U) || isa<ReturnInst>(U) || isa<DbgInfoIntrinsic>(U) || isa<BranchInst>(U)) {
                continue;
            } else {
              // errs() << "=== Unhandled User ===\n";
              // errs() << "User pointer: " << (void*)U << "\n";
              // if (auto *I = dyn_cast<Instruction>(U)) {
                // errs() << "User opcode: " << I->getOpcodeName() << "\n";
                // I->print(errs());
              // } else {
                // U->print(errs());
              // }
              // errs() << "----------------------\n";
              continue;
            }
        }
    }
}

uint64_t HeapAllocOptimizer::generateAllocID(const DebugLoc &Loc,std::string&   LocationStr) const {
  static uint64_t next_id = 1;
  uint64_t id = 0;
  if (Loc) {
    id = my_hash_combine(  
        fnv1a_hash(Loc->getFilename()),
        Loc.getLine(),
        Loc.getCol());
        LocationStr = Loc->getFilename().str() + ":" + 
               std::to_string(Loc.getLine()) + ":" + 
               std::to_string(Loc.getCol());
        // errs()<<LocationStr<<"  "<<id<<"\n";
  }else{
    errs()<<"no id "<<id<<"\n";
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