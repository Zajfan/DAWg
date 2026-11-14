#include <dawg/engine/WsolaTimeStretcher.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::engine {
namespace {

constexpr double twoPi = 2.0 * std::numbers::pi;

void computeHannWindow(std::span<float> win) noexcept
{
    const auto n = win.size();
    for (std::size_t i = 0; i < n; ++i)
    {
        win[i] = 0.5f * (1.0f - static_cast<float>(std::cos(twoPi * static_cast<double>(i) / static_cast<double>(n))));
    }
}

} // namespace

WsolaTimeStretcher::WsolaTimeStretcher(const std::uint16_t channelCount) noexcept
    : m_channelCount(channelCount)
{
}

void WsolaTimeStretcher::setStretchRatio(const float ratio) noexcept
{
    m_stretchRatio = std::clamp(ratio, 0.25f, 4.0f);
}

void WsolaTimeStretcher::reset() noexcept
{
}

std::vector<std::vector<float>> WsolaTimeStretcher::stretch(
    const std::span<const std::span<const float>> inputChannels,
    const float stretchRatio) noexcept
{
    const float ratio = std::clamp(stretchRatio, 0.25f, 4.0f);
    if (inputChannels.empty() || inputChannels[0].empty())
        return {};

    const std::size_t inFrames = inputChannels[0].size();
    const auto numChannels = inputChannels.size();

    if (std::abs(ratio - 1.0f) < 1e-4f)
    {
        // Identity copy
        std::vector<std::vector<float>> out(numChannels);
        for (std::size_t ch = 0; ch < numChannels; ++ch)
        {
            out[ch].assign(inputChannels[ch].begin(), inputChannels[ch].end());
        }
        return out;
    }

    const auto outFrames = static_cast<std::size_t>(
        std::round(static_cast<double>(inFrames) * static_cast<double>(ratio)));

    std::vector<std::vector<float>> out(numChannels, std::vector<float>(outFrames + frameSize, 0.0f));
    std::vector<float> weight(outFrames + frameSize, 0.0f);

    std::vector<float> hann(frameSize);
    computeHannWindow(hann);

    std::int64_t prevInputCenter = 0;

    for (std::size_t outPos = 0; outPos + frameSize <= outFrames; outPos += hopSize)
    {
        const auto nominalInPos = static_cast<std::int64_t>(std::round(static_cast<double>(outPos) / static_cast<double>(ratio)));
        const std::int64_t targetNaturalIn = prevInputCenter + static_cast<std::int64_t>(hopSize);

        // Cross-correlation search within [-maxSearch, +maxSearch]
        std::int64_t bestShift = 0;
        float bestCorr = -1e9f;

        const auto searchMin = -static_cast<std::int64_t>(maxSearch);
        const auto searchMax = static_cast<std::int64_t>(maxSearch);

        for (std::int64_t delta = searchMin; delta <= searchMax; delta += 4)
        {
            const std::int64_t candidateIn = nominalInPos + delta;
            if (candidateIn < 0 || candidateIn + static_cast<std::int64_t>(frameSize) > static_cast<std::int64_t>(inFrames))
                continue;
            if (targetNaturalIn < 0 || targetNaturalIn + static_cast<std::int64_t>(frameSize) > static_cast<std::int64_t>(inFrames))
            {
                bestShift = 0;
                break;
            }

            float corr = 0.0f;
            for (std::size_t j = 0; j < frameSize; j += 8)
            {
                corr += inputChannels[0][static_cast<std::size_t>(candidateIn) + j]
                      * inputChannels[0][static_cast<std::size_t>(targetNaturalIn) + j];
            }

            if (corr > bestCorr)
            {
                bestCorr = corr;
                bestShift = delta;
            }
        }

        const auto actualIn = static_cast<std::size_t>(
            std::clamp<std::int64_t>(nominalInPos + bestShift, 0, static_cast<std::int64_t>(inFrames - frameSize)));
        prevInputCenter = static_cast<std::int64_t>(actualIn);

        // Window and overlap-add into synthesis buffer
        for (std::size_t ch = 0; ch < numChannels; ++ch)
        {
            for (std::size_t i = 0; i < frameSize; ++i)
            {
                out[ch][outPos + i] += inputChannels[ch][actualIn + i] * hann[i];
            }
        }
        for (std::size_t i = 0; i < frameSize; ++i)
        {
            weight[outPos + i] += hann[i];
        }
    }

    // Normalize by overlap-add weight
    for (std::size_t ch = 0; ch < numChannels; ++ch)
    {
        out[ch].resize(outFrames);
        for (std::size_t i = 0; i < outFrames; ++i)
        {
            if (weight[i] > 1e-4f)
                out[ch][i] /= weight[i];
        }
    }

    return out;
}

} // namespace dawg::engine
