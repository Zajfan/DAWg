#pragma once

#include <dawg/engine/ProcessorNode.h>

#include <array>
#include <atomic>
#include <cmath>

namespace dawg::engine::dsp {

struct CompressorSettings
{
    bool bypassed {false};
    float thresholdDb {-18.0f};  // -60 dB to 0 dB
    float ratio {4.0f};          // 1.0 to 20.0
    float attackMs {10.0f};      // 0.1 ms to 200 ms
    float releaseMs {100.0f};    // 10 ms to 1200 ms
    float makeupGainDb {0.0f};   // 0 dB to 30 dB
    float kneeDb {3.0f};         // 0 dB (hard) to 12 dB (soft)
    bool useRms {false};
    bool sidechainEnabled {false};

    // Sidechain key filtering & listen mode
    bool sidechainListen {false};    // Route filtered key signal directly to audio output
    float sidechainHpfHz {20.0f};    // HPF cutoff on key signal (20 Hz - 2000 Hz)
    float sidechainLpfHz {20000.0f}; // LPF cutoff on key signal (500 Hz - 20000 Hz)

    // Dedicated Downward Gate / Expander
    bool gateEnabled {false};
    float gateThresholdDb {-45.0f};  // -80 dB to 0 dB
    float gateRatio {8.0f};          // 1.0 to 20.0 (expansion ratio)
    float gateAttackMs {1.0f};       // 0.1 ms to 50 ms
    float gateReleaseMs {50.0f};     // 5 ms to 500 ms

    friend bool operator==(const CompressorSettings&, const CompressorSettings&) = default;
};

class DynamicsCompressorNode final : public ProcessorNode
{
public:
    explicit DynamicsCompressorNode(CompressorSettings settings = {}) noexcept;

    void setSettings(const CompressorSettings& settings) noexcept;
    [[nodiscard]] const CompressorSettings& settings() const noexcept { return m_settings; }
    void setSidechainEnabled(bool enabled) noexcept { m_settings.sidechainEnabled = enabled; }
    [[nodiscard]] bool sidechainEnabled() const noexcept { return m_settings.sidechainEnabled; }
    void setSidechainListen(bool listen) noexcept { m_settings.sidechainListen = listen; }
    [[nodiscard]] bool sidechainListen() const noexcept { return m_settings.sidechainListen; }
    void setSidechainFilter(float hpfHz, float lpfHz) noexcept
    {
        m_settings.sidechainHpfHz = hpfHz;
        m_settings.sidechainLpfHz = lpfHz;
        updateBallistics();
    }

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void reset() noexcept override;

    void process(ProcessContext& context) noexcept override;

    [[nodiscard]] float gainReductionDb() const noexcept
    {
        return m_gainReductionDb.load(std::memory_order_relaxed);
    }

    [[nodiscard]] float gateReductionDb() const noexcept
    {
        return m_gateReductionDb.load(std::memory_order_relaxed);
    }

private:
    struct BiquadState
    {
        float x1 {0.0f};
        float x2 {0.0f};
        float y1 {0.0f};
        float y2 {0.0f};

        void reset() noexcept { x1 = x2 = y1 = y2 = 0.0f; }

        [[nodiscard]] inline float process(float in, float b0, float b1, float b2, float a1, float a2) noexcept
        {
            const float out = b0 * in + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1;
            x1 = in;
            y2 = y1;
            y1 = out;
            return out;
        }
    };

    void updateBallistics() noexcept;

    CompressorSettings m_settings;
    double m_sampleRate {48000.0};
    float m_attackCoeff {0.0f};
    float m_releaseCoeff {0.0f};
    float m_gateAttackCoeff {0.0f};
    float m_gateReleaseCoeff {0.0f};
    float m_makeupLinear {1.0f};
    float m_envelopeDb {0.0f};
    float m_gateEnvelopeDb {0.0f};

    // Sidechain Biquad Coefficients (HPF & LPF)
    float m_scHpfB0 {1.0f}, m_scHpfB1 {0.0f}, m_scHpfB2 {0.0f}, m_scHpfA1 {0.0f}, m_scHpfA2 {0.0f};
    float m_scLpfB0 {1.0f}, m_scLpfB1 {0.0f}, m_scLpfB2 {0.0f}, m_scLpfA1 {0.0f}, m_scLpfA2 {0.0f};
    std::array<BiquadState, 2> m_scHpfState {};
    std::array<BiquadState, 2> m_scLpfState {};

    std::atomic<float> m_gainReductionDb {0.0f};
    std::atomic<float> m_gateReductionDb {0.0f};
};

} // namespace dawg::engine::dsp
