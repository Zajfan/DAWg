#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <string_view>

#include "dawg/engine/ProcessorNode.h"

namespace dawg::engine::dsp {

class MultibandStereoImagerNode : public ProcessorNode
{
public:
    static constexpr std::size_t NumBands = 4;

    struct BandSettings
    {
        float width {1.0f};       // 0.0 (mono) to 3.0 (300% widened)
        float midGainDb {0.0f};   // -12.0 to +12.0 dB
        float sideGainDb {0.0f};  // -12.0 to +12.0 dB
        bool solo {false};
        bool bypass {false};
    };

    MultibandStereoImagerNode() noexcept;
    ~MultibandStereoImagerNode() override = default;

    [[nodiscard]] std::string_view name() const noexcept { return "MultibandStereoImager"; }

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;
    void process(ProcessContext& ctx) noexcept override;
    void reset() noexcept override;

    // Crossover frequency configuration
    void setCrossoverFrequencies(float f1, float f2, float f3) noexcept;
    [[nodiscard]] std::array<float, 3> crossoverFrequencies() const noexcept { return m_crossoverFreqs; }

    // Band controls
    void setBandSettings(std::size_t bandIdx, const BandSettings& settings) noexcept;
    [[nodiscard]] const BandSettings& bandSettings(std::size_t bandIdx) const noexcept;

    void setBandWidth(std::size_t bandIdx, float width) noexcept;
    void setBandMidGainDb(std::size_t bandIdx, float db) noexcept;
    void setBandSideGainDb(std::size_t bandIdx, float db) noexcept;
    void setBandSolo(std::size_t bandIdx, bool solo) noexcept;
    void setBandBypass(std::size_t bandIdx, bool bypass) noexcept;

    // Elliptic Mono Bass Filter (< cutoff is strictly collapsed to mono)
    void setMonoBassEnabled(bool enabled) noexcept { m_monoBassEnabled = enabled; }
    [[nodiscard]] bool isMonoBassEnabled() const noexcept { return m_monoBassEnabled; }

    void setMonoBassCutoff(float freqHz) noexcept;
    [[nodiscard]] float monoBassCutoff() const noexcept { return m_monoBassCutoff; }

    // Zero-Comb Haas All-Pass Widener
    void setHaasWidenerEnabled(bool enabled) noexcept { m_haasEnabled = enabled; }
    [[nodiscard]] bool isHaasWidenerEnabled() const noexcept { return m_haasEnabled; }

    void setHaasDelayMs(float ms) noexcept;
    [[nodiscard]] float haasDelayMs() const noexcept { return m_haasDelayMs; }

    // Real-Time Correlation & Balance Radar
    [[nodiscard]] float correlation() const noexcept { return m_smoothedCorrelation; }
    [[nodiscard]] float balance() const noexcept { return m_smoothedBalance; }

private:
    struct BiquadCoeffs
    {
        float b0 {1.0f}, b1 {0.0f}, b2 {0.0f};
        float a1 {0.0f}, a2 {0.0f};
    };

    struct BiquadState
    {
        float z1 {0.0f}, z2 {0.0f};
        void reset() noexcept { z1 = 0.0f; z2 = 0.0f; }
        [[nodiscard]] inline float process(float in, const BiquadCoeffs& c) noexcept
        {
            const float out = c.b0 * in + z1;
            z1 = c.b1 * in - c.a1 * out + z2;
            z2 = c.b2 * in - c.a2 * out;
            return out;
        }
    };

    struct LR4Filter
    {
        BiquadCoeffs stage1;
        BiquadCoeffs stage2;
        BiquadState stateL1, stateL2;
        BiquadState stateR1, stateR2;

        void reset() noexcept
        {
            stateL1.reset();
            stateL2.reset();
            stateR1.reset();
            stateR2.reset();
        }

        inline void processStereo(float inL, float inR, float& outL, float& outR) noexcept
        {
            outL = stateL2.process(stateL1.process(inL, stage1), stage2);
            outR = stateR2.process(stateR1.process(inR, stage1), stage2);
        }
    };

    void calculateButterworthLowpass(float fc, BiquadCoeffs& c) const noexcept;
    void calculateButterworthHighpass(float fc, BiquadCoeffs& c) const noexcept;
    void updateCrossoverFilters() noexcept;
    void updateMonoBassFilter() noexcept;

    float m_sampleRate {44100.0f};
    std::array<float, 3> m_crossoverFreqs {120.0f, 1500.0f, 6000.0f};
    std::array<BandSettings, NumBands> m_bands {};

    // Crossover filter stages
    // 3 split frequencies => 3 lowpass + 3 highpass LR4 pairs
    std::array<LR4Filter, 3> m_lowpassCrossover {};
    std::array<LR4Filter, 3> m_highpassCrossover {};

    // Mono Bass 2nd-order High-Pass on Side Channel
    bool m_monoBassEnabled {true};
    float m_monoBassCutoff {150.0f};
    BiquadCoeffs m_monoBassCoeffs {};
    BiquadState m_monoBassState {};

    // Haas All-pass Widener
    bool m_haasEnabled {false};
    float m_haasDelayMs {2.0f};
    static constexpr std::size_t MaxHaasBuffer = 2048;
    std::array<float, MaxHaasBuffer> m_haasBuffer {};
    std::size_t m_haasWriteIdx {0};
    std::size_t m_haasDelaySamples {88}; // ~2ms @ 44.1kHz

    // Radar ballistics
    float m_smoothedCorrelation {1.0f};
    float m_smoothedBalance {0.0f};
};

} // namespace dawg::engine::dsp
