#pragma once

#include <dawg/engine/AudioBufferView.h>
#include <dawg/engine/MidiEvent.h>
#include <dawg/engine/ProcessorNode.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace dawg::engine::dsp {

enum class VectorTrajectoryMode : std::uint8_t
{
    manual = 0,
    orbit,
    lissajous,
    spiral,
    bounce
};

enum class VectorWaveSource : std::uint8_t
{
    virtualAnalogSaw = 0,
    virtualAnalogPulse,
    additiveHarmonicBell,
    frequencyModulation,
    texturedNoise,
    wavetableOrgan
};

struct VectorStep
{
    bool active {true};
    VectorWaveSource waveSource {VectorWaveSource::virtualAnalogSaw};
    float durationBeats {0.25f}; // 1/16th note at 4/4
    float crossfadeRatio {0.2f};  // 0.0 to 1.0 crossfade into next step
    float gateRatio {0.8f};       // 0.1 to 1.0 gate length
    float semitoneOffset {0.0f};  // Transposition offset
    float cutoffOffsetHz {0.0f};  // Filter cutoff modulation offset
};

struct VectorVoice
{
    bool active {false};
    int noteNumber {-1};
    float velocity {0.0f};
    double phaseA {0.0};
    double phaseB {0.0};
    double phaseC {0.0};
    double phaseModC {0.0};
    double phaseD {0.0};
    float baseFrequency {440.0f};
    
    // Envelope states
    float envAmp {0.0f};
    float envFilter {0.0f};
    bool inRelease {false};

    // Filter states
    float filterZ1L {0.0f};
    float filterZ2L {0.0f};
    float filterZ1R {0.0f};
    float filterZ2R {0.0f};
};

class VectorWaveSequencerNode final : public ProcessorNode
{
public:
    static constexpr std::size_t maxSteps = 16;
    static constexpr std::size_t maxVoices = 8;

    VectorWaveSequencerNode() noexcept;
    ~VectorWaveSequencerNode() override = default;

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void process(ProcessContext& context) noexcept override;
    void reset() noexcept override;

    [[nodiscard]] std::string_view name() const noexcept { return "Vector & Wave-Sequencing Synth"; }

    // 2D Vector Joystick Position (-1.0 to +1.0)
    void setVectorPosition(float x, float y) noexcept;
    [[nodiscard]] float vectorX() const noexcept { return m_vectorX.load(std::memory_order_relaxed); }
    [[nodiscard]] float vectorY() const noexcept { return m_vectorY.load(std::memory_order_relaxed); }

    // Trajectory Automation
    void setTrajectoryMode(VectorTrajectoryMode mode) noexcept { m_trajectoryMode = mode; }
    [[nodiscard]] VectorTrajectoryMode trajectoryMode() const noexcept { return m_trajectoryMode; }
    void setTrajectoryRateHz(float rateHz) noexcept { m_trajectoryRateHz = std::max(0.01f, rateHz); }
    [[nodiscard]] float trajectoryRateHz() const noexcept { return m_trajectoryRateHz; }
    void setTrajectoryDepth(float depth) noexcept { m_trajectoryDepth = std::clamp(depth, 0.0f, 1.0f); }
    [[nodiscard]] float trajectoryDepth() const noexcept { return m_trajectoryDepth; }

    // Wave-Sequencing Parameters
    void setSequencerEnabled(bool enabled) noexcept { m_sequencerEnabled = enabled; }
    [[nodiscard]] bool isSequencerEnabled() const noexcept { return m_sequencerEnabled; }
    void setStepCount(std::size_t count) noexcept;
    [[nodiscard]] std::size_t stepCount() const noexcept { return m_stepCount; }
    void setStep(std::size_t index, const VectorStep& step) noexcept;
    [[nodiscard]] const VectorStep& step(std::size_t index) const noexcept;
    [[nodiscard]] std::size_t currentStep() const noexcept { return m_currentStep; }

    // Sound Generator Settings for Quadrants
    void setQuadrantWave(std::size_t quadrantIndex, VectorWaveSource source) noexcept;
    [[nodiscard]] VectorWaveSource quadrantWave(std::size_t quadrantIndex) const noexcept;
    void setPulseWidthA(float pw) noexcept { m_pulseWidthA = std::clamp(pw, 0.05f, 0.95f); }
    void setFmIndexC(float index) noexcept { m_fmIndexC = std::max(0.0f, index); }

    // Filter & Envelope Controls
    void setFilterCutoff(float cutoffHz) noexcept { m_filterCutoff = std::clamp(cutoffHz, 20.0f, 20000.0f); }
    [[nodiscard]] float filterCutoff() const noexcept { return m_filterCutoff; }
    void setFilterResonance(float res) noexcept { m_filterResonance = std::clamp(res, 0.1f, 10.0f); }
    [[nodiscard]] float filterResonance() const noexcept { return m_filterResonance; }
    void setAttackTime(float seconds) noexcept { m_attackTime = std::max(0.001f, seconds); }
    void setReleaseTime(float seconds) noexcept { m_releaseTime = std::max(0.005f, seconds); }

    // Note Triggers
    void noteOn(int noteNumber, float velocity) noexcept;
    void noteOff(int noteNumber) noexcept;
    void allNotesOff() noexcept;
    [[nodiscard]] std::size_t activeVoiceCount() const noexcept;

    // Weight Calculation Helper
    static void computeWeights(float x, float y, float& wA, float& wB, float& wC, float& wD) noexcept;

private:
    void renderVoiceSample(VectorVoice& voice, float wA, float wB, float wC, float wD, float& outL, float& outR) noexcept;
    float synthesizeSource(VectorWaveSource source, double& phase, double& phaseMod, float freq, float pw, float fmIndex) noexcept;
    void updateTrajectory(std::size_t numFrames) noexcept;

    double m_sampleRate {48000.0};
    std::uint16_t m_channelCount {2};

    std::atomic<float> m_vectorX {0.0f};
    std::atomic<float> m_vectorY {0.0f};

    VectorTrajectoryMode m_trajectoryMode {VectorTrajectoryMode::manual};
    float m_trajectoryRateHz {0.5f};
    float m_trajectoryDepth {0.8f};
    double m_trajectoryPhase {0.0};
    float m_bounceX {0.0f};
    float m_bounceY {0.0f};
    float m_bounceVelX {0.7f};
    float m_bounceVelY {0.5f};

    bool m_sequencerEnabled {false};
    std::size_t m_stepCount {8};
    std::size_t m_currentStep {0};
    double m_stepSampleCounter {0.0};
    double m_stepSampleDuration {12000.0}; // ~0.25s at 48kHz
    float m_tempoBpm {120.0f};

    std::array<VectorStep, maxSteps> m_steps {};
    std::array<VectorWaveSource, 4> m_quadrantSources {
        VectorWaveSource::virtualAnalogSaw,
        VectorWaveSource::virtualAnalogPulse,
        VectorWaveSource::frequencyModulation,
        VectorWaveSource::additiveHarmonicBell
    };

    float m_pulseWidthA {0.5f};
    float m_fmIndexC {2.0f};
    float m_filterCutoff {2500.0f};
    float m_filterResonance {1.5f};
    float m_attackTime {0.01f};
    float m_decayTime {0.2f};
    float m_sustainLevel {0.7f};
    float m_releaseTime {0.3f};

    std::array<VectorVoice, maxVoices> m_voices {};
};

} // namespace dawg::engine::dsp
