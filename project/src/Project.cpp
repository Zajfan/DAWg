#include <dawg/project/Project.h>

#include <algorithm>
#include <utility>

namespace dawg::project {

Project::Project(std::string name, const ProjectId id)
    : m_state {1, id, std::move(name), {}}
{
}

Project Project::create(std::string name, const ProjectId id)
{
    return Project {std::move(name), id};
}

ProjectSnapshot Project::snapshot() const
{
    return m_state;
}

TrackId Project::addAudioTrack(std::string name)
{
    if (name.empty() || m_nextTrackId == 0)
        return 0;

    const auto id = m_nextTrackId;
    m_state.tracks.push_back({id, std::move(name), false, false});
    // Unsigned wrap to the reserved invalid ID permanently rejects further adds.
    ++m_nextTrackId;
    return id;
}

bool Project::removeTrack(const TrackId id)
{
    const auto track = std::find_if(m_state.tracks.begin(), m_state.tracks.end(),
                                   [id](const Track& value) { return value.id == id; });
    if (track == m_state.tracks.end())
        return false;

    m_state.tracks.erase(track);
    return true;
}

bool Project::renameTrack(const TrackId id, std::string name)
{
    if (name.empty())
        return false;

    const auto track = std::find_if(m_state.tracks.begin(), m_state.tracks.end(),
                                   [id](const Track& value) { return value.id == id; });
    if (track == m_state.tracks.end())
        return false;

    track->name = std::move(name);
    return true;
}

} // namespace dawg::project
