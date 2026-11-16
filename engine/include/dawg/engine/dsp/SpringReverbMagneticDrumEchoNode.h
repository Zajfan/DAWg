#pragma once

#include <dawg/engine/AudioBufferView.h>
#include <dawg/engine/ProcessorNode.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dawg::engine::dsp {

enum class SpringDrumMode : std::uint8_t
{
    springOnly = 0,         // Dispersive coiled transmission spring reverb only (Fender 6G15 style)
    drumEchoOnly = 1,       // 4-head rotating magnetic drum echo only (Binson Echorec style)
    seriesDrumToSpring = 2, // Preamp -> Magnetic Drum Delay -> Spring Reverb
    parallel = 3            // Preamp -> [Drum Delay + Spring Reverb in parallel]
};

// Analog Spring Reverb & Magnetic Drum Echo Physical Simulation
// Models dispersive dual transmission springs, 4-tap rotating magnetic drum delay,
// physical wow/flutter motor drag, mechanical motor rumble, and tube/germanium preamp drive.
class SpringReverbMagneticDrumEchoNode final : public ProcessorNode
{
public:
    static constexpr std::size_t maxDelaySamples = 192000; // ~4 seconds at 48kHz
    static constexpr std::size_t dispersionStages = 6;      // Allpass dispersion cascade stages per spring
    static constexpr std::size_t numHeads = 4;

    SpringReverbMagneticDrumEchoNode() noexcept;
    ~SpringReverbMagneticDrumEchoNode() override = default;

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void reset() noexcept override;
    [[nodiscard]] std::uint32_t latencySamples() const noexcept override { return 0; }
    void process(ProcessContext& context) noexcept override;

    // Operating mode
    void setMode(SpringDrumMode mode) noexcept { m_mode = mode; }
    [[nodiscard]] SpringDrumMode mode() const noexcept { return m_mode; }

    // Wet / Dry mix: 0.0 (100% dry) to 1.0 (100% wet)
    void setMix(float mix) noexcept { m_mix = std::clamp(mix, 0.0f, 1.0f); }
    [[nodiscard]] float mix() const noexcept { return m_mix; }

    // Preamp Drive: 0.0 dB (clean) to 24.0 dB (warm germanium/tube overdrive)
    void setPreampDriveDb(float driveDb) noexcept;
    [[nodiscard]] float preampDriveDb() const noexcept { return m_preampDriveDb; }

    // Mechanical motor rumble injection: 0.0 to 1.0
    void setMotorRumble(float amount) noexcept { m_motorRumble = std::clamp(amount, 0.0f, 1.0f); }
    [[nodiscard]] float motorRumble() const noexcept { return m_motorRumble; }

    // --- Dispersive Spring Reverb Parameters ---
    // Spring Decay time (T60 in seconds): 0.5s to 6.0s
    void setSpringDecay(float decaySec) noexcept;
    [[nodiscard]] float springDecay() const noexcept { return m_springDecaySec; }

    // Spring Dispersion / "Boing" chirp intensity: 0.0 (no chirp) to 1.0 (extreme metallic chirp)
    void setSpringDispersion(float dispersion) noexcept;
    [[nodiscard]] float springDispersion() const noexcept { return m_springDispersion; }

    // Spring Rattle / Non-linear excitation: 0.0 (linear) to 1.0 (clattering metallic crash)
    void setSpringRattle(float rattle) noexcept { m_springRattle = std::clamp(rattle, 0.0f, 1.0f); }
    [[nodiscard]] float springRattle() const noexcept { return m_springRattle; }

    // Spring Tone / High-frequency damping: 0.0 (dark) to 1.0 (bright)
    void setSpringTone(float tone) noexcept;
    [[nodiscard]] float springTone() const noexcept { return m_springTone; }

    // --- Magnetic Drum Echo Parameters ---
    // Drum Motor Speed (fundamental rotation time): 50.0 ms to 800.0 ms
    void setDrumSpeedMs(float speedMs) noexcept;
    [[nodiscard]] float drumSpeedMs() const noexcept { return m_drumSpeedMs; }

    // Feedback: 0.0 (single repeat) to 1.3 (runaway self-oscillating magnetic bloom)
    void setFeedback(float feedback) noexcept { m_feedback = std::clamp(feedback, 0.0f, 1.3f); }
    [[nodiscard]] float feedback() const noexcept { return m_feedback; }

    // Playback Heads (1 to 4) configuration
    void setHeadEnabled(std::uint8_t headIndex, bool enabled) noexcept;
    [[nodiscard]] bool isHeadEnabled(std::uint8_t headIndex) const noexcept;

    void setHeadPan(std::uint8_t headIndex, float pan) noexcept; // -1.0 (left) to +1.0 (right)
    [[nodiscard]] float headPan(std::uint8_t headIndex) const noexcept;

    void setHeadLevel(std::uint8_t headIndex, float level) noexcept; // 0.0 to 1.0
    [[nodiscard]] float headLevel(std::uint8_t headIndex) const noexcept;

    // Wow & Flutter: 0.0 (pristine digital) to 1.0 (heavily degraded vintage motor drag)
    void setWowFlutter(float amount) noexcept { m_wowFlutter = std::clamp(amount, 0.0f, 1.0f); }
    [[nodiscard]] float wowFlutter() const noexcept { return m_wowFlutter; }

    // Spring vs Drum blend in parallel mode: 0.0 (drum only) to 1.0 (spring only)
    void setBlend(float blend) noexcept { m_blend = std::clamp(blend, 0.0f, 1.0f); }
    [[nodiscard]] float blend() const noexcept { return m_blend; }

private:
    struct AllpassStage
    {
        float coefficient {0.6f};
        float z1 {0.0f};

        inline float process(float in) noexcept
        {
            const float out = -coefficient * in + z1;
            z1 = in + coefficient * out;
            return out;
        }

        inline void reset() noexcept
        {
            z1 = 0.0f;
        }
    };

    struct HeadConfig
    {
        bool enabled {true};
        float pan {0.0f};   // -1.0 to +1.0
        float level {1.0f}; // 0.0 to 1.0
    };

    double m_sampleRate {48000.0};
    SpringDrumMode m_mode {SpringDrumMode::seriesDrumToSpring};
    float m_mix {0.35f};
    float m_preampDriveDb {6.0f};
    float m_preampLinearGain {2.0f};
    float m_motorRumble {0.15f};
    float m_blend {0.5f};

    // Preamp DC blocker state
    float m_dcBlockerIn {0.0f};
    float m_dcBlockerOut {0.0f};

    // Motor rumble oscillator state
    float m_rumblePhase {0.0f};

    // Spring Reverb state
    float m_springDecaySec {2.5f};
    float m_springDispersion {0.70f};
    float m_springRattle {0.30f};
    float m_springTone {0.65f};
    float m_springFeedbackGain {0.75f};
    float m_springDampingCoeff {0.35f};

    // Dual physical coiled transmission springs
    std::array<AllpassStage, dispersionStages> m_dispersionSpringL {};
    std::array<AllpassStage, dispersionStages> m_dispersionSpringR {};

    std::vector<float> m_springDelayBufL;
    std::vector<float> m_springDelayBufR;
    std::size_t m_springDelayLenL {1488}; // ~31ms at 48kHz
    std::size_t m_springDelayLenR {1872}; // ~39ms at 48kHz
    std::size_t m_springPosL {0};
    std::size_t m_springPosR {0};
    float m_springFilterStateL {0.0f};
    float m_springFilterStateR {0.0f};

    // Magnetic Drum Echo state
    float m_drumSpeedMs {320.0f};
    float m_feedback {0.45f};
    float m_wowFlutter {0.25f};

    std::array<HeadConfig, numHeads> m_heads {};

    std::vector<float> m_drumBufferL;
    std::vector<float> m_drumBufferR;
    std::size_t m_drumWritePos {0};

    // Wow & Flutter LFO phases
    float m_wowPhase {0.0f};
    float m_flutterPhase {0.0f};

    // Tape head feedback lowpass filter state
    float m_feedbackFilterStateL {0.0f};
    float m_feedbackFilterStateR {0.0f};
};

} // namespace dawg::engine::dsp
