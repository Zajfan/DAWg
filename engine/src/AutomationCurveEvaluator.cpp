#include <dawg/engine/AutomationCurveEvaluator.h>

#include <algorithm>
#include <cmath>

namespace dawg::engine {

float AutomationCurveEvaluator::shapeTension(const float normalizedT, const float tension) noexcept
{
    const float t = std::clamp(normalizedT, 0.0f, 1.0f);
    const float k = std::clamp(tension, -1.0f, 1.0f);

    if (std::abs(k) < 1e-4f)
        return t;

    // Exponential tension shaping formula
    const float alpha = k * 3.0f;
    const float denom = std::expm1(alpha);
    if (std::abs(denom) < 1e-6f)
        return t;

    return std::clamp(std::expm1(alpha * t) / denom, 0.0f, 1.0f);
}

float AutomationCurveEvaluator::evaluate(const std::span<const CurvePoint> points,
                                         const std::uint64_t samplePosition,
                                         const float defaultValue) noexcept
{
    if (points.empty())
        return defaultValue;

    if (samplePosition <= points.front().samplePosition)
        return points.front().value;

    if (samplePosition >= points.back().samplePosition)
        return points.back().value;

    // Binary search for the right-hand bounding point
    const auto it = std::upper_bound(points.begin(), points.end(), samplePosition,
                                     [](const std::uint64_t pos, const CurvePoint& pt) {
                                         return pos < pt.samplePosition;
                                     });

    if (it == points.begin())
        return it->value;
    if (it == points.end())
        return points.back().value;

    const auto& p1 = *std::prev(it);
    const auto& p2 = *it;

    if (p2.samplePosition == p1.samplePosition)
        return p2.value;

    const auto deltaSample = static_cast<double>(p2.samplePosition - p1.samplePosition);
    const auto offsetSample = static_cast<double>(samplePosition - p1.samplePosition);
    const float t = static_cast<float>(offsetSample / deltaSample);

    const float shapedT = shapeTension(t, p1.curveTension);
    return p1.value + shapedT * (p2.value - p1.value);
}

void AutomationCurveEvaluator::evaluateBlock(const std::span<const CurvePoint> points,
                                            const std::uint64_t startSample,
                                            const std::span<float> outValues,
                                            const float defaultValue) noexcept
{
    if (outValues.empty())
        return;

    if (points.empty())
    {
        std::fill(outValues.begin(), outValues.end(), defaultValue);
        return;
    }

    for (std::size_t i = 0; i < outValues.size(); ++i)
    {
        outValues[i] = evaluate(points, startSample + i, defaultValue);
    }
}

} // namespace dawg::engine
