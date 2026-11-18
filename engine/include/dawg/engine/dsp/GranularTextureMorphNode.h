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

enum class TextureWindowShape : std::uint8_t
{
    hann,
    gaussian,
    blackman,
    rectangular
};

struct TextureGrain
{
    bool active {false};
    double readPos {0.0};
    double speed {1.0};           // Resampling playback speed
    bool isReversed {false};
    std::size_t durationSamples {0};
    std::size_t ageSamples {0};
    float amplitude {1.0f};
    float panL {0.7071f};
    float panR {0.7071f};
    TextureWindowShape windowShape {TextureWindowShape::hann};
};

class GranularTextureMorphNode final : public ProcessorNode
{
public:
    static constexpr std::size_t maxGrains = 256;
    static constexpr std::size_t maxBufferFrames = 48000 * 10; // 10 seconds stereo buffer

    GranularTextureMorphNode() noexcept;
    ~GranularTextureMorphNode() override = default;

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void process(ProcessContext& context) noexcept override;
    void reset() noexcept override;

    [[nodiscard]] std::string_view name() const noexcept { return "Granular Cloud Sampler & Texture Morph"; }

    // Sample Buffer Management
    void loadSampleBuffer(const float* left, const float* right, std::size_t count);
    [[nodiscard]] std::size_t sampleBufferFrames() const noexcept { return m_bufferLength; }

    // 2D Scrubbing XY Pad
    void setPositionX(float x0to1) noexcept;       // 0.0 .. 1.0
    [[nodiscard]] float positionX() const noexcept;

    void setSprayY(float y0to1) noexcept;          // 0.0 .. 1.0 (temporal jitter)
    [[nodiscard]] float sprayY() const noexcept;

    // Grain Engine Parameters
    void setGrainLengthMs(float lengthMs) noexcept; // 1.0 .. 500.0 ms
    [[nodiscard]] float grainLengthMs() const noexcept;

    void setGrainDensity(float grainsPerSec) noexcept; // 1.0 .. 120.0
    [[nodiscard]] float grainDensity() const noexcept;

    void setWindowShape(TextureWindowShape shape) noexcept;
    [[nodiscard]] TextureWindowShape windowShape() const noexcept;

    void setReverseProbability(float prob0to1) noexcept; // 0.0 .. 1.0
    [[nodiscard]] float reverseProbability() const noexcept;

    void setPanSpread(float spread0to1) noexcept;
    [[nodiscard]] float panSpread() const noexcept;

    // Pitch & Shimmer Harmony
    void setPitchSemitones(float semitones) noexcept; // -24.0 .. +24.0
    [[nodiscard]] float pitchSemitones() const noexcept;

    void setShimmerOctaveMix(float mix0to1) noexcept; // Blends +12 / +24 octave grains
    [[nodiscard]] float shimmerOctaveMix() const noexcept;

    [[nodiscard]] std::size_t activeGrainCount() const noexcept;

private:
    [[nodiscard]] float evaluateWindow(TextureWindowShape shape, float t) const noexcept;
    void spawnGrain() noexcept;

    double m_sampleRate {48000.0};

    // Pre-allocated circular sample buffer
    std::vector<float> m_bufferL;
    std::vector<float> m_bufferR;
    std::size_t m_bufferLength {0};

    // Pool of 256 grains
    std::array<TextureGrain, maxGrains> m_grains {};

    // Parameter atomics
    std::atomic<float> m_posX {0.5f};
    std::atomic<float> m_sprayY {0.05f};
    std::atomic<float> m_grainLengthMs {50.0f};
    std::atomic<float> m_densityHz {40.0f};
    std::atomic<TextureWindowShape> m_windowShape {TextureWindowShape::hann};
    std::atomic<float> m_reverseProb {0.0f};
    std::atomic<float> m_panSpread {0.5f};
    std::atomic<float> m_pitchSemitones {0.0f};
    std::atomic<float> m_shimmerMix {0.0f};

    // Grain emission timing
    double m_spawnCounterSamples {0.0};
    std::uint32_t m_prngState {0xACE1u};
};

} // namespace dawg::engine::dsp
