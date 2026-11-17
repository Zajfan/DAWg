#include <dawg/engine/dsp/MultiTrackPhaseAlignerNode.h>

#include <algorithm>
#include <cmath>

namespace dawg::engine::dsp {

MultiTrackPhaseAlignerNode::MultiTrackPhaseAlignerNode() noexcept
{
    reset();
}

bool MultiTrackPhaseAlignerNode::prepare(
    double sampleRate,
    [[maybe_unused]] std::size_t maxBlockSize,
    [[maybe_unused]] std::uint16_t channelCount)
{
    m_sampleRate = (sampleRate > 1000.0) ? sampleRate : 48000.0;
    reset();
    return true;
}

void MultiTrackPhaseAlignerNode::reset() noexcept
{
    m_delayLineL.fill(0.0f);
    m_delayLineR.fill(0.0f);
    m_writeIdx = 0;
}

void MultiTrackPhaseAlignerNode::setDelaySamplesL(float samples) noexcept
{
    m_delaySamplesL = std::clamp(samples, 0.0f, static_cast<float>(kMaxDelaySamples - 10));
}

void MultiTrackPhaseAlignerNode::setDelaySamplesR(float samples) noexcept
{
    m_delaySamplesR = std::clamp(samples, 0.0f, static_cast<float>(kMaxDelaySamples - 10));
}

void MultiTrackPhaseAlignerNode::process(ProcessContext& context) noexcept
{
    if (!m_enabled || context.audio.channelCount == 0 || context.audio.channels[0] == nullptr)
        return;

    const std::size_t numSamples = context.audio.frameCount;
    float* const chL = context.audio.channels[0];
    float* const chR = (context.audio.channelCount > 1 && context.audio.channels[1] != nullptr)
                           ? context.audio.channels[1]
                           : nullptr;

    const float signL = m_polarityInvertL ? -1.0f : 1.0f;
    const float signR = m_polarityInvertR ? -1.0f : 1.0f;

    for (std::size_t s = 0; s < numSamples; ++s)
    {
        const float inL = chL[s];
        const float inR = (chR != nullptr) ? chR[s] : inL;

        // Push into ring buffer
        m_delayLineL[m_writeIdx] = inL;
        m_delayLineR[m_writeIdx] = inR;

        // Read fractional sub-sample delay
        const float outL = readFractionalDelay(m_delayLineL, m_writeIdx, m_delaySamplesL) * signL;
        const float outR = readFractionalDelay(m_delayLineR, m_writeIdx, m_delaySamplesR) * signR;

        chL[s] = outL;
        if (chR != nullptr)
            chR[s] = outR;

        m_writeIdx = (m_writeIdx + 1) % kMaxDelaySamples;
    }
}

float MultiTrackPhaseAlignerNode::computeCorrelationCoefficient(
    const float* chA,
    const float* chB,
    std::size_t count) noexcept
{
    if (chA == nullptr || chB == nullptr || count == 0)
        return 0.0f;

    float sumA = 0.0f;
    float sumB = 0.0f;
    for (std::size_t i = 0; i < count; ++i)
    {
        sumA += chA[i];
        sumB += chB[i];
    }
    const float meanA = sumA / static_cast<float>(count);
    const float meanB = sumB / static_cast<float>(count);

    float num = 0.0f;
    float denA = 0.0f;
    float denB = 0.0f;

    for (std::size_t i = 0; i < count; ++i)
    {
        const float diffA = chA[i] - meanA;
        const float diffB = chB[i] - meanB;
        num += diffA * diffB;
        denA += diffA * diffA;
        denB += diffB * diffB;
    }

    const float den = std::sqrt(denA * denB);
    return (den > 1e-9f) ? std::clamp(num / den, -1.0f, 1.0f) : 0.0f;
}

PhaseAlignmentAnalysis MultiTrackPhaseAlignerNode::analyzeAlignment(
    const float* refChannel,
    const float* targetChannel,
    std::size_t sampleCount,
    double sampleRate,
    std::size_t maxDelaySearchSamples) noexcept
{
    PhaseAlignmentAnalysis result;
    if (refChannel == nullptr || targetChannel == nullptr || sampleCount < 128)
        return result;

    result.correlationBefore = computeCorrelationCoefficient(refChannel, targetChannel, sampleCount);

    const std::size_t searchRadius = std::min(maxDelaySearchSamples, sampleCount / 2);

    float bestCorr = -2.0f;
    std::int64_t bestLag = 0;
    bool bestInvert = false;

    // Normalizing energy
    float energyRef = 0.0f;
    float energyTarget = 0.0f;
    for (std::size_t i = 0; i < sampleCount; ++i)
    {
        energyRef += refChannel[i] * refChannel[i];
        energyTarget += targetChannel[i] * targetChannel[i];
    }
    const float denom = std::sqrt(energyRef * energyTarget);
    if (denom < 1e-9f)
        return result;

    // Compute cross-correlation for lag in [-searchRadius, +searchRadius]
    auto computeCrossCorrAtLag = [&](std::int64_t lag) -> float {
        float sum = 0.0f;
        const std::int64_t start = std::max<std::int64_t>(0, -lag);
        const auto end = static_cast<std::int64_t>(std::min<std::size_t>(sampleCount, sampleCount - lag));
        for (std::int64_t i = start; i < end; ++i)
        {
            sum += refChannel[i] * targetChannel[i + lag];
        }
        return sum / denom;
    };

    for (std::int64_t lag = -static_cast<std::int64_t>(searchRadius); lag <= static_cast<std::int64_t>(searchRadius); ++lag)
    {
        const float corr = computeCrossCorrAtLag(lag);
        const float absCorr = std::abs(corr);

        if (absCorr > bestCorr)
        {
            bestCorr = absCorr;
            bestLag = lag;
            bestInvert = (corr < 0.0f); // Recommend polarity inversion if peak correlation is negative
        }
    }

    // Sub-sample parabolic interpolation around peak lag
    float delta = 0.0f;
    if (bestLag > -static_cast<std::int64_t>(searchRadius) && bestLag < static_cast<std::int64_t>(searchRadius))
    {
        const float ym1 = std::abs(computeCrossCorrAtLag(bestLag - 1));
        const float y0  = bestCorr;
        const float yp1 = std::abs(computeCrossCorrAtLag(bestLag + 1));
        const float d = 2.0f * (ym1 - 2.0f * y0 + yp1);
        if (std::abs(d) > 1e-9f)
        {
            delta = (ym1 - yp1) / d;
            delta = std::clamp(delta, -0.5f, 0.5f);
        }
    }

    const float subSampleOptimalLag = static_cast<float>(bestLag) + delta;
    result.optimalDelaySamples = std::abs(subSampleOptimalLag);
    result.optimalDelayMs = (result.optimalDelaySamples / static_cast<float>(sampleRate)) * 1000.0f;
    result.recommendedPolarityInvert = bestInvert;
    result.correlationAfter = bestCorr;
    result.alignmentScorePercent = std::clamp((bestCorr + 1.0f) * 50.0f, 0.0f, 100.0f);

    return result;
}

} // namespace dawg::engine::dsp
