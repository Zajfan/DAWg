#pragma once

#include <dawg/engine/dsp/BinauralSpatialPannerNode.h>
#include <dawg/engine/dsp/StudioControlRoomSimulatorNode.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace dawg::engine::dsp {

struct HeadOrientation
{
    float yawDeg {0.0f};   // -180.0 to +180.0 (turn left/right)
    float pitchDeg {0.0f}; // -90.0 to +90.0 (look up/down)
    float rollDeg {0.0f};  // -90.0 to +90.0 (tilt ear to shoulder)
};

struct Vector3D
{
    float x {0.0f};
    float y {0.0f};
    float z {0.0f};
};

// Spatial Head-Tracking OSC / Bluetooth Gyroscope Bridge (Supperware / Waves Nx style)
// Ingests real-time head orientation from OSC/Bluetooth IMU sensors, applies Kalman-style jitter smoothing,
// performs 3D rotation transforms, zero-drift sweet spot calibration, and applies dynamic updates
// to BinauralSpatialPannerNode and StudioControlRoomSimulatorNode.
class SpatialHeadTrackerBridge
{
public:
    SpatialHeadTrackerBridge() noexcept;
    ~SpatialHeadTrackerBridge() = default;

    void reset() noexcept;

    // Direct Orientation Ingestion (Angles in degrees)
    void updateRawOrientation(float yawDeg, float pitchDeg, float rollDeg) noexcept;
    void updateFromQuaternion(float w, float x, float y, float z) noexcept;

    // OSC Message Packet Parser
    // Supports:
    //   "/tracking/yaw" <float>
    //   "/tracking/pitch" <float>
    //   "/tracking/roll" <float>
    //   "/tracking/ypr" <float:yaw> <float:pitch> <float:roll>
    //   "/tracking/quat" <float:w> <float:x> <float:y> <float:z>
    bool parseOscMessage(std::string_view address, const float* args, std::size_t argCount) noexcept;

    // Sweet-Spot Calibration & Drift Compensation
    void calibrateZeroReference() noexcept;
    void setDriftCompensationEnabled(bool enabled) noexcept { m_driftCompensationEnabled = enabled; }
    [[nodiscard]] bool isDriftCompensationEnabled() const noexcept { return m_driftCompensationEnabled; }

    // Jitter Smoothing (0.0 = raw unsmoothed, 1.0 = heavy lag)
    void setSmoothingFactor(float factor) noexcept { m_smoothingFactor = std::clamp(factor, 0.0f, 0.98f); }
    [[nodiscard]] float smoothingFactor() const noexcept { return m_smoothingFactor; }

    // Calibrated Orientation Query
    [[nodiscard]] HeadOrientation calibratedOrientation() const noexcept;
    [[nodiscard]] HeadOrientation rawOrientation() const noexcept { return m_rawOrientation; }

    // 3D Vector Coordinate Transformation
    // Rotates a static speaker/sound-source vector in room space into the listener's head-centric space
    [[nodiscard]] Vector3D transformWorldToHeadSpace(const Vector3D& worldPos) const noexcept;

    // Direct Integration with DAWg Monitoring Processors
    void applyToControlRoomSimulator(StudioControlRoomSimulatorNode& room) const noexcept;
    void applyToBinauralPanner(BinauralSpatialPannerNode& panner, const Vector3D& objectWorldPos) const noexcept;

private:
    void stepFilter() noexcept;

    HeadOrientation m_rawOrientation {};
    HeadOrientation m_filteredOrientation {};
    HeadOrientation m_zeroReferenceOffset {};

    float m_smoothingFactor {0.65f};
    bool m_driftCompensationEnabled {true};
    float m_driftDecayRate {0.0001f}; // Slow highpass leak to prevent gyroscope drift
};

} // namespace dawg::engine::dsp
