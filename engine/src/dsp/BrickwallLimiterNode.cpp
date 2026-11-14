#include <dawg/engine/dsp/BrickwallLimiterNode.h>

#include <algorithm>

namespace dawg::engine::dsp {

BrickwallLimiterNode::BrickwallLimiterNode(LimiterSettings settings) noexcept
    : m_settings(settings)
{
    updateParameters();
}

void BrickwallLimiterNode::setSettings(const LimiterSettings& settings) noexcept
{
    m_settings = settings;
    updateParameters();
}

void BrickwallLimiterNode::updateParameters() noexcept
{
    m_ceilingLinear = std::pow(10.0f, m_settings.ceilingDb / 20.0f);
    const double safeRate = std::max(1000.0, m_sampleRate);
    const float safeReleaseSec = std::max(0.005f, m_settings.releaseMs * 0.001f);
    m_releaseCoeff = std::exp(-1.0f / (static_cast<float>(safeRate) * safeReleaseSec));
}

bool BrickwallLimiterNode::prepare(
    const double sampleRate,
    const std::size_t maxBlockSize,
    const std::uint16_t channelCount)
{
    if (sampleRate <= 0.0 || maxBlockSize == 0 || channelCount == 0)
    {
        return false;
    }

    m_sampleRate = sampleRate;
    m_channelCount = channelCount;

    const std::size_t lookahead = std::max<std::uint32_t>(1, m_settings.lookaheadSamples);
    m_delayStorage.assign(channelCount * lookahead, 0.0f);
    m_writeIndex = 0;
    m_currentGain = 1.0f;

    updateParameters();
    return true;
}

void BrickwallLimiterNode::reset() noexcept
{
    std::fill(m_delayStorage.begin(), m_delayStorage.end(), 0.0f);
    m_writeIndex = 0;
    m_currentGain = 1.0f;
}

void BrickwallLimiterNode::process(ProcessContext& context) noexcept
{
    if (m_settings.bypassed || context.audio.channels == nullptr || context.audio.frameCount == 0)
    {
        return;
    }

    const std::size_t activeChannels = std::min<std::size_t>(context.audio.channelCount, m_channelCount);
    const std::size_t frameCount = context.audio.frameCount;
    const std::size_t lookahead = std::max<std::uint32_t>(1, m_settings.lookaheadSamples);

    for (std::size_t i = 0; i < frameCount; ++i)
    {
        // 1. Read delayed sample from the circular buffer and write new incoming sample
        float delayedPeak = 0.0f;
        for (std::size_t ch = 0; ch < activeChannels; ++ch)
        {
            float* channelData = context.audio.channels[ch];
            if (channelData == nullptr)
            {
                continue;
            }

            float* delayLine = m_delayStorage.data() + ch * lookahead;
            const float delayedSample = delayLine[m_writeIndex];
            delayLine[m_writeIndex] = channelData[i];

            // Replace input with delayed sample
            channelData[i] = delayedSample;
            delayedPeak = std::max(delayedPeak, std::abs(delayedSample));
        }

        // 2. Compute max peak across the entire lookahead buffer to anticipate peaks
        float windowPeak = delayedPeak;
        for (std::size_t ch = 0; ch < activeChannels; ++ch)
        {
            const float* delayLine = m_delayStorage.data() + ch * lookahead;
            for (std::size_t k = 0; k < lookahead; ++k)
            {
                windowPeak = std::max(windowPeak, std::abs(delayLine[k]));
            }
        }

        // 3. Compute target gain: ensure windowPeak will not exceed ceiling
        float targetGain = 1.0f;
        if (windowPeak > m_ceilingLinear && windowPeak > 1e-6f)
        {
            targetGain = m_ceilingLinear / windowPeak;
        }

        // 4. Attack immediately to protect peaks; release smoothly
        if (targetGain < m_currentGain)
        {
            m_currentGain = targetGain;
        }
        else
        {
            m_currentGain = m_releaseCoeff * m_currentGain + (1.0f - m_releaseCoeff) * targetGain;
        }

        // 5. Apply current gain to the delayed sample
        for (std::size_t ch = 0; ch < activeChannels; ++ch)
        {
            float* channelData = context.audio.channels[ch];
            if (channelData != nullptr)
            {
                channelData[i] *= m_currentGain;
            }
        }

        m_writeIndex = (m_writeIndex + 1) % lookahead;
    }
}

} // namespace dawg::engine::dsp
