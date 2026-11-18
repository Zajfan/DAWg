#pragma once

#include <dawg/engine/dsp/HarmonicAssistant.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace dawg::engine::dsp {

enum class ModalInterchangeMode : std::uint8_t
{
    parallelDorian,
    parallelPhrygian,
    parallelLydian,
    parallelMixolydian,
    parallelAeolian,
    parallelLocrian
};

struct BorrowedChord
{
    Chord chord;
    std::string romanNumeral; // e.g. "bVI", "bII", "iv"
    std::string modeOrigin;   // e.g. "Aeolian", "Phrygian"
};

struct CircleKeyInfo
{
    std::uint8_t tonicPitchClass {0}; // 0 = C, 1 = C#, ...
    std::string majorName;
    std::string relativeMinorName;
    int accidentalCount {0}; // positive = sharps, negative = flats
    std::array<Chord, 7> diatonicChords {};
};

struct ChordTrackEvent
{
    std::uint64_t tick {0};
    Chord chord {};
    std::string customLabel;
};

class SongwritingChordAssistantEngine final
{
public:
    SongwritingChordAssistantEngine() noexcept;
    ~SongwritingChordAssistantEngine() = default;

    // =========================================================================
    // Interactive Circle of Fifths & Diatonic / Modal Interchange Matrix
    // =========================================================================

    /// Returns the 12 keys on the circle of fifths starting at C (0: C, 1: G, 2: D, etc.)
    [[nodiscard]] static std::array<CircleKeyInfo, 12> generateCircleOfFifths();

    /// Returns all diatonic chords (I..vii°) for a given root note (0=C..11=B) and scale type
    [[nodiscard]] static std::vector<Chord> getDiatonicChords(std::uint8_t root, bool isMinor = false);

    /// Returns secondary dominants (V/ii, V/iii, V/IV, V/V, V/vi) for the key
    [[nodiscard]] static std::vector<BorrowedChord> getSecondaryDominants(std::uint8_t root);

    /// Returns borrowed chords from parallel modes (Aeolian bVI, Phrygian bII, Mixolydian bVII, etc.)
    [[nodiscard]] static std::vector<BorrowedChord> getModalInterchangeChords(std::uint8_t root);

    // =========================================================================
    // Voice Leading Optimization Engine
    // =========================================================================

    /// Computes the optimal voicing/inversion of targetChord that minimizes
    /// total finger travel distance from currentNotes and retains common tones.
    [[nodiscard]] static std::vector<std::uint8_t> optimizeVoiceLeading(
        const std::vector<std::uint8_t>& currentNotes,
        const Chord& targetChord,
        int targetOctave = 4);

    // =========================================================================
    // Global Master Chord Track
    // =========================================================================
    void clearChordTrack() noexcept;
    void addChordEvent(const ChordTrackEvent& event);
    [[nodiscard]] std::size_t chordEventCount() const noexcept { return m_chordEvents.size(); }
    [[nodiscard]] const std::vector<ChordTrackEvent>& chordEvents() const noexcept { return m_chordEvents; }

    /// Returns the active chord at a specific timeline tick
    [[nodiscard]] Chord chordAtTick(std::uint64_t tick) const noexcept;

private:
    std::vector<ChordTrackEvent> m_chordEvents;
};

} // namespace dawg::engine::dsp
