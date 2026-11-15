#pragma once

#include <dawg/engine/AudioBufferView.h>
#include <dawg/engine/ProcessorNode.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace dawg::engine::dsp {

enum class ResonanceProcessingMode : std::uint8_t
{
    stereo,
    midOnly,   // Focuses de-essing on center vocal channel, leaving wide stereo cymbals bright
    sideOnly
};

// Dynamic Multi-Band De-Esser & Spectral Resonance Tamer (Soothe2 / Pro-DS style)
// Features adaptive spectral resonance suppression, tracking lookahead dynamic notch array,
// Mid/Side surgical center-vocal focus, and Delta audition mode.
class SpectralResonanceTamerNode final : public ProcessorNode
{
public:
    static constexpr std::size_t maxNotches = 8;
    static constexpr std::size_t analysisBands = 24;
    static constexpr std::size_t maxLookahead = 256;

    SpectralResonanceTamerNode() noexcept;
    ~SpectralResonanceTamerNode() override = default;

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void reset() noexcept override;
    [[nodiscard]] std::uint32_t latencySamples() const noexcept override { return static_cast<std::uint32_t>(m_lookaheadSamples); }
    void process(ProcessContext& context) noexcept override;

    // Suppression Parameters
    void setDepth(float depth) noexcept { m_depth = std::clamp(depth, 0.0f, 1.0f); }
    [[nodiscard]] float depth() const noexcept { return m_depth; }

    void setSharpness(float sharpness) noexcept { m_sharpness = std::clamp(sharpness, 0.5f, 4.0f); }
    [[nodiscard]] float sharpness() const noexcept { return m_sharpness; }

    void setThresholdDb(float threshDb) noexcept { m_thresholdDb = std::clamp(threshDb, -40.0f, 0.0f); }
    [[nodiscard]] float thresholdDb() const noexcept { return m_thresholdDb; }

    void setFrequencyRange(float lowHz, float highHz) noexcept;
    [[nodiscard]] float lowFreqHz() const noexcept { return m_lowFreqHz; }
    [[nodiscard]] float highFreqHz() const noexcept { return m_highFreqHz; }

    void setBallistics(float attackMs, float releaseMs) noexcept;

    // Mid/Side & Audition Modes
    void setProcessingMode(ResonanceProcessingMode mode) noexcept { m_processingMode = mode; }
    [[nodiscard]] ResonanceProcessingMode processingMode() const noexcept { return m_processingMode; }

    void setDeltaListen(bool delta) noexcept { m_deltaListen = delta; }
    [[nodiscard]] bool isDeltaListen() const noexcept { return m_deltaListen; }

    // Telemetry: current total dynamic suppression in dB
    [[nodiscard]] float currentSuppressionDb() const noexcept { return m_activeSuppressionDb; }

private:
    struct TrackingNotch
    {
        float freqHz {3000.0f};
        float gainLinear {1.0f};
        float targetGainLinear {1.0f};
        float q {8.0f};

        // Biquad notch filter coefficients & states
        float b0 {1.0f}, b1 {0.0f}, b2 {0.0f};
        float a1 {0.0f}, a2 {0.0f};
        float z1L {0.0f}, z2L {0.0f};
        float z1R {0.0f}, z2R {0.0f};

        void updateCoeffs(double sampleRate) noexcept;
        float processL(float in) noexcept;
        float processR(float in) noexcept;
        void reset() noexcept;
    };

    void updateBandFrequencies() noexcept;

    double m_sampleRate {48000.0};
    std::size_t m_lookaheadSamples {64};

    float m_depth {0.5f};
    float m_sharpness {1.5f};
    float m_thresholdDb {-20.0f};
    float m_lowFreqHz {1200.0f};
    float m_highFreqHz {12000.0f};

    float m_attackCoeff {0.05f};
    float m_releaseCoeff {0.002f};

    ResonanceProcessingMode m_processingMode {ResonanceProcessingMode::midOnly};
    bool m_deltaListen {false};
    float m_activeSuppressionDb {0.0f};

    // Lookahead ring buffers
    std::array<float, maxLookahead> m_lookaheadRingL {};
    std::array<float, maxLookahead> m_lookaheadRingR {};
    std::size_t m_lookaheadWritePos {0};

    // Analysis bandpass bank
    struct AnalysisBandpass
    {
        float b0 {0.05f}, b1 {0.0f}, b2 {-0.05f};
        float a1 {0.0f}, a2 {0.0f};
        float z1 {0.0f}, z2 {0.0f};

        inline float process(float in) noexcept
        {
            const float out = b0 * in + z1;
            z1 = b1 * in - a1 * out + z2;
            z2 = b2 * in - a2 * out;
            return out;
        }

        void reset() noexcept { z1 = 0.0f; z2 = 0.0f; }
    };

    std::array<float, analysisBands> m_bandFreqs {};
    std::array<float, analysisBands> m_bandEnergies {};
    std::array<AnalysisBandpass, analysisBands> m_analysisFilters {};

    // Dynamic Notch Array
    std::array<TrackingNotch, maxNotches> m_notches {};
};

} // namespace dawg::engine::dsp
