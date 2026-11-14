#include <dawg/media/AudioResampler.h>

#include <algorithm>
#include <cmath>

namespace dawg::media {
namespace {
constexpr float catmullRom(const float y0, const float y1,
                           const float y2, const float y3,
                           const float t) noexcept
{
    const float a0 = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
    const float a1 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float a2 = -0.5f * y0 + 0.5f * y2;
    const float a3 = y1;
    return ((a0 * t + a1) * t + a2) * t + a3;
}
} // namespace

bool AudioResampler::prepare(const std::uint32_t sourceSampleRate,
                             const std::uint32_t targetSampleRate,
                             const std::uint16_t channelCount,
                             const std::size_t maxOutputFrames) noexcept
{
    if (sourceSampleRate == 0 || targetSampleRate == 0 || channelCount == 0
        || maxOutputFrames == 0)
    {
        m_sourceRate = 0;
        m_targetRate = 0;
        m_channelCount = 0;
        m_maxOutputFrames = 0;
        m_history.clear();
        return false;
    }

    try
    {
        m_history.assign(channelCount, {0.0f, 0.0f, 0.0f, 0.0f});
    }
    catch (...)
    {
        m_history.clear();
        return false;
    }

    m_sourceRate = sourceSampleRate;
    m_targetRate = targetSampleRate;
    m_channelCount = channelCount;
    m_maxOutputFrames = maxOutputFrames;
    m_ratio = static_cast<double>(sourceSampleRate) / static_cast<double>(targetSampleRate);
    m_phase = 0.0;
    return true;
}

void AudioResampler::reset() noexcept
{
    m_phase = 0.0;
    for (auto& hist : m_history)
        hist = {0.0f, 0.0f, 0.0f, 0.0f};
}

std::size_t AudioResampler::process(const std::span<const float* const> input,
                                    const std::size_t inputFrames,
                                    const std::span<float* const> output,
                                    const std::size_t maxOutputFrames,
                                    std::size_t& inputFramesConsumed) noexcept
{
    inputFramesConsumed = 0;
    if (m_channelCount == 0 || inputFrames == 0 || maxOutputFrames == 0
        || input.size() < m_channelCount || output.size() < m_channelCount)
    {
        return 0;
    }

    for (std::uint16_t ch = 0; ch < m_channelCount; ++ch)
    {
        if (input[ch] == nullptr || output[ch] == nullptr)
            return 0;
    }

    if (isIdentity())
    {
        const auto count = std::min(inputFrames, maxOutputFrames);
        for (std::uint16_t ch = 0; ch < m_channelCount; ++ch)
            std::copy_n(input[ch], count, output[ch]);
        inputFramesConsumed = count;
        return count;
    }

    const auto sampleAt = [&](const std::uint16_t ch, const std::ptrdiff_t idx) noexcept -> float {
        if (idx < 0)
        {
            const auto histIdx = static_cast<std::ptrdiff_t>(4) + idx;
            if (histIdx >= 0 && histIdx < 4)
                return m_history[ch][static_cast<std::size_t>(histIdx)];
            return 0.0f;
        }
        const auto uIdx = static_cast<std::size_t>(idx);
        if (uIdx < inputFrames)
            return input[ch][uIdx];
        return input[ch][inputFrames - 1];
    };

    std::size_t produced = 0;
    while (produced < maxOutputFrames)
    {
        const auto baseIdx = static_cast<std::ptrdiff_t>(std::floor(m_phase));
        if (baseIdx >= static_cast<std::ptrdiff_t>(inputFrames))
            break;

        const float frac = static_cast<float>(m_phase - static_cast<double>(baseIdx));
        for (std::uint16_t ch = 0; ch < m_channelCount; ++ch)
        {
            const float y0 = sampleAt(ch, baseIdx - 1);
            const float y1 = sampleAt(ch, baseIdx);
            const float y2 = sampleAt(ch, baseIdx + 1);
            const float y3 = sampleAt(ch, baseIdx + 2);
            output[ch][produced] = catmullRom(y0, y1, y2, y3, frac);
        }

        ++produced;
        m_phase += m_ratio;
    }

    const auto consumed = std::min(
        inputFrames,
        static_cast<std::size_t>(std::max<std::ptrdiff_t>(0, static_cast<std::ptrdiff_t>(std::floor(m_phase)))));
    inputFramesConsumed = consumed;
    m_phase -= static_cast<double>(consumed);

    if (consumed > 0)
    {
        for (std::uint16_t ch = 0; ch < m_channelCount; ++ch)
        {
            for (std::size_t k = 0; k < 4; ++k)
            {
                const auto srcPos = static_cast<std::ptrdiff_t>(consumed) - 4
                    + static_cast<std::ptrdiff_t>(k);
                m_history[ch][k] = sampleAt(ch, srcPos);
            }
        }
    }

    return produced;
}

} // namespace dawg::media
