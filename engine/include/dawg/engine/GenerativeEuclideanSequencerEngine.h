#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <random>
#include <span>
#include <vector>

#include "dawg/engine/MidiEvent.h"
#include "dawg/engine/dsp/MusicalScale.h"

namespace dawg::engine {

struct EuclideanLaneConfig
{
    uint8_t note {60};            // Middle C
    uint8_t channel {0};
    std::size_t steps {16};       // N
    std::size_t pulses {4};       // K
    std::size_t rotation {0};     // Step rotation / offset
    float velocity {0.8f};        // 0.0 to 1.0
    float probability {1.0f};     // 0.0 to 1.0 trigger probability
    uint8_t ratchet {1};          // 1, 2, 3, or 4 sub-divisions
    float gateLength {0.75f};     // Fraction of step duration
    bool active {true};
};

class GenerativeEuclideanSequencerEngine
{
public:
    static constexpr std::size_t MaxLanes = 8;
    static constexpr std::size_t MaxStepsPerLane = 64;

    struct LaneState
    {
        EuclideanLaneConfig config {};
        std::array<bool, MaxStepsPerLane> pattern {};
        std::size_t currentStep {0};
        double stepSampleAccumulator {0.0};
        int noteOffCountdownSamples {0};
        uint8_t activeNote {0};
        bool isNoteActive {false};
        bool firstStepPending {true};
    };

    GenerativeEuclideanSequencerEngine() noexcept;
    ~GenerativeEuclideanSequencerEngine() = default;

    // Bjorklund's algorithm E(K, N)
    [[nodiscard]] static std::vector<bool> computeBjorklundPattern(std::size_t pulses, std::size_t steps) noexcept;

    void configureLane(std::size_t laneIndex, const EuclideanLaneConfig& config) noexcept;
    [[nodiscard]] const LaneState& lane(std::size_t laneIndex) const noexcept;
    [[nodiscard]] std::size_t activeLaneCount() const noexcept { return MaxLanes; }

    void reset() noexcept;

    // Advance sequencer clock by numSamples and emit MIDI notes (strictly RT-safe, zero allocation)
    // Returns number of MIDI events written into outEvents
    std::size_t process(
        std::size_t numSamples,
        double tempoBpm,
        double sampleRate,
        std::span<MidiEvent> outEvents) noexcept;

    // Markov-Chain Harmonic Progression Assistant
    void setRootNote(uint8_t root) noexcept { m_rootNote = root % 12; }
    [[nodiscard]] uint8_t rootNote() const noexcept { return m_rootNote; }

    void setScale(dsp::MusicalScale scale) noexcept { m_scale = scale; }
    [[nodiscard]] dsp::MusicalScale scale() const noexcept { return m_scale; }

    [[nodiscard]] uint8_t nextMarkovChordDegree(uint8_t currentDegree) noexcept;
    [[nodiscard]] uint8_t quantizeToScale(uint8_t midiNote) const noexcept;

private:
    std::array<LaneState, MaxLanes> m_lanes {};
    uint8_t m_rootNote {0}; // 0 = C
    dsp::MusicalScale m_scale {dsp::MusicalScale::major};
    uint32_t m_prngState {123456789};

    // Fast xorshift32 PRNG (real-time safe, non-blocking)
    [[nodiscard]] float randomFloat() noexcept;
    void refreshLanePattern(std::size_t laneIndex) noexcept;
};

} // namespace dawg::engine
