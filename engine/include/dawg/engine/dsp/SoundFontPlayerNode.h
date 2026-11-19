#pragma once

#include <dawg/engine/ProcessorNode.h>
#include <dawg/engine/dsp/ZdfLadderFilter.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace dawg::engine {

struct SoundFontInstrument
{
    std::string name {"Acoustic Concert Grand"};
    std::string category {"Piano"};
    float attackMs {5.0f};
    float decayMs {450.0f};
    float sustainLevel {0.70f};
    float releaseMs {600.0f};
    float filterCutoff {16000.0f};
    float filterResonance {0.1f};
};

class SoundFontPlayerNode final : public ProcessorNode
{
public:
    SoundFontPlayerNode() noexcept;
    ~SoundFontPlayerNode() override = default;

    bool prepare(double sampleRate, std::size_t maxBlockSize, std::uint16_t channelCount) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;

    [[nodiscard]] std::string_view name() const noexcept { return "SoundFont / Multi-Sample Player"; }

    void noteOn(std::uint8_t pitch, float velocity) noexcept;
    void noteOff(std::uint8_t pitch) noexcept;
    void allNotesOff() noexcept;

    [[nodiscard]] const SoundFontInstrument& instrument() const noexcept { return m_instrument; }
    void setInstrument(const SoundFontInstrument& inst) noexcept { m_instrument = inst; }

    void loadPreset(std::size_t index) noexcept;
    [[nodiscard]] std::size_t presetCount() const noexcept { return kPresets.size(); }
    [[nodiscard]] const char* presetName(std::size_t index) const noexcept;

private:
    struct Voice
    {
        bool active {false};
        std::uint8_t pitch {60};
        float velocity {0.8f};
        double samplePos {0.0};
        double pitchRatio {1.0};

        enum class Stage : std::uint8_t { idle, attack, decay, sustain, release };
        Stage envStage {Stage::idle};
        float envLevel {0.0f};

        dsp::ZdfLadderFilter filter;
    };

    void processVoice(Voice& v, float& outL, float& outR) noexcept;
    void generateInternalWaveform(std::size_t instIdx);

    double m_sampleRate {48000.0};
    SoundFontInstrument m_instrument;

    // Built-in multi-sample tables
    std::vector<float> m_sampleWaveform;
    double m_baseFrequency {261.63}; // C4

    static constexpr std::size_t kMaxVoices = 16;
    std::array<Voice, kMaxVoices> m_voices {};

    static const std::array<SoundFontInstrument, 4> kPresets;
};

} // namespace dawg::engine
