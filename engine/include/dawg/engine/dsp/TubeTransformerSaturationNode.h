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

enum class TubeStageType : std::uint8_t
{
    triode12AX7 = 0,
    pentodeEL34,
    consoleOpAmp2520,
    cleanAnalog
};

enum class TransformerCoreType : std::uint8_t
{
    steelCore = 0, // Rich 3rd harmonic, low-end beef
    nickelCore,    // Ultra-clean, subtle high headroom
    bypass
};

class TubeTransformerSaturationNode final : public ProcessorNode
{
public:
    TubeTransformerSaturationNode() noexcept;
    ~TubeTransformerSaturationNode() override = default;

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void process(ProcessContext& context) noexcept override;
    void reset() noexcept override;

    [[nodiscard]] std::string_view name() const noexcept { return "Vintage Tube & Transformer Saturation Rack"; }

    // Tube Stage Settings
    void setTubeType(TubeStageType type) noexcept { m_tubeType = type; }
    [[nodiscard]] TubeStageType tubeType() const noexcept { return m_tubeType; }
    void setDriveDb(float driveDb) noexcept;
    [[nodiscard]] float driveDb() const noexcept { return m_driveDb; }
    void setTubeBias(float bias) noexcept { m_tubeBias = std::clamp(bias, 0.0f, 1.0f); }
    [[nodiscard]] float tubeBias() const noexcept { return m_tubeBias; }
    void setPowerSag(float sagAmount) noexcept { m_powerSagAmount = std::clamp(sagAmount, 0.0f, 1.0f); }
    [[nodiscard]] float powerSag() const noexcept { return m_powerSagAmount; }

    // Transformer Settings
    void setTransformerCore(TransformerCoreType core) noexcept { m_transformerCore = core; }
    [[nodiscard]] TransformerCoreType transformerCore() const noexcept { return m_transformerCore; }
    void setTransformerDrive(float drive) noexcept { m_transformerDrive = std::clamp(drive, 0.0f, 2.0f); }
    [[nodiscard]] float transformerDrive() const noexcept { return m_transformerDrive; }
    void setLowEndWarmth(float warmth) noexcept { m_lowEndWarmth = std::clamp(warmth, 0.0f, 1.0f); }
    [[nodiscard]] float lowEndWarmth() const noexcept { return m_lowEndWarmth; }

    // Global Mix & Output
    void setDryWetMix(float mixRatio) noexcept { m_dryWetMix = std::clamp(mixRatio, 0.0f, 1.0f); }
    [[nodiscard]] float dryWetMix() const noexcept { return m_dryWetMix; }
    void setOutputTrimDb(float trimDb) noexcept;
    [[nodiscard]] float outputTrimDb() const noexcept { return m_outputTrimDb; }

private:
    float processTubeNonlinearity(float in, float driveLin, float bias, float sag) noexcept;
    float processTransformer(float in, float& histZ, float& phaseZ, TransformerCoreType core, float drive) noexcept;

    double m_sampleRate {48000.0};
    std::uint16_t m_channelCount {2};

    TubeStageType m_tubeType {TubeStageType::triode12AX7};
    float m_driveDb {6.0f};
    float m_driveLinear {1.995f};
    float m_tubeBias {0.35f};
    float m_powerSagAmount {0.4f};
    float m_sagEnvelopeL {0.0f};
    float m_sagEnvelopeR {0.0f};

    TransformerCoreType m_transformerCore {TransformerCoreType::steelCore};
    float m_transformerDrive {0.8f};
    float m_lowEndWarmth {0.5f};
    float m_transHysteresisZL {0.0f};
    float m_transHysteresisZR {0.0f};
    float m_transPhaseZL {0.0f};
    float m_transPhaseZR {0.0f};

    float m_dryWetMix {1.0f};
    float m_outputTrimDb {0.0f};
    float m_outputTrimLinear {1.0f};
};

} // namespace dawg::engine::dsp
