#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// typedef struct {
    // int id;
    // double value;
    // char name[32];
// } Record;
// 
// void hot_access(Record* records, int size) {
    // for(int i = 0; i < 10000; i++) {
        // for(int j = 0; j < size/2; j++) {
            // records[j].value += 0.1;
            // double temp = records[j].value;
        // }
    // }
// }
// 
// void cold_access(Record* records, int size) {
    // for(int i = 0; i < 100; i++) {
        // for(int j = size/2; j < size; j++) {
            // records[j].value += 0.1;
            // double temp = records[j].value;
        // }
    // }
// }
// 
// int main() {
    // int sizes[] = {10, 100, 1000};
    // 
    // for(int i = 0; i < 3; i++) {
        // int size = sizes[i];
        // printf("Testing with size: %d\n", size);
        // 
        // Record* records1 = (Record*)malloc(size * sizeof(Record));
        // 
        // for(int j = 0; j < size; j++) {
            // records1[j].id = j;
            // records1[j].value = j * 1.1;
            // snprintf(records1[j].name, 32, "record-%d", j);
        // }
        // 
        // hot_access(records1, size);
        // 
        // Record* records2 = (Record*)calloc(size, sizeof(Record));
        // 
        // memcpy(records2, records1, size * sizeof(Record));
        // 
        // cold_access(records2, size);
        // 
        // records1 = (Record*)realloc(records1, size * 2 * sizeof(Record));
        // 
        // for(int j = size; j < size * 2; j++) {
            // records1[j].id = j;
            // records1[j].value = j * 1.1;
            // snprintf(records1[j].name, 32, "record-%d", j);
        // }
        // 
        // free(records1);
        // free(records2);
    // }
    // 
    // Record* leak = (Record*)malloc(sizeof(Record));
    // leak->id = 999;
    // leak->value = 999.999;
    // strcpy(leak->name, "leaked-record");
    // 
    // printf("Test completed\n");
    // return 0;
// }

int main() {
    int* a = (int*)malloc(sizeof(int));
    *a = 10;
    printf("%d\n", *a);
    printf("a: %p\n", a);
    free(a);

    // int* b = (int*)malloc(sizeof(int));
    // *b = 20;
    // *b = 30;
    // *b = 40;
    // printf("%d\n", *b);
    // free(b);

    int* c = (int*)malloc(sizeof(int));
    *c = 40;
    printf("c: %p\n", c);
    return 0;
}