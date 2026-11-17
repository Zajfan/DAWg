#include <dawg/engine/dsp/StudioMonitorCalibrationNode.h>

#include <cmath>
#include <numbers>

namespace dawg::engine::dsp {

constexpr float kPi = std::numbers::pi_v<float>;

StudioMonitorCalibrationNode::StudioMonitorCalibrationNode() noexcept
{
    m_measuredDb.fill(0.0f);
    m_targetDb.fill(0.0f);
    m_correctionDb.fill(0.0f);
    setTargetCurve(SpeakerTargetCurve::flat);
}

bool StudioMonitorCalibrationNode::prepare(
    double sampleRate,
    [[maybe_unused]] std::size_t maxBlockSize,
    [[maybe_unused]] std::uint16_t channelCount)
{
    m_sampleRate = (sampleRate > 1000.0) ? sampleRate : 48000.0;
    reset();
    updateFilters();
    return true;
}

void StudioMonitorCalibrationNode::reset() noexcept
{
    for (auto& st : m_filterStatesL)
        st.reset();
    for (auto& st : m_filterStatesR)
        st.reset();

    m_auratoneHpStateL.reset();
    m_auratoneHpStateR.reset();
    m_auratoneLpStateL.reset();
    m_auratoneLpStateR.reset();
}

void StudioMonitorCalibrationNode::setTargetCurve(SpeakerTargetCurve curve) noexcept
{
    m_targetCurve = curve;

    switch (curve)
    {
    case SpeakerTargetCurve::flat:
        m_targetDb.fill(0.0f);
        break;

    case SpeakerTargetCurve::harmanInRoom:
        // Harman target: +6dB below 150Hz sloping smoothly down, flat 200Hz-1kHz, gentle -0.8dB/octave downward slope above 1kHz
        for (std::size_t i = 0; i < kNumBands; ++i)
        {
            const float f = kIsoCenterFrequencies[i];
            if (f <= 150.0f)
            {
                m_targetDb[i] = 6.0f * (1.0f - (f / 300.0f));
            }
            else if (f <= 1000.0f)
            {
                m_targetDb[i] = 0.0f;
            }
            else
            {
                const float octavesAbove1k = std::log2(f / 1000.0f);
                m_targetDb[i] = -0.8f * octavesAbove1k;
            }
        }
        break;

    case SpeakerTargetCurve::bk1974:
        // Bruel & Kjaer 1974: +3 dB at 50Hz, straight linear descent to -3 dB at 20kHz
        for (std::size_t i = 0; i < kNumBands; ++i)
        {
            const float f = kIsoCenterFrequencies[i];
            const float logNorm = std::log10(f / 20.0f) / std::log10(20000.0f / 20.0f); // 0 to 1
            m_targetDb[i] = 3.0f - (6.0f * logNorm);
        }
        break;

    case SpeakerTargetCurve::auratoneMonoCheck:
        // Auratone: 250Hz - 5kHz mid focus
        m_targetDb.fill(0.0f);
        break;
    }

    updateFilters();
}

StudioMonitorCalibrationNode::BiquadCoeffs StudioMonitorCalibrationNode::designPeakingEQ(
    double sampleRate, float freqHz, float gainDb, float Q) noexcept
{
    BiquadCoeffs c;
    if (std::abs(gainDb) < 0.05f)
    {
        c.b0 = 1.0f; c.b1 = 0.0f; c.b2 = 0.0f;
        c.a1 = 0.0f; c.a2 = 0.0f;
        return c;
    }

    const float A = std::pow(10.0f, gainDb / 40.0f);
    const float w0 = 2.0f * kPi * (std::clamp(freqHz, 10.0f, static_cast<float>(sampleRate * 0.49)) / static_cast<float>(sampleRate));
    const float alpha = std::sin(w0) / (2.0f * std::max(0.1f, Q));
    const float cosW0 = std::cos(w0);

    const float a0 = 1.0f + alpha / A;
    c.b0 = (1.0f + alpha * A) / a0;
    c.b1 = (-2.0f * cosW0) / a0;
    c.b2 = (1.0f - alpha * A) / a0;
    c.a1 = (-2.0f * cosW0) / a0;
    c.a2 = (1.0f - alpha / A) / a0;

    return c;
}

StudioMonitorCalibrationNode::BiquadCoeffs StudioMonitorCalibrationNode::designBandpass(
    double sampleRate, float freqHz, float Q) noexcept
{
    BiquadCoeffs c;
    const float w0 = 2.0f * kPi * (std::clamp(freqHz, 10.0f, static_cast<float>(sampleRate * 0.49)) / static_cast<float>(sampleRate));
    const float alpha = std::sin(w0) / (2.0f * std::max(0.1f, Q));
    const float cosW0 = std::cos(w0);

    const float a0 = 1.0f + alpha;
    c.b0 = alpha / a0;
    c.b1 = 0.0f;
    c.b2 = -alpha / a0;
    c.a1 = (-2.0f * cosW0) / a0;
    c.a2 = (1.0f - alpha) / a0;

    return c;
}

void StudioMonitorCalibrationNode::updateFilters() noexcept
{
    // Design 31 peaking EQ biquad stages with Q = 4.3 (standard 1/3 octave bandwidth)
    constexpr float kThirdOctaveQ = 4.318f;

    for (std::size_t i = 0; i < kNumBands; ++i)
    {
        const float rawDelta = m_targetDb[i] - m_measuredDb[i];
        m_correctionDb[i] = std::clamp(rawDelta, m_maxCutDb, m_maxBoostDb);
        m_filterCoeffs[i] = designPeakingEQ(m_sampleRate, kIsoCenterFrequencies[i], m_correctionDb[i], kThirdOctaveQ);
    }

    // Auratone 2nd order HPF at 250 Hz and LPF at 5000 Hz
    {
        // High-pass 250Hz Butterworth Q = 0.707
        const float w0 = 2.0f * kPi * (250.0f / static_cast<float>(m_sampleRate));
        const float alpha = std::sin(w0) / (2.0f * 0.7071f);
        const float cosW0 = std::cos(w0);
        const float a0 = 1.0f + alpha;
        m_auratoneHpCoeff.b0 = ((1.0f + cosW0) * 0.5f) / a0;
        m_auratoneHpCoeff.b1 = -(1.0f + cosW0) / a0;
        m_auratoneHpCoeff.b2 = ((1.0f + cosW0) * 0.5f) / a0;
        m_auratoneHpCoeff.a1 = (-2.0f * cosW0) / a0;
        m_auratoneHpCoeff.a2 = (1.0f - alpha) / a0;
    }
    {
        // Low-pass 5000Hz Butterworth Q = 0.707
        const float w0 = 2.0f * kPi * (5000.0f / static_cast<float>(m_sampleRate));
        const float alpha = std::sin(w0) / (2.0f * 0.7071f);
        const float cosW0 = std::cos(w0);
        const float a0 = 1.0f + alpha;
        m_auratoneLpCoeff.b0 = ((1.0f - cosW0) * 0.5f) / a0;
        m_auratoneLpCoeff.b1 = (1.0f - cosW0) / a0;
        m_auratoneLpCoeff.b2 = ((1.0f - cosW0) * 0.5f) / a0;
        m_auratoneLpCoeff.a1 = (-2.0f * cosW0) / a0;
        m_auratoneLpCoeff.a2 = (1.0f - alpha) / a0;
    }
}

std::vector<float> StudioMonitorCalibrationNode::generateLogChirpSweep(
    double sampleRate,
    const CalibrationSweepConfig& config)
{
    const auto totalSamples = static_cast<std::size_t>(std::max(1.0, config.durationSeconds * sampleRate));
    std::vector<float> sweep(totalSamples, 0.0f);

    const double f1 = std::max(10.0, static_cast<double>(config.startFreqHz));
    const double f2 = std::min(sampleRate * 0.49, static_cast<double>(config.endFreqHz));
    const double T = static_cast<double>(totalSamples) / sampleRate;
    const double rateLog = std::log(f2 / f1);

    const auto taperSamples = static_cast<std::size_t>(config.taperSeconds * sampleRate);

    for (std::size_t n = 0; n < totalSamples; ++n)
    {
        const double t = static_cast<double>(n) / sampleRate;
        const double phi = (2.0 * std::numbers::pi * f1 * T / rateLog) * (std::exp(t * rateLog / T) - 1.0);
        float sample = config.amplitude * static_cast<float>(std::sin(phi));

        // Start cosine taper fade-in
        if (n < taperSamples)
        {
            const float ramp = 0.5f * (1.0f - std::cos(kPi * static_cast<float>(n) / static_cast<float>(taperSamples)));
            sample *= ramp;
        }
        // End cosine taper fade-out
        else if (n + taperSamples >= totalSamples)
        {
            const auto rem = totalSamples - 1 - n;
            const float ramp = 0.5f * (1.0f - std::cos(kPi * static_cast<float>(rem) / static_cast<float>(taperSamples)));
            sample *= ramp;
        }

        sweep[n] = sample;
    }

    return sweep;
}

std::vector<float> StudioMonitorCalibrationNode::deconvolveImpulseResponse(
    const float* recordedMic,
    std::size_t sampleCount,
    double sampleRate,
    const CalibrationSweepConfig& config)
{
    std::vector<float> rir(std::min(sampleCount, static_cast<std::size_t>(sampleRate * 1.0)), 0.0f);
    if (recordedMic == nullptr || sampleCount == 0)
        return rir;

    // Time-reversed sweep with Farina 6dB/octave decay compensation
    const auto sweep = generateLogChirpSweep(sampleRate, config);
    if (sweep.empty())
        return rir;

    const std::size_t sweepLen = sweep.size();
    std::vector<float> inverseSweep(sweepLen);
    for (std::size_t i = 0; i < sweepLen; ++i)
    {
        const float tNorm = static_cast<float>(i) / static_cast<float>(sweepLen);
        const float envComp = std::exp(-tNorm * std::log(config.endFreqHz / config.startFreqHz));
        inverseSweep[sweepLen - 1 - i] = sweep[i] * envComp;
    }

    // Direct convolution between recorded mic input and inverse sweep filter
    const std::size_t outLen = rir.size();
    for (std::size_t n = 0; n < outLen; ++n)
    {
        float sum = 0.0f;
        const std::size_t kMax = std::min(n, sweepLen);
        for (std::size_t k = 0; k < kMax; ++k)
        {
            if (n >= k && (n - k) < sampleCount)
                sum += recordedMic[n - k] * inverseSweep[k];
        }
        rir[n] = sum;
    }

    // Normalize peak to 1.0
    float maxVal = 0.0f;
    for (const auto v : rir)
        maxVal = std::max(maxVal, std::abs(v));

    if (maxVal > 1e-6f)
    {
        const float norm = 1.0f / maxVal;
        for (auto& v : rir)
            v *= norm;
    }

    return rir;
}

bool StudioMonitorCalibrationNode::calibrateFromImpulseResponse(
    const float* impulseResponse,
    std::size_t sampleCount)
{
    if (impulseResponse == nullptr || sampleCount < 128)
        return false;

    // Measure energy in 31 ISO 1/3-octave bands by filtering the impulse response
    constexpr float kQ = 4.318f;
    for (std::size_t b = 0; b < kNumBands; ++b)
    {
        const auto bpCoeff = designBandpass(m_sampleRate, kIsoCenterFrequencies[b], kQ);
        BiquadState state;
        float bandEnergy = 0.0f;

        for (std::size_t i = 0; i < sampleCount; ++i)
        {
            const float filtered = state.process(impulseResponse[i], bpCoeff);
            bandEnergy += filtered * filtered;
        }

        const float rms = std::sqrt(bandEnergy / static_cast<float>(sampleCount));
        const float db = (rms > 1e-6f) ? 20.0f * std::log10(rms) : -100.0f;
        m_measuredDb[b] = db;
    }

    // Normalize measured dB relative to 1000 Hz reference
    const float refDb = m_measuredDb[17]; // 1000 Hz
    for (std::size_t b = 0; b < kNumBands; ++b)
        m_measuredDb[b] -= refDb;

    updateFilters();
    return true;
}

float StudioMonitorCalibrationNode::getBandCorrectionDb(std::size_t bandIdx) const noexcept
{
    return (bandIdx < kNumBands) ? m_correctionDb[bandIdx] : 0.0f;
}

float StudioMonitorCalibrationNode::getMeasuredBandDb(std::size_t bandIdx) const noexcept
{
    return (bandIdx < kNumBands) ? m_measuredDb[bandIdx] : 0.0f;
}

float StudioMonitorCalibrationNode::getTargetBandDb(std::size_t bandIdx) const noexcept
{
    return (bandIdx < kNumBands) ? m_targetDb[bandIdx] : 0.0f;
}

void StudioMonitorCalibrationNode::process(ProcessContext& context) noexcept
{
    if (!m_enabled || context.audio.channelCount == 0 || context.audio.channels[0] == nullptr)
        return;

    const std::size_t numSamples = context.audio.frameCount;
    float* const chL = context.audio.channels[0];
    float* const chR = (context.audio.channelCount > 1 && context.audio.channels[1] != nullptr)
                           ? context.audio.channels[1]
                           : nullptr;

    for (std::size_t s = 0; s < numSamples; ++s)
    {
        float inL = chL[s];
        float inR = (chR != nullptr) ? chR[s] : inL;

        if (m_targetCurve == SpeakerTargetCurve::auratoneMonoCheck)
        {
            // Mono mix check
            const float mono = 0.5f * (inL + inR);
            inL = m_auratoneLpStateL.process(m_auratoneHpStateL.process(mono, m_auratoneHpCoeff), m_auratoneLpCoeff);
            inR = inL;
        }
        else
        {
            // Apply 31-band room correction EQ cascade
            for (std::size_t b = 0; b < kNumBands; ++b)
            {
                inL = m_filterStatesL[b].process(inL, m_filterCoeffs[b]);
                if (chR != nullptr)
                    inR = m_filterStatesR[b].process(inR, m_filterCoeffs[b]);
            }
        }

        chL[s] = inL;
        if (chR != nullptr)
            chR[s] = inR;
    }
}

} // namespace dawg::engine::dsp
