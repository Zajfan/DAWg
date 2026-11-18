#pragma once

#include <dawg/engine/AudioBufferView.h>
#include <dawg/engine/ProcessorNode.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace dawg::engine::dsp {

enum class DeEsserMode : std::uint8_t
{
    splitBand = 0,
    wideband,
    auditionDelta // Sidechain listen to attenuated sibilance
};

struct Lr4FilterState
{
    float z1_lp1 {0.0f}, z2_lp1 {0.0f};
    float z1_lp2 {0.0f}, z2_lp2 {0.0f};
    float z1_hp1 {0.0f}, z2_hp1 {0.0f};
    float z1_hp2 {0.0f}, z2_hp2 {0.0f};

    void reset() noexcept
    {
        z1_lp1 = z2_lp1 = z1_lp2 = z2_lp2 = 0.0f;
        z1_hp1 = z2_hp1 = z1_hp2 = z2_hp2 = 0.0f;
    }
};

struct FormantFilterState
{
    float z1 {0.0f};
    float z2 {0.0f};
    void reset() noexcept { z1 = z2 = 0.0f; }
};

class VocalSurgicalDeEsserNode final : public ProcessorNode
{
public:
    static constexpr std::size_t maxLookaheadFrames = 256; // >5 ms at 48 kHz

    VocalSurgicalDeEsserNode() noexcept;
    ~VocalSurgicalDeEsserNode() override = default;

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void process(ProcessContext& context) noexcept override;
    void reset() noexcept override;

    [[nodiscard]] std::string_view name() const noexcept { return "Vocal Surgical De-Esser & Breath Attenuator"; }

    // De-Esser Parameters
    void setMode(DeEsserMode mode) noexcept { m_mode = mode; }
    [[nodiscard]] DeEsserMode mode() const noexcept { return m_mode; }
    void setCrossoverFrequency(float freqHz) noexcept;
    [[nodiscard]] float crossoverFrequency() const noexcept { return m_crossoverFreq; }
    void setThresholdDb(float threshDb) noexcept { m_thresholdDb = std::clamp(threshDb, -50.0f, 0.0f); }
    [[nodiscard]] float thresholdDb() const noexcept { return m_thresholdDb; }
    void setDeEsserRatio(float ratio) noexcept { m_ratio = std::clamp(ratio, 1.0f, 16.0f); }
    [[nodiscard]] float deEsserRatio() const noexcept { return m_ratio; }
    void setLookaheadMs(float ms) noexcept;
    [[nodiscard]] float lookaheadMs() const noexcept { return m_lookaheadMs; }
    [[nodiscard]] float currentGainReductionDb() const noexcept { return m_lastGainReductionDb.load(std::memory_order_relaxed); }

    // Breath Attenuator Parameters
    void setBreathAttenuatorEnabled(bool enabled) noexcept { m_breathAttenEnabled = enabled; }
    [[nodiscard]] bool isBreathAttenuatorEnabled() const noexcept { return m_breathAttenEnabled; }
    void setBreathReductionDb(float reductionDb) noexcept { m_breathReductionDb = std::clamp(reductionDb, -40.0f, 0.0f); }
    [[nodiscard]] float breathReductionDb() const noexcept { return m_breathReductionDb; }
    void setBreathSensitivity(float sens) noexcept { m_breathSensitivity = std::clamp(sens, 0.1f, 1.0f); }
    [[nodiscard]] float breathSensitivity() const noexcept { return m_breathSensitivity; }
    [[nodiscard]] bool isBreathDetected() const noexcept { return m_isBreathDetected.load(std::memory_order_relaxed); }

    // Formant & Throat Resonator
    void setFormantShiftSemitones(float semitones) noexcept;
    [[nodiscard]] float formantShiftSemitones() const noexcept { return m_formantShiftSemitones; }
    void setThroatLengthScale(float scale) noexcept;
    [[nodiscard]] float throatLengthScale() const noexcept { return m_throatLengthScale; }
    void setFormantMix(float mix) noexcept { m_formantMix = std::clamp(mix, 0.0f, 1.0f); }
    [[nodiscard]] float formantMix() const noexcept { return m_formantMix; }

private:
    void updateCrossoverCoeffs() noexcept;
    void updateFormantCoeffs() noexcept;
    void processCrossover(float in, float& low, float& high, Lr4FilterState& state) const noexcept;
    float processFormants(float in, std::array<FormantFilterState, 3>& states) const noexcept;

    double m_sampleRate {48000.0};
    std::uint16_t m_channelCount {2};

    DeEsserMode m_mode {DeEsserMode::splitBand};
    float m_crossoverFreq {6000.0f};
    float m_thresholdDb {-22.0f};
    float m_ratio {4.0f};
    float m_lookaheadMs {2.0f};
    std::size_t m_lookaheadFrames {96};

    std::atomic<float> m_lastGainReductionDb {0.0f};
    float m_envFollower {0.0f};

    // Breath Attenuation
    bool m_breathAttenEnabled {true};
    float m_breathReductionDb {-12.0f};
    float m_breathSensitivity {0.6f};
    std::atomic<bool> m_isBreathDetected {false};
    float m_breathGainSmooth {1.0f};
    float m_zeroCrossRate {0.0f};
    float m_prevSampleL {0.0f};

    // Formant controls
    float m_formantShiftSemitones {0.0f};
    float m_throatLengthScale {1.0f};
    float m_formantMix {0.0f};

    // Delay buffers for lookahead
    std::array<float, maxLookaheadFrames> m_delayBufferL {};
    std::array<float, maxLookaheadFrames> m_delayBufferR {};
    std::array<float, maxLookaheadFrames> m_lowDelayBufferL {};
    std::array<float, maxLookaheadFrames> m_lowDelayBufferR {};
    std::array<float, maxLookaheadFrames> m_highDelayBufferL {};
    std::array<float, maxLookaheadFrames> m_highDelayBufferR {};
    std::size_t m_lookaheadWriteIdx {0};

    // Filter states
    Lr4FilterState m_crossoverStateL;
    Lr4FilterState m_crossoverStateR;

    std::array<FormantFilterState, 3> m_formantStatesL;
    std::array<FormantFilterState, 3> m_formantStatesR;

    // Filter coefficients
    float m_lrB0_lp {0.0f}, m_lrB1_lp {0.0f}, m_lrB2_lp {0.0f};
    float m_lrA1_lp {0.0f}, m_lrA2_lp {0.0f};
    float m_lrB0_hp {0.0f}, m_lrB1_hp {0.0f}, m_lrB2_hp {0.0f};
    float m_lrA1_hp {0.0f}, m_lrA2_hp {0.0f};

    struct FormantBiquad
    {
        float b0 {0.0f}, b1 {0.0f}, b2 {0.0f};
        float a1 {0.0f}, a2 {0.0f};
    };
    std::array<FormantBiquad, 3> m_formantCoeffs;
};

} // namespace dawg::engine::dsp
