#include <dawg/engine/dsp/DrumSliceEngine.h>

#include <algorithm>
#include <cmath>

namespace dawg::engine::dsp {

DrumSliceResult DrumSliceEngine::sliceLoopToDrumKit(
    const float* audioL,
    const float* audioR,
    const std::size_t sampleCount,
    const double sampleRate,
    const double bpm,
    DrumPadSamplerNode* drumSampler,
    const std::size_t maxSlices)
{
    DrumSliceResult result;
    if (audioL == nullptr || sampleCount == 0 || sampleRate <= 0.0 || bpm <= 0.0)
        return result;

    const auto slices = TransientSlicerEngine::detectSlices(audioL, sampleCount, sampleRate, maxSlices, 1.2f);
    if (slices.empty())
        return result;

    result.sliceCount = slices.size();
    result.sliceStartSamples.reserve(slices.size());
    result.sliceBeatPositions.reserve(slices.size());
    result.generatedNotes.reserve(slices.size());

    const double totalBeats = (static_cast<double>(sampleCount) / sampleRate) * (bpm / 60.0);
    result.totalDurationBeats = std::max(1.0, totalBeats);

    if (drumSampler != nullptr)
    {
        TransientSlicerEngine::sliceToDrumPadSampler(audioL, audioR != nullptr ? audioR : audioL,
                                                    sampleCount, sampleRate, *drumSampler);
    }

    constexpr std::uint8_t baseMidiPitch = 36; // C1 = Standard General MIDI Bass Drum

    for (std::size_t i = 0; i < slices.size(); ++i)
    {
        const auto& sl = slices[i];
        result.sliceStartSamples.push_back(sl.startSample);

        const double startSec = static_cast<double>(sl.startSample) / sampleRate;
        const double endSec = static_cast<double>(sl.endSample) / sampleRate;
        const double beatPos = startSec * (bpm / 60.0);
        const double beatDur = std::max(0.0625, (endSec - startSec) * (bpm / 60.0));

        result.sliceBeatPositions.push_back(beatPos);

        SlicedMidiNote note;
        note.pitch = static_cast<std::uint8_t>(baseMidiPitch + (i % 16));
        note.startBeat = beatPos;
        note.durationBeats = beatDur;
        note.velocity = std::clamp(static_cast<std::uint8_t>(sl.peakAmplitude * 127.0f), static_cast<std::uint8_t>(40), static_cast<std::uint8_t>(127));
        result.generatedNotes.push_back(note);
    }

    return result;
}

std::size_t DrumSliceEngine::addWarpMarker(
    std::vector<SlicedWarpMarker>& markers,
    const std::size_t samplePosition,
    const double bpm,
    const double sampleRate)
{
    if (bpm <= 0.0 || sampleRate <= 0.0)
        return markers.size();

    const double beat = (static_cast<double>(samplePosition) / sampleRate) * (bpm / 60.0);
    SlicedWarpMarker marker;
    marker.sourceSample = samplePosition;
    marker.beatPosition = beat;
    marker.pinned = true;

    markers.push_back(marker);
    std::sort(markers.begin(), markers.end(),
              [](const auto& a, const auto& b) { return a.sourceSample < b.sourceSample; });

    for (std::size_t i = 0; i < markers.size(); ++i)
    {
        if (markers[i].sourceSample == samplePosition)
            return i;
    }
    return markers.size() - 1;
}

bool DrumSliceEngine::removeWarpMarker(std::vector<SlicedWarpMarker>& markers, const std::size_t index)
{
    if (index >= markers.size())
        return false;

    markers.erase(markers.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

void DrumSliceEngine::quantizeWarpMarkers(
    std::vector<SlicedWarpMarker>& markers,
    const double gridDivision,
    const float strength)
{
    if (gridDivision <= 0.0 || markers.empty())
        return;

    const float clampedStrength = std::clamp(strength, 0.0f, 1.0f);

    for (auto& marker : markers)
    {
        const double targetBeat = std::round(marker.beatPosition / gridDivision) * gridDivision;
        marker.beatPosition = marker.beatPosition + (targetBeat - marker.beatPosition) * static_cast<double>(clampedStrength);
    }
}

} // namespace dawg::engine::dsp
