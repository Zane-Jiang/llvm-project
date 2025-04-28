#ifndef LLVM_TRANSFORMS_HEAPPROFILER_HEAPPROFILER_H
#define LLVM_TRANSFORMS_HEAPPROFILER_HEAPPROFILER_H

#include "llvm/IR/PassManager.h"

namespace llvm{
struct HeapProfiler : public PassInfoMixin<HeapProfiler> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &);
  static bool isRequired() { return true; }

private:
  void instrumentAlloc(CallBase *CI, Module &M);
  void instrumentFree(CallBase *I, Module &M);
  void instrumentAccess(Instruction *I, Module &M);
  uint64_t generateAllocID(DebugLoc &Loc);
};
} // namespace

#endif // LLVM_TRANSFORMS_HEAPPROFILER_HEAPPROFILER_H
