// "The latest request per slot wins", for work done off the calling thread.
//
// platform::Audio posts each PlayStream to a loader thread, since reading a
// 3 MB .ogg on the frame thread froze the Vita for up to 1.3 s. A newer request
// or a cancel on the same slot bumps that slot's generation, so an older
// request is skipped when taken, and a load already in flight is seen to be
// stale (IsCurrent) before it is installed.
//
// Not thread-safe by itself: whoever holds it locks around every call.
#pragma once

#include <cstdint>
#include <deque>
#include <utility>
#include <vector>

namespace core {

template <typename Payload>
class SlotRequests {
public:
    struct Request {
        int slot = -1;
        uint32_t generation = 0;
        Payload payload{};
    };

    explicit SlotRequests(int slots) : generations_(static_cast<std::size_t>(slots > 0 ? slots : 0), 0u) {}

    void Post(int slot, Payload payload) {
        if (!Valid(slot)) return;
        const uint32_t g = ++generations_[static_cast<std::size_t>(slot)];
        queue_.push_back(Request{slot, g, std::move(payload)});
    }

    // Makes every queued or in-flight request on `slot` stale.
    void Cancel(int slot) {
        if (Valid(slot)) ++generations_[static_cast<std::size_t>(slot)];
    }

    // The oldest request still current; stale ones are dropped on the way.
    bool Take(Request& out) {
        while (!queue_.empty()) {
            Request r = std::move(queue_.front());
            queue_.pop_front();
            if (IsCurrent(r)) {
                out = std::move(r);
                return true;
            }
        }
        return false;
    }

    bool IsCurrent(const Request& r) const {
        return Valid(r.slot) && generations_[static_cast<std::size_t>(r.slot)] == r.generation;
    }

    bool Empty() const { return queue_.empty(); }

private:
    bool Valid(int slot) const { return slot >= 0 && static_cast<std::size_t>(slot) < generations_.size(); }

    std::vector<uint32_t> generations_;
    std::deque<Request> queue_;
};

}  // namespace core
