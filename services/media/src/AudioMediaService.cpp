#include <dawg/media/AudioMediaService.h>

#include <algorithm>
#include <chrono>
#include <utility>

namespace dawg::media {

AudioMediaService::AudioMediaService(AudioAssetReaderFactory& factory) noexcept
    : m_factory {factory}
{
}

AudioMediaService::~AudioMediaService()
{
    stop();
}

std::expected<project::AudioAsset, MediaError> AudioMediaService::inspect(
    const std::string_view path, const project::AudioAssetPathKind pathKind)
{
    if (path.empty())
    {
        return std::unexpected(
            MediaError {MediaError::Code::invalidPath, "Asset path must not be empty"});
    }

    auto opened = m_factory.open(path);
    if (! opened.has_value())
        return std::unexpected(std::move(opened.error()));

    const auto meta = (*opened)->metadata();
    if (meta.sampleRate == 0 || meta.channelCount == 0 || meta.frameCount == 0)
    {
        return std::unexpected(MediaError {
            MediaError::Code::invalidMetadata,
            "Asset metadata must have positive sample rate, channels, and frame count"});
    }

    return project::AudioAsset {
        0,
        std::string(path),
        pathKind,
        meta.sampleRate,
        meta.channelCount,
        meta.frameCount};
}

std::expected<void, MediaError> AudioMediaService::prepare(
    const project::AudioAsset& asset, const project::AudioClip& clip,
    const std::size_t capacityFrames)
{
    stop();
    m_reader.reset();

    if (asset.path.empty())
    {
        m_buffer.reset();
        setStatus(MediaStatusState::unavailable, "Asset path is empty");
        return std::unexpected(
            MediaError {MediaError::Code::invalidPath, "Asset path must not be empty"});
    }

    if (asset.sampleRate == 0 || asset.channelCount == 0 || asset.frameCount == 0
        || clip.assetId != asset.id || clip.lengthSamples == 0
        || clip.sourceOffset > asset.frameCount
        || clip.lengthSamples > asset.frameCount - clip.sourceOffset
        || capacityFrames == 0)
    {
        m_buffer.reset();
        setStatus(MediaStatusState::error, "Invalid asset, clip range, or buffer capacity");
        return std::unexpected(MediaError {
            MediaError::Code::invalidMetadata,
            "Invalid asset, clip range, or buffer capacity"});
    }

    if (! m_buffer.prepare(asset.channelCount, capacityFrames))
    {
        setStatus(MediaStatusState::error, "Cannot allocate playback ring buffer");
        return std::unexpected(MediaError {
            MediaError::Code::invalidMetadata, "Cannot allocate playback ring buffer"});
    }

    m_scratch.assign(static_cast<std::size_t>(asset.channelCount) * capacityFrames, 0.0f);
    m_scratchChannels.resize(asset.channelCount);
    for (std::uint16_t ch = 0; ch < asset.channelCount; ++ch)
        m_scratchChannels[ch] = m_scratch.data() + static_cast<std::size_t>(ch) * capacityFrames;

    m_buffer.requestSourceFrame(clip.sourceOffset);

    auto opened = m_factory.open(asset.path);
    if (! opened.has_value())
    {
        m_buffer.reset();
        setStatus(MediaStatusState::unavailable, opened.error().message);
        return std::unexpected(std::move(opened.error()));
    }

    const auto meta = (*opened)->metadata();
    if (meta.sampleRate != asset.sampleRate || meta.channelCount != asset.channelCount
        || meta.frameCount < clip.sourceOffset + clip.lengthSamples)
    {
        m_buffer.reset();
        setStatus(MediaStatusState::error, "Decoded WAV metadata does not match project asset");
        return std::unexpected(MediaError {
            MediaError::Code::invalidMetadata,
            "Decoded WAV metadata does not match project asset"});
    }

    m_reader = std::move(*opened);
    m_clipStartSource = clip.sourceOffset;
    m_clipEndSource = clip.sourceOffset + clip.lengthSamples;
    m_capacityFrames = capacityFrames;
    m_channelCount = asset.channelCount;
    setStatus(MediaStatusState::ready, {});
    return {};
}

void AudioMediaService::start()
{
    if (m_running.load(std::memory_order_acquire) || ! m_reader)
        return;

    m_buffer.reset();
    m_buffer.requestSourceFrame(m_clipStartSource);
    setStatus(MediaStatusState::ready, {});
    m_running.store(true, std::memory_order_release);
    m_worker = std::thread(&AudioMediaService::workerLoop, this);
}

void AudioMediaService::stop() noexcept
{
    m_running.store(false, std::memory_order_release);
    if (m_worker.joinable())
        m_worker.join();
}

engine::PlaybackRingBuffer& AudioMediaService::buffer() noexcept
{
    return m_buffer;
}

const engine::PlaybackRingBuffer& AudioMediaService::buffer() const noexcept
{
    return m_buffer;
}

MediaStatus AudioMediaService::status() const
{
    std::lock_guard<std::mutex> lock {m_statusMutex};
    return MediaStatus {m_statusState.load(std::memory_order_acquire), m_statusMessage};
}

void AudioMediaService::setStatus(const MediaStatusState state, std::string message)
{
    std::lock_guard<std::mutex> lock {m_statusMutex};
    m_statusMessage = std::move(message);
    m_statusState.store(state, std::memory_order_release);
}

void AudioMediaService::workerLoop() noexcept
{
    std::uint64_t nextWriteFrame = m_clipStartSource;
    std::uint64_t lastRequestedFrame = m_clipStartSource;

    while (m_running.load(std::memory_order_acquire))
    {
        const auto requested = m_buffer.requestedSourceFrame();
        const auto buffered = m_buffer.availableFrames();
        const auto oldestBuffered =
            nextWriteFrame >= buffered ? nextWriteFrame - buffered : m_clipStartSource;

        if (requested >= m_clipStartSource && requested < m_clipEndSource)
        {
            if (requested < lastRequestedFrame || requested > oldestBuffered)
            {
                m_buffer.reset();
                m_buffer.requestSourceFrame(requested);
                nextWriteFrame = requested;
                lastRequestedFrame = requested;
            }
            else if (requested > lastRequestedFrame)
            {
                lastRequestedFrame = requested;
            }
        }

        const auto currentBuffered = m_buffer.availableFrames();
        if (nextWriteFrame < m_clipEndSource && currentBuffered < m_capacityFrames)
        {
            const auto freeFrames = m_capacityFrames - currentBuffered;
            const auto remainingClip = static_cast<std::size_t>(m_clipEndSource - nextWriteFrame);
            const auto toRead = std::min(freeFrames, remainingClip);

            if (! m_reader->read(nextWriteFrame, m_scratchChannels, toRead))
            {
                m_buffer.reset();
                setStatus(MediaStatusState::error, "Failed to read audio frames from asset");
                return;
            }

            const auto written = m_buffer.write(nextWriteFrame, m_scratchChannels, toRead);
            nextWriteFrame += written;
        }
        else
        {
            std::this_thread::sleep_for(std::chrono::microseconds(250));
        }
    }
}

} // namespace dawg::media
