#include "dawg/engine/MusicXmlNotationEngine.h"

#include <cmath>
#include <map>
#include <sstream>

namespace dawg::engine {

SpelledPitch MusicXmlNotationEngine::spellPitch(uint8_t midiNote) const noexcept
{
    const int noteClass = midiNote % 12;
    const int oct = (static_cast<int>(midiNote) / 12) - 1;

    SpelledPitch sp {};
    sp.octave = static_cast<int8_t>(oct);

    const bool useFlats = static_cast<int8_t>(m_keySignature) < 0;

    if (!useFlats) {
        // Sharp / Natural spelling (C, G, D, A, E, B, F# Major)
        static constexpr std::array<std::pair<char, int8_t>, 12> SharpTable {{
            {'C', 0},  {'C', 1},  {'D', 0},  {'D', 1},
            {'E', 0},  {'F', 0},  {'F', 1},  {'G', 0},
            {'G', 1},  {'A', 0},  {'A', 1},  {'B', 0}
        }};
        sp.step = SharpTable[noteClass].first;
        sp.alter = SharpTable[noteClass].second;
    } else {
        // Flat / Natural spelling (F, Bb, Eb, Ab, Db, Gb Major)
        static constexpr std::array<std::pair<char, int8_t>, 12> FlatTable {{
            {'C', 0},  {'D', -1}, {'D', 0},  {'E', -1},
            {'E', 0},  {'F', 0},  {'G', -1}, {'G', 0},
            {'A', -1}, {'A', 0},  {'B', -1}, {'B', 0}
        }};
        sp.step = FlatTable[noteClass].first;
        sp.alter = FlatTable[noteClass].second;
    }
    return sp;
}

std::string MusicXmlNotationEngine::durationToTypeName(uint32_t divisions) const noexcept
{
    // Divisions based on DefaultDivisionsPerQuarter = 24
    if (divisions >= 96) return "whole";
    if (divisions >= 48) return "half";
    if (divisions >= 24) return "quarter";
    if (divisions >= 12) return "eighth";
    if (divisions >= 6)  return "16th";
    return "32nd";
}

std::vector<QuantizedScoreNote> MusicXmlNotationEngine::quantizeNotes(
    const std::vector<NotationNoteInput>& inputNotes,
    double minQuantizeSubdivision) const
{
    std::vector<QuantizedScoreNote> score;
    score.reserve(inputNotes.size());

    const double quantStep = minQuantizeSubdivision > 0.0 ? minQuantizeSubdivision : 0.25;

    for (const auto& inNote : inputNotes) {
        QuantizedScoreNote qNote {};
        qNote.pitch = spellPitch(inNote.pitch);

        // Snap start beat and duration
        const double snappedStart = std::round(inNote.startBeat / quantStep) * quantStep;
        const double rawDur = std::max(quantStep, inNote.durationBeats);
        const double snappedDur = std::round(rawDur / quantStep) * quantStep;

        qNote.startDivision = static_cast<uint32_t>(std::round(snappedStart * DefaultDivisionsPerQuarter));
        qNote.durationDivisions = static_cast<uint32_t>(std::round(snappedDur * DefaultDivisionsPerQuarter));

        // Detect triplet tuplet (approximately 1/3 or 2/3 of a beat)
        const double tripletRemainder = std::abs(snappedDur - (1.0 / 3.0));
        if (tripletRemainder < 0.05) {
            qNote.isTuplet = true;
            qNote.actualNotes = 3;
            qNote.normalNotes = 2;
        }

        // Automatic Voice & Staff Splitting: Middle C (60) cutoff
        if (inNote.pitch >= 60) {
            qNote.voice = 1;
            qNote.staff = 1; // Treble stave
        } else {
            qNote.voice = 2;
            qNote.staff = 2; // Bass stave
        }

        qNote.typeName = durationToTypeName(qNote.durationDivisions);
        score.push_back(qNote);
    }

    std::sort(score.begin(), score.end(), [](const auto& a, const auto& b) {
        if (a.startDivision != b.startDivision) {
            return a.startDivision < b.startDivision;
        }
        return a.staff < b.staff;
    });

    return score;
}

std::string MusicXmlNotationEngine::exportMusicXml(
    const std::vector<NotationNoteInput>& inputNotes,
    std::string_view pieceTitle,
    std::string_view partName) const
{
    const auto quantized = quantizeNotes(inputNotes);

    const double beatsPerMeasure = static_cast<double>(m_timeNumerator) * (4.0 / static_cast<double>(m_timeDenominator));
    const auto divPerMeasure = static_cast<uint32_t>(std::round(beatsPerMeasure * DefaultDivisionsPerQuarter));

    // Group notes into measures
    std::map<uint32_t, std::vector<QuantizedScoreNote>> measureMap;
    for (const auto& note : quantized) {
        const uint32_t measureIdx = (note.startDivision / divPerMeasure) + 1;
        measureMap[measureIdx].push_back(note);
    }
    if (measureMap.empty()) {
        measureMap[1] = {};
    }

    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    xml << "<!DOCTYPE score-partwise PUBLIC \"-//Recordare//DTD MusicXML 4.0 Partwise//EN\" \"http://www.musicxml.org/dtds/partwise.dtd\">\n";
    xml << "<score-partwise version=\"4.0\">\n";
    xml << "  <work>\n";
    xml << "    <work-title>" << pieceTitle << "</work-title>\n";
    xml << "  </work>\n";
    xml << "  <part-list>\n";
    xml << "    <score-part id=\"P1\">\n";
    xml << "      <part-name>" << partName << "</part-name>\n";
    xml << "    </score-part>\n";
    xml << "  </part-list>\n";
    xml << "  <part id=\"P1\">\n";

    for (const auto& [measureNum, notes] : measureMap) {
        xml << "    <measure number=\"" << measureNum << "\">\n";

        if (measureNum == 1) {
            xml << "      <attributes>\n";
            xml << "        <divisions>" << DefaultDivisionsPerQuarter << "</divisions>\n";
            xml << "        <key>\n";
            xml << "          <fifths>" << static_cast<int>(m_keySignature) << "</fifths>\n";
            xml << "        </key>\n";
            xml << "        <time>\n";
            xml << "          <beats>" << static_cast<int>(m_timeNumerator) << "</beats>\n";
            xml << "          <beat-type>" << static_cast<int>(m_timeDenominator) << "</beat-type>\n";
            xml << "        </time>\n";
            xml << "        <staves>2</staves>\n";
            xml << "        <clef number=\"1\">\n";
            xml << "          <sign>G</sign>\n";
            xml << "          <line>2</line>\n";
            xml << "        </clef>\n";
            xml << "        <clef number=\"2\">\n";
            xml << "          <sign>F</sign>\n";
            xml << "          <line>4</line>\n";
            xml << "        </clef>\n";
            xml << "      </attributes>\n";
        }

        for (const auto& n : notes) {
            xml << "      <note>\n";
            xml << "        <pitch>\n";
            xml << "          <step>" << n.pitch.step << "</step>\n";
            if (n.pitch.alter != 0) {
                xml << "          <alter>" << static_cast<int>(n.pitch.alter) << "</alter>\n";
            }
            xml << "          <octave>" << static_cast<int>(n.pitch.octave) << "</octave>\n";
            xml << "        </pitch>\n";
            xml << "        <duration>" << n.durationDivisions << "</duration>\n";
            xml << "        <voice>" << static_cast<int>(n.voice) << "</voice>\n";
            xml << "        <type>" << n.typeName << "</type>\n";
            xml << "        <staff>" << static_cast<int>(n.staff) << "</staff>\n";

            if (n.isTuplet) {
                xml << "        <time-modification>\n";
                xml << "          <actual-notes>" << static_cast<int>(n.actualNotes) << "</actual-notes>\n";
                xml << "          <normal-notes>" << static_cast<int>(n.normalNotes) << "</normal-notes>\n";
                xml << "        </time-modification>\n";
            }
            xml << "      </note>\n";
        }

        xml << "    </measure>\n";
    }

    xml << "  </part>\n";
    xml << "</score-partwise>\n";

    return xml.str();
}

} // namespace dawg::engine
