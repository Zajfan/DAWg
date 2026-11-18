#include <dawg/engine/dsp/AudioRestorationSuiteNode.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::engine::dsp {

namespace {

constexpr float pi = std::numbers::pi_v<float>;

inline void configureNotch(NotchFilter& filter, float freqHz, float q, double sampleRate) noexcept
{
    const float nyquist = static_cast<float>(sampleRate * 0.48);
    if (freqHz >= nyquist || freqHz < 10.0f)
    {
        filter.b0 = 1.0f; filter.b1 = 0.0f; filter.b2 = 0.0f;
        filter.a1 = 0.0f; filter.a2 = 0.0f;
        return;
    }

    const float w0 = 2.0f * pi * freqHz / static_cast<float>(sampleRate);
    const float cosW = std::cos(w0);
    const float sinW = std::sin(w0);
    const float alpha = sinW / (2.0f * std::max(1.0f, q));

    const float b0 = 1.0f;
    const float b1 = -2.0f * cosW;
    const float b2 = 1.0f;
    const float a0 = 1.0f + alpha;
    const float a1 = -2.0f * cosW;
    const float a2 = 1.0f - alpha;

    const float invA0 = 1.0f / a0;
    filter.b0 = b0 * invA0;
    filter.b1 = b1 * invA0;
    filter.b2 = b2 * invA0;
    filter.a1 = a1 * invA0;
    filter.a2 = a2 * invA0;
}

} // namespace

AudioRestorationSuiteNode::AudioRestorationSuiteNode() noexcept
{
    updateHumFilters();
}

bool AudioRestorationSuiteNode::prepare(double sampleRate, std::size_t /*maxBlockSize*/, std::uint16_t channelCount)
{
    m_sampleRate = (sampleRate > 0.0) ? sampleRate : 48000.0;
    m_channelCount = channelCount;
    reset();
    updateHumFilters();
    return true;
}

void AudioRestorationSuiteNode::reset() noexcept
{
    m_arHistoryL.fill(0.0f);
    m_arHistoryR.fill(0.0f);
    m_predErrorVarianceL = 0.001f;
    m_predErrorVarianceR = 0.001f;
    m_clippedSamplesCount.store(0, std::memory_order_relaxed);
    m_repairedClicksCount.store(0, std::memory_order_relaxed);
    m_lookBufferL.fill(0.0f);
    m_lookBufferR.fill(0.0f);
    m_lookWriteIdx = 0;

    for (auto& n : m_humNotches)
    {
        n.reset();
    }
}

void AudioRestorationSuiteNode::setClipThresholdDb(float threshDb) noexcept
{
    m_clipThresholdDb = std::clamp(threshDb, -12.0f, 0.0f);
    m_clipThresholdLinear = std::pow(10.0f, m_clipThresholdDb / 20.0f);
}

void AudioRestorationSuiteNode::setMainsFrequency(MainsHumFrequency freq) noexcept
{
    m_mainsFrequency = freq;
    updateHumFilters();
}

void AudioRestorationSuiteNode::setHumHarmonicsCount(std::size_t count) noexcept
{
    m_humHarmonicsCount = std::clamp(count, std::size_t{1}, maxHarmonics);
    updateHumFilters();
}

void AudioRestorationSuiteNode::setNotchQ(float q) noexcept
{
    m_notchQ = std::clamp(q, 5.0f, 100.0f);
    updateHumFilters();
}

void AudioRestorationSuiteNode::updateHumFilters() noexcept
{
    const float baseFreq = (m_mainsFrequency == MainsHumFrequency::hum50Hz) ? 50.0f : 60.0f;

    for (std::size_t h = 0; h < maxHarmonics; ++h)
    {
        if (h < m_humHarmonicsCount)
        {
            const float harmFreq = baseFreq * static_cast<float>(h + 1);
            configureNotch(m_humNotches[h], harmFreq, m_notchQ, m_sampleRate);
        }
        else
        {
            // Passthrough
            m_humNotches[h].b0 = 1.0f; m_humNotches[h].b1 = 0.0f; m_humNotches[h].b2 = 0.0f;
            m_humNotches[h].a1 = 0.0f; m_humNotches[h].a2 = 0.0f;
        }
    }
}

float AudioRestorationSuiteNode::repairClipSample(float in, float prev, float next, float thresh) noexcept
{
    const float absIn = std::abs(in);
    if (absIn <= thresh)
    {
        return in;
    }

    const float sign = (in >= 0.0f) ? 1.0f : -1.0f;
    // Cubic polynomial reconstruction across peak
    const float deriv = (next - prev) * 0.5f;
    const float excess = absIn - thresh;
    const float reconstructedPeak = thresh + excess * 1.6f + std::abs(deriv) * 0.2f;

    // Soft ceiling limiter to avoid divergence
    const float limited = thresh + std::tanh((reconstructedPeak - thresh) * 0.8f) * 0.4f;
    return sign * limited;
}

void AudioRestorationSuiteNode::process(ProcessContext& context) noexcept
{
    const auto blockFrames = context.audio.frameCount;
    if (blockFrames == 0 || context.audio.channels == nullptr || context.audio.channelCount == 0)
    {
        return;
    }

    if (m_listenMode == RestorationListenMode::bypass)
    {
        return;
    }

    float* left = context.audio.channels[0];
    float* right = (context.audio.channelCount > 1 && context.audio.channels[1] != nullptr)
        ? context.audio.channels[1]
        : left;

    std::size_t clipsThisBlock = 0;
    std::size_t clicksThisBlock = 0;

    for (std::size_t i = 0; i < blockFrames; ++i)
    {
        const float origL = left[i];
        const float origR = right[i];

        float procL = origL;
        float procR = origR;

        // 1. Polynomial De-Clipper
        if (m_deClipEnabled)
        {
            const float prevL = (i > 0) ? left[i - 1] : procL;
            const float nextL = (i + 1 < blockFrames) ? left[i + 1] : procL;
            if (std::abs(procL) >= m_clipThresholdLinear)
            {
                procL = repairClipSample(procL, prevL, nextL, m_clipThresholdLinear);
                ++clipsThisBlock;
            }

            const float prevR = (i > 0) ? right[i - 1] : procR;
            const float nextR = (i + 1 < blockFrames) ? right[i + 1] : procR;
            if (std::abs(procR) >= m_clipThresholdLinear)
            {
                procR = repairClipSample(procR, prevR, nextR, m_clipThresholdLinear);
                ++clipsThisBlock;
            }
        }

        // 2. Autoregressive De-Clicker
        if (m_deClickEnabled)
        {
            // Linear prediction L
            const float predL = m_arCoeffs[0] * m_arHistoryL[0] +
                                m_arCoeffs[1] * m_arHistoryL[1] +
                                m_arCoeffs[2] * m_arHistoryL[2] +
                                m_arCoeffs[3] * m_arHistoryL[3];
            const float errL = std::abs(procL - predL);
            const float stdDevL = std::sqrt(m_predErrorVarianceL);
            const float clickThreshL = std::max(0.04f, (3.5f * (1.1f - m_clickSensitivity)) * stdDevL);

            if (errL > clickThreshL && std::abs(procL) > 0.01f)
            {
                // Inpaint with prediction
                procL = predL;
                ++clicksThisBlock;
            }
            else
            {
                m_predErrorVarianceL = 0.99f * m_predErrorVarianceL + 0.01f * (errL * errL);
            }

            // Update AR history L
            m_arHistoryL[3] = m_arHistoryL[2];
            m_arHistoryL[2] = m_arHistoryL[1];
            m_arHistoryL[1] = m_arHistoryL[0];
            m_arHistoryL[0] = procL;

            // Linear prediction R
            const float predR = m_arCoeffs[0] * m_arHistoryR[0] +
                                m_arCoeffs[1] * m_arHistoryR[1] +
                                m_arCoeffs[2] * m_arHistoryR[2] +
                                m_arCoeffs[3] * m_arHistoryR[3];
            const float errR = std::abs(procR - predR);
            const float stdDevR = std::sqrt(m_predErrorVarianceR);
            const float clickThreshR = std::max(0.04f, (3.5f * (1.1f - m_clickSensitivity)) * stdDevR);

            if (errR > clickThreshR && std::abs(procR) > 0.01f)
            {
                procR = predR;
                ++clicksThisBlock;
            }
            else
            {
                m_predErrorVarianceR = 0.99f * m_predErrorVarianceR + 0.01f * (errR * errR);
            }

            // Update AR history R
            m_arHistoryR[3] = m_arHistoryR[2];
            m_arHistoryR[2] = m_arHistoryR[1];
            m_arHistoryR[1] = m_arHistoryR[0];
            m_arHistoryR[0] = procR;
        }

        // 3. Phase-Locked Mains Hum & Buzz Eliminator
        if (m_deHumEnabled)
        {
            for (std::size_t h = 0; h < m_humHarmonicsCount; ++h)
            {
                procL = m_humNotches[h].processL(procL);
                procR = m_humNotches[h].processR(procR);
            }
        }

        // 4. Output or Delta listen
        if (m_listenMode == RestorationListenMode::auditionDelta)
        {
            left[i] = origL - procL;
            if (right != left)
            {
                right[i] = origR - procR;
            }
        }
        else
        {
            left[i] = procL;
            if (right != left)
            {
                right[i] = procR;
            }
        }
    }

    if (clipsThisBlock > 0)
    {
        m_clippedSamplesCount.fetch_add(clipsThisBlock, std::memory_order_relaxed);
    }
    if (clicksThisBlock > 0)
    {
        m_repairedClicksCount.fetch_add(clicksThisBlock, std::memory_order_relaxed);
    }
}

} // namespace dawg::engine::dsp
