#include "allocator.h"
#include <sys/mman.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#define ALIGNMENT     sizeof(void*)
#define ALIGN(size)   (((size) + ALIGNMENT - 1) & ~(ALIGNMENT - 1))
#define HEADER_SIZE   sizeof(BlockHeader)
#define MIN_SPLIT     (HEADER_SIZE * 2)
#define ARENA_SIZE    (8192ULL)

typedef struct BlockHeader {
    size_t size;
    int free;
    struct BlockHeader* next_free;
} BlockHeader;

typedef struct Arena {
    size_t usable_size;
    struct Arena* next;
    void* memory;
    BlockHeader* free_list;
} Arena;

struct Allocator {
    Arena* arenas;
};

static void* sys_alloc(size_t size) {
    void* p = mmap(NULL, size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        perror("mmap failed");
        return NULL;
    }
    return p;
}

static void sys_free(void* ptr, size_t size) {
    if (ptr) munmap(ptr, size);
}

static Arena* create_arena(void) {
    size_t total = ALIGN(sizeof(Arena) + ARENA_SIZE);
    void* raw = sys_alloc(total);
    if (!raw) return NULL;

    Arena* arena = (Arena*)raw;
    arena->usable_size = ARENA_SIZE;
    arena->memory = (char*)raw + sizeof(Arena);
    arena->next = NULL;

    BlockHeader* block = (BlockHeader*)arena->memory;
    block->size = ARENA_SIZE - HEADER_SIZE;
    block->free = 1;
    block->next_free = NULL;

    arena->free_list = block;
    return arena;
}

static Arena* find_arena(Allocator* a, void* ptr) {
    char* p = (char*)ptr;
    Arena* ar = a->arenas;
    while (ar) {
        char* start = (char*)ar->memory;
        if (p >= start && p < start + ar->usable_size) {
            return ar;
        }
        ar = ar->next;
    }
    return NULL;
}

// Порядок функцій тепер точно відповідає header-файлу

Allocator* allocator_create(void) {
    Allocator* a = calloc(1, sizeof(Allocator));
    if (!a) return NULL;
    a->arenas = NULL;
    return a;
}

void* mem_alloc(Allocator* a, size_t size) {
    if (!a || size == 0) return NULL;
    size = ALIGN(size);

    Arena* arena = a->arenas;
    while (arena) {
        BlockHeader** prev = &arena->free_list;
        BlockHeader* block = *prev;

        while (block) {
            if (block->free && block->size >= size) {
                if (block->size >= size + MIN_SPLIT) {
                    BlockHeader* rest = (BlockHeader*)((char*)(block + 1) + size);
                    rest->size = block->size - size - HEADER_SIZE;
                    rest->free = 1;
                    rest->next_free = block->next_free;

                    block->size = size;
                    block->next_free = rest;
                }
                block->free = 0;
                *prev = block->next_free;
                return (char*)(block + 1);
            }
            prev = &block->next_free;
            block = *prev;
        }
        arena = arena->next;
    }

    Arena* new_arena = create_arena();
    if (!new_arena) return NULL;

    new_arena->next = a->arenas;
    a->arenas = new_arena;

    BlockHeader* block = new_arena->free_list;
    if (block->size >= size + MIN_SPLIT) {
        BlockHeader* rest = (BlockHeader*)((char*)(block + 1) + size);
        rest->size = block->size - size - HEADER_SIZE;
        rest->free = 1;
        rest->next_free = NULL;

        block->size = size;
        block->next_free = rest;
    }
    block->free = 0;
    new_arena->free_list = block->next_free;

    return (char*)(block + 1);
}

void* mem_realloc(Allocator* a, void* ptr, size_t new_size) {
    if (!ptr) return mem_alloc(a, new_size);
    if (new_size == 0) {
        mem_free(a, ptr);
        return NULL;
    }

    new_size = ALIGN(new_size);
    BlockHeader* block = (BlockHeader*)ptr - 1;

    if (new_size <= block->size) return ptr;

    void* new_ptr = mem_alloc(a, new_size);
    if (new_ptr) {
        memcpy(new_ptr, ptr, block->size);
        mem_free(a, ptr);
    }
    return new_ptr;
}

void mem_free(Allocator* a, void* ptr) {
    if (!a || !ptr) return;

    Arena* arena = find_arena(a, ptr);
    if (!arena) {
        printf("Invalid free: %p\n", ptr);
        return;
    }

    BlockHeader* block = (BlockHeader*)ptr - 1;
    if (block->free) {
        printf("Double free: %p\n", ptr);
        return;
    }

    block->free = 1;
    block->next_free = arena->free_list;
    arena->free_list = block;

    BlockHeader** curr_ptr = &arena->free_list;
    while (*curr_ptr) {
        BlockHeader* curr = *curr_ptr;
        char* curr_end = (char*)(curr + 1) + curr->size;

        BlockHeader** next_ptr = &curr->next_free;
        while (*next_ptr) {
            BlockHeader* next = *next_ptr;
            if ((char*)(next + 1) == curr_end) {
                curr->size += next->size + HEADER_SIZE;
                *next_ptr = next->next_free;
            } else {
                next_ptr = &next->next_free;
            }
        }
        curr_ptr = &curr->next_free;
    }
}

void allocator_destroy(Allocator* a) {
    if (!a) return;
    Arena* cur = a->arenas;
    while (cur) {
        Arena* nxt = cur->next;
        sys_free(cur->memory - sizeof(Arena), ALIGN(sizeof(Arena) + ARENA_SIZE));
        cur = nxt;
    }
    free(a);
}

void mem_show(Allocator* a, const char* op) {
    if (!a) return;

    printf("%s:\n", op ? op : "State");

    int idx = 1;
    Arena* ar = a->arenas;
    while (ar) {
        printf("%d: Arena (%zu bytes)\n", idx++, ar->usable_size);

        int used = 0, free_cnt = 0;
        BlockHeader* b = (BlockHeader*)ar->memory;
        char* end = (char*)ar->memory + ar->usable_size;

        while ((char*)b < end) {
            if (b->free) {
                free_cnt++;
                printf("  free  %p  size: %zu\n", (char*)(b + 1), b->size);
            } else {
                used++;
                printf("  used  %p  size: %zu\n", (char*)(b + 1), b->size);
            }
            b = (BlockHeader*)((char*)(b + 1) + b->size);
        }

        printf("  used: %d  free: %d\n\n", used, free_cnt);
        ar = ar->next;
    }
}