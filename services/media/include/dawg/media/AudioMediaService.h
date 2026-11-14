#pragma once

#include <dawg/engine/PlaybackRingBuffer.h>
#include <dawg/media/AudioAssetReader.h>
#include <dawg/project/ProjectTypes.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace dawg::media {

enum class MediaStatusState : std::uint8_t
{
    idle,
    ready,
    unavailable,
    error
};

struct MediaStatus
{
    MediaStatusState state {MediaStatusState::idle};
    std::string errorMessage;

    [[nodiscard]] bool available() const noexcept
    {
        return state == MediaStatusState::ready;
    }
};

// Message-thread controlled media session owning one background reader worker
// and one preallocated PlaybackRingBuffer. Never accessed on the audio thread.
class AudioMediaService final
{
public:
    explicit AudioMediaService(AudioAssetReaderFactory& factory) noexcept;
    ~AudioMediaService();

    AudioMediaService(const AudioMediaService&) = delete;
    AudioMediaService& operator=(const AudioMediaService&) = delete;

    [[nodiscard]] std::expected<project::AudioAsset, MediaError> inspect(
        std::string_view path,
        project::AudioAssetPathKind pathKind = project::AudioAssetPathKind::projectRelative);

    [[nodiscard]] std::expected<void, MediaError> prepare(
        const project::AudioAsset& asset,
        const project::AudioClip& clip,
        std::size_t capacityFrames);

    void start();
    void stop() noexcept;

    [[nodiscard]] engine::PlaybackRingBuffer& buffer() noexcept;
    [[nodiscard]] const engine::PlaybackRingBuffer& buffer() const noexcept;
    [[nodiscard]] MediaStatus status() const;

private:
    void setStatus(MediaStatusState state, std::string message);
    void workerLoop() noexcept;

    AudioAssetReaderFactory& m_factory;
    engine::PlaybackRingBuffer m_buffer;
    std::unique_ptr<AudioAssetReader> m_reader;
    std::vector<float> m_scratch;
    std::vector<float*> m_scratchChannels;
    std::uint64_t m_clipStartSource {};
    std::uint64_t m_clipEndSource {};
    std::size_t m_capacityFrames {};
    std::uint16_t m_channelCount {};

    std::atomic<bool> m_running {false};
    std::thread m_worker;

    mutable std::mutex m_statusMutex;
    std::atomic<MediaStatusState> m_statusState {MediaStatusState::idle};
    std::string m_statusMessage;
};

} // namespace dawg::media
