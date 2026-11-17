#include <dawg/engine/dsp/AudioScrubEngine.h>

namespace dawg::engine::dsp {

AudioScrubEngine::AudioScrubEngine() noexcept
{
    m_ringBufferL.resize(MaxBufferSamples, 0.0f);
    m_ringBufferR.resize(MaxBufferSamples, 0.0f);
    reset();
}

void AudioScrubEngine::prepare(double sampleRate) noexcept
{
    m_sampleRate = (sampleRate > 1000.0) ? sampleRate : 48000.0;
    reset();
}

void AudioScrubEngine::reset() noexcept
{
    std::fill(m_ringBufferL.begin(), m_ringBufferL.end(), 0.0f);
    std::fill(m_ringBufferR.begin(), m_ringBufferR.end(), 0.0f);

    m_currentSpeed = 0.0f;
    m_targetSpeed = 0.0f;
    m_bufferPlayheadPos = 0.0;
    m_bufferWritePos = 0;
    m_timelineSamplePos = 0;

    m_tapeFilterStateL = 0.0f;
    m_tapeFilterStateR = 0.0f;
}

void AudioScrubEngine::onJogWheelTurn(float ticks) noexcept
{
    m_mode = ScrubMode::jog;
    m_currentSpeed += ticks * m_jogSensitivity;
    m_currentSpeed = std::clamp(m_currentSpeed, -MaxScrubSpeed, MaxScrubSpeed);
}

void AudioScrubEngine::setShuttleSpeed(float speed) noexcept
{
    m_mode = ScrubMode::shuttle;
    m_targetSpeed = std::clamp(speed, -MaxScrubSpeed, MaxScrubSpeed);
}

void AudioScrubEngine::setTimelinePosition(std::int64_t samplePos) noexcept
{
    m_timelineSamplePos = samplePos;
}

void AudioScrubEngine::writeAudio(
    const float* left,
    const float* right,
    std::size_t numSamples,
    std::int64_t timelinePos) noexcept
{
    if (left == nullptr || numSamples == 0 || m_ringBufferL.empty())
        return;

    m_timelineSamplePos = timelinePos;
    const std::size_t bufSize = m_ringBufferL.size();

    for (std::size_t i = 0; i < numSamples; ++i)
    {
        m_ringBufferL[m_bufferWritePos] = left[i];
        m_ringBufferR[m_bufferWritePos] = (right != nullptr) ? right[i] : left[i];
        m_bufferWritePos = (m_bufferWritePos + 1) % bufSize;
    }
}

float AudioScrubEngine::readHermite(const std::vector<float>& buf, double pos) const noexcept
{
    if (buf.empty())
        return 0.0f;

    const auto bufSize = static_cast<double>(buf.size());
    double wrappedPos = std::fmod(pos, bufSize);
    if (wrappedPos < 0.0)
    {
        wrappedPos += bufSize;
    }

    const auto i0 = static_cast<std::size_t>(wrappedPos);
    const float mu = static_cast<float>(wrappedPos - static_cast<double>(i0));

    const std::size_t n = buf.size();
    const std::size_t im1 = (i0 + n - 1) % n;
    const std::size_t i1 = (i0 + 1) % n;
    const std::size_t i2 = (i0 + 2) % n;

    const float ym1 = buf[im1];
    const float y0 = buf[i0];
    const float y1 = buf[i1];
    const float y2 = buf[i2];

    const float c0 = y0;
    const float c1 = 0.5f * (y1 - ym1);
    const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
    const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);

    return ((c3 * mu + c2) * mu + c1) * mu + c0;
}

void AudioScrubEngine::process(
    float* leftOut,
    float* rightOut,
    std::size_t numSamples) noexcept
{
    if (leftOut == nullptr || rightOut == nullptr || numSamples == 0 || m_ringBufferL.empty())
        return;

    const auto bufSize = static_cast<double>(m_ringBufferL.size());

    for (std::size_t i = 0; i < numSamples; ++i)
    {
        // Update velocity dynamics
        if (m_mode == ScrubMode::jog)
        {
            m_currentSpeed *= m_frictionCoeff;
            if (std::abs(m_currentSpeed) < MuteSpeedThreshold)
            {
                m_currentSpeed = 0.0f;
            }
        }
        else // ScrubMode::shuttle
        {
            m_currentSpeed += m_inertiaCoeff * (m_targetSpeed - m_currentSpeed);
            if (std::abs(m_currentSpeed) < MuteSpeedThreshold && std::abs(m_targetSpeed) < MuteSpeedThreshold)
            {
                m_currentSpeed = 0.0f;
            }
        }

        if (std::abs(m_currentSpeed) < MuteSpeedThreshold)
        {
            leftOut[i] = 0.0f;
            rightOut[i] = 0.0f;
            continue;
        }

        // Advance buffer playhead by variable speed (handles bi-directional scrub)
        m_bufferPlayheadPos += static_cast<double>(m_currentSpeed);
        if (m_bufferPlayheadPos >= bufSize)
        {
            m_bufferPlayheadPos -= bufSize;
        }
        else if (m_bufferPlayheadPos < 0.0)
        {
            m_bufferPlayheadPos += bufSize;
        }

        m_timelineSamplePos += static_cast<std::int64_t>(std::round(m_currentSpeed));

        // 4-point cubic Hermite interpolation
        float rawL = readHermite(m_ringBufferL, m_bufferPlayheadPos);
        float rawR = readHermite(m_ringBufferR, m_bufferPlayheadPos);

        // Magnetic tape head gap low-pass filter (loss at slow or high speeds)
        const float speedMag = std::abs(m_currentSpeed);
        const float alpha = std::clamp(0.2f + 0.7f * std::min(speedMag, 1.0f), 0.05f, 0.95f);

        m_tapeFilterStateL += alpha * (rawL - m_tapeFilterStateL);
        m_tapeFilterStateR += alpha * (rawR - m_tapeFilterStateR);

        // Warm tape soft-saturation
        leftOut[i] = std::tanh(m_tapeFilterStateL);
        rightOut[i] = std::tanh(m_tapeFilterStateR);
    }

    if (m_scrubCb && isScrubbing())
    {
        m_scrubCb(m_timelineSamplePos, m_currentSpeed);
    }
}

} // namespace dawg::engine::dsp
