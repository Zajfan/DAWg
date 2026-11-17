#pragma once

#include <dawg/engine/RecordRingBuffer.h>
#include <dawg/media/AudioAssetWriter.h>
#include <dawg/media/WaveformCache.h>
#include <dawg/project/ProjectTypes.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <map>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace dawg::media {

struct MultiTrackRecordedTake
{
    project::TrackId trackId {0};
    project::AudioAsset asset;
    project::AudioClip clip;
    std::vector<MinMaxPeak> overview;
};

// Zero-allocation background disk streaming engine for multi-track WAV/BWF recording.
// - Drains wait-free SPSC RecordRingBuffers from multiple armed tracks concurrently.
// - Computes min/max waveform peak overview points on the fly so recorded audio
//   is visible immediately in the arrangement timeline during tracking.
class MultiTrackDiskStreamer final
{
public:
    static constexpr std::size_t overviewBucketSize = 256; // 256 frames per peak point
    static constexpr std::size_t maxTracks = 16;

    explicit MultiTrackDiskStreamer(AudioAssetWriterFactory& factory) noexcept;
    ~MultiTrackDiskStreamer();

    MultiTrackDiskStreamer(const MultiTrackDiskStreamer&) = delete;
    MultiTrackDiskStreamer& operator=(const MultiTrackDiskStreamer&) = delete;

    [[nodiscard]] std::expected<void, MediaError> prepareTrack(
        project::TrackId trackId,
        std::string_view filePath,
        std::uint32_t sampleRate,
        std::uint16_t channelCount,
        std::size_t ringCapacityFrames,
        std::uint64_t timelineStartSample,
        project::AudioAssetPathKind pathKind = project::AudioAssetPathKind::projectRelative);

    void start();
    void stop() noexcept;

    [[nodiscard]] bool isRecording() const noexcept { return m_running.load(std::memory_order_relaxed); }

    // Finalizes all armed tracks, flushes disk buffers, and returns take descriptors
    [[nodiscard]] std::vector<MultiTrackRecordedTake> finalizeAll();

    // Access ring buffer for a specific track to write from audio callback thread
    [[nodiscard]] engine::RecordRingBuffer* getTrackBuffer(project::TrackId trackId) noexcept;

    // Get live overview points for GUI timeline preview
    [[nodiscard]] std::vector<MinMaxPeak> getLiveOverview(project::TrackId trackId) const;

    [[nodiscard]] std::uint64_t getTrackRecordedFrames(project::TrackId trackId) const noexcept;

    void drainAllOnce() noexcept;

private:
    struct TrackContext
    {
        project::TrackId trackId {0};
        std::string filePath;
        project::AudioAssetPathKind pathKind {project::AudioAssetPathKind::projectRelative};
        std::uint32_t sampleRate {48000};
        std::uint16_t channelCount {1};
        std::uint64_t timelineStartSample {0};
        std::atomic<std::uint64_t> recordedFrames {0};

        engine::RecordRingBuffer buffer;
        std::unique_ptr<AudioAssetWriter> writer;

        // Scratch buffers for reading from ring buffer
        std::vector<float> scratch;
        std::vector<float*> scratchPlanes;

        // Live overview cache
        mutable std::mutex overviewMutex;
        std::vector<MinMaxPeak> overview;
        float currentBucketMin {0.0f};
        float currentBucketMax {0.0f};
        std::size_t bucketFrameCount {0};
    };

    void workerLoop() noexcept;
    void drainTrack(TrackContext& ctx) noexcept;

    AudioAssetWriterFactory& m_factory;
    std::map<project::TrackId, std::unique_ptr<TrackContext>> m_tracks;
    std::atomic<bool> m_running {false};
    std::thread m_worker;
    project::MediaAssetId m_nextAssetId {1000};
    project::ClipId m_nextClipId {2000};
};

} // namespace dawg::media
