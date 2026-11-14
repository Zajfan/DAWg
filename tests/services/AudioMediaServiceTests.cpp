#include <dawg/media/AudioAssetReader.h>
#include <dawg/media/AudioAssetWriter.h>
#include <dawg/media/AudioMediaService.h>
#include <dawg/media/AudioRecordService.h>
#include <dawg/media/AudioResampler.h>
#include <dawg/media/AudioWarpService.h>
#include <dawg/media/DiskStreamPool.h>
#include <dawg/media/OfflineRenderService.h>
#include <dawg/media/TransientDetector.h>
#include <dawg/media/WaveformCache.h>

#include <array>
#include <chrono>
#include <cmath>
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

void testWaveformCache()
{
    FakeAudioAssetReaderFactory factory;
    // 128 samples: first 64 samples are 0.25f, except sample 10 is -0.5f and sample 20 is 0.8f.
    // next 64 samples are 0.1f, except sample 80 is -0.9f and sample 90 is 0.3f.
    std::vector<float> sampleData(128, 0.0f);
    for (std::size_t i = 0; i < 64; ++i) sampleData[i] = 0.25f;
    sampleData[10] = -0.5f;
    sampleData[20] = 0.8f;
    for (std::size_t i = 64; i < 128; ++i) sampleData[i] = 0.1f;
    sampleData[80] = -0.9f;
    sampleData[90] = 0.3f;

    factory.metadata = AudioAssetMetadata {48000, 1, 128};
    factory.samples = sampleData;

    WaveformCache cache {factory, 1};

    AudioAsset asset;
    asset.id = 42;
    asset.path = "tone.wav";
    asset.sampleRate = 48000;
    asset.channelCount = 1;
    asset.frameCount = 128;

    require(! cache.isReady(asset.id), "cache is initially not ready for asset");

    // Test synchronous generation
    cache.generateSync(asset);
    require(cache.isReady(asset.id), "cache is ready after generateSync");

    // Requesting 2 pixels over all 128 samples:
    // Pixel 0 corresponds to [0, 64) -> min: -0.5f, max: 0.8f
    // Pixel 1 corresponds to [64, 128) -> min: -0.9f, max: 0.3f
    const auto peaks2px = cache.getMinMaxPeaks(asset.id, 0, 0, 128, 2);
    require(peaks2px.size() == 2, "2 pixels requested returns 2 peaks");
    require(std::abs(peaks2px[0].minVal - (-0.5f)) < 1e-4f, "pixel 0 min matches");
    require(std::abs(peaks2px[0].maxVal - 0.8f) < 1e-4f, "pixel 0 max matches");
    require(std::abs(peaks2px[1].minVal - (-0.9f)) < 1e-4f, "pixel 1 min matches");
    require(std::abs(peaks2px[1].maxVal - 0.3f) < 1e-4f, "pixel 1 max matches");

    // Requesting 1 pixel over all 128 samples -> min: -0.9f, max: 0.8f
    const auto peaks1px = cache.getMinMaxPeaks(asset.id, 0, 0, 128, 1);
    require(peaks1px.size() == 1, "1 pixel requested returns 1 peak");
    require(std::abs(peaks1px[0].minVal - (-0.9f)) < 1e-4f, "aggregated 1px min matches");
    require(std::abs(peaks1px[0].maxVal - 0.8f) < 1e-4f, "aggregated 1px max matches");

    // Test clear
    cache.clear();
    require(! cache.isReady(asset.id), "cache is not ready after clear");

    // Test asynchronous background generation
    cache.requestThumbnail(asset);
    require(waitFor([&] { return cache.isReady(asset.id); }),
            "cache becomes ready asynchronously via worker thread");

    const auto asyncPeaks = cache.getMinMaxPeaks(asset.id, 0, 0, 128, 2);
    require(asyncPeaks.size() == 2, "async thumbnail peaks ready");
    require(std::abs(asyncPeaks[0].minVal - (-0.5f)) < 1e-4f, "async pixel 0 min matches");
    require(std::abs(asyncPeaks[1].minVal - (-0.9f)) < 1e-4f, "async pixel 1 min matches");

    // Edge cases
    require(cache.getMinMaxPeaks(999, 0, 0, 128, 2).empty(), "unknown asset returns empty");
    require(cache.getMinMaxPeaks(asset.id, 5, 0, 128, 2).empty(), "invalid channel returns empty");
    require(cache.getMinMaxPeaks(asset.id, 0, 0, 128, 0).empty(), "0 target pixels returns empty");
}

void testOfflineRenderServiceMasterBounceStemsAndDither()
{
    // Test TPDF Dither
    {
        std::vector<float> buffer(1024, 0.05f);
        std::uint32_t rng = 12345;
        OfflineRenderService::applyTpdfDither(buffer.data(), buffer.size(), rng);

        // Verify all samples are quantized to 16-bit float multiples (k / 32767.0f)
        for (float val : buffer)
        {
            const float scaled = val * 32767.0f;
            const float rounded = std::round(scaled);
            require(std::abs(scaled - rounded) < 1e-4f, "sample is quantized to 16-bit int grid");
        }
    }

    // Test Master Bounce with instrument track and MIDI clip
    {
        MemoryAudioAssetWriterFactory writerFactory;
        OfflineRenderService renderer(writerFactory);

        ProjectSnapshot project;
        project.id = 1;
        project.name = "Offline Synth Render";
        project.tracks.push_back({
            .id = 1,
            .name = "Synth",
            .muted = false,
            .solo = false,
            .gainLinear = 1.0f,
            .pan = 0.0f,
            .phaseInvert = false,
            .recordArmed = false,
            .kind = TrackKind::instrument
        });

        project.midiClips.push_back({
            .id = 10,
            .trackId = 1,
            .startSample = 0,
            .lengthSamples = 2048,
            .notes = {
                {
                    .id = 100,
                    .pitch = 69, // A4
                    .velocity = 100,
                    .startSample = 0,
                    .lengthSamples = 1024
                }
            }
        });

        RenderOptions options {
            .destinationPath = "output.wav",
            .startSample = 0,
            .endSample = 2048,
            .sampleRate = 48000,
            .bitDepth = ExportBitDepth::int16,
            .applyTpdfDither = true,
            .exportStems = false
        };

        bool progressReceived = false;
        auto result = renderer.render(project, options, nullptr, [&](const RenderProgress& p) {
            if (p.fraction > 0.0f)
                progressReceived = true;
        });

        require(result.has_value(), "offline render succeeded");
        require(writerFactory.finalized, "writer was finalized");
        require(writerFactory.channels.size() == 2, "writer received stereo channels");
        require(writerFactory.channels[0].size() == 2048, "rendered exactly 2048 frames");
        require(progressReceived, "progress callbacks fired");

        // Verify synthesized audio is non-silent
        float maxVal = 0.0f;
        for (float s : writerFactory.channels[0])
            maxVal = std::max(maxVal, std::abs(s));
        require(maxVal > 0.01f, "synthesized output contains audible audio");
    }

    // Test Stem Export
    {
        MemoryAudioAssetWriterFactory writerFactory;
        OfflineRenderService renderer(writerFactory);

        ProjectSnapshot project;
        project.id = 2;
        project.name = "Stems Project";
        project.tracks.push_back({
            .id = 1,
            .name = "Track A",
            .muted = false,
            .solo = false,
            .gainLinear = 1.0f,
            .pan = -0.5f,
            .phaseInvert = false,
            .recordArmed = false,
            .kind = TrackKind::instrument
        });
        project.tracks.push_back({
            .id = 2,
            .name = "Track B",
            .muted = false,
            .solo = false,
            .gainLinear = 0.8f,
            .pan = 0.5f,
            .phaseInvert = false,
            .recordArmed = false,
            .kind = TrackKind::instrument
        });

        RenderOptions stemOptions {
            .destinationPath = "/tmp/mix.wav",
            .startSample = 0,
            .endSample = 1024,
            .sampleRate = 48000,
            .bitDepth = ExportBitDepth::float32,
            .applyTpdfDither = false,
            .exportStems = true,
            .stemTrackIds = {1} // Only track 1
        };

        auto stemResult = renderer.render(project, stemOptions);
        require(stemResult.has_value(), "stem render succeeded");
    }

    // Test Cancellation
    {
        MemoryAudioAssetWriterFactory writerFactory;
        OfflineRenderService renderer(writerFactory);

        ProjectSnapshot project;
        project.id = 3;
        project.name = "Cancel Project";

        RenderOptions options {
            .destinationPath = "cancel.wav",
            .startSample = 0,
            .endSample = 96000,
            .sampleRate = 48000
        };

        std::atomic<bool> cancelToken {true};
        auto cancelResult = renderer.render(project, options, &cancelToken);
        require(! cancelResult.has_value(), "render was cancelled");
        require(cancelResult.error().code == MediaError::Code::unavailable, "error code is unavailable");
    }
}

void testTransientDetectionOnPercussiveBursts()
{
    // Generate 1 second of silence with 2 sharp impulses at sample 4800 and 24000
    constexpr std::size_t numSamples = 48000;
    std::vector<float> audio(numSamples, 0.0f);

    // Burst 1 at sample 4800 (100 ms)
    for (std::size_t i = 0; i < 256; ++i)
        audio[4800 + i] = (i % 2 == 0 ? 0.9f : -0.9f) * (1.0f - static_cast<float>(i) / 256.0f);

    // Burst 2 at sample 24000 (500 ms)
    for (std::size_t i = 0; i < 256; ++i)
        audio[24000 + i] = (i % 2 == 0 ? 0.85f : -0.85f) * (1.0f - static_cast<float>(i) / 256.0f);

    const auto transients = TransientDetector::detect(audio, 48000.0, 1.2f);
    require(transients.size() >= 2, "detected both percussive bursts");

    // Check proximity to bursts
    bool foundBurst1 = false;
    bool foundBurst2 = false;
    for (const auto& marker : transients)
    {
        if (marker.sampleOffset >= 4600 && marker.sampleOffset <= 5000)
            foundBurst1 = true;
        if (marker.sampleOffset >= 23800 && marker.sampleOffset <= 24200)
            foundBurst2 = true;
    }
    require(foundBurst1, "detected burst 1 near sample 4800");
    require(foundBurst2, "detected burst 2 near sample 24000");
}

void testAudioWarpServiceBeatMatching()
{
    // Generate synthetic 120 BPM audio with beat transients at 0, 24000, 48000, 72000
    constexpr std::size_t numSamples = 96000;
    std::vector<float> audio(numSamples, 0.0f);
    for (std::size_t b = 0; b < 4; ++b)
    {
        const std::size_t offset = b * 24000;
        for (std::size_t i = 0; i < 256; ++i)
            audio[offset + i] = (i % 2 == 0 ? 0.9f : -0.9f) * (1.0f - static_cast<float>(i) / 256.0f);
    }

    // Auto-detect warp markers at 120 BPM
    const auto markers = AudioWarpService::autoDetectBeatWarpMarkers(audio, 48000.0, 120.0, 1.2f);
    require(! markers.empty(), "autoDetectBeatWarpMarkers finds markers");
    require(markers[0].sourceSample == 0 && markers[0].targetSample == 0, "first marker starts at zero");

    // Warping render with modified target positions (e.g. accelerating second half)
    std::vector<WarpMarker> customMarkers = {
        {.id = 1, .sourceSample = 0, .targetSample = 0},
        {.id = 2, .sourceSample = 48000, .targetSample = 48000},
        {.id = 3, .sourceSample = 96000, .targetSample = 72000} // Compress second half to 0.5x
    };

    std::array<std::span<const float>, 1> inChans {audio};
    const auto warped = AudioWarpService::renderWarpedAudio(inChans, customMarkers, numSamples);
    require(warped.size() == 1, "warped audio has 1 channel");
    require(warped[0].size() > 0, "warped audio contains samples");
    require(warped[0].size() < numSamples, "compressing second half reduces total sample length");
}
} // namespace

int main()
{
    testInspectPrepareAndWorkerFill();
    testSourceOffsetBoundedCapacityAndSeekJump();
    testOpenAndReadFailures();
    testAudioResamplerDiskStreamPoolAndRecordService();
    testWaveformCache();
    testOfflineRenderServiceMasterBounceStemsAndDither();
    testTransientDetectionOnPercussiveBursts();
    testAudioWarpServiceBeatMatching();
    std::puts("PASS: 8 media service, resampler, disk pool, record, waveform cache, offline render, transient detection, and warp tests");
}
