#include <dawg/commands/GraphCompiler.h>

#include <algorithm>
#include <limits>

namespace dawg::commands {
namespace {
engine::PlaybackRingBuffer* findClipBuffer(
    const project::ClipId clipId,
    const std::span<const ClipBufferBinding> bindings) noexcept
{
    for (const auto& binding : bindings)
    {
        if (binding.clipId == clipId)
            return binding.buffer;
    }
    return nullptr;
}

const project::AudioAsset* findAsset(
    const project::MediaAssetId assetId,
    const std::vector<project::AudioAsset>& assets) noexcept
{
    for (const auto& asset : assets)
    {
        if (asset.id == assetId)
            return &asset;
    }
    return nullptr;
}
} // namespace

std::unique_ptr<const engine::CompiledGraph> GraphCompiler::compile(
    const project::ProjectSnapshot& snapshot,
    const std::uint64_t generation,
    const std::span<const ClipBufferBinding> bindings)
{
    auto graph = std::make_unique<engine::CompiledGraph>();
    graph->generation = generation;

    if (snapshot.tracks.empty() && ! snapshot.clips.empty())
    {
        // Legacy unassigned clips compile onto a default synthetic track 1.
        engine::CompiledTrack syntheticTrack {
            .trackId = 1,
            .muted = false,
            .solo = false,
            .phaseInvert = false,
            .gainLinear = 1.0f,
            .pan = 0.0f,
            .clipOffset = 0,
            .clipCount = 0
        };
        for (const auto& clip : snapshot.clips)
        {
            const auto* asset = findAsset(clip.assetId, snapshot.assets);
            if (asset == nullptr || clip.lengthSamples == 0)
                continue;
            graph->clips.push_back(engine::CompiledClip {
                .clipId = clip.id,
                .assetId = clip.assetId,
                .buffer = findClipBuffer(clip.id, bindings),
                .startSample = clip.startSample,
                .lengthSamples = clip.lengthSamples,
                .sourceOffset = clip.sourceOffset,
                .channelCount = asset->channelCount,
                .gainLinear = clip.gainLinear,
                .fadeInSamples = clip.fadeInSamples,
                .fadeOutSamples = clip.fadeOutSamples
            });
            ++syntheticTrack.clipCount;
        }
        graph->tracks.push_back(syntheticTrack);
        return graph;
    }

    graph->tracks.reserve(snapshot.tracks.size());
    graph->clips.reserve(snapshot.clips.size());

    for (std::size_t trackIndex = 0; trackIndex < snapshot.tracks.size(); ++trackIndex)
    {
        const auto& track = snapshot.tracks[trackIndex];
        const auto clipOffset = static_cast<std::uint32_t>(graph->clips.size());
        std::uint32_t clipCount = 0;

        for (const auto& clip : snapshot.clips)
        {
            const bool belongsToTrack = (clip.trackId == track.id)
                || (clip.trackId == 0 && trackIndex == 0);
            if (! belongsToTrack || clip.lengthSamples == 0)
                continue;

            const auto* asset = findAsset(clip.assetId, snapshot.assets);
            if (asset == nullptr)
                continue;

            graph->clips.push_back(engine::CompiledClip {
                .clipId = clip.id,
                .assetId = clip.assetId,
                .buffer = findClipBuffer(clip.id, bindings),
                .startSample = clip.startSample,
                .lengthSamples = clip.lengthSamples,
                .sourceOffset = clip.sourceOffset,
                .channelCount = asset->channelCount,
                .gainLinear = clip.gainLinear,
                .fadeInSamples = clip.fadeInSamples,
                .fadeOutSamples = clip.fadeOutSamples
            });
            ++clipCount;
        }

        graph->tracks.push_back(engine::CompiledTrack {
            .trackId = track.id,
            .muted = track.muted,
            .solo = track.solo,
            .phaseInvert = track.phaseInvert,
            .gainLinear = track.gainLinear,
            .pan = track.pan,
            .clipOffset = clipOffset,
            .clipCount = clipCount
        });
    }

    return graph;
}

} // namespace dawg::commands
