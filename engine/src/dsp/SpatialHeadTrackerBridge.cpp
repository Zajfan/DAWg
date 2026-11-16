#include <dawg/engine/dsp/SpatialHeadTrackerBridge.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::engine::dsp {

namespace {

float wrapAngleDeg(float deg) noexcept
{
    while (deg > 180.0f) deg -= 360.0f;
    while (deg < -180.0f) deg += 360.0f;
    return deg;
}

} // namespace

SpatialHeadTrackerBridge::SpatialHeadTrackerBridge() noexcept
{
    reset();
}

void SpatialHeadTrackerBridge::reset() noexcept
{
    m_rawOrientation = {};
    m_filteredOrientation = {};
    m_zeroReferenceOffset = {};
}

void SpatialHeadTrackerBridge::updateRawOrientation(float yawDeg, float pitchDeg, float rollDeg) noexcept
{
    m_rawOrientation.yawDeg = wrapAngleDeg(yawDeg);
    m_rawOrientation.pitchDeg = std::clamp(pitchDeg, -90.0f, 90.0f);
    m_rawOrientation.rollDeg = std::clamp(rollDeg, -90.0f, 90.0f);

    stepFilter();
}

void SpatialHeadTrackerBridge::updateFromQuaternion(float w, float x, float y, float z) noexcept
{
    constexpr float radToDeg = 180.0f / std::numbers::pi_v<float>;

    // Quaternion to Euler conversion
    const float sinY = 2.0f * (w * y + x * z);
    const float cosY = 1.0f - 2.0f * (y * y + z * z);
    const float yaw = std::atan2(sinY, cosY) * radToDeg;

    const float sinP = 2.0f * (w * x - y * z);
    const float pitch = std::asin(std::clamp(sinP, -1.0f, 1.0f)) * radToDeg;

    const float sinR = 2.0f * (w * z + x * y);
    const float cosR = 1.0f - 2.0f * (x * x + z * z);
    const float roll = std::atan2(sinR, cosR) * radToDeg;

    updateRawOrientation(yaw, pitch, roll);
}

bool SpatialHeadTrackerBridge::parseOscMessage(
    std::string_view address,
    const float* args,
    std::size_t argCount) noexcept
{
    if (args == nullptr || argCount == 0)
    {
        return false;
    }

    if (address == "/tracking/yaw" && argCount >= 1)
    {
        updateRawOrientation(args[0], m_rawOrientation.pitchDeg, m_rawOrientation.rollDeg);
        return true;
    }
    if (address == "/tracking/pitch" && argCount >= 1)
    {
        updateRawOrientation(m_rawOrientation.yawDeg, args[0], m_rawOrientation.rollDeg);
        return true;
    }
    if (address == "/tracking/roll" && argCount >= 1)
    {
        updateRawOrientation(m_rawOrientation.yawDeg, m_rawOrientation.pitchDeg, args[0]);
        return true;
    }
    if (address == "/tracking/ypr" && argCount >= 3)
    {
        updateRawOrientation(args[0], args[1], args[2]);
        return true;
    }
    if (address == "/tracking/quat" && argCount >= 4)
    {
        updateFromQuaternion(args[0], args[1], args[2], args[3]);
        return true;
    }

    return false;
}

void SpatialHeadTrackerBridge::stepFilter() noexcept
{
    const float alpha = 1.0f - m_smoothingFactor;

    // Handle circular wrap-around on yaw filtering
    float yawDiff = wrapAngleDeg(m_rawOrientation.yawDeg - m_filteredOrientation.yawDeg);
    m_filteredOrientation.yawDeg = wrapAngleDeg(m_filteredOrientation.yawDeg + alpha * yawDiff);

    m_filteredOrientation.pitchDeg += alpha * (m_rawOrientation.pitchDeg - m_filteredOrientation.pitchDeg);
    m_filteredOrientation.rollDeg += alpha * (m_rawOrientation.rollDeg - m_filteredOrientation.rollDeg);

    // Highpass drift compensation towards zero reference
    if (m_driftCompensationEnabled)
    {
        m_filteredOrientation.yawDeg *= (1.0f - m_driftDecayRate);
        m_filteredOrientation.pitchDeg *= (1.0f - m_driftDecayRate);
        m_filteredOrientation.rollDeg *= (1.0f - m_driftDecayRate);
    }
}

void SpatialHeadTrackerBridge::calibrateZeroReference() noexcept
{
    m_zeroReferenceOffset = m_filteredOrientation;
}

HeadOrientation SpatialHeadTrackerBridge::calibratedOrientation() const noexcept
{
    HeadOrientation out;
    out.yawDeg = wrapAngleDeg(m_filteredOrientation.yawDeg - m_zeroReferenceOffset.yawDeg);
    out.pitchDeg = std::clamp(m_filteredOrientation.pitchDeg - m_zeroReferenceOffset.pitchDeg, -90.0f, 90.0f);
    out.rollDeg = std::clamp(m_filteredOrientation.rollDeg - m_zeroReferenceOffset.rollDeg, -90.0f, 90.0f);
    return out;
}

Vector3D SpatialHeadTrackerBridge::transformWorldToHeadSpace(const Vector3D& worldPos) const noexcept
{
    constexpr float degToRad = std::numbers::pi_v<float> / 180.0f;
    const auto orient = calibratedOrientation();

    // Map world object into head-centric coordinates
    const float yaw = orient.yawDeg * degToRad;
    const float pitch = orient.pitchDeg * degToRad;
    const float roll = orient.rollDeg * degToRad;

    const float cosY = std::cos(yaw), sinY = std::sin(yaw);
    const float cosP = std::cos(pitch), sinP = std::sin(pitch);
    const float cosR = std::cos(roll), sinR = std::sin(roll);

    // Rotate about Z (yaw in audio horizontal plane)
    const float x1 = cosY * worldPos.x - sinY * worldPos.y;
    const float y1 = sinY * worldPos.x + cosY * worldPos.y;
    const float z1 = worldPos.z;

    // Rotate about X (pitch elevation: looking up lowers apparent front object)
    const float x2 = x1;
    const float y2 = cosP * y1 + sinP * z1;
    const float z2 = -sinP * y1 + cosP * z1;

    // Rotate about Y (roll ear tilt)
    Vector3D out;
    out.x = cosR * x2 - sinR * z2;
    out.y = y2;
    out.z = sinR * x2 + cosR * z2;

    return out;
}

void SpatialHeadTrackerBridge::applyToControlRoomSimulator(StudioControlRoomSimulatorNode& room) const noexcept
{
    const auto orient = calibratedOrientation();
    room.setHeadYawDegrees(orient.yawDeg);
}

void SpatialHeadTrackerBridge::applyToBinauralPanner(
    BinauralSpatialPannerNode& panner,
    const Vector3D& objectWorldPos) const noexcept
{
    constexpr float radToDeg = 180.0f / std::numbers::pi_v<float>;
    const auto headPos = transformWorldToHeadSpace(objectWorldPos);

    // Convert Cartesian (x: right, y: front, z: up) to spherical azimuth and elevation
    const float dist = std::sqrt(headPos.x * headPos.x + headPos.y * headPos.y + headPos.z * headPos.z);
    if (dist < 1.0e-5f)
    {
        panner.setAzimuthDeg(0.0f);
        panner.setElevationDeg(0.0f);
        panner.setDistanceMeters(0.5f);
        return;
    }

    const float azimuth = std::atan2(headPos.x, headPos.y) * radToDeg;
    const float elevation = std::asin(std::clamp(headPos.z / dist, -1.0f, 1.0f)) * radToDeg;

    panner.setAzimuthDeg(azimuth);
    panner.setElevationDeg(elevation);
    panner.setDistanceMeters(dist);
}

} // namespace dawg::engine::dsp
