#include <hmalloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define KiB (1024UL)
#define MiB (1024UL * KiB)

char *p;
char *hp;

void access_memory(char *ptr, size_t size) {
    for (int repeat = 0; repeat < 10; repeat++) {  
        for (size_t i = 0; i < size; i++) {
            ptr[i] = 'y'; 
            volatile char temp = ptr[i]; 
        }
    }
}

double measure_access_time(char *ptr, size_t size) {
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    access_memory(ptr, size);
    clock_gettime(CLOCK_MONOTONIC, &end);
    return (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
}

int main() {
    size_t hsz = (2000) * MiB;
    size_t sz = (2000) * MiB;

    system("numactl -H ");
    p = malloc(sz);
    memset(p, 'x', sz);

    hp = hmalloc(hsz);
    memset(hp, 'x', hsz);
    printf("After hmalloc================================================\n");
    system("numactl -H ");
    printf("%ld MiB is allocated by malloc().\n", sz / MiB);
    printf("%ld MiB is allocated by hmalloc().\n", hsz / MiB);

    double local_time = measure_access_time(p, sz);
    double cxl_time = measure_access_time(hp, hsz);

    printf("Access time for local memory (malloc): %.6f seconds\n", local_time);
    printf("Access time for CXL memory (hmalloc): %.6f seconds\n", cxl_time);

    hfree(hp);
    free(p);

    return 0;
}