#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <string_view>
#include <vector>

#include "dawg/engine/ProcessorNode.h"

namespace dawg::engine::dsp {

struct Quaternion
{
    float w {1.0f};
    float x {0.0f};
    float y {0.0f};
    float z {0.0f};

    [[nodiscard]] static Quaternion identity() noexcept { return {1.0f, 0.0f, 0.0f, 0.0f}; }

    [[nodiscard]] Quaternion normalized() const noexcept
    {
        const float mag = std::sqrt(w * w + x * x + y * y + z * z);
        if (mag < 1e-9f) {
            return identity();
        }
        const float inv = 1.0f / mag;
        return {w * inv, x * inv, y * inv, z * inv};
    }

    [[nodiscard]] static Quaternion slerp(const Quaternion& q1, const Quaternion& q2, float t) noexcept
    {
        float dot = q1.w * q2.w + q1.x * q2.x + q1.y * q2.y + q1.z * q2.z;
        Quaternion q2Adj = q2;
        if (dot < 0.0f) {
            dot = -dot;
            q2Adj.w = -q2Adj.w;
            q2Adj.x = -q2Adj.x;
            q2Adj.y = -q2Adj.y;
            q2Adj.z = -q2Adj.z;
        }

        if (dot > 0.9995f) {
            // Linear interpolation for tiny angles
            return Quaternion {
                q1.w + t * (q2Adj.w - q1.w),
                q1.x + t * (q2Adj.x - q1.x),
                q1.y + t * (q2Adj.y - q1.y),
                q1.z + t * (q2Adj.z - q1.z)
            }.normalized();
        }

        const float theta = std::acos(std::clamp(dot, -1.0f, 1.0f));
        const float sinTheta = std::sin(theta);
        const float w1 = std::sin((1.0f - t) * theta) / sinTheta;
        const float w2 = std::sin(t * theta) / sinTheta;

        return Quaternion {
            w1 * q1.w + w2 * q2Adj.w,
            w1 * q1.x + w2 * q2Adj.x,
            w1 * q1.y + w2 * q2Adj.y,
            w1 * q1.z + w2 * q2Adj.z
        }.normalized();
    }
};

class SofaBinauralSpatializerNode final : public ProcessorNode
{
public:
    static constexpr std::size_t HrirTaps = 64;
    static constexpr std::size_t MaxSofaPoints = 64;

    struct SofaPoint
    {
        float azimuthDeg {0.0f};    // -180 to +180
        float elevationDeg {0.0f};  // -90 to +90
        float distanceM {1.0f};
        std::array<float, HrirTaps> irL {};
        std::array<float, HrirTaps> irR {};
    };

    SofaBinauralSpatializerNode() noexcept;
    ~SofaBinauralSpatializerNode() override = default;

    [[nodiscard]] std::string_view name() const noexcept { return "SofaBinauralSpatializer"; }

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void process(ProcessContext& context) noexcept override;
    void reset() noexcept override;

    // Source 3D Coordinates
    void setSourceAzimuth(float deg) noexcept { m_sourceAzimuthDeg = deg; }
    [[nodiscard]] float sourceAzimuth() const noexcept { return m_sourceAzimuthDeg; }

    void setSourceElevation(float deg) noexcept { m_sourceElevationDeg = std::clamp(deg, -90.0f, 90.0f); }
    [[nodiscard]] float sourceElevation() const noexcept { return m_sourceElevationDeg; }

    void setSourceDistance(float distM) noexcept { m_sourceDistanceM = std::max(0.1f, distM); }
    [[nodiscard]] float sourceDistance() const noexcept { return m_sourceDistanceM; }

    // Head-Tracking Bridge (Quaternion + SLERP)
    void setTargetHeadOrientation(const Quaternion& q) noexcept { m_targetOrientation = q.normalized(); }
    [[nodiscard]] const Quaternion& currentHeadOrientation() const noexcept { return m_currentOrientation; }

    // Custom SOFA measurement point insertion
    bool loadSofaPoint(
        float azimuthDeg,
        float elevationDeg,
        float distanceM,
        const float* irL,
        const float* irR,
        std::size_t taps) noexcept;

    [[nodiscard]] std::size_t sofaPointCount() const noexcept { return m_sofaPointCount; }

private:
    void generateDefaultSphericalHrtf() noexcept;
    void updateInterpolatedHrir() noexcept;

    double m_sampleRate {48000.0};
    float m_sourceAzimuthDeg {0.0f};
    float m_sourceElevationDeg {0.0f};
    float m_sourceDistanceM {1.0f};

    Quaternion m_targetOrientation {Quaternion::identity()};
    Quaternion m_currentOrientation {Quaternion::identity()};

    std::array<SofaPoint, MaxSofaPoints> m_sofaPoints {};
    std::size_t m_sofaPointCount {0};

    // Current active interpolated impulse responses
    std::array<float, HrirTaps> m_activeIrL {};
    std::array<float, HrirTaps> m_activeIrR {};

    // FIR circular delay buffer for real-time convolution
    static constexpr std::size_t FirBufferSize = 256;
    std::array<float, FirBufferSize> m_historyL {};
    std::array<float, FirBufferSize> m_historyR {};
    std::size_t m_writeIdx {0};
};

} // namespace dawg::engine::dsp
