#include <dawg/engine/dsp/HarmonicAssistant.h>

#include <cmath>
#include <limits>
#include <numeric>

namespace dawg::engine::dsp {
namespace {

const char* const noteNames[] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

} // namespace

std::string Chord::name() const
{
    std::string s = noteNames[root % 12];
    switch (quality)
    {
    case ChordQuality::major: break;
    case ChordQuality::minor: s += "m"; break;
    case ChordQuality::diminished: s += "dim"; break;
    case ChordQuality::augmented: s += "aug"; break;
    case ChordQuality::sus2: s += "sus2"; break;
    case ChordQuality::sus4: s += "sus4"; break;
    case ChordQuality::dom7: s += "7"; break;
    case ChordQuality::maj7: s += "maj7"; break;
    case ChordQuality::min7: s += "m7"; break;
    case ChordQuality::halfDim7: s += "m7b5"; break;
    case ChordQuality::dim7: s += "dim7"; break;
    case ChordQuality::add9: s += "add9"; break;
    case ChordQuality::nine: s += "9"; break;
    case ChordQuality::maj9: s += "maj9"; break;
    case ChordQuality::min9: s += "m9"; break;
    }

    if (bassNote >= 0 && (bassNote % 12) != (root % 12))
    {
        s += "/";
        s += noteNames[bassNote % 12];
    }
    return s;
}

std::vector<std::uint8_t> Chord::pitchClasses() const
{
    std::vector<std::uint8_t> intervals;
    switch (quality)
    {
    case ChordQuality::major: intervals = {0, 4, 7}; break;
    case ChordQuality::minor: intervals = {0, 3, 7}; break;
    case ChordQuality::diminished: intervals = {0, 3, 6}; break;
    case ChordQuality::augmented: intervals = {0, 4, 8}; break;
    case ChordQuality::sus2: intervals = {0, 2, 7}; break;
    case ChordQuality::sus4: intervals = {0, 5, 7}; break;
    case ChordQuality::dom7: intervals = {0, 4, 7, 10}; break;
    case ChordQuality::maj7: intervals = {0, 4, 7, 11}; break;
    case ChordQuality::min7: intervals = {0, 3, 7, 10}; break;
    case ChordQuality::halfDim7: intervals = {0, 3, 6, 10}; break;
    case ChordQuality::dim7: intervals = {0, 3, 6, 9}; break;
    case ChordQuality::add9: intervals = {0, 4, 7, 2}; break;
    case ChordQuality::nine: intervals = {0, 4, 7, 10, 2}; break;
    case ChordQuality::maj9: intervals = {0, 4, 7, 11, 2}; break;
    case ChordQuality::min9: intervals = {0, 3, 7, 10, 2}; break;
    }

    std::vector<std::uint8_t> pcs;
    pcs.reserve(intervals.size() + (bassNote >= 0 ? 1 : 0));

    if (bassNote >= 0)
    {
        pcs.push_back(static_cast<std::uint8_t>(bassNote % 12));
    }

    for (const auto semitone : intervals)
    {
        const auto pc = static_cast<std::uint8_t>((root + semitone) % 12);
        if (std::find(pcs.begin(), pcs.end(), pc) == pcs.end())
        {
            pcs.push_back(pc);
        }
    }
    return pcs;
}

void HarmonicAssistant::addChord(const double startBeat, const double lengthBeats, const Chord& chord)
{
    m_chords.push_back(TimelineChordEvent {
        .startBeat = startBeat,
        .lengthBeats = lengthBeats,
        .chord = chord
    });

    std::sort(m_chords.begin(), m_chords.end(), [](const TimelineChordEvent& a, const TimelineChordEvent& b) {
        return a.startBeat < b.startBeat;
    });
}

const TimelineChordEvent* HarmonicAssistant::chordAtBeat(const double beat) const noexcept
{
    for (const auto& ev : m_chords)
    {
        if (beat >= ev.startBeat && beat < (ev.startBeat + ev.lengthBeats))
        {
            return &ev;
        }
    }
    return nullptr;
}

std::string HarmonicAssistant::getRomanNumeral(const Chord& chord, const std::uint8_t keyRoot, [[maybe_unused]] const MusicalScale scale)
{
    const int diff = (static_cast<int>(chord.root) - static_cast<int>(keyRoot) + 12) % 12;
    const bool isMinorLike = (chord.quality == ChordQuality::minor || chord.quality == ChordQuality::min7 || chord.quality == ChordQuality::min9);
    const bool isDim = (chord.quality == ChordQuality::diminished || chord.quality == ChordQuality::halfDim7 || chord.quality == ChordQuality::dim7);

    std::string numeral;
    switch (diff)
    {
    case 0:  numeral = isMinorLike ? "i" : "I"; break;
    case 1:  numeral = "bII"; break; // Neapolitan
    case 2:  numeral = isMinorLike ? "ii" : "II"; break;
    case 3:  numeral = isMinorLike ? "biii" : "bIII"; break;
    case 4:  numeral = isMinorLike ? "iii" : "III"; break;
    case 5:  numeral = isMinorLike ? "iv" : "IV"; break;
    case 6:  numeral = isDim ? "bV" : "#IV"; break;
    case 7:  numeral = isMinorLike ? "v" : "V"; break;
    case 8:  numeral = isMinorLike ? "bvi" : "bVI"; break;
    case 9:  numeral = isMinorLike ? "vi" : "VI"; break;
    case 10: numeral = isMinorLike ? "bvii" : "bVII"; break;
    case 11: numeral = isDim ? "vii°" : "VII"; break;
    }

    if (chord.quality == ChordQuality::dom7 || chord.quality == ChordQuality::nine)
    {
        numeral += "7";
    }
    else if (chord.quality == ChordQuality::maj7 || chord.quality == ChordQuality::maj9)
    {
        numeral += "maj7";
    }
    else if (chord.quality == ChordQuality::min7 || chord.quality == ChordQuality::min9)
    {
        numeral += "7";
    }
    else if (chord.quality == ChordQuality::halfDim7)
    {
        numeral += "ø7";
    }

    return numeral;
}

std::uint8_t HarmonicAssistant::snapNote(
    const std::uint8_t midiNote,
    const Chord& activeChord,
    const std::uint8_t keyRoot,
    [[maybe_unused]] const MusicalScale scale,
    const bool snapToChordTones) noexcept
{
    const auto pcs = activeChord.pitchClasses();
    if (pcs.empty())
    {
        return midiNote;
    }

    const auto currentPc = static_cast<std::uint8_t>(midiNote % 12);

    if (snapToChordTones)
    {
        // Find closest chord pitch class
        int bestDist = 100;
        int bestOffset = 0;
        for (const auto pc : pcs)
        {
            int dist = static_cast<int>(pc) - static_cast<int>(currentPc);
            if (dist > 6) dist -= 12;
            if (dist < -6) dist += 12;

            if (std::abs(dist) < std::abs(bestDist))
            {
                bestDist = dist;
                bestOffset = dist;
            }
        }
        return static_cast<std::uint8_t>(std::clamp(static_cast<int>(midiNote) + bestOffset, 0, 127));
    }

    // Otherwise snap to major / minor scale tones
    const std::array<int, 7> scaleIntervals = {0, 2, 4, 5, 7, 9, 11}; // Major scale
    int bestDist = 100;
    int bestOffset = 0;
    for (const auto iv : scaleIntervals)
    {
        const auto pc = static_cast<std::uint8_t>((keyRoot + iv) % 12);
        int dist = static_cast<int>(pc) - static_cast<int>(currentPc);
        if (dist > 6) dist -= 12;
        if (dist < -6) dist += 12;

        if (std::abs(dist) < std::abs(bestDist))
        {
            bestDist = dist;
            bestOffset = dist;
        }
    }
    return static_cast<std::uint8_t>(std::clamp(static_cast<int>(midiNote) + bestOffset, 0, 127));
}

std::vector<std::uint8_t> HarmonicAssistant::optimizeVoiceLeading(
    const Chord& targetChord,
    const std::span<const std::uint8_t> previousVoicing,
    const std::uint8_t minOctave,
    const std::uint8_t maxOctave)
{
    const auto pcs = targetChord.pitchClasses();
    if (pcs.empty())
    {
        return {};
    }

    // Default 4-part voicing in root position if no previous voicing
    if (previousVoicing.empty())
    {
        std::vector<std::uint8_t> voicing;
        // Bass note in octave 3
        const auto bassPc = (targetChord.bassNote >= 0) ? targetChord.bassNote : targetChord.root;
        voicing.push_back(static_cast<std::uint8_t>(12 * minOctave + (bassPc % 12)));

        // Upper voices in octave minOctave + 1
        for (const auto pc : pcs)
        {
            voicing.push_back(static_cast<std::uint8_t>(12 * (minOctave + 1) + pc));
            if (voicing.size() >= 4) break;
        }

        std::sort(voicing.begin(), voicing.end());
        return voicing;
    }

    // Optimize each voice to minimize leap from previous voice position
    std::vector<std::uint8_t> bestVoicing;
    bestVoicing.reserve(previousVoicing.size());

    for (std::size_t v = 0; v < previousVoicing.size(); ++v)
    {
        const auto prevPitch = previousVoicing[v];
        int bestPitch = prevPitch;
        int minTravel = std::numeric_limits<int>::max();

        // Search within [minOctave, maxOctave] for closest matching pitch class
        for (int oct = minOctave; oct <= maxOctave; ++oct)
        {
            for (const auto pc : pcs)
            {
                const int candidate = oct * 12 + pc;
                const int travel = std::abs(candidate - static_cast<int>(prevPitch));
                if (travel < minTravel)
                {
                    minTravel = travel;
                    bestPitch = candidate;
                }
            }
        }
        bestVoicing.push_back(static_cast<std::uint8_t>(std::clamp(bestPitch, 0, 127)));
    }

    // Keep voices sorted ascending to avoid voice-crossing
    std::sort(bestVoicing.begin(), bestVoicing.end());
    return bestVoicing;
}

std::vector<TranscribedNote> HarmonicAssistant::generateStrum(
    const std::vector<std::uint8_t>& voicing,
    const double startBeat,
    const double durationBeats,
    const double bpm,
    const double sampleRate,
    const float strumSpeedMs,
    const bool downStroke,
    const float velocitySlope)
{
    if (voicing.empty() || bpm <= 0.0 || sampleRate <= 0.0)
    {
        return {};
    }

    std::vector<TranscribedNote> notes;
    notes.reserve(voicing.size());

    const auto numStrings = voicing.size();
    const double secPerBeat = 60.0 / bpm;
    const auto baseStartSample = static_cast<std::uint64_t>(startBeat * secPerBeat * sampleRate);
    const auto totalLengthSamples = static_cast<std::uint64_t>(durationBeats * secPerBeat * sampleRate);
    const auto delayPerStringSamples = static_cast<std::uint64_t>(strumSpeedMs * 0.001f * static_cast<float>(sampleRate));

    for (std::size_t i = 0; i < numStrings; ++i)
    {
        // Downstroke: lowest string first; Upstroke: highest string first
        const auto stringIdx = downStroke ? i : (numStrings - 1 - i);
        const auto startOffset = static_cast<std::uint64_t>(i) * delayPerStringSamples;
        const auto noteStart = baseStartSample + startOffset;
        const auto noteLen = (totalLengthSamples > startOffset) ? (totalLengthSamples - startOffset) : totalLengthSamples;

        // Velocity contour: pick dynamics across strings
        const float velNorm = 0.75f + static_cast<float>(i) * velocitySlope;
        const auto vel = static_cast<std::uint8_t>(std::clamp(std::round(velNorm * 127.0f), 40.0f, 127.0f));

        notes.push_back(TranscribedNote {
            .pitch = voicing[stringIdx],
            .velocity = vel,
            .startSample = noteStart,
            .lengthSamples = noteLen,
            .drumType = DrumHitType::kick,
            .confidence = 1.0f
        });
    }

    return notes;
}

} // namespace dawg::engine::dsp
