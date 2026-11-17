#pragma once

#include <dawg/engine/AudioBufferView.h>
#include <dawg/engine/ProcessorNode.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dawg::engine::dsp {

struct PhaseAlignmentAnalysis
{
    float optimalDelaySamples {0.0f};      // Sub-sample precision delay
    float optimalDelayMs {0.0f};           // Delay in milliseconds
    bool recommendedPolarityInvert {false}; // 180-degree polarity inversion recommended
    float correlationBefore {0.0f};        // Normalized Pearson correlation (-1.0 to +1.0) before
    float correlationAfter {0.0f};         // Normalized Pearson correlation (-1.0 to +1.0) after
    float alignmentScorePercent {0.0f};    // Phase coherence score (0% to 100%)
};

// Multi-Track Sub-Sample Phase Alignment & Polarity Optimization Processor
// Features sub-sample Lagrange fractional delay lines, polarity inversion,
// normalized cross-correlation acoustic analysis, and phase-coherence metering.
class MultiTrackPhaseAlignerNode final : public ProcessorNode
{
public:
    static constexpr std::size_t kMaxDelaySamples = 4800; // 100 ms at 48kHz

    MultiTrackPhaseAlignerNode() noexcept;
    ~MultiTrackPhaseAlignerNode() override = default;

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void reset() noexcept override;
    [[nodiscard]] std::uint32_t latencySamples() const noexcept override { return 0; }
    void process(ProcessContext& context) noexcept override;

    // Sub-sample delay settings (0.0 to 4800.0 samples)
    void setDelaySamplesL(float samples) noexcept;
    [[nodiscard]] float delaySamplesL() const noexcept { return m_delaySamplesL; }

    void setDelaySamplesR(float samples) noexcept;
    [[nodiscard]] float delaySamplesR() const noexcept { return m_delaySamplesR; }

    // Polarity Invert
    void setPolarityInvertL(bool invert) noexcept { m_polarityInvertL = invert; }
    [[nodiscard]] bool polarityInvertL() const noexcept { return m_polarityInvertL; }

    void setPolarityInvertR(bool invert) noexcept { m_polarityInvertR = invert; }
    [[nodiscard]] bool polarityInvertR() const noexcept { return m_polarityInvertR; }

    // Bypass
    void setEnabled(bool enabled) noexcept { m_enabled = enabled; }
    [[nodiscard]] bool isEnabled() const noexcept { return m_enabled; }

    // Static Cross-Correlation & Phase Optimization Analysis
    [[nodiscard]] static PhaseAlignmentAnalysis analyzeAlignment(
        const float* refChannel,
        const float* targetChannel,
        std::size_t sampleCount,
        double sampleRate,
        std::size_t maxDelaySearchSamples = 2400) noexcept;

    // Pearson Correlation Coefficient (-1.0 anti-phase to +1.0 in-phase)
    [[nodiscard]] static float computeCorrelationCoefficient(
        const float* chA,
        const float* chB,
        std::size_t count) noexcept;

private:
    [[nodiscard]] inline float readFractionalDelay(
        const std::array<float, kMaxDelaySamples>& buffer,
        std::size_t writeIdx,
        float delaySamples) const noexcept
    {
        const float totalDelay = std::clamp(delaySamples, 0.0f, static_cast<float>(kMaxDelaySamples - 5));
        const auto intDelay = static_cast<std::size_t>(totalDelay);
        const float mu = totalDelay - static_cast<float>(intDelay); // 0.0 <= mu < 1.0

        // 4-point 3rd-order Lagrange interpolation
        const float c0 = -mu * (mu - 1.0f) * (mu - 2.0f) / 6.0f;
        const float c1 = (mu + 1.0f) * (mu - 1.0f) * (mu - 2.0f) * 0.5f;
        const float c2 = -(mu + 1.0f) * mu * (mu - 2.0f) * 0.5f;
        const float c3 = (mu + 1.0f) * mu * (mu - 1.0f) / 6.0f;

        const auto getSample = [&](std::size_t offset) noexcept -> float {
            const std::size_t idx = (writeIdx + kMaxDelaySamples - offset) % kMaxDelaySamples;
            return buffer[idx];
        };

        const float s0 = getSample(intDelay);
        const float s1 = getSample(intDelay + 1);
        const float s2 = getSample(intDelay + 2);
        const float s3 = getSample(intDelay + 3);

        return c0 * s0 + c1 * s1 + c2 * s2 + c3 * s3;
    }

    double m_sampleRate {48000.0};
    bool m_enabled {true};

    float m_delaySamplesL {0.0f};
    float m_delaySamplesR {0.0f};
    bool m_polarityInvertL {false};
    bool m_polarityInvertR {false};

    std::array<float, kMaxDelaySamples> m_delayLineL {};
    std::array<float, kMaxDelaySamples> m_delayLineR {};
    std::size_t m_writeIdx {0};
};

} // namespace dawg::engine::dsp
