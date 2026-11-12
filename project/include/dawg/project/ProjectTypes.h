#pragma once

#include <cstdint>
#include <string>

namespace dawg::project {

using ProjectId = std::uint64_t;
using TrackId = std::uint64_t;
using MediaAssetId = std::uint64_t;
using ClipId = std::uint64_t;

enum class AudioAssetPathKind : std::uint8_t { projectRelative, absolute };

struct AudioAsset
{
    MediaAssetId id {};
    std::string path;
    AudioAssetPathKind pathKind {AudioAssetPathKind::projectRelative};
    std::uint32_t sampleRate {};
    std::uint16_t channelCount {};
    std::uint64_t frameCount {};

    friend bool operator==(const AudioAsset&, const AudioAsset&) = default;
};

struct AudioClip
{
    ClipId id {};
    MediaAssetId assetId {};
    std::uint64_t startSample {};
    std::uint64_t sourceOffset {};
    std::uint64_t lengthSamples {};

    friend bool operator==(const AudioClip&, const AudioClip&) = default;
};

struct Track
{
    TrackId id {};
    std::string name;
    bool muted {};
    bool solo {};

    friend bool operator==(const Track&, const Track&) = default;
};

} // namespace dawg::project
