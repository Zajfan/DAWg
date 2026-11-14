#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace dawg::media {

// Streaming 4-point Catmull-Rom cubic Hermite sample-rate converter.
// Preserves per-channel history and fractional phase across chunk boundaries.
class AudioResampler final
{
public:
    AudioResampler() noexcept = default;

    [[nodiscard]] bool prepare(std::uint32_t sourceSampleRate,
                               std::uint32_t targetSampleRate,
                               std::uint16_t channelCount,
                               std::size_t maxOutputFrames) noexcept;
    void reset() noexcept;

    [[nodiscard]] double ratio() const noexcept { return m_ratio; }
    [[nodiscard]] bool isIdentity() const noexcept
    {
        return m_sourceRate != 0 && m_sourceRate == m_targetRate;
    }

    // Converts up to inputFrames from input into up to maxOutputFrames in output,
    // recording how many input frames were consumed in inputFramesConsumed.
    [[nodiscard]] std::size_t process(std::span<const float* const> input,
                                      std::size_t inputFrames,
                                      std::span<float* const> output,
                                      std::size_t maxOutputFrames,
                                      std::size_t& inputFramesConsumed) noexcept;

private:
    std::uint32_t m_sourceRate {};
    std::uint32_t m_targetRate {};
    std::uint16_t m_channelCount {};
    std::size_t m_maxOutputFrames {};
    double m_ratio {1.0};
    double m_phase {0.0};
    std::vector<std::array<float, 4>> m_history;
};

} // namespace dawg::media
