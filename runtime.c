#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <stdbool.h>


typedef struct {
    void* ptr;           
    uint64_t size;       
    uint64_t id;         
    uint64_t read_count; 
    uint64_t write_count;
} MemoryBlock;


typedef struct HashNode {
    MemoryBlock block;
    struct HashNode* next;
} HashNode;

#define HASH_SIZE 10007
static HashNode* hash_table[HASH_SIZE] = {NULL};
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;


static uint64_t hash_ptr(void* ptr) {
    return ((uint64_t)ptr) % HASH_SIZE;
}


static MemoryBlock* find_block(void* ptr) {
    uint64_t index = hash_ptr(ptr);
    HashNode* node = hash_table[index];
    while (node) {
        if (node->block.ptr == ptr) {
            return &node->block;
        }
        node = node->next;
    }
    return NULL;
}


static void add_block(MemoryBlock block) {
    uint64_t index = hash_ptr(block.ptr);
    HashNode* new_node = (HashNode*)malloc(sizeof(HashNode));
    new_node->block = block;
    new_node->next = hash_table[index];
    hash_table[index] = new_node;
}

static void remove_block(void* ptr) {
    uint64_t index = hash_ptr(ptr);
    HashNode* node = hash_table[index];
    HashNode* prev = NULL;

    while (node) {
        if (node->block.ptr == ptr) {
            if (prev) {
                prev->next = node->next;
            } else {
                hash_table[index] = node->next;
            }
            FILE* f = fopen("heap.prof", "a");
            fprintf(f, "FINAL_STATS %lu %lu %lu %lu\n", 
                    node->block.id,
                    node->block.size,
                    node->block.read_count,
                    node->block.write_count);
            fclose(f);
            free(node);
            return;
        }
        prev = node;
        node = node->next;
    }
}

void* __heap_profile_register(void* ptr, uint64_t size, uint64_t id) {
    pthread_mutex_lock(&mutex);
    
    MemoryBlock block = {
        .ptr = ptr,
        .size = size,
        .id = id,
        .read_count = 0,
        .write_count = 0
    };
    
    add_block(block);
    
    FILE* f = fopen("heap.prof", "a");
    fprintf(f, "ALLOC %p %lu %lu\n", ptr, size, id);
    fclose(f);
    
    pthread_mutex_unlock(&mutex);
    return ptr;
}

void* __heap_profile_record_access(void* ptr, bool is_write) {
    pthread_mutex_lock(&mutex);
    
    MemoryBlock* block = find_block(ptr);
    if (block) {
        if (is_write) {
            block->write_count++;
        } else {
            block->read_count++;
        }
    }
    
    pthread_mutex_unlock(&mutex);
    return ptr;
}

void __heap_profile_unregister(void* ptr) {
    pthread_mutex_lock(&mutex);
    remove_block(ptr);
    pthread_mutex_unlock(&mutex);
}

static void __attribute__((destructor)) output_final_stats() {
    pthread_mutex_lock(&mutex);
    
    FILE* f = fopen("heap.prof", "a");
    fprintf(f, "=== Final Statistics ===\n");
    
    for (int i = 0; i < HASH_SIZE; i++) {
        HashNode* node = hash_table[i];
        while (node) {
            fprintf(f, "LEAK %lu %lu %lu %lu\n",
                    node->block.id,
                    node->block.size,
                    node->block.read_count,
                    node->block.write_count);
            node = node->next;
        }
    }
    
    fclose(f);
    pthread_mutex_unlock(&mutex);
} 