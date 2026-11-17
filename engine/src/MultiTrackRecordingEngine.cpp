#include <dawg/engine/MultiTrackRecordingEngine.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

namespace dawg::engine {

void LatencyLoopbackCalibrator::generateTestImpulse(float* outputBuffer, std::size_t sampleCount) const noexcept
{
    if (outputBuffer == nullptr || sampleCount == 0)
        return;

    std::fill_n(outputBuffer, sampleCount, 0.0f);

    // Bandlimited sync pulse centered at sample 64 (or sampleCount / 4)
    constexpr std::size_t pulseCenter = 64;
    if (sampleCount <= pulseCenter + 16)
    {
        if (sampleCount > 0)
            outputBuffer[0] = 1.0f;
        return;
    }

    for (int k = -16; k <= 16; ++k)
    {
        const auto idx = static_cast<std::size_t>(static_cast<int>(pulseCenter) + k);
        if (idx < sampleCount)
        {
            if (k == 0)
            {
                outputBuffer[idx] = 1.0f;
            }
            else
            {
                const float x = static_cast<float>(k) * 0.5f;
                const float sinc = std::sin(std::numbers::pi_v<float> * x) / (std::numbers::pi_v<float> * x);
                // Hann window taper
                const float win = 0.5f * (1.0f + std::cos(std::numbers::pi_v<float> * static_cast<float>(k) / 16.0f));
                outputBuffer[idx] = sinc * win;
            }
        }
    }
}

double LatencyLoopbackCalibrator::measureRoundTripLatency(
    const float* recordedInput,
    std::size_t sampleCount,
    std::size_t searchOffset) const noexcept
{
    if (recordedInput == nullptr || sampleCount < searchOffset + 3)
        return 0.0;

    // Find the peak magnitude in the recorded buffer
    std::size_t peakIdx = searchOffset;
    float peakVal = 0.0f;

    for (std::size_t i = searchOffset; i < sampleCount; ++i)
    {
        const float absVal = std::abs(recordedInput[i]);
        if (absVal > peakVal)
        {
            peakVal = absVal;
            peakIdx = i;
        }
    }

    if (peakVal < 1e-4f || peakIdx <= searchOffset || peakIdx >= sampleCount - 1)
        return static_cast<double>(peakIdx);

    // Parabolic (quadratic) peak interpolation for sub-sample accuracy:
    // delta = 0.5 * (alpha - gamma) / (alpha - 2*beta + gamma)
    const double alpha = std::abs(recordedInput[peakIdx - 1]);
    const double beta = std::abs(recordedInput[peakIdx]);
    const double gamma = std::abs(recordedInput[peakIdx + 1]);

    const double denom = alpha - 2.0 * beta + gamma;
    if (std::abs(denom) < 1e-9)
        return static_cast<double>(peakIdx);

    const double delta = 0.5 * (alpha - gamma) / denom;
    return static_cast<double>(peakIdx) + std::clamp(delta, -0.5, 0.5);
}

MultiTrackRecordingEngine::MultiTrackRecordingEngine() noexcept
{
    reset();
}

void MultiTrackRecordingEngine::reset() noexcept
{
    m_punchEnabled = false;
    m_punchInSample = 0;
    m_punchOutSample = 0;
    m_preRollBars = 0;
    m_latencyOffsetSamples = 0;
    m_loopRecording = false;
    m_loopStartSample = 0;
    m_loopEndSample = 0;
    m_currentTakeIndex = 1;

    for (auto& cfg : m_trackConfigs)
    {
        cfg = TrackRecordConfig {};
    }

    for (auto& rb : m_ringBuffers)
    {
        [[maybe_unused]] const bool ok = rb.prepare(1, 48000 * 4); // 4 seconds mono default buffer
        (void)ok;
        rb.reset();
    }
}

void MultiTrackRecordingEngine::setTrackConfig(std::size_t trackIdx, const TrackRecordConfig& config) noexcept
{
    if (trackIdx < maxRecordTracks)
        m_trackConfigs[trackIdx] = config;
}

const TrackRecordConfig& MultiTrackRecordingEngine::getTrackConfig(std::size_t trackIdx) const noexcept
{
    if (trackIdx < maxRecordTracks)
        return m_trackConfigs[trackIdx];
    static constexpr TrackRecordConfig kDefaultConfig {};
    return kDefaultConfig;
}

TrackRecordConfig& MultiTrackRecordingEngine::getTrackConfig(std::size_t trackIdx) noexcept
{
    if (trackIdx < maxRecordTracks)
        return m_trackConfigs[trackIdx];
    static TrackRecordConfig kFallback {};
    return kFallback;
}

void MultiTrackRecordingEngine::setPunchPoints(std::uint64_t inSample, std::uint64_t outSample) noexcept
{
    m_punchInSample = inSample;
    m_punchOutSample = std::max(inSample, outSample);
}

void MultiTrackRecordingEngine::setLoopRegion(std::uint64_t startSample, std::uint64_t endSample) noexcept
{
    m_loopStartSample = startSample;
    m_loopEndSample = std::max(startSample, endSample);
    m_currentTakeIndex = 1;
}

float MultiTrackRecordingEngine::computePunchFade(std::uint64_t frameTimelinePos) const noexcept
{
    if (!m_punchEnabled)
        return 1.0f;

    // Before punch in fade region
    if (m_punchInSample > kFadeSamples && frameTimelinePos < m_punchInSample - kFadeSamples)
        return 0.0f;
    if (m_punchInSample <= kFadeSamples && frameTimelinePos < m_punchInSample)
    {
        const float t = static_cast<float>(frameTimelinePos) / static_cast<float>(std::max(1u, static_cast<std::uint32_t>(m_punchInSample)));
        return 0.5f * (1.0f - std::cos(std::numbers::pi_v<float> * t));
    }

    // Fade-in region
    if (frameTimelinePos >= m_punchInSample - kFadeSamples && frameTimelinePos < m_punchInSample)
    {
        const float t = static_cast<float>(frameTimelinePos - (m_punchInSample - kFadeSamples)) / static_cast<float>(kFadeSamples);
        return 0.5f * (1.0f - std::cos(std::numbers::pi_v<float> * t));
    }

    // Fully active punch recording region
    if (frameTimelinePos >= m_punchInSample && frameTimelinePos <= m_punchOutSample)
        return 1.0f;

    // Fade-out region
    if (frameTimelinePos > m_punchOutSample && frameTimelinePos <= m_punchOutSample + kFadeSamples)
    {
        const float t = static_cast<float>(frameTimelinePos - m_punchOutSample) / static_cast<float>(kFadeSamples);
        return 0.5f * (1.0f + std::cos(std::numbers::pi_v<float> * t));
    }

    // After punch-out fade
    return 0.0f;
}

void MultiTrackRecordingEngine::processRecording(
    const AudioBufferView& hardwareInputs,
    AudioBufferView& monitorOut,
    AudioBufferView* cueOutA,
    AudioBufferView* cueOutB,
    std::uint64_t currentTimelineSample,
    std::size_t frameCount) noexcept
{
    if (frameCount == 0 || hardwareInputs.channels == nullptr)
        return;

    // Check loop wrap-around
    if (m_loopRecording && m_loopEndSample > m_loopStartSample)
    {
        if (currentTimelineSample >= m_loopEndSample)
        {
            m_currentTakeIndex++;
        }
    }

    // Temporary scratch buffer on stack for faded recording
    alignas(16) float scratchRecordBlock[1024];
    const std::size_t chunkFrames = std::min(frameCount, std::size_t {1024});

    for (std::size_t t = 0; t < maxRecordTracks; ++t)
    {
        const auto& cfg = m_trackConfigs[t];
        if (!cfg.armed && !cfg.inputMonitoring)
            continue;

        if (cfg.inputChannel >= hardwareInputs.channelCount)
            continue;

        const float* inChannel = hardwareInputs.channels[cfg.inputChannel];
        if (inChannel == nullptr)
            continue;

        const float phaseSign = cfg.phaseInvert ? -1.0f : 1.0f;
        const float monitorGainLin = std::pow(10.0f, cfg.monitorGainDb / 20.0f) * phaseSign;

        // Equal-power stereo pan law for direct monitoring
        const float panAngle = (std::clamp(cfg.monitorPan, -1.0f, 1.0f) + 1.0f) * 0.25f * std::numbers::pi_v<float>;
        const float panGainL = std::cos(panAngle) * monitorGainLin;
        const float panGainR = std::sin(panAngle) * monitorGainLin;

        // Direct Low-Latency Input Monitoring to Master Monitor Out
        if (cfg.inputMonitoring && monitorOut.channelCount >= 2 && monitorOut.channels != nullptr)
        {
            float* outL = monitorOut.channels[0];
            float* outR = monitorOut.channels[1];

            if (outL != nullptr && outR != nullptr)
            {
                for (std::size_t i = 0; i < frameCount; ++i)
                {
                    const float s = inChannel[i];
                    outL[i] += s * panGainL;
                    outR[i] += s * panGainR;
                }
            }
        }

        // Cue Mix A
        if (cueOutA != nullptr && cueOutA->channelCount >= 2 && cueOutA->channels != nullptr && cfg.cueSends[0].enabled && !cfg.cueSends[0].muted)
        {
            const float cueGainLin = std::pow(10.0f, cfg.cueSends[0].gainDb / 20.0f) * phaseSign;
            const float cuePanAngle = (std::clamp(cfg.cueSends[0].pan, -1.0f, 1.0f) + 1.0f) * 0.25f * std::numbers::pi_v<float>;
            const float cuePanL = std::cos(cuePanAngle) * cueGainLin;
            const float cuePanR = std::sin(cuePanAngle) * cueGainLin;

            float* outA0 = cueOutA->channels[0];
            float* outA1 = cueOutA->channels[1];

            if (outA0 != nullptr && outA1 != nullptr)
            {
                for (std::size_t i = 0; i < frameCount; ++i)
                {
                    const float s = inChannel[i];
                    outA0[i] += s * cuePanL;
                    outA1[i] += s * cuePanR;
                }
            }
        }

        // Cue Mix B
        if (cueOutB != nullptr && cueOutB->channelCount >= 2 && cueOutB->channels != nullptr && cfg.cueSends[1].enabled && !cfg.cueSends[1].muted)
        {
            const float cueGainLin = std::pow(10.0f, cfg.cueSends[1].gainDb / 20.0f) * phaseSign;
            const float cuePanAngle = (std::clamp(cfg.cueSends[1].pan, -1.0f, 1.0f) + 1.0f) * 0.25f * std::numbers::pi_v<float>;
            const float cuePanL = std::cos(cuePanAngle) * cueGainLin;
            const float cuePanR = std::sin(cuePanAngle) * cueGainLin;

            float* outB0 = cueOutB->channels[0];
            float* outB1 = cueOutB->channels[1];

            if (outB0 != nullptr && outB1 != nullptr)
            {
                for (std::size_t i = 0; i < frameCount; ++i)
                {
                    const float s = inChannel[i];
                    outB0[i] += s * cuePanL;
                    outB1[i] += s * cuePanR;
                }
            }
        }

        // Recording to Ring Buffer
        if (cfg.armed)
        {
            for (std::size_t i = 0; i < chunkFrames; ++i)
            {
                const std::uint64_t samplePos = currentTimelineSample + i;
                const float punchGain = computePunchFade(samplePos);
                scratchRecordBlock[i] = inChannel[i] * punchGain;
            }

            const float* const ptrArray[1] = {scratchRecordBlock};
            [[maybe_unused]] const auto written = m_ringBuffers[t].write(std::span<const float* const>(ptrArray, 1), chunkFrames);
            (void)written;
        }
    }
}

BwfBroadcastChunk MultiTrackRecordingEngine::createBwfChunk(
    std::uint64_t timelineSample,
    const std::string& trackName)
{
    BwfBroadcastChunk chunk {};
    chunk.description = "DAWg Multi-Track Recording - " + trackName;
    chunk.originator = "DAWg Studio Engine v2.0";
    chunk.originatorReference = "DAWG-TK-" + std::to_string(timelineSample);
    chunk.originationDate = "2026-11-17";
    chunk.originationTime = "04:30:00";
    chunk.timeReferenceSamples = timelineSample;
    return chunk;
}

void MultiTrackRecordingEngine::generateWaveformOverview(
    const float* audioData,
    std::size_t sampleCount,
    std::vector<WaveformPeakPoint>& outOverview)
{
    outOverview.clear();
    if (audioData == nullptr || sampleCount == 0)
        return;

    const std::size_t numBlocks = (sampleCount + overviewBlockSize - 1) / overviewBlockSize;
    outOverview.reserve(numBlocks);

    for (std::size_t b = 0; b < numBlocks; ++b)
    {
        const std::size_t start = b * overviewBlockSize;
        const std::size_t end = std::min(start + overviewBlockSize, sampleCount);

        float minVal = audioData[start];
        float maxVal = audioData[start];

        for (std::size_t i = start + 1; i < end; ++i)
        {
            const float s = audioData[i];
            if (s < minVal) minVal = s;
            if (s > maxVal) maxVal = s;
        }

        outOverview.push_back(WaveformPeakPoint {minVal, maxVal});
    }
}

} // namespace dawg::engine
