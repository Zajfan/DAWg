#pragma once

#include <dawg/engine/RecordRingBuffer.h>
#include <dawg/media/AudioAssetWriter.h>
#include <dawg/project/ProjectTypes.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace dawg::media {

struct RecordedTake
{
    project::AudioAsset asset;
    project::AudioClip clip;
};

// Background recording service owning one RecordRingBuffer and one disk-writer worker.
class AudioRecordService final
{
public:
    explicit AudioRecordService(AudioAssetWriterFactory& factory) noexcept;
    ~AudioRecordService();

    AudioRecordService(const AudioRecordService&) = delete;
    AudioRecordService& operator=(const AudioRecordService&) = delete;

    [[nodiscard]] std::expected<void, MediaError> prepare(
        std::string_view path,
        std::uint32_t sampleRate,
        std::uint16_t channelCount,
        std::size_t ringCapacityFrames,
        project::AudioAssetPathKind pathKind = project::AudioAssetPathKind::projectRelative);

    void start();
    void stop() noexcept;

    [[nodiscard]] std::expected<RecordedTake, MediaError> stopAndFinalize(
        project::TrackId trackId,
        std::uint64_t timelineStartSample);

    [[nodiscard]] engine::RecordRingBuffer& buffer() noexcept { return m_buffer; }
    [[nodiscard]] const engine::RecordRingBuffer& buffer() const noexcept { return m_buffer; }
    [[nodiscard]] std::uint64_t recordedFrames() const noexcept;

private:
    void drainOnce() noexcept;
    void workerLoop() noexcept;

    AudioAssetWriterFactory& m_factory;
    engine::RecordRingBuffer m_buffer;
    std::unique_ptr<AudioAssetWriter> m_writer;
    std::vector<float> m_scratch;
    std::vector<float*> m_scratchPlanes;
    std::string m_path;
    project::AudioAssetPathKind m_pathKind {project::AudioAssetPathKind::projectRelative};
    std::uint32_t m_sampleRate {};
    std::uint16_t m_channelCount {};
    std::size_t m_capacityFrames {};
    std::atomic<std::uint64_t> m_recordedFrames {0};
    std::atomic<bool> m_running {false};
    std::thread m_worker;
};

} // namespace dawg::media
