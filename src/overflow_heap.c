/*
 * PS5 RetroArch - an overflow heap in direct memory, for when the title's own
 * allocators refuse.
 *
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Two budgets are small on this console. SceLibcInternal's private heap, which
 * serves every allocation under 1 MiB, runs out long before the process does:
 * FBNeo's catalogue exhausted it (docs/PHASE_LOG.md), and PPSSPP's symbol map
 * made it refuse a 168-byte operator new with 178 MB of flexible memory still
 * free. And flexible memory itself, which anonymous mmap draws on for the large
 * allocations, is about 450 MB for the whole title. Direct memory is a separate
 * pool of several GiB (sceKernelGetDirectMemorySize).
 *
 * So src/memory_ps5.cpp keeps its routes and falls back to this heap when one
 * of them refuses: dlmalloc 2.8.6 (tooling/third_party/dlmalloc, MIT) built as a
 * single locked mspace whose segments are direct memory, mapped CPU read-write
 * outside the GPU's 4 GiB window at high word 2 (../PS5_Vulkan maps GPU memory
 * there and needs all of it). A pointer is this heap's when it lies in one of
 * its segments, which is how free and realloc route it back.
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

int64_t sceKernelGetDirectMemorySize(void);
int32_t sceKernelAllocateDirectMemory(int64_t search_start, int64_t search_end, size_t bytes,
                                      size_t alignment, int type, int64_t *start);
int32_t sceKernelMapDirectMemory(void **address, size_t bytes, int protection, int flags,
                                 int64_t start, size_t alignment);
int32_t sceKernelMunmap(void *address, size_t bytes);
int32_t sceKernelReleaseDirectMemory(int64_t start, size_t bytes);

#define OVERFLOW_PAGE 0x4000u
/* The driver's direct-memory type; CPU read and write only. */
#define OVERFLOW_MEMORY_TYPE 12
#define OVERFLOW_PROTECTION 0x03
/* Where segments are asked to go: above the GPU window (0x2_0000_0000 -
 * 0x2_ffff_ffff) and below libkernel's modules at 0x8_0000_0000. */
#define OVERFLOW_ADDRESS_HINT UINT64_C(0x400000000)
#define OVERFLOW_MAX_SEGMENTS 256

struct overflow_segment
{
    uintptr_t base;
    size_t bytes;
    int64_t start;
};

static struct overflow_segment segments[OVERFLOW_MAX_SEGMENTS];
/* Written under dlmalloc's lock; read without it by ps5_overflow_owns, which only
 * needs the segments that existed before the pointer it is asked about. */
static volatile unsigned segment_count;

static void *overflow_map(size_t bytes)
{
    bytes = (bytes + OVERFLOW_PAGE - 1) & ~(size_t)(OVERFLOW_PAGE - 1);
    unsigned slot = 0;
    while (slot < segment_count && segments[slot].bytes != 0)
        slot++;
    if (slot >= OVERFLOW_MAX_SEGMENTS)
        return (void *)-1;
    int64_t start = -1;
    if (sceKernelAllocateDirectMemory(0, sceKernelGetDirectMemorySize(), bytes, OVERFLOW_PAGE,
                                      OVERFLOW_MEMORY_TYPE, &start) != 0)
        return (void *)-1;
    void *address = (void *)(uintptr_t)OVERFLOW_ADDRESS_HINT;
    if (sceKernelMapDirectMemory(&address, bytes, OVERFLOW_PROTECTION, 0, start, OVERFLOW_PAGE) !=
            0 ||
        address == NULL)
    {
        sceKernelReleaseDirectMemory(start, bytes);
        return (void *)-1;
    }
    segments[slot].base = (uintptr_t)address;
    segments[slot].start = start;
    __atomic_store_n(&segments[slot].bytes, bytes, __ATOMIC_RELEASE);
    if (slot == segment_count)
        __atomic_store_n(&segment_count, slot + 1, __ATOMIC_RELEASE);
    return address;
}

/* Only whole segments go back: dlmalloc treats a refused partial trim as a no-op. */
static int overflow_unmap(void *address, size_t bytes)
{
    for (unsigned i = 0; i < segment_count; i++)
    {
        if (segments[i].base == (uintptr_t)address && segments[i].bytes == bytes)
        {
            __atomic_store_n(&segments[i].bytes, 0, __ATOMIC_RELEASE);
            sceKernelMunmap(address, bytes);
            sceKernelReleaseDirectMemory(segments[i].start, bytes);
            return 0;
        }
    }
    return -1;
}

#define ONLY_MSPACES 1
#define USE_LOCKS 1
#define HAVE_MORECORE 0
#define HAVE_MMAP 1
#define HAVE_MREMAP 0
#define MMAP(s) overflow_map(s)
#define DIRECT_MMAP(s) overflow_map(s)
#define MUNMAP(a, s) overflow_unmap((a), (s))
/* The PS5 target's __STDCPP_DEFAULT_NEW_ALIGNMENT__ and __BIGGEST_ALIGNMENT__ are
 * 32: its compilers emit 32-byte-aligned AVX stores into plain new objects
 * (PPSSPP's ThreadManager::Init took a SIGBUS on 16-byte alignment). */
#define MALLOC_ALIGNMENT ((size_t)32)
#define DEFAULT_GRANULARITY ((size_t)64 * 1024 * 1024)
#define DEFAULT_MMAP_THRESHOLD ((size_t)32 * 1024 * 1024)
#define MALLOC_FAILURE_ACTION errno = ENOMEM
#define NO_MALLINFO 1
#define NO_MALLOC_STATS 1
/* The console's page, so the sizes dlmalloc maps and unmaps are the ones recorded. */
#define malloc_getpagesize ((size_t)OVERFLOW_PAGE)
#include "../tooling/third_party/dlmalloc/malloc.c"

static mspace heap;
static volatile int heap_state; /* 0 none, 1 creating, 2 ready, 3 failed */

static mspace overflow_heap(void)
{
    int expected = 0;
    if (__atomic_compare_exchange_n(&heap_state, &expected, 1, 0, __ATOMIC_ACQ_REL,
                                    __ATOMIC_ACQUIRE))
    {
        heap = create_mspace(0, 1);
        __atomic_store_n(&heap_state, heap ? 2 : 3, __ATOMIC_RELEASE);
    }
    while (__atomic_load_n(&heap_state, __ATOMIC_ACQUIRE) == 1)
        ;
    return heap_state == 2 ? heap : NULL;
}

int ps5_overflow_owns(const void *pointer)
{
    const uintptr_t address = (uintptr_t)pointer;
    const unsigned count = __atomic_load_n(&segment_count, __ATOMIC_ACQUIRE);
    for (unsigned i = 0; i < count; i++)
    {
        const size_t bytes = __atomic_load_n(&segments[i].bytes, __ATOMIC_ACQUIRE);
        if (bytes != 0 && address - segments[i].base < bytes)
            return 1;
    }
    return 0;
}

void *ps5_overflow_malloc(size_t size)
{
    mspace space = overflow_heap();
    return space ? mspace_malloc(space, size ? size : 1) : NULL;
}

void *ps5_overflow_calloc(size_t count, size_t size)
{
    mspace space = overflow_heap();
    return space ? mspace_calloc(space, count ? count : 1, size ? size : 1) : NULL;
}

void *ps5_overflow_memalign(size_t alignment, size_t size)
{
    mspace space = overflow_heap();
    return space ? mspace_memalign(space, alignment, size ? size : 1) : NULL;
}

void *ps5_overflow_realloc(void *pointer, size_t size)
{
    return mspace_realloc(overflow_heap(), pointer, size);
}

size_t ps5_overflow_usable_size(const void *pointer)
{
    return mspace_usable_size(pointer);
}

void ps5_overflow_free(void *pointer)
{
    mspace_free(overflow_heap(), pointer);
}
