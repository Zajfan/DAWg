#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace dawg::engine {

enum class CrossfaderCurve : std::uint8_t
{
    smoothEqualPower, // Constant power curve (-3dB at center)
    linear,           // Linear crossfade (-6dB at center)
    scratchCut        // Sharp cut for scratching / turntablism
};

enum class CrossfadeAssign : std::uint8_t
{
    none, // Unaffected by crossfader (plays at normal track fader level)
    busA, // Fades out as crossfader moves towards B (+1.0)
    busB  // Fades out as crossfader moves towards A (-1.0)
};

class DjCrossfaderEngine final
{
public:
    DjCrossfaderEngine() noexcept = default;
    ~DjCrossfaderEngine() = default;

    /// Sets crossfader position in range [-1.0 (Bus A) .. +1.0 (Bus B)]
    void setPosition(float position) noexcept;
    [[nodiscard]] float position() const noexcept { return m_position; }

    void setCurve(CrossfaderCurve curve) noexcept { m_curve = curve; }
    [[nodiscard]] CrossfaderCurve curve() const noexcept { return m_curve; }

    /// Sets track assignment to Crossfader (None, Bus A, or Bus B)
    void setTrackAssignment(std::uint64_t trackId, CrossfadeAssign assign) noexcept;
    [[nodiscard]] CrossfadeAssign getTrackAssignment(std::uint64_t trackId) const noexcept;

    /// Computes the bus A and bus B gain multipliers based on current position and curve
    void getBusGains(float& outGainA, float& outGainB) const noexcept;

    /// Computes effective channel gain multiplying track gain by crossfader attenuation
    [[nodiscard]] float computeTrackGain(std::uint64_t trackId, float baseGain) const noexcept;

    /// Nudge tempo for beat-matching (+/- percentage, e.g. +1.0% or -1.0%)
    [[nodiscard]] static double calculateTempoNudgeBpm(double currentBpm, float nudgePercent) noexcept;

    void reset() noexcept;

private:
    float m_position {0.0f}; // Center = 0.0, Bus A = -1.0, Bus B = +1.0
    CrossfaderCurve m_curve {CrossfaderCurve::smoothEqualPower};

    struct TrackAssignEntry
    {
        std::uint64_t trackId {0};
        CrossfadeAssign assign {CrossfadeAssign::none};
    };
    static constexpr std::size_t maxTrackAssigns = 64;
    std::array<TrackAssignEntry, maxTrackAssigns> m_assignments {};
    std::size_t m_assignmentCount {0};
};

} // namespace dawg::engine
