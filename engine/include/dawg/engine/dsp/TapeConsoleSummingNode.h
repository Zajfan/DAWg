#pragma once

#include <dawg/engine/AudioBufferView.h>
#include <dawg/engine/ProcessorNode.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace dawg::engine::dsp {

enum class TapeSpeed : std::uint8_t
{
    ips15, // 15 Inches Per Second (vintage warm, rich 50 Hz head bump, softer top end)
    ips30  // 30 Inches Per Second (audiophile, punchy 80 Hz bump, pristine extended highs)
};

// Analog Tape Machine & Vintage Console Summing Suite (Studer A800 / SSL 4000E style)
// Implements magnetic tape hysteresis saturation, speed-dependent head bump resonance,
// transport wow & flutter pitch instability, transformer core coloration, and inter-channel crosstalk.
class TapeConsoleSummingNode final : public ProcessorNode
{
public:
    static constexpr std::size_t delayBufferSize = 1024;

    TapeConsoleSummingNode() noexcept;
    ~TapeConsoleSummingNode() override = default;

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void reset() noexcept override;
    [[nodiscard]] std::uint32_t latencySamples() const noexcept override { return 0; }
    void process(ProcessContext& context) noexcept override;

    // Tape Machine Parameters
    void setTapeSpeed(TapeSpeed speed) noexcept { m_tapeSpeed = speed; updateFilters(); }
    [[nodiscard]] TapeSpeed tapeSpeed() const noexcept { return m_tapeSpeed; }

    void setDrive(float drive) noexcept { m_drive = std::clamp(drive, 0.0f, 1.0f); updateGains(); }
    [[nodiscard]] float drive() const noexcept { return m_drive; }

    void setTapeBias(float bias) noexcept { m_bias = std::clamp(bias, -1.0f, 1.0f); }
    [[nodiscard]] float tapeBias() const noexcept { return m_bias; }

    void setWow(float amount) noexcept { m_wow = std::clamp(amount, 0.0f, 1.0f); }
    [[nodiscard]] float wow() const noexcept { return m_wow; }

    void setFlutter(float amount) noexcept { m_flutter = std::clamp(amount, 0.0f, 1.0f); }
    [[nodiscard]] float flutter() const noexcept { return m_flutter; }

    // Vintage Console Bus Parameters
    void setTransformerSaturation(float amount) noexcept { m_transformerSat = std::clamp(amount, 0.0f, 1.0f); }
    [[nodiscard]] float transformerSaturation() const noexcept { return m_transformerSat; }

    void setCrosstalkDb(float db) noexcept { m_crosstalkDb = std::clamp(db, -100.0f, -50.0f); updateGains(); }
    [[nodiscard]] float crosstalkDb() const noexcept { return m_crosstalkDb; }

    void setHeadBumpGainDb(float db) noexcept { m_headBumpGainDb = std::clamp(db, 0.0f, 6.0f); updateFilters(); }
    [[nodiscard]] float headBumpGainDb() const noexcept { return m_headBumpGainDb; }

private:
    struct BiquadState
    {
        float z1 {0.0f};
        float z2 {0.0f};

        inline float process(float in, float b0, float b1, float b2, float a1, float a2) noexcept
        {
            float out = b0 * in + z1;
            z1 = b1 * in - a1 * out + z2;
            z2 = b2 * in - a2 * out;
            return out;
        }

        void reset() noexcept { z1 = 0.0f; z2 = 0.0f; }
    };

    void updateGains() noexcept;
    void updateFilters() noexcept;

    double m_sampleRate {48000.0};
    TapeSpeed m_tapeSpeed {TapeSpeed::ips15};
    float m_drive {0.35f};
    float m_bias {0.0f};
    float m_wow {0.15f};
    float m_flutter {0.10f};
    float m_transformerSat {0.30f};
    float m_crosstalkDb {-75.0f};
    float m_headBumpGainDb {3.0f};

    float m_inputLinear {1.5f};
    float m_crosstalkLinear {0.00017f};

    // Filter coefficients for Head Bump & Pre/De-emphasis
    float m_headBumpB0 {1.0f}, m_headBumpB1 {0.0f}, m_headBumpB2 {0.0f};
    float m_headBumpA1 {0.0f}, m_headBumpA2 {0.0f};
    BiquadState m_headBumpL {};
    BiquadState m_headBumpR {};

    // HF De-emphasis filter state
    float m_hfFilterL {0.0f};
    float m_hfFilterR {0.0f};

    // Wow & Flutter delay lines and modulation phase
    std::array<float, delayBufferSize> m_delayBufferL {};
    std::array<float, delayBufferSize> m_delayBufferR {};
    std::size_t m_delayWritePos {0};
    float m_wowPhase {0.0f};
    float m_flutterPhase {0.0f};
};

} // namespace dawg::engine::dsp
