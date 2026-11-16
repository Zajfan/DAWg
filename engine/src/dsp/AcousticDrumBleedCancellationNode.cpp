#include <dawg/engine/dsp/AcousticDrumBleedCancellationNode.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::engine::dsp {

AcousticDrumBleedCancellationNode::AcousticDrumBleedCancellationNode() noexcept
{
    setDrumType(DrumType::snare);
    reset();
}

bool AcousticDrumBleedCancellationNode::prepare(
    double sampleRate,
    std::size_t /*maxBlockSize*/,
    std::uint16_t /*channelCount*/)
{
    m_sampleRate = (sampleRate > 1000.0) ? sampleRate : 48000.0;
    const float fs = static_cast<float>(m_sampleRate);

    // Fast attack (0.3 ms) and musical release (60 ms)
    m_attackCoeff = 1.0f - std::exp(-1.0f / (0.0003f * fs));
    m_releaseCoeff = 1.0f - std::exp(-1.0f / (0.060f * fs));

    reset();
    updateFilterCoeffs();
    return true;
}

void AcousticDrumBleedCancellationNode::reset() noexcept
{
    m_envelope = 0.0f;
    m_gateGainDb = m_spillReductionDb;
    m_holdCount = 0.0f;
    m_ringWritePos = 0;
    m_lookaheadRingL.fill(0.0f);
    m_lookaheadRingR.fill(0.0f);

    m_detZ1 = m_detZ2 = 0.0f;
    m_shelfZ1L = m_shelfZ2L = 0.0f;
    m_shelfZ1R = m_shelfZ2R = 0.0f;

    m_samplePlayPos = MaxSampleLength;
    m_triggerArmed = true;
}

void AcousticDrumBleedCancellationNode::setDrumType(DrumType type) noexcept
{
    m_drumType = type;
    switch (type)
    {
    case DrumType::snare:
        m_detCenterHz = 2200.0f; // Snare crack & snap
        m_detQ = 1.6f;
        m_shelfFreqHz = 4000.0f; // Suppress cymbal / hi-hat spill
        m_thresholdDb = -22.0f;
        break;

    case DrumType::kick:
        m_detCenterHz = 75.0f;   // Kick sub thud
        m_detQ = 1.8f;
        m_shelfFreqHz = 350.0f;  // Suppress snare / tom bleed into kick
        m_thresholdDb = -20.0f;
        break;

    case DrumType::tom:
        m_detCenterHz = 160.0f;  // Tom fundamental
        m_detQ = 1.4f;
        m_shelfFreqHz = 1200.0f; // Suppress cymbals & snare spill
        m_thresholdDb = -24.0f;
        break;

    case DrumType::custom:
        break;
    }
    updateFilterCoeffs();
}

void AcousticDrumBleedCancellationNode::updateFilterCoeffs() noexcept
{
    const float fs = static_cast<float>(m_sampleRate);

    // 1. Detection Bandpass Filter (Direct Form II Transposed)
    const float w0 = 2.0f * std::numbers::pi_v<float> * std::clamp(m_detCenterHz / fs, 0.001f, 0.45f);
    const float alpha = std::sin(w0) / (2.0f * m_detQ);
    const float a0 = 1.0f + alpha;

    m_detB0 = (m_detQ * alpha) / a0;
    m_detB1 = 0.0f;
    m_detB2 = -(m_detQ * alpha) / a0;
    m_detA1 = (-2.0f * std::cos(w0)) / a0;
    m_detA2 = (1.0f - alpha) / a0;

    // 2. High-shelf spill reduction filter
    const float wShelf = 2.0f * std::numbers::pi_v<float> * std::clamp(m_shelfFreqHz / fs, 0.001f, 0.45f);
    const float gainLin = std::pow(10.0f, m_spillReductionDb * 0.05f);
    const float A = std::sqrt(std::max(0.001f, gainLin));
    const float alphaShelf = std::sin(wShelf) * 0.5f * std::sqrt((A + 1.0f / A) * (1.0f / 0.7071f - 1.0f) + 2.0f);
    const float cosWShelf = std::cos(wShelf);

    const float a0Shelf = (A + 1.0f) - (A - 1.0f) * cosWShelf + 2.0f * std::sqrt(A) * alphaShelf;
    m_shelfB0 = (A * ((A + 1.0f) + (A - 1.0f) * cosWShelf + 2.0f * std::sqrt(A) * alphaShelf)) / a0Shelf;
    m_shelfB1 = (-2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosWShelf)) / a0Shelf;
    m_shelfB2 = (A * ((A + 1.0f) + (A - 1.0f) * cosWShelf - 2.0f * std::sqrt(A) * alphaShelf)) / a0Shelf;
    m_shelfA1 = (2.0f * ((A - 1.0f) - (A + 1.0f) * cosWShelf)) / a0Shelf;
    m_shelfA2 = ((A + 1.0f) - (A - 1.0f) * cosWShelf - 2.0f * std::sqrt(A) * alphaShelf) / a0Shelf;
}

float AcousticDrumBleedCancellationNode::calculatePhaseCorrelation(
    const float* trackA,
    const float* trackB,
    std::size_t count) const noexcept
{
    if (trackA == nullptr || trackB == nullptr || count == 0)
    {
        return 1.0f;
    }

    double sumProduct = 0.0;
    double sumA2 = 0.0;
    double sumB2 = 0.0;

    for (std::size_t i = 0; i < count; ++i)
    {
        const double a = trackA[i];
        const double b = trackB[i];
        sumProduct += a * b;
        sumA2 += a * a;
        sumB2 += b * b;
    }

    const double denom = std::sqrt(sumA2 * sumB2);
    if (denom < 1.0e-9)
    {
        return 1.0f;
    }
    return std::clamp(static_cast<float>(sumProduct / denom), -1.0f, 1.0f);
}

void AcousticDrumBleedCancellationNode::loadAugmentationSample(const float* samples, std::size_t count)
{
    if (samples == nullptr || count == 0)
    {
        m_augmentSample.clear();
        return;
    }
    const auto len = std::min(count, MaxSampleLength);
    m_augmentSample.assign(samples, samples + len);
}

void AcousticDrumBleedCancellationNode::process(ProcessContext& context) noexcept
{
    auto& audio = context.audio;
    if (audio.channels == nullptr || audio.frameCount == 0 || audio.channelCount == 0)
    {
        return;
    }

    const std::size_t numSamples = audio.frameCount;
    const std::size_t numChannels = audio.channelCount;
    float* outL = audio.channels[0];
    float* outR = (numChannels > 1) ? audio.channels[1] : nullptr;

    const float threshLin = std::pow(10.0f, m_thresholdDb * 0.05f);
    const float ringCap = static_cast<float>(m_lookaheadRingL.size());
    const float fs = static_cast<float>(m_sampleRate);
    const float holdMaxSamples = 0.025f * fs; // 25 ms hold before release
    const float phaseDelaySamples = (m_phaseDelayMs * 0.001f) * fs;

    for (std::size_t i = 0; i < numSamples; ++i)
    {
        const float inL = outL[i];
        const float inR = (outR != nullptr) ? outR[i] : inL;

        // 1. Write incoming audio to lookahead ring buffer
        m_lookaheadRingL[m_ringWritePos] = inL;
        m_lookaheadRingR[m_ringWritePos] = inR;

        // 2. Detection Filter: Bandpass isolation of drum transient
        const float detIn = 0.5f * (inL + inR);
        const float detFiltered = m_detB0 * detIn + m_detZ1;
        m_detZ1 = m_detB1 * detIn - m_detA1 * detFiltered + m_detZ2;
        m_detZ2 = m_detB2 * detIn - m_detA2 * detFiltered;

        const float detEnv = std::abs(detFiltered);

        // 3. Transient Gate Ballistics & Spill Suppression Gain
        if (detEnv > threshLin)
        {
            m_envelope += m_attackCoeff * (1.0f - m_envelope);
            m_holdCount = holdMaxSamples;

            // Trigger augmentation sample on transient onset
            if (m_triggerArmed && !m_augmentSample.empty())
            {
                m_samplePlayPos = 0;
                m_sampleVelocity = std::clamp(detEnv / (threshLin * 1.5f), 0.2f, 1.0f);
                m_triggerArmed = false;
            }
        }
        else if (m_holdCount > 0.0f)
        {
            m_holdCount -= 1.0f;
        }
        else
        {
            m_envelope += m_releaseCoeff * (0.0f - m_envelope);
            if (m_envelope < 0.1f)
            {
                m_triggerArmed = true;
            }
        }

        // Gate gain in dB: interpolates from m_spillReductionDb up to 0 dB
        m_gateGainDb = m_spillReductionDb * (1.0f - m_envelope);

        // 4. Read lookahead audio with phase alignment delay
        const float totalDelay = static_cast<float>(m_lookaheadSamples) + phaseDelaySamples;
        float readPos = static_cast<float>(m_ringWritePos) - totalDelay;
        while (readPos < 0.0f)
        {
            readPos += ringCap;
        }
        while (readPos >= ringCap)
        {
            readPos -= ringCap;
        }

        const auto idx0 = static_cast<std::size_t>(readPos) % m_lookaheadRingL.size();
        const auto idx1 = (idx0 + 1) % m_lookaheadRingL.size();
        const float frac = readPos - static_cast<float>(static_cast<std::size_t>(readPos));

        float delayedL = m_lookaheadRingL[idx0] + frac * (m_lookaheadRingL[idx1] - m_lookaheadRingL[idx0]);
        float delayedR = m_lookaheadRingR[idx0] + frac * (m_lookaheadRingR[idx1] - m_lookaheadRingR[idx0]);

        // Polarity inversion switch
        if (m_polarityInverted)
        {
            delayedL = -delayedL;
            delayedR = -delayedR;
        }

        // 5. Apply Frequency-Dependent Spill De-Bleeding
        // When gate is closing/closed, high-shelf spill filter aggressively cleans cymbal spill
        const float shelfL = m_shelfB0 * delayedL + m_shelfZ1L;
        m_shelfZ1L = m_shelfB1 * delayedL - m_shelfA1 * shelfL + m_shelfZ2L;
        m_shelfZ2L = m_shelfB2 * delayedL - m_shelfA2 * shelfL;

        const float shelfR = m_shelfB0 * delayedR + m_shelfZ1R;
        m_shelfZ1R = m_shelfB1 * delayedR - m_shelfA1 * shelfR + m_shelfZ2R;
        m_shelfZ2R = m_shelfB2 * delayedR - m_shelfA2 * shelfR;

        // Dynamic crossfade: when open (envelope=1), pass pristine delayed signal; when closed, pass de-bled filtered signal
        float cleanL = m_envelope * delayedL + (1.0f - m_envelope) * shelfL;
        float cleanR = m_envelope * delayedR + (1.0f - m_envelope) * shelfR;

        // 6. Transient Sample Augmentation Playback
        if (m_samplePlayPos < m_augmentSample.size() && m_sampleBlend > 0.001f)
        {
            const float sampleVal = m_augmentSample[m_samplePlayPos] * m_sampleVelocity * m_sampleBlend;
            cleanL = (1.0f - m_sampleBlend * 0.5f) * cleanL + sampleVal;
            cleanR = (1.0f - m_sampleBlend * 0.5f) * cleanR + sampleVal;
            ++m_samplePlayPos;
        }

        outL[i] = cleanL;
        if (outR != nullptr)
        {
            outR[i] = cleanR;
        }

        m_ringWritePos = (m_ringWritePos + 1) % m_lookaheadRingL.size();
    }
}

} // namespace dawg::engine::dsp
