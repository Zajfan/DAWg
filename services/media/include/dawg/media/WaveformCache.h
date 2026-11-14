#pragma once

#include <dawg/media/AudioAssetReader.h>
#include <dawg/project/ProjectTypes.h>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace dawg::media {

struct MinMaxPeak
{
    float minVal {0.0f};
    float maxVal {0.0f};

    friend bool operator==(const MinMaxPeak&, const MinMaxPeak&) = default;
};

struct AssetWaveform
{
    std::uint16_t channelCount {1};
    std::size_t bucketSizeFrames {64};
    std::uint64_t totalFrames {0};
    std::vector<std::vector<MinMaxPeak>> channels;
};

class WaveformCache final
{
public:
    explicit WaveformCache(AudioAssetReaderFactory& factory, std::size_t workerCount = 1);
    ~WaveformCache();

    WaveformCache(const WaveformCache&) = delete;
    WaveformCache& operator=(const WaveformCache&) = delete;

    void requestThumbnail(const project::AudioAsset& asset);
    void generateSync(const project::AudioAsset& asset);

    [[nodiscard]] bool isReady(project::MediaAssetId assetId) const noexcept;

    [[nodiscard]] std::vector<MinMaxPeak> getMinMaxPeaks(
        project::MediaAssetId assetId,
        std::uint16_t channelIndex,
        std::uint64_t startSample,
        std::uint64_t lengthSamples,
        std::size_t targetPixelCount) const;

    void clear() noexcept;

private:
    void workerLoop() noexcept;
    void processAsset(const project::AudioAsset& asset) noexcept;

    AudioAssetReaderFactory& m_factory;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::vector<project::AudioAsset> m_queue;
    std::unordered_set<project::MediaAssetId> m_inFlight;
    std::unordered_map<project::MediaAssetId, AssetWaveform> m_cache;
    std::atomic<bool> m_stopping {false};
    std::vector<std::thread> m_workers;
};

} // namespace dawg::media
