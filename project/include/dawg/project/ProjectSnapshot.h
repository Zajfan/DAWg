#pragma once

#include <dawg/project/ProjectTypes.h>

#include <vector>

namespace dawg::project {

// Owns all of its values; contains no references into a mutable Project.
struct ProjectSnapshot
{
    std::uint32_t formatVersion {2};
    ProjectId id {};
    std::string name;
    std::vector<Track> tracks;
    std::vector<AudioAsset> assets;
    std::vector<AudioClip> clips;
};

} // namespace dawg::project
