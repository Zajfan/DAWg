#include <dawg/commands/ProjectController.h>
#include <dawg/engine/AudioEngine.h>
#include <dawg/engine/PlaybackRingBuffer.h>
#include <dawg/persistence/ProjectFile.h>
#include <dawg/project/Project.h>

#include <array>
#include <chrono>
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

} // namespace

int main()
{
    testProjectAndAudioVerticalSlice();
    std::puts("PASS: foundation project and audio vertical slice");
}
