#pragma once

#include <dawg/engine/AudioBufferView.h>
#include <dawg/engine/MidiEvent.h>
#include <dawg/engine/ProcessorNode.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace dawg::engine::dsp {

enum class TripleOscWaveform : std::uint8_t
{
    sine,
    triangle,
    sawtooth,
    square,
    noise
};

enum class TripleOscModulation : std::uint8_t
{
    mix,
    am,
    fm
};

enum class TripleOscFilterType : std::uint8_t
{
    lowpass,
    highpass,
    bandpass,
    notch
};

struct TripleOscConfig
{
    TripleOscWaveform waveform {TripleOscWaveform::sawtooth};
    int coarsePitchSemitones {0};     // -24 to +24
    float finePitchCents {0.0f};      // -100.0 to +100.0
    float pan {0.0f};                 // -1.0 to +1.0
    float volume {0.8f};              // 0.0 to 1.0
    bool invertPhase {false};
    TripleOscModulation modMode {TripleOscModulation::mix};
};

/// 3xOsc Studio Edition - Legendary 3-Oscillator Subtractive / FM Synthesizer
/// High-efficiency polyphonic synthesis with morphable analog waveforms,
/// AM/FM cross-modulation matrix, stereo phase offset/inversion,
/// and analog-modeled state-variable multi-mode filter.
class TripleOscillatorSynthNode final : public ProcessorNode
{
public:
    static constexpr std::size_t maxPolyphony = 16;
    static constexpr std::size_t oscCount = 3;

    TripleOscillatorSynthNode() noexcept;
    ~TripleOscillatorSynthNode() override = default;

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void reset() noexcept override;
    [[nodiscard]] std::uint32_t latencySamples() const noexcept override { return 0; }
    void process(ProcessContext& context) noexcept override;

    // Oscillator Configuration
    void setOscConfig(std::size_t oscIdx, const TripleOscConfig& cfg) noexcept;
    [[nodiscard]] const TripleOscConfig& oscConfig(std::size_t oscIdx) const noexcept;

    // Filter Configuration
    void setFilterType(TripleOscFilterType type) noexcept { m_filterType = type; }
    [[nodiscard]] TripleOscFilterType filterType() const noexcept { return m_filterType; }
    void setFilterCutoff(float cutoffHz) noexcept { m_filterCutoffHz = std::clamp(cutoffHz, 20.0f, 20000.0f); }
    [[nodiscard]] float filterCutoff() const noexcept { return m_filterCutoffHz; }
    void setFilterResonance(float reso) noexcept { m_filterResonance = std::clamp(reso, 0.1f, 10.0f); }
    [[nodiscard]] float filterResonance() const noexcept { return m_filterResonance; }

    // Envelopes
    void setAmpAdsr(float aMs, float dMs, float sLevel, float rMs) noexcept;
    void setFilterAdsr(float aMs, float dMs, float sLevel, float rMs, float envAmount = 0.5f) noexcept;

    // Note Control
    void noteOn(std::uint8_t note, std::uint8_t velocity) noexcept;
    void noteOff(std::uint8_t note) noexcept;
    void allNotesOff() noexcept;
    void notePitchBend(std::uint8_t note, float semitones) noexcept;
    void notePressure(std::uint8_t note, float pressure) noexcept;
    void noteSlide(std::uint8_t note, float slide) noexcept;

    [[nodiscard]] std::size_t activeVoiceCount() const noexcept;

private:
    enum class EnvStage : std::uint8_t { idle, attack, decay, sustain, release };

    struct Voice
    {
        bool active {false};
        std::uint8_t note {0};
        std::uint8_t velocity {0};
        double noteFreq {440.0};
        float pitchBendSemitones {0.0f};
        float pressure {0.0f};
        float slide {0.0f};

        std::array<double, oscCount> phases {0.0, 0.0, 0.0};

        EnvStage ampStage {EnvStage::idle};
        float ampLevel {0.0f};

        EnvStage filtStage {EnvStage::idle};
        float filtLevel {0.0f};

        // State-Variable Filter (SVF) state per channel (L/R)
        float svfLowL {0.0f};
        float svfBandL {0.0f};
        float svfLowR {0.0f};
        float svfBandR {0.0f};
    };

    static float renderWaveform(double phase, TripleOscWaveform wave, std::uint32_t& noiseSeed) noexcept;

    double m_sampleRate {48000.0};
    std::array<TripleOscConfig, oscCount> m_oscConfigs {};

    TripleOscFilterType m_filterType {TripleOscFilterType::lowpass};
    float m_filterCutoffHz {4500.0f};
    float m_filterResonance {1.0f};

    // Amp ADSR
    float m_ampAttackRate {0.01f};
    float m_ampDecayRate {0.005f};
    float m_ampSustainLevel {0.8f};
    float m_ampReleaseRate {0.005f};

    // Filter ADSR
    float m_filtAttackRate {0.01f};
    float m_filtDecayRate {0.005f};
    float m_filtSustainLevel {0.5f};
    float m_filtReleaseRate {0.005f};
    float m_filtEnvAmount {0.4f};

    std::uint32_t m_noiseSeed {123456789};
    std::array<Voice, maxPolyphony> m_voices {};
};

} // namespace dawg::engine::dsp
