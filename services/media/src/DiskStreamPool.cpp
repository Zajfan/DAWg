#include <dawg/media/DiskStreamPool.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace dawg::media {

DiskStreamPool::DiskStreamPool(AudioAssetReaderFactory& factory,
                               const std::size_t workerCount) noexcept
    : m_factory(factory)
    , m_workerCount(std::max<std::size_t>(1, workerCount))
{
}

DiskStreamPool::~DiskStreamPool()
{
    stop();
}

std::expected<VoiceStreamId, MediaError> DiskStreamPool::addVoice(
    const project::AudioAsset& asset,
    const project::AudioClip& clip,
    const std::size_t ringCapacityFrames,
    const std::uint32_t engineSampleRate)
{
    stop();

    if (asset.id == 0 || clip.assetId != asset.id || clip.lengthSamples == 0
        || ringCapacityFrames == 0 || asset.sampleRate == 0 || asset.channelCount == 0
        || asset.frameCount == 0 || clip.sourceOffset > asset.frameCount
        || clip.lengthSamples > asset.frameCount - clip.sourceOffset)
    {
        return std::unexpected(MediaError {
            MediaError::Code::invalidMetadata,
            "Invalid asset or clip parameters for DiskStreamPool voice"});
    }

    auto opened = m_factory.open(asset.path);
    if (! opened.has_value() || *opened == nullptr)
    {
        return std::unexpected(opened.has_value()
            ? MediaError {MediaError::Code::cannotOpen, "Reader factory returned null"}
            : opened.error());
    }

    const auto meta = (*opened)->metadata();
    if (meta.channelCount != asset.channelCount || meta.frameCount < asset.frameCount)
    {
        return std::unexpected(MediaError {
            MediaError::Code::invalidMetadata,
            "Opened asset metadata does not match project asset"});
    }

    auto voice = std::make_unique<VoiceStream>();
    voice->id = m_nextVoiceId++;
    voice->reader = std::move(*opened);
    voice->clipStartSource = clip.sourceOffset;
    voice->clipEndSource = clip.sourceOffset + clip.lengthSamples;
    voice->nextSourceRead = clip.sourceOffset;
    voice->nextTargetWrite = clip.sourceOffset;
    voice->capacityFrames = ringCapacityFrames;
    voice->channelCount = asset.channelCount;

    const std::uint32_t targetRate = (engineSampleRate == 0) ? meta.sampleRate : engineSampleRate;
    voice->usesResampler = (targetRate != meta.sampleRate);

    if (! voice->ring.prepare(asset.channelCount, ringCapacityFrames))
    {
        return std::unexpected(MediaError {
            MediaError::Code::unavailable,
            "Cannot prepare voice PlaybackRingBuffer"});
    }
    voice->ring.requestSourceFrame(clip.sourceOffset);

    if (voice->usesResampler)
    {
        if (! voice->resampler.prepare(
                meta.sampleRate, targetRate, asset.channelCount, ringCapacityFrames))
        {
            return std::unexpected(MediaError {
                MediaError::Code::unavailable,
                "Cannot prepare voice AudioResampler"});
        }
    }

    try
    {
        voice->decodeScratch.assign(
            static_cast<std::size_t>(asset.channelCount) * ringCapacityFrames, 0.0f);
        voice->decodePlanes.resize(asset.channelCount, nullptr);
        for (std::uint16_t ch = 0; ch < asset.channelCount; ++ch)
            voice->decodePlanes[ch] = voice->decodeScratch.data() + ch * ringCapacityFrames;

        if (voice->usesResampler)
        {
            voice->resampleScratch.assign(
                static_cast<std::size_t>(asset.channelCount) * ringCapacityFrames, 0.0f);
            voice->resamplePlanes.resize(asset.channelCount, nullptr);
            for (std::uint16_t ch = 0; ch < asset.channelCount; ++ch)
                voice->resamplePlanes[ch] = voice->resampleScratch.data() + ch * ringCapacityFrames;
        }
    }
    catch (...)
    {
        return std::unexpected(MediaError {
            MediaError::Code::unavailable,
            "Cannot allocate voice scratch buffers"});
    }

    const auto id = voice->id;
    m_voices.push_back(std::move(voice));
    return id;
}

engine::PlaybackRingBuffer* DiskStreamPool::buffer(const VoiceStreamId id) noexcept
{
    for (const auto& voice : m_voices)
    {
        if (voice->id == id)
            return &voice->ring;
    }
    return nullptr;
}

void DiskStreamPool::start()
{
    if (m_running.load(std::memory_order_acquire) || m_voices.empty())
        return;

    m_running.store(true, std::memory_order_release);
    m_workers.reserve(m_workerCount);
    for (std::size_t i = 0; i < m_workerCount; ++i)
        m_workers.emplace_back([this] { workerLoop(); });
}

void DiskStreamPool::stop() noexcept
{
    m_running.store(false, std::memory_order_release);
    for (auto& worker : m_workers)
    {
        if (worker.joinable())
            worker.join();
    }
    m_workers.clear();
}

void DiskStreamPool::workerLoop() noexcept
{
    while (m_running.load(std::memory_order_acquire))
    {
        // Build urgency order by lowest availableFrames()
        VoiceStream* mostUrgent = nullptr;
        std::size_t lowestAvailable = std::numeric_limits<std::size_t>::max();

        for (const auto& voicePtr : m_voices)
        {
            auto& voice = *voicePtr;
            if (voice.nextSourceRead >= voice.clipEndSource)
                continue;
            const auto avail = voice.ring.availableFrames();
            if (avail >= voice.capacityFrames)
                continue;
            if (avail < lowestAvailable)
            {
                lowestAvailable = avail;
                mostUrgent = &voice;
            }
        }

        bool didWork = false;
        if (mostUrgent != nullptr && mostUrgent->refillMutex.try_lock())
        {
            didWork = refillVoice(*mostUrgent);
            mostUrgent->refillMutex.unlock();
        }
        else
        {
            for (const auto& voicePtr : m_voices)
            {
                if (voicePtr->refillMutex.try_lock())
                {
                    if (refillVoice(*voicePtr))
                        didWork = true;
                    voicePtr->refillMutex.unlock();
                    if (didWork)
                        break;
                }
            }
        }

        if (! didWork)
            std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
}

bool DiskStreamPool::refillVoice(VoiceStream& voice) noexcept
{
    if (! voice.reader)
        return false;

    const auto requested = std::clamp(
        voice.ring.requestedSourceFrame(),
        voice.clipStartSource,
        voice.clipEndSource);

    if (! voice.anchored)
    {
        voice.nextSourceRead = requested;
        voice.nextTargetWrite = requested;
        voice.resampler.reset();
        voice.anchored = true;
    }

    const auto available = voice.ring.availableFrames();
    if (available >= voice.capacityFrames || voice.nextSourceRead >= voice.clipEndSource)
        return false;

    const auto freeTargetFrames = voice.capacityFrames - available;
    const auto remainingSource =
        static_cast<std::size_t>(voice.clipEndSource - voice.nextSourceRead);

    if (! voice.usesResampler)
    {
        const auto toRead = std::min(freeTargetFrames, remainingSource);
        if (toRead == 0)
            return false;

        if (! voice.reader->read(voice.nextSourceRead, voice.decodePlanes, toRead))
            return false;

        const std::span<const float* const> srcPlanes {
            voice.decodePlanes.data(), voice.decodePlanes.size()};
        const auto written = voice.ring.write(voice.nextTargetWrite, srcPlanes, toRead);
        voice.nextSourceRead += written;
        voice.nextTargetWrite += written;
        return written > 0;
    }

    const auto toReadSource = std::min(voice.capacityFrames, remainingSource);
    if (toReadSource == 0)
        return false;

    if (! voice.reader->read(voice.nextSourceRead, voice.decodePlanes, toReadSource))
        return false;

    const std::span<const float* const> srcPlanes {
        voice.decodePlanes.data(), voice.decodePlanes.size()};
    std::size_t consumedSource = 0;
    const auto producedTarget = voice.resampler.process(
        srcPlanes, toReadSource, voice.resamplePlanes, freeTargetFrames, consumedSource);
    if (producedTarget == 0)
        return false;

    const std::span<const float* const> resampledPlanes {
        voice.resamplePlanes.data(), voice.resamplePlanes.size()};
    const auto written = voice.ring.write(
        voice.nextTargetWrite, resampledPlanes, producedTarget);
    voice.nextSourceRead += consumedSource;
    voice.nextTargetWrite += written;
    return written > 0;
}

} // namespace dawg::media
