#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>

namespace dawg::engine::dsp {

// Vadim Zavalishin's 4-Pole (24 dB/oct) Non-Linear ZDF Transistor Ladder Filter
// (The Art of VA Filter Design, Chapter 5)
class ZdfLadderFilter final
{
public:
    ZdfLadderFilter() noexcept = default;
    ~ZdfLadderFilter() = default;

    void reset() noexcept
    {
        s1 = 0.0f;
        s2 = 0.0f;
        s3 = 0.0f;
        s4 = 0.0f;
    }

    void setDrive(float drive) noexcept
    {
        m_drive = std::clamp(drive, 0.1f, 10.0f);
    }

    void setResonanceCompensation(float comp) noexcept
    {
        m_resComp = std::clamp(comp, 0.0f, 1.0f);
    }

    [[nodiscard]] float process(
        float in,
        float cutoffHz,
        float resonance,
        double sampleRate) noexcept
    {
        if (sampleRate <= 0.0)
        {
            return in;
        }

        // Clamp cutoff within [20 Hz, 0.49 * Fs]
        const float nyquist = static_cast<float>(0.49 * sampleRate);
        const float fc = std::clamp(cutoffHz, 20.0f, nyquist);
        const float omega = static_cast<float>(std::numbers::pi) * fc / static_cast<float>(sampleRate);
        const float g = std::tan(omega);
        const float gDiv = g / (1.0f + g);

        // Feedback resonance amount (0 to 4.0; self-oscillation begins around 3.8-4.0)
        const float k = std::clamp(resonance, 0.0f, 3.95f);

        // Passband bass compensation: adds back low-end power attenuated by resonance feedback
        const float bassComp = 1.0f + m_resComp * (0.5f * k);
        const float x = std::tanh(m_drive * in * bassComp);

        // Compute instantaneous state feedback estimate (Vadim Zavalishin ZDF formulation)
        const float g1 = gDiv;
        const float g2 = g1 * gDiv;
        const float g3 = g2 * gDiv;
        const float g4 = g3 * gDiv;

        const float sigma = g3 * s1 + g2 * s2 + g1 * s3 + s4;

        // Solve the delay-free feedback loop
        const float u = (x - k * sigma) / (1.0f + k * g4);

        // 4 cascaded 1-pole TPT (Topology-Preserving Transform) integrator stages with soft saturation
        const float v1 = gDiv * (std::tanh(u) - s1);
        const float y1 = v1 + s1;
        s1 = y1 + v1;

        const float v2 = gDiv * (y1 - s2);
        const float y2 = v2 + s2;
        s2 = y2 + v2;

        const float v3 = gDiv * (y2 - s3);
        const float y3 = v3 + s3;
        s3 = y3 + v3;

        const float v4 = gDiv * (y3 - s4);
        const float y4 = v4 + s4;
        s4 = y4 + v4;

        return y4;
    }

private:
    float s1 {0.0f};
    float s2 {0.0f};
    float s3 {0.0f};
    float s4 {0.0f};

    float m_drive {1.0f};
    float m_resComp {0.5f}; // 50% bass compensation default
};

} // namespace dawg::engine::dsp
