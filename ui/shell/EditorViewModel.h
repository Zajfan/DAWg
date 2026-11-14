#pragma once

#include <dawg/project/ProjectSnapshot.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace dawg::ui {

// A view owns its values; no mutable Project or service crosses this boundary.
struct EditorViewModel
{
    project::ProjectSnapshot project;
    bool hasAudioClip {};
    std::uint64_t clipLengthSamples {};
    std::int64_t playbackPosition {};
    bool playing {};
    bool mediaAvailable {true};
    std::string mediaError;
    std::uint64_t underrunCount {};

    [[nodiscard]] std::size_t trackCount() const noexcept { return project.tracks.size(); }
};

} // namespace dawg::ui
