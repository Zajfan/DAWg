#pragma once

#include <dawg/engine/AudioBufferView.h>
#include <dawg/engine/ProcessorNode.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace dawg::engine::dsp {

enum class EqFilterType : std::uint8_t
{
    lowCut,
    lowShelf,
    bell,
    highShelf,
    highCut,
    notch
};

enum class EqPhaseMode : std::uint8_t
{
    minimumPhase, // Zero latency, classic analog minimum phase biquads
    linearPhase   // Zero phase distortion, constant group delay, clean transients
};

struct DynamicEqBandSettings
{
    bool enabled {true};
    EqFilterType type {EqFilterType::bell};
    float frequencyHz {1000.0f}; // 20 Hz to 20 kHz
    float gainDb {0.0f};          // -24.0 dB to +24.0 dB
    float q {0.707f};             // 0.1 to 18.0

    // Dynamic EQ parameters
    bool dynamicEnabled {false};
    float thresholdDb {-18.0f};   // -60 dB to 0 dB
    float rangeDb {0.0f};         // -18 dB (compression) to +18 dB (expansion)
    float attackMs {10.0f};       // 0.5 ms to 200 ms
    float releaseMs {80.0f};      // 10 ms to 1000 ms
};

// Linear-Phase Dynamic Mastering EQ with Perceived Loudness Auto-Gain (FabFilter Pro-Q 3 style)
// Features switchable minimum-phase vs linear-phase processing, dynamic compression/expansion
// on every band, proportional-Q filter morphing, and EBU R128 auto-gain compensation.
class DynamicLinearPhaseEQNode final : public ProcessorNode
{
public:
    static constexpr std::size_t numBands = 6;
    static constexpr std::size_t firLength = 256;
    static constexpr std::size_t firLatency = firLength / 2;

    DynamicLinearPhaseEQNode() noexcept;
    ~DynamicLinearPhaseEQNode() override = default;

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void reset() noexcept override;
    [[nodiscard]] std::uint32_t latencySamples() const noexcept override
    {
        return (m_phaseMode == EqPhaseMode::linearPhase) ? static_cast<std::uint32_t>(firLatency) : 0;
    }
    void process(ProcessContext& context) noexcept override;

    // Phase Mode
    void setPhaseMode(EqPhaseMode mode) noexcept { m_phaseMode = mode; }
    [[nodiscard]] EqPhaseMode phaseMode() const noexcept { return m_phaseMode; }

    // Band Settings
    void setBandSettings(std::size_t bandIdx, const DynamicEqBandSettings& settings) noexcept;
    [[nodiscard]] const DynamicEqBandSettings& bandSettings(std::size_t bandIdx) const noexcept;

    // Auto-Gain Compensation
    void setAutoGain(bool enabled) noexcept { m_autoGainEnabled = enabled; updateAutoGainEstimate(); }
    [[nodiscard]] bool isAutoGain() const noexcept { return m_autoGainEnabled; }
    [[nodiscard]] float currentAutoGainDb() const noexcept { return m_currentAutoGainDb; }

    // Dynamic Gain Reduction / Expansion Telemetry (in dB)
    [[nodiscard]] float dynamicGainShiftDb(std::size_t bandIdx) const noexcept;

private:
    struct BiquadCoeffs
    {
        float b0 {1.0f};
        float b1 {0.0f};
        float b2 {0.0f};
        float a1 {0.0f};
        float a2 {0.0f};
    };

    struct BiquadState
    {
        float z1 {0.0f};
        float z2 {0.0f};

        inline float process(float in, const BiquadCoeffs& c) noexcept
        {
            float out = c.b0 * in + z1;
            z1 = c.b1 * in - c.a1 * out + z2;
            z2 = c.b2 * in - c.a2 * out;
            return out;
        }

        void reset() noexcept
        {
            z1 = 0.0f;
            z2 = 0.0f;
        }
    };

    struct BandRuntime
    {
        DynamicEqBandSettings settings {};
        BiquadCoeffs coeffs {};
        BiquadState stateL {};
        BiquadState stateR {};

        // Detector state
        float detectorEnvL {0.0f};
        float detectorEnvR {0.0f};
        float dynamicGainShift {0.0f};
        float attackCoeff {0.1f};
        float releaseCoeff {0.01f};
    };

    void calculateBiquadCoeffs(std::size_t bandIdx, float effectiveGainDb) noexcept;
    void updateAutoGainEstimate() noexcept;

    double m_sampleRate {48000.0};
    EqPhaseMode m_phaseMode {EqPhaseMode::minimumPhase};
    bool m_autoGainEnabled {false};
    float m_currentAutoGainDb {0.0f};
    float m_autoGainLinear {1.0f};

    std::array<BandRuntime, numBands> m_bands {};

    // Linear-phase FIR history buffer and delay line
    std::array<float, firLength> m_firHistoryL {};
    std::array<float, firLength> m_firHistoryR {};
    std::size_t m_firWritePos {0};
};

} // namespace dawg::engine::dsp
