#pragma once

#include <dawg/engine/AudioBufferView.h>
#include <dawg/engine/ProcessorNode.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace dawg::engine::dsp {

enum class ConvolutionIrPreset : std::uint8_t
{
    cathedralHall,   // 3.5s Lush Acoustic Cathedral
    vintagePlate,    // 2.0s Dense German EMT Plate
    springTank,      // 1.5s Vintage Fender Spring Tank
    celestionV30Cab, // 4x12 British Celestion V30 Closed-Back Cab
    customTweedCab   // 1x12 American Vintage Tweed Open-Back Cab
};

// Zero-Latency Non-Uniform Partitioned Convolution Reverb & Cabinet IR Engine
// Implements direct time-domain FIR head partition (0 samples latency)
// combined with partitioned frequency-domain tails, cabinet mic modeling,
// and parametric acoustic space shaping.
class ConvolutionReverbNode final : public ProcessorNode
{
public:
    ConvolutionReverbNode() noexcept;
    ~ConvolutionReverbNode() override = default;

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void reset() noexcept override;
    [[nodiscard]] std::uint32_t latencySamples() const noexcept override { return 0; }
    void process(ProcessContext& context) noexcept override;

    // Presets & Custom IR Loading
    void loadPreset(ConvolutionIrPreset preset);
    [[nodiscard]] ConvolutionIrPreset currentPreset() const noexcept { return m_preset; }

    void loadCustomImpulseResponse(const float* samples, std::size_t count, double irSampleRate);

    // Parametric Space Shaping
    void setDryWet(float mix) noexcept { m_dryWet = std::clamp(mix, 0.0f, 1.0f); }
    [[nodiscard]] float dryWet() const noexcept { return m_dryWet; }

    void setPreDelayMs(float ms) noexcept { m_preDelayMs = std::clamp(ms, 0.0f, 200.0f); }
    [[nodiscard]] float preDelayMs() const noexcept { return m_preDelayMs; }

    void setDecayScale(float scale) noexcept { m_decayScale = std::clamp(scale, 0.1f, 5.0f); }
    [[nodiscard]] float decayScale() const noexcept { return m_decayScale; }

    void setHiDamping(float damp) noexcept { m_hiDamping = std::clamp(damp, 0.0f, 1.0f); }
    [[nodiscard]] float hiDamping() const noexcept { return m_hiDamping; }

    void setStereoWidth(float width) noexcept { m_stereoWidth = std::clamp(width, 0.0f, 2.0f); }
    [[nodiscard]] float stereoWidth() const noexcept { return m_stereoWidth; }

    void setReverseIr(bool reverse) noexcept;
    [[nodiscard]] bool isReverseIr() const noexcept { return m_reverseIr; }

    // Guitar/Bass Cabinet Modeling
    void setMicDistanceInches(float dist) noexcept { m_micDistanceInches = std::clamp(dist, 1.0f, 12.0f); }
    [[nodiscard]] float micDistanceInches() const noexcept { return m_micDistanceInches; }

    void setMicOnAxis(float axis) noexcept { m_micOnAxis = std::clamp(axis, 0.0f, 1.0f); }
    [[nodiscard]] float micOnAxis() const noexcept { return m_micOnAxis; }

    [[nodiscard]] std::size_t irLengthSamples() const noexcept { return m_activeIrL.size(); }

private:
    void rebuildActiveImpulseResponse();

    double m_sampleRate {48000.0};
    ConvolutionIrPreset m_preset {ConvolutionIrPreset::cathedralHall};

    float m_dryWet {0.35f};
    float m_preDelayMs {0.0f};
    float m_decayScale {1.0f};
    float m_hiDamping {0.2f};
    float m_stereoWidth {1.0f};
    bool m_reverseIr {false};

    float m_micDistanceInches {2.0f};
    float m_micOnAxis {1.0f};

    // Raw IR storage
    std::vector<float> m_rawIrL;
    std::vector<float> m_rawIrR;

    // Processed Active IR
    std::vector<float> m_activeIrL;
    std::vector<float> m_activeIrR;

    // Zero-latency partitioned buffers:
    // Head partition: First 64 samples evaluated direct time-domain FIR (0 latency)
    static constexpr std::size_t headPartitionSize = 64;
    std::vector<float> m_headIrL;
    std::vector<float> m_headIrR;

    // Pre-delay line & convolution history ring buffer
    std::vector<float> m_inputHistoryL;
    std::vector<float> m_inputHistoryR;
    std::size_t m_historyWritePos {0};

    // High damping filter state
    float m_dampFilterL {0.0f};
    float m_dampFilterR {0.0f};
};

} // namespace dawg::engine::dsp
