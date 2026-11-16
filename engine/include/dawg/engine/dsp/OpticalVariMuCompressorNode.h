#pragma once

#include <dawg/engine/AudioBufferView.h>
#include <dawg/engine/ProcessorNode.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace dawg::engine::dsp {

enum class CompressorModel : std::uint8_t
{
    opticalT4, // Teletronix LA-2A style dual-stage electro-optical attenuator
    variMu     // Fairchild 670 style variable-mu remote-cutoff tube compressor
};

enum class SidechainFilterMode : std::uint8_t
{
    flat,
    highpass80,
    highpass150,
    thrust // API-style thrust curve (-3 dB/oct low cut, +3 dB/oct high boost)
};

// Optical Mastering Compressor & Vari-Mu Tube Limiter (Fairchild 670 / Teletronix LA-2A style)
// Features a memory-dependent dual-stage T4 electro-optical cell, program-dependent variable-mu
// tube curve with dynamic ratio, and frequency-weighted sidechain thrust filtering.
class OpticalVariMuCompressorNode final : public ProcessorNode
{
public:
    OpticalVariMuCompressorNode() noexcept;
    ~OpticalVariMuCompressorNode() override = default;

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void reset() noexcept override;
    [[nodiscard]] std::uint32_t latencySamples() const noexcept override { return 0; }
    void process(ProcessContext& context) noexcept override;

    // Compressor Model & Controls
    void setModel(CompressorModel model) noexcept { m_model = model; }
    [[nodiscard]] CompressorModel model() const noexcept { return m_model; }

    // Peak Reduction / Drive (0.0 to 1.0)
    void setPeakReduction(float reduction) noexcept { m_peakReduction = std::clamp(reduction, 0.0f, 1.0f); }
    [[nodiscard]] float peakReduction() const noexcept { return m_peakReduction; }

    void setOutputGainDb(float gainDb) noexcept { m_outputGainDb = std::clamp(gainDb, -24.0f, 24.0f); }
    [[nodiscard]] float outputGainDb() const noexcept { return m_outputGainDb; }

    void setSidechainFilter(SidechainFilterMode mode) noexcept { m_sidechainMode = mode; updateSidechainCoeffs(); }
    [[nodiscard]] SidechainFilterMode sidechainFilter() const noexcept { return m_sidechainMode; }

    // Vari-Mu specific settings
    void setVariMuSpeed(float attackMs, float releaseMs) noexcept;
    void setTubeWarmth(float warmth) noexcept { m_tubeWarmth = std::clamp(warmth, 0.0f, 1.0f); }
    [[nodiscard]] float tubeWarmth() const noexcept { return m_tubeWarmth; }

    // Dry/Wet Mix
    void setMix(float mix) noexcept { m_mix = std::clamp(mix, 0.0f, 1.0f); }
    [[nodiscard]] float mix() const noexcept { return m_mix; }

    // Telemetry: current gain reduction in dB
    [[nodiscard]] float currentGainReductionDb() const noexcept { return m_measuredGainReductionDb; }

private:
    void updateSidechainCoeffs() noexcept;
    float processSidechainFilter(float sample, float& z1, float& z2) noexcept;

    double m_sampleRate {48000.0};
    CompressorModel m_model {CompressorModel::opticalT4};
    SidechainFilterMode m_sidechainMode {SidechainFilterMode::highpass80};

    float m_peakReduction {0.5f};
    float m_outputGainDb {0.0f};
    float m_mix {1.0f};
    float m_tubeWarmth {0.35f};

    // T4 Optical Cell State (Luminescent panel & CdS photoresistor with memory lag)
    float m_t4MemoryHistory {0.0f};
    float m_t4FastCell {0.0f};
    float m_t4SlowCell {0.0f};

    // Vari-Mu Tube State
    float m_variMuGridBias {0.0f};
    float m_variMuAttackCoeff {0.1f};
    float m_variMuReleaseCoeff {0.005f};

    // Sidechain filter biquad coefficients
    float m_scB0 {1.0f}, m_scB1 {0.0f}, m_scB2 {0.0f};
    float m_scA1 {0.0f}, m_scA2 {0.0f};
    float m_scZ1L {0.0f}, m_scZ2L {0.0f};
    float m_scZ1R {0.0f}, m_scZ2R {0.0f};

    float m_measuredGainReductionDb {0.0f};
};

} // namespace dawg::engine::dsp
