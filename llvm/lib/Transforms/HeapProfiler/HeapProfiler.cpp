#include "llvm/Passes/PassPlugin.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Transforms/HeapProfiler/HeapProfiler.h"
#include <string>

using namespace llvm;
template class llvm::PassInfoMixin<HeapProfiler>;

PreservedAnalyses HeapProfiler::run(Module &M, ModuleAnalysisManager &) {
  std::vector<CallBase*> allocCalls, freeCalls;
  std::vector<Instruction*> accessInsts;

  for (Function &F : M) {
    for (BasicBlock &BB : F) {
      for (Instruction &I : BB) {
        if (auto *CI = dyn_cast<CallBase>(&I)) {
          if (Function *Callee = CI->getCalledFunction()) {
            StringRef Name = Callee->getName();
            if (Name.contains("malloc") || Name.contains("calloc") || Name.contains("realloc")) {
              allocCalls.push_back(CI);
            } else if (Name == "free") {
              freeCalls.push_back(CI);
            }
          }
        } else if (isa<LoadInst>(I) || isa<StoreInst>(I)) {
          accessInsts.push_back(&I);
        }
      }
    }
  }

  for (auto *CI : allocCalls) instrumentAlloc(CI, M);
  for (auto *CI : freeCalls) instrumentFree(CI, M);
  for (auto *I : accessInsts) instrumentAccess(I, M);

  return PreservedAnalyses::all();
}

void HeapProfiler::instrumentAlloc(CallBase *CI, Module &M) {
  IRBuilder<> Builder(CI->getNextNode() ? CI->getNextNode() : CI);
  LLVMContext &Ctx = M.getContext();
  Type* Int8PtrTy = PointerType::get(Type::getInt8Ty(Ctx), 0);
  Value *Ptr = Builder.CreateBitCast(CI, Int8PtrTy);

  if (CI->arg_size() < 1) {
    errs() << "instrumentAlloc: not enough arguments!\n";
    CI->print(errs());
    errs() << "\n";
    return;
  }
  Value *Size = CI->getArgOperand(0);

  if (Function *F = CI->getCalledFunction()) {
    if (F->getName() == "calloc") {
      if (CI->arg_size() < 2) {
        errs() << "instrumentAlloc: calloc not enough arguments!\n";
        CI->print(errs());
        errs() << "\n";
        return;
      }
      Value *Nmemb = CI->getArgOperand(1);
      Size = Builder.CreateMul(Size, Nmemb);
    }
  }

  if (Size->getType()->isPointerTy()) {
    Size = Builder.CreatePtrToInt(Size, Type::getInt64Ty(Ctx));
  } else if (Size->getType()->isIntegerTy() && Size->getType()->getIntegerBitWidth() != 64) {
    Size = Builder.CreateZExtOrTrunc(Size, Type::getInt64Ty(Ctx));
  }

  DebugLoc Loc = CI->getDebugLoc();
  std::string LocationStr;
  uint64_t ID = generateAllocID(Loc,LocationStr);
  Value *LocationStrVal = Builder.CreateGlobalString(LocationStr);

  FunctionCallee RegisterFunc = M.getOrInsertFunction(
      "__heap_profile_register",
      FunctionType::get(Int8PtrTy, 
                       {Int8PtrTy, 
                        Type::getInt64Ty(Ctx), 
                        Type::getInt64Ty(Ctx),
                        PointerType::get(Type::getInt8Ty(Ctx), 0)}, 
                       false));
  if (!RegisterFunc) {
    errs() << "instrumentAlloc: RegisterFunc is nullptr!\n";
    return;
  }
  Builder.CreateCall(RegisterFunc, {Ptr, Size, Builder.getInt64(ID), LocationStrVal});
}

void HeapProfiler::instrumentFree(CallBase *CI, Module &M) {
  IRBuilder<> Builder(CI->getNextNode() ? CI->getNextNode() : CI);

  if (CI->arg_size() < 1) {
    errs() << "instrumentFree: not enough arguments!\n";
    CI->print(errs());
    errs() << "\n";
    return;
  }
  Value *Ptr = CI->getArgOperand(0);

  LLVMContext &Ctx = M.getContext();
  Type* Int8PtrTy = PointerType::get(Type::getInt8Ty(Ctx), 0);
  Ptr = Builder.CreateBitCast(Ptr, Int8PtrTy);

  FunctionCallee UnregisterFunc = M.getOrInsertFunction(
      "__heap_profile_unregister",
      FunctionType::get(Type::getVoidTy(Ctx), {Int8PtrTy}, false));
  if (!UnregisterFunc) {
    errs() << "instrumentFree: UnregisterFunc is nullptr!\n";
    return;
  }
  Builder.CreateCall(UnregisterFunc, {Ptr});
}

void HeapProfiler::instrumentAccess(Instruction *I, Module &M) {
  Value *Addr = nullptr;
  bool isWrite = false;

  if (auto *LI = dyn_cast<LoadInst>(I)) {
    Addr = LI->getPointerOperand();
    isWrite = false;
  } else if (auto *SI = dyn_cast<StoreInst>(I)) {
    Addr = SI->getPointerOperand();
    isWrite = true;
  }

  if (!Addr) {
    errs() << "instrumentAccess: Addr is nullptr\n";
    return;
  }

  if (!Addr->getType()->isPointerTy()) {
    errs() << "instrumentAccess: Addr is not a pointer!\n";
    I->print(errs());
    errs() << "\n";
    return;
  }

  IRBuilder<> Builder(I);
  Value *AddrCast = Builder.CreateBitCast(Addr, 
      PointerType::get(Type::getInt8Ty(M.getContext()), 0));

  FunctionCallee RecordFunc = M.getOrInsertFunction(
      "__heap_profile_record_access",
      FunctionType::get(Type::getVoidTy(M.getContext()), 
                        {PointerType::get(Type::getInt8Ty(M.getContext()), 0), 
                         Type::getInt1Ty(M.getContext())}, false));
  Builder.CreateCall(RecordFunc, {AddrCast, Builder.getInt1(isWrite)});
}

uint64_t HeapProfiler::generateAllocID(DebugLoc &Loc , std::string& LocationStr) {
  static uint64_t next_id = 1;
  int id = 0;
  LocationStr = "unknown:0:0";
  if (Loc) {
    id = hash_combine(  
        hash_value(Loc->getFilename()),
        Loc.getLine(),
        Loc.getCol());
    LocationStr = Loc->getFilename().str() + ":" + 
               std::to_string(Loc.getLine()) + ":" + 
               std::to_string(Loc.getCol());
  }
  return id ? id : next_id++;
}

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {
      LLVM_PLUGIN_API_VERSION, "HeapProfiler", "v0.2",
      [](PassBuilder &PB) {
        PB.registerPipelineParsingCallback(
            [](StringRef Name, ModulePassManager &MPM,
              ArrayRef<PassBuilder::PipelineElement>) {
              // char *env = getenv("CLANG_MODE");
              // if(!env && 0 == strcmp(env, "INSTRUMENT")){
                errs()<<"addPass(HeapProfiler())";
                // MPM.addPass(HeapProfiler());
                // return true;
              // }else if(env || (!env && strcmp(env, "AOTU"))){
                // if (Name == "heap-profiler") {
                    MPM.addPass(HeapProfiler());
                    return true;
                // }
              // }
              // return false;
            });
      }};
}