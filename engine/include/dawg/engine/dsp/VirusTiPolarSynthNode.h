#pragma once

#include <dawg/engine/ProcessorNode.h>
#include <dawg/engine/dsp/ZdfLadderFilter.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <string>
#include <vector>

namespace dawg::engine {

enum class VirusOscMode : std::uint8_t
{
    classicVa,
    hypersaw,
    wavetable
};

enum class VirusFilterRouting : std::uint8_t
{
    serial4,
    serial6,
    parallel4,
    split
};

enum class VirusDistortionType : std::uint8_t
{
    off,
    softClip,
    hardClip,
    rectifier,
    tubeCurve,
    bitCrush
};

struct VirusTiPatch
{
    std::string name {"Polar Hypersaw Lead"};
    std::string category {"Lead"};

    // Oscillator 1
    VirusOscMode osc1Mode {VirusOscMode::hypersaw};
    float osc1Shape {0.0f}; // 0 = Saw, 1 = Pulse
    float hypersawDensity {7.0f}; // 1 to 9 saws
    float hypersawDetune {0.35f};
    float hypersawSpread {0.80f};

    // Oscillator 2
    VirusOscMode osc2Mode {VirusOscMode::classicVa};
    float osc2Semitone {0.0f};
    float osc2DetuneCents {9.0f};
    float osc2FmAmount {0.0f};
    bool oscSync {true};

    // Sub-Osc & Mix
    float subOscLevel {0.35f};
    float noiseLevel {0.05f};
    float oscMix {0.5f}; // 0 = Osc 1, 1 = Osc 2

    // Dual Filters
    VirusFilterRouting filterRouting {VirusFilterRouting::serial4};
    float filter1Cutoff {3800.0f};
    float filter1Resonance {2.2f};
    float filter2Cutoff {2200.0f};
    float filter2Resonance {3.5f};
    float filterBalance {0.0f}; // -1 = Filt 1, +1 = Filt 2
    float filterEnvAmount {0.60f};

    // Distortion & Character
    VirusDistortionType distortion {VirusDistortionType::softClip};
    float drive {0.45f};
    float analogDrift {0.20f};

    // Envelopes
    float ampAttack {0.003f};
    float ampDecay {0.18f};
    float ampSustain {0.65f};
    float ampRelease {0.25f};

    float filtAttack {0.005f};
    float filtDecay {0.25f};
    float filtSustain {0.20f};
    float filtRelease {0.30f};

    // LFOs
    float lfo1Rate {4.5f};
    float lfo1PitchDepth {0.0f};
    float lfo2Rate {2.0f};
    float lfo2FilterDepth {0.40f};

    // Effects
    bool delayEnabled {true};
    float delayTimeSec {0.375f}; // Dotted 8th
    float delayFeedback {0.42f};
    float delayMix {0.30f};

    bool reverbEnabled {true};
    float reverbDecay {0.55f};
    float reverbMix {0.25f};

    bool arpEnabled {false};
    float arpBpm {120.0f};
};

/// High-End Virtual Analog & Hypersaw Hardware Emulation Synth
/// Faithfully captures the sound architecture and sonic character of the Access Virus TI Polar.
class VirusTiPolarSynthNode final : public ProcessorNode
{
public:
    VirusTiPolarSynthNode() noexcept;
    ~VirusTiPolarSynthNode() override = default;

    [[nodiscard]] bool prepare(double sampleRate, std::size_t maxBlockSize,
                               std::uint16_t channelCount) override;
    void reset() noexcept override;
    [[nodiscard]] std::uint32_t latencySamples() const noexcept override { return 0; }
    void process(ProcessContext& context) noexcept override;

    // MIDI Note Triggering
    void noteOn(std::uint8_t note, float velocity) noexcept;
    void noteOff(std::uint8_t note) noexcept;
    void allNotesOff() noexcept;

    // Patch & Preset Management
    [[nodiscard]] static std::vector<VirusTiPatch> factoryPresets();
    void loadPatch(const VirusTiPatch& patch) noexcept;
    [[nodiscard]] const VirusTiPatch& currentPatch() const noexcept { return m_patch; }

    [[nodiscard]] std::size_t activeVoiceCount() const noexcept;
    [[nodiscard]] float currentLcdScopeValue() const noexcept { return m_lastOutputSample; }

    // Direct Parameter Modulations
    void setFilter1Cutoff(float hz) noexcept;
    void setFilter1Resonance(float q) noexcept;
    void setDrive(float drive) noexcept;
    void setHypersawDetune(float detune) noexcept;
    void setDelayMix(float mix) noexcept;
    void setReverbMix(float mix) noexcept;

private:
    struct Voice
    {
        bool active {false};
        std::uint8_t note {60};
        float velocity {0.8f};
        float targetFreq {261.63f};

        // Hypersaw sub-oscillators (up to 9 saws per voice)
        std::array<float, 9> osc1Phases {};
        std::array<float, 9> osc1FreqOffsets {};
        std::array<float, 9> osc1PanWeights {};

        // Oscillator 2 & Sub
        float osc2Phase {0.0f};
        float subPhase {0.0f};

        // Envelopes
        enum class Stage : std::uint8_t { idle, attack, decay, sustain, release };
        Stage ampStage {Stage::idle};
        float ampLevel {0.0f};
        Stage filtStage {Stage::idle};
        float filtLevel {0.0f};

        // Dual ZDF ladder filters per voice
        dsp::ZdfLadderFilter filter1;
        dsp::ZdfLadderFilter filter2;

        float analogDriftOffset {0.0f};
    };

    void processVoice(Voice& v, float& outL, float& outR) noexcept;
    float evaluateShape(float phase, float shape) const noexcept;
    float applyDistortion(float in, VirusDistortionType type, float drive) const noexcept;

    double m_sampleRate {48000.0};
    VirusTiPatch m_patch;

    static constexpr std::size_t kMaxVoices = 16;
    std::array<Voice, kMaxVoices> m_voices {};

    // Global LFOs
    float m_lfo1Phase {0.0f};
    float m_lfo2Phase {0.0f};

    // Delay buffer
    static constexpr std::size_t kDelayBufSize = 96000;
    std::vector<float> m_delayBufferL;
    std::vector<float> m_delayBufferR;
    std::size_t m_delayWritePos {0};

    // Reverb comb & allpass state
    std::array<float, 4> m_reverbCombsL {};
    std::array<float, 4> m_reverbCombsR {};

    float m_lastOutputSample {0.0f};
};

} // namespace dawg::engine
