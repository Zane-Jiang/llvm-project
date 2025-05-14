#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <sys/time.h>
#include <hmalloc.h>


static inline uint64_t get_time_us() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000 + tv.tv_usec;
}

// Test parameters
#define MEM_SIZE (1UL << 30)   // 1GB
#define ACCESS_COUNT 10000000  // 10 million accesses
#define CACHE_LINE_SIZE 64     // 64-byte cache line

void test_memory_latency(const char *mem_type, char *mem, size_t mem_size, size_t *access_seq) {
    printf("Testing %s memory random access latency (%d accesses)...\n", mem_type, ACCESS_COUNT);
    
    uint64_t total_latency = 0;
    uint64_t min_latency = UINT64_MAX;
    uint64_t max_latency = 0;
    
    for (size_t i = 0; i < ACCESS_COUNT; i++) {
        volatile char *addr = mem + access_seq[i];
        
        uint64_t start = get_time_us();
        volatile char val = *addr; 
        uint64_t end = get_time_us();
        
        uint64_t latency = end - start;
        
        total_latency += latency;
        if (latency < min_latency) min_latency = latency;
        if (latency > max_latency) max_latency = latency;
        
        (void)val; 
    }
    
 
    printf("\n%s Memory Test Results:\n", mem_type);
    printf("Total accesses: %d\n", ACCESS_COUNT);
    printf("Total latency (us): %lu\n", total_latency);
    printf("Average latency (ns): %.2f\n", (double)total_latency * 1000 / ACCESS_COUNT);
    printf("Minimum latency (ns): %lu\n", min_latency * 1000);
    printf("Maximum latency (ns): %lu\n", max_latency * 1000);
    printf("----------------------------------------\n");
}

int main() {

    printf("Generating random access sequence...\n");
    size_t *access_seq = (size_t *)malloc(ACCESS_COUNT * sizeof(size_t));
    if (!access_seq) {
        perror("Failed to allocate access sequence");
        return 1;
    }
    
    srand(time(NULL));
    for (size_t i = 0; i < ACCESS_COUNT; i++) {
        access_seq[i] = (size_t)(rand() % (MEM_SIZE / CACHE_LINE_SIZE)) * CACHE_LINE_SIZE;
    }
    
    printf("\nTesting LOCAL memory (malloc)...\n");
    printf("Allocating %lu MB local memory...\n", MEM_SIZE / (1 << 20));
    char *local_mem = (char *)malloc(MEM_SIZE);
    if (!local_mem) {
        perror("malloc failed");
        free(access_seq);
        return 1;
    }
    

    printf("Initializing local memory content...\n");
    for (size_t i = 0; i < MEM_SIZE; i++) {
        local_mem[i] = (char)(i % 256);
    }
    
  
    printf("Warming up cache...\n");
    for (size_t i = 0; i < ACCESS_COUNT / 100; i++) {
        __builtin_prefetch(local_mem + access_seq[i], 0, 3);
    }
    
    test_memory_latency("LOCAL", local_mem, MEM_SIZE, access_seq);
    free(local_mem);
    

    printf("\nTesting CXL memory (hmalloc)...\n");
    printf("Allocating %lu MB CXL memory...\n", MEM_SIZE / (1 << 20));
    char *cxl_mem = (char *)hmalloc(MEM_SIZE);
    if (!cxl_mem) {
        fprintf(stderr, "hmalloc failed\n");
        free(access_seq);
        return 1;
    }
    
    
    printf("Initializing CXL memory content...\n");
    for (size_t i = 0; i < MEM_SIZE; i++) {
        cxl_mem[i] = (char)(i % 256);
    }
    

    printf("Warming up cache...\n");
    for (size_t i = 0; i < ACCESS_COUNT / 100; i++) {
        __builtin_prefetch(cxl_mem + access_seq[i], 0, 3);
    }
    
    test_memory_latency("CXL", cxl_mem, MEM_SIZE, access_seq);
    hfree(cxl_mem);
    

    free(access_seq);
    
    return 0;
}