#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace dawg::engine {

class WsolaTimeStretcher final
{
public:
    static constexpr std::size_t frameSize = 512;
    static constexpr std::size_t hopSize = 256;
    static constexpr std::size_t maxSearch = 128;

    explicit WsolaTimeStretcher(std::uint16_t channelCount = 2) noexcept;
    ~WsolaTimeStretcher() = default;

    void setStretchRatio(float ratio) noexcept;
    [[nodiscard]] float stretchRatio() const noexcept { return m_stretchRatio; }
    [[nodiscard]] std::uint16_t channelCount() const noexcept { return m_channelCount; }

    void reset() noexcept;

    // Full offline stretch utility for warping audio clips or offline rendering
    [[nodiscard]] static std::vector<std::vector<float>> stretch(
        std::span<const std::span<const float>> inputChannels,
        float stretchRatio) noexcept;

private:
    float m_stretchRatio {1.0f};
    std::uint16_t m_channelCount {2};
};

} // namespace dawg::engine
