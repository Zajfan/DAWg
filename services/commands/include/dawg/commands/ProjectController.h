#pragma once

#include <dawg/engine/RenderSnapshot.h>
#include <dawg/project/Project.h>

#include <optional>

namespace dawg::commands {

// Message-thread-only command coordinator. The application separately publishes
// latestRenderSnapshot() to its engine; this service owns no audio device/engine.
class ProjectController final
{
public:
    // Throws std::length_error if the project has more than UINT32_MAX tracks.
    explicit ProjectController(project::Project project);

    // Rejected commands and exhausted generation/track counts leave state intact.
    [[nodiscard]] bool dispatch(project::ProjectCommand command);
    [[nodiscard]] std::optional<project::MediaAssetId> addAudioAsset(project::AudioAsset asset);
    [[nodiscard]] project::ProjectSnapshot snapshot() const;
    [[nodiscard]] engine::RenderSnapshot latestRenderSnapshot() const noexcept;

    [[nodiscard]] bool undo();
    [[nodiscard]] bool redo();
    [[nodiscard]] bool canUndo() const noexcept;
    [[nodiscard]] bool canRedo() const noexcept;
    void clearHistory() noexcept;

private:
    project::Project m_project;
    engine::RenderSnapshot m_renderSnapshot {};
    std::vector<project::ProjectSnapshot> m_undoStack;
    std::vector<project::ProjectSnapshot> m_redoStack;
};

} // namespace dawg::commands
