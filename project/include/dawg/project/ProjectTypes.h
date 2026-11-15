#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dawg::project {

using ProjectId = std::uint64_t;
using TrackId = std::uint64_t;
using MediaAssetId = std::uint64_t;
using ClipId = std::uint64_t;
using NoteId = std::uint64_t;
using MidiClipId = std::uint64_t;
using AutomationLaneId = std::uint64_t;
using AutomationPointId = std::uint64_t;

enum class TrackKind : std::uint8_t { audio, instrument, auxReturn, submixGroup };

enum class AutomationTargetKind : std::uint8_t
{
    trackVolume,
    trackPan,
    trackSend,
    dspParameter
};

struct AutomationTarget
{
    AutomationTargetKind kind {AutomationTargetKind::trackVolume};
    TrackId trackId {0};
    std::uint32_t slotIndex {0};
    std::uint32_t parameterIndex {0};

    friend bool operator==(const AutomationTarget&, const AutomationTarget&) = default;
};

struct AutomationPoint
{
    AutomationPointId id {0};
    std::uint64_t samplePosition {0};
    float value {1.0f};
    float curveTension {0.0f};

    friend bool operator==(const AutomationPoint&, const AutomationPoint&) = default;
};

struct AutomationLane
{
    AutomationLaneId id {0};
    AutomationTarget target {};
    std::vector<AutomationPoint> points;

    friend bool operator==(const AutomationLane&, const AutomationLane&) = default;
};

enum class AudioAssetPathKind : std::uint8_t { projectRelative, absolute };

struct WarpMarker
{
    std::uint32_t id {0};
    std::uint64_t sourceSample {0};
    std::uint64_t targetSample {0};

    friend bool operator==(const WarpMarker&, const WarpMarker&) = default;
};

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
    bool warpingEnabled {false};
    std::vector<WarpMarker> warpMarkers {};

    friend bool operator==(const AudioClip&, const AudioClip&) = default;
};

struct MidiNote
{
    NoteId id {};
    std::uint8_t pitch {60};
    std::uint8_t velocity {100};
    std::uint64_t startSample {0};
    std::uint64_t lengthSamples {24000};

    friend bool operator==(const MidiNote&, const MidiNote&) = default;
};

struct MidiClip
{
    MidiClipId id {};
    TrackId trackId {};
    std::uint64_t startSample {0};
    std::uint64_t lengthSamples {96000};
    std::vector<MidiNote> notes;

    friend bool operator==(const MidiClip&, const MidiClip&) = default;
};

enum class DspNodeType : std::uint8_t
{
    parametricEq,
    dynamicsCompressor,
    brickwallLimiter,
    tnhcSynth,
    clapPlugin,
    tnhcReverb,
    tnhcDelay,
    drumPadSampler
};

struct DspNodeDescriptor
{
    std::uint32_t slotIndex {0};
    DspNodeType type {DspNodeType::parametricEq};
    bool bypassed {false};
    std::string name;
    std::string pluginId;
    std::vector<float> parameters;

    friend bool operator==(const DspNodeDescriptor&, const DspNodeDescriptor&) = default;
};

struct TrackSend
{
    TrackId targetTrackId {0};
    float levelLinear {1.0f};
    bool preFader {false};

    friend bool operator==(const TrackSend&, const TrackSend&) = default;
};

struct Take
{
    std::uint32_t id {0};
    MediaAssetId assetId {0};
    std::uint64_t sourceOffset {0};
    std::uint64_t lengthSamples {0};
    std::string name {};

    friend bool operator==(const Take&, const Take&) = default;
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
    TrackKind kind {TrackKind::audio};
    std::vector<DspNodeDescriptor> inserts {};
    std::vector<TrackSend> sends {};
    TrackId outputBusTrackId {0};
    std::vector<Take> takes {};
    bool takeLanesExpanded {false};

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
