#include <dawg/project/ArrangementTimelineEngine.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::project {

std::uint64_t ArrangementTimelineEngine::snapToGrid(
    std::uint64_t samplePosition,
    GridSnapDivision snapDiv,
    double bpm,
    std::uint32_t sampleRate) noexcept
{
    if (snapDiv == GridSnapDivision::none || bpm <= 0.0 || sampleRate == 0)
        return samplePosition;

    const double beatSamples = (60.0 / bpm) * static_cast<double>(sampleRate);
    double stepSamples = beatSamples;

    switch (snapDiv)
    {
    case GridSnapDivision::bar:
        stepSamples = beatSamples * 4.0;
        break;
    case GridSnapDivision::halfNote:
        stepSamples = beatSamples * 2.0;
        break;
    case GridSnapDivision::quarterNote:
        stepSamples = beatSamples;
        break;
    case GridSnapDivision::eighthNote:
        stepSamples = beatSamples * 0.5;
        break;
    case GridSnapDivision::sixteenthNote:
        stepSamples = beatSamples * 0.25;
        break;
    case GridSnapDivision::thirtySecond:
        stepSamples = beatSamples * 0.125;
        break;
    case GridSnapDivision::tripletQuarter:
        stepSamples = beatSamples * (2.0 / 3.0);
        break;
    case GridSnapDivision::tripletEighth:
        stepSamples = beatSamples * (1.0 / 3.0);
        break;
    case GridSnapDivision::none:
    default:
        return samplePosition;
    }

    if (stepSamples < 1.0)
        return samplePosition;

    const double snapped = std::round(static_cast<double>(samplePosition) / stepSamples) * stepSamples;
    return static_cast<std::uint64_t>(std::max(0.0, snapped));
}

bool ArrangementTimelineEngine::splitAudioClip(
    std::vector<AudioClip>& clips,
    ClipId clipId,
    std::uint64_t splitTimelineSample,
    ClipId newClipId)
{
    auto it = std::find_if(clips.begin(), clips.end(), [clipId](const AudioClip& c) {
        return c.id == clipId;
    });

    if (it == clips.end())
        return false;

    if (splitTimelineSample <= it->startSample || splitTimelineSample >= it->startSample + it->lengthSamples)
        return false;

    const std::uint64_t leftLength = splitTimelineSample - it->startSample;
    const std::uint64_t rightLength = it->lengthSamples - leftLength;
    const std::uint64_t rightSourceOffset = it->sourceOffset + leftLength;

    AudioClip rightClip = *it;
    rightClip.id = newClipId;
    rightClip.startSample = splitTimelineSample;
    rightClip.sourceOffset = rightSourceOffset;
    rightClip.lengthSamples = rightLength;
    rightClip.fadeInSamples = 0; // Reset crossfade edges if any

    it->lengthSamples = leftLength;
    it->fadeOutSamples = 0;

    clips.push_back(rightClip);
    return true;
}

bool ArrangementTimelineEngine::trimAudioClipStart(AudioClip& clip, std::uint64_t newStartTimelineSample) noexcept
{
    if (newStartTimelineSample <= clip.startSample || newStartTimelineSample >= clip.startSample + clip.lengthSamples)
        return false;

    const std::uint64_t delta = newStartTimelineSample - clip.startSample;
    clip.startSample = newStartTimelineSample;
    clip.sourceOffset += delta;
    clip.lengthSamples -= delta;
    return true;
}

bool ArrangementTimelineEngine::trimAudioClipEnd(AudioClip& clip, std::uint64_t newEndTimelineSample) noexcept
{
    if (newEndTimelineSample <= clip.startSample || newEndTimelineSample >= clip.startSample + clip.lengthSamples)
        return false;

    clip.lengthSamples = newEndTimelineSample - clip.startSample;
    return true;
}

void ArrangementTimelineEngine::slipEditAudioClip(AudioClip& clip, std::int64_t sourceDeltaSamples) noexcept
{
    if (sourceDeltaSamples >= 0)
    {
        clip.sourceOffset += static_cast<std::uint64_t>(sourceDeltaSamples);
    }
    else
    {
        const auto absDelta = static_cast<std::uint64_t>(-sourceDeltaSamples);
        if (clip.sourceOffset > absDelta)
            clip.sourceOffset -= absDelta;
        else
            clip.sourceOffset = 0;
    }
}

bool ArrangementTimelineEngine::crossfadeAudioClips(
    AudioClip& outgoingClip,
    AudioClip& incomingClip,
    std::uint32_t crossfadeSamples) noexcept
{
    if (crossfadeSamples == 0)
        return false;

    if (outgoingClip.startSample + outgoingClip.lengthSamples < incomingClip.startSample)
        return false;

    const auto maxFade = static_cast<std::uint32_t>(std::min(outgoingClip.lengthSamples, incomingClip.lengthSamples));
    const auto appliedFade = std::min(crossfadeSamples, maxFade);

    outgoingClip.fadeOutSamples = appliedFade;
    incomingClip.fadeInSamples = appliedFade;
    return true;
}

float ArrangementTimelineEngine::computeTensionValue(
    BezierTensionShape shape,
    float normalizedT,
    float curvature) noexcept
{
    const float t = std::clamp(normalizedT, 0.0f, 1.0f);
    const float k = std::clamp(curvature, 0.01f, 0.99f);

    switch (shape)
    {
    case BezierTensionShape::linear:
        return t;

    case BezierTensionShape::exponential:
    {
        // Exponential growth (starts slow, climbs fast)
        const float alpha = k * 5.0f;
        const float denom = std::expm1(alpha);
        return (std::abs(denom) < 1e-6f) ? t : std::clamp(std::expm1(alpha * t) / denom, 0.0f, 1.0f);
    }

    case BezierTensionShape::logarithmic:
    {
        // Logarithmic saturation (climbs fast, levels off)
        const float alpha = k * 5.0f;
        const float denom = std::log1p(alpha);
        return (std::abs(denom) < 1e-6f) ? t : std::clamp(std::log1p(alpha * t) / denom, 0.0f, 1.0f);
    }

    case BezierTensionShape::sCurve:
    {
        // Sigmoidal S-curve using cosine transition
        return 0.5f * (1.0f - std::cos(std::numbers::pi_v<float> * t));
    }

    default:
        return t;
    }
}

void ArrangementTimelineEngine::AutomationRecorder::onTouchDown(
    std::uint64_t /*samplePos*/,
    float initialValue) noexcept
{
    m_isTouching = true;
    m_lastRecordedValue = initialValue;

    if (m_mode == AutomationMode::write || m_mode == AutomationMode::touch)
    {
        m_isRecording = true;
    }
}

void ArrangementTimelineEngine::AutomationRecorder::onControlMoved(
    std::uint64_t samplePos,
    float value,
    AutomationLane& lane)
{
    if (m_mode == AutomationMode::read)
        return;

    m_lastRecordedValue = value;
    m_isRecording = true;

    // Insert or update point in lane
    auto it = std::lower_bound(
        lane.points.begin(),
        lane.points.end(),
        samplePos,
        [](const AutomationPoint& pt, std::uint64_t pos) {
            return pt.samplePosition < pos;
        });

    if (it != lane.points.end() && it->samplePosition == samplePos)
    {
        it->value = value;
    }
    else
    {
        lane.points.insert(it, AutomationPoint {
            .id = m_nextPointId++,
            .samplePosition = samplePos,
            .value = value,
            .curveTension = 0.0f
        });
    }
}

void ArrangementTimelineEngine::AutomationRecorder::onTouchUp(
    std::uint64_t samplePos,
    AutomationLane& lane)
{
    m_isTouching = false;

    if (m_mode == AutomationMode::touch)
    {
        // In touch mode: create smooth auto-return ramp to baseline value
        const std::uint64_t returnSamplePos = samplePos + m_returnRampSamples;
        auto it = std::lower_bound(
            lane.points.begin(),
            lane.points.end(),
            returnSamplePos,
            [](const AutomationPoint& pt, std::uint64_t pos) {
                return pt.samplePosition < pos;
            });

        lane.points.insert(it, AutomationPoint {
            .id = m_nextPointId++,
            .samplePosition = returnSamplePos,
            .value = m_baselineValue,
            .curveTension = 0.0f
        });

        m_isRecording = false;
    }
    else if (m_mode == AutomationMode::latch)
    {
        // In latch mode: stays at m_lastRecordedValue, continues holding until transport stops
        m_isRecording = true;
    }
    else if (m_mode == AutomationMode::write)
    {
        m_isRecording = true;
    }
}

std::optional<AudioClip> ArrangementTimelineEngine::dropSessionAudioClipToTimeline(
    TrackId targetTrackId,
    std::uint64_t targetStartSample,
    ClipId newClipId,
    const SessionClipDropInfo& info)
{
    if (info.isMidi || info.lengthSamples == 0)
        return std::nullopt;

    AudioClip clip {};
    clip.id = newClipId;
    clip.assetId = info.assetId;
    clip.startSample = targetStartSample;
    clip.sourceOffset = info.sourceOffset;
    clip.lengthSamples = info.lengthSamples;
    clip.trackId = targetTrackId;
    clip.gainLinear = info.gainLinear;
    clip.fadeInSamples = 0;
    clip.fadeOutSamples = 0;
    clip.warpingEnabled = false;

    return clip;
}

} // namespace dawg::project
