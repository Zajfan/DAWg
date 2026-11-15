#pragma once

#include <dawg/engine/AudioBufferView.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dawg::engine::dsp {

enum class OversampleFactor : std::uint8_t
{
    x1 = 1,
    x2 = 2,
    x4 = 4
};

// Polyphase Half-Band Allpass IIR Section
struct HalfBandAllpassSection
{
    float state {0.0f};

    void reset() noexcept
    {
        state = 0.0f;
    }

    [[nodiscard]] float process(float in, float alpha) noexcept
    {
        const float out = alpha * (in - state) + state;
        state = in;
        return out;
    }
};

// 4th-order Half-Band Polyphase IIR Filter with >80 dB Stopband Rejection
struct HalfBandFilter
{
    // Coefficients for optimal half-band stopband attenuation
    static constexpr float a0 = 0.1617585f;
    static constexpr float a1 = 0.4440800f;
    static constexpr float a2 = 0.7330289f;
    static constexpr float a3 = 0.9102920f;

    HalfBandAllpassSection ap0;
    HalfBandAllpassSection ap1;
    HalfBandAllpassSection ap2;
    HalfBandAllpassSection ap3;

    void reset() noexcept
    {
        ap0.reset();
        ap1.reset();
        ap2.reset();
        ap3.reset();
    }

    // Upsample 1 sample into 2 interleaved samples (out0, out1)
    void upsample(float in, float& out0, float& out1) noexcept
    {
        // Branch 0: AP0 -> AP2
        const float b0 = ap2.process(ap0.process(in, a0), a2);
        // Branch 1: AP1 -> AP3
        const float b1 = ap3.process(ap1.process(in, a1), a3);
        out0 = b0;
        out1 = b1;
    }

    // Downsample 2 samples into 1 filtered sample
    [[nodiscard]] float downsample(float in0, float in1) noexcept
    {
        const float b0 = ap2.process(ap0.process(in0, a0), a2);
        const float b1 = ap3.process(ap1.process(in1, a1), a3);
        return 0.5f * (b0 + b1);
    }
};

class PolyphaseOversampler final
{
public:
    explicit PolyphaseOversampler(OversampleFactor factor = OversampleFactor::x2) noexcept;
    ~PolyphaseOversampler() = default;

    [[nodiscard]] bool prepare(double sampleRate, std::size_t maxBlockSize, std::uint16_t channelCount);
    void reset() noexcept;

    void setFactor(OversampleFactor factor) noexcept { m_factor = factor; }
    [[nodiscard]] OversampleFactor factor() const noexcept { return m_factor; }

    [[nodiscard]] double oversampledRate() const noexcept
    {
        return m_sampleRate * static_cast<double>(static_cast<std::uint8_t>(m_factor));
    }

    // Non-linear processing with polyphase anti-aliased oversampling
    // NonLinearFn is callable as: float fn(float sample, double oversampledRate)
    template <typename NonLinearFn>
    void process(AudioBufferView buffer, NonLinearFn&& fn) noexcept
    {
        if (buffer.channels == nullptr || buffer.frameCount == 0 || buffer.channelCount == 0)
        {
            return;
        }

        if (m_factor == OversampleFactor::x1)
        {
            const auto activeChannels = std::min<std::size_t>(buffer.channelCount, m_channelCount);
            for (std::size_t ch = 0; ch < activeChannels; ++ch)
            {
                auto* channelData = buffer.channels[ch];
                if (channelData == nullptr) continue;
                for (std::size_t i = 0; i < buffer.frameCount; ++i)
                {
                    channelData[i] = fn(channelData[i], m_sampleRate);
                }
            }
            return;
        }

        const auto activeChannels = std::min<std::size_t>(buffer.channelCount, m_channelCount);
        const auto frames = buffer.frameCount;
        const double rate = oversampledRate();

        if (m_factor == OversampleFactor::x2)
        {
            for (std::size_t ch = 0; ch < activeChannels; ++ch)
            {
                auto* channelData = buffer.channels[ch];
                if (channelData == nullptr) continue;
                auto& filter = m_filtersX2[ch];

                // Upsample, process, downsample
                for (std::size_t i = 0; i < frames; ++i)
                {
                    float u0 = 0.0f;
                    float u1 = 0.0f;
                    filter.upsample(channelData[i], u0, u1);

                    // Apply non-linearity at 2x sample rate
                    const float p0 = fn(u0, rate);
                    const float p1 = fn(u1, rate);

                    channelData[i] = filter.downsample(p0, p1);
                }
            }
        }
        else if (m_factor == OversampleFactor::x4)
        {
            for (std::size_t ch = 0; ch < activeChannels; ++ch)
            {
                auto* channelData = buffer.channels[ch];
                if (channelData == nullptr) continue;
                auto& f1 = m_filtersX2[ch];
                auto& f2 = m_filtersX4[ch];

                for (std::size_t i = 0; i < frames; ++i)
                {
                    float u0 = 0.0f;
                    float u1 = 0.0f;
                    f1.upsample(channelData[i], u0, u1);

                    float v00 = 0.0f;
                    float v01 = 0.0f;
                    float v10 = 0.0f;
                    float v11 = 0.0f;
                    f2.upsample(u0, v00, v01);
                    f2.upsample(u1, v10, v11);

                    // Apply non-linearity at 4x sample rate
                    const float p00 = fn(v00, rate);
                    const float p01 = fn(v01, rate);
                    const float p10 = fn(v10, rate);
                    const float p11 = fn(v11, rate);

                    const float d0 = f2.downsample(p00, p01);
                    const float d1 = f2.downsample(p10, p11);
                    channelData[i] = f1.downsample(d0, d1);
                }
            }
        }
    }

private:
    OversampleFactor m_factor {OversampleFactor::x2};
    double m_sampleRate {48000.0};
    std::size_t m_maxBlockSize {512};
    std::uint16_t m_channelCount {2};

    std::vector<HalfBandFilter> m_filtersX2;
    std::vector<HalfBandFilter> m_filtersX4;
};

} // namespace dawg::engine::dsp
