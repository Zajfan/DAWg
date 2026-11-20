#pragma once

#include <algorithm>
#include <cmath>
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

enum class TrackKind : std::uint8_t { audio, instrument, auxReturn, submixGroup, folder };

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

enum class ClipModulationTarget : std::uint8_t
{
    filterCutoff,
    filterResonance,
    pan,
    gain,
    pitchBend,
    custom
};

struct ClipModulationPoint
{
    double normalizedTime {0.0}; // [0.0, 1.0] within clip loop/length
    float value {0.5f};          // [0.0, 1.0] parameter value
    float tension {0.0f};        // [-1.0, +1.0] Bezier tension

    friend bool operator==(const ClipModulationPoint&, const ClipModulationPoint&) = default;
};

struct ClipModulationCurve
{
    ClipModulationTarget target {ClipModulationTarget::filterCutoff};
    std::string customParameterId;
    bool loopWithClip {true};
    std::vector<ClipModulationPoint> points;

    [[nodiscard]] float evaluate(double normalizedPhase) const noexcept
    {
        if (points.empty())
            return 0.5f;
        if (points.size() == 1)
            return points.front().value;

        double phase = normalizedPhase;
        if (loopWithClip)
        {
            if (phase > 1.0 || phase < 0.0)
            {
                phase = std::fmod(phase, 1.0);
                if (phase < 0.0)
                    phase += 1.0;
            }
        }
        else
        {
            phase = std::clamp(phase, 0.0, 1.0);
        }

        if (phase <= points.front().normalizedTime)
            return points.front().value;
        if (phase >= points.back().normalizedTime)
            return points.back().value;

        for (std::size_t i = 0; i < points.size() - 1; ++i)
        {
            const auto& p0 = points[i];
            const auto& p1 = points[i + 1];
            if (phase >= p0.normalizedTime && phase <= p1.normalizedTime)
            {
                const double span = p1.normalizedTime - p0.normalizedTime;
                if (span <= 1e-7)
                    return p1.value;
                const double t = (phase - p0.normalizedTime) / span;
                double shapedT = t;
                if (std::abs(p0.tension) > 0.001f)
                {
                    if (p0.tension > 0.0f)
                        shapedT = std::pow(t, 1.0 + static_cast<double>(p0.tension) * 3.0);
                    else
                        shapedT = 1.0 - std::pow(1.0 - t, 1.0 - static_cast<double>(p0.tension) * 3.0);
                }
                return static_cast<float>(static_cast<double>(p0.value) + (static_cast<double>(p1.value) - static_cast<double>(p0.value)) * shapedT);
            }
        }
        return points.back().value;
    }

    friend bool operator==(const ClipModulationCurve&, const ClipModulationCurve&) = default;
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
    std::vector<ClipModulationCurve> modulationCurves {};

    friend bool operator==(const AudioClip&, const AudioClip&) = default;
};

struct MidiNote
{
    NoteId id {};
    std::uint8_t pitch {60};
    std::uint8_t velocity {100};
    std::uint64_t startSample {0};
    std::uint64_t lengthSamples {24000};
    float vibratoDepth {0.0f};       // 0.0 to 2.0 semitones
    float vibratoRateHz {5.5f};      // 1.0 to 12.0 Hz
    float vibratoDelayMs {150.0f};   // onset delay in milliseconds
    float slideSemitones {0.0f};     // micro-pitch slide offset (-12.0 to +12.0 semitones)

    friend bool operator==(const MidiNote&, const MidiNote&) = default;
};

struct MidiClip
{
    MidiClipId id {};
    TrackId trackId {};
    std::uint64_t startSample {0};
    std::uint64_t lengthSamples {96000};
    std::vector<MidiNote> notes;
    std::vector<ClipModulationCurve> modulationCurves {};

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
    drumPadSampler,
    resonanceSuppressor,
    multibandStereoImager,
    linearPhaseMasteringEq,
    buchlaComplexSynth,
    vectorSequencer,
    physicalModelSynth,
    granularCloud,
    analogLadderSynth,
    tubeTransformer,
    analogDegrader,
    rayTracingReverb,
    virusTiPolarSynth,
    juno106Synth,
    omniFmSynth,
    soundFontPlayer,
    tripleOscillatorSynth
};

struct DspNodeDescriptor
{
    std::uint32_t slotIndex {0};
    DspNodeType type {DspNodeType::parametricEq};
    bool bypassed {false};
    std::string name;
    std::string pluginId;
    std::vector<float> parameters;
    std::string stateData {};

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
    TrackId parentFolderId {0};
    bool isFolder {false};
    bool folderCollapsed {false};
    std::uint32_t compGroupId {0}; // 0 = none, >0 = phase-locked multi-track comp group
    bool isFrozen {false};
    std::string freezeAudioPath {};
    std::vector<DspNodeDescriptor> frozenInserts {};

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

struct PunchSetting
{
    bool enabled {false};
    std::uint64_t punchInSample {0};
    std::uint64_t punchOutSample {0};
    std::uint8_t prerollBars {0}; // 0 = off, 1 = 1 bar, 2 = 2 bars
    std::uint8_t countInBars {0}; // 0 = off, 1 = 1 bar, 2 = 2 bars

    friend bool operator==(const PunchSetting&, const PunchSetting&) = default;
};

} // namespace dawg::project
