#pragma once

#include <dawg/engine/dsp/MicrotonalTuningEngine.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dawg::engine::dsp {

enum class HistoricalTuningPreset : std::uint8_t
{
    equalTemperament12,
    werckmeisterIII,
    meantoneQuarterComma,
    pythagorean,
    kirnbergerIII,
    vallotti,
    edo19,
    edo24QuarterTone,
    edo31
};

struct HermodeTuningConfig
{
    bool enabled {false};
    float depth {1.0f};          // 0.0 = Equal Temp .. 1.0 = Full Pure Just Intonation
    float glideMs {20.0f};       // Glide time for dynamic retuning
};

class MtsEspTuningEngine final
{
public:
    static constexpr std::size_t numNotes = 128;

    MtsEspTuningEngine() noexcept;
    ~MtsEspTuningEngine() = default;

    // =========================================================================
    // MTS-ESP Master / Client Protocol State
    // =========================================================================
    void setAsMaster(bool isMaster) noexcept;
    [[nodiscard]] bool isMaster() const noexcept;

    void setScaleName(std::string_view name);
    [[nodiscard]] std::string scaleName() const;

    // Note frequency retrieval (in Hz and in cents relative to 12-TET)
    [[nodiscard]] double noteFrequencyHz(int midiNote) const noexcept;
    [[nodiscard]] double noteOffsetCents(int midiNote) const noexcept;

    // Load historical temperament presets
    void loadHistoricalPreset(HistoricalTuningPreset preset);

    // Import Scala (.scl) and Keyboard Map (.kbm)
    [[nodiscard]] bool loadScala(std::string_view sclText);
    [[nodiscard]] bool loadKeyboardMap(std::string_view kbmText);

    // =========================================================================
    // Adaptive Just Intonation (Hermode Dynamic Retuning)
    // =========================================================================
    void setHermodeConfig(const HermodeTuningConfig& config) noexcept;
    [[nodiscard]] HermodeTuningConfig hermodeConfig() const noexcept;

    /// Updates dynamic retuning offsets based on the chord/notes currently sounding
    void updateActiveNotes(std::span<const std::uint8_t> activeMidiNotes) noexcept;

    /// Real-time sample-accurate frequency with dynamic Hermode retuning applied
    [[nodiscard]] double dynamicNoteFrequencyHz(int midiNote) const noexcept;

private:
    void updateTableFromHistorical(HistoricalTuningPreset preset);

    std::atomic<bool> m_isMaster {true};
    std::string m_scaleName {"12-Tone Equal Temperament"};

    // Base master tuning table (frequencies in Hz)
    std::array<std::atomic<double>, numNotes> m_masterFrequenciesHz {};
    std::array<std::atomic<double>, numNotes> m_masterOffsetsCents {};

    // Dynamic Hermode retuning offsets (cents)
    HermodeTuningConfig m_hermodeConfig {};
    std::array<std::atomic<float>, numNotes> m_hermodeOffsetsCents {};

    MicrotonalTuningEngine m_internalEngine;
};

} // namespace dawg::engine::dsp
