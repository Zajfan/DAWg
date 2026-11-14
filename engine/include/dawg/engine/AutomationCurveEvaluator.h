#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace dawg::engine {

struct CurvePoint
{
    std::uint64_t samplePosition {0};
    float value {1.0f};
    float curveTension {0.0f};

    friend bool operator==(const CurvePoint&, const CurvePoint&) = default;
};

class AutomationCurveEvaluator final
{
public:
    // Evaluates a single sample position along an ordered sequence of points
    [[nodiscard]] static float evaluate(std::span<const CurvePoint> points,
                                        std::uint64_t samplePosition,
                                        float defaultValue = 1.0f) noexcept;

    // Evaluates a contiguous block of frames [startSample, startSample + outValues.size())
    static void evaluateBlock(std::span<const CurvePoint> points,
                              std::uint64_t startSample,
                              std::span<float> outValues,
                              float defaultValue = 1.0f) noexcept;

    // Evaluates normalized curve tension shape in [0, 1]
    [[nodiscard]] static float shapeTension(float normalizedT, float tension) noexcept;
};

} // namespace dawg::engine
