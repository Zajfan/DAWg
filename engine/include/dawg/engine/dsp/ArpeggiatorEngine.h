#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dawg::engine::dsp {

enum class ArpPattern : std::uint8_t
{
    up,
    down,
    upDown,
    pingPong,
    converge,
    asPlayed,
    random,
    chord
};

struct ArpStep
{
    std::uint8_t pitch {60};
    double beatOffset {0.0};
    double durationBeats {0.25};
    std::uint8_t velocity {100};
    std::uint8_t ratchetCount {1}; // 1 = normal, 2 = 2x ratchet, 3 = 3x, 4 = 4x
};

class ArpeggiatorEngine final
{
public:
    ArpeggiatorEngine() = default;
    ~ArpeggiatorEngine() = default;

    void setPattern(ArpPattern pattern) noexcept { m_pattern = pattern; }
    [[nodiscard]] ArpPattern pattern() const noexcept { return m_pattern; }

    void setOctaveRange(int octaves) noexcept { m_octaveRange = std::clamp(octaves, 1, 4); }
    [[nodiscard]] int octaveRange() const noexcept { return m_octaveRange; }

    void setGateLength(float gate) noexcept { m_gateLength = std::clamp(gate, 0.05f, 1.0f); }
    [[nodiscard]] float gateLength() const noexcept { return m_gateLength; }

    void setSwing(float swing) noexcept { m_swing = std::clamp(swing, 0.0f, 0.5f); }
    [[nodiscard]] float swing() const noexcept { return m_swing; }

    void setRatcheting(int ratchets) noexcept { m_ratchets = std::clamp(ratchets, 1, 4); }
    [[nodiscard]] int ratcheting() const noexcept { return m_ratchets; }

    /// Generates polyphonic arpeggiated step notes for a set of held MIDI pitches
    [[nodiscard]] std::vector<ArpStep> generateSequence(
        const std::vector<std::uint8_t>& heldNotes,
        std::size_t numSteps,
        double stepDivisionBeats = 0.25) const;

private:
    ArpPattern m_pattern {ArpPattern::up};
    int m_octaveRange {1};
    float m_gateLength {0.8f};
    float m_swing {0.0f};
    int m_ratchets {1};
};

class EuclideanRhythmEngine final
{
public:
    EuclideanRhythmEngine() = delete;

    /// Generates a Euclidean rhythm pattern using the Bjorklund algorithm
    /// steps: total number of steps in loop (e.g. 16)
    /// pulses: number of active hits to distribute evenly (e.g. 5)
    /// rotation: step offset rotation (e.g. 0..steps-1)
    [[nodiscard]] static std::vector<bool> generate(int steps, int pulses, int rotation = 0);
};

} // namespace dawg::engine::dsp
