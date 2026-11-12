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

using ProjectCommand = std::variant<AddAudioTrack, RemoveTrack, RenameTrack, AddAudioClip>;

} // namespace dawg::project
