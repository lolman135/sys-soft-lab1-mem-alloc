#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include <stddef.h>

void* mem_alloc(size_t size);
void mem_free(void* ptr);
void* mem_realloc(void* ptr, size_t new_size);
void mem_show(const char* operation);

#endif