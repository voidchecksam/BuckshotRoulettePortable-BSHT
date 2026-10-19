// The executable's allocation counter and heap ledger. Every C++ operator new
// is counted (the profiler's line reports allocations per frame, "allocs
// mean/max"), and each block carries its size in a small header, so
// core::HeapLedger knows the live and peak bytes behind every new ("heap: live
// X KB, peak Y KB" around scene changes, desktop and Vita alike).
//
// Linked into the executables only: bsht_tests has its own counting
// operator new (test_stream_resampler.cpp), and a program may hold just one.
// libstdc++'s operator new[] and nothrow new call this operator new, and its
// operator delete[], sized and nothrow deletes call this operator delete, so
// replacing the pair keeps every header matched. Aligned new is untouched: it
// allocates and frees outside this pair (nothing in the game uses it).
#include "core/heap_ledger.h"
#include "core/profiler.h"

#include <atomic>
#include <cstdio>
#include <string>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>

#ifdef __vita__
#include <malloc.h>
#endif

namespace {

std::atomic<uint32_t> g_allocations{0};

// One max_align_t ahead of the caller's bytes: the block stays as aligned as
// malloc's, and the size fits in it on both platforms (8 on the Vita, 16 here).
constexpr std::size_t kHeader = alignof(std::max_align_t);
static_assert(kHeader >= sizeof(std::size_t));

// Constant-initialised (constexpr constructors), so it is ready for the first
// operator new of any static initialiser.
constinit core::HeapLedger g_ledger{kHeader};

#ifdef __vita__
// newlib's own view of its fixed 128 MB heap: what the C++ ledger cannot see
// (C malloc, chunk overhead, free space). mallinfo has no largest-free-block.
std::string NewlibReport() {
    const struct mallinfo m = mallinfo();
    char line[96];
    std::snprintf(line, sizeof line, "newlib arena %lu KB, in use %lu KB, free %lu KB",
                  static_cast<unsigned long>(m.arena / 1024), static_cast<unsigned long>(m.uordblks / 1024),
                  static_cast<unsigned long>(m.fordblks / 1024));
    return line;
}
[[maybe_unused]] const bool g_registeredMalloc = (core::RegisteredMallocReport() = &NewlibReport, true);
#endif

uint32_t Allocations() { return g_allocations.load(std::memory_order_relaxed); }

// Registered before main; GameApp attaches them to its profiler and log.
[[maybe_unused]] const bool g_registered = (core::RegisteredAllocCounter() = &Allocations,
                                            core::RegisteredHeapLedger() = &g_ledger, true);

}  // namespace

void* operator new(std::size_t size) {
    g_allocations.fetch_add(1, std::memory_order_relaxed);
    if (size == 0) size = 1;
    for (;;) {
        if (void* raw = std::malloc(size + kHeader)) {
            *static_cast<std::size_t*>(raw) = size;
            g_ledger.OnAlloc(size);
            return static_cast<unsigned char*>(raw) + kHeader;
        }
        const std::new_handler handler = std::get_new_handler();
        if (!handler) throw std::bad_alloc();
        handler();
    }
}

void operator delete(void* p) noexcept {
    if (!p) return;
    void* raw = static_cast<unsigned char*>(p) - kHeader;
    g_ledger.OnFree(*static_cast<std::size_t*>(raw));
    std::free(raw);
}

void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
