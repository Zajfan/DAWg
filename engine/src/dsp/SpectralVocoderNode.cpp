#include <dawg/engine/dsp/SpectralVocoderNode.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::engine::dsp {

SpectralVocoderNode::SpectralVocoderNode() noexcept
{
    // Compute base frequencies logarithmically spaced from 90 Hz to 9200 Hz
    constexpr float fMin = 90.0f;
    constexpr float fMax = 9200.0f;
    const float ratio = std::pow(fMax / fMin, 1.0f / static_cast<float>(numBands - 1));

    for (std::size_t i = 0; i < numBands; ++i)
    {
        m_bands[i].baseFreqHz = fMin * std::pow(ratio, static_cast<float>(i));
        m_bands[i].synthFreqHz = m_bands[i].baseFreqHz;
    }

    setEnvelopeAttackMs(m_attackMs);
    setEnvelopeReleaseMs(m_releaseMs);
    recomputeFilterFrequencies();
}

bool SpectralVocoderNode::prepare(
    const double sampleRate,
    const std::size_t maxBlockSize,
    const std::uint16_t channelCount)
{
    if (sampleRate <= 0.0 || maxBlockSize == 0 || channelCount == 0)
    {
        return false;
    }

    m_sampleRate = sampleRate;
    setEnvelopeAttackMs(m_attackMs);
    setEnvelopeReleaseMs(m_releaseMs);
    recomputeFilterFrequencies();
    reset();
    return true;
}

void SpectralVocoderNode::reset() noexcept
{
    for (auto& band : m_bands)
    {
        band.modFilterL.reset();
        band.modFilterR.reset();
        band.synthFilterL.reset();
        band.synthFilterR.reset();
        band.envelopeL = 0.0f;
        band.envelopeR = 0.0f;
    }

    m_carrierPhaseA = 0.0f;
    m_carrierPhaseB = 0.0f;
    m_carrierSubPhase = 0.0f;
    m_sibilantDetected = false;
    m_pinkNoiseB0 = 0.0f;
    m_pinkNoiseB1 = 0.0f;
    m_pinkNoiseB2 = 0.0f;
}

void SpectralVocoderNode::recomputeFilterFrequencies() noexcept
{
    constexpr float fCenterRef = 1000.0f;
    const float formantRatio = std::pow(2.0f, m_formantShiftSemitones / 12.0f);

    for (std::size_t i = 0; i < numBands; ++i)
    {
        // Spacing morph: expands or compresses band centers around 1 kHz
        const float baseLog = std::log(m_bands[i].baseFreqHz / fCenterRef);
        const float spacedFreq = fCenterRef * std::exp(baseLog * m_bandSpacing);

        // Modulator filter tuned to spaced frequency
        m_bands[i].modFilterL.setCoefficients(m_sampleRate, spacedFreq, m_bandQ);
        m_bands[i].modFilterR.setCoefficients(m_sampleRate, spacedFreq, m_bandQ);

        // Synthesis filter shifted by formant ratio
        const float synthFreq = std::clamp(spacedFreq * formantRatio, 30.0f, static_cast<float>(m_sampleRate * 0.46));
        m_bands[i].synthFreqHz = synthFreq;
        m_bands[i].synthFilterL.setCoefficients(m_sampleRate, synthFreq, m_bandQ);
        m_bands[i].synthFilterR.setCoefficients(m_sampleRate, synthFreq, m_bandQ);
    }
}

void SpectralVocoderNode::setFormantShiftSemitones(const float semitones) noexcept
{
    m_formantShiftSemitones = std::clamp(semitones, -12.0f, 12.0f);
    recomputeFilterFrequencies();
}

void SpectralVocoderNode::setBandQ(const float q) noexcept
{
    m_bandQ = std::clamp(q, 1.0f, 18.0f);
    recomputeFilterFrequencies();
}

void SpectralVocoderNode::setBandSpacing(const float spacing) noexcept
{
    m_bandSpacing = std::clamp(spacing, 0.5f, 1.5f);
    recomputeFilterFrequencies();
}

void SpectralVocoderNode::setEnvelopeAttackMs(const float attackMs) noexcept
{
    m_attackMs = std::clamp(attackMs, 0.5f, 50.0f);
    const float timeSec = m_attackMs * 0.001f;
    m_attackCoeff = 1.0f - std::exp(-1.0f / (timeSec * static_cast<float>(m_sampleRate)));
}

void SpectralVocoderNode::setEnvelopeReleaseMs(const float releaseMs) noexcept
{
    m_releaseMs = std::clamp(releaseMs, 5.0f, 250.0f);
    const float timeSec = m_releaseMs * 0.001f;
    m_releaseCoeff = 1.0f - std::exp(-1.0f / (timeSec * static_cast<float>(m_sampleRate)));
}

void SpectralVocoderNode::getBandLevels(float* destination, const std::size_t count) const noexcept
{
    if (destination == nullptr || count == 0)
    {
        return;
    }

    const auto copyCount = std::min(count, numBands);
    for (std::size_t i = 0; i < copyCount; ++i)
    {
        destination[i] = std::max(m_bands[i].envelopeL, m_bands[i].envelopeR);
    }
}

void SpectralVocoderNode::process(ProcessContext& context) noexcept
{
    if (context.audio.channelCount == 0 || context.audio.frameCount == 0)
    {
        return;
    }

    const auto frameCount = context.audio.frameCount;
    auto* modCh0 = context.audio.channels[0];
    auto* modCh1 = (context.audio.channelCount >= 2) ? context.audio.channels[1] : modCh0;

    if (modCh0 == nullptr)
    {
        return;
    }

    // Check external sidechain for carrier
    const bool hasExternalSidechain = (context.sidechain.channelCount > 0 && context.sidechain.channels != nullptr && context.sidechain.channels[0] != nullptr);
    const auto* extCarrierL = hasExternalSidechain ? context.sidechain.channels[0] : nullptr;
    const auto* extCarrierR = (hasExternalSidechain && context.sidechain.channelCount >= 2 && context.sidechain.channels[1] != nullptr)
        ? context.sidechain.channels[1]
        : extCarrierL;

    // Carrier frequency & detune increments
    const float detuneMult = std::pow(2.0f, (m_carrierDetuneCents / 1200.0f));
    const float freqA = m_carrierFreqHz;
    const float freqB = m_carrierFreqHz * detuneMult;
    const float freqSub = m_carrierFreqHz * 0.5f;

    const float phaseIncA = freqA / static_cast<float>(m_sampleRate);
    const float phaseIncB = freqB / static_cast<float>(m_sampleRate);
    const float phaseIncSub = freqSub / static_cast<float>(m_sampleRate);

    // Fast noise generator (LCG)
    auto generateNoise = [this]() noexcept -> float
    {
        m_noiseRng = m_noiseRng * 1664525u + 1013904223u;
        const float white = static_cast<float>(static_cast<std::int32_t>(m_noiseRng)) * 4.6566129e-10f;
        // 3-pole pinking filter
        m_pinkNoiseB0 = 0.99765f * m_pinkNoiseB0 + white * 0.0990460f;
        m_pinkNoiseB1 = 0.96300f * m_pinkNoiseB1 + white * 0.2965164f;
        m_pinkNoiseB2 = 0.57000f * m_pinkNoiseB2 + white * 1.0526913f;
        return (m_pinkNoiseB0 + m_pinkNoiseB1 + m_pinkNoiseB2 + white * 0.1848f) * 0.4f;
    };

    // Track sibilance over block: measure zero crossing density and high frequency energy
    std::size_t zeroCrossings = 0;
    float highFreqEnergy = 0.0f;
    for (std::size_t i = 1; i < frameCount; ++i)
    {
        if ((modCh0[i] >= 0.0f && modCh0[i - 1] < 0.0f) || (modCh0[i] < 0.0f && modCh0[i - 1] >= 0.0f))
        {
            zeroCrossings++;
        }
        const float diff = modCh0[i] - modCh0[i - 1];
        highFreqEnergy += diff * diff;
    }

    const float zcr = static_cast<float>(zeroCrossings) / static_cast<float>(frameCount);
    m_sibilantDetected = (zcr > m_sibilanceThresh && highFreqEnergy > 1e-5f);

    for (std::size_t i = 0; i < frameCount; ++i)
    {
        const float dryModL = modCh0[i];
        const float dryModR = (modCh1 != nullptr) ? modCh1[i] : dryModL;

        // 1. Generate or read Carrier
        float rawCarrierL = 0.0f;
        float rawCarrierR = 0.0f;

        if (m_carrierSource == SpectralVocoderCarrierSource::externalSidechain && extCarrierL != nullptr)
        {
            rawCarrierL = extCarrierL[i];
            rawCarrierR = (extCarrierR != nullptr) ? extCarrierR[i] : rawCarrierL;
        }
        else if (m_carrierSource == SpectralVocoderCarrierSource::internalSaw)
        {
            // Dual detuned sawtooth
            const float sawA = 2.0f * m_carrierPhaseA - 1.0f;
            const float sawB = 2.0f * m_carrierPhaseB - 1.0f;
            const float sub = 2.0f * m_carrierSubPhase - 1.0f;

            rawCarrierL = (sawA * 0.6f + sawB * 0.4f + sub * m_carrierSubLevel);
            rawCarrierR = (sawB * 0.6f + sawA * 0.4f + sub * m_carrierSubLevel);
        }
        else // internalPulse
        {
            // Dual pulse with PWM and sub oscillator
            const float pulseA = (m_carrierPhaseA < m_carrierPulseWidth) ? 1.0f : -1.0f;
            const float pulseB = (m_carrierPhaseB < m_carrierPulseWidth) ? 1.0f : -1.0f;
            const float sub = (m_carrierSubPhase < 0.5f) ? 1.0f : -1.0f;

            rawCarrierL = (pulseA * 0.65f + pulseB * 0.35f + sub * m_carrierSubLevel);
            rawCarrierR = (pulseB * 0.65f + pulseA * 0.35f + sub * m_carrierSubLevel);
        }

        // Advance carrier phases
        m_carrierPhaseA += phaseIncA;
        if (m_carrierPhaseA >= 1.0f) { m_carrierPhaseA -= 1.0f; }

        m_carrierPhaseB += phaseIncB;
        if (m_carrierPhaseB >= 1.0f) { m_carrierPhaseB -= 1.0f; }

        m_carrierSubPhase += phaseIncSub;
        if (m_carrierSubPhase >= 1.0f) { m_carrierSubPhase -= 1.0f; }

        // Sibilance noise injection: when unvoiced consonants are detected, blend noise into carrier
        const float noiseSample = generateNoise() * m_unvoicedNoiseLevel;
        if (m_sibilantDetected)
        {
            rawCarrierL = rawCarrierL * 0.35f + noiseSample * 0.85f;
            rawCarrierR = rawCarrierR * 0.35f + noiseSample * 0.85f;
        }

        // 2. Filter Bank Modulator & Synthesis Processing
        float vocodedL = 0.0f;
        float vocodedR = 0.0f;

        for (std::size_t b = 0; b < numBands; ++b)
        {
            auto& band = m_bands[b];

            // Modulator bandpass filtering
            const float modBandL = band.modFilterL.process(dryModL);
            const float modBandR = band.modFilterR.process(dryModR);

            // Envelope follower with asymmetric attack and release
            const float absModL = std::abs(modBandL);
            const float absModR = std::abs(modBandR);

            const float coeffL = (absModL > band.envelopeL) ? m_attackCoeff : m_releaseCoeff;
            const float coeffR = (absModR > band.envelopeR) ? m_attackCoeff : m_releaseCoeff;

            band.envelopeL += coeffL * (absModL - band.envelopeL);
            band.envelopeR += coeffR * (absModR - band.envelopeR);

            // For the highest 4 bands, optionally inject extra sibilance noise directly if speech consonant
            float carrierInL = rawCarrierL;
            float carrierInR = rawCarrierR;
            if (b >= numBands - 4 && m_sibilantDetected)
            {
                carrierInL += noiseSample * 0.6f;
                carrierInR += noiseSample * 0.6f;
            }

            // Synthesis bandpass filtering
            const float synthBandL = band.synthFilterL.process(carrierInL);
            const float synthBandR = band.synthFilterR.process(carrierInR);

            // Modulate synthesis carrier band by modulator envelope
            vocodedL += synthBandL * band.envelopeL;
            vocodedR += synthBandR * band.envelopeR;
        }

        // 3. Compensation & Wet / Dry Mix
        // Filter bank summation normalizer
        constexpr float bankGainScale = 2.4f;
        const float wetL = vocodedL * bankGainScale;
        const float wetR = vocodedR * bankGainScale;

        modCh0[i] = (1.0f - m_mix) * dryModL + m_mix * wetL;
        if (modCh1 != nullptr)
        {
            modCh1[i] = (1.0f - m_mix) * dryModR + m_mix * wetR;
        }
    }
}

} // namespace dawg::engine::dsp
