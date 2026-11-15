#include <dawg/engine/dsp/SpectralSidechainNode.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::engine::dsp {

SpectralSidechainNode::SpectralSidechainNode() noexcept
{
    setupBandFrequencies();
    setAttackMs(5.0f);
    setReleaseMs(60.0f);
}

void SpectralSidechainNode::setupBandFrequencies() noexcept
{
    // 32 logarithmically spaced frequency bands from 25 Hz to 18 kHz
    constexpr double minF = 25.0;
    constexpr double maxF = 18000.0;
    const double ratio = std::pow(maxF / minF, 1.0 / static_cast<double>(numBands - 1));

    double f = minF;
    for (std::size_t b = 0; b < numBands; ++b)
    {
        m_bandsL[b].centerHz = static_cast<float>(f);
        m_bandsR[b].centerHz = static_cast<float>(f);
        f *= ratio;
    }
}

void SpectralSidechainNode::setAttackMs(const float ms) noexcept
{
    m_attackMs = std::clamp(ms, 0.1f, 200.0f);
    if (m_sampleRate > 0.0)
    {
        const float timeSec = m_attackMs * 0.001f;
        m_attackCoeff = 1.0f - std::exp(-1.0f / (timeSec * static_cast<float>(m_sampleRate)));
    }
}

void SpectralSidechainNode::setReleaseMs(const float ms) noexcept
{
    m_releaseMs = std::clamp(ms, 1.0f, 1000.0f);
    if (m_sampleRate > 0.0)
    {
        const float timeSec = m_releaseMs * 0.001f;
        m_releaseCoeff = 1.0f - std::exp(-1.0f / (timeSec * static_cast<float>(m_sampleRate)));
    }
}

bool SpectralSidechainNode::prepare(
    const double sampleRate,
    const std::size_t maxBlockSize,
    const std::uint16_t channelCount)
{
    if (sampleRate <= 0.0 || maxBlockSize == 0 || channelCount == 0)
    {
        return false;
    }

    m_sampleRate = sampleRate;
    setupBandFrequencies();
    setAttackMs(m_attackMs);
    setReleaseMs(m_releaseMs);
    reset();
    return true;
}

void SpectralSidechainNode::reset() noexcept
{
    for (std::size_t b = 0; b < numBands; ++b)
    {
        m_bandsL[b].s1 = 0.0f;
        m_bandsL[b].s2 = 0.0f;
        m_bandsL[b].sidechainS1 = 0.0f;
        m_bandsL[b].sidechainS2 = 0.0f;
        m_bandsL[b].sidechainEnv = 0.0f;
        m_bandsL[b].gainReduction = 1.0f;

        m_bandsR[b].s1 = 0.0f;
        m_bandsR[b].s2 = 0.0f;
        m_bandsR[b].sidechainS1 = 0.0f;
        m_bandsR[b].sidechainS2 = 0.0f;
        m_bandsR[b].sidechainEnv = 0.0f;
        m_bandsR[b].gainReduction = 1.0f;
    }

    m_collisionRadar.fill(0.0f);
    m_sidechainEnergies.fill(0.0f);
}

void SpectralSidechainNode::getCollisionRadar(float* destination, const std::size_t count) const noexcept
{
    if (destination == nullptr || count == 0)
    {
        return;
    }

    const auto copyCount = std::min(count, numBands);
    for (std::size_t i = 0; i < copyCount; ++i)
    {
        destination[i] = m_collisionRadar[i];
    }
}

void SpectralSidechainNode::getSidechainEnergies(float* destination, const std::size_t count) const noexcept
{
    if (destination == nullptr || count == 0)
    {
        return;
    }

    const auto copyCount = std::min(count, numBands);
    for (std::size_t i = 0; i < copyCount; ++i)
    {
        destination[i] = m_sidechainEnergies[i];
    }
}

void SpectralSidechainNode::process(ProcessContext& context) noexcept
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

    const float* sc0 = nullptr;
    const float* sc1 = nullptr;
    if (context.sidechain.channels != nullptr && context.sidechain.channelCount > 0)
    {
        sc0 = context.sidechain.channels[0];
        sc1 = (context.sidechain.channelCount >= 2) ? context.sidechain.channels[1] : sc0;
    }

    const float dt = 1.0f / static_cast<float>(m_sampleRate);
    constexpr float q = 2.5f; // Moderate Q for smooth 32-band coverage
    const float k = 1.0f / q;

    // Zero collision telemetry accumulators
    std::array<float, numBands> blockCollision {};
    std::array<float, numBands> blockScEnergy {};

    for (std::size_t i = 0; i < frameCount; ++i)
    {
        const float inL = ch0[i];
        const float inR = (ch1 != nullptr) ? ch1[i] : inL;

        const float sideL = (sc0 != nullptr) ? sc0[i] : inL;
        const float sideR = (sc1 != nullptr) ? sc1[i] : inR;

        float outSumL = 0.0f;
        float outSumR = 0.0f;

        for (std::size_t b = 0; b < numBands; ++b)
        {
            auto& bandL = m_bandsL[b];
            auto& bandR = m_bandsR[b];

            const float g = std::tan(std::numbers::pi_v<float> * std::clamp(bandL.centerHz * dt, 0.001f, 0.45f));
            const float denom = 1.0f / (1.0f + g * (g + k));

            // Main SVF bandpass L
            const float v1L = (inL - bandL.s1 * (g + k) - bandL.s2) * denom;
            const float v0L = g * v1L;
            const float bpL = g * (v0L + bandL.s1) + bandL.s2;
            bandL.s1 += 2.0f * v0L;
            bandL.s2 += 2.0f * g * bpL;

            // Main SVF bandpass R
            const float v1R = (inR - bandR.s1 * (g + k) - bandR.s2) * denom;
            const float v0R = g * v1R;
            const float bpR = g * (v0R + bandR.s1) + bandR.s2;
            bandR.s1 += 2.0f * v0R;
            bandR.s2 += 2.0f * g * bpR;

            // Sidechain SVF bandpass L & R
            const float scV1L = (sideL - bandL.sidechainS1 * (g + k) - bandL.sidechainS2) * denom;
            const float scV0L = g * scV1L;
            const float scBpL = g * (scV0L + bandL.sidechainS1) + bandL.sidechainS2;
            bandL.sidechainS1 += 2.0f * scV0L;
            bandL.sidechainS2 += 2.0f * g * scBpL;

            const float scV1R = (sideR - bandR.sidechainS1 * (g + k) - bandR.sidechainS2) * denom;
            const float scV0R = g * scV1R;
            const float scBpR = g * (scV0R + bandR.sidechainS1) + bandR.sidechainS2;
            bandR.sidechainS1 += 2.0f * scV0R;
            bandR.sidechainS2 += 2.0f * g * scBpR;

            // Envelope detection on sidechain band
            const float scMag = 0.5f * (std::abs(scBpL) + std::abs(scBpR));
            const float coeffL = (scMag > bandL.sidechainEnv) ? m_attackCoeff : m_releaseCoeff;
            bandL.sidechainEnv += coeffL * (scMag - bandL.sidechainEnv);

            // Compute dynamic inverse ducking attenuation for this frequency band
            float duckGain = 1.0f;
            if (bandL.centerHz >= m_lowCutHz && bandL.centerHz <= m_highCutHz)
            {
                const float duckAmount = std::clamp(bandL.sidechainEnv * 3.5f * m_duckingAmount, 0.0f, 0.95f);
                duckGain = 1.0f - duckAmount;
            }

            bandL.gainReduction = duckGain;
            bandR.gainReduction = duckGain;

            outSumL += bpL * duckGain;
            outSumR += bpR * duckGain;

            // Track acoustic masking collision
            const float mainMag = 0.5f * (std::abs(bpL) + std::abs(bpR));
            const float collisionFactor = mainMag * scMag * 4.0f;
            blockCollision[b] = std::max(blockCollision[b], collisionFactor);
            blockScEnergy[b] = std::max(blockScEnergy[b], scMag);
        }

        ch0[i] = outSumL;
        if (ch1 != nullptr)
        {
            ch1[i] = outSumR;
        }
    }

    // Smooth collision radar telemetry
    for (std::size_t b = 0; b < numBands; ++b)
    {
        m_collisionRadar[b] = 0.8f * m_collisionRadar[b] + 0.2f * std::clamp(blockCollision[b], 0.0f, 1.0f);
        m_sidechainEnergies[b] = 0.8f * m_sidechainEnergies[b] + 0.2f * std::clamp(blockScEnergy[b], 0.0f, 1.0f);
    }
}

} // namespace dawg::engine::dsp
