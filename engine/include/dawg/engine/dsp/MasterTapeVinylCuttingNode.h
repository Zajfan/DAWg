#pragma once

#include <dawg/engine/AudioBufferView.h>
#include <dawg/engine/ProcessorNode.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace dawg::engine::dsp {

enum class EllipticalCrossoverFreq : std::uint8_t
{
    hz70,
    hz150,
    hz300
};

// True Multi-Bus Master Tape / Vinyl Cutting Suite (Neumann VMS / Studer style)
// Features vinyl disc cutting lathe simulation (Elliptical Equalizer low-mono & HF acceleration limiter),
// analog tape flanging with through-zero phasing (TZF) & tape drag slowdown, and parallel saturation bus.
class MasterTapeVinylCuttingNode final : public ProcessorNode
{
public:
    static constexpr std::size_t maxDelayBuffer = 2048;

    MasterTapeVinylCuttingNode() noexcept;
    ~MasterTapeVinylCuttingNode() override = default;

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void reset() noexcept override;
    [[nodiscard]] std::uint32_t latencySamples() const noexcept override { return 0; }
    void process(ProcessContext& context) noexcept override;

    // Vinyl Disc Cutting Lathe Simulation
    void setVinylLatheEnabled(bool enabled) noexcept { m_vinylLatheEnabled = enabled; }
    [[nodiscard]] bool isVinylLatheEnabled() const noexcept { return m_vinylLatheEnabled; }

    void setEllipticalCrossover(EllipticalCrossoverFreq freq) noexcept;
    [[nodiscard]] EllipticalCrossoverFreq ellipticalCrossover() const noexcept { return m_eeqFreq; }

    void setHfAccelerationThreshold(float threshDb) noexcept { m_hfAccelThreshDb = std::clamp(threshDb, -24.0f, 0.0f); }
    [[nodiscard]] float hfAccelerationThreshold() const noexcept { return m_hfAccelThreshDb; }

    // Analog Tape Flanging & Through-Zero Phasing (TZF)
    void setTapeFlangeEnabled(bool enabled) noexcept { m_flangeEnabled = enabled; }
    [[nodiscard]] bool isTapeFlangeEnabled() const noexcept { return m_flangeEnabled; }

    void setThroughZeroDepth(float depth) noexcept { m_tzfDepth = std::clamp(depth, 0.0f, 1.0f); }
    [[nodiscard]] float throughZeroDepth() const noexcept { return m_tzfDepth; }

    void setFlangeRateHz(float rate) noexcept { m_flangeRateHz = std::clamp(rate, 0.05f, 10.0f); }
    [[nodiscard]] float flangeRateHz() const noexcept { return m_flangeRateHz; }

    void setTapeDragSlowdown(float drag) noexcept { m_tapeDrag = std::clamp(drag, 0.0f, 1.0f); }
    [[nodiscard]] float tapeDragSlowdown() const noexcept { return m_tapeDrag; }

    // Parallel Saturation Bus
    void setTapeBiasAir(float amount) noexcept { m_tapeBiasAir = std::clamp(amount, 0.0f, 1.0f); }
    [[nodiscard]] float tapeBiasAir() const noexcept { return m_tapeBiasAir; }

    void setTransformerSaturation(float sat) noexcept { m_transformerSat = std::clamp(sat, 0.0f, 1.0f); }
    [[nodiscard]] float transformerSaturation() const noexcept { return m_transformerSat; }

    // Mix Faders
    void setDryMix(float mix) noexcept { m_dryMix = std::clamp(mix, 0.0f, 1.0f); }
    [[nodiscard]] float dryMix() const noexcept { return m_dryMix; }

    void setVinylMix(float mix) noexcept { m_vinylMix = std::clamp(mix, 0.0f, 1.0f); }
    [[nodiscard]] float vinylMix() const noexcept { return m_vinylMix; }

    void setTapeFlangeMix(float mix) noexcept { m_flangeMix = std::clamp(mix, 0.0f, 1.0f); }
    [[nodiscard]] float tapeFlangeMix() const noexcept { return m_flangeMix; }

private:
    void updateFilters() noexcept;

    double m_sampleRate {48000.0};

    // Vinyl Lathe Parameters
    bool m_vinylLatheEnabled {true};
    EllipticalCrossoverFreq m_eeqFreq {EllipticalCrossoverFreq::hz150};
    float m_eeqCutoffHz {150.0f};
    float m_hfAccelThreshDb {-12.0f};

    // Biquad 2nd-order Butterworth Highpass for Elliptical Equalizer Side channel
    float m_eeqB0 {1.0f}, m_eeqB1 {-2.0f}, m_eeqB2 {1.0f};
    float m_eeqA1 {0.0f}, m_eeqA2 {0.0f};
    float m_eeqZ1 {0.0f}, m_eeqZ2 {0.0f};

    // HF Acceleration Limiter filter & detector
    float m_hfDeriPrevL {0.0f}, m_hfDeriPrevR {0.0f};
    float m_hfGainL {1.0f}, m_hfGainR {1.0f};

    // Tape Flanger & TZF
    bool m_flangeEnabled {false};
    float m_tzfDepth {0.8f};
    float m_flangeRateHz {0.25f};
    float m_tapeDrag {0.0f};
    float m_flangeLfoPhase {0.0f};

    std::array<float, maxDelayBuffer> m_flangeRingL {};
    std::array<float, maxDelayBuffer> m_flangeRingR {};
    std::size_t m_flangeWritePos {0};

    // Saturation Bus
    float m_tapeBiasAir {0.4f};
    float m_transformerSat {0.35f};
    float m_preEmphasisL {0.0f}, m_preEmphasisR {0.0f};
    float m_deEmphasisL {0.0f}, m_deEmphasisR {0.0f};

    // Mix faders
    float m_dryMix {1.0f};
    float m_vinylMix {0.9f};
    float m_flangeMix {0.0f};
};

} // namespace dawg::engine::dsp
