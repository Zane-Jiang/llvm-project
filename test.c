#include <stdio.h>
#include <stdlib.h>
#include <string.h>


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
    free(c);

    int* d = (int*)malloc(sizeof(int));
   *d = 40;
    printf("d: %p\n", d);
    free(d);
    return 0;
}