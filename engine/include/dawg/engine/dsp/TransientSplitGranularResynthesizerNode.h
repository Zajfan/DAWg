#pragma once

#include <dawg/engine/AudioBufferView.h>
#include <dawg/engine/ProcessorNode.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dawg::engine::dsp {

enum class GranularWindowType : std::uint8_t
{
    hann,
    gaussian,
    blackman
};

enum class PitchScatterMode : std::uint8_t
{
    free,              // Continuous detuning in cents
    octaves,           // Octave jumps (-2, -1, 0, +1, +2)
    fifthsAndOctaves,  // Perfect fifths and octaves (-12, -5, 0, +7, +12)
    chordHarmonics     // Major/minor chord intervals (0, 3, 4, 7, 10, 12, 16)
};

// Transient-Split Polyphonic Granular Resynthesizer (SpecOps / Portal / Granulator style)
class TransientSplitGranularResynthesizerNode final : public ProcessorNode
{
public:
    static constexpr std::size_t MaxGrains = 64;
    static constexpr std::size_t BufferSeconds = 4;

    TransientSplitGranularResynthesizerNode() noexcept;
    ~TransientSplitGranularResynthesizerNode() override = default;

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void reset() noexcept override;
    [[nodiscard]] std::uint32_t latencySamples() const noexcept override { return 0; }
    void process(ProcessContext& context) noexcept override;

    // Transient-Sustain Split Controls
    void setTransientSensitivity(float sens) noexcept { m_transientSens = std::clamp(sens, 0.0f, 1.0f); }
    [[nodiscard]] float transientSensitivity() const noexcept { return m_transientSens; }

    void setTransientGain(float gain) noexcept { m_transientGain = std::clamp(gain, 0.0f, 2.0f); }
    [[nodiscard]] float transientGain() const noexcept { return m_transientGain; }

    void setSustainGain(float gain) noexcept { m_sustainGain = std::clamp(gain, 0.0f, 2.0f); }
    [[nodiscard]] float sustainGain() const noexcept { return m_sustainGain; }

    // Grain Cloud Parameters
    void setGrainSizeMs(float ms) noexcept { m_grainSizeMs = std::clamp(ms, 5.0f, 500.0f); }
    [[nodiscard]] float grainSizeMs() const noexcept { return m_grainSizeMs; }

    void setGrainDensity(float grainsPerSec) noexcept { m_density = std::clamp(grainsPerSec, 2.0f, 120.0f); }
    [[nodiscard]] float grainDensity() const noexcept { return m_density; }

    void setPitchScatterMode(PitchScatterMode mode) noexcept { m_pitchMode = mode; }
    [[nodiscard]] PitchScatterMode pitchScatterMode() const noexcept { return m_pitchMode; }

    void setPitchScatterAmount(float amount) noexcept { m_pitchScatterAmount = std::clamp(amount, 0.0f, 1.0f); }
    [[nodiscard]] float pitchScatterAmount() const noexcept { return m_pitchScatterAmount; }

    void setSpatialPanJitter(float jitter) noexcept { m_panJitter = std::clamp(jitter, 0.0f, 1.0f); }
    [[nodiscard]] float spatialPanJitter() const noexcept { return m_panJitter; }

    void setWindowShape(GranularWindowType shape) noexcept { m_windowType = shape; }
    [[nodiscard]] GranularWindowType windowShape() const noexcept { return m_windowType; }

    // Feedback Resynthesis
    void setFeedback(float feedback) noexcept { m_feedback = std::clamp(feedback, 0.0f, 0.95f); }
    [[nodiscard]] float feedback() const noexcept { return m_feedback; }

    // Dry / Wet Mix
    void setMix(float mix) noexcept { m_mix = std::clamp(mix, 0.0f, 1.0f); }
    [[nodiscard]] float mix() const noexcept { return m_mix; }

private:
    struct Grain
    {
        bool active {false};
        double readPos {0.0};
        double playbackRate {1.0};
        float currentSample {0.0f};
        float totalSamples {1000.0f};
        float panL {0.5f};
        float panR {0.5f};
    };

    void spawnGrain(std::size_t writePos, std::size_t bufferSize) noexcept;
    [[nodiscard]] float calculateWindow(float phaseNormalized) const noexcept;
    [[nodiscard]] float generateRandomFloat() noexcept;

    double m_sampleRate {48000.0};

    float m_transientSens {0.5f};
    float m_transientGain {1.0f};
    float m_sustainGain {1.0f};

    float m_grainSizeMs {80.0f};
    float m_density {35.0f};
    PitchScatterMode m_pitchMode {PitchScatterMode::fifthsAndOctaves};
    float m_pitchScatterAmount {0.6f};
    float m_panJitter {0.7f};
    GranularWindowType m_windowType {GranularWindowType::hann};
    float m_feedback {0.3f};
    float m_mix {0.8f};

    // Transient Split Differential Ballistics
    float m_fastEnv {0.0f};
    float m_slowEnv {0.0f};
    float m_fastCoeff {0.1f};
    float m_slowCoeff {0.01f};

    // Circular recording buffer for granular slicing
    std::vector<float> m_historyBufferL;
    std::vector<float> m_historyBufferR;
    std::size_t m_writeIndex {0};

    // Pre-allocated Grain Pool
    std::array<Grain, MaxGrains> m_grains {};
    float m_grainSpawnCounter {0.0f};

    // Tuned Feedback Comb Filter
    static constexpr std::size_t CombSize = 2048;
    std::array<float, CombSize> m_combBufferL {};
    std::array<float, CombSize> m_combBufferR {};
    std::size_t m_combIdx {0};

    std::uint32_t m_rngSeed {987654321};
};

} // namespace dawg::engine::dsp
