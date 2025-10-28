#ifndef LLVM_CGHMP_ANALYSIS_VARIABLETYPEANALYSISPASS_H
#define LLVM_CGHMP_ANALYSIS_VARIABLETYPEANALYSISPASS_H
#include "llvm/IR/PassManager.h"
namespace llvm {
    struct VariableTypeAnalysisPass : public PassInfoMixin<VariableTypeAnalysisPass> {
        public:
        PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);
        static bool isRequired() { return true; }
    };
}
#endif
