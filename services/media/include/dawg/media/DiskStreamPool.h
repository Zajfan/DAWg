#pragma once

#include <dawg/engine/PlaybackRingBuffer.h>
#include <dawg/media/AudioAssetReader.h>
#include <dawg/media/AudioResampler.h>
#include <dawg/project/ProjectTypes.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace dawg::media {

using VoiceStreamId = std::uint64_t;

// Multiplexed disk streamer pool servicing multiple PlaybackRingBuffers across
// a fixed pool of background worker threads, prioritized by lowest availableFrames().
class DiskStreamPool final
{
public:
    explicit DiskStreamPool(AudioAssetReaderFactory& factory,
                            std::size_t workerCount = 2) noexcept;
    ~DiskStreamPool();

    DiskStreamPool(const DiskStreamPool&) = delete;
    DiskStreamPool& operator=(const DiskStreamPool&) = delete;

    [[nodiscard]] std::expected<VoiceStreamId, MediaError> addVoice(
        const project::AudioAsset& asset,
        const project::AudioClip& clip,
        std::size_t ringCapacityFrames,
        std::uint32_t engineSampleRate = 0);

    [[nodiscard]] engine::PlaybackRingBuffer* buffer(VoiceStreamId id) noexcept;

    void start();
    void stop() noexcept;

private:
    struct VoiceStream
    {
        VoiceStreamId id {};
        std::unique_ptr<AudioAssetReader> reader;
        engine::PlaybackRingBuffer ring;
        AudioResampler resampler;
        std::vector<float> decodeScratch;
        std::vector<float*> decodePlanes;
        std::vector<float> resampleScratch;
        std::vector<float*> resamplePlanes;
        std::uint64_t clipStartSource {};
        std::uint64_t clipEndSource {};
        std::uint64_t nextSourceRead {};
        std::uint64_t nextTargetWrite {};
        std::size_t capacityFrames {};
        std::uint16_t channelCount {};
        bool usesResampler {};
        bool anchored {};
        std::mutex refillMutex;
    };

    void workerLoop() noexcept;
    bool refillVoice(VoiceStream& voice) noexcept;

    AudioAssetReaderFactory& m_factory;
    std::size_t m_workerCount {2};
    VoiceStreamId m_nextVoiceId {1};
    std::vector<std::unique_ptr<VoiceStream>> m_voices;
    std::atomic<bool> m_running {false};
    std::vector<std::thread> m_workers;
};

} // namespace dawg::media
