#include "dawg/engine/dsp/SofaBinauralSpatializerNode.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::engine::dsp {

SofaBinauralSpatializerNode::SofaBinauralSpatializerNode() noexcept
{
    generateDefaultSphericalHrtf();
    updateInterpolatedHrir();
    reset();
}

bool SofaBinauralSpatializerNode::prepare(
    double sampleRate,
    [[maybe_unused]] std::size_t maxBlockSize,
    [[maybe_unused]] std::uint16_t channelCount)
{
    m_sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    generateDefaultSphericalHrtf();
    updateInterpolatedHrir();
    reset();
    return true;
}

void SofaBinauralSpatializerNode::reset() noexcept
{
    m_historyL.fill(0.0f);
    m_historyR.fill(0.0f);
    m_writeIdx = 0;
    m_currentOrientation = m_targetOrientation;
}

void SofaBinauralSpatializerNode::generateDefaultSphericalHrtf() noexcept
{
    m_sofaPointCount = 0;
    const float sr = static_cast<float>(m_sampleRate);
    constexpr float speedOfSound = 343.0f;
    constexpr float headRadius = 0.0875f; // ~8.75 cm

    auto addDir = [&](float azDeg, float elDeg) {
        if (m_sofaPointCount >= MaxSofaPoints) {
            return;
        }
        auto& pt = m_sofaPoints[m_sofaPointCount++];
        pt.azimuthDeg = azDeg;
        pt.elevationDeg = elDeg;
        pt.distanceM = 1.0f;
        pt.irL.fill(0.0f);
        pt.irR.fill(0.0f);

        const float azRad = azDeg * std::numbers::pi_v<float> / 180.0f;
        const float elRad = elDeg * std::numbers::pi_v<float> / 180.0f;

        // Woodworth ITD calculation
        const float sinAz = std::sin(azRad) * std::cos(elRad);
        const float itdSeconds = (headRadius / speedOfSound) * (sinAz + std::asin(std::clamp(sinAz, -1.0f, 1.0f)));
        const float itdSamples = itdSeconds * sr;

        // Head shadow ILD factor
        const float shadowL = 1.0f - 0.4f * std::max(0.0f, sinAz);
        const float shadowR = 1.0f - 0.4f * std::max(0.0f, -sinAz);

        // Center tap around index 20
        constexpr float centerTap = 20.0f;
        const float tapL = std::clamp(centerTap - itdSamples * 0.5f, 2.0f, static_cast<float>(HrirTaps - 4));
        const float tapR = std::clamp(centerTap + itdSamples * 0.5f, 2.0f, static_cast<float>(HrirTaps - 4));

        for (std::size_t t = 0; t < HrirTaps; ++t) {
            const float diffL = static_cast<float>(t) - tapL;
            const float diffR = static_cast<float>(t) - tapR;

            // Bandlimited Gaussian impulse
            pt.irL[t] = std::exp(-0.5f * diffL * diffL) * shadowL;
            pt.irR[t] = std::exp(-0.5f * diffR * diffR) * shadowR;
        }
    };

    // 16 Horizontal plane directions
    for (int i = 0; i < 16; ++i) {
        const float az = -180.0f + static_cast<float>(i) * 22.5f;
        addDir(az, 0.0f);
    }
    // 8 Upper hemisphere directions (+45 deg elevation)
    for (int i = 0; i < 8; ++i) {
        const float az = -180.0f + static_cast<float>(i) * 45.0f;
        addDir(az, 45.0f);
    }
    // Zenith and Nadir
    addDir(0.0f, 90.0f);
    addDir(0.0f, -90.0f);
}

bool SofaBinauralSpatializerNode::loadSofaPoint(
    float azimuthDeg,
    float elevationDeg,
    float distanceM,
    const float* irL,
    const float* irR,
    std::size_t taps) noexcept
{
    if (irL == nullptr || irR == nullptr || taps == 0 || m_sofaPointCount >= MaxSofaPoints) {
        return false;
    }

    auto& pt = m_sofaPoints[m_sofaPointCount++];
    pt.azimuthDeg = azimuthDeg;
    pt.elevationDeg = elevationDeg;
    pt.distanceM = distanceM;
    pt.irL.fill(0.0f);
    pt.irR.fill(0.0f);

    const std::size_t copyLen = std::min(taps, HrirTaps);
    std::copy_n(irL, copyLen, pt.irL.begin());
    std::copy_n(irR, copyLen, pt.irR.begin());
    return true;
}

void SofaBinauralSpatializerNode::updateInterpolatedHrir() noexcept
{
    if (m_sofaPointCount == 0) {
        return;
    }

    // Convert source spherical coords to unit vector (X: right, Y: front, Z: up)
    const float srcAzRad = m_sourceAzimuthDeg * std::numbers::pi_v<float> / 180.0f;
    const float srcElRad = m_sourceElevationDeg * std::numbers::pi_v<float> / 180.0f;
    const float vx = std::sin(srcAzRad) * std::cos(srcElRad);
    const float vy = std::cos(srcAzRad) * std::cos(srcElRad);
    const float vz = std::sin(srcElRad);

    // Rotate source vector by inverse of head orientation quaternion q^-1 = (w, -x, -y, -z)
    const Quaternion invQ {m_currentOrientation.w, -m_currentOrientation.x, -m_currentOrientation.y, -m_currentOrientation.z};

    // Quaternion vector rotation: v' = q * (0, v) * q^-1
    // (t = 2 * cross(q.xyz, v); v' = v + q.w * t + cross(q.xyz, t))
    const float qx = invQ.x, qy = invQ.y, qz = invQ.z, qw = invQ.w;
    const float tx = 2.0f * (qy * vz - qz * vy);
    const float ty = 2.0f * (qz * vx - qx * vz);
    const float tz = 2.0f * (qx * vy - qy * vx);

    const float rx = vx + qw * tx + (qy * tz - qz * ty);
    const float ry = vy + qw * ty + (qz * tx - qx * tz);
    const float rz = vz + qw * tz + (qx * ty - qy * tx);

    // Convert relative unit vector back to spherical coordinates
    const float relAzDeg = std::atan2(rx, ry) * 180.0f / std::numbers::pi_v<float>;
    const float relElDeg = std::asin(std::clamp(rz, -1.0f, 1.0f)) * 180.0f / std::numbers::pi_v<float>;

    // Find nearest SOFA points using angular distance
    std::array<std::pair<float, std::size_t>, 3> nearest {{
        {1e9f, 0}, {1e9f, 0}, {1e9f, 0}
    }};

    const float relAzRad = relAzDeg * std::numbers::pi_v<float> / 180.0f;
    const float relElRad = relElDeg * std::numbers::pi_v<float> / 180.0f;

    for (std::size_t i = 0; i < m_sofaPointCount; ++i) {
        const float ptAzRad = m_sofaPoints[i].azimuthDeg * std::numbers::pi_v<float> / 180.0f;
        const float ptElRad = m_sofaPoints[i].elevationDeg * std::numbers::pi_v<float> / 180.0f;

        const float cosDist = std::sin(relElRad) * std::sin(ptElRad) +
                              std::cos(relElRad) * std::cos(ptElRad) * std::cos(relAzRad - ptAzRad);
        const float angDist = std::acos(std::clamp(cosDist, -1.0f, 1.0f));

        if (angDist < nearest[0].first) {
            nearest[2] = nearest[1];
            nearest[1] = nearest[0];
            nearest[0] = {angDist, i};
        } else if (angDist < nearest[1].first) {
            nearest[2] = nearest[1];
            nearest[1] = {angDist, i};
        } else if (angDist < nearest[2].first) {
            nearest[2] = {angDist, i};
        }
    }

    // Inverse distance weighting
    float totalWeight = 0.0f;
    std::array<float, 3> weights {};
    for (std::size_t i = 0; i < 3; ++i) {
        weights[i] = 1.0f / std::max(0.001f, nearest[i].first);
        totalWeight += weights[i];
    }
    const float norm = 1.0f / totalWeight;
    const float distAtten = 1.0f / std::max(1.0f, m_sourceDistanceM);

    m_activeIrL.fill(0.0f);
    m_activeIrR.fill(0.0f);

    for (std::size_t i = 0; i < 3; ++i) {
        const float w = weights[i] * norm * distAtten;
        const auto& pt = m_sofaPoints[nearest[i].second];
        for (std::size_t t = 0; t < HrirTaps; ++t) {
            m_activeIrL[t] += pt.irL[t] * w;
            m_activeIrR[t] += pt.irR[t] * w;
        }
    }
}

void SofaBinauralSpatializerNode::process(ProcessContext& context) noexcept
{
    auto& audio = context.audio;
    if (audio.channelCount == 0 || audio.frameCount == 0 || audio.channels[0] == nullptr) {
        return;
    }

    // Smooth head orientation tracking via SLERP
    m_currentOrientation = Quaternion::slerp(m_currentOrientation, m_targetOrientation, 0.15f);
    updateInterpolatedHrir();

    float* left = audio.channels[0];
    float* right = audio.channelCount > 1 && audio.channels[1] != nullptr ? audio.channels[1] : nullptr;

    for (std::size_t i = 0; i < audio.frameCount; ++i) {
        const float inL = left[i];
        const float inR = right != nullptr ? right[i] : inL;
        const float mono = 0.5f * (inL + inR);

        m_historyL[m_writeIdx] = mono;

        float convL = 0.0f;
        float convR = 0.0f;

        for (std::size_t t = 0; t < HrirTaps; ++t) {
            const std::size_t rIdx = (m_writeIdx + FirBufferSize - t) % FirBufferSize;
            const float sample = m_historyL[rIdx];
            convL += sample * m_activeIrL[t];
            convR += sample * m_activeIrR[t];
        }

        m_writeIdx = (m_writeIdx + 1) % FirBufferSize;

        left[i] = convL;
        if (right != nullptr) {
            right[i] = convR;
        }
    }
}

} // namespace dawg::engine::dsp
