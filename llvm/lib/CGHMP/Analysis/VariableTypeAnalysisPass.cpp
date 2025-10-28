#include "llvm/CGHMP/Analysis/VariableTypeAnalysisPass.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Type.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Passes/PassBuilder.h"
using namespace llvm;
template class llvm::PassInfoMixin<VariableTypeAnalysisPass>;

PreservedAnalyses VariableTypeAnalysisPass::run(Module &M,
                                                ModuleAnalysisManager &MAM) {
  // Implementation of the analysis would go here.
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


//   for (auto *CI : allocCalls) instrumentAlloc(CI, M);
//   for (auto *CI : freeCalls) instrumentFree(CI, M);
//   for (auto *I : accessInsts) instrumentAccess(I, M);

  return PreservedAnalyses::all();
}


extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {
      LLVM_PLUGIN_API_VERSION, "VariableTypeAnalysisPass", "v0.1",
      [](PassBuilder &PB) {
        PB.registerPipelineParsingCallback(
            [](StringRef Name, ModulePassManager &MPM,
              ArrayRef<PassBuilder::PipelineElement>) {
                errs()<<"addPass(VariableTypeAnalysisPass())";
                MPM.addPass(VariableTypeAnalysisPass());
                return true;
            });
      }};
}