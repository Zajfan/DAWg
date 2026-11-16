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

enum class DrumType : std::uint8_t
{
    snare,
    kick,
    tom,
    custom
};

// Multi-Mic Acoustic Drum Bleed & Spill Cancellation Engine (Slate Trigger / Silencer / Oxford Drum Gate style)
// Features frequency-selective transient spill gating, microphone phase alignment with auto-correlation,
// and dynamic velocity-tracked sample augmentation.
class AcousticDrumBleedCancellationNode final : public ProcessorNode
{
public:
    static constexpr std::size_t MaxLookaheadSamples = 256; // ~5.3 ms at 48 kHz
    static constexpr std::size_t MaxSampleLength = 48000;   // 1 second trigger sample

    AcousticDrumBleedCancellationNode() noexcept;
    ~AcousticDrumBleedCancellationNode() override = default;

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void reset() noexcept override;
    [[nodiscard]] std::uint32_t latencySamples() const noexcept override { return static_cast<std::uint32_t>(m_lookaheadSamples); }
    void process(ProcessContext& context) noexcept override;

    // Drum Profile & Gate Tuning
    void setDrumType(DrumType type) noexcept;
    [[nodiscard]] DrumType drumType() const noexcept { return m_drumType; }

    void setSpillReductionDb(float db) noexcept { m_spillReductionDb = std::clamp(db, -60.0f, 0.0f); }
    [[nodiscard]] float spillReductionDb() const noexcept { return m_spillReductionDb; }

    void setGateThresholdDb(float db) noexcept { m_thresholdDb = std::clamp(db, -60.0f, 0.0f); }
    [[nodiscard]] float gateThresholdDb() const noexcept { return m_thresholdDb; }

    void setLookaheadSamples(std::size_t samples) noexcept { m_lookaheadSamples = std::clamp(samples, std::size_t{0}, MaxLookaheadSamples); }
    [[nodiscard]] std::size_t lookaheadSamples() const noexcept { return m_lookaheadSamples; }

    // Microphone Phase Alignment Matrix
    void setPhaseAlignmentDelayMs(float ms) noexcept { m_phaseDelayMs = std::clamp(ms, -20.0f, 20.0f); }
    [[nodiscard]] float phaseAlignmentDelayMs() const noexcept { return m_phaseDelayMs; }

    void setPolarityInverted(bool inverted) noexcept { m_polarityInverted = inverted; }
    [[nodiscard]] bool isPolarityInverted() const noexcept { return m_polarityInverted; }

    [[nodiscard]] float calculatePhaseCorrelation(const float* trackA, const float* trackB, std::size_t count) const noexcept;

    // Transient Sample Augmentation
    void setSampleBlend(float blend) noexcept { m_sampleBlend = std::clamp(blend, 0.0f, 1.0f); }
    [[nodiscard]] float sampleBlend() const noexcept { return m_sampleBlend; }

    void loadAugmentationSample(const float* samples, std::size_t count);

    // Telemetry: current gate attenuation in dB
    [[nodiscard]] float currentGateAttenuationDb() const noexcept { return m_gateGainDb; }
    [[nodiscard]] bool isGateOpen() const noexcept { return m_gateGainDb > -3.0f; }

private:
    void updateFilterCoeffs() noexcept;

    double m_sampleRate {48000.0};
    DrumType m_drumType {DrumType::snare};

    float m_thresholdDb {-24.0f};
    float m_spillReductionDb {-20.0f};
    std::size_t m_lookaheadSamples {64}; // ~1.3 ms lookahead for zero attack truncation

    float m_phaseDelayMs {0.0f};
    bool m_polarityInverted {false};
    float m_sampleBlend {0.0f};

    // Detection bandpass filter frequencies
    float m_detCenterHz {2200.0f};
    float m_detQ {1.5f};
    float m_detB0 {1.0f}, m_detB1 {0.0f}, m_detB2 {0.0f};
    float m_detA1 {0.0f}, m_detA2 {0.0f};
    float m_detZ1 {0.0f}, m_detZ2 {0.0f};

    // Spill suppression high-shelf filter (cuts cymbal bleed when gate is closed)
    float m_shelfFreqHz {4000.0f};
    float m_shelfB0 {1.0f}, m_shelfB1 {0.0f}, m_shelfB2 {0.0f};
    float m_shelfA1 {0.0f}, m_shelfA2 {0.0f};
    float m_shelfZ1L {0.0f}, m_shelfZ2L {0.0f};
    float m_shelfZ1R {0.0f}, m_shelfZ2R {0.0f};

    // Envelope ballistics
    float m_envelope {0.0f};
    float m_gateGainDb {-20.0f};
    float m_attackCoeff {0.1f};
    float m_holdCount {0.0f};
    float m_releaseCoeff {0.005f};

    // Lookahead ring buffer
    std::array<float, MaxLookaheadSamples + 1024> m_lookaheadRingL {};
    std::array<float, MaxLookaheadSamples + 1024> m_lookaheadRingR {};
    std::size_t m_ringWritePos {0};

    // Augmentation Sample Buffer & Trigger
    std::vector<float> m_augmentSample;
    std::size_t m_samplePlayPos {MaxSampleLength};
    float m_sampleVelocity {1.0f};
    bool m_triggerArmed {true};
};

} // namespace dawg::engine::dsp
