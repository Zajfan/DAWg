#pragma once

#include <dawg/engine/AudioBufferView.h>
#include <dawg/engine/ProcessorNode.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace dawg::engine::dsp {

enum class NamAmpProfile : std::uint8_t
{
    highGainMetal,       // Modern 5150 / Rectifier style high-gain saturation
    vintageBritishPlexi, // 1968 Marshall Plexi / JCM800 touch-sensitive crunch
    classicTweedClean,   // 1959 American Tweed clean bell chime & warm tube sag
    legendaryOverdrive   // Iconic TS9 / Klon mid-hump diode clipping pedal
};

// Real-Time Neural Amp Modeler (NAM) & Dynamic Power-Amp Sag Engine
// Executes lightweight neural network inference (WaveNet / Conv1D residual architecture)
// with analog power-supply capacitor sag and dynamic tube grid bias shifting.
// Strict zero-allocation execution inside real-time audio callback budgets (< 0.5 ms).
class NeuralAmpModelerNode final : public ProcessorNode
{
public:
    static constexpr std::size_t hiddenChannels = 8;
    static constexpr std::size_t numLayers = 4;
    static constexpr std::size_t maxDilation = 8;

    NeuralAmpModelerNode() noexcept;
    ~NeuralAmpModelerNode() override = default;

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void reset() noexcept override;
    [[nodiscard]] std::uint32_t latencySamples() const noexcept override { return 0; }
    void process(ProcessContext& context) noexcept override;

    // Profile Selection
    void loadProfile(NamAmpProfile profile) noexcept;
    [[nodiscard]] NamAmpProfile currentProfile() const noexcept { return m_profile; }

    // Gain & Drive Controls
    void setInputGainDb(float gainDb) noexcept { m_inputGainDb = std::clamp(gainDb, -24.0f, 36.0f); updateGains(); }
    [[nodiscard]] float inputGainDb() const noexcept { return m_inputGainDb; }

    void setMasterVolumeDb(float volumeDb) noexcept { m_masterVolumeDb = std::clamp(volumeDb, -36.0f, 12.0f); updateGains(); }
    [[nodiscard]] float masterVolumeDb() const noexcept { return m_masterVolumeDb; }

    // Dynamic Analog Tube Physics Controls
    void setPowerSagAmount(float amount) noexcept { m_sagAmount = std::clamp(amount, 0.0f, 1.0f); }
    [[nodiscard]] float powerSagAmount() const noexcept { return m_sagAmount; }

    void setDynamicBiasShift(float bias) noexcept { m_dynamicBias = std::clamp(bias, 0.0f, 1.0f); }
    [[nodiscard]] float dynamicBiasShift() const noexcept { return m_dynamicBias; }

    // 3-Band Tone Stack + Presence
    void setBass(float bass) noexcept { m_bass = std::clamp(bass, 0.0f, 2.0f); }
    [[nodiscard]] float bass() const noexcept { return m_bass; }

    void setMiddle(float mid) noexcept { m_middle = std::clamp(mid, 0.0f, 2.0f); }
    [[nodiscard]] float middle() const noexcept { return m_middle; }

    void setTreble(float treble) noexcept { m_treble = std::clamp(treble, 0.0f, 2.0f); }
    [[nodiscard]] float treble() const noexcept { return m_treble; }

    void setPresence(float presence) noexcept { m_presence = std::clamp(presence, 0.0f, 2.0f); }
    [[nodiscard]] float presence() const noexcept { return m_presence; }

    // Real-time telemetry for UI metering
    [[nodiscard]] float currentSagVoltage() const noexcept { return m_sagVoltage; }

private:
    struct Conv1DBlock
    {
        std::size_t dilation {1};
        // Weights: hiddenChannels * hiddenChannels
        std::array<float, hiddenChannels * hiddenChannels> kernelWeight {};
        std::array<float, hiddenChannels> bias {};
        // History buffer for dilated convolution (dual mono)
        std::array<std::array<float, maxDilation * 2>, hiddenChannels> historyL {};
        std::array<std::array<float, maxDilation * 2>, hiddenChannels> historyR {};
        std::size_t writePos {0};

        void reset() noexcept;
    };

    void setupProfileWeights() noexcept;
    void updateGains() noexcept;

    double m_sampleRate {48000.0};
    NamAmpProfile m_profile {NamAmpProfile::vintageBritishPlexi};

    float m_inputGainDb {12.0f};
    float m_masterVolumeDb {0.0f};
    float m_inputLinear {3.98f};
    float m_masterLinear {1.0f};

    // Analog power-amp sag & dynamic bias
    float m_sagAmount {0.6f};
    float m_dynamicBias {0.5f};
    float m_sagVoltage {1.0f};
    float m_gridBiasL {0.0f};
    float m_gridBiasR {0.0f};

    // 4-Band Tone Stack
    float m_bass {1.0f};
    float m_middle {1.0f};
    float m_treble {1.0f};
    float m_presence {1.0f};

    // Tone Stack filter states
    float m_toneLpL {0.0f};
    float m_toneLpR {0.0f};
    float m_toneHpL {0.0f};
    float m_toneHpR {0.0f};
    float m_tonePresL {0.0f};
    float m_tonePresR {0.0f};

    // Neural Network Conv1D Residual Layers
    std::array<float, hiddenChannels> m_inputProjWeights {};
    std::array<Conv1DBlock, numLayers> m_layers {};
    std::array<float, hiddenChannels> m_outputHeadWeights {};
};

} // namespace dawg::engine::dsp
