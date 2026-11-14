#include <dawg/media/AudioWarpService.h>

#include <algorithm>
#include <cmath>

namespace dawg::media {

std::vector<project::WarpMarker> AudioWarpService::autoDetectBeatWarpMarkers(
    const std::span<const float> monoAudio,
    const double sampleRate,
    const double projectBpm,
    const float sensitivity)
{
    if (monoAudio.empty() || sampleRate <= 0.0 || projectBpm <= 0.0)
        return {};

    const auto transients = TransientDetector::detect(monoAudio, sampleRate, sensitivity);
    const double samplesPerBeat = (60.0 / projectBpm) * sampleRate;

    std::vector<project::WarpMarker> markers;
    markers.reserve(transients.size() + 2);

    // Initial boundary marker
    markers.push_back(project::WarpMarker {
        .id = 1,
        .sourceSample = 0,
        .targetSample = 0
    });

    for (const auto& marker : transients)
    {
        const auto onsetSample = marker.sampleOffset;
        if (onsetSample < samplesPerBeat * 0.25)
            continue;

        const double beatIndex = std::round(static_cast<double>(onsetSample) / samplesPerBeat);
        const auto targetSample = static_cast<std::uint64_t>(std::max(0.0, beatIndex * samplesPerBeat));

        if (onsetSample > markers.back().sourceSample + 128
            && targetSample > markers.back().targetSample + 128)
        {
            markers.push_back(project::WarpMarker {
                .id = static_cast<std::uint32_t>(markers.size() + 1),
                .sourceSample = onsetSample,
                .targetSample = targetSample
            });
        }
    }

    return markers;
}

std::vector<std::vector<float>> AudioWarpService::renderWarpedAudio(
    const std::span<const std::span<const float>> inputChannels,
    const std::vector<project::WarpMarker>& markers,
    const std::uint64_t totalSourceFrames)
{
    if (inputChannels.empty() || totalSourceFrames == 0)
        return {};

    const std::size_t numChannels = inputChannels.size();
    std::vector<std::vector<float>> output(numChannels);

    if (markers.size() < 2)
    {
        for (std::size_t c = 0; c < numChannels; ++c)
        {
            const auto frames = std::min<std::size_t>(inputChannels[c].size(), totalSourceFrames);
            output[c].assign(inputChannels[c].begin(), inputChannels[c].begin() + frames);
        }
        return output;
    }

    // Sort markers by sourceSample
    auto sortedMarkers = markers;
    std::sort(sortedMarkers.begin(), sortedMarkers.end(),
              [](const auto& a, const auto& b) { return a.sourceSample < b.sourceSample; });

    // Append tail marker if last marker ends before totalSourceFrames
    if (sortedMarkers.back().sourceSample < totalSourceFrames)
    {
        const auto lastSrc = sortedMarkers.back().sourceSample;
        const auto lastTgt = sortedMarkers.back().targetSample;
        const auto tailLen = totalSourceFrames - lastSrc;
        sortedMarkers.push_back(project::WarpMarker {
            .id = static_cast<std::uint32_t>(sortedMarkers.size() + 1),
            .sourceSample = totalSourceFrames,
            .targetSample = lastTgt + tailLen
        });
    }

    for (std::size_t i = 0; i + 1 < sortedMarkers.size(); ++i)
    {
        const auto srcStart = sortedMarkers[i].sourceSample;
        const auto srcEnd = std::min(sortedMarkers[i + 1].sourceSample, totalSourceFrames);
        if (srcEnd <= srcStart)
            continue;

        const auto srcLen = srcEnd - srcStart;
        const auto tgtStart = sortedMarkers[i].targetSample;
        const auto tgtEnd = sortedMarkers[i + 1].targetSample;
        const auto tgtLen = (tgtEnd > tgtStart) ? (tgtEnd - tgtStart) : srcLen;

        const float ratio = static_cast<float>(tgtLen) / static_cast<float>(srcLen);

        std::vector<std::span<const float>> sliceSpans;
        sliceSpans.reserve(numChannels);
        for (std::size_t c = 0; c < numChannels; ++c)
        {
            const auto avail = inputChannels[c].size();
            const auto start = std::min<std::size_t>(srcStart, avail);
            const auto len = std::min<std::size_t>(srcLen, avail - start);
            sliceSpans.push_back(inputChannels[c].subspan(start, len));
        }

        const auto stretched = engine::WsolaTimeStretcher::stretch(sliceSpans, ratio);
        for (std::size_t c = 0; c < numChannels && c < stretched.size(); ++c)
        {
            output[c].insert(output[c].end(), stretched[c].begin(), stretched[c].end());
        }
    }

    return output;
}

} // namespace dawg::media
