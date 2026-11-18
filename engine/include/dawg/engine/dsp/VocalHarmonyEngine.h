#pragma once

#include <dawg/engine/ProcessorNode.h>
#include <dawg/engine/dsp/ChordPadProgressionEngine.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace dawg::engine::dsp {

struct VocalHarmonyVoice
{
    bool enabled {true};
    int scaleDegreeOffset {2};  // Diatonic steps relative to the sung note (+2 = third above)
    int manualMidiNote {-1};    // >= 0 overrides scale-locking with a fixed MIDI target
    float gain {0.7f};
    float pan {0.0f};           // -1 (left) .. +1 (right)
    float formantSemitones {0.0f};
};

struct VocalHarmonyResult
{
    std::vector<float> left;
    std::vector<float> right;
};

// Multi-part vocal harmonizer & formant shifter (Option 3) - offline / render-time engine.
// Uses pitch-synchronous overlap-add (TD-PSOLA) driven by a YIN-style pitch tracker:
// - Pitch shifting moves grain spacing only, so formants are preserved.
// - Formant shifting resamples each grain while keeping grain spacing, so pitch is preserved.
// Harmony voices are scale-locked: every analysis period the sung pitch is snapped into the
// key/scale and moved by N diatonic degrees, so harmonies stay in key (and in tune) even if
// the lead drifts. Allocates; intended for render / bounce workflows, not the audio thread.
class VocalHarmonyEngine final
{
public:
    VocalHarmonyEngine() = delete;

    static constexpr double minF0Hz = 70.0;
    static constexpr double maxF0Hz = 1000.0;

    [[nodiscard]] static double detectFrameF0(const float* samples, std::size_t numSamples, double sampleRate) noexcept;
    [[nodiscard]] static std::vector<float> detectPitchTrack(const float* samples, std::size_t numSamples,
                                                             double sampleRate, std::size_t hopSize = 512);

    [[nodiscard]] static int snapToScale(int midiNote, std::uint8_t rootKey, ChordScale scale) noexcept;
    [[nodiscard]] static int shiftByScaleDegrees(int midiNote, std::uint8_t rootKey, ChordScale scale, int degrees) noexcept;

    [[nodiscard]] static std::vector<float> pitchShift(const float* samples, std::size_t numSamples, double sampleRate,
                                                       float semitones, float formantSemitones = 0.0f);
    [[nodiscard]] static std::vector<float> shiftFormants(const float* samples, std::size_t numSamples,
                                                          double sampleRate, float formantSemitones);

    [[nodiscard]] static VocalHarmonyResult renderHarmonies(const float* samples, std::size_t numSamples,
                                                            double sampleRate, std::uint8_t rootKey, ChordScale scale,
                                                            std::span<const VocalHarmonyVoice> voices, float dryGain = 1.0f);

    // Soprano (+5th), Alto (+3rd), Tenor (-4th), Bass (-octave)
    [[nodiscard]] static std::array<VocalHarmonyVoice, 4> defaultSatbVoices() noexcept;
};

// Real-time stereo vocal doubler (Option 3): two Haas-delayed copies with independent
// micro-detune (delay modulation, +/- cents) and slow random timing jitter.
// process() performs no heap allocations.
class VocalDoublerNode final : public ProcessorNode
{
public:
    static constexpr std::size_t delayBufferSize = 16384;

    VocalDoublerNode() noexcept = default;
    ~VocalDoublerNode() override = default;

    [[nodiscard]] bool prepare(double sampleRate, std::size_t maxBlockSize, std::uint16_t channelCount) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;

    void setHaasDelayMs(float ms) noexcept;       // 5 .. 40 ms
    void setDetuneCents(float cents) noexcept;    // 0 .. 25 cents
    void setDetuneRateHz(float hz) noexcept;      // 0.2 .. 2 Hz
    void setJitterMs(float ms) noexcept;          // 0 .. 10 ms
    void setWidth(float width) noexcept;          // 0 .. 1
    void setDoubleLevel(float level) noexcept;    // 0 .. 1
    void setDryLevel(float level) noexcept;       // 0 .. 1

    // Peak delay modulation (seconds) that yields the configured +/- cents at the configured rate
    [[nodiscard]] float modulationAmplitudeSec() const noexcept;

private:
    float nextNoise() noexcept;

    double m_sampleRate {48000.0};
    float m_haasMs {14.0f};
    float m_detuneCents {8.0f};
    float m_detuneRateHz {0.35f};
    float m_jitterMs {2.0f};
    float m_width {1.0f};
    float m_doubleLevel {0.7f};
    float m_dryLevel {1.0f};

    std::array<double, 2> m_lfoPhase {0.0, 1.5707963267948966};
    std::array<float, 2> m_jitterTarget {};
    std::array<float, 2> m_jitterValue {};
    std::uint32_t m_jitterCountdown {0};
    std::uint64_t m_rng {0xD1B54A32D192ED03ull};
    std::size_t m_writePos {0};
    std::array<float, delayBufferSize> m_buffer {};
};

} // namespace dawg::engine::dsp
