#pragma once

#include <dawg/engine/ProcessorNode.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <string>
#include <vector>

namespace dawg::engine {

enum class FmWaveform : std::uint8_t
{
    sine,
    triangle,
    saw,
    square,
    absSine
};

struct FmOperatorConfig
{
    bool enabled {true};
    FmWaveform waveform {FmWaveform::sine};
    float ratio {1.0f};      // Harmonic frequency multiplier (0.5, 1, 2, 3...)
    float fineDetuneHz {0.0f}; // -5.0 to +5.0 Hz
    float outputLevel {0.8f}; // Modulation index or carrier amplitude

    // Envelope (4 rates, 4 levels)
    float attackRateMs {10.0f};
    float decay1RateMs {150.0f};
    float decay2RateMs {400.0f};
    float releaseRateMs {350.0f};
    float sustainLevel {0.60f};

    // Feedback amount (self-modulation, primarily op 6)
    float feedback {0.0f};
};

struct OmniFmPatch
{
    std::string name {"Solid DX7 Electric Piano"};
    std::string category {"Keys"};

    // Algorithm 1 to 32
    int algorithm {1}; // 1 = Op 6 -> 5 -> 4 -> 3 -> 2 -> 1 (cascade), etc.
    float masterFeedback {0.25f};

    std::array<FmOperatorConfig, 6> operators {};
};

class OmniFmSynthNode final : public ProcessorNode
{
public:
    OmniFmSynthNode() noexcept;
    ~OmniFmSynthNode() override = default;

    bool prepare(double sampleRate, std::size_t maxBlockSize, std::uint16_t channelCount) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;

    [[nodiscard]] std::string_view name() const noexcept { return "Yamaha OmniFM 6-Op Synth"; }

    void noteOn(std::uint8_t pitch, float velocity) noexcept;
    void noteOff(std::uint8_t pitch) noexcept;
    void allNotesOff() noexcept;

    [[nodiscard]] const OmniFmPatch& patch() const noexcept { return m_patch; }
    void setPatch(const OmniFmPatch& patch) noexcept { m_patch = patch; }
    void loadPreset(std::size_t index) noexcept;
    [[nodiscard]] std::size_t presetCount() const noexcept { return kPresets.size(); }
    [[nodiscard]] const char* presetName(std::size_t index) const noexcept;

    void setAlgorithm(int algo) noexcept { m_patch.algorithm = std::clamp(algo, 1, 32); }
    void setOpLevel(std::size_t opIdx, float lvl) noexcept
    {
        if (opIdx < 6) m_patch.operators[opIdx].outputLevel = std::clamp(lvl, 0.0f, 1.0f);
    }
    void setOpRatio(std::size_t opIdx, float ratio) noexcept
    {
        if (opIdx < 6) m_patch.operators[opIdx].ratio = std::clamp(ratio, 0.25f, 32.0f);
    }

private:
    struct OpState
    {
        float phase {0.0f};
        float lastOutput {0.0f};
        float prevOutput {0.0f};

        enum class Stage : std::uint8_t { idle, attack, decay1, decay2, sustain, release };
        Stage envStage {Stage::idle};
        float envLevel {0.0f};
    };

    struct Voice
    {
        bool active {false};
        std::uint8_t pitch {60};
        float velocity {0.8f};
        float baseFreq {261.63f};

        std::array<OpState, 6> ops {};
    };

    void processVoice(Voice& v, float& outL, float& outR) noexcept;
    float evaluateOpWave(FmWaveform wave, float phase) const noexcept;

    double m_sampleRate {48000.0};
    OmniFmPatch m_patch;

    static constexpr std::size_t kMaxVoices = 16;
    std::array<Voice, kMaxVoices> m_voices {};

    static const std::array<OmniFmPatch, 6> kPresets;
};

} // namespace dawg::engine
