#pragma once

#include <dawg/engine/AudioBufferView.h>
#include <dawg/engine/ProcessorNode.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

namespace dawg::engine::dsp {

// 3D Spatial Audio & Binaural Soundstage Panner Node
// Models ITD (Interaural Time Difference via Woodworth spherical head model),
// ILD (Interaural Level Difference via head acoustic shadow),
// Pinna Elevation notch, and Atmospheric Distance Absorption.
class BinauralSpatialPannerNode final : public ProcessorNode
{
public:
    BinauralSpatialPannerNode() noexcept = default;
    ~BinauralSpatialPannerNode() override = default;

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void reset() noexcept override;
    [[nodiscard]] std::uint32_t latencySamples() const noexcept override { return 0; }
    void process(ProcessContext& context) noexcept override;

    // Spatial positioning coordinates
    void setAzimuthDeg(float deg) noexcept
    {
        // -180 (rear) to 0 (front) to +180 (rear)
        while (deg > 180.0f) deg -= 360.0f;
        while (deg < -180.0f) deg += 360.0f;
        m_azimuthDeg = deg;
    }
    [[nodiscard]] float azimuthDeg() const noexcept { return m_azimuthDeg; }

    void setElevationDeg(float deg) noexcept
    {
        // -90 (below) to +90 (above)
        m_elevationDeg = std::clamp(deg, -90.0f, 90.0f);
    }
    [[nodiscard]] float elevationDeg() const noexcept { return m_elevationDeg; }

    void setDistanceMeters(float meters) noexcept
    {
        // 0.2m (near ear) to 20m (far field)
        m_distanceMeters = std::clamp(meters, 0.2f, 20.0f);
    }
    [[nodiscard]] float distanceMeters() const noexcept { return m_distanceMeters; }

    void setHeadRadiusMeters(float radius) noexcept
    {
        m_headRadius = std::clamp(radius, 0.05f, 0.15f);
    }

private:
    // Fractional delay line for sub-sample ITD
    struct FractionalDelay
    {
        std::vector<float> buffer;
        std::size_t writeIdx {0};

        void prepare(std::size_t size)
        {
            buffer.assign(size, 0.0f);
            writeIdx = 0;
        }

        void reset() noexcept
        {
            std::fill(buffer.begin(), buffer.end(), 0.0f);
            writeIdx = 0;
        }

        void push(float sample) noexcept
        {
            if (buffer.empty()) return;
            buffer[writeIdx] = sample;
            writeIdx = (writeIdx + 1) % buffer.size();
        }

        [[nodiscard]] float read(float delaySamples) const noexcept
        {
            if (buffer.empty()) return 0.0f;
            const float d = std::clamp(delaySamples, 0.0f, static_cast<float>(buffer.size() - 2));
            const auto intD = static_cast<std::size_t>(d);
            const float frac = d - static_cast<float>(intD);

            const std::size_t i0 = (writeIdx + buffer.size() - intD - 1) % buffer.size();
            const std::size_t i1 = (i0 + buffer.size() - 1) % buffer.size();

            // Linear interpolation
            return buffer[i0] + frac * (buffer[i1] - buffer[i0]);
        }
    };

    // 1-pole Lowpass / High-shelf filter for ILD head shadow & air absorption
    struct SimpleFilter
    {
        float s {0.0f};

        void reset() noexcept { s = 0.0f; }

        [[nodiscard]] float processLp(float in, float alpha) noexcept
        {
            s = s + alpha * (in - s);
            return s;
        }
    };

    double m_sampleRate {48000.0};
    float m_azimuthDeg {0.0f};
    float m_elevationDeg {0.0f};
    float m_distanceMeters {1.5f};
    float m_headRadius {0.0875f}; // Average human head radius (~8.75 cm)

    FractionalDelay m_itdDelayLeft;
    FractionalDelay m_itdDelayRight;
    SimpleFilter m_headShadowLeft;
    SimpleFilter m_headShadowRight;
    SimpleFilter m_airAbsorption;
};

} // namespace dawg::engine::dsp
