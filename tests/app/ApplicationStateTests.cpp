#include "ApplicationState.h"

#include <dawg/commands/ProjectController.h>
#include <dawg/engine/AudioEngine.h>

#include <algorithm>
#include <array>
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

void testPlaybackCommandsAndSnapshotComposition()
{
    dawg::engine::AudioEngine engine;
    require(engine.prepare({48000.0, 32, 2}), "engine prepares");
    dawg::commands::ProjectController controller {dawg::project::Project::create("Untitled", 1)};
    dawg::app::ApplicationState state {controller, engine};

    dawg::engine::PlaybackRingBuffer ring;
    require(ring.prepare(1, 32), "ring prepares");
    constexpr std::array<float, 4> source {0.25f, 0.5f, 0.75f, 1.0f};
    const std::array<const float*, 1> sourcePlanes {source.data()};
    require(ring.write(0, sourcePlanes, source.size()) == source.size(), "ring prefill succeeds");

    require(state.dispatch(dawg::project::AddAudioTrack {"Audio 1"}), "add track succeeds");
    const auto assetId = state.addAudioAsset({
        .id = 0,
        .path = "fixture.wav",
        .pathKind = dawg::project::AudioAssetPathKind::projectRelative,
        .sampleRate = 48000,
        .channelCount = 1,
        .frameCount = 4
    });
    require(assetId.has_value() && *assetId == 1, "addAudioAsset assigns asset id");

    const auto clipId = state.addAudioClip({
        .id = 0,
        .assetId = *assetId,
        .startSample = 2,
        .sourceOffset = 0,
        .lengthSamples = 4
    });
    require(clipId.has_value() && *clipId == 1, "addAudioClip assigns clip id");

    state.setPlaybackBuffer(&ring);
    require(state.flushRenderSnapshot(), "playback snapshot publishes");
    require(state.play(), "play callback queues");
    require(state.stop(), "stop callback queues");
    require(state.returnToStart(), "return-to-start callback queues seek zero");
    require(state.play(), "second play callback queues");

    std::array<float, 8> left {};
    std::array<float, 8> right {};
    std::array<float*, 2> output {left.data(), right.data()};
    engine.process({output.data(), output.size(), left.size()}, {}, {});

    require(engine.transportState().playing, "FIFO commands leave transport playing");
    require(engine.transportState().samplePosition == 8, "transport advances across full block");
    require(left[0] == 0.0f && left[1] == 0.0f, "samples before clip are silent");
    require(left[2] == 0.25f && left[3] == 0.5f && left[4] == 0.75f && left[5] == 1.0f,
            "published clip renders from ring buffer");
    require(left[6] == 0.0f && left[7] == 0.0f, "samples after clip are silent");

    const auto readyView = state.editorViewModel();
    require(readyView.hasAudioClip, "view model reports audio clip");
    require(readyView.clipLengthSamples == 4, "view model reports clip length");
    require(readyView.playbackPosition == 8, "view model reports playback position");
    require(readyView.playing, "view model reports playing state");
    require(readyView.mediaAvailable, "view model reports available media");
    require(readyView.mediaError.empty(), "view model has empty media error when ready");
    require(readyView.underrunCount == 0, "view model reports zero underruns when ring is fed");

    state.setMediaStatus(false, "Missing WAV file");
    require(state.flushRenderSnapshot(), "unavailable media flushes empty playback description");
    require(state.returnToStart(), "seek zero queues");
    std::fill(left.begin(), left.end(), 1.0f);
    std::fill(right.begin(), right.end(), 1.0f);
    engine.process({output.data(), output.size(), left.size()}, {}, {});
    for (const float sample : left)
        require(sample == 0.0f, "unavailable media renders deterministic silence");

    const auto unavailableView = state.editorViewModel();
    require(!unavailableView.mediaAvailable, "view model reports unavailable media");
    require(unavailableView.mediaError == "Missing WAV file", "view model owns media error string");
    require(readyView.mediaAvailable && readyView.mediaError.empty(),
            "previously captured EditorViewModel remains independent value copy");
}

void testMultiTrackCompiledGraphAndMeteringInApplicationState()
{
    dawg::engine::AudioEngine engine;
    require(engine.prepare({48000.0, 8, 2}), "engine prepares");
    dawg::commands::ProjectController controller {dawg::project::Project::create("MultiTrack", 1)};
    dawg::app::ApplicationState state {controller, engine};

    require(state.dispatch(dawg::project::AddAudioTrack {"Drums"}), "add Drums track");
    require(state.dispatch(dawg::project::AddAudioTrack {"Bass"}), "add Bass track");
    require(state.dispatch(dawg::project::SetTrackGainPan {1, 0.5f, 0.0f, false}), "set Drums gain 0.5");
    require(state.dispatch(dawg::project::SetTrackGainPan {2, 0.25f, 0.0f, false}), "set Bass gain 0.25");
    require(state.dispatch(dawg::project::SetProjectLoop {true, 0, 4}), "enable 4-sample loop [0, 4)");

    const auto asset1 = state.addAudioAsset({
        .id = 0,
        .path = "drums.wav",
        .pathKind = dawg::project::AudioAssetPathKind::projectRelative,
        .sampleRate = 48000,
        .channelCount = 1,
        .frameCount = 4
    });
    const auto asset2 = state.addAudioAsset({
        .id = 0,
        .path = "bass.wav",
        .pathKind = dawg::project::AudioAssetPathKind::projectRelative,
        .sampleRate = 48000,
        .channelCount = 1,
        .frameCount = 4
    });
    require(asset1.has_value() && asset2.has_value(), "both assets registered");

    const auto clip1 = state.addAudioClip({
        .id = 0,
        .assetId = *asset1,
        .startSample = 0,
        .sourceOffset = 0,
        .lengthSamples = 4,
        .trackId = 1,
        .gainLinear = 1.0f
    });
    const auto clip2 = state.addAudioClip({
        .id = 0,
        .assetId = *asset2,
        .startSample = 0,
        .sourceOffset = 0,
        .lengthSamples = 4,
        .trackId = 2,
        .gainLinear = 1.0f
    });
    require(clip1.has_value() && clip2.has_value(), "both clips registered");

    dawg::engine::PlaybackRingBuffer ring1;
    dawg::engine::PlaybackRingBuffer ring2;
    require(ring1.prepare(1, 16) && ring2.prepare(1, 16), "both ring buffers prepare");
    constexpr std::array<float, 4> ones {1.0f, 1.0f, 1.0f, 1.0f};
    const std::array<const float*, 1> planes {ones.data()};
    require(ring1.write(0, planes, 4) == 4 && ring2.write(0, planes, 4) == 4, "prefill both rings");

    state.setClipBuffer(*clip1, &ring1);
    state.setClipBuffer(*clip2, &ring2);
    require(state.flushRenderSnapshot(), "flush multi-track CompiledGraph");
    require(state.play(), "queue play");

    std::array<float, 8> outL {};
    std::array<float, 8> outR {};
    std::array<float*, 2> outPlanes {outL.data(), outR.data()};
    engine.process({outPlanes.data(), outPlanes.size(), outL.size()}, {}, {});

    // Track 1 (1.0 * 0.5) + Track 2 (1.0 * 0.25) = 0.75 across all 8 samples (loop [0, 4) wrapped mid-block!)
    for (std::size_t i = 0; i < 8; ++i) {
        require(std::fabs(outL[i] - 0.75f) < 1e-5f && std::fabs(outR[i] - 0.75f) < 1e-5f,
                "ApplicationState multi-track CompiledGraph sums and loops across mid-block seam");
    }
    require(engine.transportState().samplePosition == 0,
            "transport wrapped twice at 4-sample loop boundary [0, 4) after 8-sample block");

    const auto vm = state.editorViewModel();
    require(std::fabs(vm.masterMeter.peakLeft - 0.75f) < 1e-5f,
            "EditorViewModel exposes live lock-free master meter reading");
}
} // namespace

int main()
{
    testCommandsUpdateEditorAndPublishToEngine();
    testPlaybackCommandsAndSnapshotComposition();
    testMultiTrackCompiledGraphAndMeteringInApplicationState();
}
