#include <dawg/engine/dsp/ChordPadProgressionEngine.h>

#include <algorithm>
#include <array>

namespace dawg::engine::dsp {

namespace {

const char* noteName(const std::uint8_t pitch) noexcept
{
    static constexpr const char* names[12] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    return names[pitch % 12];
}

std::vector<int> getScaleIntervals(const ChordScale scale) noexcept
{
    switch (scale)
    {
    case ChordScale::major:         return {0, 2, 4, 5, 7, 9, 11};
    case ChordScale::naturalMinor:  return {0, 2, 3, 5, 7, 8, 10};
    case ChordScale::dorian:        return {0, 2, 3, 5, 7, 9, 10};
    case ChordScale::phrygian:      return {0, 1, 3, 5, 7, 8, 10};
    case ChordScale::lydian:        return {0, 2, 4, 6, 7, 9, 11};
    case ChordScale::mixolydian:    return {0, 2, 4, 5, 7, 9, 10};
    case ChordScale::harmonicMinor: return {0, 2, 3, 5, 7, 8, 11};
    }
    return {0, 2, 4, 5, 7, 9, 11};
}

} // namespace

std::vector<std::uint8_t> ChordPadProgressionEngine::applyVoicing(
    const std::vector<std::uint8_t>& notes,
    const ChordVoicing voicing)
{
    if (notes.empty())
        return notes;

    std::vector<std::uint8_t> voiced = notes;
    std::sort(voiced.begin(), voiced.end());

    switch (voicing)
    {
    case ChordVoicing::rootPosition:
        break;

    case ChordVoicing::firstInversion:
        if (voiced.size() >= 2)
        {
            voiced[0] = static_cast<std::uint8_t>(std::min(127, voiced[0] + 12));
            std::sort(voiced.begin(), voiced.end());
        }
        break;

    case ChordVoicing::secondInversion:
        if (voiced.size() >= 3)
        {
            voiced[0] = static_cast<std::uint8_t>(std::min(127, voiced[0] + 12));
            voiced[1] = static_cast<std::uint8_t>(std::min(127, voiced[1] + 12));
            std::sort(voiced.begin(), voiced.end());
        }
        break;

    case ChordVoicing::drop2:
        if (voiced.size() >= 4)
        {
            // Drop second note from top down one octave
            const std::size_t dropIdx = voiced.size() - 2;
            if (voiced[dropIdx] >= 12)
            {
                voiced[dropIdx] = static_cast<std::uint8_t>(voiced[dropIdx] - 12);
                std::sort(voiced.begin(), voiced.end());
            }
        }
        break;

    case ChordVoicing::drop3:
        if (voiced.size() >= 4)
        {
            // Drop third note from top down one octave
            const std::size_t dropIdx = voiced.size() - 3;
            if (voiced[dropIdx] >= 12)
            {
                voiced[dropIdx] = static_cast<std::uint8_t>(voiced[dropIdx] - 12);
                std::sort(voiced.begin(), voiced.end());
            }
        }
        break;

    case ChordVoicing::openSpread:
        for (std::size_t i = 1; i < voiced.size(); i += 2)
        {
            if (voiced[i] + 12 <= 127)
                voiced[i] = static_cast<std::uint8_t>(voiced[i] + 12);
        }
        std::sort(voiced.begin(), voiced.end());
        break;
    }

    return voiced;
}

std::vector<ChordPad> ChordPadProgressionEngine::generateProgressionMatrix(
    const std::uint8_t rootKey,
    const ChordScale scale,
    const ChordVoicing voicing)
{
    const auto intervals = getScaleIntervals(scale);
    const std::uint8_t baseRootMidi = static_cast<std::uint8_t>(48 + (rootKey % 12)); // Octave 3

    std::vector<ChordPad> pads;
    pads.reserve(16);

    const bool isMinor = (scale == ChordScale::naturalMinor || scale == ChordScale::harmonicMinor || scale == ChordScale::dorian);

    // Roman numeral labels for rows
    std::array<std::string, 16> romans;
    if (isMinor)
    {
        romans = {
            "i7", "iiø7", "bIIImaj7", "iv7",
            "v7", "bVImaj7", "bVII7", "i(add9)",
            "iiø7", "V7", "i7", "bVImaj7",
            "iv(m9)", "bVII(sus4)", "V7(b9)", "bIImaj7"
        };
    }
    else
    {
        romans = {
            "Imaj7", "ii7", "iii7", "IVmaj7",
            "V7", "vi7", "viiø7", "I(add9)",
            "ii7", "V7", "Imaj7", "vi7",
            "iv6", "bVII7", "V7/IV", "bVImaj7"
        };
    }

    // Generate diatonic 7th chords across 7 degrees
    std::vector<std::vector<std::uint8_t>> degreeChords(7);
    for (std::size_t deg = 0; deg < 7; ++deg)
    {
        const int r = static_cast<int>(baseRootMidi) + intervals[deg];
        const int t3 = static_cast<int>(baseRootMidi) + intervals[(deg + 2) % 7] + (deg + 2 >= 7 ? 12 : 0);
        const int t5 = static_cast<int>(baseRootMidi) + intervals[(deg + 4) % 7] + (deg + 4 >= 7 ? 12 : 0);
        const int t7 = static_cast<int>(baseRootMidi) + intervals[(deg + 6) % 7] + (deg + 6 >= 7 ? 12 : 0);

        degreeChords[deg] = {
            static_cast<std::uint8_t>(r),
            static_cast<std::uint8_t>(t3),
            static_cast<std::uint8_t>(t5),
            static_cast<std::uint8_t>(t7)
        };
    }

    // Populate 16 pads
    // Pads 0..6: Diatonic degrees 1..7
    for (std::size_t i = 0; i < 7; ++i)
    {
        ChordPad pad;
        pad.padIndex = i;
        pad.romanNumeral = romans[i];
        pad.rootMidi = degreeChords[i][0];
        pad.chordName = std::string(noteName(pad.rootMidi)) + (isMinor && (i == 0 || i == 3 || i == 4) ? "m7" : "7");
        pad.midiNotes = applyVoicing(degreeChords[i], voicing);
        pads.push_back(pad);
    }

    // Pad 7: Tonic add9
    {
        ChordPad pad;
        pad.padIndex = 7;
        pad.romanNumeral = romans[7];
        pad.rootMidi = degreeChords[0][0];
        auto notes = degreeChords[0];
        notes.push_back(static_cast<std::uint8_t>(pad.rootMidi + 14)); // 9th
        pad.chordName = std::string(noteName(pad.rootMidi)) + "add9";
        pad.midiNotes = applyVoicing(notes, voicing);
        pads.push_back(pad);
    }

    // Pads 8..11: Standard cadence progression (ii - V - I - vi / iiø - V - i - bVI)
    const std::array<std::size_t, 4> cadenceDegs = { 1, 4, 0, 5 };
    for (std::size_t c = 0; c < 4; ++c)
    {
        const auto deg = cadenceDegs[c];
        ChordPad pad;
        pad.padIndex = 8 + c;
        pad.romanNumeral = romans[8 + c];
        pad.rootMidi = degreeChords[deg][0];
        pad.chordName = std::string(noteName(pad.rootMidi)) + " Cadence";
        pad.midiNotes = applyVoicing(degreeChords[deg], voicing);
        pads.push_back(pad);
    }

    // Pads 12..15: Modal interchange and altered dominants
    // Pad 12: Minor subdominant iv (borrowed in major)
    {
        ChordPad pad;
        pad.padIndex = 12;
        pad.romanNumeral = romans[12];
        pad.rootMidi = static_cast<std::uint8_t>(baseRootMidi + 5); // F
        pad.chordName = std::string(noteName(pad.rootMidi)) + "m6";
        pad.midiNotes = applyVoicing({ pad.rootMidi, static_cast<std::uint8_t>(pad.rootMidi + 3), static_cast<std::uint8_t>(pad.rootMidi + 7), static_cast<std::uint8_t>(pad.rootMidi + 9) }, voicing);
        pads.push_back(pad);
    }
    // Pad 13: Backdoor dominant bVII7
    {
        ChordPad pad;
        pad.padIndex = 13;
        pad.romanNumeral = romans[13];
        pad.rootMidi = static_cast<std::uint8_t>(baseRootMidi + 10); // Bb
        pad.chordName = std::string(noteName(pad.rootMidi)) + "7";
        pad.midiNotes = applyVoicing({ pad.rootMidi, static_cast<std::uint8_t>(pad.rootMidi + 4), static_cast<std::uint8_t>(pad.rootMidi + 7), static_cast<std::uint8_t>(pad.rootMidi + 10) }, voicing);
        pads.push_back(pad);
    }
    // Pad 14: Secondary dominant V7/IV
    {
        ChordPad pad;
        pad.padIndex = 14;
        pad.romanNumeral = romans[14];
        pad.rootMidi = baseRootMidi; // C7
        pad.chordName = std::string(noteName(pad.rootMidi)) + "7";
        pad.midiNotes = applyVoicing({ pad.rootMidi, static_cast<std::uint8_t>(pad.rootMidi + 4), static_cast<std::uint8_t>(pad.rootMidi + 7), static_cast<std::uint8_t>(pad.rootMidi + 10) }, voicing);
        pads.push_back(pad);
    }
    // Pad 15: Neapolitan / bVImaj7
    {
        ChordPad pad;
        pad.padIndex = 15;
        pad.romanNumeral = romans[15];
        pad.rootMidi = static_cast<std::uint8_t>(baseRootMidi + 8); // Ab
        pad.chordName = std::string(noteName(pad.rootMidi)) + "maj7";
        pad.midiNotes = applyVoicing({ pad.rootMidi, static_cast<std::uint8_t>(pad.rootMidi + 4), static_cast<std::uint8_t>(pad.rootMidi + 7), static_cast<std::uint8_t>(pad.rootMidi + 11) }, voicing);
        pads.push_back(pad);
    }

    return pads;
}

std::vector<StrummedNote> ChordPadProgressionEngine::generateStrum(
    const std::vector<std::uint8_t>& notes,
    const double strumDurationMs,
    const bool upStrum,
    const std::uint8_t baseVelocity)
{
    if (notes.empty())
        return {};

    std::vector<std::uint8_t> sorted = notes;
    std::sort(sorted.begin(), sorted.end());
    if (upStrum)
        std::reverse(sorted.begin(), sorted.end());

    std::vector<StrummedNote> strummed;
    strummed.reserve(sorted.size());

    const double stepMs = (sorted.size() > 1) ? (strumDurationMs / static_cast<double>(sorted.size() - 1)) : 0.0;

    for (std::size_t i = 0; i < sorted.size(); ++i)
    {
        // Slight velocity falloff across the strum
        const float falloff = 1.0f - static_cast<float>(i) * 0.04f;
        const auto vel = static_cast<std::uint8_t>(std::clamp(static_cast<float>(baseVelocity) * falloff, 30.0f, 127.0f));

        strummed.push_back(StrummedNote {
            .pitch = sorted[i],
            .timeOffsetMs = static_cast<double>(i) * stepMs,
            .velocity = vel
        });
    }

    return strummed;
}

} // namespace dawg::engine::dsp
