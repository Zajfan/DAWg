#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace dawg::engine::dsp {

enum class FactoryScale : std::uint8_t
{
    twelveEqual,      // 12-TET (Standard Western equal temperament)
    nineteenEqual,    // 19-EDO (19 equal divisions of octave, pure thirds)
    twentyFourEqual,  // 24-EDO (1/4-tone Arabic Maqam & modern microtonal)
    thirtyOneEqual,   // 31-EDO (31 equal divisions, excellent meantone)
    justIntonation5L, // 5-limit Just Intonation Chromatic Scale
    justIntonation7L, // 7-limit Just Intonation (harmonic 7th)
    slendro,          // Indonesian Gamelan 5-tone equal temperament
    pelog,            // Indonesian Gamelan 7-tone unequal temperament
    indianShruti22,   // Classical Indian 22-Shruti Raga system
    arabicRast        // Arabic Rast Maqam (quarter-tone neutral 3rd & 7th)
};

struct ScalaScale
{
    std::string name;
    std::string description;
    std::vector<double> cents; // Cumulative cents for each scale degree from base
    double formalOctaveCents {1200.0};
};

struct KeyboardMapping
{
    int mapSize {0};
    int firstMidiNote {0};
    int lastMidiNote {127};
    int middleNote {60};    // C4
    int referenceNote {69}; // A4
    double referenceFrequencyHz {440.0};
    int scaleDegreeForReference {9}; // A is typically 9th degree of 12-TET
    std::vector<int> degreeKeys; // Mapping key to scale degree (-1 = unmapped)
};

// Global Musical Microtonal Tuning Engine (Scala .scl, .kbm, and MTS retuning)
class MicrotonalTuningEngine
{
public:
    MicrotonalTuningEngine() noexcept;
    ~MicrotonalTuningEngine() = default;

    // Load factory scale presets
    void loadFactoryScale(FactoryScale scalePreset);

    // Scala (.scl) and Keyboard Mapping (.kbm) string parsers
    [[nodiscard]] bool loadScalaString(std::string_view sclContent);
    [[nodiscard]] bool loadMappingString(std::string_view kbmContent);

    // Manual custom configuration
    void setCustomScale(ScalaScale scale);
    void setKeyboardMapping(KeyboardMapping mapping);
    void setReferencePitch(double a4FrequencyHz) noexcept;

    // Active scale & tuning metadata
    [[nodiscard]] const ScalaScale& currentScale() const noexcept { return m_scale; }
    [[nodiscard]] const KeyboardMapping& currentMapping() const noexcept { return m_mapping; }
    [[nodiscard]] std::size_t scaleDegreeCount() const noexcept { return m_scale.cents.size(); }
    [[nodiscard]] double referencePitchHz() const noexcept { return m_mapping.referenceFrequencyHz; }

    // Real-Time Note Retuning (zero latency, real-time audio safe)
    [[nodiscard]] double frequencyForNote(int midiNote, double pitchBendCents = 0.0) const noexcept;
    [[nodiscard]] double centsOffsetFrom12TET(int midiNote) const noexcept;

    // Piano Roll Microtonal Grid Support
    [[nodiscard]] std::size_t divisionsPerOctave() const noexcept;
    [[nodiscard]] double centsForDegree(std::size_t degreeIndex) const noexcept;
    [[nodiscard]] std::string noteDegreeLabel(int midiNote) const;

    // Standard 12-TET reference frequency
    [[nodiscard]] static constexpr double standard12TETFrequency(int midiNote, double a4Hz = 440.0) noexcept
    {
        return a4Hz * std::pow(2.0, (static_cast<double>(midiNote) - 69.0) / 12.0);
    }

private:
    void rebuildFrequencyCache() noexcept;

    ScalaScale m_scale;
    KeyboardMapping m_mapping;

    // Fast O(1) lookup table for MIDI notes 0..127
    std::array<double, 128> m_noteFrequenciesHz {};
    std::array<double, 128> m_noteOffsetsFrom12TET {};
};

} // namespace dawg::engine::dsp
