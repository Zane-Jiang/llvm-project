#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <iostream>

struct ProfFileCleaner {
    ProfFileCleaner() {
        FILE* f = fopen("heap.prof", "w");
        fprintf(f, "=== Final Statistics ===\n");
        fprintf(f, "TYPE | ptr | size | id | read_count | write_count\n");
        if (f) fclose(f);
    }
};
static ProfFileCleaner profFileCleaner;

extern "C" {

typedef struct {
    void* ptr;
    uint64_t size;
    uint64_t id;
    uint64_t read_count;
    uint64_t write_count;
} MemoryBlock;

static std::unordered_map<void*, MemoryBlock> hash_table;
static std::mutex mtx;

void* __heap_profile_register(void* ptr, uint64_t size, uint64_t id) {
    std::lock_guard<std::mutex> lock(mtx);
    MemoryBlock block = {ptr, size, id, 0, 0};
    hash_table[ptr] = block;
    FILE* f = fopen("heap.prof", "a");
    fprintf(f, "ALLOC %p %lu %lu\n", ptr, size, id);
    fprintf(stderr, "ALLOC %p %lu %lu\n", ptr, size, id);
    fclose(f);
    return ptr;
}

void* __heap_profile_record_access(void* ptr, bool is_write) {
    std::lock_guard<std::mutex> lock(mtx);
    auto it = hash_table.find(ptr);
    if (it != hash_table.end()) {  
        fprintf(stderr, "record access hit: %d %p\n", is_write, ptr);
        if (is_write)
            it->second.write_count++;
        else
            it->second.read_count++;
    }else{
        fprintf(stderr, "record access miss: %d %p\n", is_write, ptr);
    }
    return ptr;
}

void __heap_profile_unregister(void* ptr) {
    std::lock_guard<std::mutex> lock(mtx);
    auto it = hash_table.find(ptr);
    if (it != hash_table.end()) {
        FILE* f = fopen("heap.prof", "a");
        fprintf(f, "FINAL_STATS %p %lu %lu %lu %lu\n",
                it->second.ptr,
                it->second.size,
                it->second.id,
                it->second.read_count,
                it->second.write_count);
        fclose(f);
        fprintf(stderr, "FINAL_STATS %p %lu %lu %lu %lu\n",
                it->second.ptr,
                it->second.size,
                it->second.id,
                it->second.read_count,
                it->second.write_count);    
        hash_table.erase(it);
    }
}

static void output_final_stats() __attribute__((destructor));
static void output_final_stats() {
    std::lock_guard<std::mutex> lock(mtx);
    FILE* f = fopen("heap.prof", "a");
    fprintf(f, "=== Final Statistics === hash_table size: %lu\n", hash_table.size());
    for (const auto& pair : hash_table) {
        const MemoryBlock& block = pair.second;
        fprintf(f, "LEAK %p %lu %lu %lu %lu\n",
                block.ptr,
                block.size,
                block.id,
                block.read_count,
                block.write_count);
        fprintf(stderr, "LEAK %p %lu %lu %lu %lu\n",
                block.ptr,
                block.size,
                block.id,
                block.read_count,
                block.write_count);
    }
    fclose(f);
}

} // extern "C" 