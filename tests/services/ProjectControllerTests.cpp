#include <dawg/engine/AudioEngine.h>

#include <dawg/commands/ProjectController.h>
#include <dawg/engine/RenderSnapshotPublisher.h>

#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <utility>

namespace {
void require(const bool condition, const char* message)
{
    if (! condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(EXIT_FAILURE);
    }
}

void testPublicationBoundary()
{
    dawg::engine::AudioEngine engine;
    require(engine.prepare({48000.0, 32, 2}), "engine prepares");
    std::array<float, 32> samples {};
    std::array<float*, 1> channels {samples.data()};
    require(engine.publishRenderSnapshot({1, 1}), "snapshot is accepted");
    require(engine.renderSnapshotGeneration() == 0, "publication waits for block boundary");
    engine.process({channels.data(), 1, samples.size()}, {}, {});
    require(engine.renderSnapshotGeneration() == 1, "audio adopts published generation");
}

using namespace dawg::project;
using dawg::commands::ProjectController;
using dawg::engine::AudioEngine;
using dawg::engine::RenderSnapshotPublisher;

void testProjectEditsPublishAtBlockBoundary()
{
    AudioEngine engine;
    require(engine.prepare({48000.0, 32, 2}), "engine prepares for project edits");
    ProjectController controller {Project::create("Untitled", 1)};
    const auto before = controller.latestRenderSnapshot();
    require(before.generation == 0 && before.trackCount == 0, "new project render state is empty");
    require(controller.dispatch(AddAudioTrack {"Audio 1"}), "command succeeds");
    const auto after = controller.latestRenderSnapshot();
    require(after.generation == 1 && after.trackCount == 1, "edit builds newer render state");
    require(before.generation == 0 && before.trackCount == 0, "prior value stays immutable");
    require(engine.publishRenderSnapshot(after), "prepared engine accepts project snapshot");
    require(engine.renderSnapshotGeneration() == 0, "project publication waits for process");
    std::array<float, 32> samples;
    samples.fill(1.0f);
    std::array<float*, 1> channels {samples.data()};
    engine.process({channels.data(), 1, samples.size()}, {}, {});
    require(engine.renderSnapshotGeneration() == 1, "audio adopts project edit at boundary");
    for (const auto sample : samples)
        require(sample == 0.0f, "snapshot adoption preserves silence");
    require(controller.dispatch(RenameTrack {1, "Renamed"}), "rename succeeds");
    require(controller.latestRenderSnapshot().generation == 2 &&
                controller.latestRenderSnapshot().trackCount == 1, "rename advances generation only");
    require(controller.dispatch(RemoveTrack {1}), "remove succeeds");
    require(controller.latestRenderSnapshot().generation == 3 &&
                controller.latestRenderSnapshot().trackCount == 0, "remove advances and updates count");
    require(engine.publishRenderSnapshot(controller.latestRenderSnapshot()), "next edit publishes");
    require(engine.renderSnapshotGeneration() == 1, "next edit waits for next block");
    engine.process({}, {}, {});
    require(engine.renderSnapshotGeneration() == 3, "empty block adopts latest generation");
}

void testFailedCommandsPreserveState()
{
    ProjectController controller {Project::create("Untitled", 42)};
    require(controller.dispatch(AddAudioTrack {"Original"}), "seed controller");
    const auto saved = controller.snapshot();
    for (const ProjectCommand& command : {ProjectCommand {AddAudioTrack {""}},
            ProjectCommand {RemoveTrack {999}}, ProjectCommand {RenameTrack {999, "Missing"}},
            ProjectCommand {RenameTrack {1, ""}}})
    {
        require(! controller.dispatch(command), "invalid command is rejected");
        const auto after = controller.snapshot();
        require(after.id == saved.id && after.name == saved.name &&
                    after.formatVersion == saved.formatVersion && after.tracks == saved.tracks,
                "failed command leaves project snapshot unchanged");
        require(controller.latestRenderSnapshot().generation == 1 &&
                    controller.latestRenderSnapshot().trackCount == 1, "failure preserves render state");
    }
    require(controller.dispatch(AddAudioTrack {"Second"}), "valid command after failures succeeds");
    require(controller.latestRenderSnapshot().generation == 2 &&
                controller.latestRenderSnapshot().trackCount == 2, "failures consume no generations");
    require(saved.tracks.size() == 1 && saved.tracks.front().name == "Original",
            "project snapshot owns its values across edits");
}

void testLoadedProjectAndIndependentControllers()
{
    auto loaded = Project::fromSnapshot({1, 7, "Loaded", {{9, "Lead", true, false},
                                                        {3, "Bass", false, true}}});
    require(loaded.has_value(), "loaded project reconstructs");
    ProjectController controller {std::move(*loaded)};
    require(controller.latestRenderSnapshot().generation == 0 &&
                controller.latestRenderSnapshot().trackCount == 2, "initial count comes from owned project");
    require(controller.dispatch(AddAudioTrack {"New"}), "loaded project accepts command");
    require(controller.snapshot().tracks.back().id == 10, "controller preserves project ID allocation");
    require(controller.latestRenderSnapshot().generation == 1 &&
                controller.latestRenderSnapshot().trackCount == 3, "loaded edit updates render state");
    ProjectController other {Project::create("Other", 8)};
    require(other.latestRenderSnapshot().generation == 0 && other.snapshot().tracks.empty(),
            "controllers have independent state");
}

void testPublisherCoalescingAndReuse()
{
    RenderSnapshotPublisher publisher;
    require(publisher.consume().generation == 0 && publisher.consume().trackCount == 0,
            "publisher starts with empty snapshot");
    for (std::uint64_t batch = 0; batch < 100; ++batch)
    {
        for (std::uint64_t edit = 1; edit <= 10; ++edit)
            require(publisher.publish({batch * 10 + edit, static_cast<std::uint32_t>(edit)}),
                    "quiescent audio permits publication bursts");
        const auto current = publisher.consume();
        require(current.generation == batch * 10 + 10 && current.trackCount == 10,
                "boundary coalesces to latest complete snapshot");
        require(publisher.consume().generation == current.generation,
                "no pending edit retains current snapshot without replay");
    }
}

void testPublisherConcurrentCoherence()
{
    RenderSnapshotPublisher publisher;
    constexpr std::uint64_t lastGeneration = 200000;
    std::atomic<bool> finished {};
    std::thread producer {[&] {
        for (std::uint64_t generation = 1; generation <= lastGeneration; ++generation)
        {
            while (! publisher.publish({generation, static_cast<std::uint32_t>(generation)}))
                std::this_thread::yield();
        }
        finished.store(true, std::memory_order_release);
    }};
    std::uint64_t previous = 0;
    do
    {
        const auto current = publisher.consume();
        require(current.generation >= previous, "concurrent snapshots never regress");
        require(current.trackCount == current.generation, "concurrent payload is never torn");
        previous = current.generation;
    } while (! finished.load(std::memory_order_acquire));
    producer.join();
    const auto final = publisher.consume();
    require(final.generation == lastGeneration && final.trackCount == lastGeneration,
            "last concurrent edit remains observable");
}

void testEngineBurstAndLifecycle()
{
    AudioEngine engine;
    require(engine.publishRenderSnapshot({11, 1}), "unprepared publication is independent of lifecycle");
    require(engine.publishRenderSnapshot({12, 2}), "newest unconsumed snapshot replaces older one");
    require(engine.renderSnapshotGeneration() == 0, "burst is hidden before boundary");
    require(engine.enqueueCommand({dawg::engine::EngineCommandType::play, 0}), "transport queues beside snapshot");
    engine.process({nullptr, 2, 32}, {}, {});
    require(engine.renderSnapshotGeneration() == 12 && engine.transportState().playing,
            "null-output block applies snapshots and transport commands");
    engine.reset();
    engine.release();
    require(engine.prepare({48000.0, 32, 2}), "engine prepares after release");
    engine.process({}, {}, {});
    require(engine.renderSnapshotGeneration() == 12, "lifecycle preserves adopted generation");
    require(engine.publishRenderSnapshot({13, 3}), "publisher reuses slots after lifecycle calls");
    engine.process({}, {}, {});
    require(engine.renderSnapshotGeneration() == 13, "publication continues after lifecycle calls");
}
}

int main()
{
    testPublicationBoundary();
    testProjectEditsPublishAtBlockBoundary();
    testFailedCommandsPreserveState();
    testLoadedProjectAndIndependentControllers();
    testPublisherCoalescingAndReuse();
    testPublisherConcurrentCoherence();
    testEngineBurstAndLifecycle();
    std::puts("PASS: 7 render publication and controller behavior tests");
}
