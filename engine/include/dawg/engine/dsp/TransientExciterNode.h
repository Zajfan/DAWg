#pragma once

#include <dawg/engine/AudioBufferView.h>
#include <dawg/engine/ProcessorNode.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace dawg::engine::dsp {

enum class ExciterHarmonicMode : std::uint8_t
{
    warmTube,        // 2nd-order even harmonics (warm, rich, asymmetrical)
    tapeSaturation,  // 3rd-order odd harmonics (tape density, grit, edge)
    modernExciter    // Dual 2nd + 3rd harmonics with high-frequency air
};

struct TransientBandSettings
{
    float attackDb {0.0f};   // -24.0 dB to +24.0 dB
    float sustainDb {0.0f};  // -24.0 dB to +24.0 dB
};

// Multi-Band Transient Shaper & Harmonic Exciter (Oxford Inflator / Neutron style)
// Splits audio into 3 bands (Low, Mid, High) for independent transient attack punch and sustain elongation,
// paired with a dynamic 2nd/3rd order harmonic exciter and an Inflator-style tube headroom expander.
class TransientExciterNode final : public ProcessorNode
{
public:
    static constexpr std::size_t numBands = 3; // 0 = Low, 1 = Mid, 2 = High

    TransientExciterNode() noexcept;
    ~TransientExciterNode() override = default;

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void reset() noexcept override;
    [[nodiscard]] std::uint32_t latencySamples() const noexcept override { return 0; }
    void process(ProcessContext& context) noexcept override;

    // 3-Band Transient Shaping
    void setBandAttackDb(std::size_t bandIdx, float attackDb) noexcept;
    [[nodiscard]] float bandAttackDb(std::size_t bandIdx) const noexcept;

    void setBandSustainDb(std::size_t bandIdx, float sustainDb) noexcept;
    [[nodiscard]] float bandSustainDb(std::size_t bandIdx) const noexcept;

    // Harmonic Exciter
    void setExciterMode(ExciterHarmonicMode mode) noexcept { m_exciterMode = mode; }
    [[nodiscard]] ExciterHarmonicMode exciterMode() const noexcept { return m_exciterMode; }

    void setExciterDrive(float drive) noexcept { m_exciterDrive = std::clamp(drive, 0.0f, 1.0f); }
    [[nodiscard]] float exciterDrive() const noexcept { return m_exciterDrive; }

    void setExciterMix(float mix) noexcept { m_exciterMix = std::clamp(mix, 0.0f, 1.0f); }
    [[nodiscard]] float exciterMix() const noexcept { return m_exciterMix; }

    // Oxford Inflator-Style Tube Headroom Expander
    // curve: -50.0 (subtle warm saturation) to +50.0 (maximum loudness inflation)
    void setInflatorCurve(float curve) noexcept { m_inflatorCurve = std::clamp(curve, -50.0f, 50.0f); }
    [[nodiscard]] float inflatorCurve() const noexcept { return m_inflatorCurve; }

    void setInflatorAmount(float amount) noexcept { m_inflatorAmount = std::clamp(amount, 0.0f, 1.0f); }
    [[nodiscard]] float inflatorAmount() const noexcept { return m_inflatorAmount; }

    // Telemetry: transient activity meters per band (0.0 to 1.0)
    [[nodiscard]] float bandTransientActivity(std::size_t bandIdx) const noexcept;

private:
    struct CrossoverFilter
    {
        float lpL {0.0f};
        float lpR {0.0f};
        float hpL {0.0f};
        float hpR {0.0f};
    };

    struct BandState
    {
        TransientBandSettings settings {};
        float attackLinear {1.0f};
        float sustainLinear {1.0f};
        float fastEnvL {0.0f};
        float fastEnvR {0.0f};
        float slowEnvL {0.0f};
        float slowEnvR {0.0f};
        float activityMeter {0.0f};
    };

    void updateGains() noexcept;

    double m_sampleRate {48000.0};
    std::array<BandState, numBands> m_bands {};

    ExciterHarmonicMode m_exciterMode {ExciterHarmonicMode::warmTube};
    float m_exciterDrive {0.25f};
    float m_exciterMix {0.35f};

    float m_inflatorCurve {15.0f};
    float m_inflatorAmount {0.40f};

    // 3-way crossover filters (split at 180 Hz and 3500 Hz)
    CrossoverFilter m_crossoverLow {};
    CrossoverFilter m_crossoverHigh {};

    // DC blocker filter states
    float m_dcBlockInL {0.0f};
    float m_dcBlockOutL {0.0f};
    float m_dcBlockInR {0.0f};
    float m_dcBlockOutR {0.0f};
};

} // namespace dawg::engine::dsp
