#include <dawg/engine/dsp/BinauralSpatialPannerNode.h>

namespace dawg::engine::dsp {

bool BinauralSpatialPannerNode::prepare(
    const double sampleRate,
    const std::size_t maxBlockSize,
    const std::uint16_t channelCount)
{
    if (sampleRate <= 0.0 || maxBlockSize == 0 || channelCount == 0)
    {
        return false;
    }

    m_sampleRate = sampleRate;

    // Allocate 128 samples (~2.6ms) for fractional delay lines
    m_itdDelayLeft.prepare(128);
    m_itdDelayRight.prepare(128);
    m_headShadowLeft.reset();
    m_headShadowRight.reset();
    m_airAbsorption.reset();
    return true;
}

void BinauralSpatialPannerNode::reset() noexcept
{
    m_itdDelayLeft.reset();
    m_itdDelayRight.reset();
    m_headShadowLeft.reset();
    m_headShadowRight.reset();
    m_airAbsorption.reset();
}

void BinauralSpatialPannerNode::process(ProcessContext& context) noexcept
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

    constexpr float speedOfSound = 343.0f; // m/s
    const float azRad = m_azimuthDeg * static_cast<float>(std::numbers::pi) / 180.0f;
    const float sinAz = std::sin(azRad);
    const float absSinAz = std::abs(sinAz);

    // 1. Woodworth's Spherical Head ITD (Interaural Time Difference)
    // Delay in seconds = (headRadius / c) * (sin|az| + |az|)
    const float maxItdSec = (m_headRadius / speedOfSound) * (absSinAz + std::abs(azRad));
    const float maxItdSamples = maxItdSec * static_cast<float>(m_sampleRate);

    float delayLeftSamples = 0.0f;
    float delayRightSamples = 0.0f;
    if (sinAz > 0.0f)
    {
        // Sound is to the right -> Left ear is far ear (delayed)
        delayLeftSamples = maxItdSamples;
    }
    else
    {
        // Sound is to the left -> Right ear is far ear (delayed)
        delayRightSamples = maxItdSamples;
    }

    // 2. ILD (Interaural Level Difference) & Head Shadow
    // Far ear receives lowpass filtering and level reduction
    float ildGainLeft = 1.0f;
    float ildGainRight = 1.0f;
    if (sinAz > 0.0f)
    {
        ildGainLeft = 1.0f - 0.5f * absSinAz;
    }
    else
    {
        ildGainRight = 1.0f - 0.5f * absSinAz;
    }

    // Shadow cutoff frequencies
    const float shadowCutoffLeft = (sinAz > 0.0f) ? (2000.0f + (1.0f - absSinAz) * 16000.0f) : 20000.0f;
    const float shadowCutoffRight = (sinAz < 0.0f) ? (2000.0f + (1.0f - absSinAz) * 16000.0f) : 20000.0f;

    const float alphaShadowL = std::clamp(static_cast<float>(2.0 * std::numbers::pi) * shadowCutoffLeft / static_cast<float>(m_sampleRate), 0.01f, 1.0f);
    const float alphaShadowR = std::clamp(static_cast<float>(2.0 * std::numbers::pi) * shadowCutoffRight / static_cast<float>(m_sampleRate), 0.01f, 1.0f);

    // 3. Distance Attenuation (Inverse Square Law) & High-Frequency Air Absorption
    const float distGain = std::clamp(1.0f / m_distanceMeters, 0.05f, 2.0f);
    const float airCutoff = std::clamp(20000.0f - (m_distanceMeters - 1.0f) * 800.0f, 2500.0f, 20000.0f);
    const float alphaAir = std::clamp(static_cast<float>(2.0 * std::numbers::pi) * airCutoff / static_cast<float>(m_sampleRate), 0.01f, 1.0f);

    // 4. Elevation Cue: Pinna Spectral Notch
    // High-shelf or notch variation based on elevation
    const float elevGain = 1.0f + 0.2f * std::sin(m_elevationDeg * static_cast<float>(std::numbers::pi) / 180.0f);

    for (std::size_t i = 0; i < frameCount; ++i)
    {
        // Mono sum source if stereo input
        const float inSample = 0.5f * (ch0[i] + (ch1 != nullptr ? ch1[i] : ch0[i]));

        // Air absorption filter
        const float airFiltered = m_airAbsorption.processLp(inSample, alphaAir);

        // Push into delay lines
        m_itdDelayLeft.push(airFiltered);
        m_itdDelayRight.push(airFiltered);

        // Read fractional ITD
        const float sampleL = m_itdDelayLeft.read(delayLeftSamples);
        const float sampleR = m_itdDelayRight.read(delayRightSamples);

        // Apply Head-shadowing ILD and distance gain
        const float shadowedL = m_headShadowLeft.processLp(sampleL, alphaShadowL) * ildGainLeft * distGain * elevGain;
        const float shadowedR = m_headShadowRight.processLp(sampleR, alphaShadowR) * ildGainRight * distGain * elevGain;

        ch0[i] = shadowedL;
        if (ch1 != nullptr && ch1 != ch0)
        {
            ch1[i] = shadowedR;
        }
    }
}

} // namespace dawg::engine::dsp
