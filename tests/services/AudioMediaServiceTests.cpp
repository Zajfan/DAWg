#include <dawg/media/AudioAssetReader.h>
#include <dawg/media/AudioAssetWriter.h>
#include <dawg/media/AudioMediaService.h>
#include <dawg/media/AudioRecordService.h>
#include <dawg/media/AudioResampler.h>
#include <dawg/media/DiskStreamPool.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <thread>
#include <vector>

namespace {
using namespace dawg::media;
using namespace dawg::project;

void require(const bool condition, const char* message)
{
    if (! condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(EXIT_FAILURE);
    }
}

class FakeAudioAssetReader final : public AudioAssetReader
{
public:
    FakeAudioAssetReader(AudioAssetMetadata meta, std::vector<float> samples,
                         const bool failReads = false)
        : m_metadata {meta}, m_samples {std::move(samples)}, m_failReads {failReads}
    {
    }

    [[nodiscard]] AudioAssetMetadata metadata() const noexcept override
    {
        return m_metadata;
    }

    [[nodiscard]] bool read(const std::uint64_t sourceFrame,
                            const std::span<float* const> destination,
                            const std::size_t frameCount) noexcept override
    {
        if (m_failReads || destination.size() < m_metadata.channelCount)
            return false;
        if (sourceFrame > m_metadata.frameCount
            || frameCount > m_metadata.frameCount - sourceFrame)
            return false;

        for (std::size_t ch = 0; ch < m_metadata.channelCount; ++ch)
        {
            if (destination[ch] == nullptr)
                return false;
            for (std::size_t i = 0; i < frameCount; ++i)
            {
                const auto index = static_cast<std::size_t>(sourceFrame + i);
                destination[ch][i] = m_samples[index * m_metadata.channelCount + ch];
            }
        }
        return true;
    }

private:
    AudioAssetMetadata m_metadata {};
    std::vector<float> m_samples;
    bool m_failReads {};
};

class FakeAudioAssetReaderFactory final : public AudioAssetReaderFactory
{
public:
    AudioAssetMetadata metadata {48000, 1, 8};
    std::vector<float> samples {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f};
    bool failOpen {false};
    bool failReads {false};
    MediaError::Code openErrorCode {MediaError::Code::cannotOpen};

    [[nodiscard]] std::expected<std::unique_ptr<AudioAssetReader>, MediaError>
    open(const std::string_view path) override
    {
        if (path.empty())
            return std::unexpected(MediaError {MediaError::Code::invalidPath, "empty path"});
        if (failOpen || path == "missing.wav")
            return std::unexpected(MediaError {openErrorCode, "cannot open asset"});
        auto meta = metadata;
        if (path == "voice3_24k.wav")
            meta.sampleRate = 24000;
        return std::make_unique<FakeAudioAssetReader>(meta, samples, failReads);
    }
};

template <typename Predicate>
bool waitFor(Predicate&& predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (! predicate())
    {
        if (std::chrono::steady_clock::now() >= deadline)
            return false;
        std::this_thread::yield();
    }
    return true;
}

void testInspectPrepareAndWorkerFill()
{
    FakeAudioAssetReaderFactory factory;
    AudioMediaService service {factory};

    const auto inspected = service.inspect("tone.wav");
    require(inspected.has_value() && inspected->sampleRate == 48000
                && inspected->channelCount == 1 && inspected->frameCount == 8,
            "inspection returns WAV metadata");

    auto asset = *inspected;
    asset.id = 1;
    require(service.prepare(asset, {1, asset.id, 0, 0, 8}, 16).has_value(),
            "service prepares a worker session");
    require(service.status().available(), "prepared service reports ready status");

    service.start();
    require(waitFor([&] { return service.buffer().availableFrames() == 8; }),
            "worker fills the bounded ring");

    std::array<float, 8> out {};
    std::array<float*, 1> channels {out.data()};
    require(service.buffer().read(0, channels, 8) == 8, "audio reads all eight frames");
    require((out == std::array<float, 8> {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f}),
            "buffered frames match source PCM");

    service.stop();
    service.start();
    require(waitFor([&] { return service.buffer().availableFrames() == 8; }),
            "worker stop and restart refills ring from clip start");
    service.stop();
}

void testSourceOffsetBoundedCapacityAndSeekJump()
{
    FakeAudioAssetReaderFactory factory;
    AudioMediaService service {factory};
    const AudioAsset asset {1, "tone.wav", AudioAssetPathKind::projectRelative, 48000, 1, 8};

    require(service.prepare(asset, {1, asset.id, 0, 2, 5}, 4).has_value(),
            "prepare with source offset 2, length 5, capacity 4 succeeds");
    service.start();
    require(waitFor([&] { return service.buffer().availableFrames() == 4; }),
            "worker fills up to bounded ring capacity");

    std::array<float, 2> firstTwo {};
    std::array<float*, 1> channels {firstTwo.data()};
    require(service.buffer().read(2, channels, 2) == 2,
            "audio reads two frames at source offset 2");
    require((firstTwo == std::array<float, 2> {0.3f, 0.4f}),
            "source offset frames match expected PCM");

    require(waitFor([&] { return service.buffer().availableFrames() == 3; }),
            "worker refills remaining clip frame after partial consumption");

    service.buffer().requestSourceFrame(5);
    std::array<float, 2> jumped {};
    std::array<float*, 1> jumpChannels {jumped.data()};
    require(waitFor([&] {
        return service.buffer().availableFrames() == 2
            && service.buffer().read(5, jumpChannels, 2) == 2;
    }), "worker resets and refills from jumped source frame");
    require((jumped == std::array<float, 2> {0.6f, 0.7f}),
            "jumped frames match source offset 5..6");
    service.stop();
}

void testOpenAndReadFailures()
{
    FakeAudioAssetReaderFactory factory;
    AudioMediaService service {factory};

    require(! service.inspect("").has_value(), "empty path fails inspection");
    require(! service.inspect("missing.wav").has_value(), "missing file fails inspection");

    const AudioAsset missingAsset {
        1, "missing.wav", AudioAssetPathKind::projectRelative, 48000, 1, 8};
    const auto missingResult = service.prepare(missingAsset, {1, 1, 0, 0, 8}, 8);
    require(! missingResult.has_value(), "missing asset fails prepare");
    require(service.status().state == MediaStatusState::unavailable
                && ! service.status().available(),
            "missing asset sets unavailable status");
    require(service.buffer().availableFrames() == 0,
            "unavailable asset leaves playback ring silent and empty");

    factory.failReads = true;
    const AudioAsset readableAsset {
        2, "tone.wav", AudioAssetPathKind::projectRelative, 48000, 1, 8};
    require(service.prepare(readableAsset, {1, 2, 0, 0, 8}, 8).has_value(),
            "prepare succeeds when header opens");
    service.start();
    require(waitFor([&] { return service.status().state == MediaStatusState::error; }),
            "worker read failure transitions status to error");
    require(! service.status().errorMessage.empty()
                && service.buffer().availableFrames() == 0,
            "read failure records diagnostic and writes no corrupt frames");
    service.stop();
}

void testAudioResamplerDiskStreamPoolAndRecordService()
{
    using dawg::media::AudioRecordService;
    using dawg::media::AudioResampler;
    using dawg::media::DiskStreamPool;
    using dawg::media::MemoryAudioAssetWriterFactory;

    // 1. Streaming AudioResampler (24kHz -> 48kHz 2x upsampling and 96kHz -> 48kHz 0.5x downsampling)
    AudioResampler upsampler;
    require(upsampler.prepare(24000, 48000, 1, 32), "prepare 24kHz -> 48kHz upsampler");
    constexpr std::array<float, 8> srcRamp {0.0f, 0.2f, 0.4f, 0.6f, 0.8f, 1.0f, 1.0f, 1.0f};
    const std::array<const float*, 1> srcPlanes {srcRamp.data()};
    std::array<float, 16> dstUpsampled {};
    std::array<float*, 1> dstPlanes {dstUpsampled.data()};
    std::size_t consumed = 0;
    const auto produced = upsampler.process(srcPlanes, srcRamp.size(), dstPlanes, dstUpsampled.size(), consumed);
    require(consumed == srcRamp.size() && produced >= 14,
            "24kHz -> 48kHz resampler consumes input and produces ~2x output frames");

    // 2. Multiplexed DiskStreamPool streaming 3 voices concurrently (including a resampled voice)
    FakeAudioAssetReaderFactory factory;
    DiskStreamPool pool {factory, 2};

    const AudioAsset asset1 {1, "voice1.wav", AudioAssetPathKind::projectRelative, 48000, 1, 8};
    const AudioAsset asset2 {2, "voice2.wav", AudioAssetPathKind::projectRelative, 48000, 1, 8};
    const AudioAsset asset3 {3, "voice3_24k.wav", AudioAssetPathKind::projectRelative, 24000, 1, 8};

    const auto v1 = pool.addVoice(asset1, {1, 1, 0, 0, 8}, 16, 48000);
    const auto v2 = pool.addVoice(asset2, {2, 2, 0, 2, 6}, 16, 48000);
    const auto v3 = pool.addVoice(asset3, {3, 3, 0, 0, 8}, 32, 48000);
    require(v1.has_value() && v2.has_value() && v3.has_value(),
            "DiskStreamPool registers three concurrent voice streams");

    pool.start();
    require(waitFor([&] {
        auto* b1 = pool.buffer(*v1);
        auto* b2 = pool.buffer(*v2);
        auto* b3 = pool.buffer(*v3);
        return b1 != nullptr && b2 != nullptr && b3 != nullptr
            && b1->availableFrames() == 8
            && b2->availableFrames() == 6
            && b3->availableFrames() >= 14;
    }), "DiskStreamPool workers fill all three voice ring buffers including resampled voice");
    pool.stop();

    // 3. AudioRecordService background writer draining RecordRingBuffer
    MemoryAudioAssetWriterFactory writerFactory;
    AudioRecordService recordService {writerFactory};
    require(recordService.prepare("recorded_take.wav", 48000, 2, 32).has_value(),
            "AudioRecordService prepares recording session");
    recordService.start();

    constexpr std::array<float, 4> recL {0.5f, 0.25f, -0.25f, -0.5f};
    constexpr std::array<float, 4> recR {-0.5f, -0.25f, 0.25f, 0.5f};
    const std::array<const float*, 2> recPlanes {recL.data(), recR.data()};
    require(recordService.buffer().write(recPlanes, 4) == 4,
            "audio thread writes 4 stereo frames to record service ring");

    require(waitFor([&] { return recordService.recordedFrames() == 4; }),
            "AudioRecordService worker drains and writes all 4 frames");
    const auto finalizedAsset = recordService.stopAndFinalize(7, 16);
    require(finalizedAsset.has_value(), "stopAndFinalize returns finalized asset/clip pair");
    require(finalizedAsset->asset.sampleRate == 48000
                && finalizedAsset->asset.channelCount == 2
                && finalizedAsset->asset.frameCount == 4,
            "finalized recorded AudioAsset has exact metadata");
    require(finalizedAsset->clip.startSample == 16 && finalizedAsset->clip.lengthSamples == 4,
            "finalized recorded AudioClip is placed at timeline start sample");
    require(writerFactory.finalized && writerFactory.channels.size() == 2
                && writerFactory.channels[0].size() == 4,
            "writer factory received and finalized all recorded stereo samples");
}
} // namespace

int main()
{
    testInspectPrepareAndWorkerFill();
    testSourceOffsetBoundedCapacityAndSeekJump();
    testOpenAndReadFailures();
    testAudioResamplerDiskStreamPoolAndRecordService();
    std::puts("PASS: 4 media service, resampler, disk pool, and recording tests");
}
