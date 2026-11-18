#include <dawg/engine/dsp/GranularTextureMorphNode.h>

#include <algorithm>
#include <cmath>

namespace dawg::engine::dsp {

namespace {

constexpr float pi = 3.14159265358979323846f;
constexpr float twoPi = 6.28318530717958647692f;

inline float nextRandom0to1(std::uint32_t& state) noexcept
{
    state = state * 1664525u + 1013904223u;
    return static_cast<float>(state >> 8) / 16777216.0f;
}

} // namespace

GranularTextureMorphNode::GranularTextureMorphNode() noexcept
{
    m_bufferL.resize(maxBufferFrames, 0.0f);
    m_bufferR.resize(maxBufferFrames, 0.0f);

    // Synthesize default test sample into buffer (smooth warm harmonic drone)
    m_bufferLength = 48000 * 2; // 2 seconds
    for (std::size_t i = 0; i < m_bufferLength; ++i)
    {
        const float t = static_cast<float>(i) / 48000.0f;
        const float s = 0.5f * std::sin(twoPi * 220.0f * t) +
                        0.25f * std::sin(twoPi * 440.0f * t) +
                        0.125f * std::sin(twoPi * 660.0f * t);
        m_bufferL[i] = s;
        m_bufferR[i] = s;
    }
}

bool GranularTextureMorphNode::prepare(double sampleRate, std::size_t /*maxBlockSize*/, std::uint16_t /*channelCount*/)
{
    m_sampleRate = (sampleRate > 1000.0) ? sampleRate : 48000.0;
    reset();
    return true;
}

void GranularTextureMorphNode::reset() noexcept
{
    for (auto& g : m_grains)
        g.active = false;
    m_spawnCounterSamples = 0.0;
}

void GranularTextureMorphNode::loadSampleBuffer(const float* left, const float* right, const std::size_t count)
{
    if (left == nullptr || count == 0)
        return;

    const std::size_t frames = std::min(count, maxBufferFrames);
    for (std::size_t i = 0; i < frames; ++i)
    {
        m_bufferL[i] = left[i];
        m_bufferR[i] = (right != nullptr) ? right[i] : left[i];
    }
    m_bufferLength = frames;
}

void GranularTextureMorphNode::setPositionX(const float x0to1) noexcept
{
    m_posX.store(std::clamp(x0to1, 0.0f, 1.0f), std::memory_order_release);
}

float GranularTextureMorphNode::positionX() const noexcept
{
    return m_posX.load(std::memory_order_acquire);
}

void GranularTextureMorphNode::setSprayY(const float y0to1) noexcept
{
    m_sprayY.store(std::clamp(y0to1, 0.0f, 1.0f), std::memory_order_release);
}

float GranularTextureMorphNode::sprayY() const noexcept
{
    return m_sprayY.load(std::memory_order_acquire);
}

void GranularTextureMorphNode::setGrainLengthMs(const float lengthMs) noexcept
{
    m_grainLengthMs.store(std::clamp(lengthMs, 1.0f, 500.0f), std::memory_order_release);
}

float GranularTextureMorphNode::grainLengthMs() const noexcept
{
    return m_grainLengthMs.load(std::memory_order_acquire);
}

void GranularTextureMorphNode::setGrainDensity(const float grainsPerSec) noexcept
{
    m_densityHz.store(std::clamp(grainsPerSec, 1.0f, 120.0f), std::memory_order_release);
}

float GranularTextureMorphNode::grainDensity() const noexcept
{
    return m_densityHz.load(std::memory_order_acquire);
}

void GranularTextureMorphNode::setWindowShape(const TextureWindowShape shape) noexcept
{
    m_windowShape.store(shape, std::memory_order_release);
}

TextureWindowShape GranularTextureMorphNode::windowShape() const noexcept
{
    return m_windowShape.load(std::memory_order_acquire);
}

void GranularTextureMorphNode::setReverseProbability(const float prob0to1) noexcept
{
    m_reverseProb.store(std::clamp(prob0to1, 0.0f, 1.0f), std::memory_order_release);
}

float GranularTextureMorphNode::reverseProbability() const noexcept
{
    return m_reverseProb.load(std::memory_order_acquire);
}

void GranularTextureMorphNode::setPanSpread(const float spread0to1) noexcept
{
    m_panSpread.store(std::clamp(spread0to1, 0.0f, 1.0f), std::memory_order_release);
}

float GranularTextureMorphNode::panSpread() const noexcept
{
    return m_panSpread.load(std::memory_order_acquire);
}

void GranularTextureMorphNode::setPitchSemitones(const float semitones) noexcept
{
    m_pitchSemitones.store(std::clamp(semitones, -24.0f, 24.0f), std::memory_order_release);
}

float GranularTextureMorphNode::pitchSemitones() const noexcept
{
    return m_pitchSemitones.load(std::memory_order_acquire);
}

void GranularTextureMorphNode::setShimmerOctaveMix(const float mix0to1) noexcept
{
    m_shimmerMix.store(std::clamp(mix0to1, 0.0f, 1.0f), std::memory_order_release);
}

float GranularTextureMorphNode::shimmerOctaveMix() const noexcept
{
    return m_shimmerMix.load(std::memory_order_acquire);
}

std::size_t GranularTextureMorphNode::activeGrainCount() const noexcept
{
    std::size_t count = 0;
    for (const auto& g : m_grains)
    {
        if (g.active) ++count;
    }
    return count;
}

float GranularTextureMorphNode::evaluateWindow(const TextureWindowShape shape, const float t) const noexcept
{
    const float ct = std::clamp(t, 0.0f, 1.0f);
    switch (shape)
    {
    case TextureWindowShape::hann:
        return 0.5f * (1.0f - std::cos(twoPi * ct));

    case TextureWindowShape::gaussian:
    {
        const float diff = (ct - 0.5f) / 0.18f;
        return std::exp(-0.5f * diff * diff);
    }

    case TextureWindowShape::blackman:
        return 0.42f - 0.5f * std::cos(twoPi * ct) + 0.08f * std::cos(4.0f * pi * ct);

    case TextureWindowShape::rectangular:
        if (ct < 0.02f) return ct / 0.02f;
        if (ct > 0.98f) return (1.0f - ct) / 0.02f;
        return 1.0f;
    }

    return 1.0f;
}

void GranularTextureMorphNode::spawnGrain() noexcept
{
    if (m_bufferLength == 0)
        return;

    // Find inactive grain slot in pool of 256
    TextureGrain* target = nullptr;
    for (auto& g : m_grains)
    {
        if (! g.active)
        {
            target = &g;
            break;
        }
    }
    if (target == nullptr)
        return; // Pool exhausted

    const float baseX = m_posX.load(std::memory_order_relaxed);
    const float spray = m_sprayY.load(std::memory_order_relaxed);
    const float lenMs = m_grainLengthMs.load(std::memory_order_relaxed);
    const auto shape = m_windowShape.load(std::memory_order_relaxed);
    const float revP = m_reverseProb.load(std::memory_order_relaxed);
    const float spread = m_panSpread.load(std::memory_order_relaxed);
    const float pitchSemi = m_pitchSemitones.load(std::memory_order_relaxed);
    const float shimmer = m_shimmerMix.load(std::memory_order_relaxed);

    // Compute duration in samples
    const auto durSamples = static_cast<std::size_t>(std::max(16.0, m_sampleRate * (lenMs * 0.001)));

    // Calculate start position with spray jitter
    const float jitter = (nextRandom0to1(m_prngState) * 2.0f - 1.0f) * spray * 0.5f;
    float normPos = std::clamp(baseX + jitter, 0.0f, 1.0f);
    double startSample = normPos * static_cast<double>(m_bufferLength - 1);

    // Reverse decision
    const bool isRev = (nextRandom0to1(m_prngState) < revP);

    // Resampling pitch speed
    float semi = pitchSemi;
    if (shimmer > 0.001f && nextRandom0to1(m_prngState) < shimmer)
    {
        // Octave harmony shimmer (+12 or +24 semitones)
        semi += (nextRandom0to1(m_prngState) < 0.5f) ? 12.0f : 24.0f;
    }
    const double speed = std::pow(2.0, static_cast<double>(semi) / 12.0);

    // Stereo panning
    const float pan = std::clamp(0.5f + (nextRandom0to1(m_prngState) - 0.5f) * spread, 0.0f, 1.0f);
    const float panAngle = pan * (pi * 0.5f);

    target->active = true;
    target->readPos = startSample;
    target->speed = speed;
    target->isReversed = isRev;
    target->durationSamples = durSamples;
    target->ageSamples = 0;
    target->amplitude = 0.8f / std::sqrt(std::max(1.0f, m_densityHz.load(std::memory_order_relaxed) * 0.2f));
    target->panL = std::cos(panAngle);
    target->panR = std::sin(panAngle);
    target->windowShape = shape;
}

void GranularTextureMorphNode::process(ProcessContext& context) noexcept
{
    auto& audio = context.audio;
    if (audio.channels == nullptr || audio.channelCount == 0 || audio.frameCount == 0 || m_bufferLength == 0)
        return;

    const float density = m_densityHz.load(std::memory_order_relaxed);
    const double spawnIntervalSamples = m_sampleRate / std::max(1.0f, density);

    float* outL = audio.channels[0];
    float* outR = (audio.channelCount > 1 && audio.channels[1] != nullptr) ? audio.channels[1] : outL;

    for (std::size_t i = 0; i < audio.frameCount; ++i)
    {
        // Spawn grains according to density interval
        m_spawnCounterSamples += 1.0;
        if (m_spawnCounterSamples >= spawnIntervalSamples)
        {
            m_spawnCounterSamples -= spawnIntervalSamples;
            spawnGrain();
        }

        float accL = 0.0f;
        float accR = 0.0f;

        // Accumulate active grains
        for (auto& g : m_grains)
        {
            if (! g.active)
                continue;

            const float winPos = static_cast<float>(g.ageSamples) / static_cast<float>(g.durationSamples);
            const float env = evaluateWindow(g.windowShape, winPos);

            // Linear interpolation of buffer samples
            const auto iPos = static_cast<std::size_t>(g.readPos);
            const float frac = static_cast<float>(g.readPos - static_cast<double>(iPos));
            const std::size_t nextPos = (iPos + 1 < m_bufferLength) ? iPos + 1 : 0;

            const float sL = m_bufferL[iPos] * (1.0f - frac) + m_bufferL[nextPos] * frac;
            const float sR = m_bufferR[iPos] * (1.0f - frac) + m_bufferR[nextPos] * frac;

            const float grainOutL = sL * env * g.amplitude * g.panL;
            const float grainOutR = sR * env * g.amplitude * g.panR;

            accL += grainOutL;
            accR += grainOutR;

            // Advance grain readhead
            if (g.isReversed)
            {
                g.readPos -= g.speed;
                if (g.readPos < 0.0)
                    g.readPos += static_cast<double>(m_bufferLength);
            }
            else
            {
                g.readPos += g.speed;
                if (g.readPos >= static_cast<double>(m_bufferLength))
                    g.readPos -= static_cast<double>(m_bufferLength);
            }

            ++g.ageSamples;
            if (g.ageSamples >= g.durationSamples)
            {
                g.active = false;
            }
        }

        outL[i] = accL;
        outR[i] = accR;
    }
}

} // namespace dawg::engine::dsp
