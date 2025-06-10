#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <map>
#include <thread>
#include <vector>
#include <atomic>

static FILE* log_file = nullptr;
int var_count = 0;
struct ProfFileCleaner {
    ProfFileCleaner() {
        log_file = fopen("heap.prof", "w");
        if (log_file) {
            fprintf(log_file, "=== Final Statistics ===\n");
            fprintf(log_file, "id | ptr | location | size | read_count | write_count\n");
        }
    }
};
static ProfFileCleaner profFileCleaner;

extern "C" {

typedef struct {
    void* ptr;
    uint64_t size;
    uint64_t id;
    char location[128];
    std::atomic<uint64_t> read_count;
    std::atomic<uint64_t> write_count;
} MemoryBlock;

static std::map<uintptr_t, MemoryBlock> memory_tree;
static std::mutex mtx;


void* __heap_profile_register(void* ptr, uint64_t size, uint64_t id, const char* location) {
    std::lock_guard<std::mutex> lock(mtx);
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    
    // 使用emplace构造对象，避免拷贝
    auto result = memory_tree.emplace(std::piecewise_construct,
                                    std::forward_as_tuple(addr),
                                    std::forward_as_tuple());
    MemoryBlock& block = result.first->second;
    block.ptr = ptr;
    block.size = size;
    block.id = id;
    block.read_count = 0;
    block.write_count = 0;
    var_count++;
    snprintf(block.location, sizeof(block.location), "%s", location ? location : "unknown");
    
    return ptr;
}


void* __heap_profile_record_access(void* ptr, bool is_write) {
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    std::lock_guard<std::mutex> lock(mtx);
    // 查找包含该地址的内存块
    auto it = memory_tree.upper_bound(addr);
    if (it != memory_tree.begin()) {
        --it;
        uintptr_t block_start = it->first;
        uintptr_t block_end = block_start + it->second.size;
        if (addr >= block_start && addr < block_end) {
            if (is_write) {
                it->second.write_count.fetch_add(1, std::memory_order_relaxed);
            } else {
                it->second.read_count.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
    return ptr;
}

void __heap_profile_unregister(void* ptr) {
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    std::lock_guard<std::mutex> lock(mtx);
    auto it = memory_tree.find(addr);
    if (it != memory_tree.end()) {
        if (log_file) {
            fprintf(log_file, "%lu %p %s %lu %lu %lu\n",
                    it->second.id,
                    it->second.ptr,
                    it->second.location,
                    it->second.size,
                    it->second.read_count.load(),
                    it->second.write_count.load());
        }
        memory_tree.erase(it);
    }
}

static void output_final_stats() __attribute__((destructor));
static void output_final_stats() {
    std::lock_guard<std::mutex> lock(mtx);
    
    if (log_file) {
        for (const auto& pair : memory_tree) {
            const MemoryBlock& block = pair.second;
            fprintf(log_file, "%lu %p %s %lu %lu %lu\n",
                    block.id,
                    block.ptr,
                    block.location,
                    block.size,
                    block.read_count.load(),
                    block.write_count.load());
        }
        fprintf(log_file, "var count %d\n", var_count);
        fflush(log_file);
        fclose(log_file);
    }
}

} // extern "C"