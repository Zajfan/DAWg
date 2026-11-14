#pragma once

#include <dawg/engine/ProcessorNode.h>

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

private:
    void updateBallistics() noexcept;

    CompressorSettings m_settings;
    double m_sampleRate {48000.0};
    float m_attackCoeff {0.0f};
    float m_releaseCoeff {0.0f};
    float m_makeupLinear {1.0f};
    float m_envelopeDb {0.0f};
    std::atomic<float> m_gainReductionDb {0.0f};
};

} // namespace dawg::engine::dsp
