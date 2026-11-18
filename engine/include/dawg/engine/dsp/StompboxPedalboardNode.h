#pragma once

#include <dawg/engine/ProcessorNode.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace dawg::engine::dsp {

enum class BbdModulationMode : std::uint8_t
{
    chorus,
    flanger
};

enum class TremoloShape : std::uint8_t
{
    sine,
    triangle
};

struct OverdrivePedalParams
{
    bool enabled {true};
    float driveDb {18.0f};    // 0 .. 40 dB into the clipping stage
    float tone {0.0f};        // -1 (dark) .. +1 (bright) tilt around 1 kHz
    float levelDb {-6.0f};    // -24 .. +12 dB
    float cleanBlend {1.0f};  // 0 .. 1: TS-style keeps full clean path; lower = more saturated
};

struct BbdModulationParams
{
    bool enabled {false};
    BbdModulationMode mode {BbdModulationMode::chorus};
    float rateHz {0.8f};      // 0.05 .. 8 Hz
    float depth {0.6f};       // 0 .. 1
    float feedback {0.0f};    // -0.9 .. 0.9 (flanger resonance)
    float mix {0.5f};         // 0 .. 1
};

struct OpticalTremoloParams
{
    bool enabled {false};
    float rateHz {5.0f};      // 0.5 .. 15 Hz
    float depth {0.6f};       // 0 .. 1
    TremoloShape shape {TremoloShape::sine};
};

// Modular stompbox chain (Option 4): Overdrive -> BBD Chorus/Flanger -> Optical Tremolo.
// - Overdrive: TS-style mid-hump (720 Hz HPF into the clipper), asymmetric silicon-diode
//   transfer for even harmonics, clean blend, tilt tone EQ.
// - BBD: modulated delay line with reconstruction low-pass, soft-clipped feedback, quadrature
//   LFO between left/right.
// - Optical tremolo: incandescent lamp (squared drive) feeding an LDR with fast attack and slow
//   release, giving the classic lopsided "throb".
// process() performs no heap allocations.
class StompboxPedalboardNode final : public ProcessorNode
{
public:
    static constexpr std::size_t maxChannels = 2;
    static constexpr std::size_t bbdBufferSize = 4096;

    StompboxPedalboardNode() noexcept = default;
    ~StompboxPedalboardNode() override = default;

    [[nodiscard]] bool prepare(double sampleRate, std::size_t maxBlockSize, std::uint16_t channelCount) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;

    void setOverdrive(const OverdrivePedalParams& params) noexcept;
    void setModulation(const BbdModulationParams& params) noexcept;
    void setTremolo(const OpticalTremoloParams& params) noexcept;

    [[nodiscard]] const OverdrivePedalParams& overdrive() const noexcept { return m_od; }
    [[nodiscard]] const BbdModulationParams& modulation() const noexcept { return m_bbd; }
    [[nodiscard]] const OpticalTremoloParams& tremolo() const noexcept { return m_trem; }
    [[nodiscard]] float tremoloGain() const noexcept { return m_lastTremGain; }

    // Asymmetric silicon diode pair: hard knee at +1.0, softer/earlier at -0.71.
    [[nodiscard]] static float diodeClip(float x) noexcept;

private:
    struct ChannelState
    {
        float hpLp {0.0f};       // 720 Hz split state
        float postLp {0.0f};     // post-clip treble roll-off
        float tiltLp {0.0f};     // tone tilt split
        float dcX1 {0.0f};
        float dcY1 {0.0f};
        float bbdLp {0.0f};
        float bbdFeedback {0.0f};
        std::array<float, bbdBufferSize> bbd {};
    };

    double m_sampleRate {48000.0};
    OverdrivePedalParams m_od {};
    BbdModulationParams m_bbd {};
    OpticalTremoloParams m_trem {};

    float m_driveGain {1.0f};
    float m_levelGain {1.0f};
    float m_tiltLowGain {1.0f};
    float m_tiltHighGain {1.0f};
    float m_coef720 {0.0f};
    float m_coefPost {0.0f};
    float m_coefTilt {0.0f};
    float m_coefBbdLp {0.0f};
    float m_dcCoeff {0.995f};
    float m_ldrAttack {0.0f};
    float m_ldrRelease {0.0f};

    double m_bbdPhase {0.0};
    double m_tremPhase {0.0};
    float m_ldrState {1.0f};
    float m_lastTremGain {1.0f};
    std::size_t m_writePos {0};
    std::array<ChannelState, maxChannels> m_channels {};
};

struct CabinetMicAlignment
{
    float delaySamples {0.0f};   // Delay to apply to mic B so it lines up with mic A
    bool invertPolarity {false}; // Correlation peak was negative
    float correlation {0.0f};    // Normalised peak correlation (0 .. 1)
};

// Dual-microphone cabinet IR loader (Option 4).
// Mic A and mic B impulse responses are combined into a single IR (equal-power blend,
// per-mic level, sub-sample fractional delay and polarity on mic B), then convolved
// directly. The combined IR is double-buffered and swapped atomically so parameter changes
// never allocate or block the audio thread. Direct convolution is used, so IRs are limited
// to maxIrLength taps (~43 ms at 48 kHz) - ample for close-miked cabinets.
class DualIrCabinetNode final : public ProcessorNode
{
public:
    static constexpr std::size_t maxIrLength = 2048;
    static constexpr std::size_t maxMicDelaySamples = 256;
    static constexpr std::size_t combinedLength = maxIrLength + maxMicDelaySamples + 2;
    static constexpr std::size_t maxChannels = 2;

    DualIrCabinetNode() noexcept;
    ~DualIrCabinetNode() override = default;

    [[nodiscard]] bool prepare(double sampleRate, std::size_t maxBlockSize, std::uint16_t channelCount) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;

    // slot 0 = mic A, slot 1 = mic B. Call from the message thread.
    bool loadImpulseResponse(std::size_t slot, const float* ir, std::size_t length) noexcept;
    void setMicBlend(float blend) noexcept;              // 0 = mic A only .. 1 = mic B only
    void setMicLevelsDb(float micADb, float micBDb) noexcept;
    void setMicBDelaySamples(float samples) noexcept;    // 0 .. maxMicDelaySamples (fractional)
    void setMicBPolarityInverted(bool inverted) noexcept;
    void setOutputDb(float db) noexcept;

    [[nodiscard]] CabinetMicAlignment estimateAlignment(std::size_t maxLag = maxMicDelaySamples) const noexcept;
    void applyAutoAlignment() noexcept;

    [[nodiscard]] std::size_t combinedIrLength() const noexcept;
    [[nodiscard]] float combinedTap(std::size_t index) const noexcept;

private:
    void rebuildCombinedIr() noexcept;

    std::array<std::array<float, maxIrLength>, 2> m_irs {};
    std::array<std::size_t, 2> m_irLengths {};

    float m_blend {0.5f};
    float m_levelA {1.0f};
    float m_levelB {1.0f};
    float m_micBDelay {0.0f};
    bool m_micBInverted {false};
    float m_outputGain {1.0f};

    std::array<std::array<float, combinedLength>, 2> m_combined {};
    std::array<std::size_t, 2> m_combinedLengths {};
    std::atomic<int> m_activeIr {0};

    std::array<std::array<float, combinedLength * 2>, maxChannels> m_history {};
    std::size_t m_historyPos {0};
};

} // namespace dawg::engine::dsp
