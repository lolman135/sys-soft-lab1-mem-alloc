#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "allocator.h"

int main() {

    // Manual testing
    srand(time(NULL));

    Allocator* a = allocator_create();
    if (!a) return 1;

    void* p1 = mem_alloc(a, 200);
    void* p2 = mem_alloc(a, 150);
    void* p3 = mem_alloc(a, 100);

    mem_show(a, "mem_alloc");

    mem_free(a, p2);
    mem_show(a, "mem_free p2");

    mem_free(a, p1);
    mem_free(a, p3);
    mem_show(a, "mem_free all");

    allocator_destroy(a);
    return 0;
}