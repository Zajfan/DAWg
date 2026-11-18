#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace dawg::engine {

enum class KeySignature : int8_t
{
    C_Major = 0,    // 0 sharps/flats
    G_Major = 1,    // 1 sharp (F#)
    D_Major = 2,    // 2 sharps (F#, C#)
    A_Major = 3,    // 3 sharps
    E_Major = 4,    // 4 sharps
    B_Major = 5,    // 5 sharps
    F_Sharp_Major = 6,
    F_Major = -1,   // 1 flat (Bb)
    B_Flat_Major = -2, // 2 flats (Bb, Eb)
    E_Flat_Major = -3, // 3 flats
    A_Flat_Major = -4, // 4 flats
    D_Flat_Major = -5, // 5 flats
    G_Flat_Major = -6
};

struct NotationNoteInput
{
    uint8_t pitch {60};        // MIDI pitch 0-127
    double startBeat {0.0};    // Beat position in quarter notes
    double durationBeats {1.0}; // Duration in quarter notes
    uint8_t velocity {100};
    uint8_t channel {0};
};

struct SpelledPitch
{
    char step {'C'};           // 'A' through 'G'
    int8_t alter {0};          // -1 (flat), 0 (natural), 1 (sharp)
    int8_t octave {4};         // Octave number (e.g., 4 for middle C)
};

struct QuantizedScoreNote
{
    SpelledPitch pitch;
    uint32_t startDivision {0};
    uint32_t durationDivisions {0};
    uint8_t voice {1};         // 1 = Soprano/Treble, 2 = Bass
    uint8_t staff {1};         // 1 = Treble Clef, 2 = Bass Clef
    std::string typeName;      // "whole", "half", "quarter", "eighth", "16th", "32nd"
    bool isRest {false};
    bool isTuplet {false};
    uint8_t actualNotes {3};
    uint8_t normalNotes {2};
};

class MusicXmlNotationEngine
{
public:
    static constexpr uint32_t DefaultDivisionsPerQuarter = 24; // Common divisor for 2, 3, 4, 6, 8, 12, 16

    MusicXmlNotationEngine() noexcept = default;
    ~MusicXmlNotationEngine() = default;

    void setKeySignature(KeySignature key) noexcept { m_keySignature = key; }
    [[nodiscard]] KeySignature keySignature() const noexcept { return m_keySignature; }

    void setTimeSignature(uint8_t numerator, uint8_t denominator) noexcept
    {
        m_timeNumerator = numerator > 0 ? numerator : 4;
        m_timeDenominator = denominator > 0 ? denominator : 4;
    }

    [[nodiscard]] uint8_t timeNumerator() const noexcept { return m_timeNumerator; }
    [[nodiscard]] uint8_t timeDenominator() const noexcept { return m_timeDenominator; }

    // Enharmonic Pitch Spelling (Sharp vs Flat based on Key Signature)
    [[nodiscard]] SpelledPitch spellPitch(uint8_t midiNote) const noexcept;

    // Convert raw MIDI note sequence into quantized score notes with voice splitting
    [[nodiscard]] std::vector<QuantizedScoreNote> quantizeNotes(
        const std::vector<NotationNoteInput>& inputNotes,
        double minQuantizeSubdivision = 0.25) const; // 0.25 = 16th note

    // Generate valid MusicXML 4.0 Document string
    [[nodiscard]] std::string exportMusicXml(
        const std::vector<NotationNoteInput>& inputNotes,
        std::string_view pieceTitle = "DAWg Composition",
        std::string_view partName = "Grand Piano") const;

private:
    KeySignature m_keySignature {KeySignature::C_Major};
    uint8_t m_timeNumerator {4};
    uint8_t m_timeDenominator {4};

    [[nodiscard]] std::string durationToTypeName(uint32_t divisions) const noexcept;
};

} // namespace dawg::engine
