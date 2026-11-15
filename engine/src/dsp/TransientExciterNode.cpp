#include <dawg/engine/dsp/TransientExciterNode.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::engine::dsp {

TransientExciterNode::TransientExciterNode() noexcept
{
    updateGains();
}

bool TransientExciterNode::prepare(
    const double sampleRate,
    [[maybe_unused]] const std::size_t maxBlockSize,
    [[maybe_unused]] const std::uint16_t channelCount)
{
    m_sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    reset();
    return true;
}

void TransientExciterNode::reset() noexcept
{
    for (auto& band : m_bands)
    {
        band.fastEnvL = 0.0f;
        band.fastEnvR = 0.0f;
        band.slowEnvL = 0.0f;
        band.slowEnvR = 0.0f;
        band.activityMeter = 0.0f;
    }

    m_crossoverLow = {};
    m_crossoverHigh = {};

    m_dcBlockInL = 0.0f;
    m_dcBlockOutL = 0.0f;
    m_dcBlockInR = 0.0f;
    m_dcBlockOutR = 0.0f;
}

void TransientExciterNode::updateGains() noexcept
{
    for (auto& band : m_bands)
    {
        band.attackLinear = std::pow(10.0f, band.settings.attackDb * 0.05f);
        band.sustainLinear = std::pow(10.0f, band.settings.sustainDb * 0.05f);
    }
}

void TransientExciterNode::setBandAttackDb(const std::size_t bandIdx, const float attackDb) noexcept
{
    if (bandIdx < numBands)
    {
        m_bands[bandIdx].settings.attackDb = std::clamp(attackDb, -24.0f, 24.0f);
        updateGains();
    }
}

float TransientExciterNode::bandAttackDb(const std::size_t bandIdx) const noexcept
{
    if (bandIdx < numBands)
    {
        return m_bands[bandIdx].settings.attackDb;
    }
    return 0.0f;
}

void TransientExciterNode::setBandSustainDb(const std::size_t bandIdx, const float sustainDb) noexcept
{
    if (bandIdx < numBands)
    {
        m_bands[bandIdx].settings.sustainDb = std::clamp(sustainDb, -24.0f, 24.0f);
        updateGains();
    }
}

float TransientExciterNode::bandSustainDb(const std::size_t bandIdx) const noexcept
{
    if (bandIdx < numBands)
    {
        return m_bands[bandIdx].settings.sustainDb;
    }
    return 0.0f;
}

float TransientExciterNode::bandTransientActivity(const std::size_t bandIdx) const noexcept
{
    if (bandIdx < numBands)
    {
        return m_bands[bandIdx].activityMeter;
    }
    return 0.0f;
}

void TransientExciterNode::process(ProcessContext& context) noexcept
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

    const float dt = 1.0f / static_cast<float>(m_sampleRate);

    // Crossover filter smoothing coefficients
    const float lowCutCoeff = 1.0f - std::exp(-2.0f * std::numbers::pi_v<float> * 180.0f * dt);
    const float highCutCoeff = 1.0f - std::exp(-2.0f * std::numbers::pi_v<float> * 3500.0f * dt);

    // Ballistic envelope time constants: fast attack (1.5ms), slow sustain (55ms)
    const float fastCoeff = 1.0f - std::exp(-dt / 0.0015f);
    const float slowCoeff = 1.0f - std::exp(-dt / 0.055f);

    const float inflatorC = m_inflatorCurve * 0.01f;
    const float inflatorAmt = m_inflatorAmount;
    const float exciterDrive = 1.0f + m_exciterDrive * 2.5f;
    const float exciterMix = m_exciterMix;

    for (std::size_t i = 0; i < frameCount; ++i)
    {
        const float inL = ch0[i];
        const float inR = (ch1 != nullptr) ? ch1[i] : inL;

        // 1. 3-Way Crossover Split (Low < 180 Hz, Mid 180..3500 Hz, High > 3500 Hz)
        m_crossoverLow.lpL += lowCutCoeff * (inL - m_crossoverLow.lpL);
        m_crossoverLow.lpR += lowCutCoeff * (inR - m_crossoverLow.lpR);
        const float bandLowL = m_crossoverLow.lpL;
        const float bandLowR = m_crossoverLow.lpR;

        const float midHighL = inL - bandLowL;
        const float midHighR = inR - bandLowR;

        m_crossoverHigh.lpL += highCutCoeff * (midHighL - m_crossoverHigh.lpL);
        m_crossoverHigh.lpR += highCutCoeff * (midHighR - m_crossoverHigh.lpR);
        const float bandMidL = m_crossoverHigh.lpL;
        const float bandMidR = m_crossoverHigh.lpR;

        const float bandHighL = midHighL - bandMidL;
        const float bandHighR = midHighR - bandMidR;

        const std::array<float, numBands> bandsL = {bandLowL, bandMidL, bandHighL};
        const std::array<float, numBands> bandsR = {bandLowR, bandMidR, bandHighR};

        float shapedL = 0.0f;
        float shapedR = 0.0f;

        // 2. Differential Ballistic Transient Shaping per Band
        for (std::size_t b = 0; b < numBands; ++b)
        {
            auto& state = m_bands[b];
            const float sL = bandsL[b];
            const float sR = bandsR[b];

            // Fast vs Slow envelope tracking
            const float absL = std::abs(sL);
            const float absR = std::abs(sR);

            state.fastEnvL += fastCoeff * (absL - state.fastEnvL);
            state.fastEnvR += fastCoeff * (absR - state.fastEnvR);
            state.slowEnvL += slowCoeff * (absL - state.slowEnvL);
            state.slowEnvR += slowCoeff * (absR - state.slowEnvR);

            // Transient delta
            const float deltaL = std::max(0.0f, state.fastEnvL - state.slowEnvL);
            const float deltaR = std::max(0.0f, state.fastEnvR - state.slowEnvR);

            const float weightTransL = deltaL / (state.fastEnvL + 1e-4f);
            const float weightTransR = deltaR / (state.fastEnvR + 1e-4f);
            const float weightSustL = 1.0f - weightTransL;
            const float weightSustR = 1.0f - weightTransR;

            const float gainL = 1.0f + (state.attackLinear - 1.0f) * weightTransL + (state.sustainLinear - 1.0f) * weightSustL;
            const float gainR = 1.0f + (state.attackLinear - 1.0f) * weightTransR + (state.sustainLinear - 1.0f) * weightSustR;

            shapedL += sL * gainL;
            shapedR += sR * gainR;

            state.activityMeter = std::max(state.activityMeter * 0.999f, (weightTransL + weightTransR) * 0.5f);
        }

        // 3. Dynamic Harmonic Exciter
        if (exciterMix > 0.001f)
        {
            const float exInL = shapedL * exciterDrive;
            const float exInR = shapedR * exciterDrive;
            float hL = 0.0f;
            float hR = 0.0f;

            switch (m_exciterMode)
            {
            case ExciterHarmonicMode::warmTube:
                hL = 0.35f * exInL * std::abs(exInL) + 0.08f * exInL * exInL * exInL;
                hR = 0.35f * exInR * std::abs(exInR) + 0.08f * exInR * exInR * exInR;
                break;
            case ExciterHarmonicMode::tapeSaturation:
                hL = 0.45f * (exInL - 0.333f * exInL * exInL * exInL);
                hR = 0.45f * (exInR - 0.333f * exInR * exInR * exInR);
                break;
            case ExciterHarmonicMode::modernExciter:
                hL = 0.20f * (exInL * exInL) + 0.35f * (exInL * exInL * exInL);
                hR = 0.20f * (exInR * exInR) + 0.35f * (exInR * exInR * exInR);
                break;
            }

            // DC blocking filter (pole at 0.995)
            const float dcOutL = hL - m_dcBlockInL + 0.995f * m_dcBlockOutL;
            m_dcBlockInL = hL;
            m_dcBlockOutL = dcOutL;

            const float dcOutR = hR - m_dcBlockInR + 0.995f * m_dcBlockOutR;
            m_dcBlockInR = hR;
            m_dcBlockOutR = dcOutR;

            shapedL = (1.0f - exciterMix) * shapedL + exciterMix * (shapedL + dcOutL * 0.5f);
            shapedR = (1.0f - exciterMix) * shapedR + exciterMix * (shapedR + dcOutR * 0.5f);
        }

        // 4. Oxford Inflator-Style Tube Headroom Expander
        if (inflatorAmt > 0.001f)
        {
            const auto inflate = [inflatorC](const float x) noexcept -> float {
                const float sgn = (x >= 0.0f) ? 1.0f : -1.0f;
                const float absX = std::min(1.0f, std::abs(x));
                const float y = sgn * (((1.0f + inflatorC) * absX - inflatorC * absX * absX) / (1.0f + inflatorC * absX * 0.5f));
                return std::clamp(y, -1.0f, 1.0f);
            };

            const float infL = inflate(shapedL);
            const float infR = inflate(shapedR);

            shapedL = (1.0f - inflatorAmt) * shapedL + inflatorAmt * infL;
            shapedR = (1.0f - inflatorAmt) * shapedR + inflatorAmt * infR;
        }

        // Soft tube ceiling saturation to preserve headroom without inter-sample clipping
        shapedL = std::tanh(shapedL);
        shapedR = std::tanh(shapedR);

        ch0[i] = shapedL;
        if (context.audio.channelCount >= 2)
        {
            ch1[i] = shapedR;
        }
    }
}

} // namespace dawg::engine::dsp
