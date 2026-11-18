#pragma once

#include <dawg/engine/AudioBufferView.h>
#include <dawg/engine/ProcessorNode.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace dawg::engine::dsp {

enum class TransientBand : std::uint8_t
{
    low = 0,   // Below crossover 1 (e.g. 200 Hz)
    mid = 1,   // Crossover 1 to crossover 2 (e.g. 200 Hz .. 4 kHz)
    high = 2   // Above crossover 2 (e.g. 4 kHz)
};

class MultibandTransientDuckingNode final : public ProcessorNode
{
public:
    static constexpr std::size_t numBands = 3;
    static constexpr std::size_t maxChannels = 2;

    MultibandTransientDuckingNode() noexcept;
    ~MultibandTransientDuckingNode() override = default;

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void process(ProcessContext& context) noexcept override;
    void reset() noexcept override;

    [[nodiscard]] std::string_view name() const noexcept { return "Multiband Transient Designer & Spectral Ducking"; }

    // =========================================================================
    // 3-Band Transient Shaping
    // =========================================================================
    void setAttackGainDb(TransientBand band, float gainDb) noexcept;
    [[nodiscard]] float attackGainDb(TransientBand band) const noexcept;

    void setSustainGainDb(TransientBand band, float gainDb) noexcept;
    [[nodiscard]] float sustainGainDb(TransientBand band) const noexcept;

    void setCrossoverFrequencies(float lowMidHz, float midHighHz) noexcept;
    [[nodiscard]] float lowMidCrossoverHz() const noexcept;
    [[nodiscard]] float midHighCrossoverHz() const noexcept;

    // =========================================================================
    // Intelligent Spectral Clashing Ducking (Sidechain-driven band ducking)
    // =========================================================================
    void setSpectralDuckingEnabled(bool enabled) noexcept;
    [[nodiscard]] bool isSpectralDuckingEnabled() const noexcept;

    void setDuckingThresholdDb(float thresholdDb) noexcept;
    [[nodiscard]] float duckingThresholdDb() const noexcept;

    void setDuckingAmountDb(TransientBand band, float duckDb) noexcept;
    [[nodiscard]] float duckingAmountDb(TransientBand band) const noexcept;

    // =========================================================================
    // Sub-Harmonic Kick / 808 Reinforcer
    // =========================================================================
    void setSubHarmonicEnabled(bool enabled) noexcept;
    [[nodiscard]] bool isSubHarmonicEnabled() const noexcept;

    void setSubHarmonicMix(float mix0to1) noexcept;
    [[nodiscard]] float subHarmonicMix() const noexcept;

    struct SimpleBiquad
    {
        float b0 {1.0f}, b1 {0.0f}, b2 {0.0f};
        float a1 {0.0f}, a2 {0.0f};
        float z1 {0.0f}, z2 {0.0f};

        inline float process(float in) noexcept
        {
            const float out = in * b0 + z1;
            z1 = in * b1 - a1 * out + z2;
            z2 = in * b2 - a2 * out;
            return out;
        }

        inline void reset() noexcept
        {
            z1 = 0.0f;
            z2 = 0.0f;
        }
    };

private:
    void updateCrossoverCoefficients() noexcept;

    double m_sampleRate {48000.0};

    std::atomic<float> m_lowMidHz {200.0f};
    std::atomic<float> m_midHighHz {4000.0f};

    std::array<std::atomic<float>, numBands> m_attackGainsDb {};
    std::array<std::atomic<float>, numBands> m_sustainGainsDb {};

    std::atomic<bool> m_duckingEnabled {true};
    std::atomic<float> m_duckingThresholdDb {-18.0f};
    std::array<std::atomic<float>, numBands> m_duckingAmountsDb {};

    std::atomic<bool> m_subHarmonicEnabled {false};
    std::atomic<float> m_subHarmonicMix {0.3f};

    // Filter banks per channel (Lowpass 1, Highpass 1, Lowpass 2, Highpass 2)
    std::array<SimpleBiquad, maxChannels> m_lp1 {};
    std::array<SimpleBiquad, maxChannels> m_hp1 {};
    std::array<SimpleBiquad, maxChannels> m_lp2 {};
    std::array<SimpleBiquad, maxChannels> m_hp2 {};

    // Sidechain filter banks
    std::array<SimpleBiquad, maxChannels> m_scLp1 {};
    std::array<SimpleBiquad, maxChannels> m_scHp1 {};
    std::array<SimpleBiquad, maxChannels> m_scLp2 {};
    std::array<SimpleBiquad, maxChannels> m_scHp2 {};

    // Envelope followers for fast attack (2ms) and slow sustain (40ms)
    std::array<std::array<float, numBands>, maxChannels> m_fastEnv {};
    std::array<std::array<float, numBands>, maxChannels> m_slowEnv {};

    // Sidechain band energy detectors
    std::array<float, numBands> m_sidechainEnergy {};

    // Sub-harmonic divider state
    float m_subPhase {0.0f};
    float m_subPrevSample {0.0f};
    SimpleBiquad m_subLp {};
};

} // namespace dawg::engine::dsp
