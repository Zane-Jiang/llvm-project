#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

// 简单的运行时函数实现
void* __heap_profile_register(void* ptr, uint64_t size, uint64_t id) {
    printf("[ALLOC] ptr=%p, size=%lu, id=%lu\n", ptr, size, id);
    return ptr;
}

void* __heap_profile_record_access(void* ptr, bool is_write) {
    printf("[ACCESS] ptr=%p, %s\n", ptr, is_write ? "WRITE" : "READ");
    return ptr;
}

void __heap_profile_unregister(void* ptr) {
    printf("[FREE] ptr=%p\n", ptr);
} 