#include "TransportCommandQueue.h"

namespace dawg::engine {

bool TransportCommandQueue::enqueue(const EngineCommand& command) noexcept
{
    const auto write = m_writePosition.load(std::memory_order_relaxed);
    const auto read = m_readPosition.load(std::memory_order_acquire);
    if (write - read == capacity)
        return false;

    m_commands[write % capacity] = command;
    // Publish the complete command before allowing the consumer to read it.
    m_writePosition.store(write + 1, std::memory_order_release);
    return true;
}

bool TransportCommandQueue::dequeue(EngineCommand& command) noexcept
{
    const auto read = m_readPosition.load(std::memory_order_relaxed);
    const auto write = m_writePosition.load(std::memory_order_acquire);
    if (read == write)
        return false;

    command = m_commands[read % capacity];
    // Finish reading the slot before allowing the producer to reuse it.
    m_readPosition.store(read + 1, std::memory_order_release);
    return true;
}

} // namespace dawg::engine
