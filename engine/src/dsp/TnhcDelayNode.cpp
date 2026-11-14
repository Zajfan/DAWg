#include <dawg/engine/dsp/TnhcDelayNode.h>

#include <numbers>

namespace dawg::engine {

namespace {
constexpr float kMaxDelaySeconds = 3.0f;
} // namespace

bool TnhcDelayNode::prepare(double sampleRate, std::size_t maxBlockSize, std::uint16_t channelCount)
{
    if (sampleRate <= 0.0 || maxBlockSize == 0 || channelCount == 0)
        return false;

    m_sampleRate = sampleRate;
    m_channelCount = channelCount;

    m_bufferCapacity = static_cast<std::size_t>(std::ceil(m_sampleRate * kMaxDelaySeconds)) + 16;
    m_bufferL.assign(m_bufferCapacity, 0.0f);
    m_bufferR.assign(m_bufferCapacity, 0.0f);
    m_writeIdxL = 0;
    m_writeIdxR = 0;

    reset();
    updateFilterCoeffs();
    return true;
}

void TnhcDelayNode::reset() noexcept
{
    std::fill(m_bufferL.begin(), m_bufferL.end(), 0.0f);
    std::fill(m_bufferR.begin(), m_bufferR.end(), 0.0f);
    m_writeIdxL = 0;
    m_writeIdxR = 0;
    m_lpStateL = 0.0f;
    m_lpStateR = 0.0f;
    m_hpStateL = 0.0f;
    m_hpPrevInL = 0.0f;
    m_hpStateR = 0.0f;
    m_hpPrevInR = 0.0f;
}

void TnhcDelayNode::setBpm(double bpm) noexcept
{
    m_bpm = std::clamp(bpm, 10.0, 999.0);
}

void TnhcDelayNode::setDelayTimeMsLeft(float ms) noexcept
{
    m_delayTimeMsLeft = std::clamp(ms, 1.0f, kMaxDelaySeconds * 1000.0f);
}

void TnhcDelayNode::setDelayTimeMsRight(float ms) noexcept
{
    m_delayTimeMsRight = std::clamp(ms, 1.0f, kMaxDelaySeconds * 1000.0f);
}

void TnhcDelayNode::setFeedback(float fb) noexcept
{
    m_feedback = std::clamp(fb, 0.0f, 0.98f);
}

void TnhcDelayNode::setHighCutHz(float hz) noexcept
{
    m_highCutHz = std::clamp(hz, 200.0f, 20000.0f);
    updateFilterCoeffs();
}

void TnhcDelayNode::setLowCutHz(float hz) noexcept
{
    m_lowCutHz = std::clamp(hz, 10.0f, 4000.0f);
    updateFilterCoeffs();
}

void TnhcDelayNode::setWetDry(float wet) noexcept
{
    m_wetDry = std::clamp(wet, 0.0f, 1.0f);
}

void TnhcDelayNode::updateFilterCoeffs() noexcept
{
    // 1-pole Lowpass coefficient: y[n] = y[n-1] + alpha * (x[n] - y[n-1])
    const double wcLp = 2.0 * std::numbers::pi * static_cast<double>(m_highCutHz) / m_sampleRate;
    m_lpCoeff = static_cast<float>(std::clamp(wcLp / (wcLp + 1.0), 0.001, 1.0));

    // 1-pole Highpass coefficient: alpha = 1 / (1 + 2 * pi * fc * dt)
    const double rc = 1.0 / (2.0 * std::numbers::pi * static_cast<double>(m_lowCutHz));
    const double dt = 1.0 / m_sampleRate;
    m_hpCoeff = static_cast<float>(std::clamp(rc / (rc + dt), 0.0, 0.999));
}

float TnhcDelayNode::effectiveDelayMsLeft() const noexcept
{
    if (m_syncDivision == DelaySyncDivision::freeMs)
        return m_delayTimeMsLeft;

    const double beatMs = 60000.0 / m_bpm;
    switch (m_syncDivision)
    {
        case DelaySyncDivision::quarter: return static_cast<float>(beatMs);
        case DelaySyncDivision::eighth: return static_cast<float>(beatMs * 0.5);
        case DelaySyncDivision::eighthTriplet: return static_cast<float>(beatMs * (1.0 / 3.0));
        case DelaySyncDivision::eighthDotted: return static_cast<float>(beatMs * 0.75);
        case DelaySyncDivision::sixteenth: return static_cast<float>(beatMs * 0.25);
        case DelaySyncDivision::freeMs: default: return m_delayTimeMsLeft;
    }
}

float TnhcDelayNode::effectiveDelayMsRight() const noexcept
{
    if (m_syncDivision == DelaySyncDivision::freeMs)
        return m_delayTimeMsRight;

    // For ping-pong or stereo in sync mode, Right is often offset or equal
    const double beatMs = 60000.0 / m_bpm;
    switch (m_syncDivision)
    {
        case DelaySyncDivision::quarter: return static_cast<float>(beatMs * 0.5); // 1/8 note on R for bounce
        case DelaySyncDivision::eighth: return static_cast<float>(beatMs * 0.75); // dotted 8th on R
        case DelaySyncDivision::eighthTriplet: return static_cast<float>(beatMs * (2.0 / 3.0));
        case DelaySyncDivision::eighthDotted: return static_cast<float>(beatMs * 0.5);
        case DelaySyncDivision::sixteenth: return static_cast<float>(beatMs * 0.5);
        case DelaySyncDivision::freeMs: default: return m_delayTimeMsRight;
    }
}

float TnhcDelayNode::readInterpolated(const std::vector<float>& buf, std::size_t writeIdx,
                                      float delaySamples) const noexcept
{
    if (buf.empty() || m_bufferCapacity == 0)
        return 0.0f;

    const float clampedDelay = std::clamp(delaySamples, 1.0f, static_cast<float>(m_bufferCapacity - 5));
    float readPos = static_cast<float>(writeIdx) - clampedDelay;
    while (readPos < 0.0f)
        readPos += static_cast<float>(m_bufferCapacity);

    const auto i0 = static_cast<std::size_t>(readPos) % m_bufferCapacity;
    const float frac = readPos - static_cast<float>(static_cast<std::size_t>(readPos));

    const auto im1 = (i0 + m_bufferCapacity - 1) % m_bufferCapacity;
    const auto i1 = (i0 + 1) % m_bufferCapacity;
    const auto i2 = (i0 + 2) % m_bufferCapacity;

    const float ym1 = buf[im1];
    const float y0 = buf[i0];
    const float y1 = buf[i1];
    const float y2 = buf[i2];

    // Cubic Hermite interpolation
    const float c0 = y0;
    const float c1 = 0.5f * (y1 - ym1);
    const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
    const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);

    return ((c3 * frac + c2) * frac + c1) * frac + c0;
}

void TnhcDelayNode::process(ProcessContext& context) noexcept
{
    auto& audio = context.audio;
    if (audio.channels == nullptr || audio.frameCount == 0 || audio.channelCount == 0 || m_bufferCapacity == 0)
        return;

    const auto frameCount = audio.frameCount;
    float* const chLeft = audio.channels[0];
    float* const chRight = (audio.channelCount > 1 && audio.channels[1] != nullptr)
        ? audio.channels[1]
        : chLeft;

    const float delaySamplesL = (effectiveDelayMsLeft() * 0.001f) * static_cast<float>(m_sampleRate);
    const float delaySamplesR = (effectiveDelayMsRight() * 0.001f) * static_cast<float>(m_sampleRate);

    for (std::size_t f = 0; f < frameCount; ++f)
    {
        const float dryL = chLeft[f];
        const float dryR = chRight[f];

        // 1. Read delayed samples with Hermite interpolation
        const float delayedL = readInterpolated(m_bufferL, m_writeIdxL, delaySamplesL);
        const float delayedR = readInterpolated(m_bufferR, m_writeIdxR, delaySamplesR);

        // 2. High-pass filter delayed samples
        m_hpStateL = m_hpCoeff * (m_hpStateL + delayedL - m_hpPrevInL);
        m_hpPrevInL = delayedL;
        const float hpFilteredL = m_hpStateL;

        m_hpStateR = m_hpCoeff * (m_hpStateR + delayedR - m_hpPrevInR);
        m_hpPrevInR = delayedR;
        const float hpFilteredR = m_hpStateR;

        // 3. Low-pass filter delayed samples
        m_lpStateL += m_lpCoeff * (hpFilteredL - m_lpStateL);
        const float filteredL = m_lpStateL;

        m_lpStateR += m_lpCoeff * (hpFilteredR - m_lpStateR);
        const float filteredR = m_lpStateR;

        // 4. Soft saturation on feedback signal: tanh
        const float satFbL = std::tanh(filteredL * m_feedback);
        const float satFbR = std::tanh(filteredR * m_feedback);

        // 5. Write to delay buffer
        if (m_mode == DelayMode::pingPong)
        {
            // Ping-pong: Left input + satFbR writes to Left buffer; Right input + satFbL writes to Right buffer
            m_bufferL[m_writeIdxL] = dryL + satFbR;
            m_bufferR[m_writeIdxR] = dryR + satFbL;
        }
        else
        {
            // Dual Mono
            m_bufferL[m_writeIdxL] = dryL + satFbL;
            m_bufferR[m_writeIdxR] = dryR + satFbR;
        }

        m_writeIdxL = (m_writeIdxL + 1) % m_bufferCapacity;
        m_writeIdxR = (m_writeIdxR + 1) % m_bufferCapacity;

        // 6. Output mix
        chLeft[f] = dryL * (1.0f - m_wetDry) + delayedL * m_wetDry;
        if (chLeft != chRight)
            chRight[f] = dryR * (1.0f - m_wetDry) + delayedR * m_wetDry;
    }
}

} // namespace dawg::engine
