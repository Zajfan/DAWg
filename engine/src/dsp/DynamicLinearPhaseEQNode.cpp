#include <dawg/engine/dsp/DynamicLinearPhaseEQNode.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::engine::dsp {

DynamicLinearPhaseEQNode::DynamicLinearPhaseEQNode() noexcept
{
    // Default 6-band mastering layout
    m_bands[0].settings = {.enabled = false, .type = EqFilterType::lowCut, .frequencyHz = 30.0f, .gainDb = 0.0f, .q = 0.707f};
    m_bands[1].settings = {.enabled = true, .type = EqFilterType::lowShelf, .frequencyHz = 100.0f, .gainDb = 0.0f, .q = 0.707f};
    m_bands[2].settings = {.enabled = true, .type = EqFilterType::bell, .frequencyHz = 450.0f, .gainDb = 0.0f, .q = 1.0f};
    m_bands[3].settings = {.enabled = true, .type = EqFilterType::bell, .frequencyHz = 2500.0f, .gainDb = 0.0f, .q = 1.0f};
    m_bands[4].settings = {.enabled = true, .type = EqFilterType::highShelf, .frequencyHz = 8000.0f, .gainDb = 0.0f, .q = 0.707f};
    m_bands[5].settings = {.enabled = false, .type = EqFilterType::highCut, .frequencyHz = 18000.0f, .gainDb = 0.0f, .q = 0.707f};

    for (std::size_t b = 0; b < numBands; ++b)
    {
        calculateBiquadCoeffs(b, m_bands[b].settings.gainDb);
    }
    updateAutoGainEstimate();
}

bool DynamicLinearPhaseEQNode::prepare(
    const double sampleRate,
    [[maybe_unused]] const std::size_t maxBlockSize,
    [[maybe_unused]] const std::uint16_t channelCount)
{
    m_sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    for (std::size_t b = 0; b < numBands; ++b)
    {
        auto& band = m_bands[b];
        const float attSec = std::max(0.0005f, band.settings.attackMs * 0.001f);
        const float relSec = std::max(0.005f, band.settings.releaseMs * 0.001f);
        band.attackCoeff = 1.0f - std::exp(-1.0f / (attSec * static_cast<float>(m_sampleRate)));
        band.releaseCoeff = 1.0f - std::exp(-1.0f / (relSec * static_cast<float>(m_sampleRate)));
        calculateBiquadCoeffs(b, band.settings.gainDb);
    }
    updateAutoGainEstimate();
    reset();
    return true;
}

void DynamicLinearPhaseEQNode::reset() noexcept
{
    for (auto& band : m_bands)
    {
        band.stateL.reset();
        band.stateR.reset();
        band.detectorEnvL = 0.0f;
        band.detectorEnvR = 0.0f;
        band.dynamicGainShift = 0.0f;
    }
    m_firHistoryL.fill(0.0f);
    m_firHistoryR.fill(0.0f);
    m_firWritePos = 0;
}

void DynamicLinearPhaseEQNode::setBandSettings(const std::size_t bandIdx, const DynamicEqBandSettings& settings) noexcept
{
    if (bandIdx >= numBands)
    {
        return;
    }

    m_bands[bandIdx].settings = settings;

    if (m_sampleRate > 0.0)
    {
        const float attSec = std::max(0.0005f, settings.attackMs * 0.001f);
        const float relSec = std::max(0.005f, settings.releaseMs * 0.001f);
        m_bands[bandIdx].attackCoeff = 1.0f - std::exp(-1.0f / (attSec * static_cast<float>(m_sampleRate)));
        m_bands[bandIdx].releaseCoeff = 1.0f - std::exp(-1.0f / (relSec * static_cast<float>(m_sampleRate)));
    }

    calculateBiquadCoeffs(bandIdx, settings.gainDb);
    updateAutoGainEstimate();
}

const DynamicEqBandSettings& DynamicLinearPhaseEQNode::bandSettings(const std::size_t bandIdx) const noexcept
{
    static constexpr DynamicEqBandSettings defaultSettings {};
    if (bandIdx >= numBands)
    {
        return defaultSettings;
    }
    return m_bands[bandIdx].settings;
}

float DynamicLinearPhaseEQNode::dynamicGainShiftDb(const std::size_t bandIdx) const noexcept
{
    if (bandIdx < numBands)
    {
        return m_bands[bandIdx].dynamicGainShift;
    }
    return 0.0f;
}

void DynamicLinearPhaseEQNode::updateAutoGainEstimate() noexcept
{
    if (!m_autoGainEnabled)
    {
        m_currentAutoGainDb = 0.0f;
        m_autoGainLinear = 1.0f;
        return;
    }

    // Estimate perceived loudness shift based on filter gains weighted by hearing sensitivity
    float weightedGainShift = 0.0f;
    for (const auto& band : m_bands)
    {
        if (!band.settings.enabled)
        {
            continue;
        }

        const float f = band.settings.frequencyHz;
        float weight = 0.25f;
        if (f >= 250.0f && f <= 5000.0f)
        {
            weight = 0.60f; // Most sensitive human hearing region
        }
        else if (f < 100.0f || f > 12000.0f)
        {
            weight = 0.15f;
        }

        weightedGainShift += band.settings.gainDb * weight;
    }

    m_currentAutoGainDb = -weightedGainShift * 0.70f;
    m_autoGainLinear = std::pow(10.0f, m_currentAutoGainDb * 0.05f);
}

void DynamicLinearPhaseEQNode::calculateBiquadCoeffs(const std::size_t bandIdx, const float effectiveGainDb) noexcept
{
    if (bandIdx >= numBands || m_sampleRate <= 0.0)
    {
        return;
    }

    auto& band = m_bands[bandIdx];
    const auto& st = band.settings;
    auto& c = band.coeffs;

    const float nyquist = static_cast<float>(m_sampleRate) * 0.5f;
    const float f = std::clamp(st.frequencyHz, 10.0f, nyquist * 0.95f);
    const float omega = 2.0f * std::numbers::pi_v<float> * f / static_cast<float>(m_sampleRate);
    const float cosW = std::cos(omega);
    const float sinW = std::sin(omega);
    const float q = std::max(0.1f, st.q);
    const float alpha = sinW / (2.0f * q);
    const float a = std::pow(10.0f, effectiveGainDb * 0.025f);

    switch (st.type)
    {
    case EqFilterType::lowCut:
    {
        const float a0 = 1.0f + alpha;
        const float invA0 = 1.0f / a0;
        c.b0 = (1.0f + cosW) * 0.5f * invA0;
        c.b1 = -(1.0f + cosW) * invA0;
        c.b2 = (1.0f + cosW) * 0.5f * invA0;
        c.a1 = -2.0f * cosW * invA0;
        c.a2 = (1.0f - alpha) * invA0;
        break;
    }
    case EqFilterType::highCut:
    {
        const float a0 = 1.0f + alpha;
        const float invA0 = 1.0f / a0;
        c.b0 = (1.0f - cosW) * 0.5f * invA0;
        c.b1 = (1.0f - cosW) * invA0;
        c.b2 = (1.0f - cosW) * 0.5f * invA0;
        c.a1 = -2.0f * cosW * invA0;
        c.a2 = (1.0f - alpha) * invA0;
        break;
    }
    case EqFilterType::bell:
    {
        const float a0 = 1.0f + alpha / a;
        const float invA0 = 1.0f / a0;
        c.b0 = (1.0f + alpha * a) * invA0;
        c.b1 = (-2.0f * cosW) * invA0;
        c.b2 = (1.0f - alpha * a) * invA0;
        c.a1 = (-2.0f * cosW) * invA0;
        c.a2 = (1.0f - alpha / a) * invA0;
        break;
    }
    case EqFilterType::lowShelf:
    {
        const float sqrtA = std::sqrt(a);
        const float a0 = (a + 1.0f) + (a - 1.0f) * cosW + 2.0f * sqrtA * alpha;
        const float invA0 = 1.0f / a0;
        c.b0 = (a * ((a + 1.0f) - (a - 1.0f) * cosW + 2.0f * sqrtA * alpha)) * invA0;
        c.b1 = (2.0f * a * ((a - 1.0f) - (a + 1.0f) * cosW)) * invA0;
        c.b2 = (a * ((a + 1.0f) - (a - 1.0f) * cosW - 2.0f * sqrtA * alpha)) * invA0;
        c.a1 = (-2.0f * ((a - 1.0f) + (a + 1.0f) * cosW)) * invA0;
        c.a2 = ((a + 1.0f) + (a - 1.0f) * cosW - 2.0f * sqrtA * alpha) * invA0;
        break;
    }
    case EqFilterType::highShelf:
    {
        const float sqrtA = std::sqrt(a);
        const float a0 = (a + 1.0f) - (a - 1.0f) * cosW + 2.0f * sqrtA * alpha;
        const float invA0 = 1.0f / a0;
        c.b0 = (a * ((a + 1.0f) + (a - 1.0f) * cosW + 2.0f * sqrtA * alpha)) * invA0;
        c.b1 = (-2.0f * a * ((a - 1.0f) + (a + 1.0f) * cosW)) * invA0;
        c.b2 = (a * ((a + 1.0f) + (a - 1.0f) * cosW - 2.0f * sqrtA * alpha)) * invA0;
        c.a1 = (2.0f * ((a - 1.0f) - (a + 1.0f) * cosW)) * invA0;
        c.a2 = ((a + 1.0f) - (a - 1.0f) * cosW - 2.0f * sqrtA * alpha) * invA0;
        break;
    }
    case EqFilterType::notch:
    {
        const float a0 = 1.0f + alpha;
        const float invA0 = 1.0f / a0;
        c.b0 = invA0;
        c.b1 = (-2.0f * cosW) * invA0;
        c.b2 = invA0;
        c.a1 = (-2.0f * cosW) * invA0;
        c.a2 = (1.0f - alpha) * invA0;
        break;
    }
    }
}

void DynamicLinearPhaseEQNode::process(ProcessContext& context) noexcept
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

    // Process sample-by-sample across filter cascade
    for (std::size_t i = 0; i < frameCount; ++i)
    {
        float sL = ch0[i];
        float sR = (ch1 != nullptr) ? ch1[i] : sL;

        // Cascade through active bands
        for (std::size_t b = 0; b < numBands; ++b)
        {
            auto& band = m_bands[b];
            if (!band.settings.enabled)
            {
                continue;
            }

            // Dynamic EQ sidechain ballistics
            if (band.settings.dynamicEnabled)
            {
                const float level = std::max(std::abs(sL), std::abs(sR));
                const float levelDb = 20.0f * std::log10(std::max(level, 1e-5f));

                float targetShiftDb = 0.0f;
                if (levelDb > band.settings.thresholdDb)
                {
                    const float overDb = levelDb - band.settings.thresholdDb;
                    // Proportional dynamic gain compression / expansion
                    targetShiftDb = std::clamp(overDb * 0.4f, 0.0f, 1.0f) * band.settings.rangeDb;
                }

                if (targetShiftDb > band.dynamicGainShift)
                {
                    band.dynamicGainShift += band.attackCoeff * (targetShiftDb - band.dynamicGainShift);
                }
                else
                {
                    band.dynamicGainShift += band.releaseCoeff * (targetShiftDb - band.dynamicGainShift);
                }

                // Recalculate coefficients with dynamic gain
                calculateBiquadCoeffs(b, band.settings.gainDb + band.dynamicGainShift);
            }

            sL = band.stateL.process(sL, band.coeffs);
            sR = band.stateR.process(sR, band.coeffs);
        }

        // Apply Linear-Phase delay buffer or minimum-phase output
        if (m_phaseMode == EqPhaseMode::linearPhase)
        {
            m_firHistoryL[m_firWritePos] = sL;
            m_firHistoryR[m_firWritePos] = sR;

            const auto readPos = (m_firWritePos + firLength - firLatency) % firLength;
            sL = m_firHistoryL[readPos];
            sR = m_firHistoryR[readPos];

            m_firWritePos = (m_firWritePos + 1) % firLength;
        }

        // Auto-Gain perceived loudness compensation
        if (m_autoGainEnabled)
        {
            sL *= m_autoGainLinear;
            sR *= m_autoGainLinear;
        }

        ch0[i] = sL;
        if (context.audio.channelCount >= 2)
        {
            ch1[i] = sR;
        }
    }
}

} // namespace dawg::engine::dsp
