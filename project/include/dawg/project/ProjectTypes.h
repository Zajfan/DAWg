#pragma once

#include <cstdint>
#include <string>

namespace dawg::project {

using ProjectId = std::uint64_t;
using TrackId = std::uint64_t;

struct Track
{
    TrackId id {};
    std::string name;
    bool muted {};
    bool solo {};

    friend bool operator==(const Track&, const Track&) = default;
};

} // namespace dawg::project
