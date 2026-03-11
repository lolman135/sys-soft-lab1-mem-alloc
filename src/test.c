#include "allocator.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h>
#include <string.h>

#define MAX_BLOCKS 2048
#define ITERATIONS 2000
#define MAX_ALLOC_SIZE 8192
#define MIN_ALLOC_SIZE 1

typedef struct {
    void* ptr;
    size_t requested_size;
    uint64_t checksum;
} TrackedBlock;

// Simple hash function to detect data corruption
static uint64_t simple_checksum(const void* data, size_t len) {
    if (!data || len == 0) return 0;

    uint64_t hash = 0x517cc1b727220a95ULL;  // random seed
    const uint8_t* bytes = (const uint8_t*)data;

    for (size_t i = 0; i < len; i++) {
        hash = hash * 31 + bytes[i];
    }
    return hash;
}

int main(void) {

    // Automated testing
    srand(time(NULL) ^ (uintptr_t)&main);

    Allocator* alloc = allocator_create();
    if (!alloc) {
        fprintf(stderr, "Failed to create allocator\n");
        return 1;
    }

    TrackedBlock blocks[MAX_BLOCKS];
    int active_count = 0;

    printf("Starting stress / random operations test...\n");
    printf("Iterations: %d   Max blocks at once: %d\n\n", ITERATIONS, MAX_BLOCKS);

    for (int i = 0; i < ITERATIONS; i++) {
        int r = rand() % 100;

        
        if (r < 45 && active_count < MAX_BLOCKS) {
            size_t sz = MIN_ALLOC_SIZE + (rand() % (MAX_ALLOC_SIZE - MIN_ALLOC_SIZE + 1));
            void* p = mem_alloc(alloc, sz);

            if (p) {
                // Fill block with random bytes
                for (size_t j = 0; j < sz; j++) {
                    ((uint8_t*)p)[j] = (uint8_t)rand();
                }

                uint64_t cs = simple_checksum(p, sz);

                blocks[active_count].ptr = p;
                blocks[active_count].requested_size = sz;
                blocks[active_count].checksum = cs;
                active_count++;
            }
        }
        
        else if (r < 85 && active_count > 0) {
            int idx = rand() % active_count;
            void* p = blocks[idx].ptr;

            // Verify checksum before free
            uint64_t current_cs = simple_checksum(p, blocks[idx].requested_size);
            if (current_cs != blocks[idx].checksum) {
                fprintf(stderr,
                        "\n[FAIL] Memory corruption detected before free!\n"
                        "  iteration: %d\n  block idx: %d\n  ptr: %p\n  size: %zu\n",
                        i, idx, p, blocks[idx].requested_size);
                goto cleanup;
            }

            mem_free(alloc, p);

            // Remove from tracking array (swap with last)
            blocks[idx] = blocks[active_count - 1];
            active_count--;
        }
        
        else if (active_count > 0) {
            int idx = rand() % active_count;
            void* old_ptr = blocks[idx].ptr;
            size_t old_sz = blocks[idx].requested_size;

            // Verify checksum before realloc
            uint64_t current_cs = simple_checksum(old_ptr, old_sz);
            if (current_cs != blocks[idx].checksum) {
                fprintf(stderr,
                        "\n[FAIL] Memory corruption detected before realloc!\n"
                        "  iteration: %d\n  block idx: %d\n  ptr: %p\n  size: %zu\n",
                        i, idx, old_ptr, old_sz);
                goto cleanup;
            }

            size_t new_sz = MIN_ALLOC_SIZE + (rand() % (MAX_ALLOC_SIZE - MIN_ALLOC_SIZE + 1));
            void* new_ptr = mem_realloc(alloc, old_ptr, new_sz);

            if (new_ptr) {
                // Fill new block with random data
                for (size_t j = 0; j < new_sz; j++) {
                    ((uint8_t*)new_ptr)[j] = (uint8_t)rand();
                }

                uint64_t new_cs = simple_checksum(new_ptr, new_sz);

                blocks[idx].ptr = new_ptr;
                blocks[idx].requested_size = new_sz;
                blocks[idx].checksum = new_cs;
            }
            
        }
    }

    // Final validation of remaining blocks
    printf("\nFinal validation of %d remaining blocks...\n", active_count);

    int errors = 0;
    for (int i = 0; i < active_count; i++) {
        uint64_t cs = simple_checksum(blocks[i].ptr, blocks[i].requested_size);
        if (cs != blocks[i].checksum) {
            fprintf(stderr, "[FAIL] Corruption in final check! idx=%d ptr=%p size=%zu\n",
                    i, blocks[i].ptr, blocks[i].requested_size);
            errors++;
        }
        mem_free(alloc, blocks[i].ptr);
    }

    if (errors == 0) {
        printf("All remaining blocks are intact ✓\n");
    } else {
        printf("Found %d corrupted blocks in final check!\n", errors);
    }

    mem_show(alloc, "After stress test and final free");

cleanup:
    allocator_destroy(alloc);
    printf("\nTest finished.\n");

    return (errors > 0) ? 1 : 0;
}