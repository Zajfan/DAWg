#include <dawg/engine/dsp/SongwritingChordAssistantEngine.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace dawg::engine::dsp {

namespace {

const char* const noteNames[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

} // namespace

SongwritingChordAssistantEngine::SongwritingChordAssistantEngine() noexcept
{
    // Default initial chord track: Cmaj at tick 0
    m_chordEvents.push_back(ChordTrackEvent {
        .tick = 0,
        .chord = Chord {.root = 0, .quality = ChordQuality::major, .bassNote = -1},
        .customLabel = "Cmaj"
    });
}

std::array<CircleKeyInfo, 12> SongwritingChordAssistantEngine::generateCircleOfFifths()
{
    std::array<CircleKeyInfo, 12> circle {};
    // Pitch class progression by fifths (+7 semitones modulo 12):
    // C(0), G(7), D(2), A(9), E(4), B(11), F#(6), Db(1), Ab(8), Eb(3), Bb(10), F(5)
    constexpr std::uint8_t circleRoots[12] = { 0, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10, 5 };
    constexpr int accidentals[12] = { 0, 1, 2, 3, 4, 5, 6, -5, -4, -3, -2, -1 };

    for (std::size_t i = 0; i < 12; ++i)
    {
        const auto root = circleRoots[i];
        circle[i].tonicPitchClass = root;
        circle[i].majorName = std::string(noteNames[root]) + " Maj";
        const auto relMinRoot = static_cast<std::uint8_t>((root + 9) % 12);
        circle[i].relativeMinorName = std::string(noteNames[relMinRoot]) + " Min";
        circle[i].accidentalCount = accidentals[i];

        const auto diatonics = getDiatonicChords(root, false);
        for (std::size_t c = 0; c < 7 && c < diatonics.size(); ++c)
        {
            circle[i].diatonicChords[c] = diatonics[c];
        }
    }

    return circle;
}

std::vector<Chord> SongwritingChordAssistantEngine::getDiatonicChords(const std::uint8_t root, const bool isMinor)
{
    std::vector<Chord> chords;
    chords.reserve(7);

    if (! isMinor)
    {
        // Major scale intervals: 0, 2, 4, 5, 7, 9, 11
        // I (maj7), ii (min7), iii (min7), IV (maj7), V (dom7), vi (min7), vii° (halfDim7)
        chords.push_back(Chord {.root = root, .quality = ChordQuality::maj7, .bassNote = -1});
        chords.push_back(Chord {.root = static_cast<std::uint8_t>((root + 2) % 12), .quality = ChordQuality::min7, .bassNote = -1});
        chords.push_back(Chord {.root = static_cast<std::uint8_t>((root + 4) % 12), .quality = ChordQuality::min7, .bassNote = -1});
        chords.push_back(Chord {.root = static_cast<std::uint8_t>((root + 5) % 12), .quality = ChordQuality::maj7, .bassNote = -1});
        chords.push_back(Chord {.root = static_cast<std::uint8_t>((root + 7) % 12), .quality = ChordQuality::dom7, .bassNote = -1});
        chords.push_back(Chord {.root = static_cast<std::uint8_t>((root + 9) % 12), .quality = ChordQuality::min7, .bassNote = -1});
        chords.push_back(Chord {.root = static_cast<std::uint8_t>((root + 11) % 12), .quality = ChordQuality::halfDim7, .bassNote = -1});
    }
    else
    {
        // Natural minor scale intervals: 0, 2, 3, 5, 7, 8, 10
        // i (min7), ii° (halfDim7), bIII (maj7), iv (min7), v (min7), bVI (maj7), bVII (dom7)
        chords.push_back(Chord {.root = root, .quality = ChordQuality::min7, .bassNote = -1});
        chords.push_back(Chord {.root = static_cast<std::uint8_t>((root + 2) % 12), .quality = ChordQuality::halfDim7, .bassNote = -1});
        chords.push_back(Chord {.root = static_cast<std::uint8_t>((root + 3) % 12), .quality = ChordQuality::maj7, .bassNote = -1});
        chords.push_back(Chord {.root = static_cast<std::uint8_t>((root + 5) % 12), .quality = ChordQuality::min7, .bassNote = -1});
        chords.push_back(Chord {.root = static_cast<std::uint8_t>((root + 7) % 12), .quality = ChordQuality::min7, .bassNote = -1});
        chords.push_back(Chord {.root = static_cast<std::uint8_t>((root + 8) % 12), .quality = ChordQuality::maj7, .bassNote = -1});
        chords.push_back(Chord {.root = static_cast<std::uint8_t>((root + 10) % 12), .quality = ChordQuality::dom7, .bassNote = -1});
    }

    return chords;
}

std::vector<BorrowedChord> SongwritingChordAssistantEngine::getSecondaryDominants(const std::uint8_t root)
{
    std::vector<BorrowedChord> result;
    result.reserve(5);

    // V/ii: Dominant 7th resolving to degree 2 (root + 2 + 7 = root + 9)
    result.push_back(BorrowedChord {
        .chord = Chord {.root = static_cast<std::uint8_t>((root + 9) % 12), .quality = ChordQuality::dom7, .bassNote = -1},
        .romanNumeral = "V7/ii",
        .modeOrigin = "Secondary Dominant"
    });

    // V/iii: Dominant 7th resolving to degree 4 (root + 4 + 7 = root + 11)
    result.push_back(BorrowedChord {
        .chord = Chord {.root = static_cast<std::uint8_t>((root + 11) % 12), .quality = ChordQuality::dom7, .bassNote = -1},
        .romanNumeral = "V7/iii",
        .modeOrigin = "Secondary Dominant"
    });

    // V/IV: Dominant 7th resolving to degree 5 (root + 5 + 7 = root)
    result.push_back(BorrowedChord {
        .chord = Chord {.root = root, .quality = ChordQuality::dom7, .bassNote = -1},
        .romanNumeral = "V7/IV",
        .modeOrigin = "Secondary Dominant"
    });

    // V/V: Dominant 7th resolving to degree 7 (root + 7 + 7 = root + 2)
    result.push_back(BorrowedChord {
        .chord = Chord {.root = static_cast<std::uint8_t>((root + 2) % 12), .quality = ChordQuality::dom7, .bassNote = -1},
        .romanNumeral = "V7/V",
        .modeOrigin = "Secondary Dominant"
    });

    // V/vi: Dominant 7th resolving to degree 9 (root + 9 + 7 = root + 4)
    result.push_back(BorrowedChord {
        .chord = Chord {.root = static_cast<std::uint8_t>((root + 4) % 12), .quality = ChordQuality::dom7, .bassNote = -1},
        .romanNumeral = "V7/vi",
        .modeOrigin = "Secondary Dominant"
    });

    return result;
}

std::vector<BorrowedChord> SongwritingChordAssistantEngine::getModalInterchangeChords(const std::uint8_t root)
{
    std::vector<BorrowedChord> result;
    result.reserve(6);

    // 1. Aeolian bVI (e.g. Abmaj7 in C)
    result.push_back(BorrowedChord {
        .chord = Chord {.root = static_cast<std::uint8_t>((root + 8) % 12), .quality = ChordQuality::maj7, .bassNote = -1},
        .romanNumeral = "bVI",
        .modeOrigin = "Parallel Aeolian"
    });

    // 2. Aeolian iv (e.g. Fm7 in C)
    result.push_back(BorrowedChord {
        .chord = Chord {.root = static_cast<std::uint8_t>((root + 5) % 12), .quality = ChordQuality::min7, .bassNote = -1},
        .romanNumeral = "iv",
        .modeOrigin = "Parallel Aeolian"
    });

    // 3. Mixolydian bVII (e.g. Bbmaj7 in C)
    result.push_back(BorrowedChord {
        .chord = Chord {.root = static_cast<std::uint8_t>((root + 10) % 12), .quality = ChordQuality::maj7, .bassNote = -1},
        .romanNumeral = "bVII",
        .modeOrigin = "Parallel Mixolydian"
    });

    // 4. Phrygian bII (Neapolitan chord, e.g. Dbmaj7 in C)
    result.push_back(BorrowedChord {
        .chord = Chord {.root = static_cast<std::uint8_t>((root + 1) % 12), .quality = ChordQuality::maj7, .bassNote = -1},
        .romanNumeral = "bII",
        .modeOrigin = "Parallel Phrygian"
    });

    // 5. Dorian IV7 (e.g. F7 in C)
    result.push_back(BorrowedChord {
        .chord = Chord {.root = static_cast<std::uint8_t>((root + 5) % 12), .quality = ChordQuality::dom7, .bassNote = -1},
        .romanNumeral = "IV7",
        .modeOrigin = "Parallel Dorian"
    });

    // 6. Lydian II (e.g. Dmaj7 in C)
    result.push_back(BorrowedChord {
        .chord = Chord {.root = static_cast<std::uint8_t>((root + 2) % 12), .quality = ChordQuality::maj7, .bassNote = -1},
        .romanNumeral = "II",
        .modeOrigin = "Parallel Lydian"
    });

    return result;
}

std::vector<std::uint8_t> SongwritingChordAssistantEngine::optimizeVoiceLeading(
    const std::vector<std::uint8_t>& currentNotes,
    const Chord& targetChord,
    const int targetOctave)
{
    const auto pitchClasses = targetChord.pitchClasses();
    if (pitchClasses.empty())
        return {};

    if (currentNotes.empty())
    {
        // Default close root position voicing around targetOctave
        std::vector<std::uint8_t> defaultVoicing;
        defaultVoicing.reserve(pitchClasses.size());
        const int baseMidi = targetOctave * 12;
        for (auto pc : pitchClasses)
        {
            int note = baseMidi + pc;
            if (note < baseMidi) note += 12;
            defaultVoicing.push_back(static_cast<std::uint8_t>(note));
        }
        std::sort(defaultVoicing.begin(), defaultVoicing.end());
        return defaultVoicing;
    }

    // Generate candidate inversions and octave shifts
    const int baseOctave = (targetOctave > 0 && targetOctave < 8) ? targetOctave : 4;
    std::vector<std::vector<std::uint8_t>> candidates;

    for (int octOffset = -1; octOffset <= 1; ++octOffset)
    {
        const int oct = baseOctave + octOffset;
        const int baseMidi = oct * 12;

        std::vector<int> rawNotes;
        rawNotes.reserve(pitchClasses.size());
        for (auto pc : pitchClasses)
        {
            rawNotes.push_back(baseMidi + pc);
        }
        std::sort(rawNotes.begin(), rawNotes.end());

        // Inversions: rotate bottom note up an octave
        for (std::size_t inv = 0; inv < rawNotes.size(); ++inv)
        {
            std::vector<std::uint8_t> candidate;
            candidate.reserve(rawNotes.size());
            for (auto n : rawNotes)
            {
                candidate.push_back(static_cast<std::uint8_t>(std::clamp(n, 0, 127)));
            }
            candidates.push_back(candidate);

            // Invert for next candidate
            rawNotes[0] += 12;
            std::sort(rawNotes.begin(), rawNotes.end());
        }
    }

    // Evaluate voice travel distance and common tones
    int bestScore = std::numeric_limits<int>::max();
    std::size_t bestIdx = 0;

    for (std::size_t c = 0; c < candidates.size(); ++c)
    {
        const auto& cand = candidates[c];
        int distanceCost = 0;
        int commonToneBonus = 0;

        for (std::size_t i = 0; i < std::min(cand.size(), currentNotes.size()); ++i)
        {
            const int dist = std::abs(static_cast<int>(cand[i]) - static_cast<int>(currentNotes[i]));
            distanceCost += dist;
            if (dist == 0)
                commonToneBonus += 15; // Strongly favor keeping common tones in the same voice
        }

        const int totalScore = distanceCost - commonToneBonus;
        if (totalScore < bestScore)
        {
            bestScore = totalScore;
            bestIdx = c;
        }
    }

    return candidates[bestIdx];
}

void SongwritingChordAssistantEngine::clearChordTrack() noexcept
{
    m_chordEvents.clear();
}

void SongwritingChordAssistantEngine::addChordEvent(const ChordTrackEvent& event)
{
    auto it = std::lower_bound(
        m_chordEvents.begin(),
        m_chordEvents.end(),
        event.tick,
        [](const ChordTrackEvent& a, std::uint64_t tick) { return a.tick < tick; });

    if (it != m_chordEvents.end() && it->tick == event.tick)
    {
        *it = event;
    }
    else
    {
        m_chordEvents.insert(it, event);
    }
}

Chord SongwritingChordAssistantEngine::chordAtTick(const std::uint64_t tick) const noexcept
{
    if (m_chordEvents.empty())
        return Chord {.root = 0, .quality = ChordQuality::major, .bassNote = -1};

    Chord active = m_chordEvents.front().chord;
    for (const auto& ev : m_chordEvents)
    {
        if (ev.tick <= tick)
            active = ev.chord;
        else
            break;
    }
    return active;
}

} // namespace dawg::engine::dsp
