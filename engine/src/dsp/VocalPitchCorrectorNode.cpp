#include <dawg/engine/dsp/VocalPitchCorrectorNode.h>

#include <numbers>
#include <span>

namespace dawg::engine::dsp {

VocalPitchCorrectorNode::VocalPitchCorrectorNode() noexcept
{
    m_analysisBuffer.assign(yinBufferSize, 0.0f);
    m_delayBufferL.assign(delayBufferSize, 0.0f);
    m_delayBufferR.assign(delayBufferSize, 0.0f);
}

bool VocalPitchCorrectorNode::prepare(
    const double sampleRate,
    const std::size_t maxBlockSize,
    const std::uint16_t channelCount)
{
    if (sampleRate <= 0.0 || maxBlockSize == 0 || channelCount == 0)
    {
        return false;
    }

    m_sampleRate = sampleRate;
    m_analysisBuffer.assign(yinBufferSize, 0.0f);
    m_delayBufferL.assign(delayBufferSize, 0.0f);
    m_delayBufferR.assign(delayBufferSize, 0.0f);
    reset();
    return true;
}

void VocalPitchCorrectorNode::reset() noexcept
{
    std::fill(m_analysisBuffer.begin(), m_analysisBuffer.end(), 0.0f);
    std::fill(m_delayBufferL.begin(), m_delayBufferL.end(), 0.0f);
    std::fill(m_delayBufferR.begin(), m_delayBufferR.end(), 0.0f);
    m_analysisWritePos = 0;
    m_delayWritePos = 0;
    m_phaseTapA = 0.0f;
    m_phaseTapB = 0.5f;
    m_smoothedCorrectionSemitones = 0.0f;
    m_lastPitchInfo = {};
    m_streamTimeSec = 0.0f;
    m_vibratoBpState1 = 0.0f;
    m_vibratoBpState2 = 0.0f;
    m_extractedVibratoSemitones = 0.0f;
    m_formantFilterL.reset();
    m_formantFilterR.reset();
    m_trajectoryHistory.fill({});
    m_trajectoryWritePos = 0;
}

float VocalPitchCorrectorNode::quantizeToScale(
    const float midiPitch,
    const MusicalScale scale,
    const std::uint8_t rootNote) noexcept
{
    // Define intervals relative to root note
    static constexpr std::array<int, 12> chromaticScale {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    static constexpr std::array<int, 7> majorScale {0, 2, 4, 5, 7, 9, 11};
    static constexpr std::array<int, 7> minorScale {0, 2, 3, 5, 7, 8, 10};
    static constexpr std::array<int, 7> harmonicMinorScale {0, 2, 3, 5, 7, 8, 11};
    static constexpr std::array<int, 7> dorianScale {0, 2, 3, 5, 7, 9, 10};
    static constexpr std::array<int, 7> mixolydianScale {0, 2, 4, 5, 7, 9, 10};
    static constexpr std::array<int, 5> pentatonicMinorScale {0, 3, 5, 7, 10};
    static constexpr std::array<int, 5> pentatonicMajorScale {0, 2, 4, 7, 9};

    std::span<const int> intervals;
    switch (scale)
    {
    case MusicalScale::chromatic:
        intervals = chromaticScale;
        break;
    case MusicalScale::major:
        intervals = majorScale;
        break;
    case MusicalScale::minor:
        intervals = minorScale;
        break;
    case MusicalScale::harmonicMinor:
        intervals = harmonicMinorScale;
        break;
    case MusicalScale::dorian:
        intervals = dorianScale;
        break;
    case MusicalScale::mixolydian:
        intervals = mixolydianScale;
        break;
    case MusicalScale::pentatonicMinor:
        intervals = pentatonicMinorScale;
        break;
    case MusicalScale::pentatonicMajor:
        intervals = pentatonicMajorScale;
        break;
    }

    const float relative = midiPitch - static_cast<float>(rootNote % 12);
    const int octave = static_cast<int>(std::floor(relative / 12.0f));
    const float pitchClass = relative - static_cast<float>(octave * 12);

    float closestInterval = static_cast<float>(intervals[0]);
    float minDiff = 999.0f;

    for (const auto interval : intervals)
    {
        const float candidate = static_cast<float>(interval);
        const float diff = std::abs(pitchClass - candidate);
        if (diff < minDiff)
        {
            minDiff = diff;
            closestInterval = candidate;
        }
    }

    // Check candidate wrap-around in next octave (e.g. interval 0 at +12)
    const float upperCandidateDiff = std::abs(pitchClass - (static_cast<float>(intervals[0]) + 12.0f));
    if (upperCandidateDiff < minDiff)
    {
        closestInterval = static_cast<float>(intervals[0]) + 12.0f;
    }

    return static_cast<float>(octave * 12) + closestInterval + static_cast<float>(rootNote % 12);
}

void VocalPitchCorrectorNode::getPitchTrajectory(PitchTrajectoryPoint* destination, std::size_t count) const noexcept
{
    if (destination == nullptr || count == 0)
    {
        return;
    }

    const auto copyCount = std::min(count, trajectoryHistorySize);
    for (std::size_t i = 0; i < copyCount; ++i)
    {
        const auto idx = (m_trajectoryWritePos + trajectoryHistorySize - copyCount + i) % trajectoryHistorySize;
        destination[i] = m_trajectoryHistory[idx];
    }
}

DetectedPitchInfo VocalPitchCorrectorNode::detectPitchYin(const float* buffer, const std::size_t size) const noexcept
{
    DetectedPitchInfo info {};
    if (buffer == nullptr || size < 256 || m_sampleRate <= 0.0)
    {
        return info;
    }

    // 1. Sibilance & Zero Crossing Rate (ZCR) detection for aperiodic consonants ('s', 't', 'k')
    std::size_t zeroCrossings = 0;
    float totalEnergy = 0.0f;
    for (std::size_t i = 1; i < size; ++i)
    {
        totalEnergy += buffer[i] * buffer[i];
        if ((buffer[i] >= 0.0f && buffer[i - 1] < 0.0f) || (buffer[i] < 0.0f && buffer[i - 1] >= 0.0f))
        {
            zeroCrossings++;
        }
    }

    const float zcr = static_cast<float>(zeroCrossings) / static_cast<float>(size);
    const float rms = std::sqrt(totalEnergy / static_cast<float>(size));

    if (rms < 0.005f)
    {
        // Silence
        info.isVoiced = false;
        info.isSibilant = false;
        return info;
    }

    if (zcr > 0.32f && rms > 0.01f)
    {
        // High frequency aperiodic energy = sibilant consonant
        info.isVoiced = false;
        info.isSibilant = true;
        info.confidence = 0.0f;
        return info;
    }

    // Two-pass YIN analysis:
    // Pass 1: 512-sample recent window (halfSize = 256) for zero boundary discontinuity on 180-1200 Hz
    // Pass 2: Full window for lower fundamental frequencies (50-200 Hz)
    auto runYinPass = [&](const float* pBuf, std::size_t bufLen) -> DetectedPitchInfo
    {
        DetectedPitchInfo passInfo {};
        const auto halfSize = bufLen / 2;
        if (halfSize < 64) { return passInfo; }

        std::vector<float> dPrime(halfSize, 1.0f);
        const auto minTau = std::max<std::size_t>(2, static_cast<std::size_t>(m_sampleRate / 1200.0));
        const auto maxTau = std::min<std::size_t>(halfSize - 8, static_cast<std::size_t>(m_sampleRate / 50.0));

        dPrime[0] = 1.0f;
        float runningSum = 0.0f;

        for (std::size_t tau = 1; tau < halfSize; ++tau)
        {
            float diffSum = 0.0f;
            for (std::size_t j = 0; j < halfSize; ++j)
            {
                const float diff = pBuf[j] - pBuf[j + tau];
                diffSum += diff * diff;
            }

            runningSum += diffSum;
            dPrime[tau] = (runningSum > 1e-9f) ? (diffSum * static_cast<float>(tau) / runningSum) : 1.0f;
        }

        constexpr float threshold = 0.15f;
        std::size_t bestTau = 0;

        for (std::size_t tau = minTau; tau <= maxTau; ++tau)
        {
            if (dPrime[tau] < threshold)
            {
                while (tau + 1 <= maxTau && dPrime[tau + 1] < dPrime[tau])
                {
                    ++tau;
                }
                bestTau = tau;
                break;
            }
        }

        if (bestTau == 0)
        {
            float minVal = 999.0f;
            for (std::size_t tau = minTau; tau <= maxTau; ++tau)
            {
                if (dPrime[tau] < minVal)
                {
                    minVal = dPrime[tau];
                    bestTau = tau;
                }
            }
        }

        if (bestTau < minTau || bestTau >= maxTau)
        {
            return passInfo;
        }

        // Parabolic Interpolation for sub-sample accuracy
        const float alpha = dPrime[bestTau - 1];
        const float beta = dPrime[bestTau];
        const float gamma = dPrime[bestTau + 1];
        const float denominator = 2.0f * (2.0f * beta - alpha - gamma);
        float delta = 0.0f;
        if (std::abs(denominator) > 1e-7f)
        {
            delta = (gamma - alpha) / denominator;
        }

        const float tauStar = static_cast<float>(bestTau) + delta;
        if (tauStar <= 0.0f)
        {
            return passInfo;
        }

        const float detectedHz = static_cast<float>(m_sampleRate) / tauStar;
        if (detectedHz < 40.0f || detectedHz > 1500.0f)
        {
            return passInfo;
        }

        const float confidence = std::clamp(1.0f - beta, 0.0f, 1.0f);
        const float midi = 69.0f + 12.0f * std::log2(detectedHz / 440.0f);

        passInfo.pitchHz = detectedHz;
        passInfo.midiPitch = midi;
        passInfo.confidence = confidence;
        passInfo.isVoiced = (confidence >= 0.55f);
        passInfo.targetMidiPitch = quantizeToScale(midi, m_scale, m_rootNote);

        return passInfo;
    };

    // Pass 1: analyze most recent 512 samples
    if (size >= 512)
    {
        info = runYinPass(buffer + (size - 512), 512);
        if (info.isVoiced)
        {
            return info;
        }
    }

    // Pass 2: fallback to full buffer for low fundamentals
    return runYinPass(buffer, size);
}

void VocalPitchCorrectorNode::updateFormantFilter(const float pitchRatio) noexcept
{
    // High-frequency formant compensation:
    // When pitch ratio > 1.0 (shifted up), high frequencies are boosted too much; we apply a gentle high shelf cut.
    // When pitch ratio < 1.0 (shifted down), high frequencies lose clarity; we apply a gentle high shelf boost.
    const float semitones = 12.0f * (std::log(std::max(0.25f, pitchRatio)) / std::numbers::ln2_v<float>) + m_formantShift;
    const float gainDb = m_formantPreservation ? -0.75f * semitones : 0.0f;
    const float gainLinear = std::pow(10.0f, std::clamp(gainDb, -6.0f, 6.0f) / 20.0f);

    constexpr double f0 = 2600.0; // Vocal tract formant center (~2.6 kHz)
    const double K = std::tan(std::numbers::pi * f0 / m_sampleRate);
    const double V = gainLinear;

    if (gainDb >= 0.0f)
    {
        // High shelf boost
        const double a0 = 1.0 + std::numbers::sqrt2 * K + K * K;
        m_formantFilterL.b0 = static_cast<float>((V + std::sqrt(2.0 * V) * K + K * K) / a0);
        m_formantFilterL.b1 = static_cast<float>(2.0 * (K * K - V) / a0);
        m_formantFilterL.b2 = static_cast<float>((V - std::sqrt(2.0 * V) * K + K * K) / a0);
        m_formantFilterL.a1 = static_cast<float>(2.0 * (K * K - 1.0) / a0);
        m_formantFilterL.a2 = static_cast<float>((1.0 - std::numbers::sqrt2 * K + K * K) / a0);
    }
    else
    {
        // High shelf cut
        const double a0 = V + std::sqrt(2.0 * V) * K + K * K;
        m_formantFilterL.b0 = static_cast<float>((1.0 + std::numbers::sqrt2 * K + K * K) / a0);
        m_formantFilterL.b1 = static_cast<float>(2.0 * (K * K - 1.0) / a0);
        m_formantFilterL.b2 = static_cast<float>((1.0 - std::numbers::sqrt2 * K + K * K) / a0);
        m_formantFilterL.a1 = static_cast<float>(2.0 * (K * K - V) / a0);
        m_formantFilterL.a2 = static_cast<float>((V - std::sqrt(2.0 * V) * K + K * K) / a0);
    }

    m_formantFilterR.b0 = m_formantFilterL.b0;
    m_formantFilterR.b1 = m_formantFilterL.b1;
    m_formantFilterR.b2 = m_formantFilterL.b2;
    m_formantFilterR.a1 = m_formantFilterL.a1;
    m_formantFilterR.a2 = m_formantFilterL.a2;
}

void VocalPitchCorrectorNode::process(ProcessContext& context) noexcept
{
    if (context.audio.channelCount == 0 || context.audio.frameCount == 0)
    {
        return;
    }

    const auto frameCount = context.audio.frameCount;
    auto* ch0 = context.audio.channels[0];
    auto* ch1 = (context.audio.channelCount >= 2) ? context.audio.channels[1] : ch0;

    if (ch0 == nullptr)
    {
        return;
    }

    m_streamTimeSec += static_cast<float>(frameCount) / static_cast<float>(m_sampleRate);

    // 1. Stream samples into analysis ring buffer
    for (std::size_t i = 0; i < frameCount; ++i)
    {
        const float monoSample = (ch1 != nullptr) ? 0.5f * (ch0[i] + ch1[i]) : ch0[i];
        m_analysisBuffer[m_analysisWritePos] = monoSample;
        m_analysisWritePos = (m_analysisWritePos + 1) % yinBufferSize;
    }

    // 2. Perform pitch detection on analysis buffer
    std::array<float, yinBufferSize> linearBuf {};
    for (std::size_t i = 0; i < yinBufferSize; ++i)
    {
        linearBuf[i] = m_analysisBuffer[(m_analysisWritePos + i) % yinBufferSize];
    }

    m_lastPitchInfo = detectPitchYin(linearBuf.data(), yinBufferSize);

    // 3. Compute pitch correction delta in semitones
    float targetDeltaSemitones = 0.0f;
    if (m_lastPitchInfo.isVoiced && m_correctionAmount > 0.001f)
    {
        float rawDelta = m_lastPitchInfo.targetMidiPitch - m_lastPitchInfo.midiPitch;

        // Humanize deadband: allow micro-intonation within tolerance
        if (m_humanizeAmount > 0.001f)
        {
            const float deadband = m_humanizeAmount * 0.25f; // Up to 25 cents deadband
            if (std::abs(rawDelta) < deadband)
            {
                rawDelta = 0.0f;
            }
            else if (rawDelta > 0.0f)
            {
                rawDelta -= deadband;
            }
            else
            {
                rawDelta += deadband;
            }
        }

        // Natural Vibrato Protection:
        // Human vibrato oscillates at ~4.5 - 7.0 Hz. We separate this vibrato modulation
        // and re-inject it so the baseline pitch is centered while vibrato is preserved!
        if (m_vibratoProtection > 0.001f)
        {
            // Simple 2nd-order resonant bandpass at ~5.5 Hz for block-rate modulation
            constexpr float fVibrato = 5.5f;
            const float dt = static_cast<float>(frameCount) / static_cast<float>(m_sampleRate);
            const float w0 = 2.0f * std::numbers::pi_v<float> * fVibrato * dt;
            m_vibratoBpState1 += w0 * (rawDelta - m_vibratoBpState2);
            m_vibratoBpState2 += w0 * m_vibratoBpState1;
            m_extractedVibratoSemitones = m_vibratoBpState1 * 0.5f;

            // Retain baseline pitch shift and preserve vibrato
            rawDelta = (rawDelta - m_extractedVibratoSemitones * m_vibratoProtection);
        }

        targetDeltaSemitones = rawDelta * m_correctionAmount;
        targetDeltaSemitones = std::clamp(targetDeltaSemitones, -4.0f, 4.0f);
    }

    // 4. Retune speed smoothing:
    // retuneSpeed == 0.0f -> Instant Hard-Tune snap (0ms smoothing)
    // retuneSpeed > 0.0f -> Exponential lag for natural/transparent correction
    if (m_retuneSpeed <= 0.001f)
    {
        m_smoothedCorrectionSemitones = targetDeltaSemitones;
    }
    else
    {
        const float timeConstantSec = m_retuneSpeed * 0.15f; // Up to 150ms
        const float alpha = std::exp(-static_cast<float>(frameCount) / (timeConstantSec * static_cast<float>(m_sampleRate)));
        m_smoothedCorrectionSemitones = alpha * m_smoothedCorrectionSemitones + (1.0f - alpha) * targetDeltaSemitones;
    }

    // Telemetry Trajectory Recording
    PitchTrajectoryPoint trajPoint {};
    trajPoint.timestampSec = m_streamTimeSec;
    trajPoint.detectedHz = m_lastPitchInfo.pitchHz;
    trajPoint.detectedMidiPitch = m_lastPitchInfo.midiPitch;
    trajPoint.targetMidiPitch = m_lastPitchInfo.targetMidiPitch;
    trajPoint.correctedMidiPitch = m_lastPitchInfo.midiPitch + m_smoothedCorrectionSemitones;
    trajPoint.centsDeviation = (m_lastPitchInfo.targetMidiPitch - m_lastPitchInfo.midiPitch) * 100.0f;
    trajPoint.confidence = m_lastPitchInfo.confidence;
    trajPoint.isVoiced = m_lastPitchInfo.isVoiced;
    trajPoint.isSibilant = m_lastPitchInfo.isSibilant;

    m_trajectoryHistory[m_trajectoryWritePos] = trajPoint;
    m_trajectoryWritePos = (m_trajectoryWritePos + 1) % trajectoryHistorySize;

    // Pitch shift ratio
    const float pitchRatio = std::pow(2.0f, (m_smoothedCorrectionSemitones + m_formantShift) / 12.0f);
    updateFormantFilter(pitchRatio);

    // 5. Dual-tap rotating delay line pitch shifter
    const float windowSize = std::min(1440.0f, static_cast<float>(delayBufferSize / 2));
    const float rate = (1.0f - pitchRatio) / windowSize;

    for (std::size_t i = 0; i < frameCount; ++i)
    {
        const float inL = ch0[i];
        const float inR = (ch1 != nullptr) ? ch1[i] : inL;

        // Write dry input to circular delay line
        m_delayBufferL[m_delayWritePos] = inL;
        if (ch1 != nullptr)
        {
            m_delayBufferR[m_delayWritePos] = inR;
        }

        // If unvoiced/sibilant or unity pitch ratio, pass dry signal without delay artifact
        if (m_lastPitchInfo.isSibilant || !m_lastPitchInfo.isVoiced || std::abs(pitchRatio - 1.0f) < 0.0001f)
        {
            m_delayWritePos = (m_delayWritePos + 1) % delayBufferSize;
            continue;
        }

        // Advance tap phases
        m_phaseTapA += rate;
        m_phaseTapB += rate;

        while (m_phaseTapA >= 1.0f) { m_phaseTapA -= 1.0f; }
        while (m_phaseTapA < 0.0f) { m_phaseTapA += 1.0f; }

        while (m_phaseTapB >= 1.0f) { m_phaseTapB -= 1.0f; }
        while (m_phaseTapB < 0.0f) { m_phaseTapB += 1.0f; }

        // Compute delay taps in samples
        const float delayA = m_phaseTapA * windowSize;
        const float delayB = m_phaseTapB * windowSize;

        // Linear interpolation from circular delay line
        auto readDelay = [](const std::vector<float>& buf, std::size_t writePos, float delay) noexcept -> float
        {
            float readPos = static_cast<float>(writePos) - delay;
            while (readPos < 0.0f) { readPos += static_cast<float>(delayBufferSize); }
            while (readPos >= static_cast<float>(delayBufferSize)) { readPos -= static_cast<float>(delayBufferSize); }

            const auto idx0 = static_cast<std::size_t>(readPos);
            const auto idx1 = (idx0 + 1) % delayBufferSize;
            const float frac = readPos - static_cast<float>(idx0);

            return buf[idx0] + frac * (buf[idx1] - buf[idx0]);
        };

        const float outAL = readDelay(m_delayBufferL, m_delayWritePos, delayA);
        const float outBL = readDelay(m_delayBufferL, m_delayWritePos, delayB);

        // Triangular / Hann window crossfading between taps
        const float weightA = 0.5f * (1.0f - std::cos(m_phaseTapA * 2.0f * std::numbers::pi_v<float>));
        const float weightB = 0.5f * (1.0f - std::cos(m_phaseTapB * 2.0f * std::numbers::pi_v<float>));
        const float norm = (weightA + weightB > 1e-6f) ? (1.0f / (weightA + weightB)) : 1.0f;

        const float pitchShiftedL = (outAL * weightA + outBL * weightB) * norm;
        ch0[i] = m_formantFilterL.process(pitchShiftedL);

        if (ch1 != nullptr)
        {
            const float outAR = readDelay(m_delayBufferR, m_delayWritePos, delayA);
            const float outBR = readDelay(m_delayBufferR, m_delayWritePos, delayB);
            const float pitchShiftedR = (outAR * weightA + outBR * weightB) * norm;
            ch1[i] = m_formantFilterR.process(pitchShiftedR);
        }

        m_delayWritePos = (m_delayWritePos + 1) % delayBufferSize;
    }
}

} // namespace dawg::engine::dsp
