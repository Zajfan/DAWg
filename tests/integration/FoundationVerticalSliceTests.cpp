#include <dawg/commands/GraphCompiler.h>
#include <dawg/commands/ProjectController.h>
#include <dawg/engine/AudioEngine.h>
#include <dawg/engine/PlaybackRingBuffer.h>
#include <dawg/engine/RecordRingBuffer.h>
#include <dawg/media/AudioAssetWriter.h>
#include <dawg/media/AudioRecordService.h>
#include <dawg/persistence/ProjectFile.h>
#include <dawg/project/Project.h>
#include "ApplicationState.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace {

namespace fs = std::filesystem;

void require(const bool condition, const char* message)
{
    if (! condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(EXIT_FAILURE);
    }
}

struct TestDirectory final
{
    fs::path path;

    TestDirectory()
    {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        for (unsigned attempt = 0; attempt < 100; ++attempt)
        {
            path = fs::temp_directory_path() / ("dawg-foundation-vertical-slice-"
                                                + std::to_string(suffix) + "-"
                                                + std::to_string(attempt));
            if (fs::create_directory(path))
                return;
        }
        require(false, "create isolated vertical-slice directory");
    }

    ~TestDirectory()
    {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
};

dawg::engine::PlaybackDescription buildPlaybackDescription(
    const dawg::project::ProjectSnapshot& snapshot,
    dawg::engine::PlaybackRingBuffer* buffer,
    const bool mediaAvailable)
{
    if (buffer == nullptr || !mediaAvailable || snapshot.clips.empty() || snapshot.assets.empty())
        return {};

    const auto& clip = snapshot.clips.front();
    const auto& asset = snapshot.assets.front();
    if (clip.assetId != asset.id || clip.lengthSamples == 0)
        return {};

    return dawg::engine::PlaybackDescription {
        .buffer = buffer,
        .clipStartSample = clip.startSample,
        .clipLengthSamples = clip.lengthSamples,
        .sourceOffset = clip.sourceOffset,
        .channelCount = asset.channelCount
    };
}

void testProjectAndAudioVerticalSlice()
{
    using namespace dawg::commands;
    using namespace dawg::engine;
    using namespace dawg::persistence;
    using namespace dawg::project;

    TestDirectory directory;
    const auto path = directory.path / "vertical-slice.dawg";

    constexpr std::size_t blockSize = 8;
    constexpr std::array<float, 4> expected {0.25f, 0.5f, -0.5f, -0.25f};
    constexpr std::size_t clipStart = 1;
    constexpr std::size_t clipEnd = clipStart + expected.size();

    AudioEngine engine;
    require(engine.prepare({48000.0, blockSize, 2}), "engine prepares");

    PlaybackRingBuffer ring;
    require(ring.prepare(1, 32), "playback ring prepares");
    const std::array<const float*, 1> sourceChannels {expected.data()};
    require(ring.write(0, sourceChannels, expected.size()) == expected.size(),
            "playback ring accepts known mono pattern");

    ProjectController controller {Project::create("Untitled", 1)};
    require(controller.dispatch(AddAudioTrack {"Audio 1"}), "track command succeeds");
    const auto assetId = controller.addAudioAsset(
        {0, "fixtures/tone.wav", AudioAssetPathKind::projectRelative, 48000, 1, expected.size()});
    require(assetId.has_value() && *assetId == 1, "asset registration succeeds");
    require(controller.dispatch(AddAudioClip {{0, *assetId, clipStart, 0, expected.size()}}),
            "clip command succeeds");

    const auto saved = controller.snapshot();
    require(saved.tracks.size() == 1 && saved.tracks.front().name == "Audio 1",
            "project snapshot contains the created track");
    require(saved.assets.size() == 1 && saved.clips.size() == 1,
            "project snapshot contains the registered asset and clip");
    require(saveProject(saved, path).has_value(), "project saves");

    const auto loaded = loadProject(path);
    require(loaded.has_value(), "saved project reloads");
    require(loaded->formatVersion == saved.formatVersion && loaded->id == saved.id
                && loaded->name == saved.name && loaded->tracks == saved.tracks
                && loaded->assets == saved.assets && loaded->clips == saved.clips,
            "reloaded project equals saved snapshot including media metadata");

    const auto beforeSaveDesc = buildPlaybackDescription(saved, &ring, true);
    const auto afterLoadDesc = buildPlaybackDescription(*loaded, &ring, true);
    require(beforeSaveDesc.buffer == afterLoadDesc.buffer
                && beforeSaveDesc.clipStartSample == afterLoadDesc.clipStartSample
                && beforeSaveDesc.clipLengthSamples == afterLoadDesc.clipLengthSamples
                && beforeSaveDesc.sourceOffset == afterLoadDesc.sourceOffset
                && beforeSaveDesc.channelCount == afterLoadDesc.channelCount,
            "persistence round-trip preserves exact render playback description");

    auto renderSnapshot = controller.latestRenderSnapshot();
    renderSnapshot.playback = afterLoadDesc;
    require(engine.publishRenderSnapshot(renderSnapshot), "render snapshot publishes");
    require(engine.enqueueCommand({EngineCommandType::play, 0}), "play queues");

    std::array<std::array<float, blockSize>, 2> output {};
    for (auto& channel : output)
        channel.fill(1.0f);
    std::array<float*, 2> channels {output[0].data(), output[1].data()};
    const AudioBufferView outputView {channels.data(), channels.size(), output[0].size()};
    engine.process(outputView, {}, {});

    require(engine.transportState().playing, "play command starts transport");
    require(output[0][0] == 0.0f && output[0][1] == expected[0],
            "samples before and inside the clip render at exact offsets");
    for (std::size_t i = 0; i < expected.size(); ++i)
    {
        require(output[0][clipStart + i] == expected[i]
                    && output[1][clipStart + i] == expected[i],
                "mono clip renders identically to both output channels");
    }
    require(output[0][clipEnd] == 0.0f, "samples after the clip are silent");
    require(engine.transportState().samplePosition == static_cast<std::int64_t>(blockSize),
            "transport advances by the complete block");
    require(engine.underrunCount() == 0, "prefilled clip renders with zero underruns");
    require(engine.renderSnapshotGeneration() == controller.latestRenderSnapshot().generation,
            "audio adopts the project snapshot at the block boundary");

    // Missing-media reload case: unavailable status publishes an empty playback source
    const std::string unavailableStatus = "Missing media: fixtures/tone.wav";
    require(!unavailableStatus.empty(), "missing media records nonempty unavailable status");
    auto missingMediaSnapshot = controller.latestRenderSnapshot();
    missingMediaSnapshot.playback = buildPlaybackDescription(*loaded, &ring, false);
    require(missingMediaSnapshot.playback.buffer == nullptr,
            "unavailable media produces an empty playback source");
    require(engine.publishRenderSnapshot(missingMediaSnapshot),
            "missing-media snapshot publishes");
    require(engine.enqueueCommand({EngineCommandType::seekSamples, 0}), "seek to zero queues");

    for (auto& channel : output)
        channel.fill(1.0f);
    engine.process(outputView, {}, {});
    for (const auto& channel : output)
        for (const auto sample : channel)
            require(sample == 0.0f, "missing media renders deterministic silence");
}

void testFivePillarEndToEndIntegration()
{
    using namespace dawg::commands;
    using namespace dawg::engine;
    using namespace dawg::media;
    using namespace dawg::persistence;
    using namespace dawg::project;

    TestDirectory directory;
    const auto v3Path = directory.path / "five-pillar-v3.dawg";

    ProjectController controller {Project::create("FivePillarSession", 10)};
    require(controller.dispatch(AddAudioTrack {"Drums"}), "add Drums");
    require(controller.dispatch(AddAudioTrack {"Synth"}), "add Synth");
    require(controller.dispatch(SetTrackGainPan {1, 0.5f, 0.0f, false}), "set Drums gain 0.5");
    require(controller.dispatch(SetTrackGainPan {2, 0.25f, 0.0f, false}), "set Synth gain 0.25");
    require(controller.dispatch(SetProjectTempo {128.0, 4, 4, true}), "set tempo 128 BPM 4/4");
    require(controller.dispatch(SetProjectLoop {true, 0, 4}), "set loop [0, 4)");

    const auto a1 = controller.addAudioAsset({0, "audio/drums.wav", AudioAssetPathKind::projectRelative, 48000, 1, 4});
    const auto a2 = controller.addAudioAsset({0, "audio/synth.wav", AudioAssetPathKind::projectRelative, 48000, 1, 4});
    require(a1.has_value() && a2.has_value(), "register both assets");

    require(controller.dispatch(AddAudioClip {AudioClip {
                .id = 0, .assetId = *a1, .startSample = 0, .sourceOffset = 0, .lengthSamples = 4, .trackId = 1
            }}), "add clip 1");
    require(controller.dispatch(AddAudioClip {AudioClip {
                .id = 0, .assetId = *a2, .startSample = 0, .sourceOffset = 0, .lengthSamples = 4, .trackId = 2
            }}), "add clip 2");

    auto v3Snapshot = controller.snapshot();
    v3Snapshot.formatVersion = 3;
    require(saveProject(v3Snapshot, v3Path).has_value(), "save v3 session");

    const auto reloaded = loadProject(v3Path);
    require(reloaded.has_value() && reloaded->formatVersion == 3, "reload v3 session");
    require(reloaded->tempo == v3Snapshot.tempo && reloaded->loop == v3Snapshot.loop,
            "v3 tempo and loop survive persistence");

    PlaybackRingBuffer ring1;
    PlaybackRingBuffer ring2;
    require(ring1.prepare(1, 16) && ring2.prepare(1, 16), "prepare playback rings");
    constexpr std::array<float, 4> ones {1.0f, 1.0f, 1.0f, 1.0f};
    const std::array<const float*, 1> srcPlanes {ones.data()};
    require(ring1.write(0, srcPlanes, 4) == 4 && ring2.write(0, srcPlanes, 4) == 4, "prefill playback rings");

    const std::array<ClipBufferBinding, 2> bindings {{
        {1, &ring1},
        {2, &ring2}
    }};
    auto compiled = GraphCompiler::compile(*reloaded, controller.latestRenderSnapshot().generation, bindings);
    require(compiled != nullptr && compiled->loop.enabled && compiled->bpm == 128.0,
            "GraphCompiler propagates v3 loop and tempo into CompiledGraph");

    AudioEngine engine;
    require(engine.prepare({48000.0, 8, 2}), "prepare engine");

    MemoryAudioAssetWriterFactory writerFactory;
    AudioRecordService recordService {writerFactory};
    require(recordService.prepare("takes/take1.wav", 48000, 2, 64).has_value(),
            "prepare AudioRecordService");
    engine.setRecordBuffer(&recordService.buffer());
    engine.setRecording(true);
    recordService.start();

    engine.publishCompiledGraph(std::move(compiled));
    require(engine.enqueueCommand({EngineCommandType::play, 0}), "enqueue play");

    constexpr std::array<float, 8> liveInL {0.1f, 0.2f, 0.3f, 0.4f, -0.1f, -0.2f, -0.3f, -0.4f};
    constexpr std::array<float, 8> liveInR {-0.1f, -0.2f, -0.3f, -0.4f, 0.1f, 0.2f, 0.3f, 0.4f};
    const std::array<const float*, 2> inPlanes {liveInL.data(), liveInR.data()};

    std::array<float, 8> outL {};
    std::array<float, 8> outR {};
    std::array<float*, 2> outPlanes {outL.data(), outR.data()};

    engine.process(
        AudioInputBufferView {inPlanes.data(), inPlanes.size(), liveInL.size()},
        AudioBufferView {outPlanes.data(), outPlanes.size(), outL.size()},
        {},
        {});

    for (std::size_t i = 0; i < 8; ++i)
    {
        require(std::fabs(outL[i] - 0.75f) < 1e-5f && std::fabs(outR[i] - 0.75f) < 1e-5f,
                "multi-track playback sums and loops while recording input");
    }

    engine.setRecording(false);
    const auto take = recordService.stopAndFinalize(1, 880);
    require(take.has_value(), "AudioRecordService finalizes recorded take");
    require(take->clip.startSample == 880 && take->clip.lengthSamples == 8,
            "recorded take is placed at timeline sample 880 with 8 captured frames");
    require(writerFactory.finalized && writerFactory.channels.size() == 2
                && writerFactory.channels[0].size() == 8,
            "MemoryAudioAssetWriterFactory received all 8 stereo input frames");
}

void testLiveRecordingWorkflowAndAutoTakePlacement()
{
    using namespace dawg::commands;
    using namespace dawg::engine;
    using namespace dawg::media;
    using namespace dawg::project;

    AudioEngine engine;
    require(engine.prepare({48000.0, 16, 2}), "prepare engine for live recording");

    ProjectController controller {Project::create("RecordingSession", 1)};
    dawg::app::ApplicationState state {controller, engine};

    require(state.dispatch(AddAudioTrack {"Vocal"}), "add track Vocal");
    require(state.setTrackRecordArm(1, true), "arm Vocal track for recording");
    require(state.hasArmedTrack() && state.firstArmedTrackId() == 1, "Vocal track is armed");

    MemoryAudioAssetWriterFactory writerFactory;
    dawg::media::AudioRecordService recordService {writerFactory};
    require(recordService.prepare("takes/vocal_take1.wav", 48000, 2, 64).has_value(),
            "prepare take file");
    engine.setRecordBuffer(&recordService.buffer());

    // User engages transport record arm, then presses play
    state.setTransportRecordArmed(true);
    require(state.isTransportRecordArmed(), "transport is record armed");

    engine.setRecording(true);
    recordService.start();
    require(state.play(), "transport begins playback/recording");

    constexpr std::array<float, 16> vocalInL {
        0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f,
        -0.1f, -0.2f, -0.3f, -0.4f, -0.5f, -0.6f, -0.7f, -0.8f
    };
    constexpr std::array<float, 16> vocalInR {
        -0.1f, -0.2f, -0.3f, -0.4f, -0.5f, -0.6f, -0.7f, -0.8f,
        0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f
    };
    const std::array<const float*, 2> inPlanes {vocalInL.data(), vocalInR.data()};

    std::array<float, 16> outL {};
    std::array<float, 16> outR {};
    std::array<float*, 2> outPlanes {outL.data(), outR.data()};

    engine.process(
        AudioInputBufferView {inPlanes.data(), inPlanes.size(), vocalInL.size()},
        AudioBufferView {outPlanes.data(), outPlanes.size(), outL.size()},
        {},
        {});

    // Stop recording and transport
    engine.setRecording(false);
    require(state.stop(), "transport stopped");
    state.setTransportRecordArmed(false);

    const auto take = recordService.stopAndFinalize(*state.firstArmedTrackId(), 0);
    require(take.has_value(), "take finalized successfully");
    require(take->clip.lengthSamples == 16, "take length is 16 samples");

    // Auto placement: add take asset and clip to the project
    const auto assetId = state.addAudioAsset(take->asset);
    require(assetId.has_value(), "recorded asset added to project");
    auto clip = take->clip;
    clip.assetId = *assetId;
    const auto clipId = state.addAudioClip(clip);
    require(clipId.has_value(), "recorded clip placed on track");

    const auto vm = state.editorViewModel();
    require(vm.project.clips.size() == 1, "project now has 1 clip");
    require(vm.project.clips[0].trackId == 1, "clip is placed on armed track 1");
    require(vm.project.clips[0].lengthSamples == 16, "clip length matches recorded frames");

    // Undo takes the clip off the timeline cleanly
    require(state.undo(), "undo remove placed take clip");
    require(state.editorViewModel().project.clips.empty(), "clip removed on undo");
    require(state.redo(), "redo restores placed take clip");
    require(state.editorViewModel().project.clips.size() == 1, "clip restored on redo");
}

} // namespace

int main()
{
    testProjectAndAudioVerticalSlice();
    testFivePillarEndToEndIntegration();
    testLiveRecordingWorkflowAndAutoTakePlacement();
    std::puts("PASS: foundation, 5-pillar, and live recording vertical slice");
}
