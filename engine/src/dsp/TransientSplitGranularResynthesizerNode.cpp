#include <dawg/engine/dsp/TransientSplitGranularResynthesizerNode.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::engine::dsp {

TransientSplitGranularResynthesizerNode::TransientSplitGranularResynthesizerNode() noexcept
{
    reset();
}

bool TransientSplitGranularResynthesizerNode::prepare(
    double sampleRate,
    std::size_t /*maxBlockSize*/,
    std::uint16_t /*channelCount*/)
{
    m_sampleRate = (sampleRate > 1000.0) ? sampleRate : 48000.0;
    const auto historySize = static_cast<std::size_t>(BufferSeconds * m_sampleRate);

    m_historyBufferL.assign(historySize, 0.0f);
    m_historyBufferR.assign(historySize, 0.0f);

    const float fs = static_cast<float>(m_sampleRate);
    m_fastCoeff = 1.0f - std::exp(-1.0f / (0.001f * fs)); // ~1 ms fast attack
    m_slowCoeff = 1.0f - std::exp(-1.0f / (0.025f * fs)); // ~25 ms sustain decay

    reset();
    return true;
}

void TransientSplitGranularResynthesizerNode::reset() noexcept
{
    m_fastEnv = 0.0f;
    m_slowEnv = 0.0f;
    m_writeIndex = 0;
    m_grainSpawnCounter = 0.0f;
    m_combIdx = 0;

    if (!m_historyBufferL.empty())
    {
        std::fill(m_historyBufferL.begin(), m_historyBufferL.end(), 0.0f);
        std::fill(m_historyBufferR.begin(), m_historyBufferR.end(), 0.0f);
    }

    for (auto& g : m_grains)
    {
        g.active = false;
    }

    m_combBufferL.fill(0.0f);
    m_combBufferR.fill(0.0f);
}

float TransientSplitGranularResynthesizerNode::generateRandomFloat() noexcept
{
    m_rngSeed = m_rngSeed * 1664525u + 1013904223u;
    return static_cast<float>(m_rngSeed & 0x00FFFFFF) / 16777216.0f;
}

float TransientSplitGranularResynthesizerNode::calculateWindow(float phaseNormalized) const noexcept
{
    const float p = std::clamp(phaseNormalized, 0.0f, 1.0f);
    switch (m_windowType)
    {
    case GranularWindowType::hann:
        return 0.5f * (1.0f - std::cos(2.0f * std::numbers::pi_v<float> * p));

    case GranularWindowType::gaussian:
    {
        const float x = (p - 0.5f) / 0.18f;
        return std::exp(-0.5f * x * x);
    }

    case GranularWindowType::blackman:
        return 0.42f - 0.5f * std::cos(2.0f * std::numbers::pi_v<float> * p)
                     + 0.08f * std::cos(4.0f * std::numbers::pi_v<float> * p);
    }
    return 1.0f;
}

void TransientSplitGranularResynthesizerNode::spawnGrain(
    std::size_t writePos,
    std::size_t bufferSize) noexcept
{
    Grain* freeGrain = nullptr;
    for (auto& g : m_grains)
    {
        if (!g.active)
        {
            freeGrain = &g;
            break;
        }
    }

    if (freeGrain == nullptr || bufferSize == 0)
    {
        return;
    }

    freeGrain->active = true;
    const float fs = static_cast<float>(m_sampleRate);
    freeGrain->totalSamples = std::clamp(m_grainSizeMs * 0.001f * fs, 16.0f, fs * 0.5f);
    freeGrain->currentSample = 0.0f;

    // Position grain read head 10ms to 200ms behind write head with random scrub
    const float maxOffsetSamples = std::min(static_cast<float>(bufferSize) * 0.8f, 0.25f * fs);
    const float minOffsetSamples = std::max(64.0f, 0.015f * fs);
    const float scrubOffset = minOffsetSamples + generateRandomFloat() * (maxOffsetSamples - minOffsetSamples);

    double readHead = static_cast<double>(writePos) - static_cast<double>(scrubOffset);
    while (readHead < 0.0)
    {
        readHead += static_cast<double>(bufferSize);
    }
    freeGrain->readPos = readHead;

    // Pitch scatter quantization
    float semitones = 0.0f;
    if (m_pitchScatterAmount > 0.001f)
    {
        switch (m_pitchMode)
        {
        case PitchScatterMode::free:
            semitones = (generateRandomFloat() * 2.0f - 1.0f) * 12.0f * m_pitchScatterAmount;
            break;

        case PitchScatterMode::octaves:
        {
            constexpr std::array<float, 5> octs = {-24.0f, -12.0f, 0.0f, 12.0f, 24.0f};
            const auto idx = static_cast<std::size_t>(generateRandomFloat() * 4.99f);
            semitones = octs[idx] * m_pitchScatterAmount;
            break;
        }

        case PitchScatterMode::fifthsAndOctaves:
        {
            constexpr std::array<float, 6> fifths = {-12.0f, -5.0f, 0.0f, 7.0f, 12.0f, 19.0f};
            const auto idx = static_cast<std::size_t>(generateRandomFloat() * 5.99f);
            semitones = fifths[idx] * m_pitchScatterAmount;
            break;
        }

        case PitchScatterMode::chordHarmonics:
        {
            constexpr std::array<float, 7> chord = {0.0f, 3.0f, 4.0f, 7.0f, 10.0f, 12.0f, 16.0f};
            const auto idx = static_cast<std::size_t>(generateRandomFloat() * 6.99f);
            semitones = chord[idx] * m_pitchScatterAmount;
            break;
        }
        }
    }
    freeGrain->playbackRate = std::pow(2.0, static_cast<double>(semitones) / 12.0);

    // 3D Spatial Pan Jitter
    const float pan = 0.5f + (generateRandomFloat() * 2.0f - 1.0f) * 0.5f * m_panJitter;
    const float panRad = std::clamp(pan, 0.0f, 1.0f) * (0.5f * std::numbers::pi_v<float>);
    freeGrain->panL = std::cos(panRad);
    freeGrain->panR = std::sin(panRad);
}

void TransientSplitGranularResynthesizerNode::process(ProcessContext& context) noexcept
{
    auto& audio = context.audio;
    if (audio.channels == nullptr || audio.frameCount == 0 || audio.channelCount == 0 || m_historyBufferL.empty())
    {
        return;
    }

    const std::size_t numSamples = audio.frameCount;
    const std::size_t numChannels = audio.channelCount;
    float* outL = audio.channels[0];
    float* outR = (numChannels > 1) ? audio.channels[1] : nullptr;

    const std::size_t bufSize = m_historyBufferL.size();
    const float fs = static_cast<float>(m_sampleRate);
    const float grainSpawnInc = m_density / fs;

    for (std::size_t i = 0; i < numSamples; ++i)
    {
        const float inL = outL[i];
        const float inR = (outR != nullptr) ? outR[i] : inL;

        // 1. Dual-Path Transient-Sustain Ballistic Decomposition
        const float mag = std::max(std::abs(inL), std::abs(inR));
        m_fastEnv += m_fastCoeff * (mag - m_fastEnv);
        m_slowEnv += m_slowCoeff * (mag - m_slowEnv);

        // Transient energy is fast attack rise above slow baseline
        const float rawTransient = std::max(0.0f, m_fastEnv - m_slowEnv);
        const float transientRatio = std::clamp(rawTransient * (2.0f + 8.0f * m_transientSens), 0.0f, 1.0f);

        // Discrete separation
        const float transL = inL * transientRatio * m_transientGain;
        const float transR = inR * transientRatio * m_transientGain;

        const float sustL = inL * (1.0f - transientRatio) * m_sustainGain;
        const float sustR = inR * (1.0f - transientRatio) * m_sustainGain;

        // 2. Write sustained resonant body into granular recording history
        m_historyBufferL[m_writeIndex] = sustL;
        m_historyBufferR[m_writeIndex] = sustR;

        // 3. Grain Spawning Engine
        m_grainSpawnCounter += grainSpawnInc;
        if (m_grainSpawnCounter >= 1.0f)
        {
            m_grainSpawnCounter -= 1.0f;
            spawnGrain(m_writeIndex, bufSize);
        }

        // 4. Render Active Grains
        float grainCloudL = 0.0f;
        float grainCloudR = 0.0f;

        for (auto& g : m_grains)
        {
            if (!g.active)
            {
                continue;
            }

            const float phase = g.currentSample / g.totalSamples;
            const float window = calculateWindow(phase);

            // Interpolated buffer sample read
            const auto idx0 = static_cast<std::size_t>(g.readPos) % bufSize;
            const auto idx1 = (idx0 + 1) % bufSize;
            const float frac = static_cast<float>(g.readPos - static_cast<double>(static_cast<std::size_t>(g.readPos)));

            const float sampL = m_historyBufferL[idx0] + frac * (m_historyBufferL[idx1] - m_historyBufferL[idx0]);
            const float sampR = m_historyBufferR[idx0] + frac * (m_historyBufferR[idx1] - m_historyBufferR[idx0]);

            grainCloudL += sampL * window * g.panL;
            grainCloudR += sampR * window * g.panR;

            // Advance grain
            g.readPos += g.playbackRate;
            while (g.readPos >= static_cast<double>(bufSize))
            {
                g.readPos -= static_cast<double>(bufSize);
            }
            g.currentSample += 1.0f;
            if (g.currentSample >= g.totalSamples)
            {
                g.active = false;
            }
        }

        // 5. Tuned Feedback Comb Filter
        const float combOutL = m_combBufferL[m_combIdx];
        const float combOutR = m_combBufferR[m_combIdx];

        m_combBufferL[m_combIdx] = grainCloudL + combOutL * m_feedback;
        m_combBufferR[m_combIdx] = grainCloudR + combOutR * m_feedback;

        m_combIdx = (m_combIdx + 1) % CombSize;

        const float resynthL = grainCloudL + combOutL * (m_feedback * 0.7f);
        const float resynthR = grainCloudR + combOutR * (m_feedback * 0.7f);

        // 6. Recombine: Transients pass cleanly for punch + resynthesized grain cloud
        const float wetL = transL + resynthL;
        const float wetR = transR + resynthR;

        outL[i] = m_mix * wetL + (1.0f - m_mix) * inL;
        if (outR != nullptr)
        {
            outR[i] = m_mix * wetR + (1.0f - m_mix) * inR;
        }

        m_writeIndex = (m_writeIndex + 1) % bufSize;
    }
}

} // namespace dawg::engine::dsp
