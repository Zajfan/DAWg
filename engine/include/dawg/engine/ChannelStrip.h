#pragma once

#include <dawg/engine/CompiledGraph.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>

namespace dawg::engine {

class SmoothedParameter final
{
public:
    constexpr explicit SmoothedParameter(const float initialValue = 1.0f) noexcept
        : m_current(initialValue)
        , m_target(initialValue)
    {
    }

    constexpr void reset(const float value) noexcept
    {
        m_current = value;
        m_target = value;
        m_step = 0.0f;
        m_stepsRemaining = 0;
    }

    constexpr void setTarget(const float target, const std::size_t rampSamples) noexcept
    {
        m_target = target;
        if (rampSamples == 0 || ! (m_current < target || m_current > target))
        {
            m_current = target;
            m_step = 0.0f;
            m_stepsRemaining = 0;
            return;
        }
        m_stepsRemaining = rampSamples;
        m_step = (m_target - m_current) / static_cast<float>(rampSamples);
    }

    [[nodiscard]] constexpr float next() noexcept
    {
        if (m_stepsRemaining == 0)
            return m_current;

        --m_stepsRemaining;
        if (m_stepsRemaining == 0)
        {
            m_current = m_target;
            m_step = 0.0f;
        }
        else
        {
            m_current += m_step;
        }
        return m_current;
    }

    [[nodiscard]] constexpr float current() const noexcept { return m_current; }
    [[nodiscard]] constexpr float target() const noexcept { return m_target; }
    [[nodiscard]] constexpr bool isSmoothing() const noexcept { return m_stepsRemaining > 0; }

private:
    float m_current {1.0f};
    float m_target {1.0f};
    float m_step {0.0f};
    std::size_t m_stepsRemaining {0};
};

struct StereoPanGains
{
    float left {1.0f};
    float right {1.0f};
};

// Compensated equal-power stereo pan law:
// pan = 0.0 -> {1.0, 1.0}; pan = -1.0 -> {sqrt(2), 0.0}; pan = +1.0 -> {0.0, sqrt(2)}.
[[nodiscard]] inline StereoPanGains equalPowerPanGains(const float pan) noexcept
{
    const float clamped = std::clamp(pan, -1.0f, 1.0f);
    if (! (clamped < 0.0f || clamped > 0.0f))
        return {1.0f, 1.0f};

    constexpr float kQuarterPi = std::numbers::pi_v<float> * 0.25f;
    constexpr float kSqrt2 = std::numbers::sqrt2_v<float>;
    const float angle = (clamped + 1.0f) * kQuarterPi;
    return {
        .left = kSqrt2 * std::cos(angle),
        .right = kSqrt2 * std::sin(angle)
    };
}

[[nodiscard]] constexpr float clipEnvelopeGain(
    const std::uint64_t frameInClip,
    const std::uint64_t clipLength,
    const std::uint32_t fadeInSamples,
    const std::uint32_t fadeOutSamples) noexcept
{
    if (clipLength == 0 || frameInClip >= clipLength)
        return 0.0f;

    float gain = 1.0f;
    if (fadeInSamples > 0 && frameInClip < fadeInSamples)
    {
        gain *= static_cast<float>(frameInClip) / static_cast<float>(fadeInSamples);
    }

    if (fadeOutSamples > 0 && clipLength > 0)
    {
        const auto framesFromEnd = (clipLength - 1u) - frameInClip;
        if (framesFromEnd < fadeOutSamples)
        {
            gain *= static_cast<float>(framesFromEnd) / static_cast<float>(fadeOutSamples);
        }
    }

    return gain;
}

[[nodiscard]] constexpr bool isTrackAudible(
    const CompiledTrack& track,
    const bool anyTrackSoloed) noexcept
{
    if (track.muted)
        return false;
    return ! anyTrackSoloed || track.solo;
}

} // namespace dawg::engine
