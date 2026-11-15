#pragma once

#include <dawg/engine/dsp/AudioTranscriber.h>
#include <dawg/engine/dsp/MusicalScale.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace dawg::engine::dsp {

enum class ChordQuality : std::uint8_t
{
    major,
    minor,
    diminished,
    augmented,
    sus2,
    sus4,
    dom7,
    maj7,
    min7,
    halfDim7,
    dim7,
    add9,
    nine,
    maj9,
    min9
};

struct Chord
{
    std::uint8_t root {0}; // 0 = C, 1 = C#, ... 11 = B
    ChordQuality quality {ChordQuality::major};
    std::int8_t bassNote {-1}; // -1 = root in bass, 0..11 = slash chord (e.g. C/E)

    [[nodiscard]] std::string name() const;
    [[nodiscard]] std::vector<std::uint8_t> pitchClasses() const;
    friend bool operator==(const Chord&, const Chord&) = default;
};

struct TimelineChordEvent
{
    double startBeat {0.0};
    double lengthBeats {4.0};
    Chord chord {};

    friend bool operator==(const TimelineChordEvent&, const TimelineChordEvent&) = default;
};

// Intelligent Chord Track, Voice-Leading Optimizer & Harmonic Assistant (Scaler / Cubase style)
// Provides Roman numeral harmonic analysis, scale lock, smooth voice-leading inversion solver,
// and performance guitar strumming engine.
class HarmonicAssistant final
{
public:
    HarmonicAssistant() = default;
    ~HarmonicAssistant() = default;

    // Timeline Chord Progression Management
    void addChord(double startBeat, double lengthBeats, const Chord& chord);
    void clearChords() noexcept { m_chords.clear(); }
    [[nodiscard]] const std::vector<TimelineChordEvent>& chords() const noexcept { return m_chords; }
    [[nodiscard]] const TimelineChordEvent* chordAtBeat(double beat) const noexcept;

    // Roman Numeral Harmonic Analysis (e.g. "I", "ii", "IV", "V7", "vi", "bVII", "V7/V")
    [[nodiscard]] static std::string getRomanNumeral(const Chord& chord, std::uint8_t keyRoot, MusicalScale scale);

    // Dynamic Harmonic Scale & Chord Snapping for Piano Roll
    // If snapToChordTones is true, quantizes to active chord notes (1, 3, 5, 7)
    // If false, quantizes to parent musical scale
    [[nodiscard]] static std::uint8_t snapNote(
        std::uint8_t midiNote,
        const Chord& activeChord,
        std::uint8_t keyRoot,
        MusicalScale scale,
        bool snapToChordTones = true) noexcept;

    // Smooth Voice-Leading Optimizer
    // Generates 4-part / N-part voicing that minimizes voice travel from previous voicing
    // while preventing awkward leaps and parallel fifths/octaves
    [[nodiscard]] static std::vector<std::uint8_t> optimizeVoiceLeading(
        const Chord& targetChord,
        std::span<const std::uint8_t> previousVoicing,
        std::uint8_t minOctave = 3,
        std::uint8_t maxOctave = 5);

    // Performance Strum & Humanize Engine
    // Transforms a chord voicing into realistic guitar/keyboard strum events
    // with pick direction (down/up), strum speed micro-delays, and velocity contours
    [[nodiscard]] static std::vector<TranscribedNote> generateStrum(
        const std::vector<std::uint8_t>& voicing,
        double startBeat,
        double durationBeats,
        double bpm,
        double sampleRate,
        float strumSpeedMs = 25.0f,
        bool downStroke = true,
        float velocitySlope = 0.15f);

private:
    std::vector<TimelineChordEvent> m_chords;
};

} // namespace dawg::engine::dsp
