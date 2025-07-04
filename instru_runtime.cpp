#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <atomic>
#include <chrono>

static FILE* log_file = nullptr;

struct ProfFileCleaner {
    ProfFileCleaner() {
        const char* prof_env = getenv("HEAP_PROF_PATH");
        const char* prof_path = prof_env ? prof_env : "heap.prof";
        log_file = fopen(prof_path, "w");
        if (log_file) {
            fprintf(log_file, "id | ptr | location | size | alloc_ts | free_ts\n");
            fflush(log_file);
        }
    }
};
static ProfFileCleaner profFileCleaner;

extern "C" {

struct MemoryBlock {
    void* ptr;
    uint64_t size;
    uint64_t id;
    char location[128];
    uint64_t alloc_ts;
    uint64_t free_ts;
};

static std::unordered_map<uintptr_t, MemoryBlock> memory_tree;
static std::mutex mtx;

static uint64_t get_timestamp() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

void* __heap_profile_register(void* ptr, uint64_t size, uint64_t id, const char* location) {
    if (!ptr) return ptr;
    std::lock_guard<std::mutex> lock(mtx);
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    auto& block = memory_tree[addr];
    block.ptr = ptr;
    block.size = size;
    block.id = id;
    block.alloc_ts = get_timestamp();
    block.free_ts = 0;
    if (location) {
        size_t loc_len = std::min<size_t>(strlen(location), sizeof(block.location) - 1);
        strncpy(block.location, location, loc_len);
        block.location[loc_len] = '\0';
    } else {
        snprintf(block.location, sizeof(block.location), "unknown");
    }
    return ptr;
}

void __heap_profile_unregister(void* ptr) {
    if (!ptr) return;
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    std::lock_guard<std::mutex> lock(mtx);
    auto it = memory_tree.find(addr);
    if (it != memory_tree.end()) {
        it->second.free_ts = get_timestamp();
        if (log_file) {
            fprintf(log_file, "%lu %p %s %lu %lu %lu\n",
                    it->second.id,
                    it->second.ptr,
                    it->second.location,
                    it->second.size,
                    it->second.alloc_ts,
                    it->second.free_ts);
            fflush(log_file);
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
                    block.alloc_ts,
                    block.free_ts);
        }
        fflush(log_file);
        fclose(log_file);
    }
}

} // extern "C"