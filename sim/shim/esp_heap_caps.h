#pragma once
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
// No PSRAM / internal RAM split in the browser: every capability is malloc
#define MALLOC_CAP_EXEC (1 << 0)
#define MALLOC_CAP_32BIT (1 << 1)
#define MALLOC_CAP_8BIT (1 << 2)
#define MALLOC_CAP_DMA (1 << 3)
#define MALLOC_CAP_SPIRAM (1 << 10)
#define MALLOC_CAP_INTERNAL (1 << 11)
#define MALLOC_CAP_DEFAULT (1 << 12)
static inline void* heap_caps_malloc(size_t n, uint32_t caps) { (void)caps; return malloc(n); }
static inline void* heap_caps_calloc(size_t c, size_t n, uint32_t caps) { (void)caps; return calloc(c, n); }
static inline void* heap_caps_realloc(void* p, size_t n, uint32_t caps) { (void)caps; return realloc(p, n); }
static inline void* heap_caps_aligned_alloc(size_t a, size_t n, uint32_t caps) { (void)caps; return aligned_alloc(a, (n + a - 1) / a * a); }
static inline void heap_caps_free(void* p) { free(p); }
static inline size_t heap_caps_get_free_size(uint32_t caps) { (void)caps; return 64u * 1024 * 1024; }
static inline size_t heap_caps_get_largest_free_block(uint32_t caps) { (void)caps; return 32u * 1024 * 1024; }
