#include <dawg/engine/dsp/DynamicsCompressorNode.h>

#include <algorithm>

namespace dawg::engine::dsp {

DynamicsCompressorNode::DynamicsCompressorNode(CompressorSettings settings) noexcept
    : m_settings(settings)
{
    updateBallistics();
}

void DynamicsCompressorNode::setSettings(const CompressorSettings& settings) noexcept
{
    m_settings = settings;
    updateBallistics();
}

void DynamicsCompressorNode::updateBallistics() noexcept
{
    const double safeRate = std::max(1000.0, m_sampleRate);
    const float safeAttackSec = std::max(0.0001f, m_settings.attackMs * 0.001f);
    const float safeReleaseSec = std::max(0.001f, m_settings.releaseMs * 0.001f);

    m_attackCoeff = std::exp(-1.0f / (static_cast<float>(safeRate) * safeAttackSec));
    m_releaseCoeff = std::exp(-1.0f / (static_cast<float>(safeRate) * safeReleaseSec));
    m_makeupLinear = std::pow(10.0f, m_settings.makeupGainDb / 20.0f);
}

bool DynamicsCompressorNode::prepare(
    const double sampleRate,
    const std::size_t maxBlockSize,
    const std::uint16_t channelCount)
{
    if (sampleRate <= 0.0 || maxBlockSize == 0 || channelCount == 0)
    {
        return false;
    }

    m_sampleRate = sampleRate;
    reset();
    updateBallistics();
    return true;
}

void DynamicsCompressorNode::reset() noexcept
{
    m_envelopeDb = 0.0f;
    m_gainReductionDb.store(0.0f, std::memory_order_relaxed);
}

void DynamicsCompressorNode::process(ProcessContext& context) noexcept
{
    if (m_settings.bypassed || context.audio.channels == nullptr || context.audio.frameCount == 0)
    {
        m_gainReductionDb.store(0.0f, std::memory_order_relaxed);
        return;
    }

    const std::size_t channelCount = context.audio.channelCount;
    const std::size_t frameCount = context.audio.frameCount;
    const float threshold = m_settings.thresholdDb;
    const float ratio = std::max(1.0f, m_settings.ratio);
    const float knee = std::max(0.0f, m_settings.kneeDb);
    const float halfKnee = knee * 0.5f;
    const float slopeFactor = 1.0f - (1.0f / ratio);

    float maxBlockReduction = 0.0f;

    const bool useExternalSidechain = m_settings.sidechainEnabled
        && context.sidechain.channels != nullptr
        && context.sidechain.channelCount > 0;
    float* const* const detectorChannels = useExternalSidechain ? context.sidechain.channels : context.audio.channels;
    const std::size_t detectorChannelCount = useExternalSidechain ? context.sidechain.channelCount : channelCount;

    for (std::size_t i = 0; i < frameCount; ++i)
    {
        // Level detector (peak across stereo/channels of main or external sidechain)
        float peakAbs = 0.0f;
        for (std::size_t ch = 0; ch < detectorChannelCount; ++ch)
        {
            if (detectorChannels[ch] != nullptr)
            {
                peakAbs = std::max(peakAbs, std::abs(detectorChannels[ch][i]));
            }
        }

        // Convert to dB
        const float inputDb = (peakAbs > 1e-6f) ? (20.0f * std::log10(peakAbs)) : -120.0f;

        // Static compression curve with soft knee
        const float overshoot = inputDb - threshold;
        float targetReductionDb = 0.0f;

        if (knee > 0.001f)
        {
            if (overshoot <= -halfKnee)
            {
                targetReductionDb = 0.0f;
            }
            else if (overshoot >= halfKnee)
            {
                targetReductionDb = overshoot * slopeFactor;
            }
            else
            {
                const float inKnee = overshoot + halfKnee;
                targetReductionDb = ((inKnee * inKnee) / (2.0f * knee)) * slopeFactor;
            }
        }
        else
        {
            if (overshoot > 0.0f)
            {
                targetReductionDb = overshoot * slopeFactor;
            }
        }

        // Ballistics envelope smoothing
        if (targetReductionDb > m_envelopeDb)
        {
            m_envelopeDb = m_attackCoeff * m_envelopeDb + (1.0f - m_attackCoeff) * targetReductionDb;
        }
        else
        {
            m_envelopeDb = m_releaseCoeff * m_envelopeDb + (1.0f - m_releaseCoeff) * targetReductionDb;
        }

        maxBlockReduction = std::max(maxBlockReduction, m_envelopeDb);

        // Apply gain reduction + make-up gain
        const float gainLinear = std::pow(10.0f, -m_envelopeDb / 20.0f) * m_makeupLinear;

        for (std::size_t ch = 0; ch < channelCount; ++ch)
        {
            if (context.audio.channels[ch] != nullptr)
            {
                context.audio.channels[ch][i] *= gainLinear;
            }
        }
    }

    m_gainReductionDb.store(maxBlockReduction, std::memory_order_relaxed);
}

} // namespace dawg::engine::dsp
