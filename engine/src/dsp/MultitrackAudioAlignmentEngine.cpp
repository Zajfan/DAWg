#include "dawg/engine/dsp/MultitrackAudioAlignmentEngine.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace dawg::engine::dsp {

float MultitrackAudioAlignmentEngine::computeEnvelope(const float* signal, std::size_t start, std::size_t window) const noexcept
{
    if (signal == nullptr || window == 0) {
        return 0.0f;
    }
    float sumSq = 0.0f;
    for (std::size_t i = 0; i < window; ++i) {
        const float val = signal[start + i];
        sumSq += val * val;
    }
    return std::sqrt(sumSq / static_cast<float>(window));
}

AlignmentResult MultitrackAudioAlignmentEngine::estimatePhaseAlignment(
    const float* primaryMic,
    const float* secondaryMic,
    std::size_t numSamples,
    std::size_t maxSearchLag) noexcept
{
    AlignmentResult result {};
    if (primaryMic == nullptr || secondaryMic == nullptr || numSamples < 4 || maxSearchLag == 0) {
        return result;
    }

    const std::size_t lagLimit = std::min(maxSearchLag, numSamples / 2);
    if (lagLimit == 0) {
        return result;
    }

    const std::size_t totalLags = 2 * lagLimit + 1;
    std::vector<double> crossCorr(totalLags, 0.0);

    double energyPrim = 0.0;
    double energySec = 0.0;
    for (std::size_t i = 0; i < numSamples; ++i) {
        energyPrim += static_cast<double>(primaryMic[i]) * static_cast<double>(primaryMic[i]);
        energySec += static_cast<double>(secondaryMic[i]) * static_cast<double>(secondaryMic[i]);
    }

    const double normFactor = std::sqrt(energyPrim * energySec);
    if (normFactor < 1e-12) {
        return result;
    }

    // Compute cross-correlation: R[lag] = sum_t (primary[t] * secondary[t + lag])
    for (int lag = -static_cast<int>(lagLimit); lag <= static_cast<int>(lagLimit); ++lag) {
        const std::size_t idx = static_cast<std::size_t>(lag + static_cast<int>(lagLimit));
        double sum = 0.0;
        const int startT = std::max(0, -lag);
        const int endT = std::min(static_cast<int>(numSamples), static_cast<int>(numSamples) - lag);

        for (int t = startT; t < endT; ++t) {
            sum += static_cast<double>(primaryMic[t]) * static_cast<double>(secondaryMic[t + lag]);
        }
        crossCorr[idx] = sum;
    }

    // Find peak magnitude
    std::size_t bestIdx = lagLimit;
    double bestVal = crossCorr[bestIdx];
    double bestAbsVal = std::abs(bestVal);

    for (std::size_t i = 0; i < totalLags; ++i) {
        const double absVal = std::abs(crossCorr[i]);
        if (absVal > bestAbsVal) {
            bestAbsVal = absVal;
            bestVal = crossCorr[i];
            bestIdx = i;
        }
    }

    // Sub-sample parabolic interpolation around peak
    double peakLag = static_cast<double>(static_cast<int>(bestIdx) - static_cast<int>(lagLimit));
    if (bestIdx > 0 && bestIdx + 1 < totalLags) {
        const double alpha = std::abs(crossCorr[bestIdx - 1]);
        const double beta = bestAbsVal;
        const double gamma = std::abs(crossCorr[bestIdx + 1]);
        const double denom = 2.0 * (alpha - 2.0 * beta + gamma);
        if (std::abs(denom) > 1e-12) {
            const double delta = (alpha - gamma) / denom;
            peakLag += delta;
        }
    }

    result.success = true;
    result.detectedDelaySamples = static_cast<float>(peakLag);
    result.polarityInverted = (bestVal < 0.0);
    result.correlationScore = static_cast<float>(std::clamp(bestAbsVal / normFactor, 0.0, 1.0));
    return result;
}

void MultitrackAudioAlignmentEngine::alignSecondaryMic(
    const float* inputSecondary,
    float* outputAligned,
    std::size_t numSamples,
    float delaySamples,
    bool invertPolarity) noexcept
{
    if (inputSecondary == nullptr || outputAligned == nullptr || numSamples == 0) {
        return;
    }

    const float sign = invertPolarity ? -1.0f : 1.0f;
    const auto maxIdx = static_cast<int>(numSamples - 1);

    for (std::size_t i = 0; i < numSamples; ++i) {
        // To compensate a secondary delay of D samples (where secondary is late: s[t] = p[t-D]),
        // primary[t] is reconstructed from secondary[t + D].
        const float readPos = static_cast<float>(i) + delaySamples;
        const auto base = static_cast<int>(std::floor(readPos));
        const float mu = readPos - static_cast<float>(base);

        const auto idx0 = std::clamp(base - 1, 0, maxIdx);
        const auto idx1 = std::clamp(base, 0, maxIdx);
        const auto idx2 = std::clamp(base + 1, 0, maxIdx);
        const auto idx3 = std::clamp(base + 2, 0, maxIdx);

        const float y0 = inputSecondary[idx0];
        const float y1 = inputSecondary[idx1];
        const float y2 = inputSecondary[idx2];
        const float y3 = inputSecondary[idx3];

        // 4-point cubic Hermite interpolation
        const float c0 = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
        const float c1 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c2 = -0.5f * y0 + 0.5f * y2;
        const float c3 = y1;

        const float interpolated = ((c0 * mu + c1) * mu + c2) * mu + c3;
        outputAligned[i] = sign * interpolated;
    }
}

AlignmentResult MultitrackAudioAlignmentEngine::alignDubToGuideTrack(
    const float* guideTrack,
    std::size_t guideLength,
    const float* dubTrack,
    std::size_t dubLength,
    float* alignedDubOut,
    std::size_t outCapacity) noexcept
{
    AlignmentResult result {};
    if (guideTrack == nullptr || dubTrack == nullptr || alignedDubOut == nullptr) {
        return result;
    }
    if (guideLength < 64 || dubLength < 64 || outCapacity == 0) {
        return result;
    }

    constexpr std::size_t HopSize = 128;
    constexpr std::size_t WindowSize = 256;

    const std::size_t guideFrames = guideLength > WindowSize ? (guideLength - WindowSize) / HopSize + 1 : 1;
    const std::size_t dubFrames = dubLength > WindowSize ? (dubLength - WindowSize) / HopSize + 1 : 1;

    if (guideFrames == 0 || dubFrames == 0) {
        return result;
    }

    std::vector<float> guideEnv(guideFrames, 0.0f);
    std::vector<float> dubEnv(dubFrames, 0.0f);

    for (std::size_t n = 0; n < guideFrames; ++n) {
        const std::size_t start = n * HopSize;
        const std::size_t win = std::min(WindowSize, guideLength - start);
        const float rms = computeEnvelope(guideTrack, start, win);
        guideEnv[n] = std::log1p(10.0f * rms);
    }

    for (std::size_t m = 0; m < dubFrames; ++m) {
        const std::size_t start = m * HopSize;
        const std::size_t win = std::min(WindowSize, dubLength - start);
        const float rms = computeEnvelope(dubTrack, start, win);
        dubEnv[m] = std::log1p(10.0f * rms);
    }

    // Dynamic Time Warping Matrix (N x M)
    constexpr float INF = std::numeric_limits<float>::infinity();
    std::vector<std::vector<float>> cost(guideFrames, std::vector<float>(dubFrames, INF));

    cost[0][0] = std::abs(guideEnv[0] - dubEnv[0]);
    for (std::size_t n = 1; n < guideFrames; ++n) {
        cost[n][0] = cost[n - 1][0] + std::abs(guideEnv[n] - dubEnv[0]);
    }
    for (std::size_t m = 1; m < dubFrames; ++m) {
        cost[0][m] = cost[0][m - 1] + std::abs(guideEnv[0] - dubEnv[m]);
    }

    for (std::size_t n = 1; n < guideFrames; ++n) {
        for (std::size_t m = 1; m < dubFrames; ++m) {
            const float localCost = std::abs(guideEnv[n] - dubEnv[m]);
            const float prevMin = std::min({cost[n - 1][m], cost[n][m - 1], cost[n - 1][m - 1]});
            cost[n][m] = localCost + prevMin;
        }
    }

    // Backtrack optimal warping path from (guideFrames - 1, dubFrames - 1)
    std::vector<std::pair<std::size_t, std::size_t>> path;
    std::size_t currN = guideFrames - 1;
    std::size_t currM = dubFrames - 1;
    path.push_back({currN, currM});

    while (currN > 0 || currM > 0) {
        if (currN == 0) {
            --currM;
        } else if (currM == 0) {
            --currN;
        } else {
            const float diag = cost[currN - 1][currM - 1];
            const float up = cost[currN - 1][currM];
            const float left = cost[currN][currM - 1];
            if (diag <= up && diag <= left) {
                --currN;
                --currM;
            } else if (up <= left) {
                --currN;
            } else {
                --currM;
            }
        }
        path.push_back({currN, currM});
    }

    std::reverse(path.begin(), path.end());

    // Build guide-to-dub mapping for each guide frame
    std::vector<float> guideToDubMap(guideFrames, 0.0f);
    std::size_t pathIdx = 0;
    for (std::size_t n = 0; n < guideFrames; ++n) {
        while (pathIdx + 1 < path.size() && path[pathIdx].first < n) {
            ++pathIdx;
        }
        guideToDubMap[n] = static_cast<float>(path[pathIdx].second);
    }

    // Time-stretch / align dub track into alignedDubOut according to DTW map
    const std::size_t renderLen = std::min(guideLength, outCapacity);
    const auto maxDubIdx = static_cast<int>(dubLength - 1);

    for (std::size_t t = 0; t < renderLen; ++t) {
        const float framePos = static_cast<float>(t) / static_cast<float>(HopSize);
        const auto baseFrame = static_cast<std::size_t>(std::clamp(static_cast<int>(framePos), 0, static_cast<int>(guideFrames - 1)));
        const auto nextFrame = std::min(baseFrame + 1, guideFrames - 1);
        const float frameFrac = framePos - static_cast<float>(baseFrame);

        const float targetDubFrame = guideToDubMap[baseFrame] * (1.0f - frameFrac) + guideToDubMap[nextFrame] * frameFrac;
        const float dubSamplePos = targetDubFrame * static_cast<float>(HopSize) + static_cast<float>(t % HopSize);

        const auto baseSample = static_cast<int>(std::floor(dubSamplePos));
        const float mu = dubSamplePos - static_cast<float>(baseSample);

        const auto s0 = std::clamp(baseSample, 0, maxDubIdx);
        const auto s1 = std::clamp(baseSample + 1, 0, maxDubIdx);

        // Linear interpolation for audio rendering
        alignedDubOut[t] = dubTrack[s0] * (1.0f - mu) + dubTrack[s1] * mu;
    }

    result.success = true;
    result.warpPointsCount = path.size();
    result.correlationScore = 1.0f / (1.0f + cost[guideFrames - 1][dubFrames - 1] / static_cast<float>(path.size()));
    return result;
}

} // namespace dawg::engine::dsp
