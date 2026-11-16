#pragma once

#include <dawg/project/ProjectTypes.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace dawg::project {

struct TakeLane
{
    std::uint32_t takeId {0};
    TrackId trackId {0};
    std::string name {"Take 1"};
    std::string colorHex {"#4A90E2"};
    AudioClip clip {};
    bool isAuditionSolo {false};

    friend bool operator==(const TakeLane&, const TakeLane&) = default;
};

struct CompSlice
{
    std::uint32_t sliceId {0};
    std::uint32_t takeId {0};
    std::uint64_t startSample {0};
    std::uint64_t endSample {0};
    std::uint32_t crossfadeSamples {192}; // ~4 ms at 48 kHz

    friend bool operator==(const CompSlice&, const CompSlice&) = default;
};

// Advanced Audio Comping & Multi-Take Lane Editor (Logic / Pro Tools style)
// Manages multi-take recording lanes, interactive swipe-comping with equal-power micro-crossfades,
// zero-crossing auto-snapping, take audition soloing, and "Flatten & Merge" into a consolidated audio clip.
class TakeCompManager
{
public:
    explicit TakeCompManager(TrackId trackId) noexcept;
    ~TakeCompManager() = default;

    [[nodiscard]] TrackId trackId() const noexcept { return m_trackId; }

    // Multi-Take Lane Management
    std::uint32_t addTake(AudioClip clip, const std::string& name = "", const std::string& colorHex = "");
    bool removeTake(std::uint32_t takeId);
    [[nodiscard]] const std::vector<TakeLane>& takeLanes() const noexcept { return m_takes; }
    [[nodiscard]] const TakeLane* findTake(std::uint32_t takeId) const noexcept;

    // Swipe-Comping Selection
    void selectCompSlice(
        std::uint32_t takeId,
        std::uint64_t startSample,
        std::uint64_t endSample,
        std::uint32_t crossfadeSamples = 192);

    // Swipe-comping with zero-crossing search to eliminate clicks
    void selectCompSliceWithZeroCrossing(
        std::uint32_t takeId,
        std::uint64_t startSample,
        std::uint64_t endSample,
        const float* audioBuffer,
        std::size_t bufferLength,
        std::uint32_t crossfadeSamples = 192);

    void clearCompSlices() noexcept;
    [[nodiscard]] const std::vector<CompSlice>& compSlices() const noexcept { return m_slices; }

    // Take Audition Solo
    void setTakeAuditionSolo(std::uint32_t takeId, bool solo) noexcept;
    [[nodiscard]] std::uint32_t soloedTakeId() const noexcept { return m_soloedTakeId; }

    // Flatten & Merge into Consolidated Audio Clip
    [[nodiscard]] AudioClip flattenAndMergeComp(
        ClipId newClipId,
        MediaAssetId consolidatedAssetId,
        const std::string& compName = "Comp Lead Vocal") const;

    // Crossfade Calculation Utility
    static void applyEqualPowerCrossfade(
        float* outOutgoing,
        float* outIncoming,
        std::size_t fadeLength) noexcept;

    // Find nearest zero crossing within +/- window samples
    static std::uint64_t findNearestZeroCrossing(
        const float* buffer,
        std::size_t bufferLength,
        std::uint64_t targetSample,
        std::size_t window = 64) noexcept;

private:
    TrackId m_trackId {0};
    std::uint32_t m_nextTakeId {1};
    std::uint32_t m_nextSliceId {1};
    std::uint32_t m_soloedTakeId {0}; // 0 = no solo active

    std::vector<TakeLane> m_takes;
    std::vector<CompSlice> m_slices;
};

// Melodyne-Style Pitch-Blob & Formant Canvas
// Represents segmented musical vocal notes with pitch trajectory, drift, vibrato, and formant morphing.
struct PitchBlob
{
    std::uint32_t blobId {0};
    std::uint64_t startSample {0};
    std::uint64_t endSample {0};
    float detectedMidiPitch {60.0f};      // e.g. 69.35 (A4 + 35 cents)
    float targetMidiPitch {60.0f};        // target musical semitone (e.g. 69.0)
    float fineTuneCents {0.0f};           // user fine tuning offset (-100 to +100 cents)
    float vibratoDepthScale {1.0f};       // 0.0 = completely flat, 1.0 = natural, >1.0 = exaggerated
    float formantShiftSemitones {0.0f};   // -12.0 to +12.0 formant throat shift
    float pitchDriftSlope {0.0f};         // pitch trajectory slope across syllable
    std::string noteName {"C4"};
    bool isVoiced {true};
};

class PitchBlobEditor
{
public:
    PitchBlobEditor() = default;
    ~PitchBlobEditor() = default;

    // Analyze monophonic audio waveform and segment into discrete pitch blobs
    void analyzeAudio(
        const float* audioSamples,
        std::size_t sampleCount,
        double sampleRate);

    [[nodiscard]] const std::vector<PitchBlob>& blobs() const noexcept { return m_blobs; }
    [[nodiscard]] std::vector<PitchBlob>& blobsRef() noexcept { return m_blobs; }
    [[nodiscard]] const PitchBlob* findBlob(std::uint32_t blobId) const noexcept;

    // Syllable tuning & manipulation
    bool setBlobTuning(std::uint32_t blobId, float targetMidi, float fineTuneCents) noexcept;
    bool setBlobVibrato(std::uint32_t blobId, float vibratoScale) noexcept;
    bool setBlobFormant(std::uint32_t blobId, float formantShiftSemitones) noexcept;

    // Musical scale quantize all blobs (amount: 0.0 = no change, 1.0 = 100% snapped to scale)
    void quantizeToScale(
        const std::vector<int>& allowedMidiClasses,
        float snapAmount = 1.0f);

    // Render / apply pitch & formant corrections to audio output
    void renderTunedAudio(
        const float* inputSamples,
        float* outputSamples,
        std::size_t sampleCount,
        double sampleRate) const noexcept;

    void clear() noexcept { m_blobs.clear(); m_nextBlobId = 1; }

    static std::string midiPitchToNoteName(float midiPitch);

private:
    std::vector<PitchBlob> m_blobs;
    std::uint32_t m_nextBlobId {1};
};

// Groove Template & Elastic Warp Quantizer (MPC 60 / SP-1200 / Funk Groove extraction)
struct GrooveTemplate
{
    std::string name {"Default Groove"};
    std::vector<float> stepTimingOffsets;    // Timing shift per 16th grid step (-0.25 to +0.25 beats)
    std::vector<float> stepVelocityAccents;  // Velocity scaling per 16th grid step (0.5 to 1.5)
    float swingAmount {0.50f};              // 0.50 = straight, 0.58 = MPC swing, 0.66 = triplet shuffle
    float humanizeJitter {0.0f};            // Random timing jitter amount (0.0 to 0.05 beats)
};

class GrooveQuantizer
{
public:
    GrooveQuantizer() = default;
    ~GrooveQuantizer() = default;

    // Preset groove templates
    [[nodiscard]] static GrooveTemplate mpc60Swing(float swing = 0.58f);
    [[nodiscard]] static GrooveTemplate dillaShuffle();
    [[nodiscard]] static GrooveTemplate funkHumanize();

    // Extract groove template from audio transients relative to beat grid
    [[nodiscard]] static GrooveTemplate extractGrooveFromAudio(
        const float* audioSamples,
        std::size_t sampleCount,
        double sampleRate,
        double bpm);

    // Apply groove template to timestamps (beat positions)
    static void applyGrooveToBeatTimestamps(
        std::vector<double>& beatTimestamps,
        const GrooveTemplate& groove,
        float grooveAmount = 1.0f) noexcept;
};

} // namespace dawg::project
