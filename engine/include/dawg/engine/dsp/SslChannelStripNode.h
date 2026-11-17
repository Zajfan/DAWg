#pragma once

#include <dawg/engine/AudioBufferView.h>
#include <dawg/engine/ProcessorNode.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>

namespace dawg::engine::dsp {

enum class PanLaw : std::uint8_t
{
    equalPower3dB,  // Standard -3.0 dB center attenuation
    equalPower4_5dB,// Compromise / film -4.5 dB center attenuation
    linear6dB,      // Linear acoustic sum -6.0 dB center attenuation
    constant0dB     // 0 dB center attenuation
};

struct StereoPanLawResult
{
    float gainL {1.0f};
    float gainR {1.0f};
};

[[nodiscard]] constexpr StereoPanLawResult calculatePanLawGains(float pan, PanLaw law) noexcept
{
    const float p = std::clamp(pan, -1.0f, 1.0f); // -1 (L) to +1 (R)
    constexpr float kPi = std::numbers::pi_v<float>;

    switch (law)
    {
    case PanLaw::equalPower3dB:
    {
        // Equal power 3dB: left = cos(theta), right = sin(theta), theta from 0 to pi/2
        const float theta = (p + 1.0f) * 0.25f * kPi;
        return {std::cos(theta), std::sin(theta)};
    }
    case PanLaw::equalPower4_5dB:
    {
        // 4.5 dB center dip
        const float theta = (p + 1.0f) * 0.25f * kPi;
        // Blend between 3dB equal power and 6dB linear
        const float cosVal = std::cos(theta);
        const float sinVal = std::sin(theta);
        const float linL = 0.5f * (1.0f - p);
        const float linR = 0.5f * (1.0f + p);
        return {0.5f * (cosVal + linL * std::numbers::sqrt2_v<float>),
                0.5f * (sinVal + linR * std::numbers::sqrt2_v<float>)};
    }
    case PanLaw::linear6dB:
    {
        // Linear 6dB: left = (1 - pan) / 2, right = (1 + pan) / 2
        return {0.5f * (1.0f - p), 0.5f * (1.0f + p)};
    }
    case PanLaw::constant0dB:
    {
        // Constant 0dB: left is 1.0 at center, drops to 0 at +1; right is 1.0 at center, drops to 0 at -1
        return {p <= 0.0f ? 1.0f : (1.0f - p),
                p >= 0.0f ? 1.0f : (1.0f + p)};
    }
    }
    return {1.0f, 1.0f};
}

// SSL 4000 G-Series Channel Strip Processor
// Featuring 4-band proportional-Q equalizer, high/low pass console filters,
// preamp saturation stage, stereo width/balance, phase inversion, and pan law fader.
class SslChannelStripNode final : public ProcessorNode
{
public:
    SslChannelStripNode() noexcept;
    ~SslChannelStripNode() override = default;

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void reset() noexcept override;
    [[nodiscard]] std::uint32_t latencySamples() const noexcept override { return 0; }
    void process(ProcessContext& context) noexcept override;

    // High-Pass Filter (18 dB/oct 3rd order Butterworth: 16 Hz - 350 Hz)
    void setHpfEnabled(bool enabled) noexcept { m_hpfEnabled = enabled; }
    [[nodiscard]] bool hpfEnabled() const noexcept { return m_hpfEnabled; }
    void setHpfFrequency(float freqHz) noexcept { m_hpfFreq = std::clamp(freqHz, 16.0f, 350.0f); updateFilters(); }
    [[nodiscard]] float hpfFrequency() const noexcept { return m_hpfFreq; }

    // Low-Pass Filter (12 dB/oct 2nd order: 3 kHz - 20 kHz)
    void setLpfEnabled(bool enabled) noexcept { m_lpfEnabled = enabled; }
    [[nodiscard]] bool lpfEnabled() const noexcept { return m_lpfEnabled; }
    void setLpfFrequency(float freqHz) noexcept { m_lpfFreq = std::clamp(freqHz, 3000.0f, 20000.0f); updateFilters(); }
    [[nodiscard]] float lpfFrequency() const noexcept { return m_lpfFreq; }

    // Low Band (LF): 30 Hz - 450 Hz, +/-16.5 dB, switchable Bell / Shelf
    void setLfGainDb(float gainDb) noexcept { m_lfGainDb = std::clamp(gainDb, -16.5f, 16.5f); updateFilters(); }
    [[nodiscard]] float lfGainDb() const noexcept { return m_lfGainDb; }
    void setLfFrequency(float freqHz) noexcept { m_lfFreq = std::clamp(freqHz, 30.0f, 450.0f); updateFilters(); }
    [[nodiscard]] float lfFrequency() const noexcept { return m_lfFreq; }
    void setLfBell(bool isBell) noexcept { m_lfBell = isBell; updateFilters(); }
    [[nodiscard]] bool lfBell() const noexcept { return m_lfBell; }

    // Low-Mid Band (LMF): 200 Hz - 2.5 kHz, +/-18 dB, Proportional Q (0.5 to 3.0), x3 switch
    void setLmfGainDb(float gainDb) noexcept { m_lmfGainDb = std::clamp(gainDb, -18.0f, 18.0f); updateFilters(); }
    [[nodiscard]] float lmfGainDb() const noexcept { return m_lmfGainDb; }
    void setLmfFrequency(float freqHz) noexcept { m_lmfFreq = std::clamp(freqHz, 200.0f, 2500.0f); updateFilters(); }
    [[nodiscard]] float lmfFrequency() const noexcept { return m_lmfFreq; }
    void setLmfQ(float q) noexcept { m_lmfQ = std::clamp(q, 0.5f, 4.0f); updateFilters(); }
    [[nodiscard]] float lmfQ() const noexcept { return m_lmfQ; }
    void setLmfMult3x(bool mult3x) noexcept { m_lmfMult3x = mult3x; updateFilters(); }
    [[nodiscard]] bool lmfMult3x() const noexcept { return m_lmfMult3x; }

    // High-Mid Band (HMF): 600 Hz - 7.0 kHz, +/-18 dB, Proportional Q (0.5 to 3.0), /3 switch
    void setHmfGainDb(float gainDb) noexcept { m_hmfGainDb = std::clamp(gainDb, -18.0f, 18.0f); updateFilters(); }
    [[nodiscard]] float hmfGainDb() const noexcept { return m_hmfGainDb; }
    void setHmfFrequency(float freqHz) noexcept { m_hmfFreq = std::clamp(freqHz, 600.0f, 7000.0f); updateFilters(); }
    [[nodiscard]] float hmfFrequency() const noexcept { return m_hmfFreq; }
    void setHmfQ(float q) noexcept { m_hmfQ = std::clamp(q, 0.5f, 4.0f); updateFilters(); }
    [[nodiscard]] float hmfQ() const noexcept { return m_hmfQ; }
    void setHmfDiv3(bool div3) noexcept { m_hmfDiv3 = div3; updateFilters(); }
    [[nodiscard]] bool hmfDiv3() const noexcept { return m_hmfDiv3; }

    // High Band (HF): 1.5 kHz - 16 kHz, +/-16.5 dB, switchable Bell / Shelf
    void setHfGainDb(float gainDb) noexcept { m_hfGainDb = std::clamp(gainDb, -16.5f, 16.5f); updateFilters(); }
    [[nodiscard]] float hfGainDb() const noexcept { return m_hfGainDb; }
    void setHfFrequency(float freqHz) noexcept { m_hfFreq = std::clamp(freqHz, 1500.0f, 16000.0f); updateFilters(); }
    [[nodiscard]] float hfFrequency() const noexcept { return m_hfFreq; }
    void setHfBell(bool isBell) noexcept { m_hfBell = isBell; updateFilters(); }
    [[nodiscard]] bool hfBell() const noexcept { return m_hfBell; }

    // Preamp Drive & Analog Console Saturation
    void setDrive(float drive) noexcept { m_drive = std::clamp(drive, 0.0f, 1.0f); }
    [[nodiscard]] float drive() const noexcept { return m_drive; }

    // Pan Law & Stereo Balance
    void setPanLaw(PanLaw law) noexcept { m_panLaw = law; }
    [[nodiscard]] PanLaw panLaw() const noexcept { return m_panLaw; }

    void setPan(float pan) noexcept { m_pan = std::clamp(pan, -1.0f, 1.0f); }
    [[nodiscard]] float pan() const noexcept { return m_pan; }

    void setFaderGainDb(float gainDb) noexcept { m_faderGainDb = std::clamp(gainDb, -100.0f, 12.0f); }
    [[nodiscard]] float faderGainDb() const noexcept { return m_faderGainDb; }

    // Stereo Width: 0.0 (mono) to 1.0 (normal stereo) to 2.0 (super-wide mid/side)
    void setStereoWidth(float width) noexcept { m_width = std::clamp(width, 0.0f, 2.0f); }
    [[nodiscard]] float stereoWidth() const noexcept { return m_width; }

    // Phase Invert
    void setPhaseInvertL(bool invert) noexcept { m_phaseInvertL = invert; }
    [[nodiscard]] bool phaseInvertL() const noexcept { return m_phaseInvertL; }
    void setPhaseInvertR(bool invert) noexcept { m_phaseInvertR = invert; }
    [[nodiscard]] bool phaseInvertR() const noexcept { return m_phaseInvertR; }

    // Mute & Solo
    void setMute(bool mute) noexcept { m_muted = mute; }
    [[nodiscard]] bool isMuted() const noexcept { return m_muted; }

private:
    struct BiquadState
    {
        double x1 {0.0};
        double x2 {0.0};
        double y1 {0.0};
        double y2 {0.0};

        void reset() noexcept
        {
            x1 = 0.0;
            x2 = 0.0;
            y1 = 0.0;
            y2 = 0.0;
        }

        [[nodiscard]] inline double process(double in, double b0, double b1, double b2, double a1, double a2) noexcept
        {
            const double out = b0 * in + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1;
            x1 = in;
            y2 = y1;
            y1 = out;
            return out;
        }
    };

    struct FilterCoeffs
    {
        double b0 {1.0};
        double b1 {0.0};
        double b2 {0.0};
        double a1 {0.0};
        double a2 {0.0};
    };

    void updateFilters() noexcept;

    double m_sampleRate {44100.0};

    // Filter parameters
    bool m_hpfEnabled {false};
    float m_hpfFreq {30.0f};
    bool m_lpfEnabled {false};
    float m_lpfFreq {18000.0f};

    float m_lfGainDb {0.0f};
    float m_lfFreq {100.0f};
    bool m_lfBell {false};

    float m_lmfGainDb {0.0f};
    float m_lmfFreq {800.0f};
    float m_lmfQ {1.0f};
    bool m_lmfMult3x {false};

    float m_hmfGainDb {0.0f};
    float m_hmfFreq {3000.0f};
    float m_hmfQ {1.0f};
    bool m_hmfDiv3 {false};

    float m_hfGainDb {0.0f};
    float m_hfFreq {10000.0f};
    bool m_hfBell {false};

    // Saturation & Console Fader
    float m_drive {0.0f};
    PanLaw m_panLaw {PanLaw::equalPower3dB};
    float m_pan {0.0f};
    float m_faderGainDb {0.0f};
    float m_width {1.0f};
    bool m_phaseInvertL {false};
    bool m_phaseInvertR {false};
    bool m_muted {false};

    // Filter Coefficients
    FilterCoeffs m_hpfCoeffs1 {};
    FilterCoeffs m_hpfCoeffs2 {};
    FilterCoeffs m_lpfCoeffs {};
    FilterCoeffs m_lfCoeffs {};
    FilterCoeffs m_lmfCoeffs {};
    FilterCoeffs m_hmfCoeffs {};
    FilterCoeffs m_hfCoeffs {};

    // Filter States for stereo (ch 0 and ch 1)
    std::array<BiquadState, 2> m_hpfState1 {};
    std::array<BiquadState, 2> m_hpfState2 {};
    std::array<BiquadState, 2> m_lpfState {};
    std::array<BiquadState, 2> m_lfState {};
    std::array<BiquadState, 2> m_lmfState {};
    std::array<BiquadState, 2> m_hmfState {};
    std::array<BiquadState, 2> m_hfState {};
};

} // namespace dawg::engine::dsp
