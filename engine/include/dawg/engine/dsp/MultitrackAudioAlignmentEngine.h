#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace dawg::engine::dsp {

struct AlignmentResult
{
    bool success {false};
    float correlationScore {0.0f};    // 0.0 to 1.0 (phase/energy alignment quality)
    float detectedDelaySamples {0.0f}; // Fractional sample delay detected
    bool polarityInverted {false};     // True if 180-degree phase flip is recommended
    std::size_t warpPointsCount {0};   // Number of DTW warp path nodes
};

class MultitrackAudioAlignmentEngine
{
public:
    MultitrackAudioAlignmentEngine() noexcept = default;
    ~MultitrackAudioAlignmentEngine() = default;

    // Sub-sample Multi-Mic Phase Coherence Optimization
    [[nodiscard]] AlignmentResult estimatePhaseAlignment(
        const float* primaryMic,
        const float* secondaryMic,
        std::size_t numSamples,
        std::size_t maxSearchLag = 512) noexcept;

    // Apply fractional delay phase alignment & polarity flip
    void alignSecondaryMic(
        const float* inputSecondary,
        float* outputAligned,
        std::size_t numSamples,
        float delaySamples,
        bool invertPolarity) noexcept;

    // Dynamic Time Warping (DTW) Vocal / Double-Track Alignment
    [[nodiscard]] AlignmentResult alignDubToGuideTrack(
        const float* guideTrack,
        std::size_t guideLength,
        const float* dubTrack,
        std::size_t dubLength,
        float* alignedDubOut,
        std::size_t outCapacity) noexcept;

private:
    [[nodiscard]] float computeEnvelope(const float* signal, std::size_t start, std::size_t window) const noexcept;
};

} // namespace dawg::engine::dsp
