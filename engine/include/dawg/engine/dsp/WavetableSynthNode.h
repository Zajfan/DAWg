#pragma once

#include <dawg/engine/AudioBufferView.h>
#include <dawg/engine/MidiEvent.h>
#include <dawg/engine/ProcessorNode.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dawg::engine::dsp {

enum class SpectralWarpMode : std::uint8_t
{
    none,
    sync,
    pwm,
    wavefolder,
    spectralBend
};

enum class SubOscWaveform : std::uint8_t
{
    sine,
    triangle,
    square,
    saw
};

enum class WavetableFilterMode : std::uint8_t
{
    lowpass,
    highpass,
    bandpass,
    notch
};

// Represents a 256-frame single-cycle wavetable (2048 samples per frame)
struct WavetableData
{
    static constexpr std::size_t frameSize = 2048;
    static constexpr std::size_t maxFrames = 256;

    std::size_t frameCount {64};
    // Flattened audio samples: frameCount * frameSize
    std::vector<float> samples;

    [[nodiscard]] float sample(std::size_t frameIdx, double phase) const noexcept;
    [[nodiscard]] float interpolate(double framePos, double phase) const noexcept;
};

struct WavetableOscConfig
{
    float tablePos {0.0f};                      // 0.0 to 1.0 (normalized wavetable frame position)
    SpectralWarpMode warpMode {SpectralWarpMode::none};
    float warpAmount {0.0f};                    // 0.0 to 1.0
    float semitones {0.0f};                     // -24.0 to +24.0
    float fineTuneCents {0.0f};                 // -100.0 to +100.0
    float level {0.8f};
    float pan {0.0f};
};

// Vector Wavetable Synthesizer & 2D Morphing Grid (Serum / Pigments style)
// Features 4 wavetable oscillators blended across an X-Y orbital trajectory plane,
// multi-frame wavetables, spectral warp modes (Sync, PWM, Wavefolder, Bend), and sub-oscillator.
class WavetableSynthNode final : public ProcessorNode
{
public:
    static constexpr std::size_t maxPolyphony = 16;
    static constexpr std::size_t oscCount = 4; // Osc A, B, C, D on 2D plane

    WavetableSynthNode() noexcept;
    ~WavetableSynthNode() override = default;

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void reset() noexcept override;
    [[nodiscard]] std::uint32_t latencySamples() const noexcept override { return 0; }
    void process(ProcessContext& context) noexcept override;

    // 2D Morphing Orbital Grid Controls
    void setMorphCoordinates(float x, float y) noexcept;
    [[nodiscard]] float morphX() const noexcept { return m_morphX; }
    [[nodiscard]] float morphY() const noexcept { return m_morphY; }

    void setOrbitalModulation(bool enabled, float rateHz = 0.5f, float radius = 0.5f) noexcept;
    [[nodiscard]] bool isOrbitalModulationEnabled() const noexcept { return m_orbitalEnabled; }

    // Oscillator Configuration
    void setOscConfig(std::size_t oscIdx, const WavetableOscConfig& cfg) noexcept;
    [[nodiscard]] const WavetableOscConfig& oscConfig(std::size_t oscIdx) const noexcept;
    void loadFactoryWavetable(std::size_t oscIdx, std::size_t presetType);

    // Sub-Oscillator & Noise
    void setSubOsc(bool enabled, SubOscWaveform wave, int octaveDown = 1, float level = 0.4f) noexcept;
    void setNoiseLevel(float level) noexcept { m_noiseLevel = std::clamp(level, 0.0f, 1.0f); }

    // Filter Controls
    void setFilter(WavetableFilterMode mode, float cutoffHz, float resonance, float drive = 0.0f) noexcept;

    // Envelope Parameters
    void setAmpAdsr(float aMs, float dMs, float sLevel, float rMs) noexcept;

    // MIDI Note Triggering API
    void noteOn(std::uint8_t note, std::uint8_t velocity) noexcept;
    void noteOff(std::uint8_t note) noexcept;
    void allNotesOff() noexcept;

    [[nodiscard]] std::size_t activeVoiceCount() const noexcept;

private:
    enum class EnvStage : std::uint8_t { idle, attack, decay, sustain, release };

    struct Voice
    {
        bool active {false};
        std::uint8_t note {0};
        std::uint8_t velocity {0};
        double noteFrequency {440.0};

        std::array<double, oscCount> oscPhases {0.0, 0.0, 0.0, 0.0};
        double subPhase {0.0};

        EnvStage ampStage {EnvStage::idle};
        float ampLevel {0.0f};

        // State Variable Filter states
        float svfIc1eqL {0.0f}, svfIc2eqL {0.0f};
        float svfIc1eqR {0.0f}, svfIc2eqR {0.0f};
    };

    static float applySpectralWarp(float rawPhase, SpectralWarpMode mode, float amount) noexcept;
    void generateFactoryTables();

    double m_sampleRate {48000.0};
    float m_morphX {0.0f}; // -1.0 to +1.0
    float m_morphY {0.0f}; // -1.0 to +1.0

    bool m_orbitalEnabled {false};
    float m_orbitalRateHz {0.5f};
    float m_orbitalRadius {0.5f};
    float m_orbitalPhase {0.0f};

    std::array<WavetableOscConfig, oscCount> m_oscConfigs {};
    std::array<WavetableData, oscCount> m_wavetables {};

    bool m_subEnabled {true};
    SubOscWaveform m_subWave {SubOscWaveform::sine};
    int m_subOctaveDown {1};
    float m_subLevel {0.3f};
    float m_noiseLevel {0.0f};
    std::uint32_t m_noiseSeed {54321};

    WavetableFilterMode m_filterMode {WavetableFilterMode::lowpass};
    float m_cutoffHz {5000.0f};
    float m_resonance {1.0f};
    float m_filterDrive {0.0f};

    // Amp ADSR
    float m_attackRate {0.01f};
    float m_decayRate {0.005f};
    float m_sustainLevel {0.7f};
    float m_releaseRate {0.005f};

    std::array<Voice, maxPolyphony> m_voices {};
};

} // namespace dawg::engine::dsp
