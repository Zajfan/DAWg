#pragma once

#include <dawg/project/ProjectSnapshot.h>

#include <cstddef>

namespace dawg::ui {

// A view owns its values; no mutable Project or service crosses this boundary.
struct EditorViewModel
{
    project::ProjectSnapshot project;

    [[nodiscard]] std::size_t trackCount() const noexcept { return project.tracks.size(); }
};

} // namespace dawg::ui
