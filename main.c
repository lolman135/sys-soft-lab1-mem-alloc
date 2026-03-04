#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "allocator.h"


int main() {
    srand(time(NULL)); // Initialize random number generator

    void* p1 = mem_alloc(200);
    void* p2 = mem_alloc(150);
    void* p3 = mem_alloc(100);

    mem_show("mem_alloc");

    mem_free(p2);
    mem_show("mem_free");

    mem_free(p1);
    mem_free(p3);
    mem_show("mem_free");

    return 0;
}
