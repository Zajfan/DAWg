#pragma once

#include <dawg/engine/ProcessorNode.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace dawg::engine::dsp {

// Output bus formats for the spherical object panner.
enum class SpatialBusFormat : std::uint8_t
{
    stereo20,   // L R
    surround51, // L R C LFE Ls Rs
    atmos712,   // L R C LFE Lss Rss Lrs Rrs Ltm Rtm
    atmos714    // L R C LFE Lss Rss Lrs Rrs Ltf Rtf Ltr Rtr
};

// Speaker definition. Azimuth convention: 0 deg = front, positive = to the listener's right.
struct SpatialSpeakerDef
{
    const char* label {""};
    float azimuthDeg {0.0f};
    float elevationDeg {0.0f};
    bool isLfe {false};
};

struct SphericalPosition
{
    float azimuthDeg {0.0f};    // -180 .. +180
    float elevationDeg {0.0f};  // -90 .. +90
    float distanceM {1.0f};     // 0.1 .. 50 m
    float size {0.0f};          // 0 = point source .. 1 = fully diffuse
};

enum class SpatialTrajectoryShape : std::uint8_t
{
    none,
    orbit,   // Horizontal circle at fixed elevation
    figure8, // Lemniscate of Gerono in the horizontal plane
    spiral   // Rotating while sweeping elevation from low to high
};

struct SpatialTrajectory
{
    SpatialTrajectoryShape shape {SpatialTrajectoryShape::none};
    double periodSec {4.0};         // Time for one full cycle
    float radiusM {2.0f};
    float elevationDeg {0.0f};      // Orbit / figure-8 elevation, spiral start elevation
    float spiralEndElevationDeg {60.0f};
    float spiralTurns {3.0f};       // Revolutions per spiral period
};

// Immersive spherical object panner (Option 1).
// - Energy-normalised cosine-lobe panning onto 2.0 / 5.1 / 7.1.2 / 7.1.4 layouts
// - Inverse-distance attenuation with air-absorption low-pass (0.1 .. 50 m)
// - Size/divergence diffusion
// - Zero-latency parametric binaural headphone monitor: every speaker feed is virtualised
//   with a spherical-head model (Woodworth ITD + Brown-Duda head-shadow filter).
//   NOTE: this is a parametric HRTF model, not a measured (SOFA) HRIR dataset.
// - Geometric trajectory automation (orbit, figure-8, spiral)
// process() performs no heap allocations; scratch memory is allocated in prepare().
class SpatialObjectPannerNode final : public ProcessorNode
{
public:
    static constexpr std::size_t maxSpeakers = 12;
    static constexpr std::size_t itdBufferSize = 256;

    SpatialObjectPannerNode() noexcept;
    ~SpatialObjectPannerNode() override = default;

    [[nodiscard]] bool prepare(double sampleRate, std::size_t maxBlockSize, std::uint16_t channelCount) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;

    void setFormat(SpatialBusFormat format) noexcept;
    [[nodiscard]] SpatialBusFormat format() const noexcept { return m_format; }

    void setPosition(const SphericalPosition& position) noexcept;
    [[nodiscard]] const SphericalPosition& position() const noexcept { return m_position; }

    void setTrajectory(const SpatialTrajectory& trajectory) noexcept;
    [[nodiscard]] const SpatialTrajectory& trajectory() const noexcept { return m_trajectory; }

    void setBinauralMonitoring(bool enabled) noexcept { m_binaural = enabled; }
    [[nodiscard]] bool isBinauralMonitoring() const noexcept { return m_binaural; }

    void setLfeSendDb(float db) noexcept;

    [[nodiscard]] const std::array<float, maxSpeakers>& currentGains() const noexcept { return m_currentGains; }

    // Static helpers (usable from UI / offline renderers)
    [[nodiscard]] static std::span<const SpatialSpeakerDef> speakerLayout(SpatialBusFormat format) noexcept;
    [[nodiscard]] static std::size_t speakerCount(SpatialBusFormat format) noexcept;
    static void computeGains(SpatialBusFormat format, const SphericalPosition& position,
                             std::array<float, maxSpeakers>& outGains) noexcept;
    [[nodiscard]] static float distanceGain(float distanceM) noexcept;
    [[nodiscard]] static SphericalPosition evaluateTrajectory(const SpatialTrajectory& trajectory,
                                                              const SphericalPosition& base,
                                                              double timeSec) noexcept;

private:
    struct EarModel
    {
        float delaySamples {0.0f};
        float b0 {1.0f};
        float b1 {0.0f};
        float a1 {0.0f};
    };

    struct EarState
    {
        float x1 {0.0f};
        float y1 {0.0f};
    };

    void updateBinauralModels() noexcept;
    void updateAirAbsorption() noexcept;

    double m_sampleRate {48000.0};
    SpatialBusFormat m_format {SpatialBusFormat::atmos714};
    SphericalPosition m_position {};
    SpatialTrajectory m_trajectory {};
    double m_trajectoryTimeSec {0.0};
    bool m_binaural {false};
    float m_lfeGain {0.0f};

    std::array<float, maxSpeakers> m_currentGains {};
    std::array<float, maxSpeakers> m_targetGains {};

    // Distance / air absorption
    float m_distanceGain {1.0f};
    float m_airCoeff {1.0f};
    float m_airState {0.0f};
    float m_lfeCoeff {0.0f};
    float m_lfeState {0.0f};

    // Binaural virtualiser (per speaker, per ear)
    std::array<std::array<EarModel, 2>, maxSpeakers> m_earModels {};
    std::array<std::array<EarState, 2>, maxSpeakers> m_earStates {};
    std::array<std::array<float, itdBufferSize>, maxSpeakers> m_itdBuffers {};
    std::size_t m_itdWritePos {0};

    std::vector<float> m_monoScratch;
};

} // namespace dawg::engine::dsp
