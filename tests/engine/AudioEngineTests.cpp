#include <dawg/engine/AudioEngine.h>
#include <dawg/engine/AutomationCurveEvaluator.h>
#include <dawg/engine/ChannelStrip.h>
#include <dawg/engine/CompiledGraph.h>
#include <dawg/engine/DspGraph.h>
#include <dawg/engine/GraphHandoff.h>
#include <dawg/engine/LoopRegion.h>
#include <dawg/engine/MeterTelemetry.h>
#include <dawg/engine/Metronome.h>
#include <dawg/engine/PlaybackDescription.h>
#include <dawg/engine/PlaybackRingBuffer.h>
#include <dawg/engine/ProcessorNode.h>
#include <dawg/engine/RecordRingBuffer.h>
#include <dawg/engine/SubBlockSplitter.h>
#include <dawg/engine/TempoMap.h>
#include <dawg/engine/WorkStealingScheduler.h>
#include <dawg/engine/dsp/BrickwallLimiterNode.h>
#include <dawg/engine/dsp/DynamicsCompressorNode.h>
#include <dawg/engine/dsp/ParametricEQNode.h>
#include <dawg/engine/dsp/TnhcSynthNode.h>
#include <dawg/engine/clap/ClapPluginNode.h>
#include <dawg/engine/WsolaTimeStretcher.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
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

using dawg::engine::AudioEngine;
using dawg::engine::CompiledClip;
using dawg::engine::CompiledGraph;
using dawg::engine::CompiledTrack;
using dawg::engine::EngineCommandType;
using dawg::engine::GraphHandoff;
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

void testGraphHandoffAndZeroAllocationCompiledGraphAdoption()
{
    GraphHandoff handoff;
    require(handoff.active() == nullptr, "handoff starts with no active graph");

    auto graph1 = std::make_unique<CompiledGraph>();
    graph1->generation = 1;
    graph1->tracks.push_back(CompiledTrack {.trackId = 1});

    auto graph2 = std::make_unique<CompiledGraph>();
    graph2->generation = 2;
    graph2->tracks.push_back(CompiledTrack {.trackId = 1});
    graph2->tracks.push_back(CompiledTrack {.trackId = 2});

    // Coalescing overwrite on control thread before audio adopts
    handoff.publish(std::move(graph1));
    handoff.publish(std::move(graph2));

    const CompiledGraph* adopted {};
    {
        ScopedAllocationGuard guard;
        adopted = handoff.adopt();
    }
    require(adopted != nullptr && adopted->generation == 2 && adopted->tracks.size() == 2,
            "audio thread adopts latest compiled graph without allocating");
    require(g_allocationCount.load(std::memory_order_acquire) == 0,
            "first graph adoption performs zero allocations/deallocations on audio thread");

    auto graph3 = std::make_unique<CompiledGraph>();
    graph3->generation = 3;
    handoff.publish(std::move(graph3));

    {
        ScopedAllocationGuard guard;
        adopted = handoff.adopt();
    }
    require(adopted != nullptr && adopted->generation == 3,
            "audio thread adopts replacement graph and retires previous graph");
    require(g_allocationCount.load(std::memory_order_acquire) == 0,
            "retiring previous graph on audio thread performs zero deallocations");
    require(handoff.reclaimRetired() == 1,
            "control thread reclaims and destroys the retired graph");

    // Verify AudioEngine adopts CompiledGraph and renders its clips with zero RT allocations
    AudioEngine engine;
    require(engine.prepare({48000.0, 8, 2}), "engine prepares for compiled graph playback");

    PlaybackRingBuffer ring;
    require(ring.prepare(1, 16), "ring prepares for compiled graph");
    constexpr std::array<float, 4> pattern {0.2f, 0.4f, 0.6f, 0.8f};
    const std::array<const float*, 1> planes {pattern.data()};
    require(ring.write(0, planes, pattern.size()) == pattern.size(), "prefill ring");

    auto engineGraph = std::make_unique<CompiledGraph>();
    engineGraph->generation = 42;
    engineGraph->clips.push_back(CompiledClip {
        .clipId = 1,
        .assetId = 1,
        .buffer = &ring,
        .startSample = 1,
        .lengthSamples = pattern.size(),
        .sourceOffset = 0,
        .channelCount = 1,
        .gainLinear = 1.0f,
        .fadeInSamples = 0,
        .fadeOutSamples = 0
    });
    engineGraph->tracks.push_back(CompiledTrack {
        .trackId = 1,
        .muted = false,
        .solo = false,
        .phaseInvert = false,
        .gainLinear = 1.0f,
        .pan = 0.0f,
        .clipOffset = 0,
        .clipCount = 1
    });

    engine.publishCompiledGraph(std::move(engineGraph));
    require(engine.enqueueCommand({EngineCommandType::play, 0}), "enqueue play");

    std::array<float, 8> left {};
    std::array<float, 8> right {};
    std::array<float*, 2> out {left.data(), right.data()};

    {
        ScopedAllocationGuard guard;
        engine.process({out.data(), out.size(), left.size()}, {}, {});
    }
    require(g_allocationCount.load(std::memory_order_acquire) == 0,
            "AudioEngine::process with CompiledGraph performs zero RT allocations");
    require(engine.renderSnapshotGeneration() == 42,
            "AudioEngine reports compiled graph generation");
    require(left[0] == 0.0f && left[1] == 0.2f && left[2] == 0.4f
                && left[3] == 0.6f && left[4] == 0.8f && left[5] == 0.0f,
            "CompiledGraph clip renders at exact timeline offset");
}

void testMultiTrackChannelStripFadeRampsSubBlockSplitterAndMetering()
{
    using dawg::engine::SmoothedParameter;
    using dawg::engine::SubBlockSplitter;
    using dawg::engine::clipEnvelopeGain;
    using dawg::engine::equalPowerPanGains;

    // 1. SmoothedParameter ramp verification
    SmoothedParameter smoother {0.0f};
    smoother.setTarget(1.0f, 4);
    require(smoother.isSmoothing(), "smoother active when target differs");
    require(std::fabs(smoother.next() - 0.25f) < 1e-6f, "smooth step 1");
    require(std::fabs(smoother.next() - 0.50f) < 1e-6f, "smooth step 2");
    require(std::fabs(smoother.next() - 0.75f) < 1e-6f, "smooth step 3");
    require(std::fabs(smoother.next() - 1.00f) < 1e-6f, "smooth step 4 reaches target");
    require(! smoother.isSmoothing(), "smoother finishes after rampSamples");

    // 2. Equal-power stereo pan law
    const auto center = equalPowerPanGains(0.0f);
    require(std::fabs(center.left - 1.0f) < 1e-5f && std::fabs(center.right - 1.0f) < 1e-5f,
            "center pan has unity gain on both channels");
    const auto hardLeft = equalPowerPanGains(-1.0f);
    require(hardLeft.left > 1.4f && std::fabs(hardLeft.right) < 1e-5f,
            "hard left pan silences right channel and preserves constant power");
    const auto hardRight = equalPowerPanGains(1.0f);
    require(std::fabs(hardRight.left) < 1e-5f && hardRight.right > 1.4f,
            "hard right pan silences left channel and preserves constant power");

    // 3. Clip fade-in and fade-out envelope
    require(std::fabs(clipEnvelopeGain(0, 8, 2, 2) - 0.0f) < 1e-6f, "fade-in starts at 0");
    require(std::fabs(clipEnvelopeGain(1, 8, 2, 2) - 0.5f) < 1e-6f, "fade-in midpoint is 0.5");
    require(std::fabs(clipEnvelopeGain(2, 8, 2, 2) - 1.0f) < 1e-6f, "sustain region is 1.0");
    require(std::fabs(clipEnvelopeGain(6, 8, 2, 2) - 0.5f) < 1e-6f, "fade-out midpoint is 0.5");
    require(std::fabs(clipEnvelopeGain(7, 8, 2, 2) - 0.0f) < 1e-6f, "fade-out ends at 0");

    // 4. Sub-block event splitter (unsorted + duplicate split points, zero allocations)
    constexpr std::array<std::size_t, 5> splitPoints {12, 4, 0, 16, 4};
    std::array<std::pair<std::size_t, std::size_t>, 4> slices {};
    std::size_t sliceCount = 0;
    {
        ScopedAllocationGuard guard;
        SubBlockSplitter::split(16, splitPoints, [&](const std::size_t offset, const std::size_t frames) {
            if (sliceCount < slices.size())
                slices[sliceCount++] = {offset, frames};
        });
    }
    require(g_allocationCount.load(std::memory_order_acquire) == 0,
            "SubBlockSplitter performs zero heap allocations");
    require(sliceCount == 3, "SubBlockSplitter produces 3 contiguous sub-blocks");
    require(slices[0].first == 0 && slices[0].second == 4, "slice 0 is [0, 4)");
    require(slices[1].first == 4 && slices[1].second == 8, "slice 1 is [4, 12)");
    require(slices[2].first == 12 && slices[2].second == 4, "slice 2 is [12, 16)");

    // 5. Multi-track summing, solo-in-place, phase invert, clip fade ramps, and metering
    AudioEngine engine;
    require(engine.prepare({48000.0, 4, 2}), "engine prepares for multi-track mix");

    PlaybackRingBuffer ring1;
    PlaybackRingBuffer ring2;
    PlaybackRingBuffer ring3;
    require(ring1.prepare(1, 8) && ring2.prepare(1, 8) && ring3.prepare(1, 8), "prepare 3 rings");

    constexpr std::array<float, 4> ones {1.0f, 1.0f, 1.0f, 1.0f};
    const std::array<const float*, 1> onesPlane {ones.data()};
    require(ring1.write(0, onesPlane, 4) == 4, "write ring 1");
    require(ring2.write(0, onesPlane, 4) == 4, "write ring 2");
    require(ring3.write(0, onesPlane, 4) == 4, "write ring 3");

    auto graph = std::make_unique<CompiledGraph>();
    graph->generation = 100;
    // Track 1: soloed, gain 0.5, pan -1.0 (hard left), two overlapping/adjacent clips
    graph->clips.push_back(CompiledClip {
        .clipId = 1,
        .assetId = 1,
        .buffer = &ring1,
        .startSample = 0,
        .lengthSamples = 4,
        .sourceOffset = 0,
        .channelCount = 1,
        .gainLinear = 1.0f,
        .fadeInSamples = 0,
        .fadeOutSamples = 0
    });
    // Track 2: soloed, phase inverted, gain 0.25, center pan
    graph->clips.push_back(CompiledClip {
        .clipId = 2,
        .assetId = 2,
        .buffer = &ring2,
        .startSample = 0,
        .lengthSamples = 4,
        .sourceOffset = 0,
        .channelCount = 1,
        .gainLinear = 1.0f,
        .fadeInSamples = 0,
        .fadeOutSamples = 0
    });
    // Track 3: NOT soloed -> must be silenced by Solo-In-Place!
    graph->clips.push_back(CompiledClip {
        .clipId = 3,
        .assetId = 3,
        .buffer = &ring3,
        .startSample = 0,
        .lengthSamples = 4,
        .sourceOffset = 0,
        .channelCount = 1,
        .gainLinear = 10.0f,
        .fadeInSamples = 0,
        .fadeOutSamples = 0
    });

    graph->tracks.push_back(CompiledTrack {
        .trackId = 1,
        .muted = false,
        .solo = true,
        .phaseInvert = false,
        .gainLinear = 0.5f,
        .pan = 0.0f,
        .clipOffset = 0,
        .clipCount = 1
    });
    graph->tracks.push_back(CompiledTrack {
        .trackId = 2,
        .muted = false,
        .solo = true,
        .phaseInvert = true,
        .gainLinear = 0.25f,
        .pan = 0.0f,
        .clipOffset = 1,
        .clipCount = 1
    });
    graph->tracks.push_back(CompiledTrack {
        .trackId = 3,
        .muted = false,
        .solo = false,
        .phaseInvert = false,
        .gainLinear = 1.0f,
        .pan = 0.0f,
        .clipOffset = 2,
        .clipCount = 1
    });

    engine.publishCompiledGraph(std::move(graph));
    require(engine.enqueueCommand({EngineCommandType::play, 0}), "enqueue play");

    std::array<float, 4> left {};
    std::array<float, 4> right {};
    std::array<float*, 2> out {left.data(), right.data()};

    {
        ScopedAllocationGuard guard;
        engine.process({out.data(), out.size(), left.size()}, {}, {});
    }
    require(g_allocationCount.load(std::memory_order_acquire) == 0,
            "multi-track mix performs zero RT allocations");

    // Track 1 (+0.5) + Track 2 (-0.25) = +0.25 on both channels; Track 3 is muted by solo-in-place
    for (std::size_t i = 0; i < 4; ++i)
    {
        require(std::fabs(left[i] - 0.25f) < 1e-5f && std::fabs(right[i] - 0.25f) < 1e-5f,
                "multi-track sum applies solo-in-place, gain, and phase invert");
    }

    const auto meters = engine.meterReading();
    require(std::fabs(meters.peakLeft - 0.25f) < 1e-5f
                && std::fabs(meters.peakRight - 0.25f) < 1e-5f
                && std::fabs(meters.rmsLeft - 0.25f) < 1e-5f
                && std::fabs(meters.rmsRight - 0.25f) < 1e-5f,
            "lock-free meter telemetry reports exact master peak and RMS");
}

void testTempoMapSubBlockLoopWrappingAndMetronome()
{
    using dawg::engine::LoopRegion;
    using dawg::engine::Metronome;
    using dawg::engine::TempoEvent;
    using dawg::engine::TempoMap;
    using dawg::engine::TimeSignatureEvent;

    // 1. TempoMap conversions across tempo and time signature changes
    TempoMap tempoMap;
    // 0..4 beats (0..3840 ticks) at 120 BPM (0.5s/beat = 24000 samples/beat at 48kHz -> 96000 samples)
    // From tick 3840 (bar 2) onward: 240 BPM (0.25s/beat = 12000 samples/beat at 48kHz) in 3/4 time
    const std::array<TempoEvent, 2> tempos {{
        {.tick = 0, .bpm = 120.0},
        {.tick = 3840, .bpm = 240.0}
    }};
    const std::array<TimeSignatureEvent, 2> signatures {{
        {.tick = 0, .numerator = 4, .denominator = 4},
        {.tick = 3840, .numerator = 3, .denominator = 4}
    }};
    require(tempoMap.setEvents(tempos, signatures), "TempoMap accepts sorted tempo/signature events");

    require(tempoMap.tickToSample(960.0, 48000.0) == 24000, "1 beat at 120 BPM is 24000 samples");
    require(tempoMap.tickToSample(3840.0, 48000.0) == 96000, "bar 2 downbeat is at 96000 samples");
    require(tempoMap.tickToSample(4800.0, 48000.0) == 108000, "1 beat at 240 BPM adds 12000 samples");
    require(std::fabs(tempoMap.sampleToTick(108000, 48000.0) - 4800.0) < 1e-3,
            "sampleToTick inverts tickToSample across tempo change");

    const auto bbtBar1Beat3 = tempoMap.sampleToBarBeatTick(48000, 48000.0);
    require(bbtBar1Beat3.bar == 1 && bbtBar1Beat3.beat == 3 && bbtBar1Beat3.tick == 0,
            "48000 samples is bar 1 beat 3 tick 0");
    const auto bbtBar2Beat2 = tempoMap.sampleToBarBeatTick(108000, 48000.0);
    require(bbtBar2Beat2.bar == 2 && bbtBar2Beat2.beat == 2 && bbtBar2Beat2.tick == 0,
            "108000 samples is bar 2 beat 2 tick 0 in 3/4 time");

    // 2. Sub-block loop wrapping mid-callback
    AudioEngine engine;
    require(engine.prepare({48000.0, 8, 2}), "prepare engine for loop wrap test");

    PlaybackRingBuffer ring;
    require(ring.prepare(1, 16), "prepare ring for loop wrap");
    constexpr std::array<float, 4> ramp {0.1f, 0.2f, 0.3f, 0.4f};
    const std::array<const float*, 1> rampPlanes {ramp.data()};
    require(ring.write(0, rampPlanes, ramp.size()) == ramp.size(), "prefill loop ramp");

    auto graph = std::make_unique<CompiledGraph>();
    graph->generation = 200;
    graph->loop = LoopRegion {.enabled = true, .startSample = 0, .endSample = 4};
    graph->clips.push_back(CompiledClip {
        .clipId = 1,
        .assetId = 1,
        .buffer = &ring,
        .startSample = 0,
        .lengthSamples = 4,
        .sourceOffset = 0,
        .channelCount = 1,
        .gainLinear = 1.0f,
        .fadeInSamples = 0,
        .fadeOutSamples = 0
    });
    graph->tracks.push_back(CompiledTrack {
        .trackId = 1,
        .muted = false,
        .solo = false,
        .phaseInvert = false,
        .gainLinear = 1.0f,
        .pan = 0.0f,
        .clipOffset = 0,
        .clipCount = 1
    });

    engine.publishCompiledGraph(std::move(graph));
    require(engine.enqueueCommand({EngineCommandType::seekSamples, 2}), "seek to sample 2");
    require(engine.enqueueCommand({EngineCommandType::play, 0}), "start playback");

    std::array<float, 8> left {};
    std::array<float, 8> right {};
    std::array<float*, 2> out {left.data(), right.data()};

    {
        ScopedAllocationGuard guard;
        engine.process({out.data(), out.size(), left.size()}, {}, {});
    }
    require(g_allocationCount.load(std::memory_order_acquire) == 0,
            "loop-wrapped callback performs zero RT allocations");

    // Starting at sample 2 with loop [0, 4) over an 8-sample block:
    // Frames 0..1 play samples 2..3 (0.3, 0.4)
    // Frames 2..5 wrap and play samples 0..3 (0.1, 0.2, 0.3, 0.4)
    // Frames 6..7 wrap again and play samples 0..1 (0.1, 0.2)
    // Final transport samplePosition must be 2!
    constexpr std::array<float, 8> expectedWrapped {
        0.3f, 0.4f, 0.1f, 0.2f, 0.3f, 0.4f, 0.1f, 0.2f
    };
    for (std::size_t i = 0; i < expectedWrapped.size(); ++i)
    {
        require(std::fabs(left[i] - expectedWrapped[i]) < 1e-6f,
                "mid-block loop wrap renders exact seamless samples across loop boundary");
    }
    require(engine.transportState().samplePosition == 2,
            "transport position wraps modulo loop region");

    // 3. Metronome click synthesis at beat boundary with zero RT allocations
    Metronome metronome;
    metronome.setEnabled(true);
    metronome.setGain(0.5f);
    std::fill(left.begin(), left.end(), 0.0f);
    std::fill(right.begin(), right.end(), 0.0f);
    {
        ScopedAllocationGuard guard;
        metronome.renderSlice(
            {out.data(), out.size(), left.size()},
            0, left.size(), 0, 48000.0, tempoMap);
    }
    require(g_allocationCount.load(std::memory_order_acquire) == 0,
            "Metronome::renderSlice performs zero RT allocations");
    float clickEnergy = 0.0f;
    for (const float s : left)
        clickEnergy += std::fabs(s);
    require(clickEnergy > 0.01f, "Metronome synthesizes audible downbeat click at sample 0");
}

void testRecordRingBufferAndRealtimeInputCapture()
{
    using dawg::engine::RecordRingBuffer;

    RecordRingBuffer recordRing;
    require(recordRing.prepare(2, 8), "stereo RecordRingBuffer prepares");

    AudioEngine engine;
    require(engine.prepare({48000.0, 4, 2}), "engine prepares for input capture");
    engine.setRecordBuffer(&recordRing);
    engine.setRecording(true);
    require(engine.isRecording(), "engine reports recording armed/active");

    constexpr std::array<float, 4> inLeft {0.11f, 0.22f, 0.33f, 0.44f};
    constexpr std::array<float, 4> inRight {-0.11f, -0.22f, -0.33f, -0.44f};
    std::array<float, 4> inLeftMut = inLeft;
    std::array<float, 4> inRightMut = inRight;
    std::array<float*, 2> inChannels {inLeftMut.data(), inRightMut.data()};

    std::array<float, 4> outLeft {};
    std::array<float, 4> outRight {};
    std::array<float*, 2> outChannels {outLeft.data(), outRight.data()};

    {
        ScopedAllocationGuard guard;
        engine.process(
            {inChannels.data(), inChannels.size(), inLeftMut.size()},
            {outChannels.data(), outChannels.size(), outLeft.size()},
            {},
            {});
    }
    require(g_allocationCount.load(std::memory_order_acquire) == 0,
            "realtime input capture performs zero heap allocations");
    require(recordRing.availableFrames() == 4 && recordRing.droppedFrames() == 0,
            "RecordRingBuffer captures all 4 stereo input frames");

    std::array<float, 4> capturedL {};
    std::array<float, 4> capturedR {};
    std::array<float*, 2> capturedPlanes {capturedL.data(), capturedR.data()};
    require(recordRing.read(capturedPlanes, 4) == 4, "worker drains 4 captured frames");
    require(capturedL == inLeft && capturedR == inRight,
            "captured stereo frames match input signal bit-for-bit");
}

struct SimulatedLatencyPluginState
{
    dawg::engine::DelayCompensatorNode internalDelay;
    float gain {1.0f};
    std::uint32_t declaredLatency {0};
};

void testDspGraphCycleRejectionAutomaticPdcAndWorkStealing()
{
    using dawg::engine::BiquadFilterNode;
    using dawg::engine::DelayCompensatorNode;
    using dawg::engine::DspGraph;
    using dawg::engine::ExternalPluginCallbacks;
    using dawg::engine::ExternalPluginNode;
    using dawg::engine::GainPanNode;
    using dawg::engine::ProcessContext;
    using dawg::engine::WorkStealingScheduler;

    // 1. Cycle detection rejects feedback loop in DAG
    {
        DspGraph cyclicGraph;
        const auto a = cyclicGraph.addNode(std::make_unique<GainPanNode>(1.0f, 0.0f));
        const auto b = cyclicGraph.addNode(std::make_unique<GainPanNode>(1.0f, 0.0f));
        require(cyclicGraph.addEdge(a, b) && cyclicGraph.addEdge(b, a), "add cycle edges");
        require(! cyclicGraph.compile(48000.0, 8, 2),
                "DspGraph::compile rejects cyclic graph");
    }

    // 2. Automatic Plugin Delay Compensation (PDC) phase-cancellation proof:
    // Source signal splits into two parallel branches:
    // - Branch A: ExternalPluginNode with 3-sample internal latency and gain +1.0
    // - Branch B: Zero-latency GainPanNode with gain -1.0 (phase inverted)
    // Both branches sum into Master Bus node.
    // Without PDC, Branch B arrives 3 samples early and fails to cancel Branch A.
    // With automatic PDC, DspGraph inserts a 3-sample DelayCompensatorNode on Branch B -> Master,
    // so Branch A + Branch B sum to exact 0.0f across all samples!
    SimulatedLatencyPluginState pluginState {
        .internalDelay = DelayCompensatorNode {3},
        .gain = 1.0f,
        .declaredLatency = 3
    };
    const ExternalPluginCallbacks callbacks {
        .instance = &pluginState,
        .prepare = [](void* inst, double sr, std::size_t maxBlock, std::uint16_t ch) noexcept {
            auto* st = static_cast<SimulatedLatencyPluginState*>(inst);
            return st->internalDelay.prepare(sr, maxBlock, ch);
        },
        .reset = [](void* inst) noexcept {
            static_cast<SimulatedLatencyPluginState*>(inst)->internalDelay.reset();
        },
        .latencySamples = [](void* inst) noexcept -> std::uint32_t {
            return static_cast<SimulatedLatencyPluginState*>(inst)->declaredLatency;
        },
        .process = [](void* inst, ProcessContext& ctx) noexcept {
            auto* st = static_cast<SimulatedLatencyPluginState*>(inst);
            st->internalDelay.process(ctx);
        }
    };

    DspGraph pdcGraph;
    const auto sourceNode = pdcGraph.addNode(std::make_unique<GainPanNode>(1.0f, 0.0f), true);
    const auto latentBranchA = pdcGraph.addNode(std::make_unique<ExternalPluginNode>(callbacks));
    const auto instantBranchB = pdcGraph.addNode(std::make_unique<GainPanNode>(-1.0f, 0.0f));
    const auto masterBus = pdcGraph.addNode(std::make_unique<GainPanNode>(1.0f, 0.0f), false, true);

    require(pdcGraph.addEdge(sourceNode, latentBranchA), "edge source -> branch A");
    require(pdcGraph.addEdge(sourceNode, instantBranchB), "edge source -> branch B");
    require(pdcGraph.addEdge(latentBranchA, masterBus), "edge branch A -> master");
    require(pdcGraph.addEdge(instantBranchB, masterBus), "edge branch B -> master");

    require(pdcGraph.compile(48000.0, 8, 2), "DspGraph compiles diamond graph with PDC");
    require(pdcGraph.totalLatencySamples() == 3,
            "DspGraph reports total path latency of 3 samples");
    require(pdcGraph.wavefrontCount() == 3,
            "diamond graph compiles into 3 topological wavefronts ([source], [A, B], [master])");

    WorkStealingScheduler scheduler;
    require(scheduler.prepare(2), "WorkStealingScheduler prepares 2 worker threads");

    constexpr std::array<float, 8> impulseTrain {1.0f, 0.5f, -0.25f, 0.75f, -0.5f, 0.25f, 0.1f, -0.9f};
    std::array<float, 8> inL = impulseTrain;
    std::array<float, 8> inR = impulseTrain;
    std::array<float*, 2> inPlanes {inL.data(), inR.data()};

    std::array<float, 8> outL {};
    std::array<float, 8> outR {};
    std::array<float*, 2> outPlanes {outL.data(), outR.data()};

    {
        ScopedAllocationGuard guard;
        pdcGraph.process(
            {inPlanes.data(), inPlanes.size(), inL.size()},
            {outPlanes.data(), outPlanes.size(), outL.size()},
            &scheduler);
    }
    require(g_allocationCount.load(std::memory_order_acquire) == 0,
            "DspGraph::process with automatic PDC and WorkStealingScheduler performs zero RT allocations");

    for (std::size_t i = 0; i < 8; ++i)
    {
        require(std::fabs(outL[i]) < 1e-6f && std::fabs(outR[i]) < 1e-6f,
                "automatic PDC aligns 3-sample latent branch with 0-sample inverted branch for exact null cancellation");
    }

    // 3. Built-in BiquadFilterNode lowpass filtering verification
    BiquadFilterNode lowpass {BiquadFilterNode::Type::lowPass, 1000.0, 0.707, 0.0};
    require(lowpass.prepare(48000.0, 8, 1), "BiquadFilterNode prepares");
    std::array<float, 8> stepSignal {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float*, 1> stepPlanes {stepSignal.data()};
    ProcessContext filterCtx {
        .audio = {stepPlanes.data(), 1, stepSignal.size()},
        .midi = {},
        .sampleRate = 48000.0
    };
    {
        ScopedAllocationGuard guard;
        lowpass.process(filterCtx);
    }
    require(g_allocationCount.load(std::memory_order_acquire) == 0,
            "BiquadFilterNode::process performs zero RT allocations");
    require(stepSignal[0] > 0.0f && stepSignal[0] < stepSignal[7],
            "lowpass filter smooths step input monotonically");

    scheduler.shutdown();
}

void testStudioDspSuiteAndPdcActivation()
{
    using namespace dawg::engine::dsp;

    // 1. ParametricEQNode
    ParametricEQSettings eqSettings;
    // Band 0: High-pass at 300 Hz
    eqSettings.bands[0] = BandSettings {
        .type = FilterBandType::highPass,
        .frequencyHz = 300.0,
        .q = 0.707,
        .gainDb = 0.0,
        .enabled = true
    };
    // Bands 1-3 disabled for this test
    eqSettings.bands[1].enabled = false;
    eqSettings.bands[2].enabled = false;
    eqSettings.bands[3].enabled = false;

    ParametricEQNode eq {eqSettings};
    require(eq.prepare(48000.0, 128, 2), "ParametricEQ prepares");

    // Feed a 40 Hz low-frequency sine wave and verify attenuation
    std::array<float, 128> lowSineL {};
    std::array<float, 128> lowSineR {};
    for (std::size_t i = 0; i < 128; ++i)
    {
        const float val = std::sin(2.0f * std::numbers::pi_v<float> * 40.0f * static_cast<float>(i) / 48000.0f);
        lowSineL[i] = val;
        lowSineR[i] = val;
    }
    std::array<float*, 2> eqPlanes {lowSineL.data(), lowSineR.data()};
    dawg::engine::ProcessContext eqContext {
        .audio = dawg::engine::AudioBufferView {eqPlanes.data(), 2, 128},
        .midi = {},
        .sampleRate = 48000.0
    };

    {
        ScopedAllocationGuard guard;
        eq.process(eqContext);
        require(g_allocationCount.load() == 0, "ParametricEQ process performs zero heap allocations");
    }

    // Highpass at 300 Hz should attenuate 40 Hz by > 12 dB (amplitude < 0.25)
    float maxLowOut = 0.0f;
    for (std::size_t i = 32; i < 128; ++i)
    {
        maxLowOut = std::max(maxLowOut, std::abs(lowSineL[i]));
    }
    require(maxLowOut < 0.25f, "40 Hz sine is strongly attenuated by 300 Hz high-pass filter");

    // 2. DynamicsCompressorNode
    CompressorSettings compSettings {
        .bypassed = false,
        .thresholdDb = -12.0f, // ~0.251 amplitude
        .ratio = 4.0f,
        .attackMs = 0.5f,
        .releaseMs = 50.0f,
        .makeupGainDb = 0.0f,
        .kneeDb = 0.0f,
        .useRms = false
    };
    DynamicsCompressorNode compressor {compSettings};
    require(compressor.prepare(48000.0, 128, 2), "Compressor prepares");

    // Feed a loud 1.0 amplitude signal (0 dBFS, which is 12 dB above threshold)
    std::array<float, 128> loudL {};
    std::array<float, 128> loudR {};
    std::fill(loudL.begin(), loudL.end(), 1.0f);
    std::fill(loudR.begin(), loudR.end(), 1.0f);
    std::array<float*, 2> compPlanes {loudL.data(), loudR.data()};
    dawg::engine::ProcessContext compContext {
        .audio = dawg::engine::AudioBufferView {compPlanes.data(), 2, 128},
        .midi = {},
        .sampleRate = 48000.0
    };

    {
        ScopedAllocationGuard guard;
        compressor.process(compContext);
        require(g_allocationCount.load() == 0, "Compressor process performs zero heap allocations");
    }

    // Steady state output should be attenuated and gain reduction reported
    require(loudL.back() < 0.65f, "Loud signal is attenuated by compressor");
    require(compressor.gainReductionDb() > 3.0f, "Gain reduction telemetry reports active compression");

    // 3. BrickwallLimiterNode
    LimiterSettings limSettings {
        .bypassed = false,
        .ceilingDb = -0.1f, // ~0.988553f
        .releaseMs = 50.0f,
        .lookaheadSamples = 64
    };
    BrickwallLimiterNode limiter {limSettings};
    require(limiter.prepare(48000.0, 128, 2), "BrickwallLimiter prepares");
    require(limiter.latencySamples() == 64, "Limiter declares exact lookahead latency for PDC");

    // Feed signal with hot peaks up to 2.5 (+8 dBFS)
    std::array<float, 128> hotL {};
    std::array<float, 128> hotR {};
    for (std::size_t i = 0; i < 128; ++i)
    {
        hotL[i] = (i % 8 == 0) ? 2.5f : 0.5f;
        hotR[i] = (i % 8 == 0) ? -2.5f : -0.5f;
    }
    std::array<float*, 2> limPlanes {hotL.data(), hotR.data()};
    dawg::engine::ProcessContext limContext {
        .audio = dawg::engine::AudioBufferView {limPlanes.data(), 2, 128},
        .midi = {},
        .sampleRate = 48000.0
    };

    {
        ScopedAllocationGuard guard;
        limiter.process(limContext);
        require(g_allocationCount.load() == 0, "Limiter process performs zero heap allocations");
    }

    constexpr float maxAllowedCeiling = 0.988553f + 1e-3f;
    for (std::size_t i = 0; i < 128; ++i)
    {
        require(std::abs(hotL[i]) <= maxAllowedCeiling, "Limiter strictly enforces ceiling on left");
        require(std::abs(hotR[i]) <= maxAllowedCeiling, "Limiter strictly enforces ceiling on right");
    }

    // 4. DspGraph Integration: EQ -> Compressor -> Limiter chain
    dawg::engine::DspGraph graph;
    const auto eqNodeId = graph.addNode(std::make_unique<ParametricEQNode>(eqSettings), true, false);
    const auto compNodeId = graph.addNode(std::make_unique<DynamicsCompressorNode>(compSettings), false, false);
    const auto limNodeId = graph.addNode(std::make_unique<BrickwallLimiterNode>(limSettings), false, true);

    require(graph.addEdge(eqNodeId, compNodeId), "add EQ -> Compressor edge");
    require(graph.addEdge(compNodeId, limNodeId), "add Compressor -> Limiter edge");
    require(graph.compile(48000.0, 128, 2), "compile Studio DSP Graph");
    require(graph.totalLatencySamples() == 64, "Graph total latency matches Limiter lookahead PDC");

    std::array<float, 128> inL {};
    std::array<float, 128> inR {};
    std::fill(inL.begin(), inL.end(), 1.5f);
    std::fill(inR.begin(), inR.end(), -1.5f);
    std::array<float*, 2> inPlanes {inL.data(), inR.data()};

    std::array<float, 128> outL {};
    std::array<float, 128> outR {};
    std::array<float*, 2> outPlanes {outL.data(), outR.data()};

    graph.process(
        dawg::engine::AudioBufferView {inPlanes.data(), 2, 128},
        dawg::engine::AudioBufferView {outPlanes.data(), 2, 128});

    for (std::size_t i = 0; i < 128; ++i)
    {
        require(! std::isnan(outL[i]) && ! std::isinf(outL[i]), "DSP graph output is finite");
        require(! std::isnan(outR[i]) && ! std::isinf(outR[i]), "DSP graph output is finite");
        require(std::abs(outL[i]) <= maxAllowedCeiling, "DSP graph output stays within limiter ceiling");
    }
}

void testAudioEngineMasterLimiterActivation()
{
    AudioEngine engine;
    require(engine.prepare({48000.0, 128, 2}), "engine prepares with 128-sample blocks");
    require(! engine.isMasterLimiterEnabled(), "master limiter is disabled by default");

    engine.setMasterLimiterEnabled(true);
    require(engine.isMasterLimiterEnabled(), "master limiter can be enabled");

    // Create hot multi-track sum that would severely clip without limiter:
    // 4 tracks each playing 1.0 amplitude = 4.0 (+12 dBFS)
    auto graph = std::make_unique<CompiledGraph>();
    graph->generation = 1;

    PlaybackRingBuffer ring;
    require(ring.prepare(1, 256), "playback ring prepares");
    std::array<float, 128> ones {};
    std::fill(ones.begin(), ones.end(), 1.0f);
    const std::array<const float*, 1> planes {ones.data()};
    require(ring.write(0, planes, 128) == 128, "prefill ring");

    // 4 tracks summing together
    for (std::uint64_t t = 1; t <= 4; ++t)
    {
        graph->tracks.push_back(CompiledTrack {
            .trackId = t,
            .muted = false,
            .solo = false,
            .phaseInvert = false,
            .gainLinear = 1.0f,
            .pan = 0.0f,
            .clipOffset = static_cast<std::uint32_t>(t - 1),
            .clipCount = 1
        });
        graph->clips.push_back(CompiledClip {
            .clipId = t,
            .assetId = 1,
            .buffer = &ring,
            .startSample = 0,
            .lengthSamples = 128,
            .sourceOffset = 0,
            .channelCount = 1,
            .gainLinear = 1.0f
        });
    }

    engine.publishCompiledGraph(std::move(graph));
    require(engine.enqueueCommand({EngineCommandType::play, 0}), "play command enqueues");

    std::array<float, 128> outL {};
    std::array<float, 128> outR {};
    std::array<float*, 2> outPlanes {outL.data(), outR.data()};

    // Process two 128-sample blocks (first block fills lookahead, second block outputs limited audio)
    engine.process(dawg::engine::AudioBufferView {outPlanes.data(), 2, 128}, {}, {});
    engine.process(dawg::engine::AudioBufferView {outPlanes.data(), 2, 128}, {}, {});

    constexpr float ceiling = 0.988553f + 1e-3f;
    for (std::size_t i = 0; i < 128; ++i)
    {
        require(std::abs(outL[i]) <= ceiling, "4.0 sum is clamped strictly to ceiling on left");
        require(std::abs(outR[i]) <= ceiling, "4.0 sum is clamped strictly to ceiling on right");
    }

    const auto meter = engine.meterReading();
    require(meter.peakLeft <= ceiling && meter.peakRight <= ceiling,
            "Master meters reflect limiter-clamped true peaks");
}

void testTnhcSynthNodePolyphonyAndMidi()
{
    using namespace dawg::engine;

    TnhcSynthNode synth;
    require(synth.prepare(48000.0, 512, 2), "prepare synth 48kHz stereo");
    require(synth.latencySamples() == 0, "synth declares zero latency");
    require(synth.activeVoiceCount() == 0, "initial active voice count is zero");

    // Polyphony up to 16 voices
    for (std::uint8_t note = 60; note < 76; ++note)
        synth.noteOn(note, 0.8f);
    require(synth.activeVoiceCount() == 16, "all 16 polyphonic voices active");

    // Voice stealing on 17th note
    synth.noteOn(76, 0.9f);
    require(synth.activeVoiceCount() == 16, "voice stealing keeps voice count at max 16");

    // All notes off
    synth.allNotesOff();
    synth.reset();
    require(synth.activeVoiceCount() == 0, "reset clears all voices");

    // Sample-accurate MIDI processing via ProcessContext
    std::array<float, 64> leftBuf {};
    std::array<float, 64> rightBuf {};
    leftBuf.fill(0.0f);
    rightBuf.fill(0.0f);
    std::array<float*, 2> channels {leftBuf.data(), rightBuf.data()};

    std::array<MidiEvent, 1> midiEvents {{
        MidiEvent {
            .frameOffset = 16,
            .data = {std::byte {0x90}, std::byte {69}, std::byte {100}}, // A4 Note On
            .size = 3
        }
    }};

    ProcessContext ctx {
        .audio = AudioBufferView {
            .channels = channels.data(),
            .channelCount = 2,
            .frameCount = 64
        },
        .midi = MidiBufferView {
            .events = midiEvents.data(),
            .eventCount = 1
        },
        .sampleRate = 48000.0
    };

    {
        ScopedAllocationGuard guard;
        synth.process(ctx);
    }

    // Verify frames 0..15 are silence and frames 16..63 have audio output
    for (std::size_t f = 0; f < 16; ++f)
    {
        require(std::abs(leftBuf[f]) < 1e-6f, "audio before note-on frame offset is silent");
    }

    float peakAfter = 0.0f;
    for (std::size_t f = 16; f < 64; ++f)
    {
        peakAfter = std::max(peakAfter, std::abs(leftBuf[f]));
    }
    require(peakAfter > 0.01f, "audio after note-on frame offset contains synthesized waveform");
}

static clap_plugin_latency g_mockLatency {
    .get = [](const clap_plugin*) noexcept -> std::uint32_t { return 128; }
};

static bool g_mockClapShouldCrash = false;
static std::size_t g_mockClapReceivedEvents = 0;

static clap_plugin g_mockClapPlugin {
    .desc = nullptr,
    .plugin_data = nullptr,
    .init = [](const clap_plugin*) noexcept -> bool { return true; },
    .destroy = [](const clap_plugin*) noexcept {},
    .activate = [](const clap_plugin*, double, std::uint32_t, std::uint32_t) noexcept -> bool { return true; },
    .deactivate = [](const clap_plugin*) noexcept {},
    .start_processing = [](const clap_plugin*) noexcept -> bool { return true; },
    .stop_processing = [](const clap_plugin*) noexcept {},
    .reset = [](const clap_plugin*) noexcept {},
    .process = [](const clap_plugin*, const clap_process* proc) noexcept -> clap_process_status {
        if (g_mockClapShouldCrash)
            return CLAP_PROCESS_ERROR;

        if (proc->in_events != nullptr && proc->in_events->size != nullptr)
            g_mockClapReceivedEvents = proc->in_events->size(proc->in_events);

        for (std::uint32_t ch = 0; ch < proc->audio_outputs->channel_count; ++ch)
        {
            for (std::uint32_t f = 0; f < proc->frames_count; ++f)
                proc->audio_outputs->data32[ch][f] *= 2.0f;
        }
        return CLAP_PROCESS_CONTINUE;
    },
    .get_extension = [](const clap_plugin*, const char* id) noexcept -> const void* {
        if (std::strcmp(id, CLAP_EXT_LATENCY) == 0)
            return &g_mockLatency;
        return nullptr;
    },
    .on_main_thread = [](const clap_plugin*) noexcept {}
};

void testClapPluginNodeHostingPdcAndCrashImmunity()
{
    using namespace dawg::engine;

    g_mockClapShouldCrash = false;
    g_mockClapReceivedEvents = 0;

    ClapPluginNode node(&g_mockClapPlugin, false);
    require(node.prepare(48000.0, 512, 2), "prepare CLAP plugin node");
    require(node.latencySamples() == 128, "CLAP plugin declares 128 samples latency via CLAP_EXT_LATENCY");
    require(! node.isCrashed(), "initial crash state is false");

    // Audio and MIDI processing
    std::array<float, 32> leftBuf {};
    std::array<float, 32> rightBuf {};
    leftBuf.fill(0.25f);
    rightBuf.fill(0.25f);
    std::array<float*, 2> channels {leftBuf.data(), rightBuf.data()};

    std::array<MidiEvent, 2> midiEvents {{
        MidiEvent {.frameOffset = 4, .data = {std::byte {0x90}, std::byte {60}, std::byte {100}}, .size = 3},
        MidiEvent {.frameOffset = 12, .data = {std::byte {0x80}, std::byte {60}, std::byte {0}}, .size = 3}
    }};

    ProcessContext ctx {
        .audio = AudioBufferView {
            .channels = channels.data(),
            .channelCount = 2,
            .frameCount = 32
        },
        .midi = MidiBufferView {
            .events = midiEvents.data(),
            .eventCount = 2
        },
        .sampleRate = 48000.0
    };

    {
        ScopedAllocationGuard guard;
        node.process(ctx);
    }

    require(g_mockClapReceivedEvents == 2, "CLAP plugin received translated MIDI events");
    require(std::abs(leftBuf[0] - 0.5f) < 1e-4f, "CLAP process doubled audio samples");
    require(std::abs(rightBuf[0] - 0.5f) < 1e-4f, "CLAP process doubled right audio channel");
    require(! node.isCrashed(), "plugin executed successfully without crash");

    // Crash Immunity: test handling when plugin fails
    g_mockClapShouldCrash = true;
    node.process(ctx);
    require(node.isCrashed(), "node detected CLAP_PROCESS_ERROR and marked itself crashed");

    // Subsequent process calls when crashed output silence safely without invoking plugin
    leftBuf.fill(0.7f);
    node.process(ctx);
    require(node.isCrashed(), "remains crashed until cleared");
    node.clearCrashed();
    require(node.prepare(48000.0, 512, 2), "re-prepare resets state");
}

void testAutomationCurveEvaluatorAndBezierTension()
{
    using namespace dawg::engine;

    // Empty sequence returns default value
    require(std::abs(AutomationCurveEvaluator::evaluate({}, 100, 0.75f) - 0.75f) < 1e-4f,
            "empty points returns defaultValue");

    // Single point returns constant
    std::array<CurvePoint, 1> single {{ {.samplePosition = 500, .value = 0.6f, .curveTension = 0.0f} }};
    require(std::abs(AutomationCurveEvaluator::evaluate(single, 0) - 0.6f) < 1e-4f,
            "single point before returns point value");
    require(std::abs(AutomationCurveEvaluator::evaluate(single, 1000) - 0.6f) < 1e-4f,
            "single point after returns point value");

    // Linear ramp
    std::array<CurvePoint, 2> linear {{
        {.samplePosition = 0, .value = 0.0f, .curveTension = 0.0f},
        {.samplePosition = 1000, .value = 1.0f, .curveTension = 0.0f}
    }};
    require(std::abs(AutomationCurveEvaluator::evaluate(linear, 0) - 0.0f) < 1e-4f, "linear at 0");
    require(std::abs(AutomationCurveEvaluator::evaluate(linear, 500) - 0.5f) < 1e-4f, "linear midpoint is 0.5");
    require(std::abs(AutomationCurveEvaluator::evaluate(linear, 1000) - 1.0f) < 1e-4f, "linear at 1000");

    // Tension curves
    // Convex curve (tension > 0): starts slow, accelerates later -> midpoint is < 0.5
    std::array<CurvePoint, 2> convex {{
        {.samplePosition = 0, .value = 0.0f, .curveTension = 0.8f},
        {.samplePosition = 1000, .value = 1.0f, .curveTension = 0.0f}
    }};
    const float convexMid = AutomationCurveEvaluator::evaluate(convex, 500);
    require(convexMid < 0.45f, "convex curve midpoint is delayed below linear midpoint");

    // Concave curve (tension < 0): starts fast, decelerates later -> midpoint is > 0.5
    std::array<CurvePoint, 2> concave {{
        {.samplePosition = 0, .value = 0.0f, .curveTension = -0.8f},
        {.samplePosition = 1000, .value = 1.0f, .curveTension = 0.0f}
    }};
    const float concaveMid = AutomationCurveEvaluator::evaluate(concave, 500);
    require(concaveMid > 0.55f, "concave curve midpoint is boosted above linear midpoint");

    // Block evaluation without allocation
    std::array<float, 64> blockValues {};
    {
        ScopedAllocationGuard guard;
        AutomationCurveEvaluator::evaluateBlock(linear, 200, blockValues);
    }
    for (std::size_t i = 0; i < 64; ++i)
    {
        const float expected = static_cast<float>(200 + i) / 1000.0f;
        require(std::abs(blockValues[i] - expected) < 1e-4f, "block evaluation matches sample-accurate ramp");
    }
}

void testWsolaTimeStretchingAndPitchPreservation()
{
    using namespace dawg::engine;

    // Generate 440 Hz test sine wave at 48 kHz (duration = 4800 samples = 100 ms)
    constexpr std::size_t inFrames = 4800;
    std::vector<float> sineWave(inFrames);
    for (std::size_t i = 0; i < inFrames; ++i)
    {
        sineWave[i] = static_cast<float>(std::sin(2.0 * std::numbers::pi * 440.0 * static_cast<double>(i) / 48000.0));
    }
    std::array<std::span<const float>, 1> inputChannels {sineWave};

    // 1. Identity ratio (1.0)
    const auto identOut = WsolaTimeStretcher::stretch(inputChannels, 1.0f);
    require(identOut.size() == 1, "identity stretch returns 1 channel");
    require(identOut[0].size() == inFrames, "identity stretch returns exact same frame count");
    require(std::abs(identOut[0][100] - sineWave[100]) < 1e-5f, "identity stretch preserves waveform bit-exactly");

    // 2. Slowdown stretch (1.5x length)
    const auto slowOut = WsolaTimeStretcher::stretch(inputChannels, 1.5f);
    require(slowOut.size() == 1, "stretch returns 1 channel");
    require(slowOut[0].size() == inFrames * 3 / 2, "1.5x stretch produces 1.5x frame count (7200 frames)");

    // 3. Speedup stretch (0.5x length)
    const auto fastOut = WsolaTimeStretcher::stretch(inputChannels, 0.5f);
    require(fastOut.size() == 1, "compress returns 1 channel");
    require(fastOut[0].size() == inFrames / 2, "0.5x stretch produces 0.5x frame count (2400 frames)");
}
}

#if defined(__SANITIZE_THREAD__)
#define DAWG_TSAN_ACTIVE 1
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define DAWG_TSAN_ACTIVE 1
#endif
#endif

#if !defined(DAWG_TSAN_ACTIVE)
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
#endif

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
    testGraphHandoffAndZeroAllocationCompiledGraphAdoption();
    testMultiTrackChannelStripFadeRampsSubBlockSplitterAndMetering();
    testTempoMapSubBlockLoopWrappingAndMetronome();
    testRecordRingBufferAndRealtimeInputCapture();
    testDspGraphCycleRejectionAutomaticPdcAndWorkStealing();
    testStudioDspSuiteAndPdcActivation();
    testAudioEngineMasterLimiterActivation();
    testTnhcSynthNodePolyphonyAndMidi();
    testClapPluginNodeHostingPdcAndCrashImmunity();
    testAutomationCurveEvaluatorAndBezierTension();
    testWsolaTimeStretchingAndPitchPreservation();
}
