#pragma once

#include <dawg/project/ProjectTypes.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <optional>
#include <vector>

namespace dawg::project {

enum class GridSnapDivision : std::uint8_t
{
    none,          // Sample-accurate (no snapping)
    bar,           // 1 Bar (assuming 4/4)
    halfNote,      // 1/2
    quarterNote,   // 1/4 (1 beat)
    eighthNote,    // 1/8
    sixteenthNote, // 1/16
    thirtySecond,  // 1/32
    tripletQuarter,// 1/4T
    tripletEighth  // 1/8T
};

enum class BezierTensionShape : std::uint8_t
{
    linear,
    exponential,
    logarithmic,
    sCurve
};

enum class AutomationMode : std::uint8_t
{
    read,
    write,
    touch,
    latch
};

struct SessionClipDropInfo
{
    MediaAssetId assetId {0};
    std::uint64_t sourceOffset {0};
    std::uint64_t lengthSamples {0};
    float gainLinear {1.0f};
    bool isMidi {false};
    std::vector<MidiNote> midiNotes {};
};

// Arrangement Timeline & Bezier Automation Engine
// Provides multi-track timeline editing, snapping, Bezier curves,
// Touch/Latch automation modes, and Session-to-Arrangement dropping.
class ArrangementTimelineEngine final
{
public:
    ArrangementTimelineEngine() = default;

    // Grid Snapping
    [[nodiscard]] static std::uint64_t snapToGrid(
        std::uint64_t samplePosition,
        GridSnapDivision snapDiv,
        double bpm,
        std::uint32_t sampleRate) noexcept;

    // Clip Editing Operations (Audio clips)
    static bool splitAudioClip(
        std::vector<AudioClip>& clips,
        ClipId clipId,
        std::uint64_t splitTimelineSample,
        ClipId newClipId);

    static bool trimAudioClipStart(AudioClip& clip, std::uint64_t newStartTimelineSample) noexcept;
    static bool trimAudioClipEnd(AudioClip& clip, std::uint64_t newEndTimelineSample) noexcept;
    static void slipEditAudioClip(AudioClip& clip, std::int64_t sourceDeltaSamples) noexcept;
    static bool crossfadeAudioClips(AudioClip& outgoingClip, AudioClip& incomingClip, std::uint32_t crossfadeSamples) noexcept;

    // Bezier Tension Shape Evaluation
    [[nodiscard]] static float computeTensionValue(BezierTensionShape shape, float normalizedT, float curvature = 0.5f) noexcept;

    // Real-Time Automation Recording State Machine (Read, Write, Touch, Latch)
    class AutomationRecorder
    {
    public:
        AutomationRecorder() = default;

        void setMode(AutomationMode mode) noexcept { m_mode = mode; }
        [[nodiscard]] AutomationMode mode() const noexcept { return m_mode; }

        void setBaselineValue(float baseline) noexcept { m_baselineValue = baseline; }
        [[nodiscard]] float baselineValue() const noexcept { return m_baselineValue; }

        void setReturnRampSamples(std::uint32_t samples) noexcept { m_returnRampSamples = samples; }
        [[nodiscard]] std::uint32_t returnRampSamples() const noexcept { return m_returnRampSamples; }

        void onTouchDown(std::uint64_t samplePos, float initialValue) noexcept;
        void onControlMoved(std::uint64_t samplePos, float value, AutomationLane& lane);
        void onTouchUp(std::uint64_t samplePos, AutomationLane& lane);

        [[nodiscard]] bool isTouching() const noexcept { return m_isTouching; }
        [[nodiscard]] bool isRecording() const noexcept { return m_isRecording; }
        [[nodiscard]] float lastRecordedValue() const noexcept { return m_lastRecordedValue; }

    private:
        AutomationMode m_mode {AutomationMode::read};
        float m_baselineValue {1.0f};
        float m_lastRecordedValue {1.0f};
        std::uint32_t m_returnRampSamples {4800}; // ~100ms at 48kHz
        bool m_isTouching {false};
        bool m_isRecording {false};
        std::uint64_t m_nextPointId {1000};
    };

    // Session-to-Arrangement Drop
    static std::optional<AudioClip> dropSessionAudioClipToTimeline(
        TrackId targetTrackId,
        std::uint64_t targetStartSample,
        ClipId newClipId,
        const SessionClipDropInfo& info);
};

} // namespace dawg::project
