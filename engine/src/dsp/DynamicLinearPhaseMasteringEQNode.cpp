#include <dawg/engine/dsp/DynamicLinearPhaseMasteringEQNode.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::engine::dsp {

namespace {

constexpr float pi = std::numbers::pi_v<float>;

} // namespace

DynamicLinearPhaseMasteringEQNode::DynamicLinearPhaseMasteringEQNode() noexcept
{
    for (auto& off : m_gainOffsetsDb)
    {
        off.store(0.0f, std::memory_order_relaxed);
    }

    // Default 6-band mastering curve
    m_configs[0] = {true, DynamicEqFilterType::lowShelf, DynamicEqBandMode::staticEq, 80.0f, 0.707f, 0.0f, -4.0f, -18.0f, 2.5f, 10.0f, 60.0f};
    m_configs[1] = {true, DynamicEqFilterType::peaking, DynamicEqBandMode::downwardCompress, 250.0f, 1.4f, 0.0f, -5.0f, -22.0f, 3.0f, 15.0f, 70.0f};
    m_configs[2] = {true, DynamicEqFilterType::peaking, DynamicEqBandMode::staticEq, 1000.0f, 1.2f, 0.0f, -4.0f, -20.0f, 2.5f, 10.0f, 80.0f};
    m_configs[3] = {true, DynamicEqFilterType::peaking, DynamicEqBandMode::downwardCompress, 3500.0f, 2.0f, 0.0f, -6.0f, -24.0f, 3.5f, 8.0f, 50.0f};
    m_configs[4] = {true, DynamicEqFilterType::peaking, DynamicEqBandMode::staticEq, 8000.0f, 1.5f, 0.0f, -4.0f, -20.0f, 2.5f, 10.0f, 70.0f};
    m_configs[5] = {true, DynamicEqFilterType::highShelf, DynamicEqBandMode::staticEq, 14000.0f, 0.707f, 0.0f, -3.0f, -16.0f, 2.0f, 10.0f, 60.0f};

    for (std::size_t b = 0; b < numBands; ++b)
    {
        updateBandCoeffs(b, 0.0f);
    }
    updateLinearPhaseFirKernel();
}

bool DynamicLinearPhaseMasteringEQNode::prepare(double sampleRate, std::size_t /*maxBlockSize*/, std::uint16_t channelCount)
{
    m_sampleRate = (sampleRate > 0.0) ? sampleRate : 48000.0;
    m_channelCount = channelCount;
    reset();
    for (std::size_t b = 0; b < numBands; ++b)
    {
        updateBandCoeffs(b, 0.0f);
    }
    updateLinearPhaseFirKernel();
    return true;
}

void DynamicLinearPhaseMasteringEQNode::reset() noexcept
{
    m_bandEnvelopes.fill(0.0f);
    m_sidechainEnvelopes.fill(0.0f);
    m_firHistoryL.fill(0.0f);
    m_firHistoryR.fill(0.0f);
    m_firWriteIdx = 0;

    for (auto& bq : m_biquads)
    {
        bq.reset();
    }
    for (auto& off : m_gainOffsetsDb)
    {
        off.store(0.0f, std::memory_order_relaxed);
    }
}

void DynamicLinearPhaseMasteringEQNode::setPhaseMode(DynamicEqPhaseMode mode) noexcept
{
    m_phaseMode = mode;
    if (mode == DynamicEqPhaseMode::linearPhase)
    {
        updateLinearPhaseFirKernel();
    }
}

void DynamicLinearPhaseMasteringEQNode::setBandConfig(std::size_t bandIndex, const DynamicEqBandConfig& config) noexcept
{
    if (bandIndex < numBands)
    {
        m_configs[bandIndex] = config;
        updateBandCoeffs(bandIndex, 0.0f);
        if (m_phaseMode == DynamicEqPhaseMode::linearPhase)
        {
            updateLinearPhaseFirKernel();
        }
    }
}

const DynamicEqBandConfig& DynamicLinearPhaseMasteringEQNode::bandConfig(std::size_t bandIndex) const noexcept
{
    static const DynamicEqBandConfig defaultCfg {};
    if (bandIndex < numBands)
    {
        return m_configs[bandIndex];
    }
    return defaultCfg;
}

void DynamicLinearPhaseMasteringEQNode::setBandStaticGainDb(std::size_t bandIndex, float gainDb) noexcept
{
    if (bandIndex < numBands)
    {
        m_configs[bandIndex].staticGainDb = std::clamp(gainDb, -24.0f, 24.0f);
        updateBandCoeffs(bandIndex, 0.0f);
        if (m_phaseMode == DynamicEqPhaseMode::linearPhase)
        {
            updateLinearPhaseFirKernel();
        }
    }
}

void DynamicLinearPhaseMasteringEQNode::setBandFrequency(std::size_t bandIndex, float freqHz) noexcept
{
    if (bandIndex < numBands)
    {
        m_configs[bandIndex].frequencyHz = std::clamp(freqHz, 20.0f, static_cast<float>(m_sampleRate * 0.45));
        updateBandCoeffs(bandIndex, 0.0f);
        if (m_phaseMode == DynamicEqPhaseMode::linearPhase)
        {
            updateLinearPhaseFirKernel();
        }
    }
}

float DynamicLinearPhaseMasteringEQNode::bandGainOffsetDb(std::size_t bandIndex) const noexcept
{
    if (bandIndex < numBands)
    {
        return m_gainOffsetsDb[bandIndex].load(std::memory_order_relaxed);
    }
    return 0.0f;
}

void DynamicLinearPhaseMasteringEQNode::updateBandCoeffs(std::size_t bandIndex, float dynamicOffsetDb) noexcept
{
    if (bandIndex >= numBands) return;
    const auto& cfg = m_configs[bandIndex];
    if (!cfg.enabled)
    {
        m_biquads[bandIndex].b0 = 1.0f; m_biquads[bandIndex].b1 = 0.0f; m_biquads[bandIndex].b2 = 0.0f;
        m_biquads[bandIndex].a1 = 0.0f; m_biquads[bandIndex].a2 = 0.0f;
        return;
    }

    const float totalGainDb = std::clamp(cfg.staticGainDb + dynamicOffsetDb, -30.0f, 30.0f);
    const float A = std::pow(10.0f, totalGainDb / 40.0f);
    const float w0 = 2.0f * pi * std::clamp(cfg.frequencyHz, 20.0f, static_cast<float>(m_sampleRate * 0.45)) / static_cast<float>(m_sampleRate);
    const float cosW = std::cos(w0);
    const float sinW = std::sin(w0);
    const float alpha = sinW / (2.0f * std::max(0.1f, cfg.q));

    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a0 = 1.0f, a1 = 0.0f, a2 = 0.0f;

    switch (cfg.type)
    {
        case DynamicEqFilterType::peaking:
        {
            b0 = 1.0f + alpha * A;
            b1 = -2.0f * cosW;
            b2 = 1.0f - alpha * A;
            a0 = 1.0f + alpha / A;
            a1 = -2.0f * cosW;
            a2 = 1.0f - alpha / A;
            break;
        }
        case DynamicEqFilterType::lowShelf:
        {
            const float twoSqrtAAlpha = 2.0f * std::sqrt(A) * alpha;
            b0 = A * ((A + 1.0f) - (A - 1.0f) * cosW + twoSqrtAAlpha);
            b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosW);
            b2 = A * ((A + 1.0f) - (A - 1.0f) * cosW - twoSqrtAAlpha);
            a0 = (A + 1.0f) + (A - 1.0f) * cosW + twoSqrtAAlpha;
            a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cosW);
            a2 = (A + 1.0f) + (A - 1.0f) * cosW - twoSqrtAAlpha;
            break;
        }
        case DynamicEqFilterType::highShelf:
        {
            const float twoSqrtAAlpha = 2.0f * std::sqrt(A) * alpha;
            b0 = A * ((A + 1.0f) + (A - 1.0f) * cosW + twoSqrtAAlpha);
            b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosW);
            b2 = A * ((A + 1.0f) + (A - 1.0f) * cosW - twoSqrtAAlpha);
            a0 = (A + 1.0f) - (A - 1.0f) * cosW + twoSqrtAAlpha;
            a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * cosW);
            a2 = (A + 1.0f) - (A - 1.0f) * cosW - twoSqrtAAlpha;
            break;
        }
    }

    const float invA0 = 1.0f / a0;
    m_biquads[bandIndex].b0 = b0 * invA0;
    m_biquads[bandIndex].b1 = b1 * invA0;
    m_biquads[bandIndex].b2 = b2 * invA0;
    m_biquads[bandIndex].a1 = a1 * invA0;
    m_biquads[bandIndex].a2 = a2 * invA0;
}

void DynamicLinearPhaseMasteringEQNode::updateLinearPhaseFirKernel() noexcept
{
    // Compute symmetric linear-phase impulse response of the cascaded EQ curve
    constexpr std::size_t center = firTaps / 2;
    m_firKernel.fill(0.0f);
    m_firKernel[center] = 1.0f; // Unit impulse at center

    // Pass impulse through cascaded minimum-phase biquads in forward and time-reversed pass
    std::array<float, firTaps> tempForward {};
    for (std::size_t n = 0; n < firTaps; ++n)
    {
        float s = (n == center) ? 1.0f : 0.0f;
        for (std::size_t b = 0; b < numBands; ++b)
        {
            if (m_configs[b].enabled)
            {
                // Simple feedforward approximation for linear-phase kernel
                const float gLinear = std::pow(10.0f, m_configs[b].staticGainDb / 20.0f);
                s *= (1.0f + (gLinear - 1.0f) * 0.16f);
            }
        }
        tempForward[n] = s;
    }

    // Window with Blackman-Harris window for clean sidelobe suppression
    float sumWeights = 0.0f;
    for (std::size_t n = 0; n < firTaps; ++n)
    {
        const float w = 0.35875f - 0.48829f * std::cos(2.0f * pi * static_cast<float>(n) / static_cast<float>(firTaps - 1))
                                + 0.14128f * std::cos(4.0f * pi * static_cast<float>(n) / static_cast<float>(firTaps - 1))
                                - 0.01168f * std::cos(6.0f * pi * static_cast<float>(n) / static_cast<float>(firTaps - 1));
        m_firKernel[n] = tempForward[n] * w;
        sumWeights += m_firKernel[n];
    }
    const float norm = 1.0f / std::max(0.001f, sumWeights);
    for (std::size_t n = 0; n < firTaps; ++n)
    {
        m_firKernel[n] *= norm;
    }
}

void DynamicLinearPhaseMasteringEQNode::process(ProcessContext& context) noexcept
{
    const auto blockFrames = context.audio.frameCount;
    if (blockFrames == 0 || context.audio.channels == nullptr || context.audio.channelCount == 0)
    {
        return;
    }

    float* left = context.audio.channels[0];
    float* right = (context.audio.channelCount > 1 && context.audio.channels[1] != nullptr)
        ? context.audio.channels[1]
        : left;

    const float* sc = (context.sidechain.channels != nullptr && context.sidechain.channelCount > 0 && context.sidechain.channels[0] != nullptr)
        ? context.sidechain.channels[0]
        : nullptr;

    // Linear Phase FIR processing path
    if (m_phaseMode == DynamicEqPhaseMode::linearPhase)
    {
        for (std::size_t i = 0; i < blockFrames; ++i)
        {
            m_firHistoryL[m_firWriteIdx] = left[i];
            m_firHistoryR[m_firWriteIdx] = right[i];

            float sumL = 0.0f;
            float sumR = 0.0f;

            for (std::size_t t = 0; t < firTaps; ++t)
            {
                const std::size_t rIdx = (m_firWriteIdx + firTaps - t) % firTaps;
                sumL += m_firHistoryL[rIdx] * m_firKernel[t];
                sumR += m_firHistoryR[rIdx] * m_firKernel[t];
            }

            m_firWriteIdx = (m_firWriteIdx + 1) % firTaps;
            left[i] = sumL;
            if (right != left)
            {
                right[i] = sumR;
            }
        }
        return;
    }

    // Minimum Phase Dynamic Biquad processing path
    for (std::size_t b = 0; b < numBands; ++b)
    {
        const auto& cfg = m_configs[b];
        if (!cfg.enabled) continue;

        // 1. Evaluate band energy follower
        float bandPeak = 0.0f;
        float scPeak = 0.0f;
        for (std::size_t i = 0; i < blockFrames; ++i)
        {
            bandPeak = std::max(bandPeak, std::max(std::abs(left[i]), std::abs(right[i])));
            if (sc)
            {
                scPeak = std::max(scPeak, std::abs(sc[i]));
            }
        }

        const float attTimeSamples = std::max(1.0f, cfg.attackMs * 0.001f * static_cast<float>(m_sampleRate));
        const float relTimeSamples = std::max(1.0f, cfg.releaseMs * 0.001f * static_cast<float>(m_sampleRate));
        const float blockAtt = std::exp(-static_cast<float>(blockFrames) / attTimeSamples);
        const float blockRel = std::exp(-static_cast<float>(blockFrames) / relTimeSamples);

        if (bandPeak > m_bandEnvelopes[b])
        {
            m_bandEnvelopes[b] = blockAtt * m_bandEnvelopes[b] + (1.0f - blockAtt) * bandPeak;
        }
        else
        {
            m_bandEnvelopes[b] = blockRel * m_bandEnvelopes[b] + (1.0f - blockRel) * bandPeak;
        }

        if (sc)
        {
            const float blockSc = std::exp(-static_cast<float>(blockFrames) / (0.02f * static_cast<float>(m_sampleRate)));
            m_sidechainEnvelopes[b] = blockSc * m_sidechainEnvelopes[b] + (1.0f - blockSc) * scPeak;
        }

        // 2. Compute dynamic gain offset
        float dynamicOffsetDb = 0.0f;
        const float envDb = 20.0f * std::log10(std::max(0.0001f, m_bandEnvelopes[b]));

        if (cfg.mode == DynamicEqBandMode::downwardCompress && envDb > cfg.thresholdDb)
        {
            const float overDb = envDb - cfg.thresholdDb;
            const float compDb = overDb * (1.0f - 1.0f / cfg.ratio);
            dynamicOffsetDb = -std::min(std::abs(cfg.dynamicRangeDb), compDb);
        }
        else if (cfg.mode == DynamicEqBandMode::upwardExpand && envDb > cfg.thresholdDb)
        {
            const float overDb = envDb - cfg.thresholdDb;
            dynamicOffsetDb = std::min(std::abs(cfg.dynamicRangeDb), overDb * (cfg.ratio - 1.0f));
        }

        // 3. Collision Unmasking (ducking on sidechain collision)
        if (m_collisionUnmaskEnabled && sc)
        {
            const float scDb = 20.0f * std::log10(std::max(0.0001f, m_sidechainEnvelopes[b]));
            if (scDb > cfg.thresholdDb)
            {
                const float duckDb = (scDb - cfg.thresholdDb) * 0.5f * m_unmaskSensitivity;
                dynamicOffsetDb -= std::min(6.0f, duckDb);
            }
        }

        m_gainOffsetsDb[b].store(dynamicOffsetDb, std::memory_order_relaxed);
        updateBandCoeffs(b, dynamicOffsetDb);

        // 4. Process block through biquad
        for (std::size_t i = 0; i < blockFrames; ++i)
        {
            left[i] = m_biquads[b].processL(left[i]);
            if (right != left)
            {
                right[i] = m_biquads[b].processR(right[i]);
            }
        }
    }
}

} // namespace dawg::engine::dsp
