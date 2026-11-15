#pragma once

#include <dawg/engine/MidiEvent.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dawg::engine::dsp {

enum class StepCondition : std::uint8_t
{
    none,
    pass1of2,
    pass2of2,
    pass1of4,
    pass2of4,
    pass3of4,
    pass4of4,
    firstLoop,
    notFirstLoop,
    fillOnly,
    notFill
};

struct StepData
{
    bool active {false};
    std::uint8_t velocity {100};
    float gateRatio {0.8f};             // Note duration ratio relative to step
    float pitchOffsetSemitones {0.0f};  // P-Lock pitch transposition
    float filterCutoffHz {-1.0f};       // P-Lock filter cutoff (-1 = default)
    float pan {0.0f};                   // P-Lock pan (-1.0 to +1.0)
    float probability {1.0f};           // 0.0 to 1.0 (e.g. 0.5 = 50% trigger chance)
    StepCondition condition {StepCondition::none};
    std::uint8_t ratchets {1};          // 1 = single hit, 2 = 2x, 3 = 3x, 4 = 4x roll
    float microTimingPercent {0.0f};    // -50.0% to +50.0% step nudge / swing
};

struct SequencerTrack
{
    static constexpr std::size_t maxSteps = 64;

    std::size_t stepCount {16};
    std::size_t pulseCount {4};
    std::size_t rotation {0};
    std::uint8_t midiNote {36};
    std::uint8_t midiChannel {0};
    std::size_t currentStep {0};
    std::array<StepData, maxSteps> steps {};
};

// Advanced Step Sequencer & Euclidean Polyrhythm Generator (Elektron / Arturia style)
// Features Bjorklund Euclidean polyrhythm generation, per-step parameter locks (P-Locks),
// probability & cycle conditions (1:2, 4:4, Fill), sub-step ratchets, and micro-timing nudge.
class StepSequencerEngine
{
public:
    static constexpr std::size_t maxTracks = 16;

    StepSequencerEngine() noexcept;
    ~StepSequencerEngine() = default;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    // Track Configuration
    void setTrackMidiNote(std::size_t trackIdx, std::uint8_t note, std::uint8_t channel = 0) noexcept;
    void setTrackStepCount(std::size_t trackIdx, std::size_t stepCount) noexcept;
    [[nodiscard]] std::size_t trackStepCount(std::size_t trackIdx) const noexcept;

    // Step Configuration & Parameter Locks
    void setStep(std::size_t trackIdx, std::size_t stepIdx, const StepData& step) noexcept;
    [[nodiscard]] const StepData& step(std::size_t trackIdx, std::size_t stepIdx) const noexcept;
    void setStepActive(std::size_t trackIdx, std::size_t stepIdx, bool active) noexcept;

    // Euclidean Rhythm Generator (Bjorklund's algorithm)
    static std::vector<bool> generateEuclideanPattern(
        std::size_t steps,
        std::size_t pulses,
        std::size_t rotation = 0);

    void applyEuclideanPattern(
        std::size_t trackIdx,
        std::size_t steps,
        std::size_t pulses,
        std::size_t rotation = 0,
        std::uint8_t velocity = 100);

    // Live Performance Controls
    void setFillMode(bool fillActive) noexcept { m_fillMode = fillActive; }
    [[nodiscard]] bool isFillMode() const noexcept { return m_fillMode; }

    void setPlaybackBpm(double bpm) noexcept { m_bpm = std::clamp(bpm, 20.0, 999.0); }
    [[nodiscard]] double playbackBpm() const noexcept { return m_bpm; }

    // Step Advance & MIDI Generation
    void advance(
        std::size_t sampleFrames,
        double bpm,
        std::vector<MidiEvent>& outputMidi);

    [[nodiscard]] std::size_t currentLoopIteration() const noexcept { return m_loopIteration; }

private:
    [[nodiscard]] bool shouldTriggerStep(const StepData& stepData) const noexcept;

    double m_sampleRate {48000.0};
    double m_bpm {120.0};
    bool m_fillMode {false};
    std::size_t m_loopIteration {0};

    double m_sampleAccumulator {0.0};
    std::array<SequencerTrack, maxTracks> m_tracks {};
};

} // namespace dawg::engine::dsp
