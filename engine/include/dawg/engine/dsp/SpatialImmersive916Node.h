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

enum class SpatialChannel916 : std::uint8_t
{
    left = 0,
    right,
    center,
    lfe,
    leftSideSurround,
    rightSideSurround,
    leftRearSurround,
    rightRearSurround,
    leftWide,
    rightWide,
    leftTopFront,
    rightTopFront,
    leftTopMiddle,
    rightTopMiddle,
    leftTopRear,
    rightTopRear
};

enum class SpatialOutputFormat : std::uint8_t
{
    format916 = 0, // 16 channels
    format714,     // 12 channels
    format51,      // 6 channels
    formatBinauralStereo // 2 channels
};

enum class SpatialTrajectory916 : std::uint8_t
{
    manual = 0,
    orbit,
    spiral,
    ceilingDive,
    figure8
};

struct SpeakerPosition3D
{
    float x; // -1.0 (Left) to +1.0 (Right)
    float y; // -1.0 (Rear) to +1.0 (Front)
    float z; // -1.0 (Floor) to +1.0 (Ceiling)
    bool isHeight;
    bool isLfe;
};

class SpatialImmersive916Node final : public ProcessorNode
{
public:
    static constexpr std::size_t numSpeakers = 16;

    SpatialImmersive916Node() noexcept;
    ~SpatialImmersive916Node() override = default;

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void process(ProcessContext& context) noexcept override;
    void reset() noexcept override;

    [[nodiscard]] std::string_view name() const noexcept { return "Dolby Atmos 9.1.6 Immersive Bed/Object Engine"; }

    // 3D Object Coordinates
    void setPosition(float x, float y, float z) noexcept;
    void setSpherical(float azimuthDeg, float elevationDeg, float distanceMeters) noexcept;
    [[nodiscard]] float positionX() const noexcept { return m_posX.load(std::memory_order_relaxed); }
    [[nodiscard]] float positionY() const noexcept { return m_posY.load(std::memory_order_relaxed); }
    [[nodiscard]] float positionZ() const noexcept { return m_posZ.load(std::memory_order_relaxed); }

    // Trajectory Automation
    void setTrajectory(SpatialTrajectory916 traj) noexcept { m_trajectory = traj; }
    [[nodiscard]] SpatialTrajectory916 trajectory() const noexcept { return m_trajectory; }
    void setTrajectorySpeedHz(float speedHz) noexcept { m_trajectorySpeedHz = std::max(0.01f, speedHz); }
    [[nodiscard]] float trajectorySpeedHz() const noexcept { return m_trajectorySpeedHz; }

    // Output Format & Downmix
    void setOutputFormat(SpatialOutputFormat fmt) noexcept { m_outputFormat = fmt; }
    [[nodiscard]] SpatialOutputFormat outputFormat() const noexcept { return m_outputFormat; }
    void setLfeSendLevel(float level) noexcept { m_lfeSend = std::clamp(level, 0.0f, 1.0f); }
    [[nodiscard]] float lfeSendLevel() const noexcept { return m_lfeSend; }

    // Gain Telemetry
    [[nodiscard]] float speakerGain(std::size_t channelIndex) const noexcept;

private:
    void updateSpeakerGains() noexcept;
    void updateTrajectory(std::size_t numFrames) noexcept;

    double m_sampleRate {48000.0};
    std::uint16_t m_channelCount {16};

    std::atomic<float> m_posX {0.0f};
    std::atomic<float> m_posY {0.8f}; // Center front by default
    std::atomic<float> m_posZ {0.0f}; // Ear level

    SpatialTrajectory916 m_trajectory {SpatialTrajectory916::manual};
    float m_trajectorySpeedHz {0.25f};
    double m_trajectoryPhase {0.0};

    SpatialOutputFormat m_outputFormat {SpatialOutputFormat::format916};
    float m_lfeSend {0.1f};

    std::array<SpeakerPosition3D, numSpeakers> m_speakerPositions;
    std::array<float, numSpeakers> m_speakerGains {};

    // Single-pole low-pass filter for LFE bass management (80 Hz)
    float m_lfeFilterZ {0.0f};
    float m_lfeCoeff {0.01f};
};

} // namespace dawg::engine::dsp
