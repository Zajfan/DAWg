#include <dawg/media/AudioAssetReader.h>
#include <dawg/media/AudioMediaService.h>

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
        return std::make_unique<FakeAudioAssetReader>(metadata, samples, failReads);
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
} // namespace

int main()
{
    testInspectPrepareAndWorkerFill();
    testSourceOffsetBoundedCapacityAndSeekJump();
    testOpenAndReadFailures();
    std::puts("PASS: 3 media service behavior tests");
}
