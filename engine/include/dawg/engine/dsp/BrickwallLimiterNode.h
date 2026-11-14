#pragma once

#include <dawg/engine/ProcessorNode.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace dawg::engine::dsp {

struct LimiterSettings
{
    bool bypassed {false};
    float ceilingDb {-0.1f};             // -6.0 dBFS to 0.0 dBFS
    float releaseMs {60.0f};             // 10 ms to 500 ms
    std::uint32_t lookaheadSamples {64}; // ~1.33 ms at 48 kHz

    friend bool operator==(const LimiterSettings&, const LimiterSettings&) = default;
};

class BrickwallLimiterNode final : public ProcessorNode
{
public:
    explicit BrickwallLimiterNode(LimiterSettings settings = {}) noexcept;

    void setSettings(const LimiterSettings& settings) noexcept;
    [[nodiscard]] const LimiterSettings& settings() const noexcept { return m_settings; }

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void reset() noexcept override;

    [[nodiscard]] std::uint32_t latencySamples() const noexcept override
    {
        return m_settings.bypassed ? 0 : m_settings.lookaheadSamples;
    }

    void process(ProcessContext& context) noexcept override;

private:
    void updateParameters() noexcept;

    LimiterSettings m_settings;
    double m_sampleRate {48000.0};
    std::uint16_t m_channelCount {0};
    float m_ceilingLinear {0.988553f}; // -0.1 dB
    float m_releaseCoeff {0.0f};
    float m_currentGain {1.0f};

    // Circular delay buffers for lookahead: channel -> ring buffer
    std::size_t m_writeIndex {0};
    std::vector<float> m_delayStorage;
};

} // namespace dawg::engine::dsp
