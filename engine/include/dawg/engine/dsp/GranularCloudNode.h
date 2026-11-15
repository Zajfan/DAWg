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

enum class GrainWindowShape : std::uint8_t
{
    hann,
    gaussian,
    tukey
};

// Real-Time Granular Synthesis & Audio Texture Cloud Engine (Portal / Omnisphere style)
// Spawns up to 64 micro-grains per voice with continuous audio scrubbing,
// microtonal detuning, stereo dispersal, window shaping, and tape freeze.
class GranularCloudNode final : public ProcessorNode
{
public:
    static constexpr std::size_t maxGrains = 64;
    static constexpr std::size_t maxBufferSeconds = 5;

    GranularCloudNode() noexcept;
    ~GranularCloudNode() override = default;

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void reset() noexcept override;
    [[nodiscard]] std::uint32_t latencySamples() const noexcept override { return 0; }
    void process(ProcessContext& context) noexcept override;

    // Sample Loading (or operates on live input circular stream)
    void loadSampleBuffer(const float* left, const float* right, std::size_t count);

    // Grain Cloud Parameters
    void setGrainSizeMs(float ms) noexcept { m_grainSizeMs = std::clamp(ms, 5.0f, 500.0f); }
    [[nodiscard]] float grainSizeMs() const noexcept { return m_grainSizeMs; }

    void setDensity(float grainsPerSec) noexcept { m_density = std::clamp(grainsPerSec, 1.0f, 100.0f); }
    [[nodiscard]] float density() const noexcept { return m_density; }

    void setSpray(float spray) noexcept { m_spray = std::clamp(spray, 0.0f, 1.0f); }
    [[nodiscard]] float spray() const noexcept { return m_spray; }

    void setScrubPosition(float normPos) noexcept { m_scrubPos = std::clamp(normPos, 0.0f, 1.0f); }
    [[nodiscard]] float scrubPosition() const noexcept { return m_scrubPos; }

    void setPitchShiftSemitones(float st) noexcept { m_pitchShiftSemitones = std::clamp(st, -24.0f, 24.0f); }
    [[nodiscard]] float pitchShiftSemitones() const noexcept { return m_pitchShiftSemitones; }

    void setPitchDetuneCents(float cents) noexcept { m_pitchDetuneCents = std::clamp(cents, -100.0f, 100.0f); }
    [[nodiscard]] float pitchDetuneCents() const noexcept { return m_pitchDetuneCents; }

    void setStereoDispersal(float width) noexcept { m_stereoDispersal = std::clamp(width, 0.0f, 1.0f); }
    [[nodiscard]] float stereoDispersal() const noexcept { return m_stereoDispersal; }

    void setReverseProbability(float prob) noexcept { m_reverseProb = std::clamp(prob, 0.0f, 1.0f); }
    [[nodiscard]] float reverseProbability() const noexcept { return m_reverseProb; }

    void setWindowShape(GrainWindowShape shape) noexcept { m_windowShape = shape; }
    [[nodiscard]] GrainWindowShape windowShape() const noexcept { return m_windowShape; }

    // Tape Freeze & Infinite Drone
    void setFreeze(bool freeze) noexcept { m_frozen = freeze; }
    [[nodiscard]] bool isFrozen() const noexcept { return m_frozen; }

    void setDryWet(float mix) noexcept { m_dryWet = std::clamp(mix, 0.0f, 1.0f); }
    [[nodiscard]] float dryWet() const noexcept { return m_dryWet; }

    // Telemetry: active grain count for UI visualizer
    [[nodiscard]] std::size_t activeGrainCount() const noexcept;

private:
    struct Grain
    {
        bool active {false};
        float readPos {0.0f};
        float speed {1.0f};
        std::size_t age {0};
        std::size_t duration {1000};
        float panL {0.707f};
        float panR {0.707f};
        bool reverse {false};
    };

    void spawnGrain() noexcept;
    [[nodiscard]] float evaluateWindow(std::size_t age, std::size_t duration) const noexcept;

    double m_sampleRate {48000.0};
    float m_grainSizeMs {60.0f};
    float m_density {25.0f};
    float m_spray {0.2f};
    float m_scrubPos {0.5f};
    float m_pitchShiftSemitones {0.0f};
    float m_pitchDetuneCents {0.0f};
    float m_stereoDispersal {0.8f};
    float m_reverseProb {0.1f};
    GrainWindowShape m_windowShape {GrainWindowShape::hann};

    bool m_frozen {false};
    float m_dryWet {0.5f};

    float m_spawnAccumulator {0.0f};
    std::uint32_t m_randState {123456789};

    // Preallocated circular buffer for live stream audio
    std::vector<float> m_liveBufferL;
    std::vector<float> m_liveBufferR;
    std::size_t m_liveWritePos {0};
    bool m_hasCustomSample {false};

    // Grain pool
    std::array<Grain, maxGrains> m_grains {};
};

} // namespace dawg::engine::dsp
