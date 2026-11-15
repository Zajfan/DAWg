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

    // Also check candidate wrap-around in next octave (e.g. interval 0 at +12)
    const float upperCandidateDiff = std::abs(pitchClass - (static_cast<float>(intervals[0]) + 12.0f));
    if (upperCandidateDiff < minDiff)
    {
        closestInterval = static_cast<float>(intervals[0]) + 12.0f;
    }

    return static_cast<float>(octave * 12) + closestInterval + static_cast<float>(rootNote % 12);
}

DetectedPitchInfo VocalPitchCorrectorNode::detectPitchYin(const float* buffer, const std::size_t size) const noexcept
{
    DetectedPitchInfo info {};
    if (buffer == nullptr || size < 256 || m_sampleRate <= 0.0)
    {
        return info;
    }

    const auto halfSize = size / 2;
    std::array<float, yinBufferSize / 2> dPrime {};

    // 1. Difference function
    // Limits: 50 Hz to 1200 Hz
    const auto minTau = std::max<std::size_t>(2, static_cast<std::size_t>(m_sampleRate / 1200.0));
    const auto maxTau = std::min<std::size_t>(halfSize - 1, static_cast<std::size_t>(m_sampleRate / 50.0));

    // Tau 0
    dPrime[0] = 1.0f;
    float runningSum = 0.0f;

    for (std::size_t tau = 1; tau < halfSize; ++tau)
    {
        float diffSum = 0.0f;
        for (std::size_t j = 0; j < halfSize; ++j)
        {
            const float diff = buffer[j] - buffer[j + tau];
            diffSum += diff * diff;
        }

        runningSum += diffSum;
        dPrime[tau] = (runningSum > 1e-9f) ? (diffSum * static_cast<float>(tau) / runningSum) : 1.0f;
    }

    // 2. Absolute threshold & local minimum search
    constexpr float threshold = 0.12f;
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

    // Fallback: search global minimum if no value was under threshold
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
        return info;
    }

    // 3. Parabolic Interpolation for sub-sample accuracy
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
        return info;
    }

    const float detectedHz = static_cast<float>(m_sampleRate) / tauStar;
    if (detectedHz < 40.0f || detectedHz > 1500.0f)
    {
        return info;
    }

    const float confidence = std::clamp(1.0f - beta, 0.0f, 1.0f);
    const float midi = 69.0f + 12.0f * std::log2(detectedHz / 440.0f);

    info.pitchHz = detectedHz;
    info.midiPitch = midi;
    info.confidence = confidence;
    info.isVoiced = (confidence >= 0.55f);
    info.targetMidiPitch = quantizeToScale(midi, m_scale, m_rootNote);

    return info;
}

void VocalPitchCorrectorNode::process(ProcessContext& context) noexcept
{
    if (context.audio.channels == nullptr || context.audio.frameCount == 0 || context.audio.channelCount == 0)
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

    // 1. Stream samples into analysis ring buffer
    for (std::size_t i = 0; i < frameCount; ++i)
    {
        const float monoSample = (ch1 != nullptr) ? 0.5f * (ch0[i] + ch1[i]) : ch0[i];
        m_analysisBuffer[m_analysisWritePos] = monoSample;
        m_analysisWritePos = (m_analysisWritePos + 1) % yinBufferSize;
    }

    // 2. Perform pitch detection on analysis buffer
    // Unroll linear view from circular buffer
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
        targetDeltaSemitones = (m_lastPitchInfo.targetMidiPitch - m_lastPitchInfo.midiPitch) * m_correctionAmount;
        // Clamp extreme jumps
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

    // Pitch shift ratio
    const float pitchRatio = std::pow(2.0f, (m_smoothedCorrectionSemitones + m_formantShift) / 12.0f);

    // 5. Dual-tap rotating delay line pitch shifter
    // Window length: ~30ms (1440 samples @ 48kHz)
    const float windowSize = std::min(1440.0f, static_cast<float>(delayBufferSize / 2));
    const float rate = (1.0f - pitchRatio) / windowSize;

    for (std::size_t i = 0; i < frameCount; ++i)
    {
        // Write dry input to circular delay line
        m_delayBufferL[m_delayWritePos] = ch0[i];
        if (ch1 != nullptr)
        {
            m_delayBufferR[m_delayWritePos] = ch1[i];
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

        ch0[i] = (outAL * weightA + outBL * weightB) * norm;

        if (ch1 != nullptr)
        {
            const float outAR = readDelay(m_delayBufferR, m_delayWritePos, delayA);
            const float outBR = readDelay(m_delayBufferR, m_delayWritePos, delayB);
            ch1[i] = (outAR * weightA + outBR * weightB) * norm;
        }

        m_delayWritePos = (m_delayWritePos + 1) % delayBufferSize;
    }
}

} // namespace dawg::engine::dsp
