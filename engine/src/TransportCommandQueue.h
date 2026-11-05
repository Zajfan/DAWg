#pragma once

#include <dawg/engine/EngineCommand.h>

#include <array>
#include <atomic>
#include <cstddef>

namespace dawg::engine {

// One message-thread producer and one audio-thread consumer. Each operation
// makes one bounded attempt; all 64 slots are usable. Never reset while active.
class TransportCommandQueue final
{
public:
    static constexpr std::size_t capacity = 64;

    [[nodiscard]] bool enqueue(const EngineCommand& command) noexcept;
    [[nodiscard]] bool dequeue(EngineCommand& command) noexcept;

private:
    static_assert(std::atomic<std::size_t>::is_always_lock_free);

    std::array<EngineCommand, capacity> m_commands {};
    // Unsigned monotonic counters distinguish full from empty without a spare
    // slot. Their wrap modulus is divisible by the power-of-two capacity.
    std::atomic<std::size_t> m_writePosition {};
    std::atomic<std::size_t> m_readPosition {};
};

} // namespace dawg::engine
