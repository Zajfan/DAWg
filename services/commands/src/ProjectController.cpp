#include <dawg/commands/ProjectController.h>

#include <limits>
#include <stdexcept>
#include <utility>
#include <variant>

namespace dawg::commands {

ProjectController::ProjectController(project::Project project)
    : m_project(std::move(project))
{
    const auto count = m_project.snapshot().tracks.size();
    if (count > std::numeric_limits<std::uint32_t>::max())
        throw std::length_error("Project exceeds render snapshot track capacity");
    m_renderSnapshot.trackCount = static_cast<std::uint32_t>(count);
}

bool ProjectController::dispatch(project::ProjectCommand command)
{
    const auto addsTrack = std::holds_alternative<project::AddAudioTrack>(command);
    if (m_renderSnapshot.generation == std::numeric_limits<std::uint64_t>::max() ||
        (addsTrack && m_renderSnapshot.trackCount == std::numeric_limits<std::uint32_t>::max()))
        return false;

    const auto beforeSnapshot = m_project.snapshot();
    if (! m_project.apply(command))
        return false;

    m_undoStack.push_back(beforeSnapshot);
    m_redoStack.clear();

    ++m_renderSnapshot.generation;
    m_renderSnapshot.trackCount = static_cast<std::uint32_t>(m_project.snapshot().tracks.size());
    return true;
}

std::optional<project::MediaAssetId> ProjectController::addAudioAsset(project::AudioAsset asset)
{
    if (m_renderSnapshot.generation == std::numeric_limits<std::uint64_t>::max())
        return std::nullopt;

    const auto beforeSnapshot = m_project.snapshot();
    const auto id = m_project.addAudioAsset(std::move(asset));
    if (id == 0)
        return std::nullopt;

    m_undoStack.push_back(beforeSnapshot);
    m_redoStack.clear();

    ++m_renderSnapshot.generation;
    return id;
}

bool ProjectController::undo()
{
    if (m_undoStack.empty())
        return false;

    auto previous = std::move(m_undoStack.back());
    m_undoStack.pop_back();

    auto current = m_project.snapshot();
    auto reconstructed = project::Project::fromSnapshot(std::move(previous));
    if (! reconstructed)
        return false;

    m_redoStack.push_back(std::move(current));
    m_project = std::move(*reconstructed);

    ++m_renderSnapshot.generation;
    m_renderSnapshot.trackCount = static_cast<std::uint32_t>(m_project.snapshot().tracks.size());
    return true;
}

bool ProjectController::redo()
{
    if (m_redoStack.empty())
        return false;

    auto next = std::move(m_redoStack.back());
    m_redoStack.pop_back();

    auto current = m_project.snapshot();
    auto reconstructed = project::Project::fromSnapshot(std::move(next));
    if (! reconstructed)
        return false;

    m_undoStack.push_back(std::move(current));
    m_project = std::move(*reconstructed);

    ++m_renderSnapshot.generation;
    m_renderSnapshot.trackCount = static_cast<std::uint32_t>(m_project.snapshot().tracks.size());
    return true;
}

bool ProjectController::canUndo() const noexcept
{
    return ! m_undoStack.empty();
}

bool ProjectController::canRedo() const noexcept
{
    return ! m_redoStack.empty();
}

void ProjectController::clearHistory() noexcept
{
    m_undoStack.clear();
    m_redoStack.clear();
}

project::ProjectSnapshot ProjectController::snapshot() const
{
    return m_project.snapshot();
}

engine::RenderSnapshot ProjectController::latestRenderSnapshot() const noexcept
{
    return m_renderSnapshot;
}

} // namespace dawg::commands
