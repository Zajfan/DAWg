#pragma once

#include <dawg/engine/ProcessorNode.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace dawg::engine {

enum class SynthWaveform : std::uint8_t
{
    saw,
    pulse,
    triangle,
    sine,
    noise
};

enum class SynthFilterMode : std::uint8_t
{
    lowpass,
    bandpass,
    highpass
};

struct AdsrParams
{
    float attackSec {0.005f};
    float decaySec {0.1f};
    float sustainLevel {0.7f};
    float releaseSec {0.2f};
};

class TnhcSynthNode final : public ProcessorNode
{
public:
    static constexpr std::size_t maxVoices = 16;

    TnhcSynthNode() noexcept;
    ~TnhcSynthNode() override = default;

    [[nodiscard]] bool prepare(double sampleRate, std::size_t maxBlockSize,
                               std::uint16_t channelCount) override;
    void reset() noexcept override;
    [[nodiscard]] std::uint32_t latencySamples() const noexcept override { return 0; }
    void process(ProcessContext& context) noexcept override;

    // Direct note triggering for tests or external MIDI routing
    void noteOn(std::uint8_t note, float velocity) noexcept { noteOn(0, note, velocity); }
    void noteOn(std::uint8_t channel, std::uint8_t note, float velocity) noexcept;
    void noteOff(std::uint8_t note) noexcept { noteOff(0, note); }
    void noteOff(std::uint8_t channel, std::uint8_t note) noexcept;
    void allNotesOff() noexcept;

    // MPE (MIDI Polyphonic Expression) controls
    void setMpeEnabled(bool enabled) noexcept { m_mpeEnabled = enabled; }
    [[nodiscard]] bool isMpeEnabled() const noexcept { return m_mpeEnabled; }
    void setPitchBendRange(float semitones) noexcept { m_pitchBendRange = std::clamp(semitones, 0.0f, 96.0f); }
    [[nodiscard]] float pitchBendRange() const noexcept { return m_pitchBendRange; }

    void setChannelPitchBend(std::uint8_t channel, float semitones) noexcept;
    void setChannelPressure(std::uint8_t channel, float pressure) noexcept;
    void setChannelTimbre(std::uint8_t channel, float timbre) noexcept;

    [[nodiscard]] std::size_t activeVoiceCount() const noexcept;

    // Parameter configuration
    void setOsc1Waveform(SynthWaveform wave) noexcept { m_osc1Wave = wave; }
    void setOsc2Waveform(SynthWaveform wave) noexcept { m_osc2Wave = wave; }
    void setOscMix(float mix) noexcept { m_oscMix = std::clamp(mix, 0.0f, 1.0f); }
    void setOsc2DetuneCents(float cents) noexcept { m_osc2DetuneCents = cents; }
    void setPulseWidth(float pw) noexcept { m_pulseWidth = std::clamp(pw, 0.05f, 0.95f); }

    void setFilterMode(SynthFilterMode mode) noexcept { m_filterMode = mode; }
    void setFilterCutoff(float fc) noexcept { m_filterCutoff = std::clamp(fc, 20.0f, 20000.0f); }
    void setFilterResonance(float q) noexcept { m_filterResonance = std::clamp(q, 0.5f, 20.0f); }
    void setFilterEnvAmount(float amt) noexcept { m_filterEnvAmount = std::clamp(amt, -1.0f, 1.0f); }

    void setAmpAdsr(AdsrParams params) noexcept { m_ampParams = params; }
    void setFilterAdsr(AdsrParams params) noexcept { m_filterParams = params; }

    void setLfoRate(float hz) noexcept { m_lfoRate = std::clamp(hz, 0.05f, 40.0f); }
    void setLfoPitchDepth(float semitones) noexcept { m_lfoPitchDepth = std::clamp(semitones, 0.0f, 12.0f); }
    void setLfoFilterDepth(float octaves) noexcept { m_lfoFilterDepth = std::clamp(octaves, 0.0f, 5.0f); }

private:
    enum class EnvStage : std::uint8_t
    {
        idle,
        attack,
        decay,
        sustain,
        release
    };

    struct AdsrEnvelope
    {
        EnvStage stage {EnvStage::idle};
        float currentLevel {0.0f};

        void trigger() noexcept
        {
            stage = EnvStage::attack;
        }

        void release() noexcept
        {
            if (stage != EnvStage::idle)
                stage = EnvStage::release;
        }

        void reset() noexcept
        {
            stage = EnvStage::idle;
            currentLevel = 0.0f;
        }

        float advance(const AdsrParams& params, double sampleRate) noexcept;
    };

    // Cytomic / TPTS State Variable Filter (2-pole stage)
    struct TptsSvf
    {
        float s1 {0.0f};
        float s2 {0.0f};

        void reset() noexcept
        {
            s1 = 0.0f;
            s2 = 0.0f;
        }

        float process(float in, float cutoffHz, float q, double sampleRate, SynthFilterMode mode) noexcept;
    };

    struct Voice
    {
        bool active {false};
        std::uint8_t channel {0};
        std::uint8_t note {60};
        float velocity {0.8f};
        std::uint64_t age {0};

        float pitchBendSemitones {0.0f};
        float pressure {0.0f};
        float timbre {0.5f};

        double osc1Phase {0.0};
        double osc2Phase {0.0};

        AdsrEnvelope ampEnv;
        AdsrEnvelope filterEnv;

        TptsSvf svfStage1;
        TptsSvf svfStage2;

        std::uint32_t rngState {12345};

        void reset() noexcept
        {
            active = false;
            channel = 0;
            note = 60;
            velocity = 0.8f;
            age = 0;
            pitchBendSemitones = 0.0f;
            pressure = 0.0f;
            timbre = 0.5f;
            osc1Phase = 0.0;
            osc2Phase = 0.0;
            ampEnv.reset();
            filterEnv.reset();
            svfStage1.reset();
            svfStage2.reset();
        }
    };

    [[nodiscard]] Voice* allocateVoice() noexcept;
    [[nodiscard]] float renderVoiceSample(Voice& voice, float lfoVal) noexcept;
    [[nodiscard]] float evaluateOsc(SynthWaveform wave, double phase, double phaseInc, float pulseWidth, std::uint32_t& rng) noexcept;

    double m_sampleRate {48000.0};
    std::uint16_t m_channelCount {2};
    std::uint64_t m_voiceAgeCounter {0};

    std::array<Voice, maxVoices> m_voices {};

    SynthWaveform m_osc1Wave {SynthWaveform::saw};
    SynthWaveform m_osc2Wave {SynthWaveform::pulse};
    float m_oscMix {0.5f};
    float m_osc2DetuneCents {7.0f};
    float m_pulseWidth {0.5f};

    SynthFilterMode m_filterMode {SynthFilterMode::lowpass};
    float m_filterCutoff {2000.0f};
    float m_filterResonance {2.0f};
    float m_filterEnvAmount {0.5f};

    AdsrParams m_ampParams {0.005f, 0.1f, 0.7f, 0.2f};
    AdsrParams m_filterParams {0.01f, 0.15f, 0.3f, 0.3f};

    double m_lfoPhase {0.0};
    float m_lfoRate {2.0f};
    float m_lfoPitchDepth {0.0f};
    float m_lfoFilterDepth {0.0f};

    bool m_mpeEnabled {true};
    float m_pitchBendRange {48.0f}; // Standard 48 semitones for MPE
    std::array<float, 16> m_channelPitchBend {};
    std::array<float, 16> m_channelPressure {};
    std::array<float, 16> m_channelTimbre {};
};

} // namespace dawg::engine
