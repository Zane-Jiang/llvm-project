#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <map>
#include <thread>
#include <vector>
#include <atomic>
#include <random>
#include <cctype>
#include <algorithm>

static FILE* log_file = nullptr;
int var_count = 0;

static uint32_t g_sampling_interval = 1000;

struct ProfFileCleaner {
    ProfFileCleaner() {
        const char* env = getenv("HEAP_SAMPLING_INTERVAL");
        if (env) {
            char* end;
            long val = strtol(env, &end, 10);
            if (end != env && *end == '\0' && val > 0 && val <= UINT32_MAX) {
                g_sampling_interval = static_cast<uint32_t>(val);
            } else {
                fprintf(stderr, "Invalid HEAP_SAMPLING_INTERVAL value: %s. Using default %u\n", 
                        env, g_sampling_interval);
            }
        }
        
        const char* prof_env = getenv("HEAP_PROF_PATH");
        const char* prof_path = prof_env ? prof_env : "heap.prof";
        log_file = fopen(prof_path, "w");
        if (log_file) {
            fprintf(log_file, "=== Final Statistics (Sampling Rate: 1/%u) ===\n", g_sampling_interval);
            fprintf(log_file, "id | ptr | location | size | read_count | write_count\n");
            fflush(log_file);
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

static thread_local std::mt19937* rng = nullptr;
static thread_local std::uniform_int_distribution<uint32_t>* dist = nullptr;

static bool should_sample() {
    if (g_sampling_interval == 1) return true;
    

    if (rng == nullptr) {
        rng = new std::mt19937(
            std::random_device{}() ^ 
            static_cast<uint64_t>(reinterpret_cast<uintptr_t>(rng)) ^
            static_cast<uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()))
        );
        
        dist = new std::uniform_int_distribution<uint32_t>(0, g_sampling_interval - 1);
    }
    
    return (*dist)(*rng) == 0;
}

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
    
    if (location) {
        size_t loc_len = std::min<size_t>(strlen(location), sizeof(block.location) - 1);
        strncpy(block.location, location, loc_len);
        block.location[loc_len] = '\0';
    } else {
        snprintf(block.location, sizeof(block.location), "unknown");
    }
    
    return ptr;
}


void* __heap_profile_record_access(void* ptr, bool is_write) {
    if (g_sampling_interval == 0 || !should_sample()) {
        return ptr;
    }

    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    std::lock_guard<std::mutex> lock(mtx);
    
    auto it = memory_tree.upper_bound(addr);
    if (it != memory_tree.begin()) {
        --it;
        uintptr_t block_start = it->first;
        uintptr_t block_end = block_start + it->second.size;
        if (addr >= block_start && addr < block_end) {
            if (is_write) {
                it->second.write_count.fetch_add(g_sampling_interval, std::memory_order_relaxed);
            } else {
                it->second.read_count.fetch_add(g_sampling_interval, std::memory_order_relaxed);
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
                    block.read_count.load(),
                    block.write_count.load());
        }
        fprintf(log_file, "var count %d\n", var_count);
        fprintf(log_file, "sampling rate 1/%u\n", g_sampling_interval);
        fflush(log_file);
        fclose(log_file);
    }
}

} // extern "C"