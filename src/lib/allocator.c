#include "allocator.h"
#include <sys/mman.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#define ALIGN(size) (((size) + sizeof(void*) - 1) & ~(sizeof(void*) - 1))
#define BLOCK_SIZE sizeof(BlockHeader)

#define ARENA_SIZE (8192)     // 8KB arena size
#define MAX_BLOCKS 10         // Maximum number of blocks per arena

typedef struct BlockHeader {
    size_t size;
    struct BlockHeader* next;
    int free;
    int first;  // Is this the first block in the arena?
    int last;   // Is this the last block in the arena?
} BlockHeader;

typedef struct Arena {
    size_t size;
    struct Arena* next;
    void* data;
} Arena;

struct Allocator {
    Arena* arena_list;
};

static void* request_memory_from_kernel(size_t size) {
    void* ptr = mmap(NULL, size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) {
        perror("mmap failed");
        return NULL;
    }
    return ptr;
}

static void release_memory_to_kernel(void* ptr, size_t size) {
    if (munmap(ptr, size) == -1) {
        perror("munmap failed");
    }
}

static Arena* create_arena(size_t request_size) {
    size_t arena_total = ALIGN(sizeof(Arena) + ARENA_SIZE);
    void* memory = request_memory_from_kernel(arena_total);
    if (!memory) return NULL;

    Arena* arena = (Arena*)memory;
    arena->size = ARENA_SIZE;
    arena->next = NULL;
    arena->data = (void*)(arena + 1);

    // Сохраняем случайность, как было
    int num_blocks = rand() % MAX_BLOCKS + 1;
    if (num_blocks < 1) num_blocks = 1;

    size_t base_block_size = ARENA_SIZE / num_blocks;
    size_t remainder = ARENA_SIZE % num_blocks;

    BlockHeader* last_block = (BlockHeader*)arena->data;
    last_block->size = base_block_size + remainder;  // первый получает остаток
    last_block->free = 1;
    last_block->first = 1;
    last_block->last = (num_blocks == 1) ? 1 : 0;
    last_block->next = NULL;

    BlockHeader* prev = last_block;

    for (int i = 1; i < num_blocks; ++i) {
        BlockHeader* new_block = (BlockHeader*)((char*)prev + prev->size + BLOCK_SIZE);
        new_block->size = base_block_size;
        new_block->free = 1;
        new_block->first = 0;
        new_block->last = (i == num_blocks - 1) ? 1 : 0;
        new_block->next = NULL;

        prev->next = new_block;
        prev = new_block;
    }

    return arena;
}

Allocator* allocator_create(void) {
    Allocator* a = calloc(1, sizeof(Allocator));
    if (!a) return NULL;
    a->arena_list = NULL;
    return a;
}

void allocator_destroy(Allocator* a) {
    if (!a) return;
    Arena* curr = a->arena_list;
    while (curr) {
        Arena* next = curr->next;
        release_memory_to_kernel(curr, ALIGN(sizeof(Arena) + ARENA_SIZE));
        curr = next;
    }
    free(a);
}

void* mem_alloc(Allocator* a, size_t size) {
    if (!a) return NULL;
    size = ALIGN(size);

    Arena* arena = a->arena_list;
    while (arena) {
        BlockHeader* block = (BlockHeader*)arena->data;
        while (block) {
            if (block->free && block->size >= size) {
                block->free = 0;
                return (void*)(block + 1);
            }
            block = block->next;
        }
        arena = arena->next;
    }

    // новая арена
    Arena* new_arena = create_arena(size);
    if (!new_arena) return NULL;

    new_arena->next = a->arena_list;
    a->arena_list = new_arena;

    // пробуем ещё раз (теперь в новой арене)
    BlockHeader* block = (BlockHeader*)new_arena->data;
    while (block) {
        if (block->free && block->size >= size) {
            block->free = 0;
            return (void*)(block + 1);
        }
        block = block->next;
    }

    return NULL;
}

void mem_free(Allocator* a, void* ptr) {
    if (!a || !ptr) return;

    BlockHeader* to_free = (BlockHeader*)ptr - 1;
    if (to_free->free) {
        printf("Double free attempt: %p\n", ptr);
        return;
    }
    to_free->free = 1;

    // слияние (как было, но теперь через структуру)
    Arena* arena = a->arena_list;
    while (arena) {
        BlockHeader* curr = (BlockHeader*)arena->data;
        while (curr && curr->next) {
            if (curr->free && curr->next->free) {
                curr->size += curr->next->size + BLOCK_SIZE;
                curr->next = curr->next->next;
                // не продвигаем curr → проверяем этот же блок ещё раз
            } else {
                curr = curr->next;
            }
        }
        arena = arena->next;
    }
}

void* mem_realloc(Allocator* a, void* ptr, size_t new_size) {
    if (!a) return NULL;
    if (!ptr) return mem_alloc(a, new_size);

    BlockHeader* block = (BlockHeader*)ptr - 1;
    new_size = ALIGN(new_size);

    if (new_size <= block->size) {
        return ptr;           // ← важное улучшение: не копируем зря
    }

    void* new_ptr = mem_alloc(a, new_size);
    if (new_ptr) {
        memcpy(new_ptr, ptr, block->size);
        mem_free(a, ptr);
    }
    return new_ptr;
}

void mem_show(Allocator* a, const char* operation) {
    if (!a) return;

    printf("%s:\n", operation ? operation : "State");

    Arena* arena = a->arena_list;
    int arena_count = 1;

    while (arena) {
        printf("%d: Arena (%zu bytes)\n", arena_count++, arena->size);

        BlockHeader* block = (BlockHeader*)arena->data;
        int busy = 0, free_cnt = 0;

        while (block) {
            if (block->free) {
                free_cnt++;
                printf("* Block at %p → Size: %zu, Busy: No, First: %s, Last: %s\n",
                       (void*)(block + 1), block->size,
                       block->first ? "Yes" : "No",
                       block->last ? "Yes" : "No");
            } else {
                busy++;
                printf(" Block at %p → Size: %zu, Busy: Yes, First: %s, Last: %s\n",
                       (void*)(block + 1), block->size,
                       block->first ? "Yes" : "No",
                       block->last ? "Yes" : "No");
            }
            block = block->next;
        }

        printf("Occupied: %d/%d blocks\n\n", busy, busy + free_cnt);
        arena = arena->next;
    }
}