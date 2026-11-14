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
    std::vector<MidiClip> midiClips;
    std::vector<AutomationLane> automationLanes;
    TempoSetting tempo {};
    LoopSetting loop {};

    ProjectSnapshot() = default;
    ProjectSnapshot(std::uint32_t version, ProjectId projId, std::string projName,
                    std::vector<Track> trk = {}, std::vector<AudioAsset> ast = {},
                    std::vector<AudioClip> clp = {}, std::vector<MidiClip> mClips = {},
                    std::vector<AutomationLane> lanes = {},
                    TempoSetting tmp = {}, LoopSetting lp = {})
        : formatVersion(version), id(projId), name(std::move(projName)),
          tracks(std::move(trk)), assets(std::move(ast)), clips(std::move(clp)),
          midiClips(std::move(mClips)), automationLanes(std::move(lanes)),
          tempo(tmp), loop(lp) {}

    friend bool operator==(const ProjectSnapshot&, const ProjectSnapshot&) = default;
};

} // namespace dawg::project
