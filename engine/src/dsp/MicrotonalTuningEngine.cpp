#include <dawg/engine/dsp/MicrotonalTuningEngine.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace dawg::engine::dsp {

namespace {

double ratioToCents(double numerator, double denominator) noexcept
{
    if (numerator <= 0.0 || denominator <= 0.0)
    {
        return 0.0;
    }
    return 1200.0 * (std::log(numerator / denominator) / std::log(2.0));
}

std::string trim(std::string_view sv)
{
    const auto first = sv.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos)
    {
        return {};
    }
    const auto last = sv.find_last_not_of(" \t\r\n");
    return std::string(sv.substr(first, (last - first + 1)));
}

} // namespace

MicrotonalTuningEngine::MicrotonalTuningEngine() noexcept
{
    loadFactoryScale(FactoryScale::twelveEqual);
}

void MicrotonalTuningEngine::loadFactoryScale(FactoryScale scalePreset)
{
    m_scale.cents.clear();

    switch (scalePreset)
    {
    case FactoryScale::twelveEqual:
        m_scale.name = "12-TET";
        m_scale.description = "12-tone equal temperament";
        m_scale.formalOctaveCents = 1200.0;
        for (int i = 1; i <= 12; ++i)
        {
            m_scale.cents.push_back(i * 100.0);
        }
        break;

    case FactoryScale::nineteenEqual:
        m_scale.name = "19-EDO";
        m_scale.description = "19 equal divisions of octave";
        m_scale.formalOctaveCents = 1200.0;
        for (int i = 1; i <= 19; ++i)
        {
            m_scale.cents.push_back(i * (1200.0 / 19.0));
        }
        break;

    case FactoryScale::twentyFourEqual:
        m_scale.name = "24-EDO";
        m_scale.description = "24 equal divisions of octave (Quarter-tones)";
        m_scale.formalOctaveCents = 1200.0;
        for (int i = 1; i <= 24; ++i)
        {
            m_scale.cents.push_back(i * 50.0);
        }
        break;

    case FactoryScale::thirtyOneEqual:
        m_scale.name = "31-EDO";
        m_scale.description = "31 equal divisions of octave (Meantone)";
        m_scale.formalOctaveCents = 1200.0;
        for (int i = 1; i <= 31; ++i)
        {
            m_scale.cents.push_back(i * (1200.0 / 31.0));
        }
        break;

    case FactoryScale::justIntonation5L:
        m_scale.name = "5-Limit Just Intonation";
        m_scale.description = "Ptolemaic 12-tone chromatic just intonation";
        m_scale.formalOctaveCents = 1200.0;
        m_scale.cents = {
            ratioToCents(16.0, 15.0),  // 111.73 cents
            ratioToCents(9.0, 8.0),    // 203.91 cents
            ratioToCents(6.0, 5.0),    // 315.64 cents
            ratioToCents(5.0, 4.0),    // 386.31 cents
            ratioToCents(4.0, 3.0),    // 498.04 cents
            ratioToCents(45.0, 32.0),  // 590.22 cents
            ratioToCents(3.0, 2.0),    // 701.96 cents
            ratioToCents(8.0, 5.0),    // 813.69 cents
            ratioToCents(5.0, 3.0),    // 884.36 cents
            ratioToCents(9.0, 5.0),    // 1017.60 cents
            ratioToCents(15.0, 8.0),   // 1088.27 cents
            1200.0
        };
        break;

    case FactoryScale::justIntonation7L:
        m_scale.name = "7-Limit Just Intonation";
        m_scale.description = "Harmonic 7-limit tuning with 7/4 subharmonic";
        m_scale.formalOctaveCents = 1200.0;
        m_scale.cents = {
            ratioToCents(16.0, 15.0),
            ratioToCents(9.0, 8.0),
            ratioToCents(7.0, 6.0),    // 266.87 cents (subminor 3rd)
            ratioToCents(5.0, 4.0),
            ratioToCents(4.0, 3.0),
            ratioToCents(7.0, 5.0),    // 582.51 cents (septimal tritone)
            ratioToCents(3.0, 2.0),
            ratioToCents(8.0, 5.0),
            ratioToCents(5.0, 3.0),
            ratioToCents(7.0, 4.0),    // 968.83 cents (harmonic 7th)
            ratioToCents(15.0, 8.0),
            1200.0
        };
        break;

    case FactoryScale::slendro:
        m_scale.name = "Slendro";
        m_scale.description = "Indonesian Gamelan 5-tone equal scale";
        m_scale.formalOctaveCents = 1200.0;
        for (int i = 1; i <= 5; ++i)
        {
            m_scale.cents.push_back(i * 240.0);
        }
        break;

    case FactoryScale::pelog:
        m_scale.name = "Pelog";
        m_scale.description = "Indonesian Gamelan 7-tone unequal scale";
        m_scale.formalOctaveCents = 1200.0;
        m_scale.cents = {125.0, 275.0, 545.0, 670.0, 785.0, 955.0, 1200.0};
        break;

    case FactoryScale::indianShruti22:
        m_scale.name = "22-Shruti";
        m_scale.description = "Classical Indian Raga 22-Shruti division";
        m_scale.formalOctaveCents = 1200.0;
        m_scale.cents = {
            ratioToCents(256.0, 243.0), ratioToCents(16.0, 15.0),
            ratioToCents(10.0, 9.0),    ratioToCents(9.0, 8.0),
            ratioToCents(32.0, 27.0),   ratioToCents(6.0, 5.0),
            ratioToCents(5.0, 4.0),     ratioToCents(81.0, 64.0),
            ratioToCents(4.0, 3.0),     ratioToCents(27.0, 20.0),
            ratioToCents(45.0, 32.0),   ratioToCents(729.0, 512.0),
            ratioToCents(3.0, 2.0),     ratioToCents(128.0, 81.0),
            ratioToCents(8.0, 5.0),     ratioToCents(5.0, 3.0),
            ratioToCents(27.0, 16.0),   ratioToCents(16.0, 9.0),
            ratioToCents(9.0, 5.0),     ratioToCents(15.0, 8.0),
            ratioToCents(243.0, 128.0), 1200.0
        };
        break;

    case FactoryScale::arabicRast:
        m_scale.name = "Maqam Rast";
        m_scale.description = "Arabic Maqam Rast (quarter-tone 3rd and 7th degrees)";
        m_scale.formalOctaveCents = 1200.0;
        m_scale.cents = {
            200.0,  // D
            350.0,  // E half-flat (Sikah)
            500.0,  // F
            700.0,  // G
            900.0,  // A
            1050.0, // B half-flat (Awj)
            1200.0  // C octave
        };
        break;
    }

    // Default keyboard mapping: middle C = 60, reference A4 = 69, 440 Hz
    m_mapping.mapSize = 0;
    m_mapping.firstMidiNote = 0;
    m_mapping.lastMidiNote = 127;
    m_mapping.middleNote = 60;
    m_mapping.referenceNote = 69;
    m_mapping.referenceFrequencyHz = 440.0;
    m_mapping.scaleDegreeForReference = 9;
    m_mapping.degreeKeys.clear();

    rebuildFrequencyCache();
}

bool MicrotonalTuningEngine::loadScalaString(std::string_view sclContent)
{
    std::istringstream stream {std::string(sclContent)};
    std::string line;

    std::string description;
    int expectedCount = -1;
    std::vector<double> parsedCents;

    while (std::getline(stream, line))
    {
        const auto trimmed = trim(line);
        if (trimmed.empty() || trimmed.front() == '!')
        {
            continue;
        }

        if (description.empty())
        {
            // Check if line is purely integer (in case scale description was omitted)
            const bool isAllDigits = !trimmed.empty() && std::all_of(trimmed.begin(), trimmed.end(), [](unsigned char c) {
                return std::isdigit(c);
            });
            if (isAllDigits)
            {
                description = "Custom Scala Scale";
                try
                {
                    expectedCount = std::stoi(trimmed);
                }
                catch (...)
                {
                    return false;
                }
                continue;
            }

            description = trimmed;
            continue;
        }

        if (expectedCount < 0)
        {
            try
            {
                expectedCount = std::stoi(trimmed);
            }
            catch (...)
            {
                return false;
            }
            if (expectedCount <= 0 || expectedCount > 1024)
            {
                return false;
            }
            continue;
        }

        // Parsing interval line: remove comments/trailing tokens
        const auto commentPos = trimmed.find('!');
        const auto cleanLine = trim(trimmed.substr(0, commentPos));
        if (cleanLine.empty())
        {
            continue;
        }

        const auto slashPos = cleanLine.find('/');
        if (slashPos != std::string::npos)
        {
            try
            {
                const double num = std::stod(cleanLine.substr(0, slashPos));
                const double den = std::stod(cleanLine.substr(slashPos + 1));
                if (den <= 0.0)
                {
                    return false;
                }
                parsedCents.push_back(ratioToCents(num, den));
            }
            catch (...)
            {
                return false;
            }
        }
        else if (cleanLine.find('.') != std::string::npos)
        {
            try
            {
                parsedCents.push_back(std::stod(cleanLine));
            }
            catch (...)
            {
                return false;
            }
        }
        else
        {
            // Integer without dot is treated as ratio n/1
            try
            {
                const double num = std::stod(cleanLine);
                parsedCents.push_back(ratioToCents(num, 1.0));
            }
            catch (...)
            {
                return false;
            }
        }

        if (static_cast<int>(parsedCents.size()) == expectedCount)
        {
            break;
        }
    }

    if (parsedCents.empty() || static_cast<int>(parsedCents.size()) != expectedCount)
    {
        return false;
    }

    m_scale.name = description;
    m_scale.description = description;
    m_scale.cents = std::move(parsedCents);
    m_scale.formalOctaveCents = m_scale.cents.back();

    rebuildFrequencyCache();
    return true;
}

bool MicrotonalTuningEngine::loadMappingString(std::string_view kbmContent)
{
    std::istringstream stream {std::string(kbmContent)};
    std::string line;
    std::vector<std::string> lines;

    while (std::getline(stream, line))
    {
        const auto trimmed = trim(line);
        if (!trimmed.empty() && trimmed.front() != '!')
        {
            lines.push_back(trimmed);
        }
    }

    if (lines.size() < 7)
    {
        return false;
    }

    try
    {
        m_mapping.mapSize = std::stoi(lines[0]);
        m_mapping.firstMidiNote = std::stoi(lines[1]);
        m_mapping.lastMidiNote = std::stoi(lines[2]);
        m_mapping.middleNote = std::stoi(lines[3]);
        m_mapping.referenceNote = std::stoi(lines[4]);
        m_mapping.referenceFrequencyHz = std::stod(lines[5]);
        m_mapping.scaleDegreeForReference = std::stoi(lines[6]);

        m_mapping.degreeKeys.clear();
        for (std::size_t i = 7; i < lines.size(); ++i)
        {
            if (lines[i] == "x" || lines[i] == "X")
            {
                m_mapping.degreeKeys.push_back(-1);
            }
            else
            {
                m_mapping.degreeKeys.push_back(std::stoi(lines[i]));
            }
        }
    }
    catch (...)
    {
        return false;
    }

    rebuildFrequencyCache();
    return true;
}

void MicrotonalTuningEngine::setCustomScale(ScalaScale scale)
{
    m_scale = std::move(scale);
    if (!m_scale.cents.empty())
    {
        m_scale.formalOctaveCents = m_scale.cents.back();
    }
    rebuildFrequencyCache();
}

void MicrotonalTuningEngine::setKeyboardMapping(KeyboardMapping mapping)
{
    m_mapping = std::move(mapping);
    rebuildFrequencyCache();
}

void MicrotonalTuningEngine::setReferencePitch(double a4FrequencyHz) noexcept
{
    m_mapping.referenceFrequencyHz = std::max(10.0, a4FrequencyHz);
    rebuildFrequencyCache();
}

void MicrotonalTuningEngine::rebuildFrequencyCache() noexcept
{
    const std::size_t numDegrees = m_scale.cents.size();
    if (numDegrees == 0)
    {
        for (int note = 0; note < 128; ++note)
        {
            m_noteFrequenciesHz[note] = standard12TETFrequency(note, m_mapping.referenceFrequencyHz);
            m_noteOffsetsFrom12TET[note] = 0.0;
        }
        return;
    }

    const double octaveCents = (m_scale.formalOctaveCents > 0.0) ? m_scale.formalOctaveCents : 1200.0;

    // Helper to calculate total cents from base middleNote for a given MIDI note
    auto calculateNoteCents = [this, numDegrees, octaveCents](int midiNote) noexcept -> double {
        if (!m_mapping.degreeKeys.empty() && m_mapping.mapSize > 0)
        {
            const int delta = midiNote - m_mapping.middleNote;
            const int mapSize = static_cast<int>(m_mapping.degreeKeys.size());
            int oct = delta / mapSize;
            int rem = delta % mapSize;
            if (rem < 0)
            {
                rem += mapSize;
                oct -= 1;
            }
            const int degree = m_mapping.degreeKeys[rem];
            if (degree < 0)
            {
                return 0.0; // Unmapped key
            }
            const double degCents = (degree == 0) ? 0.0 : m_scale.cents[(degree - 1) % numDegrees];
            return oct * octaveCents + degCents;
        }

        // Standard sequential mapping
        const int delta = midiNote - m_mapping.middleNote;
        const int scaleSize = static_cast<int>(numDegrees);
        int oct = delta / scaleSize;
        int rem = delta % scaleSize;
        if (rem < 0)
        {
            rem += scaleSize;
            oct -= 1;
        }

        const double degCents = (rem == 0) ? 0.0 : m_scale.cents[rem - 1];
        return oct * octaveCents + degCents;
    };

    const double refNoteCents = calculateNoteCents(m_mapping.referenceNote);
    const double refFreq = (m_mapping.referenceFrequencyHz > 10.0) ? m_mapping.referenceFrequencyHz : 440.0;

    for (int note = 0; note < 128; ++note)
    {
        if (note < m_mapping.firstMidiNote || note > m_mapping.lastMidiNote)
        {
            // Outside retuned range: fall back to 12-TET
            m_noteFrequenciesHz[note] = standard12TETFrequency(note, refFreq);
            m_noteOffsetsFrom12TET[note] = 0.0;
            continue;
        }

        const double noteCents = calculateNoteCents(note);
        const double centsDiff = noteCents - refNoteCents;
        const double freq = refFreq * std::pow(2.0, centsDiff / 1200.0);
        m_noteFrequenciesHz[note] = freq;

        const double stdFreq = standard12TETFrequency(note, refFreq);
        m_noteOffsetsFrom12TET[note] = 1200.0 * (std::log(freq / stdFreq) / std::log(2.0));
    }
}

double MicrotonalTuningEngine::frequencyForNote(int midiNote, double pitchBendCents) const noexcept
{
    const int clampedNote = std::clamp(midiNote, 0, 127);
    const double baseFreq = m_noteFrequenciesHz[clampedNote];
    if (std::abs(pitchBendCents) < 0.0001)
    {
        return baseFreq;
    }
    return baseFreq * std::pow(2.0, pitchBendCents / 1200.0);
}

double MicrotonalTuningEngine::centsOffsetFrom12TET(int midiNote) const noexcept
{
    const int clampedNote = std::clamp(midiNote, 0, 127);
    return m_noteOffsetsFrom12TET[clampedNote];
}

std::size_t MicrotonalTuningEngine::divisionsPerOctave() const noexcept
{
    return m_scale.cents.empty() ? 12 : m_scale.cents.size();
}

double MicrotonalTuningEngine::centsForDegree(std::size_t degreeIndex) const noexcept
{
    if (m_scale.cents.empty())
    {
        return static_cast<double>(degreeIndex + 1) * 100.0;
    }
    if (degreeIndex >= m_scale.cents.size())
    {
        return m_scale.formalOctaveCents;
    }
    return m_scale.cents[degreeIndex];
}

std::string MicrotonalTuningEngine::noteDegreeLabel(int midiNote) const
{
    static constexpr std::array<const char*, 12> pitchNames = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };

    const int clampedNote = std::clamp(midiNote, 0, 127);
    const int octave = (clampedNote / 12) - 1;
    const std::string standardName = std::string(pitchNames[clampedNote % 12]) + std::to_string(octave);

    const double offsetCents = m_noteOffsetsFrom12TET[clampedNote];
    if (std::abs(offsetCents) < 0.5)
    {
        return standardName;
    }

    char buf[32];
    if (offsetCents > 0.0)
    {
        std::snprintf(buf, sizeof(buf), "%s +%.1fc", standardName.c_str(), offsetCents);
    }
    else
    {
        std::snprintf(buf, sizeof(buf), "%s %.1fc", standardName.c_str(), offsetCents);
    }
    return std::string(buf);
}

} // namespace dawg::engine::dsp
