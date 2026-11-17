#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dawg::engine {

enum class AutomationDrawTool : std::uint8_t
{
    pencil,
    line,
    bezier,
    sine,
    saw
};

struct SubLanePoint
{
    std::uint64_t samplePosition {0};
    float value {0.5f};
    float tension {0.0f}; // -1.0 (logarithmic) .. 0.0 (linear) .. +1.0 (exponential)

    friend bool operator==(const SubLanePoint&, const SubLanePoint&) = default;
};

class AutomationSubLaneEngine final
{
public:
    AutomationSubLaneEngine() = delete;

    /// Generates thinned curve points from freehand pencil gestures with deadband tolerance
    [[nodiscard]] static std::vector<SubLanePoint> generatePencil(
        const std::vector<SubLanePoint>& rawPoints,
        std::uint64_t minSampleDelta = 256,
        float minValueDelta = 0.005f);

    /// Generates straight line ramp points
    [[nodiscard]] static std::vector<SubLanePoint> generateLine(
        std::uint64_t startSample,
        float startValue,
        std::uint64_t endSample,
        float endValue);

    /// Generates bezier curved points with configurable curvature tension
    [[nodiscard]] static std::vector<SubLanePoint> generateBezier(
        std::uint64_t startSample,
        float startValue,
        std::uint64_t endSample,
        float endValue,
        float tension,
        std::size_t intermediateSteps = 16);

    /// Generates sinusoidal LFO cycle modulation points
    [[nodiscard]] static std::vector<SubLanePoint> generateSineLfo(
        std::uint64_t startSample,
        std::uint64_t endSample,
        double sampleRate,
        double frequencyHz,
        float minValue = 0.0f,
        float maxValue = 1.0f,
        std::size_t stepsPerCycle = 16);

    /// Generates sawtooth modulation ramp points
    [[nodiscard]] static std::vector<SubLanePoint> generateSawLfo(
        std::uint64_t startSample,
        std::uint64_t endSample,
        double sampleRate,
        double frequencyHz,
        float minValue = 0.0f,
        float maxValue = 1.0f);
};

} // namespace dawg::engine
