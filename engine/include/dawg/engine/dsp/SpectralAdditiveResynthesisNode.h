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

struct SinusoidalPartial
{
    float frequencyHz {440.0f};
    float amplitude {0.0f};
    float phase {0.0f};
};

struct SpectralFrame
{
    static constexpr std::size_t maxPartials = 32;
    std::array<SinusoidalPartial, maxPartials> partials {};
    std::size_t partialCount {0};
    float noiseLevel {0.0f};
    float spectralCentroid {1000.0f};
};

class SpectralAdditiveResynthesisNode final : public ProcessorNode
{
public:
    static constexpr std::size_t maxPartials = SpectralFrame::maxPartials;
    static constexpr std::size_t maxFrames = 256;

    SpectralAdditiveResynthesisNode() noexcept;
    ~SpectralAdditiveResynthesisNode() override = default;

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void process(ProcessContext& context) noexcept override;
    void reset() noexcept override;

    [[nodiscard]] std::string_view name() const noexcept { return "Spectral Additive Resynthesis & Morphing Sampler"; }

    // Load synthesized spectral trajectories (e.g. from analysis of two samples)
    void loadStreamFrames(std::size_t streamIndex, const std::vector<SpectralFrame>& frames);

    // 2D Morphing Controls
    void setMorphX(float x0to1) noexcept;             // 0.0 = Stream A, 1.0 = Stream B
    [[nodiscard]] float morphX() const noexcept;

    void setFormantWarp(float factor) noexcept;        // 0.5 (octave down) .. 2.0 (octave up)
    [[nodiscard]] float formantWarp() const noexcept;

    void setInharmonicity(float amount) noexcept;      // 0.0 (harmonic) .. 1.0 (bell/metallic)
    [[nodiscard]] float inharmonicity() const noexcept;

    // Time-Freezing & Scrubbing
    void setTimeFreeze(bool freeze) noexcept;
    [[nodiscard]] bool isTimeFrozen() const noexcept;

    void setScrubPosition(float pos0to1) noexcept;    // 0.0 .. 1.0 position in sample frames
    [[nodiscard]] float scrubPosition() const noexcept;

    void setResidualNoiseMix(float mix0to1) noexcept;
    [[nodiscard]] float residualNoiseMix() const noexcept;

    void setFundamentalPitch(float pitchHz) noexcept;
    [[nodiscard]] float fundamentalPitch() const noexcept;

private:
    double m_sampleRate {48000.0};

    // Pre-allocated frame tables for Stream A (slot 0) and Stream B (slot 1)
    std::array<std::array<SpectralFrame, maxFrames>, 2> m_streams {};
    std::array<std::size_t, 2> m_streamFrameCounts {0, 0};

    // Parameter atomics
    std::atomic<float> m_morphX {0.0f};
    std::atomic<float> m_formantWarp {1.0f};
    std::atomic<float> m_inharmonicity {0.0f};
    std::atomic<bool> m_timeFreeze {false};
    std::atomic<float> m_scrubPosition {0.0f};
    std::atomic<float> m_residualNoiseMix {0.1f};
    std::atomic<float> m_fundamentalPitch {440.0f};

    // Oscillator phase accumulators for 32 additive partials
    std::array<double, maxPartials> m_phases {};
    float m_playbackPlayhead {0.0f};

    // PRNG noise state
    std::uint32_t m_rngState {0x12345678};
    float m_noiseFilterState {0.0f};
};

} // namespace dawg::engine::dsp
