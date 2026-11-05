#include <dawg/project/Project.h>

#include <algorithm>
#include <limits>
#include <unordered_set>
#include <utility>

namespace dawg::project {

Project::Project(ProjectSnapshot state, const TrackId nextTrackId)
    : m_state {std::move(state)}, m_nextTrackId {nextTrackId}
{
}

Project Project::create(std::string name, const ProjectId id)
{
    return Project {{1, id, std::move(name), {}}, 1};
}

std::optional<Project> Project::fromSnapshot(ProjectSnapshot snapshot)
{
    if (snapshot.formatVersion != 1 || snapshot.id == 0 || snapshot.name.empty())
        return std::nullopt;

    std::unordered_set<TrackId> ids;
    TrackId largestId {};
    for (const auto& track : snapshot.tracks)
    {
        if (track.id == 0 || track.name.empty() || ! ids.insert(track.id).second)
            return std::nullopt;

        largestId = std::max(largestId, track.id);
    }

    const TrackId nextId = largestId == std::numeric_limits<TrackId>::max() ? 0 : largestId + 1;
    return Project {std::move(snapshot), nextId};
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
