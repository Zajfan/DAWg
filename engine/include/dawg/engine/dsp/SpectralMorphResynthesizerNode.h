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

enum class SpectralCarrierSource : std::uint8_t
{
    sidechain = 0,
    internalSawPad,
    internalHarmonicOrgan,
    internalWhiteNoise
};

class SpectralMorphResynthesizerNode final : public ProcessorNode
{
public:
    static constexpr std::size_t fftSize = 512;
    static constexpr std::size_t halfFft = fftSize / 2;
    static constexpr std::size_t hopSize = 128; // 75% overlap

    SpectralMorphResynthesizerNode() noexcept;
    ~SpectralMorphResynthesizerNode() override = default;

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void process(ProcessContext& context) noexcept override;
    void reset() noexcept override;

    [[nodiscard]] std::string_view name() const noexcept { return "Spectral Morphing Resynthesizer & Cross-Synthesis Rack"; }

    // Carrier Source
    void setCarrierSource(SpectralCarrierSource src) noexcept { m_carrierSource = src; }
    [[nodiscard]] SpectralCarrierSource carrierSource() const noexcept { return m_carrierSource; }
    void setCarrierPitchHz(float freqHz) noexcept { m_carrierPitchHz = std::clamp(freqHz, 20.0f, 2000.0f); }
    [[nodiscard]] float carrierPitchHz() const noexcept { return m_carrierPitchHz; }

    // Spectral Morph Controls
    void setMorphFactor(float morph) noexcept { m_morphFactor = std::clamp(morph, 0.0f, 1.0f); }
    [[nodiscard]] float morphFactor() const noexcept { return m_morphFactor; }
    void setSpectralFreeze(bool freeze) noexcept { m_spectralFreeze = freeze; }
    [[nodiscard]] bool isSpectralFreeze() const noexcept { return m_spectralFreeze; }
    void setPhaseScramble(float scramble) noexcept { m_phaseScramble = std::clamp(scramble, 0.0f, 1.0f); }
    [[nodiscard]] float phaseScramble() const noexcept { return m_phaseScramble; }

    // Harmonic Smear & Inharmonic Tilt
    void setHarmonicSmear(float smear) noexcept { m_harmonicSmear = std::clamp(smear, 0.0f, 1.0f); }
    [[nodiscard]] float harmonicSmear() const noexcept { return m_harmonicSmear; }
    void setInharmonicTilt(float tilt) noexcept { m_inharmonicTilt = std::clamp(tilt, -1.0f, 1.0f); }
    [[nodiscard]] float inharmonicTilt() const noexcept { return m_inharmonicTilt; }

private:
    void processSpectralFrame() noexcept;
    void performDft(const float* timeDomain, float* realOut, float* imagOut) noexcept;
    void performIdft(const float* realIn, const float* imagIn, float* timeOut) noexcept;

    double m_sampleRate {48000.0};
    std::uint16_t m_channelCount {2};

    SpectralCarrierSource m_carrierSource {SpectralCarrierSource::internalSawPad};
    float m_carrierPitchHz {130.81f}; // C3
    double m_carrierPhase {0.0};

    float m_morphFactor {0.5f};
    bool m_spectralFreeze {false};
    float m_phaseScramble {0.2f};
    float m_harmonicSmear {0.0f};
    float m_inharmonicTilt {0.0f};

    std::uint32_t m_prngSeed {0x76543210};

    // STFT Buffers
    std::array<float, fftSize> m_window {};
    std::array<float, fftSize> m_inputFifoL {};
    std::array<float, fftSize> m_inputFifoCarrier {};
    std::array<float, fftSize> m_outputAccumL {};
    std::size_t m_fifoWritePos {0};

    // Spectral Mag/Phase buffers
    std::array<float, halfFft + 1> m_modulatorMag {};
    std::array<float, halfFft + 1> m_carrierMag {};
    std::array<float, halfFft + 1> m_carrierPhaseBuf {};
    std::array<float, halfFft + 1> m_frozenMag {};
    std::array<float, halfFft + 1> m_smearedMag {};

    // DFT working arrays
    std::array<float, fftSize> m_dftReal {};
    std::array<float, fftSize> m_dftImag {};
    std::array<float, fftSize> m_synthReal {};
    std::array<float, fftSize> m_synthImag {};
    std::array<float, fftSize> m_timeRecon {};
};

} // namespace dawg::engine::dsp
