#include <dawg/engine/AutomationSubLaneEngine.h>

#include <algorithm>
#include <cmath>

namespace dawg::engine {

std::vector<SubLanePoint> AutomationSubLaneEngine::generatePencil(
    const std::vector<SubLanePoint>& rawPoints,
    const std::uint64_t minSampleDelta,
    const float minValueDelta)
{
    if (rawPoints.empty())
        return {};

    std::vector<SubLanePoint> filtered;
    filtered.reserve(rawPoints.size());
    filtered.push_back(rawPoints.front());

    for (std::size_t i = 1; i < rawPoints.size(); ++i)
    {
        const auto& prev = filtered.back();
        const auto& cur = rawPoints[i];

        const std::uint64_t dt = cur.samplePosition > prev.samplePosition ? (cur.samplePosition - prev.samplePosition) : 0;
        const float dv = std::abs(cur.value - prev.value);

        if (dt >= minSampleDelta || dv >= minValueDelta || i == rawPoints.size() - 1)
        {
            filtered.push_back(cur);
        }
    }

    return filtered;
}

std::vector<SubLanePoint> AutomationSubLaneEngine::generateLine(
    const std::uint64_t startSample,
    const float startValue,
    const std::uint64_t endSample,
    const float endValue)
{
    return {
        SubLanePoint { .samplePosition = startSample, .value = std::clamp(startValue, 0.0f, 1.0f), .tension = 0.0f },
        SubLanePoint { .samplePosition = std::max(startSample + 1, endSample), .value = std::clamp(endValue, 0.0f, 1.0f), .tension = 0.0f }
    };
}

std::vector<SubLanePoint> AutomationSubLaneEngine::generateBezier(
    const std::uint64_t startSample,
    const float startValue,
    const std::uint64_t endSample,
    const float endValue,
    const float tension,
    const std::size_t intermediateSteps)
{
    if (endSample <= startSample || intermediateSteps < 2)
    {
        return generateLine(startSample, startValue, endSample, endValue);
    }

    std::vector<SubLanePoint> points;
    points.reserve(intermediateSteps + 1);

    const auto totalSamples = endSample - startSample;
    const float clampedTension = std::clamp(tension, -1.0f, 1.0f);

    for (std::size_t step = 0; step <= intermediateSteps; ++step)
    {
        const float t = static_cast<float>(step) / static_cast<float>(intermediateSteps);
        // Tension curve shaping: power curve for exponential/logarithmic transitions
        float shapedT = t;
        if (std::abs(clampedTension) > 0.001f)
        {
            const float p = std::exp(-clampedTension * 2.0f);
            shapedT = std::pow(t, p);
        }

        const float val = startValue + (endValue - startValue) * shapedT;
        const auto sPos = startSample + static_cast<std::uint64_t>(std::round(static_cast<double>(t) * static_cast<double>(totalSamples)));

        points.push_back(SubLanePoint {
            .samplePosition = sPos,
            .value = std::clamp(val, 0.0f, 1.0f),
            .tension = clampedTension
        });
    }

    return points;
}

std::vector<SubLanePoint> AutomationSubLaneEngine::generateSineLfo(
    const std::uint64_t startSample,
    const std::uint64_t endSample,
    const double sampleRate,
    const double frequencyHz,
    const float minValue,
    const float maxValue,
    const std::size_t stepsPerCycle)
{
    if (endSample <= startSample || sampleRate <= 0.0 || frequencyHz <= 0.0 || stepsPerCycle < 4)
        return {};

    const double samplesPerCycle = sampleRate / frequencyHz;
    const auto stepDeltaSamples = static_cast<std::uint64_t>(std::max<double>(1.0, samplesPerCycle / static_cast<double>(stepsPerCycle)));

    const float mid = (minValue + maxValue) * 0.5f;
    const float amp = (maxValue - minValue) * 0.5f;

    std::vector<SubLanePoint> points;
    constexpr double twoPi = 6.2831853071795864769;

    for (std::uint64_t s = startSample; s <= endSample; s += stepDeltaSamples)
    {
        const double phase = static_cast<double>(s - startSample) / samplesPerCycle;
        const float val = mid + amp * static_cast<float>(std::sin(phase * twoPi));
        points.push_back(SubLanePoint {
            .samplePosition = s,
            .value = std::clamp(val, 0.0f, 1.0f),
            .tension = 0.0f
        });
    }

    if (points.empty() || points.back().samplePosition < endSample)
    {
        const double phase = static_cast<double>(endSample - startSample) / samplesPerCycle;
        const float val = mid + amp * static_cast<float>(std::sin(phase * twoPi));
        points.push_back(SubLanePoint {
            .samplePosition = endSample,
            .value = std::clamp(val, 0.0f, 1.0f),
            .tension = 0.0f
        });
    }

    return points;
}

std::vector<SubLanePoint> AutomationSubLaneEngine::generateSawLfo(
    const std::uint64_t startSample,
    const std::uint64_t endSample,
    const double sampleRate,
    const double frequencyHz,
    const float minValue,
    const float maxValue)
{
    if (endSample <= startSample || sampleRate <= 0.0 || frequencyHz <= 0.0)
        return {};

    const double samplesPerCycle = sampleRate / frequencyHz;
    const auto cycleSamples = static_cast<std::uint64_t>(std::max<double>(2.0, samplesPerCycle));

    std::vector<SubLanePoint> points;

    for (std::uint64_t cycleStart = startSample; cycleStart < endSample; cycleStart += cycleSamples)
    {
        const std::uint64_t cycleEnd = std::min(cycleStart + cycleSamples - 1, endSample);
        points.push_back(SubLanePoint {
            .samplePosition = cycleStart,
            .value = std::clamp(minValue, 0.0f, 1.0f),
            .tension = 0.0f
        });
        points.push_back(SubLanePoint {
            .samplePosition = cycleEnd,
            .value = std::clamp(maxValue, 0.0f, 1.0f),
            .tension = 0.0f
        });
    }

    return points;
}

} // namespace dawg::engine
