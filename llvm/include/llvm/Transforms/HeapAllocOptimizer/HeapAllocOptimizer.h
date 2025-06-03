#ifndef LLVM_TRANSFORMS_HEAPPROFILER_HEAPALLOCOPTIMIZER_H
#define LLVM_TRANSFORMS_HEAPPROFILER_HEAPALLOCOPTIMIZER_H
#include "llvm/IR/PassManager.h"
namespace llvm{
    struct SHeapVar {
    uint64_t id;
    uint64_t size;
    uint64_t read_count;
    uint64_t write_count;
    bool bLocal{true};
    std::string ptr;
    std::string location;
    double cost{0};
    double byte_cost{0};
    SHeapVar() = default;
    SHeapVar(uint64_t id_, uint64_t size_, uint64_t read_count_, uint64_t write_count_, const std::string& n)
        : id(id_), size(size_), read_count(read_count_), write_count(write_count_), ptr(n) {}
    static std::unique_ptr<SHeapVar> create(uint64_t id, uint64_t sz, 
                                        uint64_t rd, uint64_t wr,
                                        std::string p) {
        return std::make_unique<SHeapVar>(id, sz, rd, wr, std::string(p));
    }
  };
using HeapAccessMap = std::unique_ptr<llvm::DenseMap<uint64_t, std::unique_ptr<SHeapVar>>>;
  struct MemoryParams {
    double ddr_read_lat;   
    double ddr_write_lat;  
    double cxl_read_lat;   
    double cxl_write_lat; 
    double ddr_capacity;
  };
    struct HeapAllocOptimizer : public PassInfoMixin<HeapAllocOptimizer> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
  static bool isRequired() { return true; }

private:
  StringRef selectAllocator(CallBase *CI) const;
  void replaceAllocator(CallBase *CI, Module &M) const;
  bool loadProfileDataFromTxt();
  bool allocStrategy();
  uint64_t generateAllocID(const DebugLoc &Loc) const;
  HeapAccessMap  m_pAccessMap;
};
} // namespace

#endif // LLVM_TRANSFORMS_HEAPPROFILER_HEAPALLOCOPTIMIZER_H