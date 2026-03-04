#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include <stddef.h>

typedef struct Allocator Allocator;

Allocator* allocator_create(void);
void       allocator_destroy(Allocator* a);

void*      mem_alloc(Allocator* a, size_t size);
void       mem_free(Allocator* a, void* ptr);
void*      mem_realloc(Allocator* a, void* ptr, size_t new_size);
void       mem_show(Allocator* a, const char* operation);

#endif