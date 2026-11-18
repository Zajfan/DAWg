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

enum class SpecializedWarpMode : std::uint8_t
{
    beats,      // Transient-preserving slicing & gating
    tones,      // Pitch-synchronous monophonic stretch
    texture,    // Granular stretch with flux modulation
    complexPro  // Dual envelope multi-band formant-locked stretch
};

class SpecializedClipWarpEngine final : public ProcessorNode
{
public:
    static constexpr std::size_t ringBufferSize = 32768;

    SpecializedClipWarpEngine() noexcept;
    ~SpecializedClipWarpEngine() override = default;

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void process(ProcessContext& context) noexcept override;
    void reset() noexcept override;

    [[nodiscard]] std::string_view name() const noexcept { return "Specialized Audio Clip Warp Engine"; }

    // Warp Mode Selection
    void setWarpMode(SpecializedWarpMode mode) noexcept;
    [[nodiscard]] SpecializedWarpMode warpMode() const noexcept;

    // Time-stretch & Pitch transposition
    void setStretchFactor(float ratio) noexcept;       // 0.25 (4x faster) .. 4.0 (4x slower)
    [[nodiscard]] float stretchFactor() const noexcept;

    void setPitchSemitones(float semitones) noexcept;   // -48.0 .. +48.0
    [[nodiscard]] float pitchSemitones() const noexcept;

    void setFormantPreservation(float amount0to1) noexcept;
    [[nodiscard]] float formantPreservation() const noexcept;

    // Mode-specific parameters
    void setTextureGrainSizeMs(float grainMs) noexcept; // 10.0 .. 150.0 ms
    [[nodiscard]] float textureGrainSizeMs() const noexcept;

    void setTextureFlux(float flux0to1) noexcept;
    [[nodiscard]] float textureFlux() const noexcept;

    void setBeatsTransientSensitivity(float sens0to1) noexcept;
    [[nodiscard]] float beatsTransientSensitivity() const noexcept;

private:
    double m_sampleRate {48000.0};

    std::atomic<SpecializedWarpMode> m_warpMode {SpecializedWarpMode::beats};
    std::atomic<float> m_stretchFactor {1.0f};
    std::atomic<float> m_pitchSemitones {0.0f};
    std::atomic<float> m_formantPreserve {1.0f};
    std::atomic<float> m_grainSizeMs {40.0f};
    std::atomic<float> m_textureFlux {0.2f};
    std::atomic<float> m_beatsSensitivity {0.7f};

    // Pre-allocated circular history buffer
    std::array<float, ringBufferSize> m_ringL {};
    std::array<float, ringBufferSize> m_ringR {};
    std::size_t m_writePos {0};
    double m_readPos {0.0};

    // Transient detector for Beats mode
    float m_prevEnergy {0.0f};
    bool m_inTransient {false};
    std::size_t m_transientHoldSamples {0};

    // Granular overlap-add state for Texture & Complex Pro modes
    double m_grainPhase {0.0};
    std::uint32_t m_prngState {0x98765u};
};

} // namespace dawg::engine::dsp
