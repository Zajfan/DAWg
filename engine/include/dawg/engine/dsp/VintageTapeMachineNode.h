#pragma once

#include <dawg/engine/ProcessorNode.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dawg::engine::dsp {

enum class TapeMachineSpeed : std::uint8_t
{
    ips7_5,
    ips15,
    ips30
};

enum class TapeEqStandard : std::uint8_t
{
    nab, // NAB (US): 7.5/15 ips = 50 us + 3180 us LF, 30 ips = 17.5 us (AES)
    iec  // IEC/CCIR (EU): 7.5 ips = 70 us, 15 ips = 35 us, 30 ips = 17.5 us
};

enum class TapeReferenceTone : std::uint8_t
{
    off,
    hz100,
    hz1k,
    hz10k
};

// First-order shelf section H(s) = g * (1 + s*tau1) / (1 + s*tau2), bilinear transform.
struct TapeShelfFilter
{
    float b0 {1.0f};
    float b1 {0.0f};
    float a1 {0.0f};
    float x1 {0.0f};
    float y1 {0.0f};

    void design(double sampleRate, float gain, float tau1, float tau2) noexcept;
    float process(float x) noexcept
    {
        const float y = b0 * x + b1 * x1 - a1 * y1;
        x1 = x;
        y1 = y;
        return y;
    }
    void reset() noexcept { x1 = 0.0f; y1 = 0.0f; }
};

// Vintage analog tape machine (Option 2).
// Signal flow per channel:
//   drive -> record pre-emphasis (LF + HF) -> magnetic hysteresis -> tape hiss
//   -> playback de-emphasis -> gap-loss low-pass -> head-bump peak -> capstan wow/flutter
//   -> DC blocker -> output trim
// Because pre- and de-emphasis are exact inverses, the machine is flat at low level while
// high frequencies saturate earlier when driven - the characteristic tape HF compression.
// process() performs no heap allocations.
class VintageTapeMachineNode final : public ProcessorNode
{
public:
    static constexpr std::size_t maxChannels = 2;
    static constexpr std::size_t delayBufferSize = 8192;

    VintageTapeMachineNode() noexcept = default;
    ~VintageTapeMachineNode() override = default;

    [[nodiscard]] bool prepare(double sampleRate, std::size_t maxBlockSize, std::uint16_t channelCount) override;
    void reset() noexcept override;
    [[nodiscard]] std::uint32_t latencySamples() const noexcept override;
    void process(ProcessContext& context) noexcept override;

    void setSpeed(TapeMachineSpeed speed) noexcept { m_speed = speed; updateFilters(); }
    [[nodiscard]] TapeMachineSpeed speed() const noexcept { return m_speed; }

    void setEqStandard(TapeEqStandard eq) noexcept { m_eq = eq; updateFilters(); }
    [[nodiscard]] TapeEqStandard eqStandard() const noexcept { return m_eq; }

    void setInputDriveDb(float db) noexcept;            // 0 .. +24 dB
    void setBias(float bias) noexcept;                  // 0 = under-biased (wide loop) .. 1 = over-biased
    void setAsymmetry(float amount) noexcept;           // 0 .. 1: even-harmonic content
    void setHeadBumpDb(float db) noexcept;              // 0 .. +6 dB
    void setWow(float depth, float rateHz) noexcept;    // depth 0..1, rate 0.5..2 Hz
    void setFlutter(float depth, float rateHz) noexcept;// depth 0..1, rate 6..15 Hz
    void setHissDb(float db) noexcept;                  // -120 (off) .. -40 dBFS
    void setReferenceTone(TapeReferenceTone tone) noexcept { m_tone = tone; }
    void setOutputTrimDb(float db) noexcept;

    [[nodiscard]] float headBumpFrequencyHz() const noexcept;
    [[nodiscard]] float highFrequencyTimeConstantUs() const noexcept;
    [[nodiscard]] float lowFrequencyTimeConstantUs() const noexcept;

    // Magnetic hysteresis transfer: a Preisach "play operator" (backlash of half-width `width`)
    // blended into a tanh magnetisation curve. Small signals stay linear (the play state does not
    // move), larger excursions trace a loop whose width shrinks as bias increases.
    // Returns the magnetisation and updates playState. Exposed for tests/visualisation.
    [[nodiscard]] static float hysteresis(float h, float& playState, float width, float loopWeight,
                                          float asymmetry) noexcept;

private:
    struct Biquad
    {
        float b0 {1.0f}, b1 {0.0f}, b2 {0.0f}, a1 {0.0f}, a2 {0.0f};
        float z1 {0.0f}, z2 {0.0f};
        float process(float x) noexcept
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        void reset() noexcept { z1 = 0.0f; z2 = 0.0f; }
    };

    struct ChannelState
    {
        TapeShelfFilter preLf, preHf, deHf, deLf;
        Biquad headBump;
        float gapLossState {0.0f};
        float playState {0.0f};
        float dcX1 {0.0f};
        float dcY1 {0.0f};
        std::array<float, delayBufferSize> delay {};
    };

    void updateFilters() noexcept;
    float nextNoise() noexcept;

    double m_sampleRate {48000.0};
    TapeMachineSpeed m_speed {TapeMachineSpeed::ips15};
    TapeEqStandard m_eq {TapeEqStandard::nab};
    TapeReferenceTone m_tone {TapeReferenceTone::off};

    float m_driveGain {1.0f};
    float m_bias {0.6f};
    float m_asymmetry {0.1f};
    float m_headBumpDb {2.5f};
    float m_wowDepth {0.0f};
    float m_wowRateHz {0.8f};
    float m_flutterDepth {0.0f};
    float m_flutterRateHz {9.0f};
    float m_hissGain {0.0f};
    float m_outputTrim {1.0f};

    float m_gapLossCoeff {1.0f};
    float m_dcCoeff {0.995f};
    double m_wowPhase {0.0};
    double m_flutterPhase {0.0};
    double m_tonePhase {0.0};
    float m_wowDrift {0.0f};
    std::uint64_t m_rng {0x9E3779B97F4A7C15ull};
    std::size_t m_writePos {0};

    std::array<ChannelState, maxChannels> m_channels {};
};

} // namespace dawg::engine::dsp
