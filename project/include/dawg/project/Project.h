#pragma once

#include <dawg/project/ProjectCommand.h>
#include <dawg/project/ProjectSnapshot.h>

namespace dawg::project {

// The message thread owns the editable project and performs every call.
// This model is not synchronized and must never be accessed by audio processing.
class Project final
{
public:
    [[nodiscard]] static Project create(std::string name, ProjectId id);
    [[nodiscard]] ProjectSnapshot snapshot() const;

    // IDs start at 1, increase on successful adds and are never reused.
    // Returns 0 for an empty name or exhausted IDs, without changing state.
    TrackId addAudioTrack(std::string name);
    // Empty names and unknown IDs fail without changing state.
    [[nodiscard]] bool removeTrack(TrackId id);
    [[nodiscard]] bool renameTrack(TrackId id, std::string name);
    [[nodiscard]] bool apply(const ProjectCommand& command);

private:
    Project(std::string name, ProjectId id);

    ProjectSnapshot m_state;
    TrackId m_nextTrackId {1};
};

} // namespace dawg::project
