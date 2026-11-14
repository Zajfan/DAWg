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

using ProjectCommand = std::variant<
    AddAudioTrack,
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
    SetProjectTempo,
    SetProjectLoop>;

} // namespace dawg::project
