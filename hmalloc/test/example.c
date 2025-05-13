/* Copyright (c) 2024 SK hynix, Inc. */
/* SPDX-License-Identifier: BSD 2-Clause */

#include "../include/hmalloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KiB (1024UL)
#define MiB (1024UL * KiB)

char *p;
char *hp;

int main() {
    size_t hsz = 3000;
    size_t sz = 3000;

    system("numactl -H");
    p = malloc(sz * MiB);
    memset(p, 'x', sz * MiB);

    hp = hmalloc(hsz * MiB);
    memset(hp, 'x', hsz * MiB);

    system("numactl -H");
    printf("%ld MiB is allocated by malloc().\n", sz);
    printf("%ld MiB is allocated by hmalloc().\n", hsz);
    printf("Press enter to stop.\n");
    getchar();

    hfree(hp);
    free(p);

    return 0;
}
