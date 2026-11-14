#pragma once

#include "EditorViewModel.h"

#include <dawg/commands/ProjectController.h>
#include <dawg/engine/AudioEngine.h>
#include <dawg/engine/PlaybackRingBuffer.h>

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

namespace dawg::app {

// Message-thread composition shared by the application and headless tests.
// Service owners outlive this adapter; the UI receives only owned view values.
class ApplicationState final
{
public:
    ApplicationState(commands::ProjectController& controller, engine::AudioEngine& engine)
        : m_controller(controller), m_engine(engine) {}

    void setPlaybackBuffer(engine::PlaybackRingBuffer* buffer) noexcept
    {
        m_playbackBuffer = buffer;
        m_renderPending = true;
    }

    void setMediaStatus(const bool available, std::string error = {})
    {
        m_mediaAvailable = available;
        m_mediaError = std::move(error);
        m_renderPending = true;
    }

    [[nodiscard]] bool play() noexcept
    {
        return m_engine.enqueueCommand({engine::EngineCommandType::play, 0});
    }

    [[nodiscard]] bool stop() noexcept
    {
        return m_engine.enqueueCommand({engine::EngineCommandType::stop, 0});
    }

    [[nodiscard]] bool returnToStart() noexcept
    {
        return m_engine.enqueueCommand({engine::EngineCommandType::seekSamples, 0});
    }

    [[nodiscard]] std::optional<project::MediaAssetId> addAudioAsset(project::AudioAsset asset)
    {
        const auto id = m_controller.addAudioAsset(std::move(asset));
        if (!id)
            return std::nullopt;
        m_renderPending = true;
        static_cast<void>(flushRenderSnapshot());
        return id;
    }

    [[nodiscard]] std::optional<project::ClipId> addAudioClip(project::AudioClip clip)
    {
        if (!dispatch(project::AddAudioClip {std::move(clip)}))
            return std::nullopt;
        const auto current = m_controller.snapshot();
        if (current.clips.empty())
            return std::nullopt;
        return current.clips.back().id;
    }

    [[nodiscard]] ui::EditorViewModel editorViewModel() const
    {
        auto snapshot = m_controller.snapshot();
        const auto transport = m_engine.transportState();
        const auto resolved = resolveClipAndAsset(snapshot);
        const bool hasClip = resolved.has_value();
        const std::uint64_t clipLength = hasClip ? resolved->first.lengthSamples : 0;

        return ui::EditorViewModel {
            .project = std::move(snapshot),
            .hasAudioClip = hasClip,
            .clipLengthSamples = clipLength,
            .playbackPosition = transport.samplePosition,
            .playing = transport.playing,
            .mediaAvailable = m_mediaAvailable,
            .mediaError = m_mediaError,
            .underrunCount = m_engine.underrunCount()
        };
    }

    [[nodiscard]] bool dispatch(project::ProjectCommand command)
    {
        if (!m_controller.dispatch(std::move(command)))
            return false;
        m_renderPending = true;
        static_cast<void>(flushRenderSnapshot());
        return true;
    }

    // A contended publication coalesces to the latest edit on the next UI tick.
    [[nodiscard]] bool flushRenderSnapshot()
    {
        if (!m_renderPending)
            return true;

        auto render = m_controller.latestRenderSnapshot();
        render.playback = {};

        if (m_playbackBuffer != nullptr && m_mediaAvailable) {
            const auto projectSnapshot = m_controller.snapshot();
            if (const auto resolved = resolveClipAndAsset(projectSnapshot)) {
                render.playback = engine::PlaybackDescription {
                    .buffer = m_playbackBuffer,
                    .clipStartSample = resolved->first.startSample,
                    .clipLengthSamples = resolved->first.lengthSamples,
                    .sourceOffset = resolved->first.sourceOffset,
                    .channelCount = resolved->second.channelCount
                };
            }
        }

        m_renderPending = !m_engine.publishRenderSnapshot(render);
        return !m_renderPending;
    }

private:
    [[nodiscard]] static std::optional<std::pair<project::AudioClip, project::AudioAsset>>
    resolveClipAndAsset(const project::ProjectSnapshot& snapshot)
    {
        for (const auto& clip : snapshot.clips) {
            if (clip.lengthSamples == 0)
                continue;
            const auto assetIt = std::find_if(
                snapshot.assets.begin(), snapshot.assets.end(),
                [&clip](const project::AudioAsset& asset) { return asset.id == clip.assetId; });
            if (assetIt == snapshot.assets.end())
                continue;
            if (assetIt->channelCount == 0 || assetIt->frameCount == 0)
                continue;
            if (clip.sourceOffset > assetIt->frameCount
                || clip.lengthSamples > assetIt->frameCount - clip.sourceOffset) {
                continue;
            }
            return std::pair {clip, *assetIt};
        }
        return std::nullopt;
    }

    commands::ProjectController& m_controller;
    engine::AudioEngine& m_engine;
    engine::PlaybackRingBuffer* m_playbackBuffer {};
    bool m_mediaAvailable {true};
    std::string m_mediaError;
    bool m_renderPending {true};
};

} // namespace dawg::app
