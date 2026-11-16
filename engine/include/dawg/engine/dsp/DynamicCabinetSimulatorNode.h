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

enum class CabinetModel : std::uint8_t
{
    vintage4x12 = 0, // Classic UK 4x12 closed-back cabinet (V30 / Greenback punch & presence)
    openBack2x12 = 1, // Vintage American 2x12 open-back combo (warm chime, scooped mids)
    modern1x12 = 2,  // Boutique studio 1x12 ported cabinet (tight bass, focused midrange)
    bass4x10 = 3     // Punchy 4x10 bass cab with extended low end and tight transient response
};

// Dynamic Convolution & Non-Linear Transformer Cabinet Simulator
// Modeled after Two Notes Torpedo, Kemper Profiling, and vintage tube power amp/transformer interactions.
// Features dynamic multi-impulse convolution with speaker excursion morphing,
// reactive impedance curve (speaker mechanical resonance & voice-coil inductive rise),
// power amp tube sag compression, and microphone positioning (Distance & Axis).
class DynamicCabinetSimulatorNode final : public ProcessorNode
{
public:
    static constexpr std::size_t irLength = 64;

    DynamicCabinetSimulatorNode() noexcept;
    ~DynamicCabinetSimulatorNode() override = default;

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void reset() noexcept override;
    [[nodiscard]] std::uint32_t latencySamples() const noexcept override { return 0; }
    void process(ProcessContext& context) noexcept override;

    // Cabinet Model Selection
    void setCabinetModel(CabinetModel model) noexcept;
    [[nodiscard]] CabinetModel cabinetModel() const noexcept { return m_model; }

    // Microphone Positioning (0.0 to 1.0)
    // Distance: 0.0 = close mic (heavy proximity bass), 1.0 = distant room mic
    void setMicrophoneDistance(float distance) noexcept;
    [[nodiscard]] float microphoneDistance() const noexcept { return m_micDistance; }

    // Axis: 0.0 = center / on-axis (maximum bite & brightness), 1.0 = cap edge / off-axis (warm & smooth)
    void setMicrophoneAxis(float axis) noexcept;
    [[nodiscard]] float microphoneAxis() const noexcept { return m_micAxis; }

    // Power Amp Tube Sag (0.0 to 1.0): Power supply voltage sag and spongy dynamic bloom
    void setTubeSag(float sag) noexcept { m_tubeSag = std::clamp(sag, 0.0f, 1.0f); }
    [[nodiscard]] float tubeSag() const noexcept { return m_tubeSag; }

    // Speaker Cone Breakup & Non-linear excursion saturation (0.0 to 1.0)
    void setSpeakerBreakup(float breakup) noexcept { m_speakerBreakup = std::clamp(breakup, 0.0f, 1.0f); }
    [[nodiscard]] float speakerBreakup() const noexcept { return m_speakerBreakup; }

    // Reactive Impedance Curve: Mechanical resonance thump around 80 Hz (-6 to +12 dB)
    void setImpedanceResonanceAmount(float db) noexcept;
    [[nodiscard]] float impedanceResonanceAmount() const noexcept { return m_resonanceDb; }

    // Reactive Impedance Curve: Voice-coil inductive rise above 3 kHz (-6 to +12 dB)
    void setImpedanceInductiveAmount(float db) noexcept;
    [[nodiscard]] float impedanceInductiveAmount() const noexcept { return m_inductiveDb; }

    // Output Level (-24.0 dB to +12.0 dB)
    void setOutputLevelDb(float db) noexcept;
    [[nodiscard]] float outputLevelDb() const noexcept { return m_outputLevelDb; }

    // Wet / Dry Mix (0.0 to 1.0)
    void setMix(float mix) noexcept { m_mix = std::clamp(mix, 0.0f, 1.0f); }
    [[nodiscard]] float mix() const noexcept { return m_mix; }

    // Real-Time Telemetry
    [[nodiscard]] float currentTubeSag() const noexcept { return m_currentSag; }
    [[nodiscard]] float currentConeExcursion() const noexcept { return m_currentExcursion; }

    struct Biquad
    {
        float b0 {1.0f};
        float b1 {0.0f};
        float b2 {0.0f};
        float a1 {0.0f};
        float a2 {0.0f};
        float z1 {0.0f};
        float z2 {0.0f};

        inline float process(float in) noexcept
        {
            const float out = b0 * in + z1;
            z1 = b1 * in - a1 * out + z2;
            z2 = b2 * in - a2 * out;
            return out;
        }

        inline void reset() noexcept
        {
            z1 = 0.0f;
            z2 = 0.0f;
        }
    };

private:
    void recomputeFilters() noexcept;
    void rebuildImpulseResponses() noexcept;

    double m_sampleRate {48000.0};
    CabinetModel m_model {CabinetModel::vintage4x12};
    float m_micDistance {0.15f};
    float m_micAxis {0.20f};
    float m_tubeSag {0.35f};
    float m_speakerBreakup {0.25f};
    float m_resonanceDb {3.5f};
    float m_inductiveDb {4.0f};
    float m_outputLevelDb {0.0f};
    float m_outputGainLinear {1.0f};
    float m_mix {1.0f};

    // Telemetry
    float m_currentSag {0.0f};
    float m_currentExcursion {0.0f};

    // Power amp sag envelope ballistics
    float m_sagEnvelope {0.0f};
    float m_sagAttackCoeff {0.05f};
    float m_sagReleaseCoeff {0.005f};

    // Reactive Impedance filters (stereo)
    // Low resonance bell (~80 Hz)
    Biquad m_resFilterL;
    Biquad m_resFilterR;
    // Inductive high shelf (~3.2 kHz)
    Biquad m_indFilterL;
    Biquad m_indFilterR;

    // Mic distance & off-axis filters (stereo)
    Biquad m_micFilterL;
    Biquad m_micFilterR;

    // Direct FIR Convolution buffers
    std::array<float, irLength> m_activeIrClean {};
    std::array<float, irLength> m_activeIrDriven {};

    std::array<float, irLength> m_firHistoryL {};
    std::array<float, irLength> m_firHistoryR {};
    std::size_t m_firWritePos {0};
};

} // namespace dawg::engine::dsp
