// Live and peak bytes behind C++ operator new. The executables' operator
// new/delete feed one ledger (platform/alloc_count.cpp: each block carries its
// size in a header); GameApp logs it around every scene change and every 600
// frames as
//   "heap: <stage>, new live X KB in N blocks (+H KB headers), peak Y KB[; newlib ...]".
// The Vita's newlib heap is a fixed 128 MB block (_init_vita_heap) that
// vglMemFree's "ram" does not show, and running out of it is a std::bad_alloc.
//
// What it does not cover: newlib's own state. C malloc (SDL, stb, stdio),
// malloc's chunk overhead and fragmentation are outside it, so the Vita line
// carries newlib's mallinfo() too (RegisteredMallocReport), and the header's
// own cost is stated (HeaderBytes) because it is paid in the same heap.
#pragma once

#include <atomic>
#include <cstdint>
#include <string>

namespace core {

class HeapLedger {
public:
    constexpr HeapLedger() = default;
    constexpr explicit HeapLedger(std::uint64_t headerBytes) : headerBytes_(headerBytes) {}

    void OnAlloc(std::uint64_t bytes) {
        blocks_.fetch_add(1, std::memory_order_relaxed);
        const std::uint64_t live = live_.fetch_add(bytes, std::memory_order_relaxed) + bytes;
        std::uint64_t peak = peak_.load(std::memory_order_relaxed);
        while (live > peak && !peak_.compare_exchange_weak(peak, live, std::memory_order_relaxed)) {
        }
    }
    void OnFree(std::uint64_t bytes) {
        blocks_.fetch_sub(1, std::memory_order_relaxed);
        live_.fetch_sub(bytes, std::memory_order_relaxed);
    }

    std::uint64_t Live() const { return live_.load(std::memory_order_relaxed); }
    std::uint64_t Peak() const { return peak_.load(std::memory_order_relaxed); }
    std::uint64_t Blocks() const { return blocks_.load(std::memory_order_relaxed); }
    // What the size headers themselves occupy in the heap right now.
    std::uint64_t HeaderBytes() const { return Blocks() * headerBytes_; }
    // The peak restarts at the current live bytes: measure one stretch (a scene change).
    void ResetPeak() { peak_.store(Live(), std::memory_order_relaxed); }

private:
    std::atomic<std::uint64_t> live_{0};
    std::atomic<std::uint64_t> peak_{0};
    std::atomic<std::uint64_t> blocks_{0};
    std::uint64_t headerBytes_ = 0;
};

// The executable's ledger, or nullptr where no counting operator new is linked
// (bsht_tests, the tools). Registered before main by platform/alloc_count.cpp.
inline HeapLedger*& RegisteredHeapLedger() {
    static HeapLedger* ledger = nullptr;
    return ledger;
}

// The C heap's own view ("newlib arena A KB, in use U KB, free F KB" from
// mallinfo()), registered on the Vita only; nullptr elsewhere.
using MallocReport = std::string (*)();
inline MallocReport& RegisteredMallocReport() {
    static MallocReport report = nullptr;
    return report;
}

}  // namespace core
