#include <dawg/project/Project.h>

#include <type_traits>

namespace dawg::project {

bool Project::apply(const ProjectCommand& command)
{
    return std::visit([this](const auto& operation) -> bool
    {
        using Operation = std::decay_t<decltype(operation)>;
        if constexpr (std::is_same_v<Operation, AddAudioTrack>)
            return addAudioTrack(operation.name) != 0;
        else if constexpr (std::is_same_v<Operation, AddInstrumentTrack>)
            return addInstrumentTrack(operation.name) != 0;
        else if constexpr (std::is_same_v<Operation, AddAuxReturnTrack>)
            return addAuxReturnTrack(operation.name) != 0;
        else if constexpr (std::is_same_v<Operation, AddSubmixGroupTrack>)
            return addSubmixGroupTrack(operation.name) != 0;
        else if constexpr (std::is_same_v<Operation, AddFolderTrack>)
            return addFolderTrack(operation.name) != 0;
        else if constexpr (std::is_same_v<Operation, SetTrackParentFolder>)
            return setTrackParentFolder(operation.trackId, operation.folderId);
        else if constexpr (std::is_same_v<Operation, SetFolderCollapsed>)
            return setFolderCollapsed(operation.folderId, operation.collapsed);
        else if constexpr (std::is_same_v<Operation, SetTrackCompGroup>)
            return setTrackCompGroup(operation.trackId, operation.compGroupId);
        else if constexpr (std::is_same_v<Operation, AddTrackSend>)
            return addTrackSend(operation.trackId, operation.send);
        else if constexpr (std::is_same_v<Operation, RemoveTrackSend>)
            return removeTrackSend(operation.trackId, operation.targetTrackId);
        else if constexpr (std::is_same_v<Operation, SetTrackSendLevel>)
            return setTrackSendLevel(operation.trackId, operation.targetTrackId, operation.levelLinear);
        else if constexpr (std::is_same_v<Operation, SetTrackOutputBus>)
            return setTrackOutputBus(operation.trackId, operation.outputBusTrackId);
        else if constexpr (std::is_same_v<Operation, AddTakeToTrack>)
            return addTakeToTrack(operation.trackId, operation.take);
        else if constexpr (std::is_same_v<Operation, ToggleTakeLanesExpanded>)
            return toggleTakeLanesExpanded(operation.trackId);
        else if constexpr (std::is_same_v<Operation, PromoteTakeRegion>)
            return promoteTakeRegion(operation.trackId, operation.takeId, operation.startSample, operation.lengthSamples);
        else if constexpr (std::is_same_v<Operation, RemoveTrack>)
            return removeTrack(operation.id);
        else if constexpr (std::is_same_v<Operation, RenameTrack>)
            return renameTrack(operation.id, operation.name);
        else if constexpr (std::is_same_v<Operation, AddAudioClip>)
            return addAudioClip(operation.clip) != 0;
        else if constexpr (std::is_same_v<Operation, AddMidiClip>)
            return addMidiClip(operation.clip) != 0;
        else if constexpr (std::is_same_v<Operation, RemoveMidiClip>)
            return removeMidiClip(operation.id);
        else if constexpr (std::is_same_v<Operation, AddMidiNote>)
            return addMidiNote(operation.clipId, operation.note) != 0;
        else if constexpr (std::is_same_v<Operation, RemoveMidiNote>)
            return removeMidiNote(operation.clipId, operation.noteId);
        else if constexpr (std::is_same_v<Operation, MoveMidiNote>)
            return moveMidiNote(operation.clipId, operation.noteId, operation.newStartSample, operation.newPitch);
        else if constexpr (std::is_same_v<Operation, ResizeMidiNote>)
            return resizeMidiNote(operation.clipId, operation.noteId, operation.newLengthSamples);
        else if constexpr (std::is_same_v<Operation, SetMidiNoteVelocity>)
            return setMidiNoteVelocity(operation.clipId, operation.noteId, operation.newVelocity);
        else if constexpr (std::is_same_v<Operation, AddAutomationLane>)
            return addAutomationLane(operation.lane) != 0;
        else if constexpr (std::is_same_v<Operation, RemoveAutomationLane>)
            return removeAutomationLane(operation.laneId);
        else if constexpr (std::is_same_v<Operation, AddAutomationPoint>)
            return addAutomationPoint(operation.laneId, operation.point) != 0;
        else if constexpr (std::is_same_v<Operation, RemoveAutomationPoint>)
            return removeAutomationPoint(operation.laneId, operation.pointId);
        else if constexpr (std::is_same_v<Operation, MoveAutomationPoint>)
            return moveAutomationPoint(operation.laneId, operation.pointId, operation.newSamplePosition, operation.newValue, operation.newTension);
        else if constexpr (std::is_same_v<Operation, SetTrackMute>)
            return setTrackMute(operation.id, operation.muted);
        else if constexpr (std::is_same_v<Operation, SetTrackSolo>)
            return setTrackSolo(operation.id, operation.solo);
        else if constexpr (std::is_same_v<Operation, SetTrackGainPan>)
            return setTrackGainPan(operation.id, operation.gainLinear, operation.pan, operation.phaseInvert);
        else if constexpr (std::is_same_v<Operation, SetTrackRecordArm>)
            return setTrackRecordArm(operation.id, operation.armed);
        else if constexpr (std::is_same_v<Operation, ReorderTracks>)
            return reorderTracks(operation.trackIds);
        else if constexpr (std::is_same_v<Operation, RemoveClip>)
            return removeClip(operation.id);
        else if constexpr (std::is_same_v<Operation, MoveClip>)
            return moveClip(operation.id, operation.newTrackId, operation.newStartSample);
        else if constexpr (std::is_same_v<Operation, TrimClip>)
            return trimClip(operation.id, operation.newSourceOffset, operation.newLengthSamples);
        else if constexpr (std::is_same_v<Operation, SplitClip>)
            return splitClip(operation.id, operation.splitSample);
        else if constexpr (std::is_same_v<Operation, DuplicateClip>)
            return duplicateClip(operation.id, operation.newStartSample);
        else if constexpr (std::is_same_v<Operation, SetClipGainFade>)
            return setClipGainFade(operation.id, operation.gainLinear, operation.fadeInSamples, operation.fadeOutSamples);
        else if constexpr (std::is_same_v<Operation, SetProjectTempo>)
            return setProjectTempo(operation.bpm, operation.timeSigNumerator, operation.timeSigDenominator, operation.metronomeEnabled);
        else if constexpr (std::is_same_v<Operation, SetProjectLoop>)
            return setProjectLoop(operation.enabled, operation.startSample, operation.endSample);
        else if constexpr (std::is_same_v<Operation, AddTrackInsert>)
            return addTrackInsert(operation.trackId, operation.descriptor);
        else if constexpr (std::is_same_v<Operation, RemoveTrackInsert>)
            return removeTrackInsert(operation.trackId, operation.slotIndex);
        else if constexpr (std::is_same_v<Operation, SetTrackInsertBypassed>)
            return setTrackInsertBypassed(operation.trackId, operation.slotIndex, operation.bypassed);
        else
            return false;
    }, command);
}

} // namespace dawg::project
