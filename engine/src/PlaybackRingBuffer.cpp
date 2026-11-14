#include <dawg/engine/PlaybackRingBuffer.h>

#include <algorithm>
#include <limits>

namespace dawg::engine {

bool PlaybackRingBuffer::prepare(const std::size_t channelCount,
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

void PlaybackRingBuffer::reset() noexcept
{
    m_anchored.store(false, std::memory_order_release);
    m_readFrame.store(0, std::memory_order_relaxed);
    m_writeFrame.store(0, std::memory_order_relaxed);
    m_requestedFrame.store(0, std::memory_order_release);
}

void PlaybackRingBuffer::requestSourceFrame(const std::uint64_t sourceFrame) noexcept
{
    m_requestedFrame.store(sourceFrame, std::memory_order_release);
}

std::uint64_t PlaybackRingBuffer::requestedSourceFrame() const noexcept
{
    return m_requestedFrame.load(std::memory_order_acquire);
}

std::size_t PlaybackRingBuffer::write(const std::uint64_t sourceFrame,
                                      const std::span<const float* const> source,
                                      const std::size_t frameCount) noexcept
{
    if (m_channelCount == 0 || m_capacityFrames == 0 || frameCount == 0
        || source.size() < m_channelCount)
        return 0;

    for (std::size_t channel = 0; channel < m_channelCount; ++channel)
    {
        if (source[channel] == nullptr)
            return 0;
    }

    const bool wasAnchored = m_anchored.load(std::memory_order_acquire);
    const std::uint64_t readFrame =
        wasAnchored ? m_readFrame.load(std::memory_order_acquire) : sourceFrame;
    const std::uint64_t writeFrame =
        wasAnchored ? m_writeFrame.load(std::memory_order_relaxed) : sourceFrame;

    if (wasAnchored && sourceFrame != writeFrame)
        return 0;

    const auto buffered = static_cast<std::size_t>(writeFrame - readFrame);
    if (buffered >= m_capacityFrames)
        return 0;

    const auto freeCapacity = m_capacityFrames - buffered;
    const auto maxBeforeOverflow = std::numeric_limits<std::uint64_t>::max() - writeFrame;
    const auto accepted = static_cast<std::size_t>(std::min<std::uint64_t>(
        std::min(frameCount, freeCapacity), maxBeforeOverflow));
    if (accepted == 0)
        return 0;

    for (std::size_t channel = 0; channel < m_channelCount; ++channel)
    {
        float* const plane = m_samples.data() + channel * m_capacityFrames;
        const float* const src = source[channel];
        for (std::size_t index = 0; index < accepted; ++index)
        {
            const auto slot = static_cast<std::size_t>((writeFrame + index) % m_capacityFrames);
            plane[slot] = src[index];
        }
    }

    if (! wasAnchored)
        m_readFrame.store(sourceFrame, std::memory_order_relaxed);
    m_writeFrame.store(writeFrame + accepted, std::memory_order_release);
    if (! wasAnchored)
        m_anchored.store(true, std::memory_order_release);

    return accepted;
}

std::size_t PlaybackRingBuffer::read(const std::uint64_t sourceFrame,
                                     const std::span<float* const> destination,
                                     const std::size_t frameCount) noexcept
{
    if (m_channelCount == 0 || m_capacityFrames == 0 || frameCount == 0
        || destination.size() < m_channelCount)
        return 0;

    for (std::size_t channel = 0; channel < m_channelCount; ++channel)
    {
        if (destination[channel] == nullptr)
            return 0;
    }

    if (! m_anchored.load(std::memory_order_acquire))
        return 0;

    const auto writeFrame = m_writeFrame.load(std::memory_order_acquire);
    const auto readFrame = m_readFrame.load(std::memory_order_relaxed);
    if (sourceFrame < readFrame || sourceFrame >= writeFrame)
        return 0;

    const auto available = static_cast<std::size_t>(writeFrame - sourceFrame);
    const auto accepted = std::min(frameCount, available);

    for (std::size_t channel = 0; channel < m_channelCount; ++channel)
    {
        const float* const plane = m_samples.data() + channel * m_capacityFrames;
        float* const dst = destination[channel];
        for (std::size_t index = 0; index < accepted; ++index)
        {
            const auto slot = static_cast<std::size_t>((sourceFrame + index) % m_capacityFrames);
            dst[index] = plane[slot];
        }
    }

    m_readFrame.store(sourceFrame + accepted, std::memory_order_release);
    return accepted;
}

std::size_t PlaybackRingBuffer::availableFrames() const noexcept
{
    if (! m_anchored.load(std::memory_order_acquire))
        return 0;

    const auto writeFrame = m_writeFrame.load(std::memory_order_acquire);
    const auto readFrame = m_readFrame.load(std::memory_order_acquire);
    return writeFrame >= readFrame ? static_cast<std::size_t>(writeFrame - readFrame) : 0;
}

} // namespace dawg::engine
