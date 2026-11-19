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
    graph->loop = engine::LoopRegion {
        .enabled = snapshot.loop.enabled,
        .startSample = snapshot.loop.startSample,
        .endSample = snapshot.loop.endSample
    };
    graph->bpm = snapshot.tempo.bpm;
    graph->timeSigNumerator = snapshot.tempo.timeSigNumerator;
    graph->timeSigDenominator = snapshot.tempo.timeSigDenominator;
    graph->metronomeEnabled = snapshot.tempo.metronomeEnabled;

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

        std::vector<engine::CompiledInsert> compiledInserts;
        compiledInserts.reserve(track.inserts.size());
        for (const auto& ins : track.inserts)
        {
            compiledInserts.push_back(engine::CompiledInsert {
                .slotIndex = ins.slotIndex,
                .dspType = static_cast<std::uint8_t>(ins.type),
                .bypassed = ins.bypassed,
                .name = ins.name,
                .pluginId = ins.pluginId,
                .parameters = ins.parameters
            });
        }

        std::vector<engine::CompiledSend> compiledSends;
        compiledSends.reserve(track.sends.size());
        for (const auto& s : track.sends)
        {
            compiledSends.push_back(engine::CompiledSend {
                .targetTrackId = s.targetTrackId,
                .levelLinear = s.levelLinear,
                .preFader = s.preFader
            });
        }

        graph->tracks.push_back(engine::CompiledTrack {
            .trackId = track.id,
            .muted = track.muted,
            .solo = track.solo,
            .phaseInvert = track.phaseInvert,
            .gainLinear = track.gainLinear,
            .pan = track.pan,
            .clipOffset = clipOffset,
            .clipCount = clipCount,
            .trackKind = static_cast<std::uint8_t>(track.kind),
            .inserts = std::move(compiledInserts),
            .sends = std::move(compiledSends)
        });
    }

    graph->midiClips.reserve(snapshot.midiClips.size());
    for (const auto& mClip : snapshot.midiClips)
    {
        if (mClip.lengthSamples == 0)
            continue;

        engine::CompiledMidiClip cmClip {
            .clipId = mClip.id,
            .trackId = mClip.trackId,
            .startSample = mClip.startSample,
            .lengthSamples = mClip.lengthSamples,
            .notes = {}
        };
        cmClip.notes.reserve(mClip.notes.size());
        for (const auto& n : mClip.notes)
        {
            if (n.lengthSamples == 0 || n.pitch > 127)
                continue;

            cmClip.notes.push_back(engine::CompiledMidiNote {
                .noteId = n.id,
                .startSample = n.startSample,
                .lengthSamples = n.lengthSamples,
                .pitch = n.pitch,
                .velocity = n.velocity
            });
        }
        graph->midiClips.push_back(std::move(cmClip));
    }

    graph->automationLanes.reserve(snapshot.automationLanes.size());
    for (const auto& lane : snapshot.automationLanes)
    {
        engine::CompiledAutomationLane cLane {
            .id = lane.id,
            .targetKind = static_cast<std::uint8_t>(lane.target.kind),
            .trackId = lane.target.trackId,
            .slotIndex = lane.target.slotIndex,
            .parameterIndex = lane.target.parameterIndex,
            .points = {}
        };
        cLane.points.reserve(lane.points.size());
        for (const auto& pt : lane.points)
        {
            cLane.points.push_back(engine::CurvePoint {
                .samplePosition = pt.samplePosition,
                .value = pt.value,
                .curveTension = pt.curveTension
            });
        }
        std::sort(cLane.points.begin(), cLane.points.end(),
                  [](const engine::CurvePoint& a, const engine::CurvePoint& b) noexcept {
                      return a.samplePosition < b.samplePosition;
                  });
        graph->automationLanes.push_back(std::move(cLane));
    }

    return graph;
}

} // namespace dawg::commands
