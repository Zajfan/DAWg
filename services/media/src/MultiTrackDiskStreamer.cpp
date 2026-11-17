#include <dawg/media/MultiTrackDiskStreamer.h>

#include <algorithm>
#include <chrono>

namespace dawg::media {

MultiTrackDiskStreamer::MultiTrackDiskStreamer(AudioAssetWriterFactory& factory) noexcept
    : m_factory(factory)
{
}

MultiTrackDiskStreamer::~MultiTrackDiskStreamer()
{
    stop();
}

std::expected<void, MediaError> MultiTrackDiskStreamer::prepareTrack(
    project::TrackId trackId,
    std::string_view filePath,
    std::uint32_t sampleRate,
    std::uint16_t channelCount,
    std::size_t ringCapacityFrames,
    std::uint64_t timelineStartSample,
    project::AudioAssetPathKind pathKind)
{
    if (m_running.load(std::memory_order_relaxed))
    {
        return std::unexpected(MediaError {
            MediaError::Code::unavailable,
            "Cannot prepare tracks while disk streamer is running"});
    }

    if (filePath.empty() || sampleRate == 0 || channelCount == 0 || ringCapacityFrames == 0)
    {
        return std::unexpected(MediaError {
            MediaError::Code::invalidMetadata,
            "Invalid parameters for track recording"});
    }

    auto writerResult = m_factory.create(filePath, sampleRate, channelCount);
    if (!writerResult.has_value() || *writerResult == nullptr)
    {
        return std::unexpected(writerResult.has_value()
            ? MediaError {MediaError::Code::cannotOpen, "Writer factory returned null"}
            : writerResult.error());
    }

    auto ctx = std::make_unique<TrackContext>();
    ctx->trackId = trackId;
    ctx->filePath = std::string(filePath);
    ctx->pathKind = pathKind;
    ctx->sampleRate = sampleRate;
    ctx->channelCount = channelCount;
    ctx->timelineStartSample = timelineStartSample;
    ctx->recordedFrames = 0;
    ctx->writer = std::move(*writerResult);

    if (!ctx->buffer.prepare(channelCount, ringCapacityFrames))
    {
        return std::unexpected(MediaError {
            MediaError::Code::unavailable,
            "Failed to allocate RecordRingBuffer for track"});
    }

    // Allocate scratch frames for draining (up to 4096 frames at a time)
    constexpr std::size_t scratchFrames = 4096;
    ctx->scratch.assign(scratchFrames * channelCount, 0.0f);
    ctx->scratchPlanes.resize(channelCount);
    for (std::size_t ch = 0; ch < channelCount; ++ch)
    {
        ctx->scratchPlanes[ch] = ctx->scratch.data() + ch * scratchFrames;
    }

    m_tracks[trackId] = std::move(ctx);
    return {};
}

void MultiTrackDiskStreamer::start()
{
    if (m_running.exchange(true))
        return;

    m_worker = std::thread(&MultiTrackDiskStreamer::workerLoop, this);
}

void MultiTrackDiskStreamer::stop() noexcept
{
    if (!m_running.exchange(false))
        return;

    if (m_worker.joinable())
    {
        m_worker.join();
    }

    drainAllOnce();
}

void MultiTrackDiskStreamer::drainTrack(TrackContext& ctx) noexcept
{
    constexpr std::size_t chunkFrames = 4096;

    while (ctx.buffer.availableFrames() > 0)
    {
        const std::size_t toRead = std::min(ctx.buffer.availableFrames(), chunkFrames);
        if (toRead == 0)
            break;

        const std::size_t readCount = ctx.buffer.read(
            std::span<float* const>(ctx.scratchPlanes.data(), ctx.channelCount),
            toRead);

        if (readCount == 0)
            break;

        // Write to audio asset writer
        const float* const* planePointers = ctx.scratchPlanes.data();
        if (ctx.writer)
        {
            (void)ctx.writer->write(
                std::span<const float* const>(planePointers, ctx.channelCount),
                readCount);
        }

        // Live waveform peak overview calculation
        {
            std::lock_guard<std::mutex> lock(ctx.overviewMutex);
            const float* ch0 = ctx.scratchPlanes[0];
            for (std::size_t i = 0; i < readCount; ++i)
            {
                const float s = ch0[i];
                if (ctx.bucketFrameCount == 0)
                {
                    ctx.currentBucketMin = s;
                    ctx.currentBucketMax = s;
                }
                else
                {
                    if (s < ctx.currentBucketMin) ctx.currentBucketMin = s;
                    if (s > ctx.currentBucketMax) ctx.currentBucketMax = s;
                }

                ctx.bucketFrameCount++;
                if (ctx.bucketFrameCount >= overviewBucketSize)
                {
                    ctx.overview.push_back(MinMaxPeak {ctx.currentBucketMin, ctx.currentBucketMax});
                    ctx.bucketFrameCount = 0;
                }
            }
        }

        ctx.recordedFrames.fetch_add(readCount, std::memory_order_relaxed);
    }
}

void MultiTrackDiskStreamer::drainAllOnce() noexcept
{
    for (auto& [id, ctx] : m_tracks)
    {
        if (ctx)
        {
            drainTrack(*ctx);
        }
    }
}

void MultiTrackDiskStreamer::workerLoop() noexcept
{
    while (m_running.load(std::memory_order_relaxed))
    {
        drainAllOnce();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    drainAllOnce();
}

engine::RecordRingBuffer* MultiTrackDiskStreamer::getTrackBuffer(project::TrackId trackId) noexcept
{
    auto it = m_tracks.find(trackId);
    if (it != m_tracks.end() && it->second)
    {
        return &it->second->buffer;
    }
    return nullptr;
}

std::vector<MinMaxPeak> MultiTrackDiskStreamer::getLiveOverview(project::TrackId trackId) const
{
    auto it = m_tracks.find(trackId);
    if (it != m_tracks.end() && it->second)
    {
        std::lock_guard<std::mutex> lock(it->second->overviewMutex);
        return it->second->overview;
    }
    return {};
}

std::uint64_t MultiTrackDiskStreamer::getTrackRecordedFrames(project::TrackId trackId) const noexcept
{
    auto it = m_tracks.find(trackId);
    if (it != m_tracks.end() && it->second)
    {
        return it->second->recordedFrames.load(std::memory_order_relaxed);
    }
    return 0;
}

std::vector<MultiTrackRecordedTake> MultiTrackDiskStreamer::finalizeAll()
{
    stop();

    std::vector<MultiTrackRecordedTake> results;
    results.reserve(m_tracks.size());

    for (auto& [id, ctx] : m_tracks)
    {
        if (!ctx)
            continue;

        if (ctx->writer)
        {
            (void)ctx->writer->finalize();
        }

        // Flush remaining bucket if needed
        {
            std::lock_guard<std::mutex> lock(ctx->overviewMutex);
            if (ctx->bucketFrameCount > 0)
            {
                ctx->overview.push_back(MinMaxPeak {ctx->currentBucketMin, ctx->currentBucketMax});
                ctx->bucketFrameCount = 0;
            }
        }

        MultiTrackRecordedTake take {};
        take.trackId = ctx->trackId;

        // Populate asset
        take.asset.id = m_nextAssetId++;
        take.asset.path = ctx->filePath;
        take.asset.pathKind = ctx->pathKind;
        take.asset.sampleRate = ctx->sampleRate;
        take.asset.channelCount = ctx->channelCount;
        take.asset.frameCount = ctx->recordedFrames.load(std::memory_order_relaxed);

        // Populate clip
        take.clip.id = m_nextClipId++;
        take.clip.assetId = take.asset.id;
        take.clip.trackId = ctx->trackId;
        take.clip.startSample = ctx->timelineStartSample;
        take.clip.sourceOffset = 0;
        take.clip.lengthSamples = take.asset.frameCount;
        take.clip.gainLinear = 1.0f;

        // Copy overview
        take.overview = ctx->overview;

        results.push_back(std::move(take));
    }

    m_tracks.clear();
    return results;
}

} // namespace dawg::media
