#include <dawg/commands/ProjectController.h>
#include <dawg/engine/AudioEngine.h>
#include <dawg/persistence/ProjectFile.h>
#include <dawg/project/Project.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

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

void testProjectAndAudioVerticalSlice()
{
    using namespace dawg::commands;
    using namespace dawg::engine;
    using namespace dawg::persistence;
    using namespace dawg::project;

    TestDirectory directory;
    const auto path = directory.path / "vertical-slice.dawg";

    AudioEngine engine;
    require(engine.prepare({48000.0, 64, 2}), "engine prepares");

    ProjectController controller {Project::create("Untitled", 1)};
    require(controller.dispatch(AddAudioTrack {"Audio 1"}), "track command succeeds");
    const auto saved = controller.snapshot();
    require(saved.tracks.size() == 1 && saved.tracks.front().name == "Audio 1",
            "project snapshot contains the created track");
    require(saveProject(saved, path).has_value(), "project saves");

    require(engine.publishRenderSnapshot(controller.latestRenderSnapshot()),
            "render snapshot publishes");
    require(engine.enqueueCommand({EngineCommandType::play, 0}), "play queues");

    std::array<std::array<float, 64>, 2> samples {};
    for (auto& channel : samples)
        channel.fill(1.0f);
    std::array<float*, 2> channels {samples[0].data(), samples[1].data()};
    const AudioBufferView output {channels.data(), channels.size(), samples[0].size()};
    engine.process(output, {}, {});
    require(engine.transportState().playing, "transport is playing");
    require(engine.renderSnapshotGeneration() == controller.latestRenderSnapshot().generation,
            "audio adopts the project snapshot at the block boundary");
    for (const auto& channel : samples)
        for (const auto sample : channel)
            require(sample == 0.0f, "empty graph renders deterministic silence");

    const auto loaded = loadProject(path);
    require(loaded.has_value(), "saved project reloads");
    require(loaded->id == saved.id && loaded->name == saved.name
                && loaded->tracks == saved.tracks,
            "reloaded project equals saved snapshot");
}

} // namespace

int main()
{
    testProjectAndAudioVerticalSlice();
    std::puts("PASS: foundation project and audio vertical slice");
}
