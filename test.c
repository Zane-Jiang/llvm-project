#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 一个简单的结构体，用于测试不同的访问模式
typedef struct {
    int id;
    double value;
    char name[32];
} Record;

// 热点访问函数
void hot_access(Record* records, int size) {
    for(int i = 0; i < 10000; i++) {
        // 频繁读取前半部分
        for(int j = 0; j < size/2; j++) {
            records[j].value += 0.1;  // 写操作
            double temp = records[j].value;  // 读操作
        }
    }
}

// 冷访问函数
void cold_access(Record* records, int size) {
    for(int i = 0; i < 100; i++) {
        // 较少访问后半部分
        for(int j = size/2; j < size; j++) {
            records[j].value += 0.1;  // 写操作
            double temp = records[j].value;  // 读操作
        }
    }
}

int main() {
    int sizes[] = {10, 100, 1000};
    
    for(int i = 0; i < 3; i++) {
        int size = sizes[i];
        printf("Testing with size: %d\n", size);
        
        // 使用malloc分配内存
        Record* records1 = (Record*)malloc(size * sizeof(Record));
        
        // 初始化数据
        for(int j = 0; j < size; j++) {
            records1[j].id = j;
            records1[j].value = j * 1.1;
            snprintf(records1[j].name, 32, "record-%d", j);
        }
        
        // 进行热点访问
        hot_access(records1, size);
        
        // 使用calloc分配另一块内存
        Record* records2 = (Record*)calloc(size, sizeof(Record));
        
        // 复制数据
        memcpy(records2, records1, size * sizeof(Record));
        
        // 进行冷访问
        cold_access(records2, size);
        
        // 使用realloc扩展内存
        records1 = (Record*)realloc(records1, size * 2 * sizeof(Record));
        
        // 对扩展的内存进行一些操作
        for(int j = size; j < size * 2; j++) {
            records1[j].id = j;
            records1[j].value = j * 1.1;
            snprintf(records1[j].name, 32, "record-%d", j);
        }
        
        // 释放内存
        free(records1);
        free(records2);
    }
    
    // 测试内存泄漏
    Record* leak = (Record*)malloc(sizeof(Record));
    leak->id = 999;
    leak->value = 999.999;
    strcpy(leak->name, "leaked-record");
    
    printf("Test completed\n");
    return 0;
} 