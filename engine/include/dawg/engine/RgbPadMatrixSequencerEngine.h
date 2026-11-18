#pragma once

#include <dawg/engine/MidiEvent.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace dawg::engine {

enum class ControllerGridType : std::uint8_t
{
    launchpadProX = 0,
    push2,
    generic8x8
};

enum class GridPadViewMode : std::uint8_t
{
    sessionClips = 0,
    inKeyMelody,
    chromaticMelody,
    stepSequencer
};

enum class MelodicScaleType : std::uint8_t
{
    major = 0,
    naturalMinor,
    dorian,
    phrygian,
    lydian,
    mixolydian,
    harmonicMinor,
    pentatonicMajor,
    pentatonicMinor,
    blues
};

enum class PadLedState : std::uint8_t
{
    off = 0,
    dimColor,
    brightColor,
    playingPulse,
    queuedFlash,
    recordingRed,
    mutedAmber
};

struct HardwareStep
{
    bool active {false};
    std::uint8_t noteNumber {60}; // Middle C
    std::uint8_t velocity {100};
    float probability {1.0f};     // 0.0 to 1.0
    std::uint8_t ratchetCount {1}; // 1 = normal, 2, 3, 4 = rolls
};

struct PadEventResult
{
    bool handled {false};
    GridPadViewMode viewMode {GridPadViewMode::sessionClips};
    int padRow {-1};
    int padCol {-1};
    int noteNumber {-1};
    float velocity {0.0f};
    bool isNoteOn {false};
    bool isNoteOff {false};
    std::size_t launchedClipTrack {0};
    std::size_t launchedClipScene {0};
};

class RgbPadMatrixSequencerEngine
{
public:
    static constexpr std::size_t matrixRows = 8;
    static constexpr std::size_t matrixCols = 8;
    static constexpr std::size_t totalPads = matrixRows * matrixCols;
    static constexpr std::size_t maxSteps = 32;

    RgbPadMatrixSequencerEngine() noexcept;
    ~RgbPadMatrixSequencerEngine() = default;

    void reset() noexcept;

    // Controller & View Configuration
    void setControllerType(ControllerGridType type) noexcept { m_controllerType = type; }
    [[nodiscard]] ControllerGridType controllerType() const noexcept { return m_controllerType; }
    void setViewMode(GridPadViewMode mode) noexcept { m_viewMode = mode; }
    [[nodiscard]] GridPadViewMode viewMode() const noexcept { return m_viewMode; }

    // Melodic In-Key Configuration
    void setRootNote(int rootMidiNote) noexcept { m_rootNote = (rootMidiNote % 12 + 12) % 12; }
    [[nodiscard]] int rootNote() const noexcept { return m_rootNote; }
    void setScaleType(MelodicScaleType scale) noexcept { m_scaleType = scale; }
    [[nodiscard]] MelodicScaleType scaleType() const noexcept { return m_scaleType; }
    void setBaseOctave(int octave) noexcept { m_baseOctave = std::clamp(octave, 0, 8); }
    [[nodiscard]] int baseOctave() const noexcept { return m_baseOctave; }

    // Step Sequencer Parameters
    void setStepSequencerLength(std::size_t steps) noexcept;
    [[nodiscard]] std::size_t stepSequencerLength() const noexcept { return m_stepSequencerLength; }
    void setStep(std::size_t index, const HardwareStep& step) noexcept;
    [[nodiscard]] const HardwareStep& step(std::size_t index) const noexcept;
    void setStepPlayhead(std::size_t stepIndex) noexcept { m_currentPlayheadStep = stepIndex % m_stepSequencerLength; }
    [[nodiscard]] std::size_t stepPlayhead() const noexcept { return m_currentPlayheadStep; }

    // Pad State & Color Control (Session Mode)
    void setPadState(std::size_t row, std::size_t col, PadLedState state, std::uint8_t baseVelocityColor = 21) noexcept;
    [[nodiscard]] PadLedState padState(std::size_t row, std::size_t col) const noexcept;

    // Bidirectional MIDI Processing
    [[nodiscard]] PadEventResult processMidiIn(const MidiEvent& event) noexcept;
    std::size_t renderLedUpdates(MidiEvent* outEvents, std::size_t maxOutEvents, double timeSeconds, double beatPosition) noexcept;

    // Helper: Map Pad Coordinates to Hardware MIDI Note
    [[nodiscard]] std::uint8_t padToMidiNote(std::size_t row, std::size_t col) const noexcept;
    [[nodiscard]] bool midiNoteToPad(std::uint8_t note, std::size_t& row, std::size_t& col) const noexcept;

    // Helper: Scale Note Calculation
    [[nodiscard]] int padToMelodicNote(std::size_t row, std::size_t col) const noexcept;
    [[nodiscard]] bool isNoteInScale(int midiNote) const noexcept;
    [[nodiscard]] bool isRootNote(int midiNote) const noexcept;

private:
    [[nodiscard]] std::uint8_t computeLedVelocity(std::size_t row, std::size_t col, double timeSeconds, double beatPosition) const noexcept;

    ControllerGridType m_controllerType {ControllerGridType::launchpadProX};
    GridPadViewMode m_viewMode {GridPadViewMode::sessionClips};

    int m_rootNote {0}; // 0 = C
    MelodicScaleType m_scaleType {MelodicScaleType::naturalMinor};
    int m_baseOctave {3};

    std::size_t m_stepSequencerLength {16};
    std::size_t m_currentPlayheadStep {0};
    std::array<HardwareStep, maxSteps> m_steps {};

    std::array<std::array<PadLedState, matrixCols>, matrixRows> m_padStates {};
    std::array<std::array<std::uint8_t, matrixCols>, matrixRows> m_padBaseColors {};
    std::array<std::array<std::uint8_t, matrixCols>, matrixRows> m_lastSentVelocity {};
};

} // namespace dawg::engine
