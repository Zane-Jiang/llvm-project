#include "llvm/Passes/PassPlugin.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Type.h"
#include "llvm/Transforms/HeapProfiler/HeapProfiler.h"

using namespace llvm;
template class llvm::PassInfoMixin<HeapProfiler>;

PreservedAnalyses HeapProfiler::run(Module &M, ModuleAnalysisManager &) {
  LLVMContext &Ctx = M.getContext();
  Type* VoidPtrTy  = Type::getInt8PtrTy(Ctx);
  Type* Int8PtrTy = Type::getInt8PtrTy(Ctx);
  
  FunctionCallee RegisterFunc = M.getOrInsertFunction(
      "__heap_profile_register", 
      FunctionType::get(VoidPtrTy, {Int8PtrTy, Type::getInt64Ty(Ctx), Type::getInt64Ty(Ctx)}, false));

  FunctionCallee RecordFunc = M.getOrInsertFunction(
      "__heap_profile_record_access",
      FunctionType::get(Type::getVoidTy(Ctx), {Int8PtrTy, Type::getInt1Ty(Ctx)}, false));

  FunctionCallee UnregisterFunc = M.getOrInsertFunction(
        "__heap_profile_unregister",
        FunctionType::get(Type::getVoidTy(Ctx), {Int8PtrTy}, false));
      
  for (Function &F : M) {
    for (BasicBlock &BB : F) {
      for (Instruction &I : BB) {
          if (auto *CI = dyn_cast<CallBase>(&I)) {
            if (Function *Callee = CI->getCalledFunction()) {
              StringRef Name = Callee->getName();
                if (Name.contains("malloc") || Name.contains("calloc") || 
                  Name.contains("realloc")) {
                  instrumentAlloc(CI, M);
                } else if (Name == "free") {
                  instrumentFree(CI, M);
                }
              }
          } else if (isa<LoadInst>(I) || isa<StoreInst>(I)) {
                instrumentAccess(&I, M);
          }
      }
    }
  }

  return PreservedAnalyses::all();
}

void HeapProfiler::instrumentAlloc(CallBase *CI, Module &M) {
  IRBuilder<> Builder(CI->getNextNode());
  LLVMContext &Ctx = M.getContext();
  Type* Int8PtrTy = PointerType::get(Type::getInt8Ty(Ctx), 0);
  Value *Ptr = Builder.CreateBitCast(CI, Int8PtrTy);
  
  Value *Size = CI->getArgOperand(0);
  if (CI->getCalledFunction()->getName() == "calloc") {
    Value *Nmemb = CI->getArgOperand(1);
    Size = Builder.CreateMul(Size, Nmemb);
  }
  
  DebugLoc Loc = CI->getDebugLoc();
  uint64_t ID = generateAllocID(Loc);

  FunctionCallee RegisterFunc = M.getFunction("__heap_profile_register");
  Builder.CreateCall(RegisterFunc, {Ptr, Size, Builder.getInt64(ID)});
}

void HeapProfiler::instrumentFree(CallBase *CI, Module &M) {
  IRBuilder<> Builder(CI);
  Value *Ptr = CI->getArgOperand(0);
  LLVMContext &Ctx = M.getContext();
  Type* Int8PtrTy = PointerType::get(Type::getInt8Ty(Ctx), 0);
  Ptr = Builder.CreateBitCast(Ptr, Int8PtrTy);
  
  FunctionCallee UnregisterFunc = M.getFunction("__heap_profile_unregister");
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
  
  if (!Addr) return;
  
  IRBuilder<> Builder(I);
  Value *AddrCast = Builder.CreateBitCast(Addr, 
      PointerType::get(Type::getInt8Ty(M.getContext()), 0));
  
  FunctionCallee RecordFunc = M.getFunction("__heap_profile_record_access");
  Builder.CreateCall(RecordFunc, {AddrCast, Builder.getInt1(isWrite)});
}

uint64_t HeapProfiler::generateAllocID(DebugLoc &Loc) {
  if (!Loc) return 0;
  return hash_combine(
      hash_value(Loc->getFilename()),
      Loc.getLine(),
      Loc.getCol());
}

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {
      LLVM_PLUGIN_API_VERSION, "HeapProfiler", "v0.2",
      [](PassBuilder &PB) {
        PB.registerPipelineParsingCallback(
            [](StringRef Name, ModulePassManager &MPM,
               ArrayRef<PassBuilder::PipelineElement>) {
              if (Name == "heap-profiler") {
                MPM.addPass(HeapProfiler());
                return true;
              }
              return false;
            });
      }};
}