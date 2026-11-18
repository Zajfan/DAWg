#include <dawg/engine/dsp/VocalSurgicalDeEsserNode.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::engine::dsp {

namespace {

constexpr float pi = std::numbers::pi_v<float>;

} // namespace

VocalSurgicalDeEsserNode::VocalSurgicalDeEsserNode() noexcept
{
    updateCrossoverCoeffs();
    updateFormantCoeffs();
}

bool VocalSurgicalDeEsserNode::prepare(double sampleRate, std::size_t /*maxBlockSize*/, std::uint16_t channelCount)
{
    m_sampleRate = (sampleRate > 0.0) ? sampleRate : 48000.0;
    m_channelCount = channelCount;
    reset();
    setLookaheadMs(m_lookaheadMs);
    updateCrossoverCoeffs();
    updateFormantCoeffs();
    return true;
}

void VocalSurgicalDeEsserNode::reset() noexcept
{
    m_delayBufferL.fill(0.0f);
    m_delayBufferR.fill(0.0f);
    m_lowDelayBufferL.fill(0.0f);
    m_lowDelayBufferR.fill(0.0f);
    m_highDelayBufferL.fill(0.0f);
    m_highDelayBufferR.fill(0.0f);
    m_lookaheadWriteIdx = 0;

    m_crossoverStateL.reset();
    m_crossoverStateR.reset();

    for (auto& s : m_formantStatesL) s.reset();
    for (auto& s : m_formantStatesR) s.reset();

    m_envFollower = 0.0f;
    m_lastGainReductionDb.store(0.0f, std::memory_order_relaxed);
    m_breathGainSmooth = 1.0f;
    m_zeroCrossRate = 0.0f;
    m_prevSampleL = 0.0f;
    m_isBreathDetected.store(false, std::memory_order_relaxed);
}

void VocalSurgicalDeEsserNode::setCrossoverFrequency(float freqHz) noexcept
{
    m_crossoverFreq = std::clamp(freqHz, 3000.0f, 12000.0f);
    updateCrossoverCoeffs();
}

void VocalSurgicalDeEsserNode::setLookaheadMs(float ms) noexcept
{
    m_lookaheadMs = std::clamp(ms, 0.0f, 5.0f);
    const auto frames = static_cast<std::size_t>((m_lookaheadMs * 0.001f) * static_cast<float>(m_sampleRate));
    m_lookaheadFrames = std::clamp(frames, std::size_t{1}, maxLookaheadFrames - 1);
}

void VocalSurgicalDeEsserNode::setFormantShiftSemitones(float semitones) noexcept
{
    m_formantShiftSemitones = std::clamp(semitones, -12.0f, 12.0f);
    updateFormantCoeffs();
}

void VocalSurgicalDeEsserNode::setThroatLengthScale(float scale) noexcept
{
    m_throatLengthScale = std::clamp(scale, 0.6f, 1.5f);
    updateFormantCoeffs();
}

void VocalSurgicalDeEsserNode::updateCrossoverCoeffs() noexcept
{
    // Butterworth 2nd-order stages (cascade of 2 yields Linkwitz-Riley 4th-order)
    const float w0 = 2.0f * pi * std::clamp(m_crossoverFreq, 1000.0f, static_cast<float>(m_sampleRate * 0.45)) / static_cast<float>(m_sampleRate);
    const float cosW = std::cos(w0);
    const float sinW = std::sin(w0);
    const float alpha = sinW / (2.0f * 0.70710678f);

    // Lowpass
    const float lpB0 = (1.0f - cosW) * 0.5f;
    const float lpB1 = 1.0f - cosW;
    const float lpB2 = (1.0f - cosW) * 0.5f;
    const float lpA0 = 1.0f + alpha;
    const float lpA1 = -2.0f * cosW;
    const float lpA2 = 1.0f - alpha;

    const float invLpA0 = 1.0f / lpA0;
    m_lrB0_lp = lpB0 * invLpA0;
    m_lrB1_lp = lpB1 * invLpA0;
    m_lrB2_lp = lpB2 * invLpA0;
    m_lrA1_lp = lpA1 * invLpA0;
    m_lrA2_lp = lpA2 * invLpA0;

    // Highpass
    const float hpB0 = (1.0f + cosW) * 0.5f;
    const float hpB1 = -(1.0f + cosW);
    const float hpB2 = (1.0f + cosW) * 0.5f;
    const float hpA0 = 1.0f + alpha;
    const float hpA1 = -2.0f * cosW;
    const float hpA2 = 1.0f - alpha;

    const float invHpA0 = 1.0f / hpA0;
    m_lrB0_hp = hpB0 * invHpA0;
    m_lrB1_hp = hpB1 * invHpA0;
    m_lrB2_hp = hpB2 * invHpA0;
    m_lrA1_hp = hpA1 * invHpA0;
    m_lrA2_hp = hpA2 * invHpA0;
}

void VocalSurgicalDeEsserNode::updateFormantCoeffs() noexcept
{
    // Typical vowel formants F1, F2, F3
    constexpr std::array<float, 3> baseFreqs = {700.0f, 1800.0f, 2800.0f};
    constexpr std::array<float, 3> qFactors = {3.5f, 4.0f, 5.0f};

    const float shiftFactor = std::pow(2.0f, m_formantShiftSemitones / 12.0f) / m_throatLengthScale;

    for (std::size_t i = 0; i < 3; ++i)
    {
        const float f = std::clamp(baseFreqs[i] * shiftFactor, 100.0f, static_cast<float>(m_sampleRate * 0.45));
        const float w0 = 2.0f * pi * f / static_cast<float>(m_sampleRate);
        const float sinW = std::sin(w0);
        const float cosW = std::cos(w0);
        const float alpha = sinW / (2.0f * qFactors[i]);

        const float b0 = alpha;
        const float b1 = 0.0f;
        const float b2 = -alpha;
        const float a0 = 1.0f + alpha;
        const float a1 = -2.0f * cosW;
        const float a2 = 1.0f - alpha;

        const float invA0 = 1.0f / a0;
        m_formantCoeffs[i] = FormantBiquad {
            .b0 = b0 * invA0,
            .b1 = b1 * invA0,
            .b2 = b2 * invA0,
            .a1 = a1 * invA0,
            .a2 = a2 * invA0
        };
    }
}

void VocalSurgicalDeEsserNode::processCrossover(float in, float& low, float& high, Lr4FilterState& s) const noexcept
{
    // Lowpass stage 1
    const float lp1 = m_lrB0_lp * in + s.z1_lp1;
    s.z1_lp1 = m_lrB1_lp * in - m_lrA1_lp * lp1 + s.z2_lp1;
    s.z2_lp1 = m_lrB2_lp * in - m_lrA2_lp * lp1;

    // Lowpass stage 2
    low = m_lrB0_lp * lp1 + s.z1_lp2;
    s.z1_lp2 = m_lrB1_lp * lp1 - m_lrA1_lp * low + s.z2_lp2;
    s.z2_lp2 = m_lrB2_lp * lp1 - m_lrA2_lp * low;

    // Highpass stage 1
    const float hp1 = m_lrB0_hp * in + s.z1_hp1;
    s.z1_hp1 = m_lrB1_hp * in - m_lrA1_hp * hp1 + s.z2_hp1;
    s.z2_hp1 = m_lrB2_hp * in - m_lrA2_hp * hp1;

    // Highpass stage 2
    high = m_lrB0_hp * hp1 + s.z1_hp2;
    s.z1_hp2 = m_lrB1_hp * hp1 - m_lrA1_hp * high + s.z2_hp2;
    s.z2_hp2 = m_lrB2_hp * hp1 - m_lrA2_hp * high;
}

float VocalSurgicalDeEsserNode::processFormants(float in, std::array<FormantFilterState, 3>& states) const noexcept
{
    float res = 0.0f;
    for (std::size_t i = 0; i < 3; ++i)
    {
        const auto& c = m_formantCoeffs[i];
        auto& s = states[i];
        const float out = c.b0 * in + s.z1;
        s.z1 = c.b1 * in - c.a1 * out + s.z2;
        s.z2 = c.b2 * in - c.a2 * out;
        res += out;
    }
    return in * (1.0f - m_formantMix) + (in + res * 1.5f) * m_formantMix;
}

void VocalSurgicalDeEsserNode::process(ProcessContext& context) noexcept
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

    const float attackCoeff = std::exp(-1.0f / (0.001f * static_cast<float>(m_sampleRate))); // 1ms attack
    const float releaseCoeff = std::exp(-1.0f / (0.050f * static_cast<float>(m_sampleRate))); // 50ms release
    const float thresholdLinear = std::pow(10.0f, m_thresholdDb / 20.0f);
    const float breathTargetLinear = std::pow(10.0f, m_breathReductionDb / 20.0f);

    float maxGrDbThisBlock = 0.0f;
    bool anyBreathThisBlock = false;

    for (std::size_t i = 0; i < blockFrames; ++i)
    {
        const float inL = left[i];
        const float inR = right[i];

        // 1. Crossover split
        float lowL = 0.0f, highL = 0.0f;
        float lowR = 0.0f, highR = 0.0f;
        processCrossover(inL, lowL, highL, m_crossoverStateL);
        processCrossover(inR, lowR, highR, m_crossoverStateR);

        // 2. Store into lookahead delay buffers
        m_delayBufferL[m_lookaheadWriteIdx] = inL;
        m_delayBufferR[m_lookaheadWriteIdx] = inR;
        m_lowDelayBufferL[m_lookaheadWriteIdx] = lowL;
        m_lowDelayBufferR[m_lookaheadWriteIdx] = lowR;
        m_highDelayBufferL[m_lookaheadWriteIdx] = highL;
        m_highDelayBufferR[m_lookaheadWriteIdx] = highR;

        // 3. Sidechain Envelope Detection on current highBand
        const float highAbs = std::max(std::abs(highL), std::abs(highR));
        if (highAbs > m_envFollower)
        {
            m_envFollower = attackCoeff * m_envFollower + (1.0f - attackCoeff) * highAbs;
        }
        else
        {
            m_envFollower = releaseCoeff * m_envFollower + (1.0f - releaseCoeff) * highAbs;
        }

        // Calculate De-Esser Gain Reduction
        float gainReduction = 1.0f;
        if (m_envFollower > thresholdLinear && thresholdLinear > 0.0001f)
        {
            const float envDb = 20.0f * std::log10(m_envFollower);
            const float overDb = envDb - m_thresholdDb;
            const float grDb = overDb * (1.0f - (1.0f / m_ratio));
            gainReduction = std::pow(10.0f, -grDb / 20.0f);
            maxGrDbThisBlock = std::max(maxGrDbThisBlock, grDb);
        }

        // 4. Breath Detection
        const bool signCross = ((inL >= 0.0f && m_prevSampleL < 0.0f) || (inL < 0.0f && m_prevSampleL >= 0.0f));
        m_prevSampleL = inL;
        m_zeroCrossRate += ((signCross ? 1.0f : 0.0f) - m_zeroCrossRate) * 0.01f;

        const float inRms = std::abs(inL);
        // Breaths characterized by high zero crossing (>0.15), RMS between -50dB and -20dB, and high energy in high vs low
        const bool isBreath = m_breathAttenEnabled &&
                              (m_zeroCrossRate > (0.25f * (1.1f - m_breathSensitivity))) &&
                              (inRms > 0.003f && inRms < 0.15f) &&
                              (std::abs(highL) > std::abs(lowL) * 0.5f);

        if (isBreath)
        {
            anyBreathThisBlock = true;
            m_breathGainSmooth += (breathTargetLinear - m_breathGainSmooth) * 0.005f; // Smooth dip
        }
        else
        {
            m_breathGainSmooth += (1.0f - m_breathGainSmooth) * 0.005f; // Smooth recovery
        }

        // 5. Retrieve delayed audio from lookahead buffer
        const std::size_t readIdx = (m_lookaheadWriteIdx + maxLookaheadFrames - m_lookaheadFrames) % maxLookaheadFrames;
        const float delayedInL = m_delayBufferL[readIdx];
        const float delayedInR = m_delayBufferR[readIdx];
        const float delayedLowL = m_lowDelayBufferL[readIdx];
        const float delayedLowR = m_lowDelayBufferR[readIdx];
        const float delayedHighL = m_highDelayBufferL[readIdx];
        const float delayedHighR = m_highDelayBufferR[readIdx];

        m_lookaheadWriteIdx = (m_lookaheadWriteIdx + 1) % maxLookaheadFrames;

        float outL = 0.0f;
        float outR = 0.0f;

        switch (m_mode)
        {
            case DeEsserMode::splitBand:
            {
                // Only highband is attenuated; lowband is preserved with exact phase reconstruction
                outL = delayedLowL + (delayedHighL * gainReduction);
                outR = delayedLowR + (delayedHighR * gainReduction);
                break;
            }
            case DeEsserMode::wideband:
            {
                outL = delayedInL * gainReduction;
                outR = delayedInR * gainReduction;
                break;
            }
            case DeEsserMode::auditionDelta:
            {
                // Listen strictly to the sibilance that is removed
                outL = delayedHighL * (1.0f - gainReduction);
                outR = delayedHighR * (1.0f - gainReduction);
                break;
            }
        }

        // Apply breath attenuation
        outL *= m_breathGainSmooth;
        outR *= m_breathGainSmooth;

        // Apply formant / throat resonator
        if (m_formantMix > 0.001f)
        {
            outL = processFormants(outL, m_formantStatesL);
            outR = processFormants(outR, m_formantStatesR);
        }

        left[i] = outL;
        if (right != left)
        {
            right[i] = outR;
        }
    }

    m_lastGainReductionDb.store(maxGrDbThisBlock, std::memory_order_relaxed);
    m_isBreathDetected.store(anyBreathThisBlock, std::memory_order_relaxed);
}

} // namespace dawg::engine::dsp
