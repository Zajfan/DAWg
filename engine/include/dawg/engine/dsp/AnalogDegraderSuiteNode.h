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

enum class DegraderProfile : std::uint8_t
{
    typeI_Ferric = 0,
    typeII_Chrome,
    typeIV_Metal,
    microcassette,
    vhs_HiFi,
    vinyl_33,
    vinyl_45,
    vinyl_78
};

struct BiquadState
{
    float z1 {0.0f};
    float z2 {0.0f};

    void reset() noexcept { z1 = 0.0f; z2 = 0.0f; }
};

struct BiquadCoeffs
{
    float b0 {1.0f};
    float b1 {0.0f};
    float b2 {0.0f};
    float a1 {0.0f};
    float a2 {0.0f};

    [[nodiscard]] inline float process(float in, BiquadState& s) const noexcept
    {
        const float out = b0 * in + s.z1;
        s.z1 = b1 * in - a1 * out + s.z2;
        s.z2 = b2 * in - a2 * out;
        return out;
    }
};

class AnalogDegraderSuiteNode final : public ProcessorNode
{
public:
    AnalogDegraderSuiteNode() noexcept;
    ~AnalogDegraderSuiteNode() override = default;

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void process(ProcessContext& context) noexcept override;
    void reset() noexcept override;

    [[nodiscard]] std::string_view name() const noexcept { return "Analog Tape, Cassette & Vinyl Degrader Suite"; }

    // Profile Selection
    void setProfile(DegraderProfile profile) noexcept;
    [[nodiscard]] DegraderProfile profile() const noexcept { return m_profile; }

    // Tape & Saturation Controls
    void setDrive(float driveDb) noexcept { m_driveDb = std::clamp(driveDb, -12.0f, 36.0f); }
    [[nodiscard]] float drive() const noexcept { return m_driveDb; }
    void setTapeHysteresis(float amount) noexcept { m_hysteresis = std::clamp(amount, 0.0f, 1.0f); }
    [[nodiscard]] float tapeHysteresis() const noexcept { return m_hysteresis; }
    void setHeadBumpGain(float gainDb) noexcept { m_headBumpDb = std::clamp(gainDb, 0.0f, 12.0f); }
    [[nodiscard]] float headBumpGain() const noexcept { return m_headBumpDb; }

    // Azimuth Misalignment & Dropouts
    void setAzimuthTiltMicros(float microseconds) noexcept { m_azimuthMicros = std::clamp(microseconds, 0.0f, 80.0f); }
    [[nodiscard]] float azimuthTiltMicros() const noexcept { return m_azimuthMicros; }
    void setAzimuthJitter(float jitterAmount) noexcept { m_azimuthJitter = std::clamp(jitterAmount, 0.0f, 1.0f); }
    [[nodiscard]] float azimuthJitter() const noexcept { return m_azimuthJitter; }
    void setDropoutRate(float rate) noexcept { m_dropoutRate = std::clamp(rate, 0.0f, 1.0f); }
    [[nodiscard]] float dropoutRate() const noexcept { return m_dropoutRate; }

    // Vinyl Mechanics
    void setVinylCrackleLevel(float level) noexcept { m_crackleLevel = std::clamp(level, 0.0f, 1.0f); }
    [[nodiscard]] float vinylCrackleLevel() const noexcept { return m_crackleLevel; }
    void setDustPopDensity(float density) noexcept { m_dustPopDensity = std::clamp(density, 0.0f, 1.0f); }
    [[nodiscard]] float dustPopDensity() const noexcept { return m_dustPopDensity; }
    void setWarpWowDepth(float depth) noexcept { m_warpWowDepth = std::clamp(depth, 0.0f, 1.0f); }
    [[nodiscard]] float warpWowDepth() const noexcept { return m_warpWowDepth; }
    void setRadialArmPosition(float pos) noexcept { m_radialPosition = std::clamp(pos, 0.0f, 1.0f); } // 0.0 = outer edge, 1.0 = inner groove
    [[nodiscard]] float radialArmPosition() const noexcept { return m_radialPosition; }
    void setMonoBassEnabled(bool enabled) noexcept { m_monoBassEnabled = enabled; }
    [[nodiscard]] bool isMonoBassEnabled() const noexcept { return m_monoBassEnabled; }

private:
    void updateFilters() noexcept;
    float readDelayFractional(const std::vector<float>& buffer, std::size_t writeIdx, double delaySamples) const noexcept;

    double m_sampleRate {48000.0};
    std::uint16_t m_channelCount {2};

    DegraderProfile m_profile {DegraderProfile::typeI_Ferric};
    float m_driveDb {0.0f};
    float m_hysteresis {0.3f};
    float m_headBumpDb {3.0f};
    float m_azimuthMicros {15.0f}; // 15 microseconds tilt
    float m_azimuthJitter {0.2f};
    float m_dropoutRate {0.1f};

    float m_crackleLevel {0.05f};
    float m_dustPopDensity {0.02f};
    float m_warpWowDepth {0.1f};
    float m_radialPosition {0.2f}; // Outer track
    bool m_monoBassEnabled {true};

    // Delay lines for azimuth micro-delay and warp wow
    static constexpr std::size_t maxDelayFrames = 4096;
    std::vector<float> m_delayBufferL;
    std::vector<float> m_delayBufferR;
    std::size_t m_writeIdx {0};

    // State machines
    double m_warpPhase {0.0};
    double m_azimuthJitterPhase {0.0};
    float m_dropoutGain {1.0f};
    std::uint32_t m_prngSeed {0x12345678};

    // Filters for profiles
    BiquadCoeffs m_headBumpCoeffs;
    BiquadState m_headBumpStateL;
    BiquadState m_headBumpStateR;

    BiquadCoeffs m_highLossCoeffs;
    BiquadState m_highLossStateL;
    BiquadState m_highLossStateR;

    BiquadCoeffs m_microBandpassCoeffs;
    BiquadState m_microBandpassStateL;
    BiquadState m_microBandpassStateR;

    BiquadCoeffs m_monoBassCoeffs;
    BiquadState m_monoBassStateSide;
};

} // namespace dawg::engine::dsp
