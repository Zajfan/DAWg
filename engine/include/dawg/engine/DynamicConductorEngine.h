#pragma once

#include <dawg/engine/TempoMap.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace dawg::engine {

enum class TempoCurveType : std::uint8_t
{
    step,
    linear,
    exponential,
    sCurve
};

enum class MeterClickAccent : std::uint8_t
{
    low = 0,    // Unaccented off-beat
    mid = 1,    // Secondary subdivision pulse (e.g. beat 3 or 5 in 7/8)
    high = 2    // Primary downbeat (beat 1)
};

struct ConductorTempoNode
{
    std::uint64_t tick {0};
    double bpm {120.0};
    TempoCurveType curveType {TempoCurveType::linear};
    float tension {0.0f}; // -1.0 .. +1.0 for curvature
};

struct ComplexMeterNode
{
    std::uint32_t bar {1};
    std::uint16_t numerator {7};
    std::uint16_t denominator {8};
    std::vector<std::uint16_t> groupings {2, 2, 3}; // e.g. 7/8 = 2+2+3
};

struct FilmHitPoint
{
    std::string name {"Hit Point"};
    double timeSeconds {0.0};
    std::uint32_t targetBar {1};
    double targetBeat {1.0};
};

struct FittedTempoSolution
{
    double initialBpm {120.0};
    double targetBpm {120.0};
    double timeErrorMs {0.0};
    bool succeeded {false};
};

class DynamicConductorEngine final
{
public:
    DynamicConductorEngine() noexcept;
    ~DynamicConductorEngine() = default;

    // =========================================================================
    // Continuous Tempo Automation Curves
    // =========================================================================
    void clearTempoNodes() noexcept;
    void addTempoNode(const ConductorTempoNode& node);
    [[nodiscard]] std::size_t tempoNodeCount() const noexcept { return m_tempoNodes.size(); }
    [[nodiscard]] const std::vector<ConductorTempoNode>& tempoNodes() const noexcept { return m_tempoNodes; }

    /// Evaluates continuous BPM at a given tick with linear/exponential/S-curve interpolation
    [[nodiscard]] double bpmAtTick(std::uint64_t tick) const noexcept;

    /// Integrated sample-accurate conversion between ticks and seconds / samples
    [[nodiscard]] double tickToSeconds(double tick) const noexcept;
    [[nodiscard]] double secondsToTick(double seconds) const noexcept;

    // =========================================================================
    // Complex & Additive Meter Signatures (7/8, 5/4, 11/16, etc.)
    // =========================================================================
    void clearMeterNodes() noexcept;
    void addMeterNode(const ComplexMeterNode& node);
    [[nodiscard]] std::size_t meterNodeCount() const noexcept { return m_meterNodes.size(); }
    [[nodiscard]] const std::vector<ComplexMeterNode>& meterNodes() const noexcept { return m_meterNodes; }

    /// Returns the active complex meter for a given measure
    [[nodiscard]] ComplexMeterNode meterAtBar(std::uint32_t bar) const noexcept;

    /// Returns the metronome click accent (high, mid, low) for a given subdivision index in a bar
    [[nodiscard]] static MeterClickAccent evaluateClickAccent(
        const ComplexMeterNode& meter,
        std::size_t subdivisionIndex) noexcept;

    // =========================================================================
    // Film Scoring Hit-Point Tempo Fitter
    // =========================================================================

    /// Calculates the exact constant BPM required to land between two visual hit points
    [[nodiscard]] static FittedTempoSolution fitConstantTempo(
        double startTimeSeconds,
        double endTimeSeconds,
        double elapsedBeats) noexcept;

    /// Calculates a linear tempo ramp (accelerando / ritardando) from initialBpm
    /// so the playhead arrives precisely at the visual cue
    [[nodiscard]] static FittedTempoSolution fitAccelerandoRamp(
        double startTimeSeconds,
        double endTimeSeconds,
        double initialBpm,
        double elapsedBeats) noexcept;

private:
    std::vector<ConductorTempoNode> m_tempoNodes;
    std::vector<ComplexMeterNode> m_meterNodes;
};

} // namespace dawg::engine
