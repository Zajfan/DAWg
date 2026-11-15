#pragma once

#include <dawg/engine/AudioBufferView.h>
#include <dawg/engine/ProcessorNode.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace dawg::engine {

enum class LfoWaveform : std::uint8_t
{
    sine,
    triangle,
    sawUp,
    sawDown,
    square,
    randomSampleHold
};

enum class ModSyncDivision : std::uint8_t
{
    bars4,
    bars2,
    bar1,
    half,
    quarter,
    eighth,
    sixteenth,
    thirtySecond,
    eighthTriplet,
    sixteenthTriplet
};

struct MacroTarget
{
    std::uint32_t targetId {0};
    float minVal {0.0f};
    float maxVal {1.0f};
    bool inverted {false};
};

struct MacroKnob
{
    std::string name {"Macro"};
    float value {0.0f}; // 0.0 to 1.0
    std::vector<MacroTarget> targets;

    [[nodiscard]] float evaluateTarget(std::size_t idx) const noexcept
    {
        if (idx >= targets.size())
            return value;
        const auto& t = targets[idx];
        const float norm = t.inverted ? (1.0f - value) : value;
        return t.minVal + norm * (t.maxVal - t.minVal);
    }
};

class ModulatorRack final
{
public:
    static constexpr std::size_t kNumMacros = 8;
    static constexpr std::size_t kNumLfos = 2;

    ModulatorRack() noexcept;
    ~ModulatorRack() = default;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    // Macro Access
    [[nodiscard]] MacroKnob& macro(std::size_t index) noexcept { return m_macros[index % kNumMacros]; }
    [[nodiscard]] const MacroKnob& macro(std::size_t index) const noexcept { return m_macros[index % kNumMacros]; }
    void setMacroValue(std::size_t index, float value) noexcept;

    // LFO Configuration
    void setLfoWaveform(std::size_t lfoIdx, LfoWaveform wave) noexcept;
    void setLfoSyncDivision(std::size_t lfoIdx, ModSyncDivision div) noexcept;
    void setLfoFreeRateHz(std::size_t lfoIdx, double rateHz) noexcept;
    void setLfoTempoSynced(std::size_t lfoIdx, bool synced) noexcept;
    void setLfoDepth(std::size_t lfoIdx, float depth) noexcept;
    [[nodiscard]] float lfoValue(std::size_t lfoIdx) const noexcept;

    // Envelope Follower Configuration
    void setEnvFollowerAttack(float ms) noexcept;
    void setEnvFollowerRelease(float ms) noexcept;
    void setEnvFollowerGain(float gain) noexcept;
    [[nodiscard]] float envFollowerValue() const noexcept { return m_envFollowerLevel; }

    // Step Modulator (16 Steps)
    void setStepValue(std::size_t stepIdx, float val) noexcept;
    [[nodiscard]] float stepValue(std::size_t stepIdx) const noexcept;
    void setStepDivision(ModSyncDivision div) noexcept { m_stepDivision = div; }
    [[nodiscard]] float currentStepModValue() const noexcept { return m_stepModCurrentValue; }
    [[nodiscard]] int currentStepIndex() const noexcept { return m_currentStepIdx; }

    // RT Processing
    void process(const float* const* inputAudio, std::size_t channelCount,
                 std::size_t frameCount, double bpm, std::int64_t timelineSample) noexcept;

private:
    [[nodiscard]] double syncDivisionToBeats(ModSyncDivision div) const noexcept;
    [[nodiscard]] float evaluateLfoWave(LfoWaveform wave, double phase, std::size_t lfoIdx) noexcept;

    double m_sampleRate {48000.0};
    std::array<MacroKnob, kNumMacros> m_macros;

    // LFO State
    struct LfoState
    {
        LfoWaveform wave {LfoWaveform::sine};
        ModSyncDivision division {ModSyncDivision::quarter};
        double freeRateHz {1.0};
        bool tempoSynced {true};
        float depth {1.0f};
        double phase {0.0};
        float lastOutput {0.0f};
        float shValue {0.0f};
        std::uint32_t rngSeed {12345};
    };
    std::array<LfoState, kNumLfos> m_lfos;

    // Envelope Follower State
    float m_envAttackMs {5.0f};
    float m_envReleaseMs {120.0f};
    float m_envGain {1.5f};
    float m_envFollowerLevel {0.0f};

    // Step Modulator State
    std::array<float, 16> m_stepValues {0.2f, 0.5f, 0.8f, 0.4f, 1.0f, 0.6f, 0.3f, 0.7f,
                                        0.1f, 0.4f, 0.9f, 0.5f, 0.8f, 0.3f, 0.6f, 0.0f};
    ModSyncDivision m_stepDivision {ModSyncDivision::sixteenth};
    int m_currentStepIdx {0};
    float m_stepModCurrentValue {0.0f};
};

} // namespace dawg::engine
