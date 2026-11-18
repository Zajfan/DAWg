#pragma once

#include <dawg/engine/AudioBufferView.h>
#include <dawg/engine/ProcessorNode.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace dawg::engine::dsp {

enum class DynamicEqPhaseMode : std::uint8_t
{
    minimumPhase = 0,
    linearPhase
};

enum class DynamicEqBandMode : std::uint8_t
{
    staticEq = 0,
    downwardCompress,
    upwardExpand,
    downwardExpand
};

enum class DynamicEqFilterType : std::uint8_t
{
    lowShelf = 0,
    peaking,
    highShelf
};

struct DynamicEqBandConfig
{
    bool enabled {true};
    DynamicEqFilterType type {DynamicEqFilterType::peaking};
    DynamicEqBandMode mode {DynamicEqBandMode::staticEq};
    float frequencyHz {1000.0f};
    float q {1.0f};
    float staticGainDb {0.0f};
    float dynamicRangeDb {-6.0f}; // Max compression/expansion
    float thresholdDb {-20.0f};
    float ratio {3.0f};
    float attackMs {10.0f};
    float releaseMs {80.0f};
};

struct EqBiquad
{
    float b0 {1.0f}, b1 {0.0f}, b2 {0.0f};
    float a1 {0.0f}, a2 {0.0f};
    float z1L {0.0f}, z2L {0.0f};
    float z1R {0.0f}, z2R {0.0f};

    void reset() noexcept
    {
        z1L = z2L = z1R = z2R = 0.0f;
    }

    [[nodiscard]] inline float processL(float in) noexcept
    {
        const float out = b0 * in + z1L;
        z1L = b1 * in - a1 * out + z2L;
        z2L = b2 * in - a2 * out;
        return out;
    }

    [[nodiscard]] inline float processR(float in) noexcept
    {
        const float out = b0 * in + z1R;
        z1R = b1 * in - a1 * out + z2R;
        z2R = b2 * in - a2 * out;
        return out;
    }
};

class DynamicLinearPhaseMasteringEQNode final : public ProcessorNode
{
public:
    static constexpr std::size_t numBands = 6;
    static constexpr std::size_t firTaps = 129; // Symmetric odd-length linear phase FIR

    DynamicLinearPhaseMasteringEQNode() noexcept;
    ~DynamicLinearPhaseMasteringEQNode() override = default;

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void process(ProcessContext& context) noexcept override;
    void reset() noexcept override;

    [[nodiscard]] std::string_view name() const noexcept { return "Dynamic Linear-Phase Mastering EQ"; }

    // Phase Mode
    void setPhaseMode(DynamicEqPhaseMode mode) noexcept;
    [[nodiscard]] DynamicEqPhaseMode phaseMode() const noexcept { return m_phaseMode; }

    // Band Configurations
    void setBandConfig(std::size_t bandIndex, const DynamicEqBandConfig& config) noexcept;
    [[nodiscard]] const DynamicEqBandConfig& bandConfig(std::size_t bandIndex) const noexcept;
    void setBandStaticGainDb(std::size_t bandIndex, float gainDb) noexcept;
    void setBandFrequency(std::size_t bandIndex, float freqHz) noexcept;

    // Sidechain Collision Unmasking
    void setCollisionUnmaskingEnabled(bool enabled) noexcept { m_collisionUnmaskEnabled = enabled; }
    [[nodiscard]] bool isCollisionUnmaskingEnabled() const noexcept { return m_collisionUnmaskEnabled; }
    void setUnmaskSensitivity(float sens) noexcept { m_unmaskSensitivity = std::clamp(sens, 0.1f, 2.0f); }
    [[nodiscard]] float unmaskSensitivity() const noexcept { return m_unmaskSensitivity; }

    // Telemetry: Current dynamic gain reduction/expansion per band
    [[nodiscard]] float bandGainOffsetDb(std::size_t bandIndex) const noexcept;

private:
    void updateBandCoeffs(std::size_t bandIndex, float dynamicOffsetDb) noexcept;
    void updateLinearPhaseFirKernel() noexcept;

    double m_sampleRate {48000.0};
    std::uint16_t m_channelCount {2};

    DynamicEqPhaseMode m_phaseMode {DynamicEqPhaseMode::minimumPhase};
    bool m_collisionUnmaskEnabled {false};
    float m_unmaskSensitivity {1.0f};

    std::array<DynamicEqBandConfig, numBands> m_configs;
    std::array<EqBiquad, numBands> m_biquads;

    // Envelope followers & telemetry
    std::array<float, numBands> m_bandEnvelopes {};
    std::array<float, numBands> m_sidechainEnvelopes {};
    std::array<std::atomic<float>, numBands> m_gainOffsetsDb;

    // Linear Phase FIR delay buffers
    std::array<float, firTaps> m_firKernel {};
    std::array<float, firTaps> m_firHistoryL {};
    std::array<float, firTaps> m_firHistoryR {};
    std::size_t m_firWriteIdx {0};
};

} // namespace dawg::engine::dsp
