#include <dawg/engine/dsp/MtsEspTuningEngine.h>

#include <algorithm>
#include <cmath>

namespace dawg::engine::dsp {

namespace {

constexpr double standardA4 = 440.0;

inline double calc12TetHz(int note) noexcept
{
    return standardA4 * std::pow(2.0, (static_cast<double>(note) - 69.0) / 12.0);
}

} // namespace

MtsEspTuningEngine::MtsEspTuningEngine() noexcept
{
    loadHistoricalPreset(HistoricalTuningPreset::equalTemperament12);
    for (auto& h : m_hermodeOffsetsCents)
        h.store(0.0f, std::memory_order_relaxed);
}

void MtsEspTuningEngine::setAsMaster(const bool isMaster) noexcept
{
    m_isMaster.store(isMaster, std::memory_order_release);
}

bool MtsEspTuningEngine::isMaster() const noexcept
{
    return m_isMaster.load(std::memory_order_acquire);
}

void MtsEspTuningEngine::setScaleName(const std::string_view name)
{
    m_scaleName = std::string(name);
}

std::string MtsEspTuningEngine::scaleName() const
{
    return m_scaleName;
}

double MtsEspTuningEngine::noteFrequencyHz(const int midiNote) const noexcept
{
    if (midiNote < 0 || midiNote >= static_cast<int>(numNotes))
        return calc12TetHz(midiNote);
    return m_masterFrequenciesHz[static_cast<std::size_t>(midiNote)].load(std::memory_order_relaxed);
}

double MtsEspTuningEngine::noteOffsetCents(const int midiNote) const noexcept
{
    if (midiNote < 0 || midiNote >= static_cast<int>(numNotes))
        return 0.0;
    return m_masterOffsetsCents[static_cast<std::size_t>(midiNote)].load(std::memory_order_relaxed);
}

void MtsEspTuningEngine::loadHistoricalPreset(const HistoricalTuningPreset preset)
{
    updateTableFromHistorical(preset);
}

void MtsEspTuningEngine::updateTableFromHistorical(const HistoricalTuningPreset preset)
{
    // Cents deviations per semitone (C, C#, D, D#, E, F, F#, G, G#, A, A#, B) relative to 12-TET
    std::array<double, 12> pitchClassOffsets {};

    switch (preset)
    {
    case HistoricalTuningPreset::equalTemperament12:
        m_scaleName = "12-Tone Equal Temperament";
        pitchClassOffsets.fill(0.0);
        break;

    case HistoricalTuningPreset::werckmeisterIII:
        m_scaleName = "Werckmeister III (1691)";
        // Four pure 1/4 comma narrow fifths: C-G, G-D, D-A, B-F#
        pitchClassOffsets = { 0.0, -9.8, -7.8, -5.9, -9.8, -3.9, -11.7, -3.9, -7.8, -9.8, -5.9, -7.8 };
        break;

    case HistoricalTuningPreset::meantoneQuarterComma:
        m_scaleName = "1/4-Comma Meantone (Pietro Aaron 1523)";
        // Pure 5:4 major thirds (386.3 cents instead of 400 cents)
        pitchClassOffsets = { 0.0, -24.0, -7.0, +10.0, -14.0, +3.5, -20.5, -3.5, -27.5, -10.5, +7.0, -17.5 };
        break;

    case HistoricalTuningPreset::pythagorean:
        m_scaleName = "Pythagorean Tuning (Pure 3:2 Fifths)";
        pitchClassOffsets = { 0.0, -10.0, +4.0, -6.0, +8.0, -2.0, +12.0, +2.0, -8.0, +6.0, -4.0, +10.0 };
        break;

    case HistoricalTuningPreset::kirnbergerIII:
        m_scaleName = "Kirnberger III (Johann Kirnberger 1779)";
        pitchClassOffsets = { 0.0, -9.8, -6.8, -5.9, -13.7, -2.0, -9.8, -3.4, -7.8, -10.3, -5.9, -8.3 };
        break;

    case HistoricalTuningPreset::vallotti:
        m_scaleName = "Vallotti (Francesco Antonio Vallotti 1779)";
        pitchClassOffsets = { 0.0, -3.9, -3.9, -2.0, -7.8, +2.0, -5.9, -2.0, -3.9, -5.9, 0.0, -7.8 };
        break;

    case HistoricalTuningPreset::edo19:
        m_scaleName = "19-EDO Equal Division";
        break;

    case HistoricalTuningPreset::edo24QuarterTone:
        m_scaleName = "24-EDO Quarter-Tone / Arabic Maqam";
        break;

    case HistoricalTuningPreset::edo31:
        m_scaleName = "31-EDO Huygens Meantone";
        break;
    }

    if (preset == HistoricalTuningPreset::edo19)
    {
        for (int n = 0; n < 128; ++n)
        {
            // 19 divisions of 1200 cents = 63.158 cents per step
            const double centsFromA4 = static_cast<double>(n - 69) * (1200.0 / 19.0);
            const double freq = standardA4 * std::pow(2.0, centsFromA4 / 1200.0);
            const double tet12 = calc12TetHz(n);
            const double offset = 1200.0 * std::log2(freq / tet12);
            m_masterFrequenciesHz[static_cast<std::size_t>(n)].store(freq, std::memory_order_relaxed);
            m_masterOffsetsCents[static_cast<std::size_t>(n)].store(offset, std::memory_order_relaxed);
        }
    }
    else if (preset == HistoricalTuningPreset::edo24QuarterTone)
    {
        for (int n = 0; n < 128; ++n)
        {
            // Standard 12-TET notes match, quarter-tones are at half-semitones
            const double tet12 = calc12TetHz(n);
            m_masterFrequenciesHz[static_cast<std::size_t>(n)].store(tet12, std::memory_order_relaxed);
            m_masterOffsetsCents[static_cast<std::size_t>(n)].store(0.0, std::memory_order_relaxed);
        }
    }
    else if (preset == HistoricalTuningPreset::edo31)
    {
        for (int n = 0; n < 128; ++n)
        {
            // 31 divisions = 38.71 cents per step
            const double centsFromA4 = static_cast<double>(n - 69) * (1200.0 / 31.0);
            const double freq = standardA4 * std::pow(2.0, centsFromA4 / 1200.0);
            const double tet12 = calc12TetHz(n);
            const double offset = 1200.0 * std::log2(freq / tet12);
            m_masterFrequenciesHz[static_cast<std::size_t>(n)].store(freq, std::memory_order_relaxed);
            m_masterOffsetsCents[static_cast<std::size_t>(n)].store(offset, std::memory_order_relaxed);
        }
    }
    else
    {
        for (int n = 0; n < 128; ++n)
        {
            const auto pc = static_cast<std::size_t>(n % 12);
            const double centsDev = pitchClassOffsets[pc];
            const double tet12 = calc12TetHz(n);
            const double freq = tet12 * std::pow(2.0, centsDev / 1200.0);
            m_masterFrequenciesHz[static_cast<std::size_t>(n)].store(freq, std::memory_order_relaxed);
            m_masterOffsetsCents[static_cast<std::size_t>(n)].store(centsDev, std::memory_order_relaxed);
        }
    }
}

bool MtsEspTuningEngine::loadScala(const std::string_view sclText)
{
    if (m_internalEngine.loadScalaString(sclText))
    {
        m_scaleName = m_internalEngine.currentScale().name;
        for (int n = 0; n < 128; ++n)
        {
            const double freq = m_internalEngine.frequencyForNote(n);
            const double offset = m_internalEngine.centsOffsetFrom12TET(n);
            m_masterFrequenciesHz[static_cast<std::size_t>(n)].store(freq, std::memory_order_release);
            m_masterOffsetsCents[static_cast<std::size_t>(n)].store(offset, std::memory_order_release);
        }
        return true;
    }
    return false;
}

bool MtsEspTuningEngine::loadKeyboardMap(const std::string_view kbmText)
{
    return m_internalEngine.loadMappingString(kbmText);
}

void MtsEspTuningEngine::setHermodeConfig(const HermodeTuningConfig& config) noexcept
{
    m_hermodeConfig = config;
    if (! config.enabled)
    {
        for (auto& h : m_hermodeOffsetsCents)
            h.store(0.0f, std::memory_order_release);
    }
}

HermodeTuningConfig MtsEspTuningEngine::hermodeConfig() const noexcept
{
    return m_hermodeConfig;
}

void MtsEspTuningEngine::updateActiveNotes(const std::span<const std::uint8_t> activeMidiNotes) noexcept
{
    if (! m_hermodeConfig.enabled || activeMidiNotes.empty())
    {
        for (auto& h : m_hermodeOffsetsCents)
            h.store(0.0f, std::memory_order_relaxed);
        return;
    }

    // Determine candidate root (lowest active note)
    std::uint8_t root = activeMidiNotes[0];
    for (const auto note : activeMidiNotes)
    {
        if (note < root) root = note;
    }

    const float depth = std::clamp(m_hermodeConfig.depth, 0.0f, 1.0f);

    // Apply pure harmonic intervals relative to root:
    // Pure Major 3rd (5:4 ratio = 386.31 cents => -13.69 cents deviation from 12-TET 400 cents)
    // Pure Minor 3rd (6:5 ratio = 315.64 cents => +15.64 cents deviation from 12-TET 300 cents)
    // Pure 5th       (3:2 ratio = 701.96 cents => +1.96 cents deviation from 12-TET 700 cents)
    for (const auto note : activeMidiNotes)
    {
        if (note >= numNotes) continue;

        const int semitones = (static_cast<int>(note) - static_cast<int>(root)) % 12;
        float offsetCents = 0.0f;

        if (semitones == 4)       // Major 3rd
            offsetCents = -13.69f * depth;
        else if (semitones == 3)  // Minor 3rd
            offsetCents = +15.64f * depth;
        else if (semitones == 7)  // Perfect 5th
            offsetCents = +1.96f * depth;

        m_hermodeOffsetsCents[note].store(offsetCents, std::memory_order_relaxed);
    }
}

double MtsEspTuningEngine::dynamicNoteFrequencyHz(const int midiNote) const noexcept
{
    const double baseHz = noteFrequencyHz(midiNote);
    if (! m_hermodeConfig.enabled || midiNote < 0 || midiNote >= static_cast<int>(numNotes))
        return baseHz;

    const float dynCents = m_hermodeOffsetsCents[static_cast<std::size_t>(midiNote)].load(std::memory_order_relaxed);
    if (std::abs(dynCents) < 0.01f)
        return baseHz;

    return baseHz * std::pow(2.0, static_cast<double>(dynCents) / 1200.0);
}

} // namespace dawg::engine::dsp
