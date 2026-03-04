#include <sys/mman.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#define ALIGN(size) (((size) + sizeof(void*) - 1) & ~(sizeof(void*) - 1))
#define BLOCK_SIZE sizeof(BlockHeader)
#define ARENA_SIZE (16384) // 8KB arena size
#define MAX_BLOCKS 10    // Maximum number of blocks per arena

typedef struct BlockHeader {
    size_t size;
    struct BlockHeader* next;
    int free;
    int first; // Is this the first block in the arena?
    int last;  // Is this the last block in the arena?
} BlockHeader;

typedef struct Arena {
    size_t size;
    struct Arena* next;
    void* data;
} Arena;

Arena* arena_list = NULL;

void* request_memory_from_kernel(size_t size) {
    void* ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) {
        perror("mmap failed");
        return NULL;
    }
    return ptr;
}

void release_memory_to_kernel(void* ptr, size_t size) {
    if (munmap(ptr, size) == -1) {
        perror("munmap failed");
    }
}

Arena* create_arena(size_t size) {
    size = ALIGN(size);
    void* memory = request_memory_from_kernel(size + sizeof(Arena));
    if (!memory) return NULL;

    Arena* arena = (Arena*)memory;
    arena->size = size;
    arena->next = NULL;
    arena->data = (void*)(arena + 1);

    // Random number of blocks in this arena (1 to MAX_BLOCKS)
    int num_blocks = rand() % MAX_BLOCKS + 1;

    BlockHeader* last_block = (BlockHeader*)arena->data;
    last_block->free = 1;
    last_block->size = size / num_blocks;
    last_block->first = 1;  // Mark as the first block
    last_block->last = 0;   // Not the last block yet
    last_block->next = NULL;

    // Create the remaining blocks
    for (int i = 1; i < num_blocks; ++i) {
        BlockHeader* new_block = (BlockHeader*)(((char*)last_block) + last_block->size);
        new_block->size = size / num_blocks;
        new_block->free = 1;
        new_block->first = 0;  // Not the first block
        new_block->last = 0;   // Not the last block yet
        new_block->next = NULL;
        last_block->next = new_block;
        last_block = new_block;
    }

    // Mark the last block
    last_block->last = 1;

    if (!arena_list) {
        arena_list = arena;
    } else {
        Arena* current = arena_list;
        while (current->next) {
            current = current->next;
        }
        current->next = arena;
    }

    return arena;
}

void* mem_alloc(size_t size) {
    size = ALIGN(size);
    Arena* arena = arena_list;
    while (arena) {
        BlockHeader* block = (BlockHeader*)arena->data;
        while (block) {
            if (block->size >= size && block->free) {
                block->free = 0;
                return (void*)(block + 1);
            }
            block = block->next;
        }
        arena = arena->next;
    }

    // If no suitable block is found, create a new arena
    Arena* new_arena = create_arena(size + sizeof(BlockHeader));
    if (!new_arena) {
        return NULL;
    }

    // Try to allocate in the new arena
    return mem_alloc(size);
}

void mem_free(void* ptr) {
    if (!ptr) return;

    // Find the block header and check if it was already freed
    BlockHeader* block = (BlockHeader*)ptr - 1;

    if (block->free) {
        printf("Attempt to free already freed block: %p\n", ptr);
        return; // Block is already freed
    }

    block->free = 1;

    // Try to merge adjacent free blocks
    Arena* arena = arena_list;
    while (arena) {
        BlockHeader* current_block = (BlockHeader*)arena->data;
        while (current_block) {
            if (current_block->free && current_block->next && current_block->next->free) {
                current_block->size += current_block->next->size + BLOCK_SIZE;
                current_block->next = current_block->next->next;
            }
            current_block = current_block->next;
        }
        arena = arena->next;
    }
}

void* mem_realloc(void* ptr, size_t new_size) {
    if (!ptr) {
        return mem_alloc(new_size);
    }

    BlockHeader* block = (BlockHeader*)ptr - 1;
    if (block->size >= new_size) {
        return ptr;
    }

    void* new_ptr = mem_alloc(new_size);
    if (new_ptr) {
        memcpy(new_ptr, ptr, block->size);
        mem_free(ptr);
    }

    return new_ptr;
}

void mem_show(const char* operation) {
    Arena* arena = arena_list;
    int arena_count = 1;

    printf("%s:\n", operation);  // Output the current operation

    while (arena) {
        printf("%d:\n", arena_count);
        printf("Arena (%zu bytes)\n", arena->size);

        BlockHeader* block = (BlockHeader*)arena->data;
        int busy_count = 0;
        int free_count = 0;

        while (block) {
            if (block->free) {
                free_count++;
                printf("* Block at %p → Size: %zu, Busy: No, First: %s, Last: %s\n",
                       block, block->size,
                       block->first ? "Yes" : "No",
                       block->last ? "Yes" : "No");
            } else {
                busy_count++;
                printf("  Block at %p → Size: %zu, Busy: Yes, First: %s, Last: %s\n",
                       block, block->size,
                       block->first ? "Yes" : "No",
                       block->last ? "Yes" : "No");
            }
            block = block->next;
        }

        printf("Occupied: %d/%d blocks\n", busy_count, busy_count + free_count);

        arena = arena->next;
        arena_count++;
    }
}

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
