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

// 32-Band Adaptive Dynamic Spectral Sidechain & Masking Eliminator (Trackspacer / Soothe style)
// Carves out colliding frequency bands from the main track where the sidechain signal is active,
// eliminating muddy frequency masking with zero audible overall volume pumping.
class SpectralSidechainNode final : public ProcessorNode
{
public:
    static constexpr std::size_t numBands = 32;

    SpectralSidechainNode() noexcept;
    ~SpectralSidechainNode() override = default;

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void reset() noexcept override;
    [[nodiscard]] std::uint32_t latencySamples() const noexcept override { return 0; }
    void process(ProcessContext& context) noexcept override;

    // Ducking controls
    void setDuckingAmount(float amount) noexcept { m_duckingAmount = std::clamp(amount, 0.0f, 1.0f); }
    [[nodiscard]] float duckingAmount() const noexcept { return m_duckingAmount; }

    void setAttackMs(float ms) noexcept;
    [[nodiscard]] float attackMs() const noexcept { return m_attackMs; }

    void setReleaseMs(float ms) noexcept;
    [[nodiscard]] float releaseMs() const noexcept { return m_releaseMs; }

    void setLowCutHz(float hz) noexcept { m_lowCutHz = std::clamp(hz, 20.0f, 2000.0f); }
    [[nodiscard]] float lowCutHz() const noexcept { return m_lowCutHz; }

    void setHighCutHz(float hz) noexcept { m_highCutHz = std::clamp(hz, 500.0f, 20000.0f); }
    [[nodiscard]] float highCutHz() const noexcept { return m_highCutHz; }

    // Audition Delta Mode: Solo strictly the carved-out difference signal
    void setAuditionDelta(bool enabled) noexcept { m_auditionDelta = enabled; }
    [[nodiscard]] bool isAuditionDelta() const noexcept { return m_auditionDelta; }

    // Adaptive Q: Wider Q at low bass (1.5) to prevent ringing, sharper Q at high/mids (4.5) for formant isolation
    void setAdaptiveQEnabled(bool enabled) noexcept { m_adaptiveQ = enabled; }
    [[nodiscard]] bool isAdaptiveQEnabled() const noexcept { return m_adaptiveQ; }

    // Sidechain Sensitivity (0.1 to 3.0)
    void setSensitivity(float sens) noexcept { m_sensitivity = std::clamp(sens, 0.1f, 3.0f); }
    [[nodiscard]] float sensitivity() const noexcept { return m_sensitivity; }

    // Real-Time Visual Frequency Collision Radar Readout
    void getCollisionRadar(float* destination, std::size_t count) const noexcept;
    void getSidechainEnergies(float* destination, std::size_t count) const noexcept;

private:
    struct BandFilter
    {
        float centerHz {1000.0f};
        float q {2.5f};
        float s1 {0.0f};
        float s2 {0.0f};
        float sidechainS1 {0.0f};
        float sidechainS2 {0.0f};
        float sidechainEnv {0.0f};
        float gainReduction {1.0f};
    };

    void setupBandFrequencies() noexcept;

    double m_sampleRate {48000.0};
    float m_duckingAmount {0.6f};
    float m_sensitivity {1.0f};
    float m_attackMs {5.0f};
    float m_releaseMs {60.0f};
    float m_lowCutHz {30.0f};
    float m_highCutHz {16000.0f};
    bool m_auditionDelta {false};
    bool m_adaptiveQ {true};

    float m_attackCoeff {0.1f};
    float m_releaseCoeff {0.01f};

    std::array<BandFilter, numBands> m_bandsL {};
    std::array<BandFilter, numBands> m_bandsR {};

    // Collision radar telemetry (32 bands)
    std::array<float, numBands> m_collisionRadar {};
    std::array<float, numBands> m_sidechainEnergies {};
};

} // namespace dawg::engine::dsp
