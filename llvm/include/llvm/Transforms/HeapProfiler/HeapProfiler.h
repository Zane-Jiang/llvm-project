#ifndef LLVM_TRANSFORMS_HEAPPROFILER_HEAPPROFILER_H
#define LLVM_TRANSFORMS_HEAPPROFILER_HEAPPROFILER_H

#include "llvm/IR/PassManager.h"

namespace llvm{
struct HeapProfiler : public PassInfoMixin<HeapProfiler> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &);
  static bool isRequired() { return true; }
};
} // namespace

#endif // LLVM_TRANSFORMS_HEAPPROFILER_HEAPPROFILER_H
