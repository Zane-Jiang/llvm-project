#ifndef LLVM_TRANSFORMS_HEAPPROFILER_HEAPALLOCOPTIMIZER_H
#define LLVM_TRANSFORMS_HEAPPROFILER_HEAPALLOCOPTIMIZER_H
#include "llvm/IR/PassManager.h"
namespace llvm{
    struct HeapAllocOptimizer : public PassInfoMixin<HeapAllocOptimizer> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
  static bool isRequired() { return false; }
private:
  StringRef selectAllocator(uint64_t hotness) const;
  void replaceAllocator(CallBase *CI, StringRef NewAlloc, Module &M) const;
  bool loadProfileData(Module &M, DenseMap<uint64_t, uint64_t> &AccessMap);
};
} // namespace

#endif // LLVM_TRANSFORMS_HEAPPROFILER_HEAPALLOCOPTIMIZER_H