#ifndef LLVM_TRANSFORMS_HEAPPROFILER_HEAPALLOCOPTIMIZER_H
#define LLVM_TRANSFORMS_HEAPPROFILER_HEAPALLOCOPTIMIZER_H
#include "llvm/IR/PassManager.h"
namespace llvm{
    struct HeapAllocOptimizer : public PassInfoMixin<HeapAllocOptimizer> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
  static bool isRequired() { return false; }
private:
  StringRef selectAllocator(CallBase *CI) const;
  void replaceAllocator(CallBase *CI, Module &M) const;
  bool loadProfileDataFromTxt(DenseMap<uint64_t, uint64_t> &AccessMap);
  uint64_t generateAllocID(const DebugLoc &Loc) const;
  DenseMap<uint64_t, uint64_t> AccessMap;
};
} // namespace

#endif // LLVM_TRANSFORMS_HEAPPROFILER_HEAPALLOCOPTIMIZER_H