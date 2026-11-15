#include <dawg/engine/dsp/AudioWarpEngine.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::engine::dsp {

void AudioWarpEngine::sortMarkers()
{
    std::sort(m_markers.begin(), m_markers.end(), [](const WarpMarker& a, const WarpMarker& b) {
        return a.sourceSample < b.sourceSample;
    });
}

std::size_t AudioWarpEngine::addMarker(const std::size_t sourceSample, const double beatPosition, const bool pinned)
{
    // If a marker already exists within 16 samples, update it
    for (std::size_t i = 0; i < m_markers.size(); ++i)
    {
        if (std::abs(static_cast<std::int64_t>(m_markers[i].sourceSample) - static_cast<std::int64_t>(sourceSample)) <= 16)
        {
            m_markers[i].sourceSample = sourceSample;
            m_markers[i].beatPosition = beatPosition;
            m_markers[i].pinned = pinned;
            sortMarkers();
            return i;
        }
    }

    m_markers.push_back(WarpMarker {
        .sourceSample = sourceSample,
        .beatPosition = beatPosition,
        .pinned = pinned
    });

    sortMarkers();

    for (std::size_t i = 0; i < m_markers.size(); ++i)
    {
        if (m_markers[i].sourceSample == sourceSample)
        {
            return i;
        }
    }
    return m_markers.size() - 1;
}

bool AudioWarpEngine::removeMarker(const std::size_t index)
{
    if (index >= m_markers.size())
    {
        return false;
    }
    m_markers.erase(m_markers.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

bool AudioWarpEngine::moveMarker(const std::size_t index, const double newBeatPosition)
{
    if (index >= m_markers.size())
    {
        return false;
    }
    m_markers[index].beatPosition = std::max(0.0, newBeatPosition);
    m_markers[index].pinned = true;
    return true;
}

void AudioWarpEngine::clearMarkers()
{
    m_markers.clear();
}

const WarpMarker* AudioWarpEngine::getMarker(const std::size_t index) const noexcept
{
    if (index >= m_markers.size())
    {
        return nullptr;
    }
    return &m_markers[index];
}

std::size_t AudioWarpEngine::autoDetectTransients(
    const float* audioData,
    const std::size_t sampleCount,
    const double sampleRate,
    const double bpm)
{
    if (audioData == nullptr || sampleCount < 256 || sampleRate <= 0.0 || bpm <= 0.0)
    {
        return 0;
    }

    // Always guarantee start and end boundary markers
    addMarker(0, 0.0, true);

    const double beatsPerSec = bpm / 60.0;
    const auto totalDurationSec = static_cast<double>(sampleCount) / sampleRate;
    const double endBeat = totalDurationSec * beatsPerSec;
    addMarker(sampleCount > 0 ? sampleCount - 1 : 0, endBeat, true);

    // Energy flux transient detector
    constexpr std::size_t blockSize = 128;
    float prevEnergy = 0.0f;
    std::size_t transientsFound = 0;

    for (std::size_t pos = 0; pos + blockSize <= sampleCount; pos += blockSize)
    {
        float energy = 0.0f;
        for (std::size_t j = 0; j < blockSize; ++j)
        {
            const float s = audioData[pos + j];
            energy += s * s;
        }
        energy = std::sqrt(energy / static_cast<float>(blockSize));

        // Detect onset spike
        if (energy > 0.02f && energy > prevEnergy * 2.2f)
        {
            const double sec = static_cast<double>(pos) / sampleRate;
            const double beat = sec * beatsPerSec;
            addMarker(pos, beat, false);
            ++transientsFound;
        }

        prevEnergy = std::max(energy, 0.005f);
    }

    return transientsFound;
}

void AudioWarpEngine::quantizeMarkers(const double gridDivision, const float strength)
{
    if (gridDivision <= 0.0 || m_markers.empty())
    {
        return;
    }

    const float s = std::clamp(strength, 0.0f, 1.0f);

    for (auto& marker : m_markers)
    {
        // Don't modify locked start marker
        if (marker.sourceSample == 0 && marker.beatPosition == 0.0)
        {
            continue;
        }

        const double targetBeat = std::round(marker.beatPosition / gridDivision) * gridDivision;
        marker.beatPosition = marker.beatPosition + (targetBeat - marker.beatPosition) * static_cast<double>(s);
    }
}

void AudioWarpEngine::applyGroove(const ExtractedGroove& groove, const float strength)
{
    if (m_markers.empty())
    {
        return;
    }

    const float s = std::clamp(strength, 0.0f, 1.0f);
    // Groove swing offset in beats (e.g. 16th note swing pushes odd 16th notes by swingOffset)
    const double swingOffset = static_cast<double>(groove.swingPercent * 0.01f) * 0.25 * static_cast<double>(s);

    for (auto& marker : m_markers)
    {
        // Check if marker lands near an offbeat 16th note (0.25, 0.75, 1.25, etc.)
        const double sixteenth = marker.beatPosition * 4.0;
        const auto nearestSixteenth = static_cast<int>(std::round(sixteenth));
        if (nearestSixteenth % 2 == 1 && std::abs(sixteenth - nearestSixteenth) < 0.2)
        {
            marker.beatPosition += swingOffset;
        }
    }
}

double AudioWarpEngine::beatToSourceSample(const double beatPosition, const double bpm, const double sampleRate) const noexcept
{
    if (bpm <= 0.0 || sampleRate <= 0.0)
    {
        return 0.0;
    }

    const double samplesPerBeat = (60.0 / bpm) * sampleRate;

    if (m_markers.empty())
    {
        return std::max(0.0, beatPosition * samplesPerBeat);
    }

    if (m_markers.size() == 1)
    {
        const auto& m0 = m_markers.front();
        return std::max(0.0, static_cast<double>(m0.sourceSample) + (beatPosition - m0.beatPosition) * samplesPerBeat);
    }

    // Extrapolate before first marker
    if (beatPosition <= m_markers.front().beatPosition)
    {
        const auto& m0 = m_markers[0];
        const auto& m1 = m_markers[1];
        const double deltaBeats = m1.beatPosition - m0.beatPosition;
        const double slope = (deltaBeats > 1e-6)
            ? (static_cast<double>(m1.sourceSample) - static_cast<double>(m0.sourceSample)) / deltaBeats
            : samplesPerBeat;
        return std::max(0.0, static_cast<double>(m0.sourceSample) + (beatPosition - m0.beatPosition) * slope);
    }

    // Extrapolate after last marker
    if (beatPosition >= m_markers.back().beatPosition)
    {
        const auto& mEnd = m_markers.back();
        const auto& mPrev = m_markers[m_markers.size() - 2];
        const double deltaBeats = mEnd.beatPosition - mPrev.beatPosition;
        const double slope = (deltaBeats > 1e-6)
            ? (static_cast<double>(mEnd.sourceSample) - static_cast<double>(mPrev.sourceSample)) / deltaBeats
            : samplesPerBeat;
        return std::max(0.0, static_cast<double>(mEnd.sourceSample) + (beatPosition - mEnd.beatPosition) * slope);
    }

    // Binary search for enclosing interval
    std::size_t low = 0;
    std::size_t high = m_markers.size() - 1;

    while (low + 1 < high)
    {
        const std::size_t mid = (low + high) / 2;
        if (m_markers[mid].beatPosition <= beatPosition)
        {
            low = mid;
        }
        else
        {
            high = mid;
        }
    }

    const auto& mA = m_markers[low];
    const auto& mB = m_markers[high];
    const double deltaBeats = mB.beatPosition - mA.beatPosition;

    if (deltaBeats <= 1e-6)
    {
        return static_cast<double>(mA.sourceSample);
    }

    const double t = (beatPosition - mA.beatPosition) / deltaBeats;
    return static_cast<double>(mA.sourceSample) + t * (static_cast<double>(mB.sourceSample) - static_cast<double>(mA.sourceSample));
}

double AudioWarpEngine::sourceSampleToBeat(const double sampleIndex, const double bpm, const double sampleRate) const noexcept
{
    if (bpm <= 0.0 || sampleRate <= 0.0)
    {
        return 0.0;
    }

    const double beatsPerSample = 1.0 / ((60.0 / bpm) * sampleRate);

    if (m_markers.empty())
    {
        return std::max(0.0, sampleIndex * beatsPerSample);
    }

    if (m_markers.size() == 1)
    {
        const auto& m0 = m_markers.front();
        return std::max(0.0, m0.beatPosition + (sampleIndex - static_cast<double>(m0.sourceSample)) * beatsPerSample);
    }

    if (sampleIndex <= static_cast<double>(m_markers.front().sourceSample))
    {
        const auto& m0 = m_markers[0];
        const auto& m1 = m_markers[1];
        const double deltaSamps = static_cast<double>(m1.sourceSample) - static_cast<double>(m0.sourceSample);
        const double slope = (deltaSamps > 1e-6)
            ? (m1.beatPosition - m0.beatPosition) / deltaSamps
            : beatsPerSample;
        return std::max(0.0, m0.beatPosition + (sampleIndex - static_cast<double>(m0.sourceSample)) * slope);
    }

    if (sampleIndex >= static_cast<double>(m_markers.back().sourceSample))
    {
        const auto& mEnd = m_markers.back();
        const auto& mPrev = m_markers[m_markers.size() - 2];
        const double deltaSamps = static_cast<double>(mEnd.sourceSample) - static_cast<double>(mPrev.sourceSample);
        const double slope = (deltaSamps > 1e-6)
            ? (mEnd.beatPosition - mPrev.beatPosition) / deltaSamps
            : beatsPerSample;
        return std::max(0.0, mEnd.beatPosition + (sampleIndex - static_cast<double>(mEnd.sourceSample)) * slope);
    }

    std::size_t low = 0;
    std::size_t high = m_markers.size() - 1;

    while (low + 1 < high)
    {
        const std::size_t mid = (low + high) / 2;
        if (static_cast<double>(m_markers[mid].sourceSample) <= sampleIndex)
        {
            low = mid;
        }
        else
        {
            high = mid;
        }
    }

    const auto& mA = m_markers[low];
    const auto& mB = m_markers[high];
    const double deltaSamps = static_cast<double>(mB.sourceSample) - static_cast<double>(mA.sourceSample);

    if (deltaSamps <= 1e-6)
    {
        return mA.beatPosition;
    }

    const double t = (sampleIndex - static_cast<double>(mA.sourceSample)) / deltaSamps;
    return mA.beatPosition + t * (mB.beatPosition - mA.beatPosition);
}

std::vector<std::vector<float>> AudioWarpEngine::renderWarped(
    const std::span<const std::span<const float>> inputChannels,
    const double sampleRate,
    const double bpm,
    const double startBeat,
    const double endBeat,
    [[maybe_unused]] const WarpMode mode) const
{
    if (inputChannels.empty() || inputChannels[0].empty() || sampleRate <= 0.0 || bpm <= 0.0 || endBeat <= startBeat)
    {
        return {};
    }

    const auto numChannels = inputChannels.size();
    const auto srcSampleCount = inputChannels[0].size();

    const double secondsPerBeat = 60.0 / bpm;
    const double durationSeconds = (endBeat - startBeat) * secondsPerBeat;
    const auto totalOutFrames = static_cast<std::size_t>(std::round(durationSeconds * sampleRate));

    if (totalOutFrames == 0)
    {
        return {};
    }

    std::vector<std::vector<float>> output(numChannels, std::vector<float>(totalOutFrames, 0.0f));

    // Phase-locked warped resampling with cubic Hermite interpolation
    for (std::size_t frame = 0; frame < totalOutFrames; ++frame)
    {
        const double curTimeSec = static_cast<double>(frame) / sampleRate;
        const double curBeat = startBeat + curTimeSec * (bpm / 60.0);

        const double srcPos = beatToSourceSample(curBeat, bpm, sampleRate);
        const auto baseIdx = static_cast<std::int64_t>(std::floor(srcPos));
        const float frac = static_cast<float>(srcPos - static_cast<double>(baseIdx));

        for (std::size_t ch = 0; ch < numChannels; ++ch)
        {
            const auto& src = inputChannels[ch];

            // 4-point cubic Hermite interpolation
            const auto getSample = [&](const std::int64_t idx) -> float {
                if (idx < 0) return src[0];
                if (idx >= static_cast<std::int64_t>(srcSampleCount)) return src[srcSampleCount - 1];
                return src[static_cast<std::size_t>(idx)];
            };

            const float p0 = getSample(baseIdx - 1);
            const float p1 = getSample(baseIdx);
            const float p2 = getSample(baseIdx + 1);
            const float p3 = getSample(baseIdx + 2);

            // Hermite interpolation polynomial
            const float c0 = p1;
            const float c1 = 0.5f * (p2 - p0);
            const float c2 = p0 - 2.5f * p1 + 2.0f * p2 - 0.5f * p3;
            const float c3 = 0.5f * (p3 - p0) + 1.5f * (p1 - p2);

            const float sample = ((c3 * frac + c2) * frac + c1) * frac + c0;
            output[ch][frame] = sample;
        }
    }

    return output;
}

} // namespace dawg::engine::dsp
