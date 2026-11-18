#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <string_view>

#include "dawg/engine/ProcessorNode.h"

namespace dawg::engine::dsp {

class AdaptiveResonanceSuppressorNode final : public ProcessorNode
{
public:
    static constexpr std::size_t FftSize = 1024;
    static constexpr std::size_t HopSize = 256;      // 75% overlap
    static constexpr std::size_t HalfFft = FftSize / 2 + 1; // 513 bins

    AdaptiveResonanceSuppressorNode() noexcept;
    ~AdaptiveResonanceSuppressorNode() override = default;

    [[nodiscard]] std::string_view name() const noexcept { return "Adaptive Dynamic Resonance Suppressor"; }

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void process(ProcessContext& context) noexcept override;
    void reset() noexcept override;

    // Suppression Controls
    void setDepth(float depth) noexcept { m_depth = std::clamp(depth, 0.0f, 1.0f); }
    [[nodiscard]] float depth() const noexcept { return m_depth; }

    void setSharpness(float sharpness) noexcept { m_sharpness = std::clamp(sharpness, 0.1f, 5.0f); }
    [[nodiscard]] float sharpness() const noexcept { return m_sharpness; }

    void setAuditionDelta(bool delta) noexcept { m_auditionDelta = delta; }
    [[nodiscard]] bool isAuditionDelta() const noexcept { return m_auditionDelta; }

    // Spectral Balancer / Contrast Controls (Gullfoss style)
    void setRecover(float recover) noexcept { m_recover = std::clamp(recover, 0.0f, 1.0f); }
    [[nodiscard]] float recover() const noexcept { return m_recover; }

    void setTame(float tame) noexcept { m_tame = std::clamp(tame, 0.0f, 1.0f); }
    [[nodiscard]] float tame() const noexcept { return m_tame; }

    void setSpectralTilt(float tiltDb) noexcept { m_spectralTilt = std::clamp(tiltDb, -6.0f, 6.0f); }
    [[nodiscard]] float spectralTilt() const noexcept { return m_spectralTilt; }

private:
    void initWindowAndErb() noexcept;
    void processStftChannel(
        const float* input,
        float* output,
        std::size_t frameCount,
        std::size_t channelIdx) noexcept;

    void analyzeAndSuppress(
        std::array<float, FftSize>& real,
        std::array<float, FftSize>& imag,
        std::size_t channelIdx) noexcept;

    // Fast Radix-2 In-place Cooley-Tukey FFT (zero allocation)
    static void fft(std::array<float, FftSize>& real, std::array<float, FftSize>& imag) noexcept;
    static void ifft(std::array<float, FftSize>& real, std::array<float, FftSize>& imag) noexcept;

    double m_sampleRate {48000.0};
    float m_depth {0.5f};
    float m_sharpness {1.0f};
    bool m_auditionDelta {false};

    float m_recover {0.0f};
    float m_tame {0.5f};
    float m_spectralTilt {0.0f};

    // Precomputed window and ERB masking curves
    std::array<float, FftSize> m_window {};
    std::array<float, HalfFft> m_erbBandwidths {};
    std::array<float, HalfFft> m_binFrequencies {};

    // Per-channel OLA ring buffers
    static constexpr std::size_t RingBufferSize = FftSize * 4;
    std::array<std::array<float, RingBufferSize>, 2> m_inputBuffer {};
    std::array<std::array<float, RingBufferSize>, 2> m_outputBuffer {};
    std::array<std::size_t, 2> m_inWritePos {};
    std::array<std::size_t, 2> m_outReadPos {};
    std::array<std::size_t, 2> m_hopCounter {};

    // Spectral history for temporal resonance smoothing
    std::array<std::array<float, HalfFft>, 2> m_smoothedMagnitudes {};
};

} // namespace dawg::engine::dsp
