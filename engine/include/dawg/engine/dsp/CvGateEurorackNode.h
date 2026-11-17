#pragma once

#include <dawg/engine/ProcessorNode.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace dawg::engine::dsp {

enum class CvOutputMode : std::uint8_t
{
    pitch1VPerOct,
    gateHighLow,
    triggerPulse,
    clock24PPQN,
    lfoModulation
};

enum class CvLfoShape : std::uint8_t
{
    sine,
    triangle,
    rampUp,
    rampDown,
    square,
    sampleAndHold
};

struct CvChannelConfig
{
    CvOutputMode mode {CvOutputMode::pitch1VPerOct};
    float maxVolts {5.0f};           // Peak voltage range: 5.0V or 10.0V
    float portamentoMs {0.0f};       // Pitch CV glide / slew rate
    float lfoRateHz {2.0f};          // LFO modulation frequency
    CvLfoShape lfoShape {CvLfoShape::triangle};
    float lfoDepth {1.0f};           // 0.0 .. 1.0 modulation depth
};

// Real-Time DC-Coupled CV/Gate Eurorack Node (Zero heap allocations in process())
class CvGateEurorackNode final : public ProcessorNode
{
public:
    static constexpr std::size_t maxCvChannels = 4;
    static constexpr std::size_t calibrationPoints = 128;

    CvGateEurorackNode() noexcept;
    ~CvGateEurorackNode() override = default;

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void process(ProcessContext& context) noexcept override;
    void reset() noexcept override;

    [[nodiscard]] std::string_view name() const noexcept { return "Eurorack CV/Gate Bridge"; }

    // Channel configuration
    void setChannelConfig(std::size_t channelIdx, const CvChannelConfig& config) noexcept;
    [[nodiscard]] CvChannelConfig getChannelConfig(std::size_t channelIdx) const noexcept;

    // Real-time MIDI input events
    void noteOn(std::uint8_t note, std::uint8_t velocity) noexcept;
    void noteOff(std::uint8_t note) noexcept;
    void setPitchBend(float semitones) noexcept; // -12.0 .. +12.0

    // Clock sync
    void tickClock() noexcept;

    // Calibration table: maps MIDI note [0..127] to calibrated voltage multiplier
    void setCalibrationOffset(std::uint8_t note, float voltageOffset) noexcept;
    [[nodiscard]] float getCalibrationOffset(std::uint8_t note) const noexcept;

private:
    double m_sampleRate {48000.0};
    std::array<CvChannelConfig, maxCvChannels> m_channels {};

    // Pitch & Portamento state
    std::atomic<std::uint8_t> m_currentNote {60}; // Middle C
    std::atomic<bool> m_gateActive {false};
    std::atomic<float> m_pitchBendSemitones {0.0f};
    float m_targetPitchVoltage {0.0f};
    float m_currentPitchVoltage {0.0f};

    // Trigger & Clock state
    std::uint32_t m_triggerCountdownSamples {0};
    std::uint32_t m_clockPulseSamples {0};

    // LFO phase
    double m_lfoPhase {0.0};
    float m_lastSahValue {0.0f};

    // Calibration lookup
    std::array<std::atomic<float>, calibrationPoints> m_calibrationOffsets {};
};

} // namespace dawg::engine::dsp
