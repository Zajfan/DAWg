#pragma once

#include <dawg/engine/ProcessorNode.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace dawg::engine {

struct SamplerPad
{
    std::string name {"Pad"};
    std::vector<float> sampleL;
    std::vector<float> sampleR;
    float pitchSemi {0.0f};      // -24.0 to +24.0 semitones
    bool reverse {false};
    bool granularScrub {false};  // Micro-grain looping & scrub texture mode
    float grainSizeMs {40.0f};   // Grain duration (10ms to 200ms)
    float startNorm {0.0f};      // 0.0 to 1.0
    float endNorm {1.0f};        // 0.0 to 1.0
    int chokeGroup {0};          // 0 = none, 1..8 = choke group
    float attackSec {0.002f};
    float decaySec {0.18f};
    float sustainLevel {1.0f};
    float releaseSec {0.25f};
    float filterCutoffHz {20000.0f};
    float filterResonance {0.707f};
    float gainLinear {1.0f};
};

class DrumPadSamplerNode final : public ProcessorNode
{
public:
    static constexpr std::size_t kNumPads = 16;
    static constexpr std::size_t kMaxVoices = 16;

    DrumPadSamplerNode() noexcept;
    ~DrumPadSamplerNode() override = default;

    [[nodiscard]] bool prepare(double sampleRate, std::size_t maxBlockSize,
                               std::uint16_t channelCount) override;
    void reset() noexcept override;
    [[nodiscard]] std::uint32_t latencySamples() const noexcept override { return 0; }
    void process(ProcessContext& context) noexcept override;

    // Direct Pad Triggering (zero RT allocations)
    void triggerPad(std::size_t padIdx, float velocity) noexcept;
    void releasePad(std::size_t padIdx) noexcept;
    void stopAll() noexcept;

    // MIDI Note mapping: C1 (36) to D#2 (51) maps to pads 0..15
    void noteOn(std::uint8_t note, float velocity) noexcept;
    void noteOff(std::uint8_t note) noexcept;

    // Pad parameter access
    [[nodiscard]] SamplerPad& pad(std::size_t padIdx) noexcept { return m_pads[padIdx % kNumPads]; }
    [[nodiscard]] const SamplerPad& pad(std::size_t padIdx) const noexcept { return m_pads[padIdx % kNumPads]; }

    void loadSampleIntoPad(std::size_t padIdx, std::vector<float> left, std::vector<float> right, std::string name);
    void sliceSampleAcrossPads(const float* data, std::size_t lengthSamples, double sampleRate, int numSlices = 16);

    [[nodiscard]] bool isPadActive(std::size_t padIdx) const noexcept;

private:
    void initFactorySounds();

    enum class EnvStage : std::uint8_t
    {
        idle,
        attack,
        decay,
        sustain,
        release
    };

    struct SamplerVoice
    {
        bool active {false};
        std::size_t padIndex {0};
        double readPos {0.0};
        double stepRate {1.0};
        float velocity {1.0f};
        EnvStage stage {EnvStage::idle};
        float envLevel {0.0f};
        float lpPrevL {0.0f};
        float lpPrevR {0.0f};
        double grainPos {0.0};
        double grainLen {1920.0};
    };

    double m_sampleRate {48000.0};
    std::array<SamplerPad, kNumPads> m_pads;
    std::array<SamplerVoice, kMaxVoices> m_voices;
};

} // namespace dawg::engine
