#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <thread>

extern "C" {

struct MemoryBlock {
    void* ptr;
    uint64_t size;
    uint64_t id;
    char location[128];
    uint64_t read_count;
    uint64_t write_count;
};


struct IntervalNode {
    uintptr_t start, end, max_end;
    MemoryBlock block;
    IntervalNode *left, *right;
    IntervalNode(uintptr_t s, uintptr_t e, const MemoryBlock& b)
        : start(s), end(e), max_end(e), block(b), left(nullptr), right(nullptr) {}
};

class IntervalTree {
    IntervalNode* root = nullptr;

    IntervalNode* insert(IntervalNode* node, uintptr_t s, uintptr_t e, const MemoryBlock& b) {
        if (!node) return new IntervalNode(s, e, b);
        if (s < node->start)
            node->left = insert(node->left, s, e, b);
        else
            node->right = insert(node->right, s, e, b);
        node->max_end = node->max_end;
        node->max_end = std::max(node->max_end, e);
        if (node->left) node->max_end = std::max(node->max_end, node->left->max_end);
        if (node->right) node->max_end = std::max(node->max_end, node->right->max_end);
            return node;
    }

    IntervalNode* find(IntervalNode* node, uintptr_t addr) {
        if (!node) return nullptr;
        if (addr >= node->start && addr < node->end)
            return node;
        if (node->left && addr < node->left->max_end)
            return find(node->left, addr);
        return find(node->right, addr);
    }

    void inorder_output(IntervalNode* node, FILE* f) {
        if (!node) return;
        inorder_output(node->left, f);
        const MemoryBlock& b = node->block;
        fprintf(f, "%lu %p %s %lu %lu %lu\n",
                b.id, b.ptr, b.location, b.size, b.read_count, b.write_count);
        inorder_output(node->right, f);
    }

    void destroy(IntervalNode* node) {
        if (!node) return;
        destroy(node->left);
        destroy(node->right);
        delete node;
    }

public:
    ~IntervalTree() { destroy(root); }

    void insert(uintptr_t s, uintptr_t e, const MemoryBlock& b) {
        root = insert(root, s, e, b);
    }

    MemoryBlock* find(uintptr_t addr) {
        IntervalNode* node = find(root, addr);
        return node ? &node->block : nullptr;
    }

    void output(FILE* f) {
        inorder_output(root, f);
    }
};

static IntervalTree tree;
static std::mutex mtx;


thread_local std::unordered_map<uintptr_t, uint64_t> read_cache;


static FILE* g_profile_file = nullptr;

struct ProfFileInitializer {
    ProfFileInitializer() {
        g_profile_file = fopen("heap.prof", "w");
        if (g_profile_file) {
            fprintf(g_profile_file, "=== Final Statistics ===\n");
            fprintf(g_profile_file, "id | ptr | location | size | read_count | write_count\n");
        }
    }
    ~ProfFileInitializer() {
        if (g_profile_file) {
            tree.output(g_profile_file); // 最后输出所有剩余块
            fclose(g_profile_file);
            g_profile_file = nullptr;
        }
    }
};
static ProfFileInitializer cleaner;


void* __heap_profile_register(void* ptr, uint64_t size, uint64_t id, const char* location) {
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    MemoryBlock block;
    block.ptr = ptr;
    block.size = size;
    block.id = id;
    block.read_count = 0;
    block.write_count = 0;
    snprintf(block.location, sizeof(block.location), "%s", location ? location : "unknown");

    std::lock_guard<std::mutex> lock(mtx);
    tree.insert(addr, addr + size, block);
    return ptr;
}


void* __heap_profile_record_access(void* ptr, bool is_write) {
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    if (!is_write) {
        auto& count = read_cache[addr];
        count++;
        if (count >= 1024) {
            std::lock_guard<std::mutex> lock(mtx);
            MemoryBlock* block = tree.find(addr);
            if (block) block->read_count += count;
            count = 0;
        }
    } else {
        std::lock_guard<std::mutex> lock(mtx);
        MemoryBlock* block = tree.find(addr);
        if (block) block->write_count++;
    }
    return ptr;
}


void __heap_profile_unregister(void* ptr) {
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    std::lock_guard<std::mutex> lock(mtx);
    MemoryBlock* block = tree.find(addr);
    if (block && g_profile_file) {
        fprintf(g_profile_file, "%lu %p %s %lu %lu %lu\n",
                block->id, block->ptr, block->location, block->size,
                block->read_count, block->write_count);
    }
}

} // extern "C"
