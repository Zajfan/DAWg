#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <vector>

#include <dawg/engine/AudioBufferView.h>
#include <dawg/engine/ChannelStrip.h>
#include <dawg/engine/MidiEvent.h>

namespace dawg::engine
{
struct ProcessContext
{
    AudioBufferView audio {};
    MidiBufferView midi {};
    double sampleRate {48000.0};
    AudioBufferView sidechain {};
};

class ProcessorNode
{
public:
    virtual ~ProcessorNode() = default;

    [[nodiscard]] virtual bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) = 0;

    virtual void reset() noexcept = 0;

    [[nodiscard]] virtual std::uint32_t latencySamples() const noexcept
    {
        return 0;
    }

    virtual void process(ProcessContext& context) noexcept = 0;
};

class GainPanNode final : public ProcessorNode
{
public:
    explicit GainPanNode(float gain = 1.0f, float pan = 0.0f) noexcept
        : m_gain(gain)
        , m_pan(std::clamp(pan, -1.0f, 1.0f))
    {
    }

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override
    {
        return sampleRate > 0.0 && maxBlockSize > 0 && channelCount > 0;
    }

    void reset() noexcept override {}

    void process(ProcessContext& context) noexcept override
    {
        if (context.audio.channels == nullptr || context.audio.channelCount == 0 || context.audio.frameCount == 0)
        {
            return;
        }

        if (context.audio.channelCount == 1)
        {
            auto* ch0 = context.audio.channels[0];
            if (ch0 == nullptr)
            {
                return;
            }
            for (std::size_t i = 0; i < context.audio.frameCount; ++i)
            {
                ch0[i] *= m_gain;
            }
            return;
        }

        const auto [panLeft, panRight] = equalPowerPanGains(m_pan);
        const float leftGain = m_gain * panLeft;
        const float rightGain = m_gain * panRight;

        if (context.audio.channels[0] != nullptr)
        {
            for (std::size_t i = 0; i < context.audio.frameCount; ++i)
            {
                context.audio.channels[0][i] *= leftGain;
            }
        }
        if (context.audio.channels[1] != nullptr)
        {
            for (std::size_t i = 0; i < context.audio.frameCount; ++i)
            {
                context.audio.channels[1][i] *= rightGain;
            }
        }
        for (std::size_t ch = 2; ch < context.audio.channelCount; ++ch)
        {
            if (context.audio.channels[ch] != nullptr)
            {
                for (std::size_t i = 0; i < context.audio.frameCount; ++i)
                {
                    context.audio.channels[ch][i] *= m_gain;
                }
            }
        }
    }

private:
    float m_gain {1.0f};
    float m_pan {0.0f};
};

class DelayCompensatorNode final : public ProcessorNode
{
public:
    explicit DelayCompensatorNode(std::uint32_t delaySamples = 0) noexcept
        : m_delaySamples(delaySamples)
    {
    }

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override
    {
        if (sampleRate <= 0.0 || maxBlockSize == 0 || channelCount == 0)
        {
            return false;
        }

        m_channelCount = channelCount;
        m_writeCursor = 0;
        if (m_delaySamples > 0)
        {
            m_buffer.assign(
                static_cast<std::size_t>(channelCount) * static_cast<std::size_t>(m_delaySamples),
                0.0f);
        }
        else
        {
            m_buffer.clear();
        }
        return true;
    }

    void reset() noexcept override
    {
        std::fill(m_buffer.begin(), m_buffer.end(), 0.0f);
        m_writeCursor = 0;
    }

    [[nodiscard]] std::uint32_t latencySamples() const noexcept override
    {
        return m_delaySamples;
    }

    void process(ProcessContext& context) noexcept override
    {
        if (m_delaySamples == 0 || context.audio.channels == nullptr || context.audio.frameCount == 0)
        {
            return;
        }

        const auto activeChannels = std::min<std::size_t>(context.audio.channelCount, m_channelCount);
        const auto delayLen = static_cast<std::size_t>(m_delaySamples);

        for (std::size_t sampleIndex = 0; sampleIndex < context.audio.frameCount; ++sampleIndex)
        {
            for (std::size_t ch = 0; ch < activeChannels; ++ch)
            {
                auto* channelData = context.audio.channels[ch];
                if (channelData == nullptr)
                {
                    continue;
                }

                auto* delayLine = m_buffer.data() + ch * delayLen;
                const float delayedSample = delayLine[m_writeCursor];
                delayLine[m_writeCursor] = channelData[sampleIndex];
                channelData[sampleIndex] = delayedSample;
            }

            m_writeCursor = (m_writeCursor + 1U) % delayLen;
        }
    }

private:
    std::uint32_t m_delaySamples {0};
    std::uint16_t m_channelCount {0};
    std::size_t m_writeCursor {0};
    std::vector<float> m_buffer;
};

class BiquadFilterNode final : public ProcessorNode
{
public:
    enum class Type : std::uint8_t
    {
        lowPass,
        highPass,
        peaking
    };

    BiquadFilterNode(Type type, double frequencyHz, double q, double gainDb = 0.0) noexcept
        : m_type(type)
        , m_frequencyHz(frequencyHz)
        , m_q(q)
        , m_gainDb(gainDb)
    {
    }

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override
    {
        if (sampleRate <= 0.0 || maxBlockSize == 0 || channelCount == 0 || m_frequencyHz <= 0.0 || m_q <= 0.0)
        {
            return false;
        }

        m_states.assign(channelCount, FilterState {});
        computeCoefficients(sampleRate);
        return true;
    }

    void reset() noexcept override
    {
        std::fill(m_states.begin(), m_states.end(), FilterState {});
    }

    void process(ProcessContext& context) noexcept override
    {
        if (context.audio.channels == nullptr || context.audio.frameCount == 0)
        {
            return;
        }

        const auto activeChannels = std::min<std::size_t>(context.audio.channelCount, m_states.size());
        for (std::size_t ch = 0; ch < activeChannels; ++ch)
        {
            auto* channelData = context.audio.channels[ch];
            if (channelData == nullptr)
            {
                continue;
            }

            auto& state = m_states[ch];
            for (std::size_t i = 0; i < context.audio.frameCount; ++i)
            {
                const double x = static_cast<double>(channelData[i]);
                const double y = m_b0 * x + state.z1;
                state.z1 = m_b1 * x - m_a1 * y + state.z2;
                state.z2 = m_b2 * x - m_a2 * y;
                channelData[i] = static_cast<float>(y);
            }
        }
    }

private:
    struct FilterState
    {
        double z1 {0.0};
        double z2 {0.0};
    };

    void computeCoefficients(double sampleRate) noexcept
    {
        const double clampedFreq = std::clamp(m_frequencyHz, 10.0, 0.49 * sampleRate);
        const double omega = 2.0 * std::numbers::pi_v<double> * clampedFreq / sampleRate;
        const double sinOmega = std::sin(omega);
        const double cosOmega = std::cos(omega);
        const double alpha = sinOmega / (2.0 * m_q);

        double b0 = 1.0;
        double b1 = 0.0;
        double b2 = 0.0;
        double a0 = 1.0;
        double a1 = 0.0;
        double a2 = 0.0;

        switch (m_type)
        {
        case Type::lowPass:
            b0 = (1.0 - cosOmega) * 0.5;
            b1 = 1.0 - cosOmega;
            b2 = (1.0 - cosOmega) * 0.5;
            a0 = 1.0 + alpha;
            a1 = -2.0 * cosOmega;
            a2 = 1.0 - alpha;
            break;
        case Type::highPass:
            b0 = (1.0 + cosOmega) * 0.5;
            b1 = -(1.0 + cosOmega);
            b2 = (1.0 + cosOmega) * 0.5;
            a0 = 1.0 + alpha;
            a1 = -2.0 * cosOmega;
            a2 = 1.0 - alpha;
            break;
        case Type::peaking:
        {
            const double amp = std::pow(10.0, m_gainDb / 40.0);
            b0 = 1.0 + alpha * amp;
            b1 = -2.0 * cosOmega;
            b2 = 1.0 - alpha * amp;
            a0 = 1.0 + alpha / amp;
            a1 = -2.0 * cosOmega;
            a2 = 1.0 - alpha / amp;
            break;
        }
        }

        m_b0 = b0 / a0;
        m_b1 = b1 / a0;
        m_b2 = b2 / a0;
        m_a1 = a1 / a0;
        m_a2 = a2 / a0;
    }

    Type m_type {Type::lowPass};
    double m_frequencyHz {1000.0};
    double m_q {0.707};
    double m_gainDb {0.0};

    double m_b0 {1.0};
    double m_b1 {0.0};
    double m_b2 {0.0};
    double m_a1 {0.0};
    double m_a2 {0.0};
    std::vector<FilterState> m_states;
};

struct ExternalPluginCallbacks
{
    void* instance {nullptr};
    bool (*prepare)(void* instance, double sampleRate, std::size_t maxBlockSize, std::uint16_t channelCount) noexcept {nullptr};
    void (*reset)(void* instance) noexcept {nullptr};
    std::uint32_t (*latencySamples)(void* instance) noexcept {nullptr};
    void (*process)(void* instance, ProcessContext& context) noexcept {nullptr};
};

class ExternalPluginNode final : public ProcessorNode
{
public:
    explicit ExternalPluginNode(ExternalPluginCallbacks callbacks) noexcept
        : m_callbacks(callbacks)
    {
    }

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override
    {
        if (m_callbacks.prepare == nullptr)
        {
            return true;
        }
        return m_callbacks.prepare(m_callbacks.instance, sampleRate, maxBlockSize, channelCount);
    }

    void reset() noexcept override
    {
        if (m_callbacks.reset != nullptr)
        {
            m_callbacks.reset(m_callbacks.instance);
        }
    }

    [[nodiscard]] std::uint32_t latencySamples() const noexcept override
    {
        if (m_callbacks.latencySamples == nullptr)
        {
            return 0;
        }
        return m_callbacks.latencySamples(m_callbacks.instance);
    }

    void process(ProcessContext& context) noexcept override
    {
        if (m_callbacks.process != nullptr)
        {
            m_callbacks.process(m_callbacks.instance, context);
        }
    }

private:
    ExternalPluginCallbacks m_callbacks {};
};
} // namespace dawg::engine
