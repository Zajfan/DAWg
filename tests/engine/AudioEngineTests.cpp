#include <dawg/engine/AudioEngine.h>
#include <dawg/engine/PlaybackDescription.h>
#include <dawg/engine/PlaybackRingBuffer.h>

#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <span>

namespace {
void require(const bool condition, const char* message)
{
    if (! condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(EXIT_FAILURE);
    }
}

using dawg::engine::EngineCommandType;
using dawg::engine::PlaybackDescription;
using dawg::engine::PlaybackRingBuffer;
using dawg::engine::RenderSnapshot;

std::atomic<bool> g_trackAllocations {false};
std::atomic<std::size_t> g_allocationCount {0};

struct ScopedAllocationGuard final
{
    ScopedAllocationGuard() noexcept
    {
        g_allocationCount.store(0, std::memory_order_relaxed);
        g_trackAllocations.store(true, std::memory_order_release);
    }

    ~ScopedAllocationGuard() noexcept
    {
        g_trackAllocations.store(false, std::memory_order_release);
    }
};

void testTransportCommands()
{
    dawg::engine::AudioEngine engine;
    require(engine.prepare({48000.0, 32, 2}), "engine prepares for transport");
    std::array<float, 32> samples {};
    samples.fill(1.0f);
    std::array<float*, 1> channels {samples.data()};
    const dawg::engine::AudioBufferView output {channels.data(), 1, samples.size()};
    require(! engine.transportState().playing, "new transport is stopped");
    require(engine.transportState().samplePosition == 0, "new transport starts at zero");

    require(engine.enqueueCommand({EngineCommandType::play, 0}), "play command is accepted");
    require(! engine.transportState().playing, "enqueue does not mutate transport");
    engine.process(output, {}, {});
    require(engine.transportState().playing, "audio thread applies play command");
    require(engine.transportState().samplePosition == 32, "playing block advances transport");
    for (const auto sample : samples)
        require(sample == 0.0f, "transport processing preserves silent output");

    require(engine.enqueueCommand({EngineCommandType::seekSamples, 96000}),
            "seek command is accepted");
    require(engine.transportState().samplePosition == 32, "seek waits for processing");
    engine.process(output, {}, {});
    require(engine.transportState().samplePosition == 96032,
            "audio thread applies seek command and advances by block");
    require(engine.transportState().playing, "seek preserves playing state");

    require(engine.enqueueCommand({EngineCommandType::stop, 0}), "stop command is accepted");
    engine.process(output, {}, {});
    require(! engine.transportState().playing, "audio thread applies stop command");
    require(engine.transportState().samplePosition == 96032, "stop preserves sample position");
    engine.process(output, {}, {true, 123, 90.0});
    require(! engine.transportState().playing && engine.transportState().samplePosition == 96032,
            "empty queue and supplied snapshot do not change command-owned state");

    require(engine.enqueueCommand({EngineCommandType::seekSamples, -25}),
            "negative seek command is accepted");
    engine.process(output, {}, {});
    require(engine.transportState().samplePosition == 0, "negative seek clamps to zero");
}

void testTransportFifoAndEmptyBlocks()
{
    dawg::engine::AudioEngine engine;
    require(engine.enqueueCommand({EngineCommandType::play, 0}), "FIFO play queues");
    require(engine.enqueueCommand({EngineCommandType::seekSamples, 96000}), "FIFO seek queues");
    require(engine.enqueueCommand({EngineCommandType::seekSamples, 48000}), "second seek queues");
    require(engine.enqueueCommand({EngineCommandType::stop, 0}), "FIFO stop queues");
    engine.process({}, {}, {});
    require(! engine.transportState().playing, "last play/stop command wins in FIFO order");
    require(engine.transportState().samplePosition == 48000, "last seek wins in FIFO order");

    require(engine.enqueueCommand({EngineCommandType::stop, 0}), "second FIFO stop queues");
    require(engine.enqueueCommand({EngineCommandType::play, 0}), "second FIFO play queues");
    engine.process({nullptr, 2, 32}, {}, {});
    require(engine.transportState().playing, "commands apply before null-output early return");
    require(engine.transportState().samplePosition == 48000, "play preserves sample position");
}

void testTransportCapacityAndReuse()
{
    dawg::engine::AudioEngine engine;
    // Refill beyond one ring lap to catch lost slots and stale commands.
    for (std::int64_t batch = 0; batch < 3; ++batch)
    {
        for (std::int64_t index = 0; index < 64; ++index)
            require(engine.enqueueCommand({EngineCommandType::seekSamples, batch * 64 + index}),
                    "all 64 queue slots accept commands");
        require(! engine.enqueueCommand({EngineCommandType::seekSamples, 999999}),
                "65th command is rejected without overwriting queued commands");
        engine.process({}, {}, {});
        require(engine.transportState().samplePosition == batch * 64 + 63,
                "full queue drains in FIFO order and rejected command is absent");
    }
    require(engine.enqueueCommand({EngineCommandType::play, 0}), "queue accepts play after draining");
    engine.process({}, {}, {});
    require(engine.transportState().playing, "reused queue applies play");
    engine.process({}, {}, {});
    require(engine.transportState().samplePosition == 191, "drained commands are not replayed");
}

void testLifecycleAndValidation()
{
    dawg::engine::AudioEngine engine;
    require(! engine.isPrepared(), "new engine is unprepared");
    engine.reset();
    require(! engine.isPrepared(), "reset does not prepare an engine");
    require(engine.prepare({48000.0, 32, 2}), "engine prepares valid configuration");
    require(engine.isPrepared(), "prepared state is observable");
    engine.reset();
    require(engine.isPrepared(), "reset preserves prepared state");
    engine.release();
    require(! engine.isPrepared(), "release clears prepared state");
    engine.release();
    require(! engine.isPrepared(), "release is idempotent");

    const std::array invalid {
        dawg::engine::EngineConfig {0.0, 32, 2},
        dawg::engine::EngineConfig {-48000.0, 32, 2},
        dawg::engine::EngineConfig {48000.0, 0, 2},
        dawg::engine::EngineConfig {48000.0, 32, 0},
        dawg::engine::EngineConfig {std::numeric_limits<double>::quiet_NaN(), 32, 2},
        dawg::engine::EngineConfig {std::numeric_limits<double>::infinity(), 32, 2}
    };
    for (const auto& config : invalid)
    {
        require(engine.prepare({44100.0, 16, 1}), "engine can be prepared again");
        require(! engine.prepare(config), "invalid configuration is rejected");
        require(! engine.isPrepared(), "failed prepare leaves engine unprepared");
    }
}

void testSilentOutput()
{
    dawg::engine::AudioEngine engine;
    require(engine.prepare({48000.0, 32, 2}), "engine prepares for rendering");
    std::array<std::array<float, 32>, 2> samples {};
    for (auto& channel : samples)
        channel.fill(1.0f);
    std::array<float*, 2> channels {samples[0].data(), samples[1].data()};

    engine.process({channels.data(), channels.size(), samples[0].size()}, {}, {});
    for (const auto& channel : samples)
        for (const auto sample : channel)
            require(sample == 0.0f, "initial renderer clears output");
}

void testViewBoundsAndEmptyOutput()
{
    dawg::engine::AudioEngine engine;
    require(engine.prepare({48000.0, 32, 2}), "engine prepares for partial blocks");
    std::array<float, 5> samples {9.0f, 1.0f, 1.0f, 1.0f, 9.0f};
    std::array<float, 3> untouched {7.0f, 7.0f, 7.0f};
    std::array<float*, 3> channels {samples.data() + 1, nullptr, untouched.data()};
    engine.process({channels.data(), 2, 3}, {}, {true, 1024, 90.0});
    require(samples == std::array<float, 5> {9.0f, 0.0f, 0.0f, 0.0f, 9.0f},
            "only supplied frames are cleared, including with null channels");
    require(untouched == std::array<float, 3> {7.0f, 7.0f, 7.0f},
            "channels beyond the view remain untouched");

    engine.process({}, {}, {});
    engine.process({nullptr, 2, 32}, {}, {});
    engine.process({channels.data(), 0, 3}, {}, {});
    engine.process({channels.data(), channels.size(), 0}, {}, {});
    require(untouched[0] == 7.0f, "empty views do not write samples");

    engine.reset();
    untouched.fill(1.0f);
    engine.process({channels.data() + 2, 1, 3}, {}, {});
    require(untouched == std::array<float, 3> {}, "reset preserves silent rendering");
    engine.release();
    untouched.fill(1.0f);
    engine.process({channels.data() + 2, 1, 3}, {}, {});
    require(untouched == std::array<float, 3> {}, "unprepared output is safely silent");
}

void testPlaybackRingBuffer()
{
    PlaybackRingBuffer ring;
    require(! ring.prepare(0, 8) && ! ring.prepare(2, 0), "invalid ring dimensions are rejected");
    require(ring.prepare(2, 8), "two-channel ring prepares");
    require(ring.availableFrames() == 0, "new ring is empty");
    ring.requestSourceFrame(42);
    require(ring.requestedSourceFrame() == 42, "requested source frame round-trips");

    std::array<float, 8> leftSource {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    std::array<float, 8> rightSource {-1.0f, -2.0f, -3.0f, -4.0f, -5.0f, -6.0f, -7.0f, -8.0f};
    std::array<const float*, 2> sourceChannels {leftSource.data(), rightSource.data()};
    require(ring.write(100, sourceChannels, 8) == 8, "worker writes eight stereo frames");
    require(ring.availableFrames() == 8, "available frames match written count");
    require(ring.write(108, sourceChannels, 4) == 0, "full ring does not overwrite unread frames");
    require(ring.write(999, sourceChannels, 4) == 0, "discontinuous write is rejected");

    std::array<float, 8> leftOut {};
    std::array<float, 8> rightOut {};
    std::array<float*, 2> outChannels {leftOut.data(), rightOut.data()};
    std::array<float*, 2> nullDest {leftOut.data(), nullptr};
    require(ring.read(100, nullDest, 4) == 0, "null destination channel is rejected");
    require(ring.read(96, outChannels, 4) == 0, "read before buffered range is rejected");
    require(ring.read(108, outChannels, 4) == 0, "read after buffered range is rejected");
    require(ring.availableFrames() == 8, "rejected reads preserve buffered frames");

    require(ring.read(100, outChannels, 5) == 5, "first five frames read at exact source frame");
    require(ring.availableFrames() == 3, "three frames remain buffered");
    require(ring.read(105, outChannels, 6) == 3,
            "partial read returns bounded count when remaining data is smaller");
    require(ring.availableFrames() == 0, "all frames are consumed");

    require(ring.write(108, sourceChannels, 4) == 4, "ring wraps and accepts next contiguous frames");
    ring.reset();
    require(ring.availableFrames() == 0 && ring.requestedSourceFrame() == 0,
            "reset clears buffered samples and requested position");
    require(ring.read(108, outChannels, 4) == 0, "reset does not retain old samples");
    require(ring.write(20, sourceChannels, 8) == 8, "reset preserves prepared capacity");
    require(ring.read(20, outChannels, 8) == 8, "rewritten stereo frames read back");
    require(leftOut == leftSource && rightOut == rightSource,
            "stereo channels preserve exact sample values");
}

void testSingleClipPlayback()
{
    PlaybackRingBuffer ring;
    require(ring.prepare(1, 16), "ring prepares off the audio thread");
    std::array<float, 4> source {0.25f, 0.5f, 0.75f, 1.0f};
    std::array<const float*, 1> sourceChannels {source.data()};
    require(ring.write(10, sourceChannels, source.size()) == source.size(),
            "worker writes absolute source frames");

    dawg::engine::AudioEngine engine;
    require(engine.prepare({48000.0, 8, 2}), "engine prepares for clip rendering");
    require(engine.publishRenderSnapshot({1, 1, {&ring, 2, 4, 10, 1}}),
            "clip render snapshot publishes");
    require(engine.enqueueCommand({EngineCommandType::play, 0}), "play queues");

    std::array<std::array<float, 8>, 2> outputSamples {};
    for (auto& channel : outputSamples)
        channel.fill(9.0f);
    std::array<float*, 2> channels {outputSamples[0].data(), outputSamples[1].data()};
    const dawg::engine::AudioBufferView output {channels.data(), channels.size(),
                                                outputSamples[0].size()};
    engine.process(output, {}, {});

    const std::array<float, 8> expected {0.0f, 0.0f, 0.25f, 0.5f, 0.75f, 1.0f, 0.0f, 0.0f};
    require(outputSamples[0] == expected && outputSamples[1] == expected,
            "mono clip maps to every output channel with exact silence before and after");
    require(engine.transportState().samplePosition == 8, "transport advances by full block");
    require(engine.underrunCount() == 0, "fully buffered clip has zero underruns");
    require(ring.requestedSourceFrame() == 10, "engine requests clip source offset");

    for (auto& channel : outputSamples)
        channel.fill(3.0f);
    engine.process(output, {}, {});
    for (const auto& channel : outputSamples)
        for (const auto sample : channel)
            require(sample == 0.0f, "block after clip renders silence");
    require(engine.transportState().samplePosition == 16, "transport continues advancing");
    require(engine.underrunCount() == 0, "post-clip silence is not an underrun");

    require(engine.enqueueCommand({EngineCommandType::seekSamples, 0}), "return to start queues");
    engine.process({}, {}, {});
    require(engine.transportState().samplePosition == 0 && ring.requestedSourceFrame() == 10,
            "seeking to zero resets playback ring request to clip source offset");
}

void testStereoClipAndUnderrunTelemetry()
{
    PlaybackRingBuffer stereoRing;
    require(stereoRing.prepare(2, 8), "stereo ring prepares");
    std::array<float, 3> left {0.1f, 0.2f, 0.3f};
    std::array<float, 3> right {-0.1f, -0.2f, -0.3f};
    std::array<const float*, 2> stereoSource {left.data(), right.data()};
    require(stereoRing.write(0, stereoSource, 3) == 3, "stereo ring writes three frames");

    dawg::engine::AudioEngine engine;
    require(engine.prepare({48000.0, 4, 3}), "engine prepares for 3-channel output");
    require(engine.publishRenderSnapshot({1, 1, {&stereoRing, 0, 4, 0, 2}}),
            "stereo clip snapshot publishes");
    require(engine.enqueueCommand({EngineCommandType::play, 0}), "play queues");

    std::array<std::array<float, 4>, 3> out {};
    std::array<float*, 3> outChannels {out[0].data(), out[1].data(), out[2].data()};
    engine.process({outChannels.data(), outChannels.size(), out[0].size()}, {}, {});
    require((out[0] == std::array<float, 4> {0.1f, 0.2f, 0.3f, 0.0f}),
            "stereo channel 0 renders available frames and leaves missing frame silent");
    require((out[1] == std::array<float, 4> {-0.1f, -0.2f, -0.3f, 0.0f}),
            "stereo channel 1 renders available frames and leaves missing frame silent");
    require((out[2] == std::array<float, 4> {0.0f, 0.0f, 0.0f, 0.0f}),
            "extra output channel remains silent");
    require(engine.underrunCount() == 1 && engine.transportState().underrunCount == 1,
            "partial underrun increments underrun telemetry");
    require(engine.transportState().samplePosition == 4,
            "underrun does not change transport progression");

    require(engine.publishRenderSnapshot({2, 1, {nullptr, 4, 4, 0, 1}}),
            "null buffer snapshot publishes");
    engine.process({outChannels.data(), outChannels.size(), out[0].size()}, {}, {});
    require(engine.underrunCount() == 2 && engine.transportState().samplePosition == 8,
            "missing ring increments underrun count and advances transport");

    engine.reset();
    require(! engine.transportState().playing && engine.transportState().samplePosition == 0
                && engine.underrunCount() == 0,
            "reset clears transport position and underrun telemetry");
}

void testZeroAllocationOnAudioCallbackPath()
{
    PlaybackRingBuffer ring;
    require(ring.prepare(1, 16), "ring prepares before allocation guard");
    std::array<float, 4> source {0.25f, 0.5f, 0.75f, 1.0f};
    std::array<const float*, 1> sourceChannels {source.data()};
    require(ring.write(0, sourceChannels, source.size()) == source.size(),
            "ring fills before allocation guard");

    dawg::engine::AudioEngine engine;
    require(engine.prepare({48000.0, 4, 2}), "engine prepares before allocation guard");
    require(engine.publishRenderSnapshot({1, 1, {&ring, 0, 8, 0, 1}}),
            "snapshot publishes before allocation guard");
    require(engine.enqueueCommand({EngineCommandType::play, 0}),
            "play queues before allocation guard");

    std::array<std::array<float, 4>, 2> samples {};
    std::array<float*, 2> validChannels {samples[0].data(), samples[1].data()};
    std::array<float*, 2> nullChannel {nullptr, samples[1].data()};

    {
        ScopedAllocationGuard guard;
        engine.process({validChannels.data(), validChannels.size(), samples[0].size()}, {}, {});
        engine.process({nullChannel.data(), nullChannel.size(), samples[0].size()}, {}, {});
        engine.process({}, {}, {});
    }
    require(g_allocationCount.load(std::memory_order_acquire) == 0,
            "audio callback path performs zero heap allocations or deallocations");
}
}

void* operator new(std::size_t size)
{
    if (g_trackAllocations.load(std::memory_order_acquire))
        g_allocationCount.fetch_add(1, std::memory_order_relaxed);
    if (void* ptr = std::malloc(size == 0 ? 1 : size))
        return ptr;
    throw std::bad_alloc {};
}

void* operator new[](std::size_t size)
{
    return ::operator new(size);
}

void* operator new(std::size_t size, std::align_val_t alignment)
{
    if (g_trackAllocations.load(std::memory_order_acquire))
        g_allocationCount.fetch_add(1, std::memory_order_relaxed);
    const auto align = static_cast<std::size_t>(alignment);
    const auto rounded = ((size == 0 ? 1 : size) + align - 1) / align * align;
    if (void* ptr = std::aligned_alloc(align, rounded))
        return ptr;
    throw std::bad_alloc {};
}

void* operator new[](std::size_t size, std::align_val_t alignment)
{
    return ::operator new(size, alignment);
}

void operator delete(void* ptr) noexcept
{
    if (g_trackAllocations.load(std::memory_order_acquire))
        g_allocationCount.fetch_add(1, std::memory_order_relaxed);
    std::free(ptr);
}

void operator delete[](void* ptr) noexcept
{
    ::operator delete(ptr);
}

void operator delete(void* ptr, std::size_t) noexcept
{
    ::operator delete(ptr);
}

void operator delete[](void* ptr, std::size_t) noexcept
{
    ::operator delete(ptr);
}

void operator delete(void* ptr, std::align_val_t) noexcept
{
    ::operator delete(ptr);
}

void operator delete[](void* ptr, std::align_val_t) noexcept
{
    ::operator delete(ptr);
}

void operator delete(void* ptr, std::size_t, std::align_val_t) noexcept
{
    ::operator delete(ptr);
}

void operator delete[](void* ptr, std::size_t, std::align_val_t) noexcept
{
    ::operator delete(ptr);
}

int main()
{
    testTransportCommands();
    testTransportFifoAndEmptyBlocks();
    testTransportCapacityAndReuse();
    testLifecycleAndValidation();
    testSilentOutput();
    testViewBoundsAndEmptyOutput();
    testPlaybackRingBuffer();
    testSingleClipPlayback();
    testStereoClipAndUnderrunTelemetry();
    testZeroAllocationOnAudioCallbackPath();
}
