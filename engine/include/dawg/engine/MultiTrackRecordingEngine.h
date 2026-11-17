#pragma once

#include <dawg/engine/AudioBufferView.h>
#include <dawg/engine/RecordRingBuffer.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <string>
#include <vector>

namespace dawg::engine {

struct CueMixSend
{
    bool enabled {true};
    float gainDb {0.0f}; // -inf to +6 dB
    float pan {0.0f};    // -1 (L) to +1 (R)
    bool muted {false};
};

struct TrackRecordConfig
{
    bool armed {false};
    bool inputMonitoring {false}; // Direct low-latency monitoring
    std::uint16_t inputChannel {0};
    float monitorGainDb {0.0f};
    float monitorPan {0.0f};
    bool phaseInvert {false};

    // Cue Mix Sends (e.g. Cue A for Singer, Cue B for Drummer)
    std::array<CueMixSend, 2> cueSends {};
};

// Overview Peak Min/Max pair for fast GUI waveform rendering
struct WaveformPeakPoint
{
    float min {-1.0f};
    float max {1.0f};
};

// Broadcast WAV (BWF) bext chunk metadata
struct BwfBroadcastChunk
{
    std::string description {"DAWg Audio Recording"};
    std::string originator {"DAWg Digital Audio Workstation"};
    std::string originatorReference;
    std::string originationDate {"2026-11-17"};
    std::string originationTime {"04:20:00"};
    std::uint64_t timeReferenceSamples {0}; // SMPTE sample alignment
};

// Hardware Loopback Latency Measurement Tool
class LatencyLoopbackCalibrator
{
public:
    LatencyLoopbackCalibrator() = default;

    // Generates a band-limited test chirp / impulse to send to output
    void generateTestImpulse(float* outputBuffer, std::size_t sampleCount) const noexcept;

    // Analyzes loopback recording buffer and calculates round-trip latency in fractional samples
    [[nodiscard]] double measureRoundTripLatency(
        const float* recordedInput,
        std::size_t sampleCount,
        std::size_t searchOffset = 0) const noexcept;
};

// Multi-Track Recording Engine with Punch-In/Out, Zero-Latency Cue Mixes,
// Loop Recording with Automatic Take Lanes, and BWF Overview Caches.
class MultiTrackRecordingEngine final
{
public:
    static constexpr std::size_t maxRecordTracks = 16;
    static constexpr std::size_t overviewBlockSize = 256; // 256 samples per min/max point

    MultiTrackRecordingEngine() noexcept;
    ~MultiTrackRecordingEngine() = default;

    void reset() noexcept;

    // Track Armed & Monitoring Configuration
    void setTrackConfig(std::size_t trackIdx, const TrackRecordConfig& config) noexcept;
    [[nodiscard]] const TrackRecordConfig& getTrackConfig(std::size_t trackIdx) const noexcept;
    [[nodiscard]] TrackRecordConfig& getTrackConfig(std::size_t trackIdx) noexcept;

    // Punch-In / Punch-Out Engine
    void setPunchEnabled(bool enabled) noexcept { m_punchEnabled = enabled; }
    [[nodiscard]] bool punchEnabled() const noexcept { return m_punchEnabled; }

    void setPunchPoints(std::uint64_t inSample, std::uint64_t outSample) noexcept;
    [[nodiscard]] std::uint64_t punchInSample() const noexcept { return m_punchInSample; }
    [[nodiscard]] std::uint64_t punchOutSample() const noexcept { return m_punchOutSample; }

    // Pre-Roll & Count-In
    void setPreRollBars(std::uint32_t bars) noexcept { m_preRollBars = bars; }
    [[nodiscard]] std::uint32_t preRollBars() const noexcept { return m_preRollBars; }

    // Latency Alignment Offset (Round-Trip Latency Compensation)
    void setLatencyCompensationSamples(std::uint32_t samples) noexcept { m_latencyOffsetSamples = samples; }
    [[nodiscard]] std::uint32_t latencyCompensationSamples() const noexcept { return m_latencyOffsetSamples; }

    // Loop Recording & Take Lane Management
    void setLoopRecordingEnabled(bool enabled) noexcept { m_loopRecording = enabled; }
    [[nodiscard]] bool loopRecordingEnabled() const noexcept { return m_loopRecording; }
    void setLoopRegion(std::uint64_t startSample, std::uint64_t endSample) noexcept;
    [[nodiscard]] std::uint32_t currentTakeIndex() const noexcept { return m_currentTakeIndex; }

    // Real-Time Audio Process (Audio Callback thread - zero heap allocations)
    // - Monitors live inputs into master or cue mixes with micro-faded punch boundaries
    // - Writes active audio into the RecordRingBuffers
    void processRecording(
        const AudioBufferView& hardwareInputs,
        AudioBufferView& monitorOut,
        AudioBufferView* cueOutA,
        AudioBufferView* cueOutB,
        std::uint64_t currentTimelineSample,
        std::size_t frameCount) noexcept;

    // BWF Metadata & Peak Overview Caches
    [[nodiscard]] static BwfBroadcastChunk createBwfChunk(
        std::uint64_t timelineSample,
        const std::string& trackName);

    // Compute peak overview min/max cache for an audio buffer
    static void generateWaveformOverview(
        const float* audioData,
        std::size_t sampleCount,
        std::vector<WaveformPeakPoint>& outOverview);

private:
    [[nodiscard]] float computePunchFade(std::uint64_t frameTimelinePos) const noexcept;

    std::array<TrackRecordConfig, maxRecordTracks> m_trackConfigs {};
    std::array<RecordRingBuffer, maxRecordTracks> m_ringBuffers {};

    bool m_punchEnabled {false};
    std::uint64_t m_punchInSample {0};
    std::uint64_t m_punchOutSample {0};
    static constexpr std::uint32_t kFadeSamples = 240; // 5ms fade at 48kHz

    std::uint32_t m_preRollBars {0};
    std::uint32_t m_latencyOffsetSamples {0};

    bool m_loopRecording {false};
    std::uint64_t m_loopStartSample {0};
    std::uint64_t m_loopEndSample {0};
    std::uint32_t m_currentTakeIndex {1};
};

} // namespace dawg::engine
