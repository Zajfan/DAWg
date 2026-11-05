#pragma once

#include <dawg/project/ProjectCommand.h>
#include <dawg/project/ProjectSnapshot.h>

#include <optional>

namespace dawg::project {

// The message thread owns the editable project and performs every call.
// This model is not synchronized and must never be accessed by audio processing.
class Project final
{
public:
    [[nodiscard]] static Project create(std::string name, ProjectId id);
    // Reconstructs owned state without JUCE or persistence dependencies.
    // Returns nullopt for unsupported versions (only 1 is supported), zero
    // project/track IDs, empty project/track names or duplicate track IDs.
    // Preserves track order and flags; allocation resumes above the largest ID.
    // A loaded maximum TrackId is valid but leaves further allocation exhausted.
    [[nodiscard]] static std::optional<Project> fromSnapshot(ProjectSnapshot snapshot);
    [[nodiscard]] ProjectSnapshot snapshot() const;

    // New-project IDs start at 1; successful adds increase IDs without reuse.
    // Returns 0 for an empty name or exhausted IDs, without changing state.
    TrackId addAudioTrack(std::string name);
    // Empty names and unknown IDs fail without changing state.
    [[nodiscard]] bool removeTrack(TrackId id);
    [[nodiscard]] bool renameTrack(TrackId id, std::string name);
    [[nodiscard]] bool apply(const ProjectCommand& command);

private:
    Project(ProjectSnapshot state, TrackId nextTrackId);

    ProjectSnapshot m_state;
    TrackId m_nextTrackId {1};
};

} // namespace dawg::project
