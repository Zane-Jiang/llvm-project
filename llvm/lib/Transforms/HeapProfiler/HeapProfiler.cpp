#include "llvm/Passes/PassPlugin.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"

#include "llvm/Transforms/HeapProfiler/HeapProfiler.h"

using namespace llvm;
template class llvm::PassInfoMixin<HeapProfiler>;

PreservedAnalyses HeapProfiler::run(Module &M, ModuleAnalysisManager &) {
  int mallocCount = 0;
  for (Function &F : M) {
    for (BasicBlock &BB : F) {
      for (Instruction &I : BB) {
        if (auto *CB = dyn_cast<CallBase>(&I)) {
          Value *CalledValue = CB->getCalledOperand();
          Function *Callee = dyn_cast<Function>(CalledValue->stripPointerCasts());

          if (Callee && Callee->getName() == "malloc") {
            mallocCount++;
          }
        }
      }
    }
  }

  errs() << "Total malloc calls: " << mallocCount << "\n";
  return PreservedAnalyses::all();
}

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return {
      LLVM_PLUGIN_API_VERSION, "HeapProfiler", "v1.0",
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