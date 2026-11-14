#pragma once

#include <dawg/project/ProjectTypes.h>

#include <string>
#include <variant>
#include <vector>

namespace dawg::project {

struct AddAudioTrack
{
    std::string name;
};

struct AddInstrumentTrack
{
    std::string name;
};

struct RemoveTrack
{
    TrackId id {};
};

struct RenameTrack
{
    TrackId id {};
    std::string name;
};

struct AddAudioClip
{
    AudioClip clip;
};

struct SetTrackMute
{
    TrackId id {};
    bool muted {};
};

struct SetTrackSolo
{
    TrackId id {};
    bool solo {};
};

struct SetTrackGainPan
{
    TrackId id {};
    float gainLinear {1.0f};
    float pan {0.0f};
    bool phaseInvert {};
};

struct SetProjectTempo
{
    double bpm {120.0};
    std::uint8_t timeSigNumerator {4};
    std::uint8_t timeSigDenominator {4};
    bool metronomeEnabled {false};
};

struct SetProjectLoop
{
    bool enabled {false};
    std::uint64_t startSample {0};
    std::uint64_t endSample {0};
};

struct SetTrackRecordArm
{
    TrackId id {};
    bool armed {};
};

struct ReorderTracks
{
    std::vector<TrackId> trackIds;
};

struct RemoveClip
{
    ClipId id {};
};

struct MoveClip
{
    ClipId id {};
    TrackId newTrackId {};
    std::uint64_t newStartSample {};
};

struct TrimClip
{
    ClipId id {};
    std::uint64_t newSourceOffset {};
    std::uint64_t newLengthSamples {};
};

struct SplitClip
{
    ClipId id {};
    std::uint64_t splitSample {};
};

struct DuplicateClip
{
    ClipId id {};
    std::uint64_t newStartSample {};
};

struct SetClipGainFade
{
    ClipId id {};
    float gainLinear {1.0f};
    std::uint32_t fadeInSamples {};
    std::uint32_t fadeOutSamples {};
};

struct AddMidiClip
{
    MidiClip clip;
};

struct RemoveMidiClip
{
    MidiClipId id {};
};

struct AddMidiNote
{
    MidiClipId clipId {};
    MidiNote note {};
};

struct RemoveMidiNote
{
    MidiClipId clipId {};
    NoteId noteId {};
};

struct MoveMidiNote
{
    MidiClipId clipId {};
    NoteId noteId {};
    std::uint64_t newStartSample {};
    std::uint8_t newPitch {};
};

struct ResizeMidiNote
{
    MidiClipId clipId {};
    NoteId noteId {};
    std::uint64_t newLengthSamples {};
};

using ProjectCommand = std::variant<
    AddAudioTrack,
    AddInstrumentTrack,
    RemoveTrack,
    RenameTrack,
    ReorderTracks,
    SetTrackMute,
    SetTrackSolo,
    SetTrackGainPan,
    SetTrackRecordArm,
    AddAudioClip,
    RemoveClip,
    MoveClip,
    TrimClip,
    SplitClip,
    DuplicateClip,
    SetClipGainFade,
    AddMidiClip,
    RemoveMidiClip,
    AddMidiNote,
    RemoveMidiNote,
    MoveMidiNote,
    ResizeMidiNote,
    SetProjectTempo,
    SetProjectLoop>;

} // namespace dawg::project
