#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace dawg::engine {

// Wait-free SPSC planar float ring buffer for realtime audio input capture.
// - Audio thread calls write() (never blocks, never allocates).
// - Disk-writer worker calls read().
class RecordRingBuffer final
{
public:
    [[nodiscard]] bool prepare(std::size_t channelCount, std::size_t capacityFrames) noexcept;
    void reset() noexcept;

    [[nodiscard]] std::size_t write(std::span<const float* const> input,
                                    std::size_t frameCount) noexcept;
    [[nodiscard]] std::size_t read(std::span<float* const> destination,
                                   std::size_t maxFrames) noexcept;

    [[nodiscard]] std::size_t channelCount() const noexcept { return m_channelCount; }
    [[nodiscard]] std::size_t capacityFrames() const noexcept { return m_capacityFrames; }
    [[nodiscard]] std::size_t availableFrames() const noexcept;
    [[nodiscard]] std::uint64_t droppedFrames() const noexcept;
    [[nodiscard]] std::uint64_t totalWrittenFrames() const noexcept;

private:
    std::vector<float> m_samples;
    std::size_t m_channelCount {};
    std::size_t m_capacityFrames {};
    std::atomic<std::uint64_t> m_readIndex {0};
    std::atomic<std::uint64_t> m_writeIndex {0};
    std::atomic<std::uint64_t> m_droppedFrames {0};
};

} // namespace dawg::engine
