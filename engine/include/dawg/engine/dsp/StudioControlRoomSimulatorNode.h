#pragma once

#include <dawg/engine/AudioBufferView.h>
#include <dawg/engine/ProcessorNode.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace dawg::engine::dsp {

enum class StudioEnvironment : std::uint8_t
{
    masteringSuite, // Pristine, acoustically treated mastering room with farfield mains
    nearfieldStudio, // Intimate control room with nearfield reference monitors
    largeClubPA     // High-energy venue with large dispersion PA and sub reinforcement
};

// Surround Binaural Room Impulse Response (BRIR) & Studio Control Room Simulator
// Simulates reference studio monitors over headphones using binaural crossfeed (ITD/ILD),
// pinna head-shadow filtering, virtual head yaw tracking, and room early reflection modeling.
class StudioControlRoomSimulatorNode final : public ProcessorNode
{
public:
    StudioControlRoomSimulatorNode() noexcept;
    ~StudioControlRoomSimulatorNode() override = default;

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void reset() noexcept override;
    [[nodiscard]] std::uint32_t latencySamples() const noexcept override { return 0; }
    void process(ProcessContext& context) noexcept override;

    // Environment Selection
    void setEnvironment(StudioEnvironment env) noexcept;
    [[nodiscard]] StudioEnvironment environment() const noexcept { return m_environment; }

    // Listener Positioning
    // Head Yaw angle in degrees (-45.0 to +45.0)
    void setHeadYawDegrees(float yawDeg) noexcept;
    [[nodiscard]] float headYawDegrees() const noexcept { return m_headYawDeg; }

    // Distance to monitors in meters (0.8m to 3.5m)
    void setMonitorDistanceMeters(float distanceM) noexcept;
    [[nodiscard]] float monitorDistanceMeters() const noexcept { return m_distanceMeters; }

    // Crossfeed and Ambience amounts
    void setCrossfeedAmount(float amount) noexcept { m_crossfeedAmount = std::clamp(amount, 0.0f, 1.0f); }
    [[nodiscard]] float crossfeedAmount() const noexcept { return m_crossfeedAmount; }

    void setRoomAmbienceAmount(float amount) noexcept { m_roomAmbienceAmount = std::clamp(amount, 0.0f, 1.0f); }
    [[nodiscard]] float roomAmbienceAmount() const noexcept { return m_roomAmbienceAmount; }

    // Master Simulation Dry/Wet (0.0 = Raw Stereo, 1.0 = Fully Simulated Space)
    void setMix(float mix) noexcept { m_mix = std::clamp(mix, 0.0f, 1.0f); }
    [[nodiscard]] float mix() const noexcept { return m_mix; }

private:
    void updateAcousticParameters() noexcept;
    void updateHeadShadowFilters() noexcept;

    double m_sampleRate {48000.0};
    StudioEnvironment m_environment {StudioEnvironment::masteringSuite};

    float m_headYawDeg {0.0f};
    float m_distanceMeters {1.8f};
    float m_crossfeedAmount {0.85f};
    float m_roomAmbienceAmount {0.4f};
    float m_mix {1.0f};

    // Calculated ITD delays in fractional samples
    float m_itdSamplesLeftToRight {13.0f}; // ~270 us at 48kHz
    float m_itdSamplesRightToLeft {13.0f};

    // Delay line buffer for crossfeed & early reflections (4096 samples >= ~85ms at 48kHz)
    static constexpr std::size_t DelayBufferSize = 4096;
    std::array<float, DelayBufferSize> m_delayBufferL {};
    std::array<float, DelayBufferSize> m_delayBufferR {};
    std::size_t m_writeIndex {0};

    // Head-shadow 1st-order low-pass filter states
    float m_shadowCoeffL {0.5f};
    float m_shadowCoeffR {0.5f};
    float m_shadowStateL {0.0f};
    float m_shadowStateR {0.0f};

    // Early reflection taps (relative delay samples and gain coefficients)
    struct EarlyReflectionTap
    {
        std::size_t delaySamples {0};
        float gainL {0.0f};
        float gainR {0.0f};
    };
    std::array<EarlyReflectionTap, 4> m_earlyTaps {};

    // Simple room decay feedback comb filters for background diffusion
    std::array<float, 512> m_combBufferL {};
    std::array<float, 640> m_combBufferR {};
    std::size_t m_combIdxL {0};
    std::size_t m_combIdxR {0};
    float m_combFeedback {0.35f};
};

} // namespace dawg::engine::dsp
