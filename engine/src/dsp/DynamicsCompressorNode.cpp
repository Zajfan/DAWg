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

    const float safeGateAttackSec = std::max(0.0001f, m_settings.gateAttackMs * 0.001f);
    const float safeGateReleaseSec = std::max(0.001f, m_settings.gateReleaseMs * 0.001f);
    m_gateAttackCoeff = std::exp(-1.0f / (static_cast<float>(safeRate) * safeGateAttackSec));
    m_gateReleaseCoeff = std::exp(-1.0f / (static_cast<float>(safeRate) * safeGateReleaseSec));

    // Calculate Sidechain Bandpass Filter (HPF & LPF)
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kQ = 0.7071067811865475f;

    // HPF
    if (m_settings.sidechainHpfHz > 20.5f)
    {
        const float f0 = std::clamp(m_settings.sidechainHpfHz, 20.0f, static_cast<float>(safeRate * 0.45));
        const float w0 = 2.0f * kPi * (f0 / static_cast<float>(safeRate));
        const float cosW0 = std::cos(w0);
        const float alpha = std::sin(w0) / (2.0f * kQ);
        const float a0 = 1.0f + alpha;

        m_scHpfB0 = ((1.0f + cosW0) * 0.5f) / a0;
        m_scHpfB1 = (-(1.0f + cosW0)) / a0;
        m_scHpfB2 = ((1.0f + cosW0) * 0.5f) / a0;
        m_scHpfA1 = (-2.0f * cosW0) / a0;
        m_scHpfA2 = (1.0f - alpha) / a0;
    }
    else
    {
        m_scHpfB0 = 1.0f; m_scHpfB1 = 0.0f; m_scHpfB2 = 0.0f;
        m_scHpfA1 = 0.0f; m_scHpfA2 = 0.0f;
    }

    // LPF
    if (m_settings.sidechainLpfHz < static_cast<float>(safeRate * 0.48))
    {
        const float f0 = std::clamp(m_settings.sidechainLpfHz, 100.0f, static_cast<float>(safeRate * 0.48));
        const float w0 = 2.0f * kPi * (f0 / static_cast<float>(safeRate));
        const float cosW0 = std::cos(w0);
        const float alpha = std::sin(w0) / (2.0f * kQ);
        const float a0 = 1.0f + alpha;

        m_scLpfB0 = ((1.0f - cosW0) * 0.5f) / a0;
        m_scLpfB1 = (1.0f - cosW0) / a0;
        m_scLpfB2 = ((1.0f - cosW0) * 0.5f) / a0;
        m_scLpfA1 = (-2.0f * cosW0) / a0;
        m_scLpfA2 = (1.0f - alpha) / a0;
    }
    else
    {
        m_scLpfB0 = 1.0f; m_scLpfB1 = 0.0f; m_scLpfB2 = 0.0f;
        m_scLpfA1 = 0.0f; m_scLpfA2 = 0.0f;
    }
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
    m_gateEnvelopeDb = 0.0f;
    for (auto& s : m_scHpfState) s.reset();
    for (auto& s : m_scLpfState) s.reset();
    m_gainReductionDb.store(0.0f, std::memory_order_relaxed);
    m_gateReductionDb.store(0.0f, std::memory_order_relaxed);
}

void DynamicsCompressorNode::process(ProcessContext& context) noexcept
{
    if (m_settings.bypassed || context.audio.channels == nullptr || context.audio.frameCount == 0)
    {
        m_gainReductionDb.store(0.0f, std::memory_order_relaxed);
        m_gateReductionDb.store(0.0f, std::memory_order_relaxed);
        return;
    }

    const std::size_t channelCount = context.audio.channelCount;
    const std::size_t frameCount = context.audio.frameCount;

    const bool useExternalSidechain = m_settings.sidechainEnabled
        && context.sidechain.channels != nullptr
        && context.sidechain.channelCount > 0;
    float* const* const detectorChannels = useExternalSidechain ? context.sidechain.channels : context.audio.channels;
    const std::size_t detectorChannelCount = useExternalSidechain ? context.sidechain.channelCount : channelCount;

    // Sidechain Listen mode: replace audio output directly with filtered key signal!
    if (m_settings.sidechainListen)
    {
        for (std::size_t ch = 0; ch < channelCount; ++ch)
        {
            if (context.audio.channels[ch] == nullptr)
                continue;

            const std::size_t srcCh = std::min(ch, detectorChannelCount - 1);
            const float* const src = (detectorChannels[srcCh] != nullptr) ? detectorChannels[srcCh] : detectorChannels[0];
            auto& hpf = m_scHpfState[ch % 2];
            auto& lpf = m_scLpfState[ch % 2];

            for (std::size_t i = 0; i < frameCount; ++i)
            {
                float s = (src != nullptr) ? src[i] : 0.0f;
                s = hpf.process(s, m_scHpfB0, m_scHpfB1, m_scHpfB2, m_scHpfA1, m_scHpfA2);
                s = lpf.process(s, m_scLpfB0, m_scLpfB1, m_scLpfB2, m_scLpfA1, m_scLpfA2);
                context.audio.channels[ch][i] = s;
            }
        }
        m_gainReductionDb.store(0.0f, std::memory_order_relaxed);
        m_gateReductionDb.store(0.0f, std::memory_order_relaxed);
        return;
    }

    const float threshold = m_settings.thresholdDb;
    const float ratio = std::max(1.0f, m_settings.ratio);
    const float knee = std::max(0.0f, m_settings.kneeDb);
    const float halfKnee = knee * 0.5f;
    const float slopeFactor = 1.0f - (1.0f / ratio);

    const bool gateOn = m_settings.gateEnabled;
    const float gateThresh = m_settings.gateThresholdDb;
    const float gateRatio = std::max(1.0f, m_settings.gateRatio);
    const float gateSlope = 1.0f - (1.0f / gateRatio);

    float maxCompReduction = 0.0f;
    float maxGateReduction = 0.0f;

    for (std::size_t i = 0; i < frameCount; ++i)
    {
        // Level detector (peak across stereo detector channels with sidechain HPF/LPF)
        float peakAbs = 0.0f;
        for (std::size_t ch = 0; ch < detectorChannelCount; ++ch)
        {
            if (detectorChannels[ch] != nullptr)
            {
                float s = detectorChannels[ch][i];
                auto& hpf = m_scHpfState[ch % 2];
                auto& lpf = m_scLpfState[ch % 2];
                s = hpf.process(s, m_scHpfB0, m_scHpfB1, m_scHpfB2, m_scHpfA1, m_scHpfA2);
                s = lpf.process(s, m_scLpfB0, m_scLpfB1, m_scLpfB2, m_scLpfA1, m_scLpfA2);
                peakAbs = std::max(peakAbs, std::abs(s));
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

        // Ballistics envelope smoothing for compressor
        if (targetReductionDb > m_envelopeDb)
        {
            m_envelopeDb = m_attackCoeff * m_envelopeDb + (1.0f - m_attackCoeff) * targetReductionDb;
        }
        else
        {
            m_envelopeDb = m_releaseCoeff * m_envelopeDb + (1.0f - m_releaseCoeff) * targetReductionDb;
        }

        maxCompReduction = std::max(maxCompReduction, m_envelopeDb);

        // Downward Gate / Expander
        float targetGateRedDb = 0.0f;
        if (gateOn)
        {
            const float undershoot = gateThresh - inputDb;
            if (undershoot > 0.0f)
            {
                targetGateRedDb = std::min(60.0f, undershoot * gateSlope);
            }

            if (targetGateRedDb > m_gateEnvelopeDb)
            {
                // Gate opening / closing ballistics
                m_gateEnvelopeDb = m_gateAttackCoeff * m_gateEnvelopeDb + (1.0f - m_gateAttackCoeff) * targetGateRedDb;
            }
            else
            {
                m_gateEnvelopeDb = m_gateReleaseCoeff * m_gateEnvelopeDb + (1.0f - m_gateReleaseCoeff) * targetGateRedDb;
            }
            maxGateReduction = std::max(maxGateReduction, m_gateEnvelopeDb);
        }

        // Combined gain reduction linear factor
        const float totalReductionDb = m_envelopeDb + (gateOn ? m_gateEnvelopeDb : 0.0f);
        const float gainLinear = std::pow(10.0f, -totalReductionDb / 20.0f) * m_makeupLinear;

        for (std::size_t ch = 0; ch < channelCount; ++ch)
        {
            if (context.audio.channels[ch] != nullptr)
            {
                context.audio.channels[ch][i] *= gainLinear;
            }
        }
    }

    m_gainReductionDb.store(maxCompReduction, std::memory_order_relaxed);
    m_gateReductionDb.store(maxGateReduction, std::memory_order_relaxed);
}

} // namespace dawg::engine::dsp
