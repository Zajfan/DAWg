#include <dawg/media/WaveformCache.h>

#include <algorithm>
#include <cmath>

namespace dawg::media {

WaveformCache::WaveformCache(AudioAssetReaderFactory& factory, const std::size_t workerCount)
    : m_factory {factory}
{
    m_workers.reserve(workerCount);
    for (std::size_t i = 0; i < workerCount; ++i)
    {
        m_workers.emplace_back([this] { workerLoop(); });
    }
}

WaveformCache::~WaveformCache()
{
    m_stopping.store(true);
    m_cv.notify_all();
    for (auto& worker : m_workers)
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }
}

void WaveformCache::requestThumbnail(const project::AudioAsset& asset)
{
    {
        std::lock_guard lock(m_mutex);
        if (m_cache.contains(asset.id) || m_inFlight.contains(asset.id))
        {
            return;
        }
        for (const auto& item : m_queue)
        {
            if (item.id == asset.id)
            {
                return;
            }
        }
        m_inFlight.insert(asset.id);
        m_queue.push_back(asset);
    }
    m_cv.notify_one();
}

void WaveformCache::generateSync(const project::AudioAsset& asset)
{
    {
        std::lock_guard lock(m_mutex);
        if (m_cache.contains(asset.id))
        {
            return;
        }
        m_inFlight.insert(asset.id);
    }

    processAsset(asset);
}

bool WaveformCache::isReady(const project::MediaAssetId assetId) const noexcept
{
    std::lock_guard lock(m_mutex);
    return m_cache.contains(assetId);
}

std::vector<MinMaxPeak> WaveformCache::getMinMaxPeaks(
    const project::MediaAssetId assetId,
    const std::uint16_t channelIndex,
    const std::uint64_t startSample,
    const std::uint64_t lengthSamples,
    const std::size_t targetPixelCount) const
{
    if (targetPixelCount == 0 || lengthSamples == 0)
    {
        return {};
    }

    std::lock_guard lock(m_mutex);
    const auto it = m_cache.find(assetId);
    if (it == m_cache.end())
    {
        return {};
    }

    const auto& waveform = it->second;
    if (channelIndex >= waveform.channels.size())
    {
        return {};
    }

    const auto& chPeaks = waveform.channels[channelIndex];
    if (chPeaks.empty() || waveform.totalFrames == 0)
    {
        return {};
    }

    std::vector<MinMaxPeak> result(targetPixelCount, MinMaxPeak {0.0f, 0.0f});

    const std::uint64_t totalFrames = waveform.totalFrames;
    if (startSample >= totalFrames)
    {
        return result;
    }

    const std::uint64_t endSample = std::min(startSample + lengthSamples, totalFrames);
    const std::size_t bucketSize = waveform.bucketSizeFrames;

    for (std::size_t p = 0; p < targetPixelCount; ++p)
    {
        const std::uint64_t pxStart = startSample + (p * lengthSamples) / targetPixelCount;
        const std::uint64_t pxEnd = startSample + ((p + 1) * lengthSamples) / targetPixelCount;

        if (pxStart >= totalFrames || pxStart >= endSample)
        {
            result[p] = MinMaxPeak {0.0f, 0.0f};
            continue;
        }

        const std::uint64_t effectiveEnd = std::max(pxStart + 1, std::min(pxEnd, endSample));
        const std::size_t startBucket = static_cast<std::size_t>(pxStart / bucketSize);
        const std::size_t endBucket = static_cast<std::size_t>((effectiveEnd + bucketSize - 1) / bucketSize);

        const std::size_t bEnd = std::min(endBucket, chPeaks.size());
        if (startBucket >= bEnd)
        {
            result[p] = MinMaxPeak {0.0f, 0.0f};
            continue;
        }

        float minVal = chPeaks[startBucket].minVal;
        float maxVal = chPeaks[startBucket].maxVal;
        for (std::size_t b = startBucket + 1; b < bEnd; ++b)
        {
            if (chPeaks[b].minVal < minVal)
            {
                minVal = chPeaks[b].minVal;
            }
            if (chPeaks[b].maxVal > maxVal)
            {
                maxVal = chPeaks[b].maxVal;
            }
        }

        result[p] = MinMaxPeak {minVal, maxVal};
    }

    return result;
}

void WaveformCache::clear() noexcept
{
    std::lock_guard lock(m_mutex);
    m_queue.clear();
    m_cache.clear();
    m_inFlight.clear();
}

void WaveformCache::workerLoop() noexcept
{
    while (! m_stopping.load())
    {
        project::AudioAsset asset;
        {
            std::unique_lock lock(m_mutex);
            m_cv.wait(lock, [this] {
                return m_stopping.load() || ! m_queue.empty();
            });

            if (m_stopping.load() && m_queue.empty())
            {
                break;
            }

            if (m_queue.empty())
            {
                continue;
            }

            asset = std::move(m_queue.front());
            m_queue.erase(m_queue.begin());
        }

        processAsset(asset);
    }
}

void WaveformCache::processAsset(const project::AudioAsset& asset) noexcept
{
    auto readerResult = m_factory.open(asset.path);
    if (! readerResult.has_value() || ! readerResult.value())
    {
        std::lock_guard lock(m_mutex);
        m_inFlight.erase(asset.id);
        return;
    }

    auto reader = std::move(readerResult.value());
    const auto meta = reader->metadata();
    if (meta.channelCount == 0 || meta.frameCount == 0)
    {
        std::lock_guard lock(m_mutex);
        m_inFlight.erase(asset.id);
        return;
    }

    constexpr std::size_t bucketSize = 64;
    const std::size_t totalBuckets = static_cast<std::size_t>((meta.frameCount + bucketSize - 1) / bucketSize);

    AssetWaveform waveform;
    waveform.channelCount = meta.channelCount;
    waveform.bucketSizeFrames = bucketSize;
    waveform.totalFrames = meta.frameCount;
    waveform.channels.resize(meta.channelCount);
    for (auto& ch : waveform.channels)
    {
        ch.resize(totalBuckets, MinMaxPeak {0.0f, 0.0f});
    }

    constexpr std::size_t chunkSize = 4096;
    std::vector<float> sampleStorage(chunkSize * meta.channelCount, 0.0f);
    std::vector<float*> channelPointers(meta.channelCount, nullptr);
    for (std::size_t ch = 0; ch < meta.channelCount; ++ch)
    {
        channelPointers[ch] = sampleStorage.data() + ch * chunkSize;
    }

    std::uint64_t currentFrame = 0;
    while (currentFrame < meta.frameCount)
    {
        const std::size_t framesToRead = static_cast<std::size_t>(
            std::min<std::uint64_t>(chunkSize, meta.frameCount - currentFrame));

        if (! reader->read(currentFrame, channelPointers, framesToRead))
        {
            break;
        }

        for (std::size_t ch = 0; ch < meta.channelCount; ++ch)
        {
            const float* chSamples = channelPointers[ch];
            for (std::size_t f = 0; f < framesToRead; ++f)
            {
                const std::uint64_t frameIdx = currentFrame + f;
                const std::size_t bucketIdx = static_cast<std::size_t>(frameIdx / bucketSize);
                if (bucketIdx >= totalBuckets)
                {
                    break;
                }

                const float val = chSamples[f];
                auto& peak = waveform.channels[ch][bucketIdx];
                if (frameIdx % bucketSize == 0)
                {
                    peak.minVal = val;
                    peak.maxVal = val;
                }
                else
                {
                    if (val < peak.minVal)
                    {
                        peak.minVal = val;
                    }
                    if (val > peak.maxVal)
                    {
                        peak.maxVal = val;
                    }
                }
            }
        }

        currentFrame += framesToRead;
    }

    std::lock_guard lock(m_mutex);
    m_cache[asset.id] = std::move(waveform);
    m_inFlight.erase(asset.id);
}

} // namespace dawg::media
