#pragma once

#include <dawg/engine/dsp/AudioTranscriber.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace dawg::engine::dsp {

enum class WarpMode : std::uint8_t
{
    beats,   // Transient-preserving slice-and-hold (percussive loops, drums)
    tonal,   // Formant-preserving pitch-synchronous WSOLA (vocals, monophonic acoustic)
    complex  // Polyphonic phase-locked granular stretch (full mixes, guitars, pianos)
};

struct WarpMarker
{
    std::size_t sourceSample {0}; // Sample index in original audio file
    double beatPosition {0.0};    // Musical beat position on timeline (quarter notes)
    bool pinned {false};          // User-locked marker vs auto-detected transient

    friend bool operator==(const WarpMarker&, const WarpMarker&) = default;
};

// Elastic Audio Time-Warping Engine (Ableton / Pro Tools Elastic Audio style)
// Maps arbitrary timeline musical beat coordinates to source audio samples non-linearly
// while preserving transients, pitch, and phase alignment.
class AudioWarpEngine final
{
public:
    AudioWarpEngine() = default;
    ~AudioWarpEngine() = default;

    // Marker Management
    std::size_t addMarker(std::size_t sourceSample, double beatPosition, bool pinned = true);
    bool removeMarker(std::size_t index);
    bool moveMarker(std::size_t index, double newBeatPosition);
    void clearMarkers();

    [[nodiscard]] const std::vector<WarpMarker>& markers() const noexcept { return m_markers; }
    [[nodiscard]] std::size_t markerCount() const noexcept { return m_markers.size(); }
    [[nodiscard]] const WarpMarker* getMarker(std::size_t index) const noexcept;

    // Auto-detect audio transients and generate initial unpinned warp markers
    std::size_t autoDetectTransients(
        const float* audioData,
        std::size_t sampleCount,
        double sampleRate,
        double bpm);

    // Grid Quantization & Groove Alignment
    // gridDivision: 1.0 = Quarter, 0.5 = 8th, 0.25 = 16th, 0.125 = 32nd, 0.33333 = Triplet
    void quantizeMarkers(double gridDivision = 0.25, float strength = 1.0f);
    void applyGroove(const ExtractedGroove& groove, float strength = 1.0f);

    // Continuous beat <-> source sample coordinate translation
    [[nodiscard]] double beatToSourceSample(double beatPosition, double bpm, double sampleRate) const noexcept;
    [[nodiscard]] double sourceSampleToBeat(double sampleIndex, double bpm, double sampleRate) const noexcept;

    // Offline / Preview Time-Warp Rendering
    // Renders warped audio channels matching timeline length from startBeat to endBeat
    [[nodiscard]] std::vector<std::vector<float>> renderWarped(
        std::span<const std::span<const float>> inputChannels,
        double sampleRate,
        double bpm,
        double startBeat,
        double endBeat,
        WarpMode mode = WarpMode::complex) const;

private:
    void sortMarkers();

    std::vector<WarpMarker> m_markers;
};

} // namespace dawg::engine::dsp
