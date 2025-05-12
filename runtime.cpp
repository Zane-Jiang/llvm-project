#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <map>
#include <iostream>

struct ProfFileCleaner {
    ProfFileCleaner() {
        FILE* f = fopen("heap.prof", "w");
        fprintf(f, "=== Final Statistics ===\n");
        fprintf(f, "id | ptr | location | size | read_count | write_count\n");
        if (f) fclose(f);
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

void* __heap_profile_register(void* ptr, uint64_t size, uint64_t id, const char* location) {
    std::lock_guard<std::mutex> lock(mtx);
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    MemoryBlock block;
    block.ptr = ptr;
    block.size = size;
    block.id = id;
    block.read_count = 0;
    block.write_count = 0;
    if (location) {
        snprintf(block.location, sizeof(block.location), "%s", location);
    } else {
        snprintf(block.location, sizeof(block.location), "unknown");
    }
    memory_tree[addr] = block;
    return ptr;
}

void* __heap_profile_record_access(void* ptr, bool is_write) {
    std::lock_guard<std::mutex> lock(mtx);
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    
    auto it = memory_tree.upper_bound(addr);
    if (it != memory_tree.begin()) {
        --it;
        uintptr_t block_start = it->first;
        uintptr_t block_end = block_start + it->second.size;
        
        // 检查访问的地址是否在这个内存块的范围内
        if (addr >= block_start && addr < block_end) {
            // fprintf(stderr, "record access hit: %d %p\n", is_write, ptr);
            if (is_write)
                it->second.write_count++;
            else
                it->second.read_count++;
        } else {
            // fprintf(stderr, "record access miss: %d %p\n", is_write, ptr);
        }
    } else {
        // fprintf(stderr, "record access miss: %d %p\n", is_write, ptr);
    }
    return ptr;
}

void __heap_profile_unregister(void* ptr) {
    std::lock_guard<std::mutex> lock(mtx);
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    auto it = memory_tree.find(addr);
    if (it != memory_tree.end()) {
        FILE* f = fopen("heap.prof", "a");
        fprintf(f, "%lu %p %s %lu %lu %lu\n",
                it->second.id,
                it->second.ptr,
                it->second.location,
                it->second.size,
                it->second.read_count,
                it->second.write_count);
        fclose(f);
        memory_tree.erase(it);
    }
}

static void output_final_stats() __attribute__((destructor));
static void output_final_stats() {
    // todo :add memory leak static and deal
    // Here are some bugs and remove is temporaily


    // std::lock_guard<std::mutex> lock(mtx);
    // FILE* f = fopen("heap.prof", "a");
    // for (const auto& pair : memory_tree) {
        // const MemoryBlock& block = pair.second;
        // fprintf(stderr, "(LEAK) %lu %p %s %lu %lu %lu\n",
                // block.id,
                // block.ptr,
                // block.location,
                // block.size,
                // block.read_count,
                // block.write_count);
    // }
    // fclose(f);
}

} // extern "C" 