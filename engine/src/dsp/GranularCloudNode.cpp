#include <dawg/engine/dsp/GranularCloudNode.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::engine::dsp {
namespace {

inline float nextRandFloat(std::uint32_t& state) noexcept
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return static_cast<float>(state & 0x00FFFFFF) / static_cast<float>(0x01000000);
}

} // namespace

GranularCloudNode::GranularCloudNode() noexcept
{
    reset();
}

bool GranularCloudNode::prepare(
    const double sampleRate,
    [[maybe_unused]] const std::size_t maxBlockSize,
    [[maybe_unused]] const std::uint16_t channelCount)
{
    m_sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    const auto bufferSamples = static_cast<std::size_t>(m_sampleRate * static_cast<double>(maxBufferSeconds));

    if (m_liveBufferL.size() != bufferSamples)
    {
        m_liveBufferL.assign(bufferSamples, 0.0f);
        m_liveBufferR.assign(bufferSamples, 0.0f);
    }

    reset();
    return true;
}

void GranularCloudNode::reset() noexcept
{
    m_liveWritePos = 0;
    m_spawnAccumulator = 0.0f;
    for (auto& g : m_grains)
    {
        g.active = false;
        g.age = 0;
    }
}

void GranularCloudNode::loadSampleBuffer(const float* left, const float* right, const std::size_t count)
{
    if (left == nullptr || count == 0)
    {
        m_hasCustomSample = false;
        return;
    }

    m_liveBufferL.assign(left, left + count);
    if (right != nullptr)
    {
        m_liveBufferR.assign(right, right + count);
    }
    else
    {
        m_liveBufferR = m_liveBufferL;
    }

    m_hasCustomSample = true;
    m_liveWritePos = 0;
    reset();
}

std::size_t GranularCloudNode::activeGrainCount() const noexcept
{
    std::size_t count = 0;
    for (const auto& g : m_grains)
    {
        if (g.active)
        {
            ++count;
        }
    }
    return count;
}

float GranularCloudNode::evaluateWindow(const std::size_t age, const std::size_t duration) const noexcept
{
    if (duration == 0)
    {
        return 0.0f;
    }

    const float phase = static_cast<float>(age) / static_cast<float>(duration);

    switch (m_windowShape)
    {
    case GrainWindowShape::hann:
        return 0.5f * (1.0f - std::cos(2.0f * std::numbers::pi_v<float> * phase));

    case GrainWindowShape::gaussian:
    {
        constexpr float sigma = 0.28f;
        const float x = (phase - 0.5f) / sigma;
        return std::exp(-0.5f * x * x);
    }

    case GrainWindowShape::tukey:
    {
        // 25% tapered cosine window
        constexpr float alpha = 0.25f;
        if (phase < alpha * 0.5f)
        {
            return 0.5f * (1.0f + std::cos(std::numbers::pi_v<float> * (2.0f * phase / alpha - 1.0f)));
        }
        if (phase > 1.0f - alpha * 0.5f)
        {
            return 0.5f * (1.0f + std::cos(std::numbers::pi_v<float> * (2.0f * (1.0f - phase) / alpha - 1.0f)));
        }
        return 1.0f;
    }
    }

    return 1.0f;
}

void GranularCloudNode::spawnGrain() noexcept
{
    if (m_liveBufferL.empty())
    {
        return;
    }

    // Find first inactive grain or recycle oldest
    Grain* target = nullptr;
    std::size_t maxAge = 0;

    for (auto& g : m_grains)
    {
        if (!g.active)
        {
            target = &g;
            break;
        }
        if (g.age > maxAge)
        {
            maxAge = g.age;
            target = &g;
        }
    }

    if (target == nullptr)
    {
        return;
    }

    const auto bufSize = m_liveBufferL.size();
    const auto durSamples = static_cast<std::size_t>(
        std::max(32.0f, m_grainSizeMs * 0.001f * static_cast<float>(m_sampleRate)));

    target->active = true;
    target->age = 0;
    target->duration = durSamples;

    // Scrub position + randomized position spray
    const float basePos = m_scrubPos * static_cast<float>(bufSize);
    const float sprayRange = m_spray * static_cast<float>(bufSize) * 0.25f;
    const float offset = (nextRandFloat(m_randState) - 0.5f) * 2.0f * sprayRange;
    float startPos = basePos + offset;

    while (startPos < 0.0f) startPos += static_cast<float>(bufSize);
    while (startPos >= static_cast<float>(bufSize)) startPos -= static_cast<float>(bufSize);
    target->readPos = startPos;

    // Pitch speed computation (semitones + detune cents)
    const float detuneCents = (nextRandFloat(m_randState) - 0.5f) * 2.0f * m_pitchDetuneCents;
    const float totalSemitones = m_pitchShiftSemitones + (detuneCents * 0.01f);
    target->speed = std::pow(2.0f, totalSemitones / 12.0f);

    // Reverse probability
    target->reverse = (nextRandFloat(m_randState) < m_reverseProb);

    // Stereo dispersal pan
    const float pan = std::clamp(0.5f + (nextRandFloat(m_randState) - 0.5f) * m_stereoDispersal, 0.0f, 1.0f);
    const float panAngle = pan * (std::numbers::pi_v<float> * 0.5f);
    target->panL = std::cos(panAngle);
    target->panR = std::sin(panAngle);
}

void GranularCloudNode::process(ProcessContext& context) noexcept
{
    if (context.audio.channels == nullptr || context.audio.frameCount == 0 || context.audio.channelCount == 0)
    {
        return;
    }

    const auto frameCount = context.audio.frameCount;
    auto* ch0 = context.audio.channels[0];
    auto* ch1 = (context.audio.channelCount >= 2) ? context.audio.channels[1] : ch0;

    if (ch0 == nullptr || m_liveBufferL.empty())
    {
        return;
    }

    const auto bufSize = m_liveBufferL.size();
    const float dt = 1.0f / static_cast<float>(m_sampleRate);
    const float spawnRate = m_density * dt;

    const float wet = m_dryWet;
    const float dry = 1.0f - wet;

    for (std::size_t i = 0; i < frameCount; ++i)
    {
        const float inL = ch0[i];
        const float inR = (ch1 != nullptr) ? ch1[i] : inL;

        // Record incoming live audio into circular buffer (unless custom sample is loaded)
        if (!m_hasCustomSample)
        {
            m_liveBufferL[m_liveWritePos] = inL;
            m_liveBufferR[m_liveWritePos] = inR;
            m_liveWritePos = (m_liveWritePos + 1) % bufSize;
        }

        // Spawn grains
        m_spawnAccumulator += spawnRate;
        while (m_spawnAccumulator >= 1.0f)
        {
            m_spawnAccumulator -= 1.0f;
            spawnGrain();
        }

        // Render active grains
        float wetL = 0.0f;
        float wetR = 0.0f;
        std::size_t activeCount = 0;

        for (auto& g : m_grains)
        {
            if (!g.active)
            {
                continue;
            }

            ++activeCount;
            const float win = evaluateWindow(g.age, g.duration);

            // Read linear interpolated sample from buffer
            const auto idx0 = static_cast<std::size_t>(g.readPos) % bufSize;
            const auto idx1 = (idx0 + 1) % bufSize;
            const float frac = g.readPos - std::floor(g.readPos);

            const float sL = m_liveBufferL[idx0] + frac * (m_liveBufferL[idx1] - m_liveBufferL[idx0]);
            const float sR = m_liveBufferR[idx0] + frac * (m_liveBufferR[idx1] - m_liveBufferR[idx0]);

            wetL += sL * win * g.panL;
            wetR += sR * win * g.panR;

            // Advance grain playhead
            if (g.reverse)
            {
                g.readPos -= g.speed;
                if (g.readPos < 0.0f)
                {
                    g.readPos += static_cast<float>(bufSize);
                }
            }
            else
            {
                g.readPos += g.speed;
                if (g.readPos >= static_cast<float>(bufSize))
                {
                    g.readPos -= static_cast<float>(bufSize);
                }
            }

            ++g.age;
            if (g.age >= g.duration)
            {
                g.active = false;
            }
        }

        // Normalization scaling based on active grain density
        const float norm = (activeCount > 0) ? (1.5f / std::sqrt(static_cast<float>(activeCount) + 1.0f)) : 0.0f;
        wetL *= norm;
        wetR *= norm;

        // Mix dry / wet
        ch0[i] = dry * inL + wet * wetL;
        if (context.audio.channelCount >= 2)
        {
            ch1[i] = dry * inR + wet * wetR;
        }

        // Advance scrub position when not tape-frozen
        if (!m_frozen && !m_hasCustomSample)
        {
            m_scrubPos = static_cast<float>(m_liveWritePos) / static_cast<float>(bufSize);
        }
    }
}

} // namespace dawg::engine::dsp
