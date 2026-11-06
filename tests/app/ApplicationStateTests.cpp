#include "ApplicationState.h"

#include <dawg/commands/ProjectController.h>
#include <dawg/engine/AudioEngine.h>

#include <cstdio>
#include <cstdlib>

namespace {
void require(bool condition, const char* message)
{
    if (! condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(EXIT_FAILURE);
    }
}

void testCommandsUpdateEditorAndPublishToEngine()
{
    dawg::engine::AudioEngine engine;
    require(engine.prepare({48000.0, 32, 2}), "engine prepares");
    dawg::commands::ProjectController controller {dawg::project::Project::create("Untitled", 1)};
    dawg::app::ApplicationState application {controller, engine};
    const auto before = application.editorViewModel();
    require(before.trackCount() == 0, "new editor has no tracks");
    require(application.dispatch(dawg::project::AddAudioTrack {"Audio 1"}), "add command succeeds");
    const auto after = application.editorViewModel();
    require(after.trackCount() == 1, "editor reports one track after application command");
    require(after.project.tracks.front().name == "Audio 1", "editor receives track name");
    require(before.trackCount() == 0, "previous editor value remains unchanged");
    require(engine.renderSnapshotGeneration() == 0, "application waits for the audio block boundary");
    engine.process({}, {}, {});
    require(engine.renderSnapshotGeneration() == 1, "application publishes accepted command to engine");
    require(! application.dispatch(dawg::project::AddAudioTrack {""}), "invalid command is rejected");
    require(application.editorViewModel().trackCount() == 1, "rejected edit preserves editor state");
    engine.process({}, {}, {});
    require(engine.renderSnapshotGeneration() == 1, "rejected edit preserves engine generation");

    require(application.dispatch(dawg::project::RenameTrack {1, "Renamed"}), "rename succeeds");
    require(application.dispatch(dawg::project::RemoveTrack {1}), "remove succeeds");
    require(application.flushRenderSnapshot(), "pending render snapshot can be retried on a message tick");
    engine.process({}, {}, {});
    require(engine.renderSnapshotGeneration() == 3, "audio adopts latest application edit");
    require(application.editorViewModel().trackCount() == 0, "editor reflects removal");
    require(after.project.tracks.front().name == "Audio 1", "held editor value owns its track data");
}
} // namespace

int main()
{
    testCommandsUpdateEditorAndPublishToEngine();
}
