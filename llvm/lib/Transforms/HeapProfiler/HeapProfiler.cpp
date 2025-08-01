#include "llvm/Passes/PassPlugin.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Transforms/HeapProfiler/HeapProfiler.h"
#include <string>
#include "llvm/IR/Dominators.h"
#include <vector>
#include <algorithm>
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/GraphWriter.h"
#include "llvm/IR/CFG.h"
#include "llvm/Transforms/Utils/HeapVarIDUtil.h"

using namespace llvm;
template class llvm::PassInfoMixin<HeapProfiler>;

PreservedAnalyses HeapProfiler::run(Module &M, ModuleAnalysisManager &) {
  std::vector<CallBase*> allocCalls, freeCalls;

  for (Function &F : M) {
    for (BasicBlock &BB : F) {
      for (Instruction &I : BB) {
        if (auto *CI = dyn_cast<CallBase>(&I)) {
          if (Function *Callee = CI->getCalledFunction()) {
            StringRef Name = Callee->getName();
            if (Name == "malloc" || Name == "calloc" || Name ==  "realloc" || Name == "posix_memalign"
                || Name == "_Znwm" || Name == "_Znam") {
                  if(Name == "posix_memalign") {
                    errs() << "posix_memalign: " << CI->getDebugLoc().getLine() << "\n";
                  }
              allocCalls.push_back(CI);
            } else if (Name == "free" || Name == "hmunmap" 
                || Name == "_ZdlPv" || Name == "_ZdaPv") {
              freeCalls.push_back(CI);
            }
          }
        }
      }
    }
  }

  for (auto *CI : allocCalls) instrumentAlloc(CI, M);
  for (auto *CI : freeCalls) instrumentFree(CI, M);
  return PreservedAnalyses::all();
}

void HeapProfiler::instrumentAlloc(CallBase *CI, Module &M) {
  IRBuilder<> Builder(CI->getNextNode() ? CI->getNextNode() : CI);
  if (auto *Invoke = dyn_cast<InvokeInst>(CI)) {
    BasicBlock *normalDest = Invoke->getNormalDest();
    Builder.SetInsertPoint(&*normalDest->getFirstInsertionPt());
  }
  LLVMContext &Ctx = M.getContext();
  Type* Int8PtrTy = PointerType::get(Type::getInt8Ty(Ctx), 0);
  Value *Ptr = CI;
  if (!Ptr->getType()->isPointerTy()) {
    Ptr = Builder.CreateIntToPtr(Ptr, Int8PtrTy);
  } else if (Ptr->getType() != Int8PtrTy) {
    Ptr = Builder.CreateBitCast(Ptr, Int8PtrTy);
  }

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
    }else if (F->getName() == "posix_memalign") {
      errs() << "instrumentAlloc: posix_memalign\n";
      if (CI->arg_size() < 3) {
        errs() << "instrumentAlloc: posix_memalign not enough arguments!\n";
        CI->print(errs());
        errs() << "\n";
        return;
      }
      Size = CI->getArgOperand(2); 
      Value *PtrPtr = CI->getArgOperand(0); 
      Ptr = Builder.CreateLoad(Int8PtrTy, PtrPtr, "posix_mem_ptr"); 
      errs() << "instrumentAlloc: posix_memalign Ptr: " << Ptr << " Size: " << Size << "\n" ;
    }
  }

  if (Size->getType()->isPointerTy()) {
    Size = Builder.CreatePtrToInt(Size, Type::getInt64Ty(Ctx));
  } else if (Size->getType()->isIntegerTy() && Size->getType()->getIntegerBitWidth() != 64) {
    Size = Builder.CreateZExtOrTrunc(Size, Type::getInt64Ty(Ctx));
  }

  DebugLoc Loc = CI->getDebugLoc();
  std::string LocationStr;
  uint64_t ID = heapid::generateAllocID(Loc, LocationStr, CI->getParent(), *CI->getFunction());
  if (Function *F = CI->getCalledFunction()) {
    LocationStr += F->getName();
  }
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