#include <dawg/media/TransientDetector.h>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace dawg::media {

std::vector<TransientMarker> TransientDetector::detect(
    const std::span<const float> samples,
    const double sampleRate,
    const float sensitivity) noexcept
{
    std::vector<TransientMarker> markers;
    if (samples.size() < 128 || sampleRate <= 0.0)
        return markers;

    constexpr std::size_t hopSize = 128;
    const std::size_t numBlocks = samples.size() / hopSize;
    if (numBlocks < 4)
        return markers;

    // Calculate energy flux across hops
    std::vector<float> energyFlux(numBlocks, 0.0f);
    for (std::size_t b = 0; b < numBlocks; ++b)
    {
        const std::size_t offset = b * hopSize;
        float hfcEnergy = 0.0f;
        for (std::size_t i = 1; i < hopSize; ++i)
        {
            const float diff = samples[offset + i] - samples[offset + i - 1];
            hfcEnergy += diff * diff;
        }
        energyFlux[b] = std::sqrt(hfcEnergy / static_cast<float>(hopSize));
    }

    // Adaptive moving average threshold
    constexpr std::size_t windowSize = 8;
    const float sensFactor = std::max(0.2f, sensitivity);

    for (std::size_t b = 1; b < numBlocks - 1; ++b)
    {
        const std::size_t startW = (b >= windowSize) ? (b - windowSize) : 0;
        const std::size_t endW = std::min(numBlocks, b + windowSize);

        float sum = 0.0f;
        for (std::size_t w = startW; w < endW; ++w)
            sum += energyFlux[w];
        const float localMean = sum / static_cast<float>(endW - startW);

        const float threshold = localMean * sensFactor + 0.005f;

        // Peak picking: local maximum above threshold
        if (energyFlux[b] > threshold && energyFlux[b] > energyFlux[b - 1] && energyFlux[b] >= energyFlux[b + 1])
        {
            markers.push_back({
                .sampleOffset = static_cast<std::uint64_t>(b * hopSize),
                .strength = energyFlux[b]
            });
        }
    }

    return markers;
}

} // namespace dawg::media
