#pragma once

#include <dawg/engine/ProcessorNode.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dawg::engine {

enum class DelayMode : std::uint8_t
{
    dualMono,
    pingPong
};

enum class DelaySyncDivision : std::uint8_t
{
    freeMs,
    quarter,
    eighth,
    eighthTriplet,
    eighthDotted,
    sixteenth
};

class TnhcDelayNode final : public ProcessorNode
{
public:
    TnhcDelayNode() noexcept = default;
    ~TnhcDelayNode() override = default;

    [[nodiscard]] bool prepare(double sampleRate, std::size_t maxBlockSize,
                               std::uint16_t channelCount) override;
    void reset() noexcept override;
    [[nodiscard]] std::uint32_t latencySamples() const noexcept override { return 0; }
    void process(ProcessContext& context) noexcept override;

    // Parameters
    void setMode(DelayMode mode) noexcept { m_mode = mode; }
    [[nodiscard]] DelayMode mode() const noexcept { return m_mode; }

    void setSyncDivision(DelaySyncDivision div) noexcept { m_syncDivision = div; }
    [[nodiscard]] DelaySyncDivision syncDivision() const noexcept { return m_syncDivision; }

    void setBpm(double bpm) noexcept;
    [[nodiscard]] double bpm() const noexcept { return m_bpm; }

    void setDelayTimeMsLeft(float ms) noexcept;
    [[nodiscard]] float delayTimeMsLeft() const noexcept { return m_delayTimeMsLeft; }

    void setDelayTimeMsRight(float ms) noexcept;
    [[nodiscard]] float delayTimeMsRight() const noexcept { return m_delayTimeMsRight; }

    void setFeedback(float fb) noexcept;
    [[nodiscard]] float feedback() const noexcept { return m_feedback; }

    void setHighCutHz(float hz) noexcept;
    [[nodiscard]] float highCutHz() const noexcept { return m_highCutHz; }

    void setLowCutHz(float hz) noexcept;
    [[nodiscard]] float lowCutHz() const noexcept { return m_lowCutHz; }

    void setWetDry(float wet) noexcept;
    [[nodiscard]] float wetDry() const noexcept { return m_wetDry; }

private:
    [[nodiscard]] float readInterpolated(const std::vector<float>& buf, std::size_t writeIdx,
                                         float delaySamples) const noexcept;
    void updateFilterCoeffs() noexcept;
    [[nodiscard]] float effectiveDelayMsLeft() const noexcept;
    [[nodiscard]] float effectiveDelayMsRight() const noexcept;

    double m_sampleRate {48000.0};
    std::uint16_t m_channelCount {2};
    double m_bpm {120.0};

    DelayMode m_mode {DelayMode::pingPong};
    DelaySyncDivision m_syncDivision {DelaySyncDivision::freeMs};

    float m_delayTimeMsLeft {375.0f};  // 1/8 note at 120 bpm
    float m_delayTimeMsRight {500.0f}; // 1/4 note at 120 bpm
    float m_feedback {0.45f};
    float m_highCutHz {7500.0f};
    float m_lowCutHz {120.0f};
    float m_wetDry {0.35f};

    // Filter coefficients & states
    float m_lpCoeff {0.5f};
    float m_hpCoeff {0.01f};
    float m_lpStateL {0.0f};
    float m_lpStateR {0.0f};
    float m_hpStateL {0.0f};
    float m_hpPrevInL {0.0f};
    float m_hpStateR {0.0f};
    float m_hpPrevInR {0.0f};

    std::vector<float> m_bufferL;
    std::vector<float> m_bufferR;
    std::size_t m_writeIdxL {0};
    std::size_t m_writeIdxR {0};
    std::size_t m_bufferCapacity {0};
};

} // namespace dawg::engine
