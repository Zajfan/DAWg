#pragma once

#include <dawg/project/ProjectTypes.h>

#include <variant>

namespace dawg::project {

struct AddAudioTrack
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

using ProjectCommand = std::variant<
    AddAudioTrack,
    RemoveTrack,
    RenameTrack,
    AddAudioClip,
    SetTrackMute,
    SetTrackSolo,
    SetTrackGainPan>;

} // namespace dawg::project
