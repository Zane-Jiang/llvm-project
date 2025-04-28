#include <stdlib.h>
#include <stdio.h>

int main() {
    // 简单的内存分配和访问
    int* p = (int*)malloc(sizeof(int));
    *p = 42;        // 写操作
    printf("%d\n", *p);  // 读操作
    free(p);
    return 0;
} 