#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace dawg::engine::dsp {

enum class ScrubMode : std::uint8_t
{
    jog,     // Tactile jog-wheel bursts with friction deceleration
    shuttle  // Continuous variable-speed playback with tape reel inertia
};

// Variable-Speed Bi-Directional Tape Audio Scrub Buffer & MCU Jog-Wheel Shuttle Engine
// Features 4-point cubic Hermite interpolation, bi-directional playback (-4.0x to +4.0x),
// tape reel inertia ballistics, magnetic head gap frequency modeling, and timeline playhead tracking.
class AudioScrubEngine final
{
public:
    static constexpr std::size_t MaxBufferSamples = 192000; // 4 seconds at 48 kHz
    static constexpr float MaxScrubSpeed = 4.0f;
    static constexpr float MuteSpeedThreshold = 0.01f;

    using PlayheadScrubCallback = std::function<void(std::int64_t samplePosition, float currentSpeed)>;

    AudioScrubEngine() noexcept;
    ~AudioScrubEngine() = default;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    // Mode & Dynamics Configuration
    void setMode(ScrubMode mode) noexcept { m_mode = mode; }
    [[nodiscard]] ScrubMode mode() const noexcept { return m_mode; }

    void setInertia(float inertia) noexcept { m_inertiaCoeff = std::clamp(inertia, 0.001f, 0.5f); }
    [[nodiscard]] float inertia() const noexcept { return m_inertiaCoeff; }

    void setFriction(float friction) noexcept { m_frictionCoeff = std::clamp(friction, 0.80f, 0.999f); }
    [[nodiscard]] float friction() const noexcept { return m_frictionCoeff; }

    void setJogSensitivity(float sensitivity) noexcept { m_jogSensitivity = std::clamp(sensitivity, 0.01f, 2.0f); }
    [[nodiscard]] float jogSensitivity() const noexcept { return m_jogSensitivity; }

    // Transport Shuttle & Jog-Wheel Events
    // Invoked from Mackie Control MCU jog wheel (CC 0x3C) or GUI scrub controls
    void onJogWheelTurn(float ticks) noexcept;
    void setShuttleSpeed(float speed) noexcept;
    [[nodiscard]] float currentSpeed() const noexcept { return m_currentSpeed; }
    [[nodiscard]] float targetSpeed() const noexcept { return m_targetSpeed; }

    // Timeline Position
    void setTimelinePosition(std::int64_t samplePos) noexcept;
    [[nodiscard]] std::int64_t timelinePosition() const noexcept { return m_timelineSamplePos; }

    void setPlayheadScrubCallback(PlayheadScrubCallback cb) { m_scrubCb = std::move(cb); }

    // Audio Buffer Streaming (Zero Heap Allocation)
    // Feeds recent timeline audio into the circular scrub buffer
    void writeAudio(
        const float* left,
        const float* right,
        std::size_t numSamples,
        std::int64_t timelinePos) noexcept;

    // Real-Time Audio Output Rendering (Zero Heap Allocation)
    // Reads from scrub buffer at variable speed with cubic Hermite interpolation and tape head saturation
    void process(
        float* leftOut,
        float* rightOut,
        std::size_t numSamples) noexcept;

    [[nodiscard]] bool isScrubbing() const noexcept { return std::abs(m_currentSpeed) >= MuteSpeedThreshold; }

private:
    [[nodiscard]] float readHermite(const std::vector<float>& buf, double pos) const noexcept;

    double m_sampleRate {48000.0};
    ScrubMode m_mode {ScrubMode::jog};

    float m_inertiaCoeff {0.05f};     // Tape reel inertia damping for shuttle
    float m_frictionCoeff {0.92f};    // Mechanical friction decay for jog ticks
    float m_jogSensitivity {0.25f};   // Speed impulse per jog tick

    float m_targetSpeed {0.0f};
    float m_currentSpeed {0.0f};

    std::int64_t m_timelineSamplePos {0};
    double m_bufferPlayheadPos {0.0};
    std::size_t m_bufferWritePos {0};

    std::vector<float> m_ringBufferL;
    std::vector<float> m_ringBufferR;

    // Tape head gap filter states
    float m_tapeFilterStateL {0.0f};
    float m_tapeFilterStateR {0.0f};

    PlayheadScrubCallback m_scrubCb;
};

} // namespace dawg::engine::dsp
