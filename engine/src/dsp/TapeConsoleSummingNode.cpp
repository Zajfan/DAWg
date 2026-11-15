#include <dawg/engine/dsp/TapeConsoleSummingNode.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::engine::dsp {

TapeConsoleSummingNode::TapeConsoleSummingNode() noexcept
{
    updateGains();
    updateFilters();
}

bool TapeConsoleSummingNode::prepare(
    const double sampleRate,
    [[maybe_unused]] const std::size_t maxBlockSize,
    [[maybe_unused]] const std::uint16_t channelCount)
{
    m_sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    updateFilters();
    reset();
    return true;
}

void TapeConsoleSummingNode::reset() noexcept
{
    m_headBumpL.reset();
    m_headBumpR.reset();
    m_hfFilterL = 0.0f;
    m_hfFilterR = 0.0f;
    m_delayBufferL.fill(0.0f);
    m_delayBufferR.fill(0.0f);
    m_delayWritePos = 0;
    m_wowPhase = 0.0f;
    m_flutterPhase = 0.0f;
}

void TapeConsoleSummingNode::updateGains() noexcept
{
    m_inputLinear = std::pow(10.0f, (m_drive * 16.0f) * 0.05f);
    m_crosstalkLinear = std::pow(10.0f, m_crosstalkDb * 0.05f);
}

void TapeConsoleSummingNode::updateFilters() noexcept
{
    if (m_sampleRate <= 0.0)
    {
        return;
    }

    // 15 IPS has head bump at ~50 Hz; 30 IPS has head bump at ~82 Hz
    const float centerHz = (m_tapeSpeed == TapeSpeed::ips15) ? 50.0f : 82.0f;
    const float q = (m_tapeSpeed == TapeSpeed::ips15) ? 1.2f : 1.4f;
    const float gainDb = m_headBumpGainDb;

    const float omega = 2.0f * std::numbers::pi_v<float> * centerHz / static_cast<float>(m_sampleRate);
    const float cosW = std::cos(omega);
    const float sinW = std::sin(omega);
    const float alpha = sinW / (2.0f * q);
    const float a = std::pow(10.0f, gainDb * 0.025f);

    const float a0 = 1.0f + alpha / a;
    const float invA0 = 1.0f / a0;

    m_headBumpB0 = (1.0f + alpha * a) * invA0;
    m_headBumpB1 = (-2.0f * cosW) * invA0;
    m_headBumpB2 = (1.0f - alpha * a) * invA0;
    m_headBumpA1 = (-2.0f * cosW) * invA0;
    m_headBumpA2 = (1.0f - alpha / a) * invA0;
}

void TapeConsoleSummingNode::process(ProcessContext& context) noexcept
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
    // HF de-emphasis cutoff: ~16.5 kHz for 15 IPS, ~23 kHz for 30 IPS
    const float hfCutoff = (m_tapeSpeed == TapeSpeed::ips15) ? 16500.0f : 23000.0f;
    const float hfCoeff = 1.0f - std::exp(-2.0f * std::numbers::pi_v<float> * hfCutoff * dt);

    const float wowInc = 2.0f * std::numbers::pi_v<float> * 0.75f * dt;
    const float flutterInc = 2.0f * std::numbers::pi_v<float> * 8.5f * dt;

    for (std::size_t i = 0; i < frameCount; ++i)
    {
        float inL = ch0[i] * m_inputLinear;
        float inR = (context.audio.channelCount >= 2) ? (ch1[i] * m_inputLinear) : inL;

        // 1. Console Input Transformer Saturation (monotonic core saturation)
        if (m_transformerSat > 0.001f)
        {
            const float satStrength = m_transformerSat * 0.35f;
            inL = inL / (1.0f + satStrength * std::abs(inL));
            inR = inR / (1.0f + satStrength * std::abs(inR));
        }

        // 2. Tape Head Bump Resonance
        inL = m_headBumpL.process(inL, m_headBumpB0, m_headBumpB1, m_headBumpB2, m_headBumpA1, m_headBumpA2);
        inR = m_headBumpR.process(inR, m_headBumpB0, m_headBumpB1, m_headBumpB2, m_headBumpA1, m_headBumpA2);

        // 3. Magnetic Tape Hysteresis & Flux Saturation
        const float biasOffset = m_bias * 0.15f;
        const float satL = std::tanh(inL + biasOffset) - biasOffset * 0.5f;
        const float satR = std::tanh(inR + biasOffset) - biasOffset * 0.5f;

        // 4. High-Frequency Tape Roll-Off
        m_hfFilterL += hfCoeff * (satL - m_hfFilterL);
        m_hfFilterR += hfCoeff * (satR - m_hfFilterR);

        // 5. Tape Transport Wow, Flutter & Micro-Speed Pitch Drift
        m_delayBufferL[m_delayWritePos] = m_hfFilterL;
        m_delayBufferR[m_delayWritePos] = m_hfFilterR;

        const float wowMod = m_wow * 3.5f * std::sin(m_wowPhase);
        const float flutterMod = m_flutter * 1.5f * std::sin(m_flutterPhase);
        const float totalDelay = 12.0f + wowMod + flutterMod;

        const auto baseDelay = static_cast<std::size_t>(std::floor(totalDelay));
        const float frac = totalDelay - static_cast<float>(baseDelay);

        const auto read0 = (m_delayWritePos + delayBufferSize - baseDelay) % delayBufferSize;
        const auto read1 = (m_delayWritePos + delayBufferSize - baseDelay - 1) % delayBufferSize;

        const float readL = m_delayBufferL[read0] * (1.0f - frac) + m_delayBufferL[read1] * frac;
        const float readR = m_delayBufferR[read0] * (1.0f - frac) + m_delayBufferR[read1] * frac;

        m_delayWritePos = (m_delayWritePos + 1) % delayBufferSize;

        m_wowPhase += wowInc;
        if (m_wowPhase >= 2.0f * std::numbers::pi_v<float>) m_wowPhase -= 2.0f * std::numbers::pi_v<float>;

        m_flutterPhase += flutterInc;
        if (m_flutterPhase >= 2.0f * std::numbers::pi_v<float>) m_flutterPhase -= 2.0f * std::numbers::pi_v<float>;

        // 6. Vintage Console Channel-to-Channel Crosstalk
        const float outL = readL + m_crosstalkLinear * readR;
        const float outR = readR + m_crosstalkLinear * readL;

        ch0[i] = outL;
        if (context.audio.channelCount >= 2)
        {
            ch1[i] = outR;
        }
    }
}

} // namespace dawg::engine::dsp
