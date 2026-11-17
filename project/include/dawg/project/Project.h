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
    // Returns nullopt for unsupported versions (only normalized version 2 is supported),
    // invalid IDs/metadata, duplicate IDs, or invalid clip references/ranges.
    // Preserves vector order; each ID domain resumes above its own largest ID.
    [[nodiscard]] static std::optional<Project> fromSnapshot(ProjectSnapshot snapshot);
    [[nodiscard]] ProjectSnapshot snapshot() const;

    // New-project IDs start at 1; successful adds increase IDs without reuse.
    // Returns 0 for an empty name or exhausted IDs, without changing state.
    TrackId addAudioTrack(std::string name);
    TrackId addInstrumentTrack(std::string name);
    TrackId addAuxReturnTrack(std::string name);
    TrackId addSubmixGroupTrack(std::string name);
    TrackId addFolderTrack(std::string name);
    [[nodiscard]] bool setTrackParentFolder(TrackId trackId, TrackId folderId);
    [[nodiscard]] bool setFolderCollapsed(TrackId folderId, bool collapsed);
    [[nodiscard]] bool setTrackCompGroup(TrackId trackId, std::uint32_t compGroupId);
    [[nodiscard]] bool addTrackSend(TrackId trackId, TrackSend send);
    [[nodiscard]] bool removeTrackSend(TrackId trackId, TrackId targetTrackId);
    [[nodiscard]] bool setTrackSendLevel(TrackId trackId, TrackId targetTrackId, float levelLinear);
    [[nodiscard]] bool setTrackOutputBus(TrackId trackId, TrackId outputBusTrackId);
    [[nodiscard]] bool addTakeToTrack(TrackId trackId, Take take);
    [[nodiscard]] bool toggleTakeLanesExpanded(TrackId trackId);
    [[nodiscard]] bool promoteTakeRegion(TrackId trackId, std::uint32_t takeId, std::uint64_t startSample, std::uint64_t lengthSamples);
    [[nodiscard]] bool addTrackInsert(TrackId trackId, DspNodeDescriptor descriptor);
    [[nodiscard]] bool removeTrackInsert(TrackId trackId, std::uint32_t slotIndex);
    [[nodiscard]] bool setTrackInsertBypassed(TrackId trackId, std::uint32_t slotIndex, bool bypassed);
    MediaAssetId addAudioAsset(AudioAsset asset);
    ClipId addAudioClip(AudioClip clip);
    MidiClipId addMidiClip(MidiClip clip);
    [[nodiscard]] bool removeMidiClip(MidiClipId id);
    NoteId addMidiNote(MidiClipId clipId, MidiNote note);
    [[nodiscard]] bool removeMidiNote(MidiClipId clipId, NoteId noteId);
    [[nodiscard]] bool moveMidiNote(MidiClipId clipId, NoteId noteId, std::uint64_t newStartSample, std::uint8_t newPitch);
    [[nodiscard]] bool resizeMidiNote(MidiClipId clipId, NoteId noteId, std::uint64_t newLengthSamples);
    [[nodiscard]] bool setMidiNoteVelocity(MidiClipId clipId, NoteId noteId, std::uint8_t newVelocity);
    AutomationLaneId addAutomationLane(AutomationLane lane);
    [[nodiscard]] bool removeAutomationLane(AutomationLaneId laneId);
    AutomationPointId addAutomationPoint(AutomationLaneId laneId, AutomationPoint point);
    [[nodiscard]] bool removeAutomationPoint(AutomationLaneId laneId, AutomationPointId pointId);
    [[nodiscard]] bool moveAutomationPoint(AutomationLaneId laneId, AutomationPointId pointId,
                                           std::uint64_t newSamplePosition, float newValue, float newTension);
    // Empty names and unknown IDs fail without changing state.
    [[nodiscard]] bool removeTrack(TrackId id);
    [[nodiscard]] bool renameTrack(TrackId id, std::string name);
    [[nodiscard]] bool setTrackMute(TrackId id, bool muted);
    [[nodiscard]] bool setTrackSolo(TrackId id, bool solo);
    [[nodiscard]] bool setTrackGainPan(TrackId id, float gainLinear, float pan, bool phaseInvert);
    [[nodiscard]] bool setTrackRecordArm(TrackId id, bool armed);
    [[nodiscard]] bool reorderTracks(const std::vector<TrackId>& trackIds);
    [[nodiscard]] bool removeClip(ClipId id);
    [[nodiscard]] bool moveClip(ClipId id, TrackId newTrackId, std::uint64_t newStartSample);
    [[nodiscard]] bool trimClip(ClipId id, std::uint64_t newSourceOffset, std::uint64_t newLengthSamples);
    [[nodiscard]] bool splitClip(ClipId id, std::uint64_t splitSample);
    [[nodiscard]] bool duplicateClip(ClipId id, std::uint64_t newStartSample);
    [[nodiscard]] bool setClipGainFade(ClipId id, float gainLinear, std::uint32_t fadeInSamples, std::uint32_t fadeOutSamples);
    [[nodiscard]] bool setProjectTempo(double bpm, std::uint8_t numerator, std::uint8_t denominator, bool metronomeEnabled);
    [[nodiscard]] bool setProjectLoop(bool enabled, std::uint64_t startSample, std::uint64_t endSample);
    [[nodiscard]] bool apply(const ProjectCommand& command);

private:
    Project(ProjectSnapshot state, TrackId nextTrackId, MediaAssetId nextAssetId,
            ClipId nextClipId, NoteId nextNoteId = 1, AutomationLaneId nextLaneId = 1,
            AutomationPointId nextPointId = 1);

    ProjectSnapshot m_state;
    TrackId m_nextTrackId {1};
    MediaAssetId m_nextAssetId {1};
    ClipId m_nextClipId {1};
    NoteId m_nextNoteId {1};
    AutomationLaneId m_nextLaneId {1};
    AutomationPointId m_nextPointId {1};
};

} // namespace dawg::project
