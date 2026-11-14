#include <dawg/engine/RecordRingBuffer.h>

#include <algorithm>
#include <limits>

namespace dawg::engine {

bool RecordRingBuffer::prepare(const std::size_t channelCount,
                               const std::size_t capacityFrames) noexcept
{
    if (channelCount == 0 || capacityFrames == 0
        || channelCount > std::numeric_limits<std::size_t>::max() / capacityFrames)
    {
        m_samples.clear();
        m_channelCount = 0;
        m_capacityFrames = 0;
        reset();
        return false;
    }

    try
    {
        m_samples.assign(channelCount * capacityFrames, 0.0f);
    }
    catch (...)
    {
        m_samples.clear();
        m_channelCount = 0;
        m_capacityFrames = 0;
        reset();
        return false;
    }

    m_channelCount = channelCount;
    m_capacityFrames = capacityFrames;
    reset();
    return true;
}

void RecordRingBuffer::reset() noexcept
{
    m_readIndex.store(0, std::memory_order_relaxed);
    m_writeIndex.store(0, std::memory_order_relaxed);
    m_droppedFrames.store(0, std::memory_order_release);
}

std::size_t RecordRingBuffer::write(const std::span<const float* const> input,
                                    const std::size_t frameCount) noexcept
{
    if (m_channelCount == 0 || m_capacityFrames == 0 || frameCount == 0
        || input.size() < m_channelCount)
    {
        return 0;
    }

    for (std::size_t ch = 0; ch < m_channelCount; ++ch)
    {
        if (input[ch] == nullptr)
            return 0;
    }

    const auto readIdx = m_readIndex.load(std::memory_order_acquire);
    const auto writeIdx = m_writeIndex.load(std::memory_order_relaxed);
    const auto buffered = static_cast<std::size_t>(writeIdx - readIdx);

    if (buffered >= m_capacityFrames)
    {
        m_droppedFrames.fetch_add(frameCount, std::memory_order_relaxed);
        return 0;
    }

    const auto freeSpace = m_capacityFrames - buffered;
    const auto accepted = std::min(frameCount, freeSpace);
    if (accepted < frameCount)
    {
        m_droppedFrames.fetch_add(frameCount - accepted, std::memory_order_relaxed);
    }

    for (std::size_t ch = 0; ch < m_channelCount; ++ch)
    {
        float* const dstPlane = m_samples.data() + ch * m_capacityFrames;
        const float* const srcPlane = input[ch];
        for (std::size_t i = 0; i < accepted; ++i)
        {
            const auto slot = static_cast<std::size_t>((writeIdx + i) % m_capacityFrames);
            dstPlane[slot] = srcPlane[i];
        }
    }

    m_writeIndex.store(writeIdx + accepted, std::memory_order_release);
    return accepted;
}

std::size_t RecordRingBuffer::read(const std::span<float* const> destination,
                                   const std::size_t maxFrames) noexcept
{
    if (m_channelCount == 0 || m_capacityFrames == 0 || maxFrames == 0
        || destination.size() < m_channelCount)
    {
        return 0;
    }

    for (std::size_t ch = 0; ch < m_channelCount; ++ch)
    {
        if (destination[ch] == nullptr)
            return 0;
    }

    const auto writeIdx = m_writeIndex.load(std::memory_order_acquire);
    const auto readIdx = m_readIndex.load(std::memory_order_relaxed);
    if (writeIdx <= readIdx)
        return 0;

    const auto available = static_cast<std::size_t>(writeIdx - readIdx);
    const auto accepted = std::min(maxFrames, available);

    for (std::size_t ch = 0; ch < m_channelCount; ++ch)
    {
        const float* const srcPlane = m_samples.data() + ch * m_capacityFrames;
        float* const dstPlane = destination[ch];
        for (std::size_t i = 0; i < accepted; ++i)
        {
            const auto slot = static_cast<std::size_t>((readIdx + i) % m_capacityFrames);
            dstPlane[i] = srcPlane[slot];
        }
    }

    m_readIndex.store(readIdx + accepted, std::memory_order_release);
    return accepted;
}

std::size_t RecordRingBuffer::availableFrames() const noexcept
{
    const auto writeIdx = m_writeIndex.load(std::memory_order_acquire);
    const auto readIdx = m_readIndex.load(std::memory_order_acquire);
    return writeIdx >= readIdx ? static_cast<std::size_t>(writeIdx - readIdx) : 0;
}

std::uint64_t RecordRingBuffer::droppedFrames() const noexcept
{
    return m_droppedFrames.load(std::memory_order_acquire);
}

std::uint64_t RecordRingBuffer::totalWrittenFrames() const noexcept
{
    return m_writeIndex.load(std::memory_order_acquire);
}

} // namespace dawg::engine
