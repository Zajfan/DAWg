#include <dawg/media/AudioRecordService.h>

#include <chrono>

namespace dawg::media {
namespace {
class MemoryWriter final : public AudioAssetWriter
{
public:
    explicit MemoryWriter(MemoryAudioAssetWriterFactory& owner,
                          const std::uint16_t channelCount)
        : m_owner(owner)
    {
        m_owner.channels.assign(channelCount, {});
        m_owner.finalized = false;
    }

    [[nodiscard]] bool write(const std::span<const float* const> channels,
                             const std::size_t frameCount) noexcept override
    {
        if (channels.size() < m_owner.channels.size())
            return false;
        try
        {
            for (std::size_t ch = 0; ch < m_owner.channels.size(); ++ch)
            {
                if (channels[ch] == nullptr)
                    return false;
                m_owner.channels[ch].insert(
                    m_owner.channels[ch].end(),
                    channels[ch],
                    channels[ch] + frameCount);
            }
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    [[nodiscard]] bool finalize() noexcept override
    {
        m_owner.finalized = true;
        return true;
    }

private:
    MemoryAudioAssetWriterFactory& m_owner;
};
} // namespace

std::expected<std::unique_ptr<AudioAssetWriter>, MediaError>
MemoryAudioAssetWriterFactory::create(const std::string_view path,
                                      const std::uint32_t sampleRate,
                                      const std::uint16_t channelCount)
{
    if (path.empty() || sampleRate == 0 || channelCount == 0)
    {
        return std::unexpected(MediaError {
            MediaError::Code::invalidMetadata,
            "Invalid parameters for MemoryAudioAssetWriterFactory"});
    }
    return std::make_unique<MemoryWriter>(*this, channelCount);
}

AudioRecordService::AudioRecordService(AudioAssetWriterFactory& factory) noexcept
    : m_factory(factory)
{
}

AudioRecordService::~AudioRecordService()
{
    stop();
}

std::expected<void, MediaError> AudioRecordService::prepare(
    const std::string_view path,
    const std::uint32_t sampleRate,
    const std::uint16_t channelCount,
    const std::size_t ringCapacityFrames,
    const project::AudioAssetPathKind pathKind)
{
    stop();

    if (path.empty() || sampleRate == 0 || channelCount == 0 || ringCapacityFrames == 0)
    {
        return std::unexpected(MediaError {
            MediaError::Code::invalidMetadata,
            "Invalid recording parameters"});
    }

    auto created = m_factory.create(path, sampleRate, channelCount);
    if (! created.has_value() || *created == nullptr)
    {
        return std::unexpected(created.has_value()
            ? MediaError {MediaError::Code::cannotOpen, "Writer factory returned null"}
            : created.error());
    }

    if (! m_buffer.prepare(channelCount, ringCapacityFrames))
    {
        return std::unexpected(MediaError {
            MediaError::Code::unavailable,
            "Cannot prepare RecordRingBuffer"});
    }

    try
    {
        m_scratch.assign(static_cast<std::size_t>(channelCount) * ringCapacityFrames, 0.0f);
        m_scratchPlanes.resize(channelCount, nullptr);
        for (std::uint16_t ch = 0; ch < channelCount; ++ch)
            m_scratchPlanes[ch] = m_scratch.data() + ch * ringCapacityFrames;
        m_path = std::string(path);
    }
    catch (...)
    {
        return std::unexpected(MediaError {
            MediaError::Code::unavailable,
            "Cannot allocate recording scratch storage"});
    }

    m_writer = std::move(*created);
    m_pathKind = pathKind;
    m_sampleRate = sampleRate;
    m_channelCount = channelCount;
    m_capacityFrames = ringCapacityFrames;
    m_recordedFrames.store(0, std::memory_order_release);
    return {};
}

void AudioRecordService::start()
{
    if (m_running.load(std::memory_order_acquire) || ! m_writer)
        return;

    m_running.store(true, std::memory_order_release);
    m_worker = std::thread([this] { workerLoop(); });
}

void AudioRecordService::stop() noexcept
{
    m_running.store(false, std::memory_order_release);
    if (m_worker.joinable())
        m_worker.join();
    drainOnce();
}

std::expected<RecordedTake, MediaError> AudioRecordService::stopAndFinalize(
    const project::TrackId trackId,
    const std::uint64_t timelineStartSample)
{
    stop();
    if (! m_writer)
    {
        return std::unexpected(MediaError {
            MediaError::Code::unavailable,
            "No active recording writer to finalize"});
    }

    if (! m_writer->finalize())
    {
        return std::unexpected(MediaError {
            MediaError::Code::readFailure,
            "Failed to finalize recorded audio file"});
    }

    const auto totalFrames = m_recordedFrames.load(std::memory_order_acquire);
    if (totalFrames == 0)
    {
        return std::unexpected(MediaError {
            MediaError::Code::invalidMetadata,
            "Recorded take contains zero frames"});
    }

    return RecordedTake {
        .asset = project::AudioAsset {
            .id = 0,
            .path = m_path,
            .pathKind = m_pathKind,
            .sampleRate = m_sampleRate,
            .channelCount = m_channelCount,
            .frameCount = totalFrames
        },
        .clip = project::AudioClip {
            .id = 0,
            .assetId = 0,
            .startSample = timelineStartSample,
            .sourceOffset = 0,
            .lengthSamples = totalFrames,
            .trackId = trackId
        }
    };
}

std::uint64_t AudioRecordService::recordedFrames() const noexcept
{
    return m_recordedFrames.load(std::memory_order_acquire);
}

void AudioRecordService::drainOnce() noexcept
{
    if (! m_writer)
        return;

    while (m_buffer.availableFrames() > 0)
    {
        const auto readCount = m_buffer.read(m_scratchPlanes, m_capacityFrames);
        if (readCount == 0)
            break;
        const std::span<const float* const> planes {
            m_scratchPlanes.data(), m_scratchPlanes.size()};
        if (m_writer->write(planes, readCount))
            m_recordedFrames.fetch_add(readCount, std::memory_order_release);
    }
}

void AudioRecordService::workerLoop() noexcept
{
    while (m_running.load(std::memory_order_acquire))
    {
        if (m_buffer.availableFrames() > 0)
            drainOnce();
        else
            std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
}

} // namespace dawg::media
