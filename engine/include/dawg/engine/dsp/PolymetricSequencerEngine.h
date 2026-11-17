#pragma once

#include <dawg/engine/MidiEvent.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace dawg::engine::dsp {

enum class StepClockDivision : std::uint8_t
{
    div_1_32,
    div_1_16,
    div_1_8,
    div_1_8_triplet,
    div_1_4
};

enum class MusicalScaleType : std::uint8_t
{
    chromatic,
    major,
    naturalMinor,
    dorian,
    mixolydian,
    pentatonicMinor,
    pentatonicMajor,
    blues
};

struct SequencerStep
{
    bool active {false};
    std::uint8_t pitch {60};       // MIDI Note Number (Middle C = 60)
    float velocity {0.8f};         // 0.0 to 1.0
    float gateFraction {0.75f};    // Gate length relative to step duration (0.1 to 1.0)
    float probability {1.0f};      // 0.0 to 1.0 trigger chance
    std::uint8_t ratchet {1};      // 1 to 4 burst triggers per step
    float microTimingOffset {0.0f};// Fractional step offset (-0.5 to +0.5)
};

struct PolymetricTrack
{
    static constexpr std::size_t maxSteps = 64;

    bool enabled {true};
    std::uint8_t midiChannel {1}; // 1 - 16
    std::uint8_t stepCount {16};  // Polymetric length (e.g. 16, 7, 11, 5)
    StepClockDivision division {StepClockDivision::div_1_16};

    std::array<SequencerStep, maxSteps> steps {};
    std::size_t currentStep {0};
    double subSampleAccumulator {0.0};
};

// Generative Polymetric MIDI Sequencer Engine (Bitwig / Squarp Hapax Style)
// Features independent track step lengths, Bjorklund Euclidean rhythm generator,
// Markov-chain harmonic pitch generator, ratcheting, swing & humanization.
class PolymetricSequencerEngine final
{
public:
    static constexpr std::size_t maxTracks = 8;

    PolymetricSequencerEngine() noexcept;
    ~PolymetricSequencerEngine() = default;

    void reset() noexcept;

    // Track Configuration
    void setTrackEnabled(std::size_t trackIdx, bool enabled) noexcept;
    void setTrackStepCount(std::size_t trackIdx, std::uint8_t stepCount) noexcept;
    void setTrackDivision(std::size_t trackIdx, StepClockDivision div) noexcept;
    void setTrackMidiChannel(std::size_t trackIdx, std::uint8_t channel) noexcept;
    [[nodiscard]] PolymetricTrack& getTrack(std::size_t trackIdx) noexcept;
    [[nodiscard]] const PolymetricTrack& getTrack(std::size_t trackIdx) const noexcept;

    // Step Manipulation
    void setStep(std::size_t trackIdx, std::size_t stepIdx, const SequencerStep& step) noexcept;

    // Bjorklund Euclidean Rhythm Generation
    // Maximally distributes `hits` over `totalSteps` with optional `rotation`
    void generateEuclidean(
        std::size_t trackIdx,
        std::size_t hits,
        std::size_t totalSteps,
        int rotation = 0,
        std::uint8_t pitch = 60,
        float velocity = 0.85f) noexcept;

    // Markov-Chain Harmonic Progression Generator
    // Generates musically coherent pitch patterns constrained to scale degrees
    void generateMarkovProgression(
        std::size_t trackIdx,
        std::uint8_t rootNote,
        MusicalScaleType scale,
        float randomnessTemperature = 0.5f,
        std::uint32_t seed = 12345) noexcept;

    struct GeneratedChord
    {
        std::uint8_t root {60};
        std::array<std::uint8_t, 4> notes {}; // up to 4 voices (triad + 7th)
        std::size_t voiceCount {3};
        std::string romanNumeral;
    };

    // Generates a sequence of harmonic chord voicings via Markov transition probability matrix
    [[nodiscard]] static std::vector<GeneratedChord> generateMarkovChords(
        std::uint8_t rootNote,
        MusicalScaleType scale,
        std::size_t chordCount = 4,
        float randomnessTemperature = 0.3f,
        std::uint32_t seed = 424242) noexcept;

    // Groove, Swing Templates & Humanization
    enum class SwingTemplate : std::uint8_t
    {
        straight50,
        mpc60_54,
        mpc60_58,
        mpc60_62,
        sp1200_59,
        sp1200_65,
        triplet66
    };

    void applySwingTemplate(SwingTemplate tmpl) noexcept;

    // Groove, Swing & Humanization
    void setSwingPercentage(float swingPercent) noexcept { m_swingPercent = std::clamp(swingPercent, 50.0f, 75.0f); }
    [[nodiscard]] float swingPercentage() const noexcept { return m_swingPercent; }

    void setHumanizeTiming(float amount0To1) noexcept { m_humanizeTiming = std::clamp(amount0To1, 0.0f, 1.0f); }
    [[nodiscard]] float humanizeTiming() const noexcept { return m_humanizeTiming; }

    void setHumanizeVelocity(float amount0To1) noexcept { m_humanizeVelocity = std::clamp(amount0To1, 0.0f, 1.0f); }
    [[nodiscard]] float humanizeVelocity() const noexcept { return m_humanizeVelocity; }

    // Real-Time Block Processing
    // Dispatches generated Note-On and Note-Off events into outEvents buffer
    // Returns number of events produced
    std::size_t processBlock(
        double bpm,
        double sampleRate,
        std::size_t frameCount,
        std::span<MidiEvent> outEvents) noexcept;

private:
    [[nodiscard]] double getStepSamples(StepClockDivision div, double bpm, double sampleRate) const noexcept;
    [[nodiscard]] static bool isScaleDegree(std::uint8_t semitoneFromRoot, MusicalScaleType scale) noexcept;

    std::array<PolymetricTrack, maxTracks> m_tracks {};
    float m_swingPercent {50.0f};       // 50% = straight, 58% = MPC60, 66% = triplet
    float m_humanizeTiming {0.0f};     // 0.0 to 1.0
    float m_humanizeVelocity {0.0f};   // 0.0 to 1.0
    std::uint32_t m_prngState {77777}; // Real-time determinism PRNG
};

} // namespace dawg::engine::dsp
