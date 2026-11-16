#pragma once

#include <dawg/engine/AudioBufferView.h>
#include <dawg/engine/ProcessorNode.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dawg::engine::dsp {

enum class SpectralMorphMode : std::uint8_t
{
    linearBlend,   // Smooth linear magnitude interpolation between Stream A and B
    crossMultiply, // Zero-phase harmonic envelope cross-multiplication (Vocoder / Kyma style)
    formantWarp    // Spectral envelope shift and formant centroid transfer
};

// Spectral Morphing Cross-Synthesizer & Formant Convolver (Zynaptiq Morph / Kyma style)
// Analyzes two audio streams via dual-stream STFT, interpolating magnitude spectra,
// warping formant peaks, and performing zero-phase cross-synthesis reconstruction.
class SpectralMorphCrossSynthNode final : public ProcessorNode
{
public:
    static constexpr std::size_t FftSize = 512;
    static constexpr std::size_t HopSize = 128; // 75% overlap
    static constexpr std::size_t NumBins = FftSize / 2 + 1;

    SpectralMorphCrossSynthNode() noexcept;
    ~SpectralMorphCrossSynthNode() override = default;

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void reset() noexcept override;
    [[nodiscard]] std::uint32_t latencySamples() const noexcept override { return static_cast<std::uint32_t>(FftSize); }
    void process(ProcessContext& context) noexcept override;

    // Morphing Vector (X, Y)
    // Morph X (0.0 = 100% Stream A, 1.0 = 100% Stream B)
    void setMorphX(float x) noexcept { m_morphX = std::clamp(x, 0.0f, 1.0f); }
    [[nodiscard]] float morphX() const noexcept { return m_morphX; }

    // Formant Shift / Centroid Warping Y (0.0 to 1.0)
    void setFormantWarpY(float y) noexcept { m_formantWarpY = std::clamp(y, 0.0f, 1.0f); }
    [[nodiscard]] float formantWarpY() const noexcept { return m_formantWarpY; }

    void setMorphMode(SpectralMorphMode mode) noexcept { m_mode = mode; }
    [[nodiscard]] SpectralMorphMode morphMode() const noexcept { return m_mode; }

    // Dry / Wet Mix
    void setMix(float mix) noexcept { m_mix = std::clamp(mix, 0.0f, 1.0f); }
    [[nodiscard]] float mix() const noexcept { return m_mix; }

private:
    void processStftFrame() noexcept;
    void computeFft(const std::array<float, FftSize>& input, std::array<float, NumBins>& mag, std::array<float, NumBins>& phase) noexcept;
    void computeIfft(const std::array<float, NumBins>& mag, const std::array<float, NumBins>& phase, std::array<float, FftSize>& output) noexcept;

    double m_sampleRate {48000.0};
    SpectralMorphMode m_mode {SpectralMorphMode::linearBlend};

    float m_morphX {0.5f};
    float m_formantWarpY {0.5f};
    float m_mix {1.0f};

    // Analysis window (Hann)
    std::array<float, FftSize> m_window {};

    // Input sliding buffers for Stream A (Main) and Stream B (Sidechain)
    std::array<float, FftSize> m_inputBufferA {};
    std::array<float, FftSize> m_inputBufferB {};
    std::size_t m_inputPos {0};

    // Output overlap-add accumulation buffer
    std::array<float, FftSize * 2> m_outputOlaBufferL {};
    std::array<float, FftSize * 2> m_outputOlaBufferR {};
    std::size_t m_outputReadPos {0};

    // Spectral intermediate magnitude & phase bins
    std::array<float, NumBins> m_magA {};
    std::array<float, NumBins> m_phaseA {};
    std::array<float, NumBins> m_magB {};
    std::array<float, NumBins> m_phaseB {};

    std::array<float, NumBins> m_morphMag {};
    std::array<float, NumBins> m_morphPhase {};
    std::array<float, FftSize> m_synthFrameL {};
};

} // namespace dawg::engine::dsp
