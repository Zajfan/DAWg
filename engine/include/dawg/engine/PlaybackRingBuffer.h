#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace dawg::engine {

class PlaybackRingBuffer final
{
public:
    [[nodiscard]] bool prepare(std::size_t channelCount, std::size_t capacityFrames) noexcept;
    void reset() noexcept;
    void requestSourceFrame(std::uint64_t sourceFrame) noexcept;
    [[nodiscard]] std::uint64_t requestedSourceFrame() const noexcept;
    [[nodiscard]] std::size_t write(std::uint64_t sourceFrame,
                                    std::span<const float* const> source,
                                    std::size_t frameCount) noexcept;
    [[nodiscard]] std::size_t read(std::uint64_t sourceFrame,
                                   std::span<float* const> destination,
                                   std::size_t frameCount) noexcept;
    [[nodiscard]] std::size_t availableFrames() const noexcept;

private:
    std::vector<float> m_samples;
    std::size_t m_channelCount {};
    std::size_t m_capacityFrames {};
    std::atomic<std::uint64_t> m_anchorFrame {0};
    std::atomic<std::uint64_t> m_readFrame {0};
    std::atomic<std::uint64_t> m_writeFrame {0};
    std::atomic<std::uint64_t> m_requestedFrame {0};
    std::atomic<bool> m_anchored {false};
};

} // namespace dawg::engine
