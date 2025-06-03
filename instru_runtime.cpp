#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <map>
#include <unordered_map>
#include <thread>
#include <vector>

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
    uint64_t read_count;
    uint64_t write_count;
} MemoryBlock;

static std::map<uintptr_t, MemoryBlock> memory_tree;
static std::mutex mtx;

// thread-local addr -> (read_cnt, write_cnt)
thread_local std::unordered_map<uintptr_t, std::pair<uint64_t, uint64_t>> access_cache;

static std::vector<std::unordered_map<uintptr_t, std::pair<uint64_t, uint64_t>>*> all_access_caches;
static std::mutex access_caches_mtx;

void* __heap_profile_register(void* ptr, uint64_t size, uint64_t id, const char* location) {
    std::lock_guard<std::mutex> lock(mtx);
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    MemoryBlock block;
    block.ptr = ptr;
    block.size = size;
    block.id = id;
    block.read_count = 0;
    block.write_count = 0;
    var_count++;
    snprintf(block.location, sizeof(block.location), "%s", location ? location : "unknown");
    memory_tree[addr] = block;
    
    {
        std::lock_guard<std::mutex> lock(access_caches_mtx);
        all_access_caches.push_back(&access_cache);
    }
    
    return ptr;
}

void* __heap_profile_record_access(void* ptr, bool is_write) {
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    auto& stat = access_cache[addr];
    if (is_write) stat.second++;
    else stat.first++;

    if ((stat.first + stat.second) >= 1024) {
        std::lock_guard<std::mutex> lock(mtx);
        auto it = memory_tree.upper_bound(addr);
        if (it != memory_tree.begin()) {
            --it;
            uintptr_t block_start = it->first;
            uintptr_t block_end = block_start + it->second.size;
            if (addr >= block_start && addr < block_end) {
                it->second.read_count += stat.first;
                it->second.write_count += stat.second;
            }
        }
        stat = {0, 0}; 
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
                    it->second.read_count,
                    it->second.write_count);
        }
        memory_tree.erase(it);
    }
}

static void merge_thread_local_access_counts() {
    std::lock_guard<std::mutex> lock(access_caches_mtx);
    for (auto* cache : all_access_caches) {
        for (const auto& entry : *cache) {
            uintptr_t addr = entry.first;
            uint64_t read_count = entry.second.first;
            uint64_t write_count = entry.second.second;
            
            auto it = memory_tree.upper_bound(addr);
            if (it != memory_tree.begin()) {
                --it;
                uintptr_t block_start = it->first;
                uintptr_t block_end = block_start + it->second.size;
                if (addr >= block_start && addr < block_end) {
                    it->second.read_count += read_count;
                    it->second.write_count += write_count;
                }
            }
        }
    }
}

static void output_final_stats() __attribute__((destructor));
static void output_final_stats() {
    std::lock_guard<std::mutex> lock(mtx);
    merge_thread_local_access_counts();
    
    if (log_file) {
        for (const auto& pair : memory_tree) {
            const MemoryBlock& block = pair.second;
            fprintf(log_file, "%lu %p %s %lu %lu %lu\n",
                    block.id,
                    block.ptr,
                    block.location,
                    block.size,
                    block.read_count,
                    block.write_count);
        }
        fprintf(log_file, "var count %d\n", var_count);
        fflush(log_file);
        fclose(log_file);
    }
}

} // extern "C"