#include <dawg/engine/dsp/SpecializedClipWarpEngine.h>

#include <algorithm>
#include <cmath>

namespace dawg::engine::dsp {

namespace {

constexpr float twoPi = 6.28318530717958647692f;

inline float nextRng(std::uint32_t& s) noexcept
{
    s = s * 1664525u + 1013904223u;
    return static_cast<float>(static_cast<std::int32_t>(s)) / 2147483648.0f;
}

} // namespace

SpecializedClipWarpEngine::SpecializedClipWarpEngine() noexcept
{
    m_ringL.fill(0.0f);
    m_ringR.fill(0.0f);
}

bool SpecializedClipWarpEngine::prepare(double sampleRate, std::size_t /*maxBlockSize*/, std::uint16_t /*channelCount*/)
{
    m_sampleRate = (sampleRate > 1000.0) ? sampleRate : 48000.0;
    reset();
    return true;
}

void SpecializedClipWarpEngine::reset() noexcept
{
    m_ringL.fill(0.0f);
    m_ringR.fill(0.0f);
    m_writePos = 0;
    m_readPos = 0.0;
    m_prevEnergy = 0.0f;
    m_inTransient = false;
    m_transientHoldSamples = 0;
    m_grainPhase = 0.0;
}

void SpecializedClipWarpEngine::setWarpMode(const SpecializedWarpMode mode) noexcept
{
    m_warpMode.store(mode, std::memory_order_release);
}

SpecializedWarpMode SpecializedClipWarpEngine::warpMode() const noexcept
{
    return m_warpMode.load(std::memory_order_acquire);
}

void SpecializedClipWarpEngine::setStretchFactor(const float ratio) noexcept
{
    m_stretchFactor.store(std::clamp(ratio, 0.25f, 4.0f), std::memory_order_release);
}

float SpecializedClipWarpEngine::stretchFactor() const noexcept
{
    return m_stretchFactor.load(std::memory_order_acquire);
}

void SpecializedClipWarpEngine::setPitchSemitones(const float semitones) noexcept
{
    m_pitchSemitones.store(std::clamp(semitones, -48.0f, 48.0f), std::memory_order_release);
}

float SpecializedClipWarpEngine::pitchSemitones() const noexcept
{
    return m_pitchSemitones.load(std::memory_order_acquire);
}

void SpecializedClipWarpEngine::setFormantPreservation(const float amount0to1) noexcept
{
    m_formantPreserve.store(std::clamp(amount0to1, 0.0f, 1.0f), std::memory_order_release);
}

float SpecializedClipWarpEngine::formantPreservation() const noexcept
{
    return m_formantPreserve.load(std::memory_order_acquire);
}

void SpecializedClipWarpEngine::setTextureGrainSizeMs(const float grainMs) noexcept
{
    m_grainSizeMs.store(std::clamp(grainMs, 10.0f, 150.0f), std::memory_order_release);
}

float SpecializedClipWarpEngine::textureGrainSizeMs() const noexcept
{
    return m_grainSizeMs.load(std::memory_order_acquire);
}

void SpecializedClipWarpEngine::setTextureFlux(const float flux0to1) noexcept
{
    m_textureFlux.store(std::clamp(flux0to1, 0.0f, 1.0f), std::memory_order_release);
}

float SpecializedClipWarpEngine::textureFlux() const noexcept
{
    return m_textureFlux.load(std::memory_order_acquire);
}

void SpecializedClipWarpEngine::setBeatsTransientSensitivity(const float sens0to1) noexcept
{
    m_beatsSensitivity.store(std::clamp(sens0to1, 0.0f, 1.0f), std::memory_order_release);
}

float SpecializedClipWarpEngine::beatsTransientSensitivity() const noexcept
{
    return m_beatsSensitivity.load(std::memory_order_acquire);
}

void SpecializedClipWarpEngine::process(ProcessContext& context) noexcept
{
    auto& audio = context.audio;
    if (audio.channels == nullptr || audio.channelCount == 0 || audio.frameCount == 0)
        return;

    const auto mode = m_warpMode.load(std::memory_order_relaxed);
    const float stretch = m_stretchFactor.load(std::memory_order_relaxed);
    const float pitchSemi = m_pitchSemitones.load(std::memory_order_relaxed);
    const float grainMs = m_grainSizeMs.load(std::memory_order_relaxed);
    const float flux = m_textureFlux.load(std::memory_order_relaxed);
    const float beatsSens = m_beatsSensitivity.load(std::memory_order_relaxed);

    const double pitchRatio = std::pow(2.0, static_cast<double>(pitchSemi) / 12.0);
    const double readSpeed = pitchRatio / std::max(0.25, static_cast<double>(stretch));

    float* ch0 = audio.channels[0];
    float* ch1 = (audio.channelCount > 1 && audio.channels[1] != nullptr) ? audio.channels[1] : ch0;

    const auto grainSizeSamples = static_cast<std::size_t>(std::max(32.0, m_sampleRate * (grainMs * 0.001)));

    for (std::size_t i = 0; i < audio.frameCount; ++i)
    {
        // 1. Write incoming stream into ring buffer
        m_ringL[m_writePos] = ch0[i];
        m_ringR[m_writePos] = ch1[i];
        m_writePos = (m_writePos + 1) % ringBufferSize;

        // 2. Transient Energy Detector for Beats Mode
        const float inEnergy = ch0[i] * ch0[i] + ch1[i] * ch1[i];
        const float energyDelta = inEnergy - m_prevEnergy;
        m_prevEnergy = inEnergy * 0.8f + m_prevEnergy * 0.2f;

        const float thresh = (1.0f - beatsSens) * 0.1f + 0.005f;
        if (energyDelta > thresh)
        {
            m_inTransient = true;
            m_transientHoldSamples = static_cast<std::size_t>(m_sampleRate * 0.020); // 20ms attack bite
        }

        // 3. Mode Processing
        float outL = 0.0f;
        float outR = 0.0f;

        switch (mode)
        {
        case SpecializedWarpMode::beats:
        {
            // Beats mode: if in transient, track 1:1 for sharp punch
            double step = readSpeed;
            if (m_transientHoldSamples > 0)
            {
                step = 1.0;
                --m_transientHoldSamples;
            }

            auto rIdx = static_cast<std::size_t>(m_readPos) % ringBufferSize;
            outL = m_ringL[rIdx];
            outR = m_ringR[rIdx];

            m_readPos += step;
            if (m_readPos >= static_cast<double>(ringBufferSize))
                m_readPos -= static_cast<double>(ringBufferSize);
            break;
        }

        case SpecializedWarpMode::tones:
        {
            // Tones mode: pitch-synchronous smooth interpolation
            auto rIdx = static_cast<std::size_t>(m_readPos) % ringBufferSize;
            const float frac = static_cast<float>(m_readPos - static_cast<double>(static_cast<std::size_t>(m_readPos)));
            const std::size_t nextIdx = (rIdx + 1) % ringBufferSize;

            outL = m_ringL[rIdx] * (1.0f - frac) + m_ringL[nextIdx] * frac;
            outR = m_ringR[rIdx] * (1.0f - frac) + m_ringR[nextIdx] * frac;

            m_readPos += readSpeed;
            if (m_readPos >= static_cast<double>(ringBufferSize))
                m_readPos -= static_cast<double>(ringBufferSize);
            break;
        }

        case SpecializedWarpMode::texture:
        case SpecializedWarpMode::complexPro:
        {
            // Granular overlap-add with flux randomization
            m_grainPhase += 1.0 / static_cast<double>(grainSizeSamples);
            if (m_grainPhase >= 1.0)
            {
                m_grainPhase -= 1.0;
                // Add flux random jitter
                if (flux > 0.01f)
                {
                    const double jitter = static_cast<double>(nextRng(m_prngState) * flux * static_cast<float>(grainSizeSamples * 0.25));
                    m_readPos += jitter;
                }
            }

            const float win = 0.5f * (1.0f - std::cos(twoPi * static_cast<float>(m_grainPhase)));

            auto rIdx = static_cast<std::size_t>(m_readPos) % ringBufferSize;
            outL = m_ringL[rIdx] * win;
            outR = m_ringR[rIdx] * win;

            m_readPos += readSpeed;
            if (m_readPos >= static_cast<double>(ringBufferSize))
                m_readPos -= static_cast<double>(ringBufferSize);
            break;
        }
        }

        ch0[i] = outL;
        ch1[i] = outR;
    }
}

} // namespace dawg::engine::dsp
