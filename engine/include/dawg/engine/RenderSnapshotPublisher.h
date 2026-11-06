#pragma once

#include <dawg/engine/RenderSnapshot.h>

#include <array>
#include <atomic>
#include <cstdint>

namespace dawg::engine {

// Single message-thread producer and single audio-thread consumer. Three slots
// have distinct owners: producer spare, published handoff, and audio current.
// Each call is bounded, allocation-free and lock-free. Intermediate edits may
// coalesce; the caller may retry a rejected publication on a later message tick.
// Construct/destroy only while both threads are stopped; never reset live slots.
class RenderSnapshotPublisher final
{
public:
    [[nodiscard]] bool publish(RenderSnapshot snapshot) noexcept;
    // Call once at the start of each audio block. Returns an owned value.
    [[nodiscard]] RenderSnapshot consume() noexcept;

private:
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
    static constexpr std::uint32_t pending = 4;
    static constexpr std::uint32_t indexMask = 3;

    std::array<RenderSnapshot, 3> m_slots {};
    // The pending bit distinguishes a new publication from the retired audio
    // slot. Exchanging ownership prevents a load/announce reclamation race.
    std::atomic<std::uint32_t> m_publishedIndex {1};
    std::atomic<std::uint32_t> m_audioCurrentIndex {0};
    std::uint32_t m_writeIndex {2}; // Accessed by the producer only.
};

} // namespace dawg::engine
