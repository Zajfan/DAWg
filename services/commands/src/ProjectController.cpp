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
    if (! m_project.apply(command))
        return false;

    ++m_renderSnapshot.generation;
    if (addsTrack)
        ++m_renderSnapshot.trackCount;
    else if (std::holds_alternative<project::RemoveTrack>(command))
        --m_renderSnapshot.trackCount;
    return true;
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
