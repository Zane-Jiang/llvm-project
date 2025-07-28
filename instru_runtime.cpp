#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <atomic>
#include <time.h>
#include <iostream>
#include <thread> 
#include <limits>

extern "C" {
static FILE* log_file = nullptr;


struct ProfFileCleaner {
    ProfFileCleaner() {
        const char* prof_env = getenv("HEAP_PROF_PATH");
        const char* prof_path = prof_env ? prof_env : "heap.prof";
        log_file = fopen(prof_path, "w");
        if (log_file) {
            fprintf(log_file, "   id       |           ptr     |           location           | size(Byte)  | alloc_ts | free_ts\n");
            fflush(log_file);
        }
    }
};
static ProfFileCleaner profFileCleaner;



struct MemoryBlock {
    void* ptr;
    uint64_t size;
    uint64_t id;
    char location[128];
    double alloc_ts;
    double free_ts;
};

static double get_timestamp() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static std::mutex& get_mtx() {
    static std::mutex mtx;
    return mtx;
}

//递归重入保护
static thread_local bool in_hook = false;

static void output_final_stats() ;
struct MemoryTreeWithDtor : public std::unordered_map<uintptr_t, MemoryBlock> {
    ~MemoryTreeWithDtor() {
        output_final_stats();
        // fprintf(stderr, "[memory_tree] destructor called, size=%zu, addr=%p\n", this->size(), this);
        fflush(stderr);
    }
};

static MemoryTreeWithDtor& get_memory_tree() {
    static MemoryTreeWithDtor memory_tree;
    return memory_tree;
}

void* __heap_profile_register(void* ptr, uint64_t size, uint64_t id, const char* location) {
    if (in_hook) return ptr;
    in_hook = true;
    if (!ptr) { in_hook = false; return ptr; }
    std::lock_guard<std::mutex> lock(get_mtx());
    auto& memory_tree = get_memory_tree();
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    auto& block = memory_tree[addr];
    block.ptr = ptr;
    block.size = size;
    block.id = id;
    block.alloc_ts = get_timestamp();
    block.free_ts = std::numeric_limits<double>::max();
    if (location) {
        size_t loc_len = (strlen(location) < sizeof(block.location) - 1) ? strlen(location) : sizeof(block.location) - 1;
        for (size_t i = 0; i < loc_len; ++i) block.location[i] = location[i];
        block.location[loc_len] = '\0';
    } else {
        const char* unknown = "unknown";
        for (size_t i = 0; i < sizeof(block.location) - 1 && unknown[i]; ++i) block.location[i] = unknown[i];
        block.location[sizeof(block.location) - 1] = '\0';
    }
    in_hook = false;
    return ptr;
}

void __heap_profile_unregister(void* ptr) {
    if (in_hook) return;
    in_hook = true;
    if (!ptr) { in_hook = false; return; }
    std::lock_guard<std::mutex> lock(get_mtx());
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    auto& memory_tree = get_memory_tree();
    auto it = memory_tree.find(addr);
    if (it != memory_tree.end()) {
        it->second.free_ts = get_timestamp();
        if (log_file) {
            fprintf(log_file, "%lu %p %s %lu %.6f %.6f\n",
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
    in_hook = false;
}

static void output_final_stats() {
    std::lock_guard<std::mutex> lock(get_mtx());
    auto& memory_tree = get_memory_tree();
    // fprintf(stderr, "output_final_stats called, memory_tree size=%zu\n", memory_tree.size());
    fflush(stderr);
    if (log_file) {
        for (const auto& pair : memory_tree) {
            const MemoryBlock& block = pair.second;
            fprintf(log_file, "%lu %p %s %lu %.6f %.6f\n",
                    block.id,
                    block.ptr,
                    block.location,
                    block.size,
                    block.alloc_ts,
                    block.free_ts);
        }
        fflush(log_file);
        fclose(log_file);
        printf("file saved at %s\n", getenv("HEAP_PROF_PATH"));
    }
}

} // extern "C"