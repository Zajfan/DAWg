#include <dawg/engine/dsp/SpectralResonanceTamerNode.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::engine::dsp {

void SpectralResonanceTamerNode::TrackingNotch::updateCoeffs(double sampleRate) noexcept
{
    const float clampedFreq = std::clamp(freqHz, 20.0f, static_cast<float>(sampleRate * 0.45));
    const float w0 = 2.0f * std::numbers::pi_v<float> * clampedFreq / static_cast<float>(sampleRate);
    const float alpha = std::sin(w0) / (2.0f * std::max(q, 0.5f));
    const float v0 = gainLinear;

    // Peaking EQ filter formulation with gainLinear
    const float invV0 = 1.0f / std::max(v0, 0.001f);
    const float rawB0 = 1.0f + alpha * v0;
    const float rawB1 = -2.0f * std::cos(w0);
    const float rawB2 = 1.0f - alpha * v0;
    const float rawA0 = 1.0f + alpha * invV0;
    const float rawA1 = -2.0f * std::cos(w0);
    const float rawA2 = 1.0f - alpha * invV0;

    b0 = rawB0 / rawA0;
    b1 = rawB1 / rawA0;
    b2 = rawB2 / rawA0;
    a1 = rawA1 / rawA0;
    a2 = rawA2 / rawA0;
}

float SpectralResonanceTamerNode::TrackingNotch::processL(float in) noexcept
{
    const float out = b0 * in + z1L;
    z1L = b1 * in - a1 * out + z2L;
    z2L = b2 * in - a2 * out;
    return out;
}

float SpectralResonanceTamerNode::TrackingNotch::processR(float in) noexcept
{
    const float out = b0 * in + z1R;
    z1R = b1 * in - a1 * out + z2R;
    z2R = b2 * in - a2 * out;
    return out;
}

void SpectralResonanceTamerNode::TrackingNotch::reset() noexcept
{
    z1L = 0.0f;
    z2L = 0.0f;
    z1R = 0.0f;
    z2R = 0.0f;
}

SpectralResonanceTamerNode::SpectralResonanceTamerNode() noexcept
{
    updateBandFrequencies();
    setBallistics(1.0f, 35.0f);
    reset();
}

void SpectralResonanceTamerNode::setFrequencyRange(float lowHz, float highHz) noexcept
{
    m_lowFreqHz = std::clamp(lowHz, 100.0f, 8000.0f);
    m_highFreqHz = std::clamp(highHz, m_lowFreqHz + 500.0f, 20000.0f);
    updateBandFrequencies();
}

void SpectralResonanceTamerNode::setBallistics(float attackMs, float releaseMs) noexcept
{
    const double aSamp = (std::max(attackMs, 0.1f) * 0.001) * m_sampleRate;
    m_attackCoeff = static_cast<float>(1.0 - std::exp(-1.0 / aSamp));

    const double rSamp = (std::max(releaseMs, 1.0f) * 0.001) * m_sampleRate;
    m_releaseCoeff = static_cast<float>(1.0 - std::exp(-1.0 / rSamp));
}

void SpectralResonanceTamerNode::updateBandFrequencies() noexcept
{
    const float logLow = std::log10(m_lowFreqHz);
    const float logHigh = std::log10(m_highFreqHz);
    const float step = (logHigh - logLow) / static_cast<float>(analysisBands - 1);

    for (std::size_t i = 0; i < analysisBands; ++i) {
        const float freq = std::pow(10.0f, logLow + static_cast<float>(i) * step);
        m_bandFreqs[i] = freq;

        // Bandpass filter biquad coefficients (Q = 4.0)
        const float w0 = 2.0f * std::numbers::pi_v<float> * freq / static_cast<float>(m_sampleRate);
        const float alpha = std::sin(w0) / (2.0f * 4.0f);
        const float a0 = 1.0f + alpha;

        m_analysisFilters[i].b0 = alpha / a0;
        m_analysisFilters[i].b1 = 0.0f;
        m_analysisFilters[i].b2 = -alpha / a0;
        m_analysisFilters[i].a1 = (-2.0f * std::cos(w0)) / a0;
        m_analysisFilters[i].a2 = (1.0f - alpha) / a0;
    }
}

bool SpectralResonanceTamerNode::prepare(
    double sampleRate,
    [[maybe_unused]] std::size_t maxBlockSize,
    [[maybe_unused]] std::uint16_t channelCount)
{
    m_sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    m_lookaheadSamples = std::min(static_cast<std::size_t>(m_sampleRate * 0.0015), maxLookahead - 1); // ~1.5 ms lookahead
    updateBandFrequencies();
    reset();
    setBallistics(1.0f, 35.0f);
    return true;
}

void SpectralResonanceTamerNode::reset() noexcept
{
    m_lookaheadRingL.fill(0.0f);
    m_lookaheadRingR.fill(0.0f);
    m_lookaheadWritePos = 0;
    m_bandEnergies.fill(1e-6f);
    m_activeSuppressionDb = 0.0f;

    for (auto& f : m_analysisFilters) {
        f.reset();
    }

    for (std::size_t n = 0; n < maxNotches; ++n) {
        m_notches[n].freqHz = m_lowFreqHz + static_cast<float>(n) * 1000.0f;
        m_notches[n].gainLinear = 1.0f;
        m_notches[n].targetGainLinear = 1.0f;
        m_notches[n].q = 8.0f * m_sharpness;
        m_notches[n].updateCoeffs(m_sampleRate);
        m_notches[n].reset();
    }
}

void SpectralResonanceTamerNode::process(ProcessContext& context) noexcept
{
    auto& audio = context.audio;
    if (audio.channels == nullptr || audio.frameCount == 0 || audio.channelCount == 0) {
        return;
    }

    const std::size_t numSamples = audio.frameCount;
    const std::size_t numChannels = audio.channelCount;
    float* ch0 = audio.channels[0];
    float* ch1 = numChannels > 1 ? audio.channels[1] : nullptr;

    const float notchQ = std::clamp(6.0f * m_sharpness, 2.0f, 24.0f);
    float maxCurrentCutDb = 0.0f;

    for (std::size_t s = 0; s < numSamples; ++s) {
        const float inL = ch0[s];
        const float inR = ch1 != nullptr ? ch1[s] : inL;

        // 1. Write current audio to lookahead ring buffer
        m_lookaheadRingL[m_lookaheadWritePos] = inL;
        m_lookaheadRingR[m_lookaheadWritePos] = inR;

        // 2. Read lookahead delayed audio
        const auto readPos = (m_lookaheadWritePos + maxLookahead - m_lookaheadSamples) % maxLookahead;
        const float delayedL = m_lookaheadRingL[readPos];
        const float delayedR = m_lookaheadRingR[readPos];
        m_lookaheadWritePos = (m_lookaheadWritePos + 1) % maxLookahead;

        // 3. Spectral Energy Analysis on input
        const float detectSample = 0.5f * (inL + inR);
        for (std::size_t b = 0; b < analysisBands; ++b) {
            const float bSample = m_analysisFilters[b].process(detectSample);
            const float bMag = std::abs(bSample);
            const float diff = bMag - m_bandEnergies[b];
            m_bandEnergies[b] += (diff > 0.0f ? 0.08f : 0.005f) * diff;
        }

        // 4. Resonance peak detection against moving spectral baseline
        float baseline = 0.0f;
        for (float e : m_bandEnergies) {
            baseline += e;
        }
        baseline /= static_cast<float>(analysisBands);

        // Find highest resonance spikes exceeding baseline + threshold
        const float threshLinear = std::pow(10.0f, m_thresholdDb / 20.0f);

        for (std::size_t n = 0; n < maxNotches; ++n) {
            auto& notch = m_notches[n];
            const std::size_t bIdx = (n * (analysisBands / maxNotches)) % analysisBands;
            const float bandE = m_bandEnergies[bIdx];

            float targetCutDb = 0.0f;
            if (bandE > threshLinear && bandE > baseline * 1.5f && m_depth > 0.001f) {
                const float prominenceRatio = bandE / std::max(baseline, 1e-5f);
                const float rawCut = std::min(18.0f, std::log2(prominenceRatio) * 6.0f * m_depth);
                targetCutDb = -rawCut;
            }

            maxCurrentCutDb = std::min(maxCurrentCutDb, targetCutDb);

            const float targetLinear = std::pow(10.0f, targetCutDb / 20.0f);
            const float coeff = (targetLinear < notch.gainLinear) ? m_attackCoeff : m_releaseCoeff;
            notch.gainLinear += coeff * (targetLinear - notch.gainLinear);
            notch.freqHz = m_bandFreqs[bIdx];
            notch.q = notchQ;
            notch.updateCoeffs(m_sampleRate);
        }

        // 5. Apply Tracking Dynamic Notch Filter Array
        float outL = delayedL;
        float outR = delayedR;

        if (m_processingMode == ResonanceProcessingMode::midOnly && numChannels > 1) {
            // Surgical Mid-Channel focus (lead vocals)
            float mid = 0.5f * (delayedL + delayedR);
            const float side = 0.5f * (delayedL - delayedR);

            for (auto& notch : m_notches) {
                mid = notch.processL(mid);
            }

            outL = mid + side;
            outR = mid - side;
        } else if (m_processingMode == ResonanceProcessingMode::sideOnly && numChannels > 1) {
            const float mid = 0.5f * (delayedL + delayedR);
            float side = 0.5f * (delayedL - delayedR);

            for (auto& notch : m_notches) {
                side = notch.processR(side);
            }

            outL = mid + side;
            outR = mid - side;
        } else {
            // Stereo mode
            for (auto& notch : m_notches) {
                outL = notch.processL(outL);
                outR = notch.processR(outR);
            }
        }

        // 6. Delta Audition Mode (listen to only the removed harshness/sibilance)
        if (m_deltaListen) {
            outL = delayedL - outL;
            outR = delayedR - outR;
        }

        ch0[s] = outL;
        if (ch1 != nullptr) {
            ch1[s] = outR;
        }
    }

    m_activeSuppressionDb = maxCurrentCutDb;
}

} // namespace dawg::engine::dsp
