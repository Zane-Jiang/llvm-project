#include <stdio.h>
#include <stdlib.h>

int main() {
    // 测试malloc和free
    int* ptr = (int*)malloc(sizeof(int) * 10);
    
    // 写操作
    for(int i = 0; i < 10; i++) {
        ptr[i] = i;
    }
    
    // 读操作
    for(int i = 0; i < 10; i++) {
        printf("%d ", ptr[i]);
    }
    printf("\n");
    
    free(ptr);
    
    // 测试内存泄漏
    int* leak = (int*)malloc(sizeof(int));
    *leak = 42;
    
    return 0;
} 