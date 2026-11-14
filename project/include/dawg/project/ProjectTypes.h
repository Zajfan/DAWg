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
    TrackId trackId {};
    float gainLinear {1.0f};
    std::uint32_t fadeInSamples {};
    std::uint32_t fadeOutSamples {};

    friend bool operator==(const AudioClip&, const AudioClip&) = default;
};

struct Track
{
    TrackId id {};
    std::string name;
    bool muted {};
    bool solo {};
    float gainLinear {1.0f};
    float pan {0.0f};
    bool phaseInvert {};
    bool recordArmed {};

    friend bool operator==(const Track&, const Track&) = default;
};

struct TempoSetting
{
    double bpm {120.0};
    std::uint8_t timeSigNumerator {4};
    std::uint8_t timeSigDenominator {4};
    bool metronomeEnabled {false};

    friend bool operator==(const TempoSetting&, const TempoSetting&) = default;
};

struct LoopSetting
{
    bool enabled {false};
    std::uint64_t startSample {0};
    std::uint64_t endSample {0};

    friend bool operator==(const LoopSetting&, const LoopSetting&) = default;
};

} // namespace dawg::project
