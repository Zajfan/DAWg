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

enum class AnalogOscWaveform : std::uint8_t
{
    triangle,
    sawtooth,
    square,
    pulse,
    sine
};

enum class DualFilterRouting : std::uint8_t
{
    series,
    parallel
};

struct AnalogOscConfig
{
    AnalogOscWaveform waveform {AnalogOscWaveform::sawtooth};
    int octave {0};             // -2 to +2 octaves
    float semitones {0.0f};     // -24.0 to +24.0
    float fineTuneCents {0.0f}; // -100.0 to +100.0
    float pulseWidth {0.5f};    // 0.05 to 0.95
    float level {0.8f};
    bool syncToOsc1 {false};
};

// Analog Ladder & Diode Ring Modulator Synth (Minimoog / Buchla style)
// Features multi-oscillator analog synthesis, authentic germanium diode bridge ring modulation,
// series/parallel switchable 4-pole transistor & 3-pole diode ladder dual filters with self-oscillation,
// and classic headphone-out-to-input feedback overdrive loop.
class AnalogLadderRingModSynthNode final : public ProcessorNode
{
public:
    static constexpr std::size_t maxPolyphony = 12;
    static constexpr std::size_t oscCount = 3;

    AnalogLadderRingModSynthNode() noexcept;
    ~AnalogLadderRingModSynthNode() override = default;

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void reset() noexcept override;
    [[nodiscard]] std::uint32_t latencySamples() const noexcept override { return 0; }
    void process(ProcessContext& context) noexcept override;

    // Oscillator Configuration
    void setOscConfig(std::size_t oscIdx, const AnalogOscConfig& cfg) noexcept;
    [[nodiscard]] const AnalogOscConfig& oscConfig(std::size_t oscIdx) const noexcept;

    // Germanium Diode Ring Modulator
    void setRingModMix(float mix) noexcept { m_ringModMix = std::clamp(mix, 0.0f, 1.0f); }
    [[nodiscard]] float ringModMix() const noexcept { return m_ringModMix; }

    void setRingModDiodeThreshold(float thresh) noexcept { m_diodeThreshold = std::clamp(thresh, 0.1f, 2.0f); }
    [[nodiscard]] float ringModDiodeThreshold() const noexcept { return m_diodeThreshold; }

    // Dual-Filter Architecture
    void setFilterRouting(DualFilterRouting routing) noexcept { m_routing = routing; }
    [[nodiscard]] DualFilterRouting filterRouting() const noexcept { return m_routing; }

    // Filter 1: 4-Pole 24 dB/oct Transistor Ladder (Moog style)
    void setTransistorLadder(float cutoffHz, float resonance, float drive = 0.0f) noexcept;
    [[nodiscard]] float transistorCutoff() const noexcept { return m_transistorCutoffHz; }
    [[nodiscard]] float transistorResonance() const noexcept { return m_transistorResonance; }

    // Filter 2: 3-Pole 18 dB/oct Diode Ladder (TB-303 / EMS style)
    void setDiodeLadder(float cutoffHz, float resonance) noexcept;
    [[nodiscard]] float diodeCutoff() const noexcept { return m_diodeCutoffHz; }
    [[nodiscard]] float diodeResonance() const noexcept { return m_diodeResonance; }

    void setFilterBlend(float blend) noexcept { m_filterBlend = std::clamp(blend, 0.0f, 1.0f); }
    [[nodiscard]] float filterBlend() const noexcept { return m_filterBlend; }

    // Feedback Overdrive Loop (Headphone-to-input trick)
    void setFeedbackOverdrive(float amount) noexcept { m_feedbackAmount = std::clamp(amount, 0.0f, 2.0f); }
    [[nodiscard]] float feedbackOverdrive() const noexcept { return m_feedbackAmount; }

    // ADSR Envelopes
    void setAmpAdsr(float aMs, float dMs, float sLevel, float rMs) noexcept;
    void setFilterAdsr(float aMs, float dMs, float sLevel, float rMs, float envAmount = 0.5f) noexcept;

    // Note Triggering API
    void noteOn(std::uint8_t note, std::uint8_t velocity) noexcept;
    void noteOff(std::uint8_t note) noexcept;
    void allNotesOff() noexcept;

    // MPE (MIDI Polyphonic Expression) 5D Control API
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

        // MPE 5D Note Expressions
        float pitchBendSemitones {0.0f}; // Glide (-48.0 to +48.0 semitones)
        float pressure {0.0f};           // Press (0.0 to 1.0)
        float slide {0.0f};              // Slide / CC 74 (0.0 to 1.0)

        std::array<double, oscCount> phases {0.0, 0.0, 0.0};

        EnvStage ampStage {EnvStage::idle};
        float ampLevel {0.0f};

        EnvStage filtStage {EnvStage::idle};
        float filtLevel {0.0f};

        // 4-Pole Transistor Ladder states (4 stages)
        std::array<float, 4> transStatesL {0.0f, 0.0f, 0.0f, 0.0f};
        std::array<float, 4> transStatesR {0.0f, 0.0f, 0.0f, 0.0f};

        // 3-Pole Diode Ladder states (3 stages)
        std::array<float, 3> diodeStatesL {0.0f, 0.0f, 0.0f};
        std::array<float, 3> diodeStatesR {0.0f, 0.0f, 0.0f};

        float feedbackPrevL {0.0f};
        float feedbackPrevR {0.0f};
    };

    static float renderWaveform(double phase, AnalogOscWaveform wave, float pw) noexcept;
    static float diodeRingModulate(float carrier, float modulator, float diodeThresh) noexcept;

    double m_sampleRate {48000.0};
    std::array<AnalogOscConfig, oscCount> m_oscConfigs {};

    float m_ringModMix {0.0f};
    float m_diodeThreshold {0.6f};

    DualFilterRouting m_routing {DualFilterRouting::series};
    float m_transistorCutoffHz {3500.0f};
    float m_transistorResonance {1.0f};
    float m_transistorDrive {0.0f};

    float m_diodeCutoffHz {2500.0f};
    float m_diodeResonance {1.0f};
    float m_filterBlend {0.5f}; // 0.0 = Filter 1 only, 1.0 = Filter 2 only

    float m_feedbackAmount {0.0f};

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

    std::array<Voice, maxPolyphony> m_voices {};
};

} // namespace dawg::engine::dsp
