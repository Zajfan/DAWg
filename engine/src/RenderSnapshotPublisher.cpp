#include <dawg/engine/RenderSnapshotPublisher.h>

namespace dawg::engine {

bool RenderSnapshotPublisher::publish(const RenderSnapshot snapshot) noexcept
{
    // The spare is never the handoff slot. It can briefly match the announced
    // audio index while audio finishes its ownership exchange; do not wait.
    if (m_writeIndex == m_audioCurrentIndex.load(std::memory_order_acquire))
        return false;

    m_slots[m_writeIndex] = snapshot;
    // Release the completed payload and acquire the retired slot before reuse.
    m_writeIndex = m_publishedIndex.exchange(m_writeIndex | pending,
                                            std::memory_order_acq_rel) & indexMask;
    return true;
}

RenderSnapshot RenderSnapshotPublisher::consume() noexcept
{
    auto current = m_audioCurrentIndex.load(std::memory_order_relaxed);
    if ((m_publishedIndex.load(std::memory_order_acquire) & pending) != 0)
    {
        // Atomically claim the latest publication and retire the previous
        // audio slot. A producer may replace the handoff before this exchange,
        // but can never reclaim the slot returned by the exchange to audio.
        current = m_publishedIndex.exchange(current, std::memory_order_acq_rel) & indexMask;
        m_audioCurrentIndex.store(current, std::memory_order_release);
    }
    return m_slots[current];
}

} // namespace dawg::engine
