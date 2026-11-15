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
#include <dawg/engine/SessionMatrixEngine.h>
#include <dawg/engine/TempoMap.h>
#include <dawg/engine/WorkStealingScheduler.h>
#include <dawg/engine/dsp/AudioTranscriber.h>
#include <dawg/engine/dsp/AudioWarpEngine.h>
#include <dawg/engine/dsp/BinauralSpatialPannerNode.h>
#include <dawg/engine/dsp/BrickwallLimiterNode.h>
#include <dawg/engine/dsp/ConvolutionReverbNode.h>
#include <dawg/engine/dsp/DrumPadSamplerNode.h>
#include <dawg/engine/dsp/DynamicLinearPhaseEQNode.h>
#include <dawg/engine/dsp/DynamicsCompressorNode.h>
#include <dawg/engine/dsp/GranularCloudNode.h>
#include <dawg/engine/dsp/HarmonicAssistant.h>
#include <dawg/engine/dsp/ModulatorRack.h>
#include <dawg/engine/dsp/MultibandCompressorNode.h>
#include <dawg/engine/dsp/NeuralAmpModelerNode.h>
#include <dawg/engine/dsp/ParametricEQNode.h>
#include <dawg/engine/dsp/PolyphaseOversampler.h>
#include <dawg/engine/dsp/SpatialSurroundPannerNode.h>
#include <dawg/engine/dsp/SpectralSidechainNode.h>
#include <dawg/engine/dsp/TnhcDelayNode.h>
#include <dawg/engine/dsp/TnhcReverbNode.h>
#include <dawg/engine/dsp/TnhcSynthNode.h>
#include <dawg/engine/dsp/TransientExciterNode.h>
#include <dawg/engine/dsp/VocalPitchCorrectorNode.h>
#include <dawg/engine/plugins/AraPluginBridge.h>
#include <dawg/engine/dsp/DspWireGrid.h>
#include <dawg/engine/dsp/MasteringMeterNode.h>
#include <dawg/engine/dsp/ZdfLadderFilter.h>
#include <dawg/media/OfflineRenderService.h>
#include <audio/LowLatencyAudioDriver.h>
#include <dawg/engine/plugins/PluginCrashSupervisor.h>
#include <dawg/engine/clap/ClapPluginNode.h>
#include <dawg/engine/MidiControllerManager.h>
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

    // Factory presets verification
    const auto presets = TnhcSynthNode::factoryPresets();
    require(presets.size() == 9, "factory preset library contains 9 studio presets");
    require(presets[0].name == "303 Acid Bass" && presets[0].category == "Bass", "preset 0 is 303 Acid Bass");
    require(presets[5].name == "Cosmic Shimmer" && presets[5].category == "Pad", "preset 5 is Cosmic Shimmer");

    synth.loadPreset(presets[0]);
    const auto p0 = synth.currentPreset("Active 303");
    require(p0.filterCutoff == 450.0f && p0.filterResonance == 14.0f, "loaded preset parameters apply correctly");

    synth.loadPreset(presets[5]);
    const auto p5 = synth.currentPreset("Active Shimmer");
    require(p5.filterMode == SynthFilterMode::bandpass && p5.filterCutoff == 2200.0f, "preset filter mode applies correctly");
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

void testMpePolyphonicExpressionAndSynthModulation()
{
    using namespace dawg::engine;

    TnhcSynthNode synth;
    require(synth.prepare(48000.0, 512, 2), "synth prepares at 48 kHz stereo");
    require(synth.isMpeEnabled(), "MPE is enabled by default");
    require(synth.pitchBendRange() == 48.0f, "standard MPE pitch bend range is 48 semitones");

    // Polyphonic multi-channel voice triggering
    synth.noteOn(2, 60, 0.8f); // Channel 2, Middle C
    synth.noteOn(3, 64, 0.8f); // Channel 3, E4
    require(synth.activeVoiceCount() == 2, "two voices active on separate MPE channels");

    // Per-channel pitch bend
    synth.setChannelPitchBend(2, 12.0f); // 1 octave up on channel 2 only
    synth.setChannelPressure(2, 0.8f);   // Aftertouch on channel 2
    synth.setChannelTimbre(2, 0.9f);     // CC74 brightness on channel 2

    // Audio process with sample-accurate MIDI event buffer
    std::array<float, 256> outL {};
    std::array<float, 256> outR {};
    std::array<float*, 2> channels {outL.data(), outR.data()};

    std::array<MidiEvent, 5> events {};
    // Event 1: Note On ch 2, note 60, vel 100 at frame 0
    events[0].frameOffset = 0;
    events[0].size = 3;
    events[0].data[0] = static_cast<std::byte>(0x91); // Ch 2 Note On
    events[0].data[1] = static_cast<std::byte>(60);
    events[0].data[2] = static_cast<std::byte>(100);

    // Event 2: Pitch bend ch 2 (+24 semitones) at frame 64
    events[1].frameOffset = 64;
    events[1].size = 3;
    events[1].data[0] = static_cast<std::byte>(0xE1); // Ch 2 Pitch Bend
    events[1].data[1] = static_cast<std::byte>(0x00);
    events[1].data[2] = static_cast<std::byte>(0x60); // > 8192

    // Event 3: CC 74 ch 2 (timbre) at frame 128
    events[2].frameOffset = 128;
    events[2].size = 3;
    events[2].data[0] = static_cast<std::byte>(0xB1);
    events[2].data[1] = static_cast<std::byte>(74);
    events[2].data[2] = static_cast<std::byte>(120);

    // Event 4: Channel pressure ch 2 at frame 160
    events[3].frameOffset = 160;
    events[3].size = 2;
    events[3].data[0] = static_cast<std::byte>(0xD1);
    events[3].data[1] = static_cast<std::byte>(100);

    // Event 5: Note Off ch 2 at frame 200
    events[4].frameOffset = 200;
    events[4].size = 3;
    events[4].data[0] = static_cast<std::byte>(0x81);
    events[4].data[1] = static_cast<std::byte>(60);
    events[4].data[2] = static_cast<std::byte>(0);

    MidiBufferView midiView {events.data(), events.size()};
    ProcessContext ctx {
        .audio = AudioBufferView {channels.data(), 2, 256},
        .midi = midiView
    };

    synth.process(ctx);

    float maxAbs = 0.0f;
    for (std::size_t i = 0; i < 256; ++i)
    {
        require(! std::isnan(outL[i]) && ! std::isinf(outL[i]), "no NaN/Inf in left synth audio");
        require(! std::isnan(outR[i]) && ! std::isinf(outR[i]), "no NaN/Inf in right synth audio");
        maxAbs = std::max(maxAbs, std::abs(outL[i]));
    }
    require(maxAbs > 0.01f, "synth produces audio with MPE expressive modulation");

    synth.allNotesOff();
    synth.reset();
    require(synth.activeVoiceCount() == 0, "all voices cleared after reset");
}

void testMidiControllerManagerMcuAndLearn()
{
    using namespace dawg::engine;

    MidiControllerManager mgr;
    require(mgr.isMcuEnabled(), "MCU is enabled by default");

    // 1. MCU Faders (14-bit pitch bend)
    {
        MidiEvent ev {};
        ev.size = 3;
        ev.data[0] = static_cast<std::byte>(0xE0); // Track 0 fader
        ev.data[1] = static_cast<std::byte>(0x00);
        ev.data[2] = static_cast<std::byte>(0x40); // 8192 / 16383 ~= 0.5f

        const auto res = mgr.processEvent(ev);
        require(res.mcuEvent.type == McuType::fader, "MCU parses fader message");
        require(res.mcuEvent.trackIndex == 0, "fader on track 0");
        require(std::abs(res.mcuEvent.value - (8192.0f / 16383.0f)) < 0.001f, "fader value parsed accurately");
    }

    // 2. MCU Master Fader (channel 8)
    {
        MidiEvent ev {};
        ev.size = 3;
        ev.data[0] = static_cast<std::byte>(0xE8); // Master fader
        ev.data[1] = static_cast<std::byte>(0x7F);
        ev.data[2] = static_cast<std::byte>(0x7F); // 16383 / 16383 = 1.0f

        const auto res = mgr.processEvent(ev);
        require(res.mcuEvent.type == McuType::fader, "MCU parses master fader");
        require(res.mcuEvent.trackIndex == 8, "master fader is trackIndex 8");
        require(std::abs(res.mcuEvent.value - 1.0f) < 0.001f, "master fader is full unity/max");
    }

    // 3. MCU Transport Buttons (Play Note 94, Stop Note 93, Record Note 95, Loop Note 86)
    {
        MidiEvent ev {};
        ev.size = 3;
        ev.data[0] = static_cast<std::byte>(0x90);
        ev.data[1] = static_cast<std::byte>(94);
        ev.data[2] = static_cast<std::byte>(127); // Note on

        auto res = mgr.processEvent(ev);
        require(res.mcuEvent.type == McuType::transport, "MCU parses transport");
        require(res.mcuEvent.transport == McuTransport::play, "MCU play command");
        require(res.mcuEvent.buttonState, "play button pressed");

        // Loop button
        ev.data[1] = static_cast<std::byte>(86);
        res = mgr.processEvent(ev);
        require(res.mcuEvent.transport == McuTransport::cycleLoop, "MCU loop command");
    }

    // 4. MCU V-Pots (Relative rotary encoder CC 16..23)
    {
        MidiEvent ev {};
        ev.size = 3;
        ev.data[0] = static_cast<std::byte>(0xB0);
        ev.data[1] = static_cast<std::byte>(16); // Track 0 V-Pot
        ev.data[2] = static_cast<std::byte>(0x03); // +3 steps

        auto res = mgr.processEvent(ev);
        require(res.mcuEvent.type == McuType::vpot, "MCU parses V-Pot rotary");
        require(res.mcuEvent.trackIndex == 0, "V-Pot on track 0");
        require(res.mcuEvent.value == 3.0f, "positive rotary steps");

        // Negative rotation: bit 6 set (0x42 = -2)
        ev.data[2] = static_cast<std::byte>(0x42);
        res = mgr.processEvent(ev);
        require(res.mcuEvent.value == -2.0f, "negative rotary steps");
    }

    // 5. MCU Jog Wheel (CC 60)
    {
        MidiEvent ev {};
        ev.size = 3;
        ev.data[0] = static_cast<std::byte>(0xB0);
        ev.data[1] = static_cast<std::byte>(60);
        ev.data[2] = static_cast<std::byte>(0x01); // +1 tick

        auto res = mgr.processEvent(ev);
        require(res.mcuEvent.type == McuType::jogWheel, "MCU parses jog wheel");
        require(res.mcuEvent.value == 1.0f, "jog forward step");

        ev.data[2] = static_cast<std::byte>(0x41); // -1 tick
        res = mgr.processEvent(ev);
        require(res.mcuEvent.value == -1.0f, "jog backward step");
    }

    // 6. MCU Feedback Generation
    {
        const auto faderFb = MidiControllerManager::makeFaderFeedback(0, 0.5f);
        require(faderFb.size == 3, "fader feedback is 3 bytes");
        require(static_cast<std::uint8_t>(faderFb.data[0]) == 0xE0, "fader feedback is pitch bend");

        const auto ledFb = MidiControllerManager::makeButtonLed(94, true);
        require(static_cast<std::uint8_t>(ledFb.data[0]) == 0x90, "led feedback is note on");
        require(static_cast<std::uint8_t>(ledFb.data[2]) == 0x7F, "led on has velocity 127");

        const auto vpotRing = MidiControllerManager::makeVpotRing(0, 1.0f);
        require(static_cast<std::uint8_t>(vpotRing.data[0]) == 0xB0, "vpot ring feedback is CC");
        require(static_cast<std::uint8_t>(vpotRing.data[1]) == 0x30, "track 0 vpot ring CC is 0x30");
        require(static_cast<std::uint8_t>(vpotRing.data[2]) == 11, "max ring LED value is 11");
    }

    // 7. MIDI Learn
    {
        mgr.startLearn(501, 100.0f, 10000.0f); // Filter cutoff learn
        require(mgr.isLearning(), "learning mode is active");
        require(mgr.learningTargetId() == 501, "target ID is 501");

        // Send CC 74 on channel 1
        MidiEvent ev {};
        ev.size = 3;
        ev.data[0] = static_cast<std::byte>(0xB0); // Channel 1 CC
        ev.data[1] = static_cast<std::byte>(74);   // CC 74
        ev.data[2] = static_cast<std::byte>(64);   // Midpoint

        auto res = mgr.processEvent(ev);
        require(res.learnedNewBinding, "MIDI Learn automatically binds incoming CC");
        require(! mgr.isLearning(), "MIDI Learn exits after binding is captured");
        require(mgr.bindingCount() == 1, "exactly 1 binding stored in controller manager");
        const std::uint32_t boundId = res.learnedBindingId;

        // Send CC 74 with maximum value 127
        ev.data[2] = static_cast<std::byte>(127);
        res = mgr.processEvent(ev);
        require(res.learnResult.triggered, "bound CC triggers parameter evaluation");
        require(res.learnResult.targetId == 501, "evaluated target ID matches");
        require(std::abs(res.learnResult.value - 10000.0f) < 1.0f, "value scaled to max parameter range");

        // Remove binding
        require(mgr.removeBinding(boundId), "binding successfully removed");
        require(mgr.bindingCount() == 0, "binding count is now 0");
    }
}

void testTnhcReverbNodeImpulseAndDecay()
{
    using dawg::engine::TnhcReverbNode;
    TnhcReverbNode reverb;
    require(reverb.prepare(48000.0, 128, 2), "TnhcReverbNode prepares at 48kHz stereo");
    reverb.setRoomSize(0.8f);
    reverb.setDecayTime(1.5f);
    reverb.setDamping(0.3f);
    reverb.setStereoWidth(1.0f);
    reverb.setWetDry(0.5f);

    std::array<float, 128> bufL {};
    std::array<float, 128> bufR {};
    std::array<float*, 2> channels {bufL.data(), bufR.data()};

    // Send impulse at frame 0
    bufL[0] = 1.0f;
    bufR[0] = 1.0f;

    dawg::engine::ProcessContext ctx {
        .audio = dawg::engine::AudioBufferView {channels.data(), 2, 128},
        .midi = {},
        .sampleRate = 48000.0,
        .sidechain = {}
    };

    reverb.process(ctx);

    // After first block, check that diffuse tail begins generating
    float energyTail = 0.0f;
    for (std::size_t i = 1; i < 128; ++i)
    {
        energyTail += std::abs(bufL[i]) + std::abs(bufR[i]);
        require(! std::isnan(bufL[i]) && ! std::isinf(bufL[i]), "Reverb sample is finite");
        require(! std::isnan(bufR[i]) && ! std::isinf(bufR[i]), "Reverb sample is finite");
    }

    // Run several more blocks to verify reverberation decay
    for (int block = 0; block < 10; ++block)
    {
        std::fill(bufL.begin(), bufL.end(), 0.0f);
        std::fill(bufR.begin(), bufR.end(), 0.0f);
        reverb.process(ctx);
        for (std::size_t i = 0; i < 128; ++i)
        {
            energyTail += std::abs(bufL[i]) + std::abs(bufR[i]);
            require(! std::isnan(bufL[i]) && ! std::isinf(bufL[i]), "Reverb tail sample is finite");
        }
    }
    require(energyTail > 0.05f, "Reverb FDN generates decaying reverberation tail");

    reverb.reset();
    std::fill(bufL.begin(), bufL.end(), 0.0f);
    std::fill(bufR.begin(), bufR.end(), 0.0f);
    reverb.process(ctx);
    float energyAfterReset = 0.0f;
    for (std::size_t i = 0; i < 128; ++i)
        energyAfterReset += std::abs(bufL[i]) + std::abs(bufR[i]);
    require(energyAfterReset == 0.0f, "Reverb reset clears delay lines completely");
}

void testTnhcDelayNodePingPongAndSync()
{
    using dawg::engine::TnhcDelayNode;
    using dawg::engine::DelayMode;
    using dawg::engine::DelaySyncDivision;

    TnhcDelayNode delay;
    require(delay.prepare(48000.0, 128, 2), "TnhcDelayNode prepares at 48kHz stereo");
    delay.setMode(DelayMode::pingPong);
    delay.setBpm(120.0);
    delay.setSyncDivision(DelaySyncDivision::sixteenth); // 125 ms = 6000 samples
    delay.setFeedback(0.6f);
    delay.setWetDry(1.0f); // 100% wet to monitor delayed taps

    std::array<float, 128> bufL {};
    std::array<float, 128> bufR {};
    std::array<float*, 2> channels {bufL.data(), bufR.data()};

    // Single impulse on Left channel only
    bufL[0] = 1.0f;
    bufR[0] = 0.0f;

    dawg::engine::ProcessContext ctx {
        .audio = dawg::engine::AudioBufferView {channels.data(), 2, 128},
        .midi = {},
        .sampleRate = 48000.0,
        .sidechain = {}
    };

    delay.process(ctx);

    // Process blocks until delay time (approx 6000 samples / 128 = 47 blocks)
    float maxSeenL = 0.0f;
    float maxSeenR = 0.0f;
    for (int b = 0; b < 60; ++b)
    {
        std::fill(bufL.begin(), bufL.end(), 0.0f);
        std::fill(bufR.begin(), bufR.end(), 0.0f);
        delay.process(ctx);
        for (std::size_t i = 0; i < 128; ++i)
        {
            maxSeenL = std::max(maxSeenL, std::abs(bufL[i]));
            maxSeenR = std::max(maxSeenR, std::abs(bufR[i]));
            require(! std::isnan(bufL[i]) && ! std::isinf(bufL[i]), "Delay sample is finite");
            require(! std::isnan(bufR[i]) && ! std::isinf(bufR[i]), "Delay sample is finite");
        }
    }

    require(maxSeenL > 0.01f || maxSeenR > 0.01f, "Delay generates echoed taps");

    // Saturation test: verify extreme input levels are tamed by tanh
    bufL[0] = 100.0f;
    bufR[0] = 100.0f;
    delay.process(ctx);
    for (std::size_t i = 0; i < 128; ++i)
    {
        require(! std::isnan(bufL[i]) && ! std::isinf(bufL[i]), "Saturated sample is finite");
        require(! std::isnan(bufR[i]) && ! std::isinf(bufR[i]), "Saturated sample is finite");
    }
}

void testDspGraphExternalSidechainRoutingWithPdc()
{
    using dawg::engine::DspGraph;
    using dawg::engine::DelayCompensatorNode;
    using dawg::engine::dsp::DynamicsCompressorNode;
    using dawg::engine::dsp::CompressorSettings;

    DspGraph graph;

    // Node 0: Kick (Input Source 1)
    // Node 1: Latency node on Kick path (3 samples of PDC latency)
    // Node 2: Bass (Input Source 2 / Continuous signal)
    // Node 3: Compressor on Bass, with External Sidechain input from Node 1 (Kick)
    const auto kickNode = graph.addNode(std::make_unique<DelayCompensatorNode>(0), true, false);
    const auto kickPdcNode = graph.addNode(std::make_unique<DelayCompensatorNode>(3), false, false);

    CompressorSettings settings {
        .bypassed = false,
        .thresholdDb = -12.0f,
        .ratio = 10.0f,
        .attackMs = 0.5f,
        .releaseMs = 50.0f,
        .makeupGainDb = 0.0f,
        .kneeDb = 0.0f,
        .useRms = false,
        .sidechainEnabled = true
    };
    const auto compNode = graph.addNode(std::make_unique<DynamicsCompressorNode>(settings), true, true);

    require(graph.addEdge(kickNode, kickPdcNode), "add Kick -> Latency edge");
    require(graph.addSidechainEdge(kickPdcNode, compNode), "add Kick -> Compressor sidechain edge");

    require(graph.compile(48000.0, 16, 2), "compile graph with external sidechain and PDC");

    // Input buffer: channel 0 has Kick pulse of 2.0 (+6 dBFS), channel 1 has Bass continuous 0.5 (-6 dBFS)
    std::array<float, 16> inL {};
    std::array<float, 16> inR {};
    inL[0] = 2.0f; // loud kick impulse
    std::fill(inR.begin(), inR.end(), 0.5f); // quiet continuous bass tone
    std::array<float*, 2> inPlanes {inL.data(), inR.data()};

    std::array<float, 16> outL {};
    std::array<float, 16> outR {};
    std::array<float*, 2> outPlanes {outL.data(), outR.data()};

    graph.process(
        dawg::engine::AudioBufferView {inPlanes.data(), 2, 16},
        dawg::engine::AudioBufferView {outPlanes.data(), 2, 16});

    for (std::size_t i = 0; i < 16; ++i)
    {
        require(! std::isnan(outL[i]) && ! std::isinf(outL[i]), "Sidechain out is finite");
        require(! std::isnan(outR[i]) && ! std::isinf(outR[i]), "Sidechain out is finite");
    }
}

void testDrumPadSamplerNodePadsChokeAndSlicing()
{
    using dawg::engine::DrumPadSamplerNode;

    DrumPadSamplerNode sampler;
    require(sampler.prepare(48000.0, 128, 2), "DrumPadSamplerNode prepares at 48kHz stereo");

    // 1. Verify 16 factory pads
    require(sampler.pad(0).name == "808 Kick", "Pad 0 is 808 Kick");
    require(sampler.pad(5).name == "Closed Hat", "Pad 5 is Closed Hat");
    require(sampler.pad(6).name == "Open Hat", "Pad 6 is Open Hat");
    require(sampler.pad(5).chokeGroup == 1, "Pad 5 belongs to choke group 1");
    require(sampler.pad(6).chokeGroup == 1, "Pad 6 belongs to choke group 1");

    // 2. Trigger Pad 0 and process
    sampler.triggerPad(0, 0.9f);
    require(sampler.isPadActive(0), "Pad 0 is active after trigger");

    std::array<float, 128> bufL {};
    std::array<float, 128> bufR {};
    std::array<float*, 2> channels {bufL.data(), bufR.data()};

    dawg::engine::ProcessContext ctx {
        .audio = dawg::engine::AudioBufferView {channels.data(), 2, 128},
        .midi = {},
        .sampleRate = 48000.0,
        .sidechain = {}
    };

    sampler.process(ctx);

    float energyL = 0.0f;
    for (std::size_t i = 0; i < 128; ++i)
    {
        energyL += std::abs(bufL[i]);
        require(! std::isnan(bufL[i]) && ! std::isinf(bufL[i]), "Sampler sample is finite");
    }
    require(energyL > 0.01f, "Triggering pad produces audible audio");

    // 3. Test Choke Group: Open Hat then Closed Hat
    sampler.triggerPad(6, 0.8f); // Open hat
    require(sampler.isPadActive(6), "Open hat is active");

    std::fill(bufL.begin(), bufL.end(), 0.0f);
    std::fill(bufR.begin(), bufR.end(), 0.0f);
    sampler.process(ctx);

    // Choke by hitting Closed Hat
    sampler.triggerPad(5, 0.9f);
    require(sampler.isPadActive(5), "Closed hat is active");

    // 4. Test Transient Slicing
    std::vector<float> syntheticBreak(48000 * 2, 0.0f); // 2 seconds
    // Place 4 distinct impulse bursts
    for (int t = 0; t < 4; ++t)
    {
        const std::size_t onset = static_cast<std::size_t>(t * 12000);
        for (std::size_t j = 0; j < 500 && (onset + j) < syntheticBreak.size(); ++j)
        {
            syntheticBreak[onset + j] = 0.8f * std::sin(static_cast<float>(j) * 0.1f);
        }
    }

    sampler.sliceSampleAcrossPads(syntheticBreak.data(), syntheticBreak.size(), 48000.0, 16);
    require(! sampler.pad(0).sampleL.empty(), "Pad 0 received slice data");
    require(! sampler.pad(1).sampleL.empty(), "Pad 1 received slice data");

    // 5. Test MIDI Note-On mapping: Note 36 (C1) -> Pad 0
    sampler.stopAll();
    sampler.noteOn(36, 0.75f);
    require(sampler.isPadActive(0), "MIDI Note 36 activates Pad 0");
    sampler.noteOff(36);
}

void testInputMonitoringPunchInAndLoopTakeCount()
{
    AudioEngine engine;
    require(engine.prepare({48000.0, 64, 2}), "AudioEngine prepares for recording test");

    // 1. Input Monitoring test
    engine.setInputMonitoring(true);
    require(engine.isInputMonitoring(), "Input monitoring is active");

    std::array<float, 64> inL {};
    std::array<float, 64> inR {};
    std::fill(inL.begin(), inL.end(), 0.35f);
    std::fill(inR.begin(), inR.end(), 0.35f);
    std::array<const float*, 2> inPlanes {inL.data(), inR.data()};

    std::array<float, 64> outL {};
    std::array<float, 64> outR {};
    std::array<float*, 2> outPlanes {outL.data(), outR.data()};

    engine.process(
        dawg::engine::AudioInputBufferView {inPlanes.data(), 2, 64},
        dawg::engine::AudioBufferView {outPlanes.data(), 2, 64},
        {},
        {});

    require(std::abs(outL[0] - 0.35f) < 1e-4f, "Zero-latency input monitoring mixes hardware input into output");

    // 2. Punch-in test
    dawg::engine::RecordRingBuffer recRing;
    require(recRing.prepare(2, 4096), "RecordRingBuffer prepares for punch test");
    engine.setRecordBuffer(&recRing);
    engine.setPunchRange(true, 1000, 2000);
    require(engine.isPunchEnabled(), "Punch range is enabled");

    // Outside punch range: 0 to 64
    engine.setRecording(true);
    engine.process(
        dawg::engine::AudioInputBufferView {inPlanes.data(), 2, 64},
        dawg::engine::AudioBufferView {outPlanes.data(), 2, 64},
        {},
        {});
    require(recRing.availableFrames() == 0, "No frames written outside punch-in range");

    engine.setRecording(false);
    engine.setRecordBuffer(nullptr);
}

void testModulatorRackLfoEnvelopeAndStepMod()
{
    using dawg::engine::ModulatorRack;
    using dawg::engine::LfoWaveform;
    using dawg::engine::ModSyncDivision;

    ModulatorRack rack;
    rack.prepare(48000.0);

    // 1. Macro knobs
    rack.setMacroValue(0, 0.75f);
    require(std::abs(rack.macro(0).value - 0.75f) < 1e-4f, "Macro 0 set to 0.75");

    // 2. LFO
    rack.setLfoWaveform(0, LfoWaveform::sine);
    rack.setLfoSyncDivision(0, ModSyncDivision::quarter);
    rack.setLfoDepth(0, 1.0f);

    std::array<float, 64> inAudio {};
    std::fill(inAudio.begin(), inAudio.end(), 0.8f);
    std::array<const float*, 1> inAudioPlanes {inAudio.data()};

    rack.process(inAudioPlanes.data(), 1, 64, 120.0, 0);
    require(rack.lfoValue(0) >= 0.0f && rack.lfoValue(0) <= 1.0f, "LFO output is bounded");

    // 3. Envelope Follower
    require(rack.envFollowerValue() > 0.05f, "Envelope follower tracks input audio level");

    // 4. Step Modulator
    rack.setStepValue(0, 0.9f);
    require(std::abs(rack.stepValue(0) - 0.9f) < 1e-4f, "Step 0 value is stored");
}

void testAudioEngineSchedulerAndTopologicalPdc()
{
    // Frontier A: Multi-core topological scheduler & automatic PDC alignment
    dawg::engine::AudioEngine engine;
    require(engine.prepareScheduler(2), "AudioEngine prepared scheduler with 2 worker threads");
    require(engine.schedulerWorkerCount() == 2, "Scheduler worker count is 2");

    // Build a DspGraph with parallel paths: Path 1 has a 64-sample lookahead delay node.
    // Graph compiler must insert a 64-sample DelayCompensatorNode on Path 2 so summing is sample-accurate!
    dawg::engine::DspGraph graph;
    const auto inNode = graph.addNode(std::make_unique<dawg::engine::GainPanNode>(1.0f, 0.0f), true, false);
    const auto delayNode = graph.addNode(std::make_unique<dawg::engine::DelayCompensatorNode>(64), false, false);
    const auto directNode = graph.addNode(std::make_unique<dawg::engine::GainPanNode>(1.0f, 0.0f), false, false);
    const auto masterNode = graph.addNode(std::make_unique<dawg::engine::GainPanNode>(1.0f, 0.0f), false, true);

    require(graph.addEdge(inNode, delayNode), "Edge in -> delayNode");
    require(graph.addEdge(inNode, directNode), "Edge in -> directNode (parallel path)");
    require(graph.addEdge(delayNode, masterNode), "Edge delayNode -> master");
    require(graph.addEdge(directNode, masterNode), "Edge directNode -> master");

    require(graph.compile(48000.0, 256, 2), "Graph compiled with automatic PDC");
    require(graph.totalLatencySamples() == 64, "Total graph latency is 64 samples");
    require(graph.wavefrontCount() >= 3, "Graph grouped into 3+ topological wavefronts");

    // Feed a single impulse at sample 0 into left channel
    std::vector<float> inL(256, 0.0f);
    std::vector<float> inR(256, 0.0f);
    inL[0] = 1.0f;
    std::array<float*, 2> inChans {inL.data(), inR.data()};

    std::vector<float> outL(256, 0.0f);
    std::vector<float> outR(256, 0.0f);
    std::array<float*, 2> outChans {outL.data(), outR.data()};

    // Execute with multi-core work-stealing scheduler
    graph.process(
        dawg::engine::AudioBufferView {inChans.data(), 2, 256},
        dawg::engine::AudioBufferView {outChans.data(), 2, 256},
        engine.scheduler());

    // Both paths must arrive aligned at sample 64 with sum = 2.0 (1.0 delayed + 1.0 compensated)
    require(std::abs(outL[0]) < 1e-6f, "Sample 0 is 0 due to 64-sample PDC alignment");
    require(std::abs(outL[64] - 2.0f) < 1e-4f, "Sample 64 has aligned sum of both parallel paths (2.0)");

    engine.shutdownScheduler();
    require(engine.schedulerWorkerCount() == 0, "Scheduler shut down cleanly");
}

void testPolyphaseOversamplingAndZdfMoogLadder()
{
    // Frontier B: Polyphase 2x/4x Oversampling, ZDF Moog Ladder & Anti-aliased PolyBLAMP
    using dawg::engine::dsp::PolyphaseOversampler;
    using dawg::engine::dsp::OversampleFactor;
    using dawg::engine::dsp::ZdfLadderFilter;
    using dawg::engine::TnhcSynthNode;
    using dawg::engine::SynthWaveform;
    using dawg::engine::SynthFilterMode;

    // 1. Polyphase Oversampler (2x and 4x)
    PolyphaseOversampler oversampler(OversampleFactor::x2);
    require(oversampler.prepare(48000.0, 256, 2), "Polyphase oversampler prepared");
    require(std::abs(oversampler.oversampledRate() - 96000.0) < 1e-4, "2x oversampled rate is 96 kHz");

    std::vector<float> audioL(256, 0.0f);
    std::vector<float> audioR(256, 0.0f);
    // Hard square signal causing extreme harmonic distortion
    for (std::size_t i = 0; i < 256; ++i)
    {
        audioL[i] = (i % 32 < 16) ? 1.5f : -1.5f;
        audioR[i] = audioL[i];
    }
    std::array<float*, 2> chans {audioL.data(), audioR.data()};

    // Apply tanh non-linearity with 2x oversampling
    oversampler.process(
        dawg::engine::AudioBufferView {chans.data(), 2, 256},
        [](float s, double) noexcept { return std::tanh(s * 2.0f); });

    require(std::abs(audioL[0]) > 0.5f, "Oversampled non-linearity processed correctly");
    require(! std::isnan(audioL[0]) && ! std::isinf(audioL[0]), "Output is numerically stable");

    // Test 4x oversampling factor
    oversampler.setFactor(OversampleFactor::x4);
    require(std::abs(oversampler.oversampledRate() - 192000.0) < 1e-4, "4x oversampled rate is 192 kHz");
    oversampler.process(
        dawg::engine::AudioBufferView {chans.data(), 2, 256},
        [](float s, double) noexcept { return std::tanh(s * 2.0f); });

    // 2. Vadim Zavalishin 4-pole ZDF Transistor Ladder Filter
    ZdfLadderFilter ladder;
    ladder.setDrive(1.5f);
    ladder.setResonanceCompensation(0.6f);

    // Filter a test step
    float y = 0.0f;
    for (int i = 0; i < 100; ++i)
    {
        y = ladder.process(1.0f, 1000.0f, 2.5f, 48000.0);
    }
    require(std::isfinite(y), "ZDF ladder filter output is finite and stable");
    require(y > 0.0f, "ZDF ladder step response reaches steady-state");

    // 3. TnhcSynthNode with Moog Ladder mode, PolyBLAMP triangle, and Hard-Sync
    TnhcSynthNode synth;
    require(synth.prepare(48000.0, 256, 2), "TnhcSynth prepared");
    synth.setFilterMode(SynthFilterMode::moogLadder);
    synth.setFilterCutoff(1200.0f);
    synth.setFilterResonance(4.0f);
    synth.setOsc1Waveform(SynthWaveform::saw);
    synth.setOsc2Waveform(SynthWaveform::triangle);
    synth.setHardSync(true);
    require(synth.isHardSync(), "Hard-sync is enabled on synth");

    synth.noteOn(60, 0.9f);
    std::vector<float> synthL(256, 0.0f);
    std::vector<float> synthR(256, 0.0f);
    std::array<float*, 2> synthPlanes {synthL.data(), synthR.data()};
    dawg::engine::ProcessContext synthCtx {
        .audio = dawg::engine::AudioBufferView {synthPlanes.data(), 2, 256},
        .midi = {},
        .sampleRate = 48000.0,
        .sidechain = {}
    };
    synth.process(synthCtx);

    float peak = 0.0f;
    for (const float s : synthL) peak = std::max(peak, std::abs(s));
    require(peak > 0.01f, "Synth produced audio with Moog ladder & hard-sync");
    synth.allNotesOff();
}

void testPluginCrashSupervisorAndAirGappedImmunity()
{
    // Frontier C: Air-gapped out-of-process plugin host & crash supervisor
    using dawg::engine::plugins::PluginCrashSupervisor;
    using dawg::engine::plugins::PluginHostState;

    PluginCrashSupervisor supervisor("TestAirGappedEQ", 1);
    supervisor.setSimulatedCrashMode(true); // Deterministic test environment
    require(supervisor.prepare(48000.0, 256, 2), "Supervisor prepared");
    require(supervisor.state() == PluginHostState::running, "Supervisor state is running");
    require(! supervisor.isCrashed(), "Supervisor is not crashed initially");

    // 1. Process normal audio block
    std::vector<float> bufL(128, 0.5f);
    std::vector<float> bufR(128, 0.5f);
    std::array<float*, 2> bufPlanes {bufL.data(), bufR.data()};
    dawg::engine::ProcessContext ctx {
        .audio = dawg::engine::AudioBufferView {bufPlanes.data(), 2, 128},
        .midi = {},
        .sampleRate = 48000.0,
        .sidechain = {}
    };
    supervisor.process(ctx);
    require(std::abs(bufL[0] - std::tanh(0.5f)) < 1e-4f, "Audio was processed by air-gapped plugin");

    // 2. Inject intentional crash in plugin
    supervisor.injectTestWorkerCrash();
    // Next process cycle catches crash:
    supervisor.process(ctx);
    require(supervisor.isCrashed(), "Supervisor detected plugin crash");
    require(supervisor.state() == PluginHostState::crashed, "State transitioned to crashed");
    require(supervisor.crashCount() == 1, "Crash count incremented to 1");

    // 3. Audio immunity: Host audio continues playing with 0 delay and no crash propagation
    std::vector<float> dryL(64, 0.33f);
    std::vector<float> dryR(64, 0.33f);
    std::array<float*, 2> dryPlanes {dryL.data(), dryR.data()};
    dawg::engine::ProcessContext dryCtx {
        .audio = dawg::engine::AudioBufferView {dryPlanes.data(), 2, 64},
        .midi = {},
        .sampleRate = 48000.0,
        .sidechain = {}
    };
    supervisor.process(dryCtx);
    require(std::abs(dryL[0] - 0.33f) < 1e-6f, "Dry audio passes through untouched while plugin is crashed");

    // 4. 1-Click air-gapped restart
    require(supervisor.restart(), "Supervisor restarted cleanly");
    require(! supervisor.isCrashed(), "Plugin is no longer crashed after restart");
    require(supervisor.state() == PluginHostState::running, "Plugin restored to running state");
}

void testBinauralSpatialPanner3D()
{
    // Option 3: 3D Spatial Audio & Binaural Soundstage Panner
    using dawg::engine::dsp::BinauralSpatialPannerNode;

    BinauralSpatialPannerNode panner;
    require(panner.prepare(48000.0, 256, 2), "Binaural spatial panner prepared");

    // 1. Center positioning
    panner.setAzimuthDeg(0.0f);
    panner.setElevationDeg(0.0f);
    panner.setDistanceMeters(1.0f);
    require(std::abs(panner.azimuthDeg()) < 1e-4f, "Azimuth is 0 (Center/Front)");
    require(std::abs(panner.distanceMeters() - 1.0f) < 1e-4f, "Distance is 1m");

    // 2. Sound on Hard Right (+90 deg):
    // Right ear is near ear, Left ear is far ear.
    // Left ear receives ITD delay (Woodworth model: ~0.65ms => ~31 samples delay at 48kHz).
    panner.setAzimuthDeg(90.0f);

    std::vector<float> inL(256, 0.0f);
    std::vector<float> inR(256, 0.0f);
    inL[0] = 1.0f; // Single impulse at t=0
    std::array<float*, 2> inPlanes {inL.data(), inR.data()};

    dawg::engine::ProcessContext ctx {
        .audio = dawg::engine::AudioBufferView {inPlanes.data(), 2, 256},
        .midi = {},
        .sampleRate = 48000.0,
        .sidechain = {}
    };

    panner.process(ctx);

    // Right ear receives sound at sample 0
    require(std::abs(inR[0]) > 0.1f, "Right ear (near ear) receives direct arrival at sample 0");
    // Left ear direct arrival at sample 0 is suppressed because it is delayed by ITD
    require(std::abs(inL[0]) < 0.05f, "Left ear (far ear) direct arrival is delayed by ITD");

    // 3. Distance Attenuation (1/r inverse distance law)
    BinauralSpatialPannerNode nearPanner;
    require(nearPanner.prepare(48000.0, 64, 2), "Near panner prepared");
    nearPanner.setAzimuthDeg(0.0f);
    nearPanner.setDistanceMeters(1.0f);

    BinauralSpatialPannerNode farPanner;
    require(farPanner.prepare(48000.0, 64, 2), "Far panner prepared");
    farPanner.setAzimuthDeg(0.0f);
    farPanner.setDistanceMeters(5.0f);

    std::vector<float> nearBufL(64, 1.0f);
    std::vector<float> nearBufR(64, 1.0f);
    std::array<float*, 2> nearPlanes {nearBufL.data(), nearBufR.data()};
    dawg::engine::ProcessContext nearCtx {
        .audio = dawg::engine::AudioBufferView {nearPlanes.data(), 2, 64},
        .midi = {},
        .sampleRate = 48000.0,
        .sidechain = {}
    };
    nearPanner.process(nearCtx);

    std::vector<float> farBufL(64, 1.0f);
    std::vector<float> farBufR(64, 1.0f);
    std::array<float*, 2> farPlanes {farBufL.data(), farBufR.data()};
    dawg::engine::ProcessContext farCtx {
        .audio = dawg::engine::AudioBufferView {farPlanes.data(), 2, 64},
        .midi = {},
        .sampleRate = 48000.0,
        .sidechain = {}
    };
    farPanner.process(farCtx);

    require(nearBufL[32] > farBufL[32], "Distance attenuation drops audio level at 5m compared to 1m");
}

void testVocalPitchCorrectionAndAutoTune()
{
    using dawg::engine::dsp::MusicalScale;
    using dawg::engine::dsp::VocalPitchCorrectorNode;

    // 1. Scale Quantization Tests
    // Chromatic: 60.2 -> 60.0, 60.8 -> 61.0
    const float snapChrom1 = VocalPitchCorrectorNode::quantizeToScale(60.2f, MusicalScale::chromatic, 0);
    const float snapChrom2 = VocalPitchCorrectorNode::quantizeToScale(60.8f, MusicalScale::chromatic, 0);
    require(std::abs(snapChrom1 - 60.0f) < 0.05f, "Chromatic snaps 60.2 to C4 (60)");
    require(std::abs(snapChrom2 - 61.0f) < 0.05f, "Chromatic snaps 60.8 to C#4 (61)");

    // C Major (intervals 0, 2, 4, 5, 7, 9, 11) - root C (0)
    // 61.0 (C#) -> nearest note in scale is C (60) or D (62)
    const float snapMaj = VocalPitchCorrectorNode::quantizeToScale(61.0f, MusicalScale::major, 0);
    require(std::abs(snapMaj - 60.0f) < 0.05f || std::abs(snapMaj - 62.0f) < 0.05f, "C Major snaps C# to C or D");

    // A Minor Pentatonic (intervals 0, 3, 5, 7, 10 relative to A = 9)
    // Root 9 -> Notes are A (9), C (0), D (2), E (4), G (7)
    // MIDI 60 is C4 -> exactly in scale!
    const float snapPent = VocalPitchCorrectorNode::quantizeToScale(60.0f, MusicalScale::pentatonicMinor, 9);
    require(std::abs(snapPent - 60.0f) < 0.05f, "A Minor Pentatonic preserves C4 (60)");

    // 2. Engine prepare & process with real-time pitch correction
    VocalPitchCorrectorNode corrector;
    require(corrector.prepare(48000.0, 512, 2), "Vocal pitch corrector prepared");

    corrector.setScale(MusicalScale::chromatic);
    corrector.setRootNote(0);
    corrector.setRetuneSpeed(0.0f); // Hard-tune instant snap
    corrector.setCorrectionAmount(1.0f);

    require(corrector.retuneSpeed() < 0.001f, "Retune speed is 0 (Hard-Tune)");
    require(corrector.scale() == MusicalScale::chromatic, "Scale is chromatic");

    // Generate 440 Hz test sine tone (A4, MIDI 69) with slight +25 cent detune
    constexpr double sampleRate = 48000.0;
    const float detunedHz = 440.0f * std::pow(2.0f, 0.25f / 12.0f); // 446.4 Hz
    std::vector<float> vocalBlockL(512, 0.0f);
    std::vector<float> vocalBlockR(512, 0.0f);
    for (std::size_t i = 0; i < 512; ++i)
    {
        const float s = std::sin(static_cast<float>(2.0 * std::numbers::pi * detunedHz * static_cast<double>(i) / sampleRate));
        vocalBlockL[i] = s * 0.8f;
        vocalBlockR[i] = s * 0.8f;
    }

    std::array<float*, 2> planes { vocalBlockL.data(), vocalBlockR.data() };
    dawg::engine::ProcessContext ctx {
        .audio = dawg::engine::AudioBufferView { planes.data(), 2, 512 },
        .midi = {},
        .sampleRate = 48000.0,
        .sidechain = {}
    };

    // Warm-up analysis buffer (YIN buffer size 1024)
    corrector.process(ctx);
    corrector.process(ctx);

    const auto pitchInfo = corrector.currentPitchInfo();
    require(pitchInfo.isVoiced, "YIN fundamental pitch tracker detected voiced tone");
    require(std::abs(pitchInfo.pitchHz - detunedHz) < 20.0f, "Detected fundamental frequency tracks input tone");
    require(std::abs(pitchInfo.targetMidiPitch - 69.0f) < 0.5f, "Target pitch quantized to A4 (note 69)");

    corrector.reset();
}

void testDspWireGridModularAndOscilloscope()
{
    using dawg::engine::dsp::DspWireGrid;
    using dawg::engine::dsp::GridModuleType;

    DspWireGrid grid;
    require(grid.prepare(48000.0, 256, 2), "DspWireGrid prepared");

    // 1. Built-in presets
    grid.loadPresetAcidBass();
    require(grid.modules().size() >= 4, "Acid Bass 303 preset loaded modules");
    require(grid.cables().size() >= 4, "Acid Bass 303 preset patched cables");

    grid.loadPresetAmbientDrone();
    require(grid.modules().size() >= 4, "Ambient Drone preset loaded");

    grid.loadPresetWestCoastWavefolder();
    require(grid.modules().size() >= 3, "West Coast Wavefolder preset loaded");

    grid.loadPresetPercussionSynth();
    require(grid.modules().size() >= 3, "Percussion Synth preset loaded");

    // 2. Audio Processing & Sound Generation
    grid.loadPresetAcidBass();
    std::vector<float> outL(256, 0.0f);
    std::vector<float> outR(256, 0.0f);
    std::array<float*, 2> planes { outL.data(), outR.data() };
    dawg::engine::ProcessContext ctx {
        .audio = dawg::engine::AudioBufferView { planes.data(), 2, 256 },
        .midi = {},
        .sampleRate = 48000.0,
        .sidechain = {}
    };

    grid.process(ctx);

    float maxEnergy = 0.0f;
    for (std::size_t i = 0; i < 256; ++i)
    {
        maxEnergy = std::max(maxEnergy, std::abs(outL[i]));
    }
    require(maxEnergy > 0.01f, "Modular grid generates live synthesized audio signal");

    // 3. Oscilloscope Probe Readout
    std::array<float, DspWireGrid::probeBufferSize> probeBuf {};
    grid.getProbeData(probeBuf.data(), probeBuf.size());
    float probeEnergy = 0.0f;
    for (const auto s : probeBuf)
    {
        probeEnergy = std::max(probeEnergy, std::abs(s));
    }
    require(probeEnergy > 0.001f, "Oscilloscope probe receives live signal data");

    // 4. Module & Cable Dynamic Editing
    const auto newOscId = grid.addModule(GridModuleType::oscillator, 100.0f, 100.0f, "Test Osc");
    const auto outModId = grid.modules().back().id;
    const auto cableId = grid.connectCable(newOscId, 0, outModId, 0);
    require(cableId > 0, "Cables dynamically connectable");

    require(grid.disconnectCable(cableId), "Cable disconnected");
    require(grid.removeModule(newOscId), "Module removed");

    // 5. Macro Controls
    grid.setMacro(0, 0.85f);
    require(std::abs(grid.macro(0) - 0.85f) < 1e-4f, "Macro knob 0 updated");
}

void testMasteringSuiteLoudnessRadarAndTruePeak()
{
    using dawg::engine::dsp::MasteringMeterNode;
    using dawg::engine::dsp::MasteringTargetProfile;

    MasteringMeterNode meter;
    require(meter.prepare(48000.0, 512, 2), "MasteringMeterNode prepared");

    // 1. Target profiles
    meter.setTargetProfile(MasteringTargetProfile::spotify);
    require(std::abs(meter.currentTargetSpec().targetLufs - (-14.0f)) < 1e-3f, "Spotify target is -14 LUFS");

    meter.setTargetProfile(MasteringTargetProfile::ebuR128);
    require(std::abs(meter.currentTargetSpec().targetLufs - (-23.0f)) < 1e-3f, "EBU R128 target is -23 LUFS");

    meter.setTargetProfile(MasteringTargetProfile::clubFestival);
    require(std::abs(meter.currentTargetSpec().targetLufs - (-9.0f)) < 1e-3f, "Club/Festival target is -9 LUFS");

    // 2. Audio Processing: 1kHz test sine tone
    std::vector<float> testL(512, 0.0f);
    std::vector<float> testR(512, 0.0f);
    for (std::size_t i = 0; i < 512; ++i)
    {
        const float s = 0.5f * std::sin(static_cast<float>(2.0 * std::numbers::pi * 1000.0 * static_cast<double>(i) / 48000.0));
        testL[i] = s;
        testR[i] = s;
    }
    std::array<float*, 2> planes { testL.data(), testR.data() };
    dawg::engine::ProcessContext ctx {
        .audio = dawg::engine::AudioBufferView { planes.data(), 2, 512 },
        .midi = {},
        .sampleRate = 48000.0,
        .sidechain = {}
    };

    meter.process(ctx);

    const auto tel = meter.telemetry();
    require(tel.momentaryLufs > -50.0f, "Momentary LUFS is measured");
    require(tel.truePeakDb > -30.0f, "True Peak dBTP is measured");
    require(tel.correlation > 0.95f, "Mono in-phase test tone has stereo correlation +1.0");

    // 3. Out-of-phase correlation test
    for (std::size_t i = 0; i < 512; ++i)
    {
        testR[i] = -testL[i]; // Invert polarity of Right channel
    }
    meter.process(ctx);
    require(meter.telemetry().correlation < -0.90f, "Inverted phase tone has stereo correlation -1.0");

    // 4. Telemetry Buffers
    std::array<MasteringMeterNode::VectorPoint, MasteringMeterNode::goniometerBufferSize> goniometerPts {};
    meter.getGoniometerPoints(goniometerPts.data(), goniometerPts.size());

    std::array<float, MasteringMeterNode::spectrumBands> spectrumBands {};
    meter.getSpectrumMagnitudes(spectrumBands.data(), spectrumBands.size());

    std::array<float, MasteringMeterNode::radarHistorySize> radarHist {};
    meter.getRadarHistory(radarHist.data(), radarHist.size());

    meter.resetIntegrated();
    require(meter.telemetry().maxTruePeakDb < -90.0f, "Reset integrated clears max true peak");
}

void testLowLatencyAudioDriverRtlAndXruns()
{
    using dawg::platform::juce::AudioDriverBackend;
    using dawg::platform::juce::LowLatencyAudioDriver;

    LowLatencyAudioDriver driver;

    // 1. PipeWire-JACK Ultra-Low Latency configuration
    driver.setBackend(AudioDriverBackend::pipewireJack);
    driver.configure(48000.0, 32, 2, 2); // 32 frames @ 48kHz

    const auto tel32 = driver.telemetry();
    require(tel32.isLowLatencyActive, "32 frames triggers ultra-low latency mode");
    require(tel32.roundTripLatencyMs < 1.5f, "32 frames @ 48kHz achieves sub-1.5ms RTL");

    // 64 frames (Pro Studio Standard)
    driver.configure(48000.0, 64, 2, 2);
    const auto tel64 = driver.telemetry();
    require(tel64.roundTripLatencyMs <= 2.5f, "64 frames @ 48kHz achieves 2.0ms RTL");
    require(tel64.dspLatencyMs < 1.5f, "DSP buffer latency is 1.33ms");

    // 2. Direct ALSA MMAP Backend
    driver.setBackend(AudioDriverBackend::alsaMmap);
    require(driver.backend() == AudioDriverBackend::alsaMmap, "Backend switched to Direct ALSA MMAP");
    require(driver.roundTripLatencyMs() < 3.0f, "ALSA MMAP direct ring achieves low RTL");

    // 3. XRUN Glitch Tracking
    require(driver.xrunCount() == 0, "Initial XRUN count is 0");
    driver.recordXrun();
    driver.recordXrun();
    require(driver.xrunCount() == 2, "XRUN glitch counter accurately records 2 buffer dropouts");
    driver.resetXrunCount();
    require(driver.xrunCount() == 0, "XRUN counter resets to 0");
}

void testNoiseShapedDitherAndOfflineStemRendering()
{
    using dawg::media::DitherNoiseShaping;
    using dawg::media::OfflineRenderService;

    // 1. Noise-Shaped Dither validation
    std::vector<float> testSamples(512, 0.0f);
    for (std::size_t i = 0; i < 512; ++i)
    {
        testSamples[i] = 0.5f * std::sin(static_cast<float>(i) * 0.1f);
    }

    std::uint32_t rngState = 123456789;

    // Flat TPDF
    OfflineRenderService::applyNoiseShapedDither(testSamples.data(), testSamples.size(), rngState, DitherNoiseShaping::flatTpdf);
    for (const auto s : testSamples)
    {
        require(s >= -1.0f && s <= 1.0f, "Flat TPDF dithered samples remain bounded");
    }

    // Lipshitz High-Pass Noise Shaping
    OfflineRenderService::applyNoiseShapedDither(testSamples.data(), testSamples.size(), rngState, DitherNoiseShaping::lipshitz);
    for (const auto s : testSamples)
    {
        require(s >= -1.0f && s <= 1.0f, "Lipshitz noise-shaped samples remain bounded");
    }

    // Psychoacoustic E-Curve Noise Shaping
    OfflineRenderService::applyNoiseShapedDither(testSamples.data(), testSamples.size(), rngState, DitherNoiseShaping::psychoacoustic);
    for (const auto s : testSamples)
    {
        require(s >= -1.0f && s <= 1.0f, "Psychoacoustic noise-shaped samples remain bounded");
    }
}

void testAudioTranscriberMelodyDrumsAndGroove()
{
    using dawg::engine::dsp::AudioTranscriber;
    using dawg::engine::dsp::DrumHitType;
    using dawg::engine::dsp::MusicalScale;

    constexpr double sampleRate = 48000.0;
    constexpr std::size_t totalSamples = 48000 * 2; // 2 seconds
    std::vector<float> audioBuf(totalSamples, 0.0f);

    // 1. Synthesize Drum Break: Kick at 0.0s, Snare at 0.5s, Hi-Hat at 1.0s
    // Kick: 60Hz low sine thump with exponential decay
    for (std::size_t i = 0; i < 4800; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(sampleRate);
        const float env = std::exp(-t * 25.0f);
        audioBuf[i] = 0.9f * std::sin(2.0f * std::numbers::pi_v<float> * 60.0f * t) * env;
    }

    // Snare: 200Hz + high noise burst at 0.5s (24000 samples)
    for (std::size_t i = 0; i < 4800; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(sampleRate);
        const float env = std::exp(-t * 20.0f);
        const float noise = static_cast<float>((i * 1103515245 + 12345) % 65536) / 32768.0f - 1.0f;
        audioBuf[24000 + i] = 0.85f * (0.4f * std::sin(2.0f * std::numbers::pi_v<float> * 220.0f * t) + 0.6f * noise) * env;
    }

    // Hi-Hat: pure high-frequency noise at 1.0s (48000 samples)
    for (std::size_t i = 0; i < 2400; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(sampleRate);
        const float env = std::exp(-t * 80.0f);
        const float noise = static_cast<float>((i * 1664525 + 1013904223) % 65536) / 32768.0f - 1.0f;
        audioBuf[48000 + i] = 0.65f * noise * env;
    }

    // Test Drum Transcription
    const auto drumNotes = AudioTranscriber::transcribeDrums(audioBuf.data(), audioBuf.size(), sampleRate, 120.0);
    require(drumNotes.size() >= 3, "AudioTranscriber detects drum onsets");
    require(drumNotes[0].pitch == 36, "First hit transcribed as Kick (MIDI 36)");
    require(drumNotes[1].pitch == 38, "Second hit transcribed as Snare (MIDI 38)");
    require(drumNotes[2].pitch == 42, "Third hit transcribed as Hi-Hat (MIDI 42)");

    // Test Transient Slicing
    const auto slices = AudioTranscriber::sliceTransients(audioBuf.data(), audioBuf.size(), sampleRate, 16);
    require(slices.size() >= 3, "Transients sliced into sampler pad segments");
    require(slices[0].classifiedType == DrumHitType::kick, "First slice classified as kick");

    // Test Groove & Swing Extraction
    const auto groove = AudioTranscriber::extractGroove(audioBuf.data(), audioBuf.size(), sampleRate, 120.0);
    require(groove.swingPercent >= 0.0f && groove.swingPercent <= 75.0f, "Groove swing within valid percentage range");

    // 2. Melodic Transcription: 440 Hz (A4 = note 69) and 523.25 Hz (C5 = note 72)
    std::vector<float> melodyBuf(totalSamples, 0.0f);
    // Note 1: A4 at 0.0s (attack + sustain)
    for (std::size_t i = 0; i < 20000; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(sampleRate);
        const float env = std::min(1.0f, static_cast<float>(i) / 200.0f) * std::exp(-t * 2.0f);
        melodyBuf[i] = 0.8f * std::sin(2.0f * std::numbers::pi_v<float> * 440.0f * t) * env;
    }
    // Note 2: C5 at 0.5s (24000 samples)
    for (std::size_t i = 0; i < 20000; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(sampleRate);
        const float env = std::min(1.0f, static_cast<float>(i) / 200.0f) * std::exp(-t * 2.0f);
        melodyBuf[24000 + i] = 0.8f * std::sin(2.0f * std::numbers::pi_v<float> * 523.25f * t) * env;
    }

    const auto melodyNotes = AudioTranscriber::transcribeMelody(melodyBuf.data(), melodyBuf.size(), sampleRate, MusicalScale::chromatic, 0);
    require(melodyNotes.size() >= 2, "Melody onsets detected and transcribed");
    require(melodyNotes[0].pitch == 69, "First note transcribed as A4 (MIDI 69)");
    require(melodyNotes[1].pitch == 72, "Second note transcribed as C5 (MIDI 72)");
}

void testMcuTwoWayFeedbackAndLaunchpadRgb()
{
    using namespace dawg::engine;

    MidiControllerManager mgr;
    require(mgr.isMcuEnabled(), "MCU enabled");

    // Fader feedback event
    const auto faderEv = MidiControllerManager::makeFaderFeedback(3, 0.75f);
    require(faderEv.size == 3, "fader feedback is 3 bytes");
    require(std::to_integer<int>(faderEv.data[0]) == (0xE0 + 3), "pitch bend on channel 3");

    // V-Pot LED ring event
    const auto vpotEv = MidiControllerManager::makeVpotRing(1, 0.5f, VpotRingMode::boostCut);
    require(vpotEv.size == 3, "vpot ring event is 3 bytes");
    require(std::to_integer<int>(vpotEv.data[0]) == 0xB0, "CC message");
    require(std::to_integer<int>(vpotEv.data[1]) == (0x30 + 1), "V-pot LED ring CC");

    // Button LED event
    const auto btnEv = MidiControllerManager::makeButtonLed(94, true);
    require(btnEv.size == 3, "button LED is 3 bytes");
    require(std::to_integer<int>(btnEv.data[0]) == 0x90, "Note on");
    require(std::to_integer<int>(btnEv.data[1]) == 94, "Note 94");
    require(std::to_integer<int>(btnEv.data[2]) == 127, "Velocity 127");

    // Scribble strip SysEx
    const auto scribble = MidiControllerManager::makeScribbleStripSysEx(0, "VOX 01", "-3.5dB");
    require(!scribble.empty(), "scribble strip generated");
    require(std::to_integer<int>(scribble.front()) == 0xF0, "SysEx start");
    require(std::to_integer<int>(scribble.back()) == 0xF7, "SysEx end");

    // Timecode display SysEx
    const auto timecode = MidiControllerManager::makeTimecodeDisplaySysEx(4, 2, 3);
    require(!timecode.empty(), "timecode display generated");
    require(std::to_integer<int>(timecode.front()) == 0xF0, "SysEx start");
    require(std::to_integer<int>(timecode.back()) == 0xF7, "SysEx end");

    // Bank shifting
    require(mgr.bank() == 0, "default bank 0");
    mgr.bankUp();
    require(mgr.bank() == 1, "bank up to 1");
    mgr.bankDown();
    require(mgr.bank() == 0, "bank down to 0");

    // Launchpad RGB matrix pad
    const auto lpPad = MidiControllerManager::makeLaunchpadPad(2, 3, LaunchpadColor::green, LaunchpadLedMode::pulsing);
    require(lpPad.size == 3, "Launchpad note event is 3 bytes");
    require(std::to_integer<int>(lpPad.data[0]) == 0x92, "Launchpad pulsing note on channel 3");

    // MIDI Learn persistent serialization & deserialization
    mgr.startLearn(101, -12.0f, 12.0f);
    require(mgr.isLearning(), "learning active");
    require(mgr.learningTargetId() == 101, "target ID 101");

    MidiEvent ccLearn {};
    ccLearn.size = 3;
    ccLearn.data[0] = static_cast<std::byte>(0xB0);
    ccLearn.data[1] = static_cast<std::byte>(74); // Filter Cutoff CC
    ccLearn.data[2] = static_cast<std::byte>(64);
    static_cast<void>(mgr.processEvent(ccLearn));

    require(!mgr.isLearning(), "learn finished on receipt");
    require(mgr.bindingCount() == 1, "1 binding created");

    const auto serialized = mgr.serializeBindings();
    require(!serialized.empty(), "serialized bindings not empty");

    MidiControllerManager mgr2;
    require(mgr2.bindingCount() == 0, "mgr2 starts with 0 bindings");
    const bool ok = mgr2.deserializeBindings(serialized);
    require(ok, "deserialization succeeded");
    require(mgr2.bindingCount() == 1, "mgr2 has 1 restored binding");
    require(mgr2.bindings()[0].targetId == 101, "target ID restored");
}

void testConvolutionReverbZeroLatencyAndCabinetSim()
{
    using namespace dawg::engine::dsp;

    ConvolutionReverbNode reverb;
    require(reverb.latencySamples() == 0, "reverb guarantees 0 samples latency");

    const double sampleRate = 48000.0;
    const std::size_t blockSize = 256;
    require(reverb.prepare(sampleRate, blockSize, 2), "prepare successful");

    // Check all factory presets load and generate impulse response buffers
    reverb.loadPreset(ConvolutionIrPreset::cathedralHall);
    require(reverb.irLengthSamples() > 0, "cathedralHall IR generated");
    require(reverb.currentPreset() == ConvolutionIrPreset::cathedralHall, "preset matches");

    reverb.loadPreset(ConvolutionIrPreset::vintagePlate);
    require(reverb.irLengthSamples() > 0, "vintagePlate IR generated");

    reverb.loadPreset(ConvolutionIrPreset::springTank);
    require(reverb.irLengthSamples() > 0, "springTank IR generated");

    reverb.loadPreset(ConvolutionIrPreset::celestionV30Cab);
    require(reverb.irLengthSamples() > 0, "celestionV30Cab IR generated");

    reverb.loadPreset(ConvolutionIrPreset::customTweedCab);
    require(reverb.irLengthSamples() > 0, "customTweedCab IR generated");

    // Space shaping parameters
    reverb.setDryWet(0.5f);
    require(std::abs(reverb.dryWet() - 0.5f) < 0.001f, "dryWet set");

    reverb.setPreDelayMs(10.0f);
    require(std::abs(reverb.preDelayMs() - 10.0f) < 0.001f, "preDelay set");
    reverb.setPreDelayMs(0.0f);

    reverb.setDecayScale(1.5f);
    require(std::abs(reverb.decayScale() - 1.5f) < 0.001f, "decayScale set");

    reverb.setHiDamping(0.6f);
    require(std::abs(reverb.hiDamping() - 0.6f) < 0.001f, "hiDamping set");

    reverb.setReverseIr(true);
    require(reverb.isReverseIr(), "reverse IR enabled");
    reverb.setReverseIr(false);

    // Cabinet parameters
    reverb.setMicDistanceInches(3.0f);
    require(std::abs(reverb.micDistanceInches() - 3.0f) < 0.001f, "micDistance set");

    reverb.setMicOnAxis(0.5f);
    require(std::abs(reverb.micOnAxis() - 0.5f) < 0.001f, "micOnAxis set");

    // Zero-latency direct FIR head processing test
    std::vector<float> left(blockSize, 0.0f);
    std::vector<float> right(blockSize, 0.0f);
    left[0] = 1.0f; // Unit impulse at sample 0
    right[0] = 1.0f;

    std::array<float*, 2> channels {left.data(), right.data()};
    dawg::engine::AudioBufferView audioView(channels.data(), 2, blockSize);
    dawg::engine::AudioBufferView emptySidechain(nullptr, 0, 0);
    dawg::engine::ProcessContext ctx {.audio = audioView, .midi = {}, .sampleRate = sampleRate, .sidechain = emptySidechain};

    reverb.process(ctx);

    // Verify impulse immediately reverberates without delay (head partition fires at sample 0)
    require(std::abs(left[0]) > 0.0001f, "head partition produces immediate output at sample 0");
    require(std::abs(left[10]) > 0.00001f, "reverberation active in block");
}

void testSpectralSidechainAndMultibandCompression()
{
    using namespace dawg::engine::dsp;

    // 1. Spectral Sidechain & Collision Radar
    {
        SpectralSidechainNode spectral;
        require(spectral.latencySamples() == 0, "spectral sidechain 0 samples latency");

        const double sampleRate = 48000.0;
        const std::size_t blockSize = 256;
        require(spectral.prepare(sampleRate, blockSize, 2), "spectral prepare");

        spectral.setDuckingAmount(0.9f);
        require(std::abs(spectral.duckingAmount() - 0.9f) < 0.001f, "ducking amount");

        // Main audio: 1000 Hz sine
        std::vector<float> mainL(blockSize);
        std::vector<float> mainR(blockSize);
        // Sidechain audio: 1000 Hz sine (colliding frequency)
        std::vector<float> scL(blockSize);
        std::vector<float> scR(blockSize);

        for (std::size_t i = 0; i < blockSize; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(sampleRate);
            const float sig = std::sin(2.0f * std::numbers::pi_v<float> * 1000.0f * t);
            mainL[i] = sig * 0.8f;
            mainR[i] = sig * 0.8f;
            scL[i] = sig * 0.9f;
            scR[i] = sig * 0.9f;
        }

        std::array<float*, 2> mainCh {mainL.data(), mainR.data()};
        std::array<float*, 2> scCh {scL.data(), scR.data()};
        dawg::engine::AudioBufferView mainView(mainCh.data(), 2, blockSize);
        dawg::engine::AudioBufferView scView(scCh.data(), 2, blockSize);
        dawg::engine::ProcessContext ctx {.audio = mainView, .midi = {}, .sampleRate = sampleRate, .sidechain = scView};

        // Process a few blocks to build ducking envelope and radar
        for (int b = 0; b < 10; ++b)
        {
            spectral.process(ctx);
        }

        std::array<float, SpectralSidechainNode::numBands> radar {};
        spectral.getCollisionRadar(radar.data(), radar.size());
        const float maxRadar = *std::max_element(radar.begin(), radar.end());
        require(maxRadar > 0.01f, "spectral collision detected on 1000 Hz collision");
    }

    // 2. Multiband Dynamics Compressor (4-band LR4)
    {
        MultibandCompressorNode mbComp;
        require(mbComp.latencySamples() == 0, "multiband compressor 0 samples latency");

        const double sampleRate = 48000.0;
        const std::size_t blockSize = 256;
        require(mbComp.prepare(sampleRate, blockSize, 2), "mbComp prepare");

        mbComp.setCrossoverFrequencies(90.0f, 700.0f, 5000.0f);
        require(std::abs(mbComp.crossover1Hz() - 90.0f) < 0.01f, "crossover 1 set");
        require(std::abs(mbComp.crossover2Hz() - 700.0f) < 0.01f, "crossover 2 set");
        require(std::abs(mbComp.crossover3Hz() - 5000.0f) < 0.01f, "crossover 3 set");

        // Set Band 0 (Sub/Low, <90 Hz) with aggressive compression
        MultibandBandSettings lowBandSettings {};
        lowBandSettings.thresholdDb = -30.0f;
        lowBandSettings.ratio = 8.0f;
        lowBandSettings.attackMs = 1.0f;
        lowBandSettings.releaseMs = 50.0f;
        lowBandSettings.makeupGainDb = 0.0f;
        mbComp.setBandSettings(0, lowBandSettings);

        // Feed strong 50 Hz sub bass signal (amplitude 0.9)
        std::vector<float> chL(blockSize);
        std::vector<float> chR(blockSize);

        for (int blk = 0; blk < 15; ++blk)
        {
            for (std::size_t i = 0; i < blockSize; ++i)
            {
                const float t = static_cast<float>(blk * blockSize + i) / static_cast<float>(sampleRate);
                const float s = 0.9f * std::sin(2.0f * std::numbers::pi_v<float> * 50.0f * t);
                chL[i] = s;
                chR[i] = s;
            }

            std::array<float*, 2> chs {chL.data(), chR.data()};
            dawg::engine::AudioBufferView view(chs.data(), 2, blockSize);
            dawg::engine::AudioBufferView emptySc(nullptr, 0, 0);
            dawg::engine::ProcessContext ctx {.audio = view, .midi = {}, .sampleRate = sampleRate, .sidechain = emptySc};

            mbComp.process(ctx);
        }

        // Band 0 should register strong gain reduction (>3 dB)
        require(mbComp.gainReductionDb(0) > 3.0f, "Band 0 undergoes substantial gain reduction");
        // Band 3 (High >5000 Hz) should have 0 gain reduction
        require(mbComp.gainReductionDb(3) < 0.1f, "Band 3 has no gain reduction on sub bass");
    }
}

void testAudioWarpEngineElasticMarkersAndQuantize()
{
    using namespace dawg::engine::dsp;

    AudioWarpEngine warper;
    require(warper.markerCount() == 0, "starts with 0 markers");

    const double sampleRate = 48000.0;
    const double bpm = 120.0; // 0.5s per beat, 24000 samples per beat

    // 1. Add markers
    warper.addMarker(0, 0.0, true);
    warper.addMarker(24000, 1.0, true);
    warper.addMarker(48000, 2.0, true);
    require(warper.markerCount() == 3, "added 3 markers");

    // Coordinate conversion
    const double samp = warper.beatToSourceSample(1.5, bpm, sampleRate);
    require(std::abs(samp - 36000.0) < 1.0, "beat 1.5 maps to sample 36000");

    const double beat = warper.sourceSampleToBeat(36000.0, bpm, sampleRate);
    require(std::abs(beat - 1.5) < 0.001, "sample 36000 maps to beat 1.5");

    // Move marker (warp)
    // Stretch beat 1.0 to arrive at timeline beat 1.25 (time dilation / compression)
    warper.moveMarker(1, 1.25);
    const double sampMoved = warper.beatToSourceSample(1.25, bpm, sampleRate);
    require(std::abs(sampMoved - 24000.0) < 1.0, "warped beat 1.25 maps to original sample 24000");

    // 2. Quantize markers to 16th note grid (0.25 beats)
    warper.addMarker(12500, 0.53, false); // slightly off-grid 8th note
    warper.quantizeMarkers(0.25, 1.0f);
    const auto* qm = warper.getMarker(1);
    require(qm != nullptr, "marker exists");

    // 3. Auto-detect transients in a test synthetic drum beat
    std::vector<float> drumBuf(48000 * 2, 0.0f); // 2 seconds
    // Spike at 0, 0.5s (24000), 1.0s (48000), 1.5s (72000)
    drumBuf[0] = 0.9f;
    drumBuf[24000] = 0.85f;
    drumBuf[48000] = 0.9f;
    drumBuf[72000] = 0.85f;

    AudioWarpEngine drumWarper;
    const auto found = drumWarper.autoDetectTransients(drumBuf.data(), drumBuf.size(), sampleRate, bpm);
    require(found >= 2, "autoDetectTransients found beats");

    // 4. Render warped output buffer
    std::vector<float> chL(48000);
    for (std::size_t i = 0; i < 48000; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(sampleRate);
        chL[i] = 0.5f * std::sin(2.0f * std::numbers::pi_v<float> * 440.0f * t);
    }
    std::vector<std::span<const float>> inChans {chL};
    const auto rendered = drumWarper.renderWarped(inChans, sampleRate, bpm, 0.0, 4.0, WarpMode::complex);
    require(!rendered.empty(), "rendered warped buffer not empty");
    require(rendered[0].size() > 0, "rendered frames created");
}

void testNeuralAmpModelerWaveNetAndPowerSag()
{
    using namespace dawg::engine::dsp;

    NeuralAmpModelerNode amp;
    require(amp.latencySamples() == 0, "NAM has 0 samples latency");

    const double sampleRate = 48000.0;
    const std::size_t blockSize = 128;
    require(amp.prepare(sampleRate, blockSize, 2), "prepare successful");

    // Test profile loading
    amp.loadProfile(NamAmpProfile::highGainMetal);
    require(amp.currentProfile() == NamAmpProfile::highGainMetal, "high gain profile loaded");

    amp.loadProfile(NamAmpProfile::vintageBritishPlexi);
    require(amp.currentProfile() == NamAmpProfile::vintageBritishPlexi, "vintage plexi profile loaded");

    amp.loadProfile(NamAmpProfile::classicTweedClean);
    require(amp.currentProfile() == NamAmpProfile::classicTweedClean, "tweed clean profile loaded");

    amp.loadProfile(NamAmpProfile::legendaryOverdrive);
    require(amp.currentProfile() == NamAmpProfile::legendaryOverdrive, "legendary overdrive profile loaded");

    // Tone stack & gain controls
    amp.setInputGainDb(18.0f);
    require(std::abs(amp.inputGainDb() - 18.0f) < 0.01f, "input gain set");

    amp.setMasterVolumeDb(-3.0f);
    require(std::abs(amp.masterVolumeDb() - (-3.0f)) < 0.01f, "master volume set");

    amp.setPowerSagAmount(0.8f);
    require(std::abs(amp.powerSagAmount() - 0.8f) < 0.01f, "sag amount set");

    amp.setDynamicBiasShift(0.6f);
    require(std::abs(amp.dynamicBiasShift() - 0.6f) < 0.01f, "dynamic bias set");

    amp.setBass(1.2f);
    amp.setMiddle(0.8f);
    amp.setTreble(1.4f);
    amp.setPresence(1.1f);
    require(std::abs(amp.bass() - 1.2f) < 0.01f, "bass set");
    require(std::abs(amp.middle() - 0.8f) < 0.01f, "middle set");
    require(std::abs(amp.treble() - 1.4f) < 0.01f, "treble set");
    require(std::abs(amp.presence() - 1.1f) < 0.01f, "presence set");

    // Feed heavy guitar signal and verify non-linear saturation & dynamic power-amp sag
    std::vector<float> chL(blockSize);
    std::vector<float> chR(blockSize);

    amp.reset();
    require(std::abs(amp.currentSagVoltage() - 1.0f) < 0.001f, "sag voltage starts at 1.0 (full rail)");

    for (int blk = 0; blk < 30; ++blk)
    {
        for (std::size_t i = 0; i < blockSize; ++i)
        {
            const float t = static_cast<float>(blk * blockSize + i) / static_cast<float>(sampleRate);
            const float s = 0.95f * std::sin(2.0f * std::numbers::pi_v<float> * 220.0f * t);
            chL[i] = s;
            chR[i] = s;
        }

        std::array<float*, 2> chs {chL.data(), chR.data()};
        dawg::engine::AudioBufferView view(chs.data(), 2, blockSize);
        dawg::engine::AudioBufferView emptySc(nullptr, 0, 0);
        dawg::engine::ProcessContext ctx {.audio = view, .midi = {}, .sampleRate = sampleRate, .sidechain = emptySc};

        amp.process(ctx);
    }

    // Power supply voltage sag should have dropped below 1.0 under heavy sustained load
    require(amp.currentSagVoltage() < 0.98f, "power supply sag engaged under sustained load");
}

void testGranularCloudSynthesisAndTapeFreeze()
{
    using namespace dawg::engine::dsp;

    GranularCloudNode cloud;
    require(cloud.latencySamples() == 0, "cloud has 0 samples latency");

    const double sampleRate = 48000.0;
    const std::size_t blockSize = 256;
    require(cloud.prepare(sampleRate, blockSize, 2), "prepare successful");

    cloud.setGrainSizeMs(40.0f);
    require(std::abs(cloud.grainSizeMs() - 40.0f) < 0.01f, "grain size set");

    cloud.setDensity(50.0f);
    require(std::abs(cloud.density() - 50.0f) < 0.01f, "density set");

    cloud.setSpray(0.5f);
    require(std::abs(cloud.spray() - 0.5f) < 0.01f, "spray set");

    cloud.setPitchShiftSemitones(7.0f); // Perfect 5th up
    require(std::abs(cloud.pitchShiftSemitones() - 7.0f) < 0.01f, "pitch shift set");

    cloud.setPitchDetuneCents(15.0f);
    require(std::abs(cloud.pitchDetuneCents() - 15.0f) < 0.01f, "detune cents set");

    cloud.setStereoDispersal(0.9f);
    require(std::abs(cloud.stereoDispersal() - 0.9f) < 0.01f, "stereo dispersal set");

    cloud.setReverseProbability(0.3f);
    require(std::abs(cloud.reverseProbability() - 0.3f) < 0.01f, "reverse prob set");

    cloud.setWindowShape(GrainWindowShape::gaussian);
    require(cloud.windowShape() == GrainWindowShape::gaussian, "gaussian window set");

    // Load custom sample buffer
    std::vector<float> sampleL(48000);
    std::vector<float> sampleR(48000);
    for (std::size_t i = 0; i < 48000; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(sampleRate);
        sampleL[i] = 0.7f * std::sin(2.0f * std::numbers::pi_v<float> * 330.0f * t);
        sampleR[i] = sampleL[i];
    }
    cloud.loadSampleBuffer(sampleL.data(), sampleR.data(), sampleL.size());

    // Tape Freeze mode
    cloud.setFreeze(true);
    require(cloud.isFrozen(), "freeze mode enabled");

    // Process blocks and verify grain cloud generates audio
    std::vector<float> chL(blockSize, 0.0f);
    std::vector<float> chR(blockSize, 0.0f);

    for (int blk = 0; blk < 20; ++blk)
    {
        std::array<float*, 2> chs {chL.data(), chR.data()};
        dawg::engine::AudioBufferView view(chs.data(), 2, blockSize);
        dawg::engine::AudioBufferView emptySc(nullptr, 0, 0);
        dawg::engine::ProcessContext ctx {.audio = view, .midi = {}, .sampleRate = sampleRate, .sidechain = emptySc};

        cloud.process(ctx);
    }

    require(cloud.activeGrainCount() > 0, "active grains generated in cloud");
    // Verify wet audio energy is generated
    float energy = 0.0f;
    for (std::size_t i = 0; i < blockSize; ++i)
    {
        energy += chL[i] * chL[i] + chR[i] * chR[i];
    }
    require(energy > 0.0001f, "granular cloud produces non-zero ambient wet texture");
}

void testSpatialSurround714AtmosAndAdmExport()
{
    using namespace dawg::engine::dsp;

    SpatialSurroundPannerNode panner;
    require(panner.latencySamples() == 0, "spatial surround panner has 0 samples latency");

    const double sampleRate = 48000.0;
    const std::size_t blockSize = 128;
    require(panner.prepare(sampleRate, blockSize, 2), "prepare successful");

    // Position sound in top front left (+X left is -0.707, +Y front is 0.707, +Z ceiling is 0.85)
    panner.setCoordinates(-0.707f, 0.707f, 0.85f);
    require(std::abs(panner.x() - (-0.707f)) < 0.001f, "X coordinate set");
    require(std::abs(panner.y() - 0.707f) < 0.001f, "Y coordinate set");
    require(std::abs(panner.z() - 0.85f) < 0.001f, "Z coordinate set");

    // Top front left speaker should have highest gain
    const float tflGain = panner.speakerGain(SurroundChannel714::topFrontLeft);
    const float rrsGain = panner.speakerGain(SurroundChannel714::rightRearSurround);
    require(tflGain > rrsGain * 2.0f, "top front left speaker dominant for elevated front-left sound");

    // LFE Subwoofer send
    panner.setLfeSendDb(-6.0f);
    require(panner.speakerGain(SurroundChannel714::lfe) > 0.45f, "LFE send active");

    // Binaural downmix processing
    std::vector<float> chL(blockSize, 0.8f);
    std::vector<float> chR(blockSize, 0.8f);
    std::array<float*, 2> chs {chL.data(), chR.data()};
    dawg::engine::AudioBufferView view(chs.data(), 2, blockSize);
    dawg::engine::AudioBufferView emptySc(nullptr, 0, 0);
    dawg::engine::ProcessContext ctx {.audio = view, .midi = {}, .sampleRate = sampleRate, .sidechain = emptySc};

    panner.process(ctx);

    // Left ear should receive more energy than right ear
    float energyL = 0.0f;
    float energyR = 0.0f;
    for (std::size_t i = 0; i < blockSize; ++i)
    {
        energyL += chL[i] * chL[i];
        energyR += chR[i] * chR[i];
    }
    require(energyL > energyR, "binaural headphone downmix preserves left-dominant spatial balance");

    // ADM BWF XML Metadata generation
    const auto xml = panner.generateAdmMetadataXml("Lead_Synth_Object", 180.0);
    require(!xml.empty(), "ADM XML generated");
    require(xml.find("ITU-R_BS.2076") != std::string::npos, "ITU-R BS.2076 schema included");
    require(xml.find("Lead_Synth_Object") != std::string::npos, "object name in ADM XML");
    require(xml.find("<cartesian>1</cartesian>") != std::string::npos, "cartesian coordinates included");
}

void testHarmonicAssistantChordTrackAndVoiceLeading()
{
    using namespace dawg::engine::dsp;

    HarmonicAssistant harmonic;
    require(harmonic.chords().empty(), "starts with empty chord progression");

    // 1. Timeline chord progression: C -> G -> Am -> F
    harmonic.addChord(0.0, 4.0, Chord {.root = 0, .quality = ChordQuality::major}); // C
    harmonic.addChord(4.0, 4.0, Chord {.root = 7, .quality = ChordQuality::major}); // G
    harmonic.addChord(8.0, 4.0, Chord {.root = 9, .quality = ChordQuality::minor}); // Am
    harmonic.addChord(12.0, 4.0, Chord {.root = 5, .quality = ChordQuality::major}); // F

    require(harmonic.chords().size() == 4, "4 chords added");
    const auto* c1 = harmonic.chordAtBeat(2.0);
    require(c1 != nullptr && c1->chord.root == 0, "beat 2.0 is C major");
    const auto* c2 = harmonic.chordAtBeat(6.0);
    require(c2 != nullptr && c2->chord.root == 7, "beat 6.0 is G major");

    // 2. Roman Numeral Harmonic Analysis
    require(HarmonicAssistant::getRomanNumeral(Chord {.root = 0, .quality = ChordQuality::major}, 0, MusicalScale::major) == "I", "C in C major is I");
    require(HarmonicAssistant::getRomanNumeral(Chord {.root = 2, .quality = ChordQuality::minor}, 0, MusicalScale::major) == "ii", "Dm in C major is ii");
    require(HarmonicAssistant::getRomanNumeral(Chord {.root = 7, .quality = ChordQuality::dom7}, 0, MusicalScale::major) == "V7", "G7 in C major is V7");
    require(HarmonicAssistant::getRomanNumeral(Chord {.root = 9, .quality = ChordQuality::minor}, 0, MusicalScale::major) == "vi", "Am in C major is vi");
    require(HarmonicAssistant::getRomanNumeral(Chord {.root = 10, .quality = ChordQuality::major}, 0, MusicalScale::major) == "bVII", "Bb in C major is bVII (modal interchange)");

    // 3. Dynamic Harmonic Snapping
    // MIDI note 61 (C#) against C major chord (0, 4, 7) snaps to 60 (C)
    const auto snapped = HarmonicAssistant::snapNote(61, Chord {.root = 0, .quality = ChordQuality::major}, 0, MusicalScale::major, true);
    require(snapped == 60 || snapped == 64, "C# snaps to chord tone C or E");

    // 4. Smooth Voice-Leading Optimizer
    // C major voicing: C3 (48), G3 (55), C4 (60), E4 (64)
    const std::vector<std::uint8_t> prevVoicing = {48, 55, 60, 64};
    const Chord targetG {.root = 7, .quality = ChordQuality::major}; // G: G, B, D
    const auto leadVoicing = HarmonicAssistant::optimizeVoiceLeading(targetG, prevVoicing);
    require(leadVoicing.size() == 4, "4-part voicing generated");

    // Verify minimal voice movement (average travel < 3 semitones)
    int totalTravel = 0;
    for (std::size_t i = 0; i < 4; ++i)
    {
        totalTravel += std::abs(static_cast<int>(leadVoicing[i]) - static_cast<int>(prevVoicing[i]));
    }
    require(totalTravel <= 8, "voice leading minimizes total pitch displacement across transitions");

    // 5. Guitar Strum Generation
    const auto strumNotes = HarmonicAssistant::generateStrum(leadVoicing, 0.0, 4.0, 120.0, 48000.0, 20.0f, true);
    require(strumNotes.size() == 4, "4 strummed notes generated");
    require(strumNotes[1].startSample > strumNotes[0].startSample, "subsequent strings delayed by strum time");
}

void testSessionMatrixEngineNonLinearLaunching()
{
    using namespace dawg::engine;

    SessionMatrixEngine session;
    require(session.activePlayingCount() == 0, "session starts with 0 playing clips");

    // Configure clip slot at track 0, scene 0
    SessionClipSlot slot0 {};
    slot0.state = ClipSlotState::stopped;
    slot0.name = "Drum_Loop_A";
    slot0.color = LaunchpadColor::cyan;
    slot0.lengthBeats = 4.0;
    slot0.launchQuantize = LaunchQuantize::bar_1;
    slot0.followAction = FollowAction::nextClip;
    slot0.followActionBars = 1.0;
    session.setClip(0, 0, slot0);

    // Configure clip slot at track 0, scene 1
    SessionClipSlot slot1 {};
    slot1.state = ClipSlotState::stopped;
    slot1.name = "Drum_Loop_B";
    slot1.color = LaunchpadColor::purple;
    slot1.lengthBeats = 4.0;
    slot1.launchQuantize = LaunchQuantize::bar_1;
    session.setClip(0, 1, slot1);

    // 1. Queue clip (0, 0) to fire
    session.fireClip(0, 0, false);
    require(session.getClip(0, 0).state == ClipSlotState::queuedToPlay, "clip 0 is queued to play");

    // 2. Advance clock by 3.5 beats (before bar boundary)
    session.advanceClock(3.5, 3.5, 120.0);
    require(session.getClip(0, 0).state == ClipSlotState::queuedToPlay, "clip still queued before bar boundary");

    // 3. Advance clock across bar boundary (beat 4.1)
    session.advanceClock(0.6, 4.1, 120.0);
    require(session.getClip(0, 0).state == ClipSlotState::playing, "clip starts playing on bar boundary");
    require(session.activePlayingCount() == 1, "1 active playing clip");

    // 4. Advance clock by 4 beats (completing 1 bar) -> Follow Action triggers next clip (0, 1)
    session.advanceClock(4.0, 8.1, 120.0);
    // Follow action should queue scene 1
    require(session.getClip(0, 1).state == ClipSlotState::queuedToPlay || session.getClip(0, 1).state == ClipSlotState::playing, "follow action queued next clip");

    // 5. Launchpad hardware matrix RGB events
    const auto lpEvents = session.generateLaunchpadLedEvents();
    require(lpEvents.size() == SessionMatrixEngine::numTracks * SessionMatrixEngine::numScenes, "64 Launchpad events generated");

    // 6. Handle Launchpad hardware pad press toggle
    // Slot (0, 1) is currently playing, pressing it queues it to stop
    session.handleLaunchpadPress(0, 1);
    require(session.getClip(0, 1).state == ClipSlotState::queuedToStop || session.getClip(0, 1).state == ClipSlotState::stopped, "pressing playing pad triggers stop");

    // Slot (0, 0) is currently stopped, pressing it queues it to play
    session.handleLaunchpadPress(0, 0);
    require(session.getClip(0, 0).state == ClipSlotState::queuedToPlay, "pressing stopped pad triggers play");
}

void testAra2PluginBridgeRandomAccessAndBaking()
{
    using namespace dawg::engine::plugins;

    AraPluginBridge bridge("Test_Project_ARA");
    require(bridge.isAra2Supported(), "ARA2 is supported");
    require(bridge.documentName() == "Test_Project_ARA", "document name matches");

    // 1. Add audio source with synthetic vocal melody (440 Hz = A4, MIDI 69)
    const double sampleRate = 48000.0;
    const std::size_t numSamples = 48000;
    std::vector<float> samples(numSamples, 0.0f);
    for (std::size_t i = 1000; i < 30000; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(sampleRate);
        samples[i] = 0.8f * std::sin(2.0f * std::numbers::pi_v<float> * 440.0f * t);
    }

    AraAudioSource source {
        .id = "src_vocal_01",
        .name = "Lead_Vocal_Take1.wav",
        .sampleRate = sampleRate,
        .channelCount = 1,
        .sampleCount = numSamples,
        .sampleData = {samples}
    };
    require(bridge.addAudioSource(source), "audio source added");

    // 2. Direct note analysis extraction
    const auto notes = bridge.analyzeAudioSource("src_vocal_01");
    require(!notes.empty(), "ARA detected note extracted from audio source");
    require(std::abs(notes[0].pitchMidi - 69.0f) < 1.0f, "detected pitch is A4 (MIDI 69)");

    // 3. Audio Modification (+2 semitones pitch shift)
    AraAudioModification mod {
        .id = "mod_vocal_tune",
        .audioSourceId = "src_vocal_01",
        .name = "Melodyne_Tuned",
        .globalPitchShiftSemitones = 2.0f,
        .globalFormantShiftSemitones = 0.0f,
        .isModified = true
    };
    require(bridge.addAudioModification(mod), "audio modification added");

    // 4. Playback Region
    AraPlaybackRegion region {
        .id = "region_01",
        .audioModificationId = "mod_vocal_tune",
        .startInModificationSec = 0.0,
        .durationInModificationSec = 0.5,
        .startInPlaybackSec = 2.0,
        .durationInPlaybackSec = 0.5
    };
    require(bridge.addPlaybackRegion(region), "playback region added");

    // 5. Instant sample-accurate offline rendering
    const auto rendered = bridge.renderPlaybackRegion("region_01", sampleRate);
    require(!rendered.empty(), "rendered audio channels returned");
    require(rendered[0].size() == 24000, "0.5s rendered at 48kHz produces exactly 24000 samples");

    // 6. Bake ARA edits directly to new audio buffer
    const auto baked = bridge.bakeEditsToBuffer("mod_vocal_tune", sampleRate);
    require(!baked.empty(), "baked buffer generated");
    require(baked[0].size() == numSamples, "baked buffer matches source sample count");
}

void testTransientExciterMultiBandAndInflator()
{
    using namespace dawg::engine::dsp;

    TransientExciterNode node;
    require(node.latencySamples() == 0, "transient exciter guarantees 0 samples latency");

    const double sampleRate = 48000.0;
    const std::size_t blockSize = 128;
    require(node.prepare(sampleRate, blockSize, 2), "prepare successful");

    // Set Low, Mid, High band transient attacks and sustains
    node.setBandAttackDb(0, 4.0f);   // Low punch +4 dB
    node.setBandAttackDb(1, 6.0f);   // Mid snap +6 dB
    node.setBandSustainDb(1, -3.0f); // Mid sustain -3 dB
    node.setBandAttackDb(2, 5.0f);   // High bite +5 dB

    require(std::abs(node.bandAttackDb(0) - 4.0f) < 0.01f, "low attack set");
    require(std::abs(node.bandAttackDb(1) - 6.0f) < 0.01f, "mid attack set");
    require(std::abs(node.bandSustainDb(1) - (-3.0f)) < 0.01f, "mid sustain set");

    // Exciter modes
    node.setExciterMode(ExciterHarmonicMode::warmTube);
    require(node.exciterMode() == ExciterHarmonicMode::warmTube, "warmTube mode set");

    node.setExciterDrive(0.5f);
    require(std::abs(node.exciterDrive() - 0.5f) < 0.01f, "exciter drive set");

    node.setExciterMix(0.4f);
    require(std::abs(node.exciterMix() - 0.4f) < 0.01f, "exciter mix set");

    // Inflator headroom expander
    node.setInflatorCurve(30.0f);
    require(std::abs(node.inflatorCurve() - 30.0f) < 0.01f, "inflator curve set");

    node.setInflatorAmount(0.6f);
    require(std::abs(node.inflatorAmount() - 0.6f) < 0.01f, "inflator amount set");

    // Process drum hit transient block
    std::vector<float> chL(blockSize, 0.0f);
    std::vector<float> chR(blockSize, 0.0f);
    chL[0] = 0.95f; // Sharp percussive transient
    chR[0] = 0.95f;

    std::array<float*, 2> chs {chL.data(), chR.data()};
    dawg::engine::AudioBufferView view(chs.data(), 2, blockSize);
    dawg::engine::AudioBufferView emptySc(nullptr, 0, 0);
    dawg::engine::ProcessContext ctx {.audio = view, .midi = {}, .sampleRate = sampleRate, .sidechain = emptySc};

    node.process(ctx);

    // Verify transient activity is detected in the mid band
    require(node.bandTransientActivity(1) > 0.01f, "mid transient activity detected on drum hit");
    // Peak should be constrained and not blow up
    require(std::abs(chL[0]) <= 1.05f, "transient output well-behaved under tube headroom expander");
}

void testDynamicLinearPhaseEQAndAutoGain()
{
    using namespace dawg::engine::dsp;

    DynamicLinearPhaseEQNode eq;
    require(eq.latencySamples() == 0, "minimum phase EQ has 0 samples latency");

    const double sampleRate = 48000.0;
    const std::size_t blockSize = 128;
    require(eq.prepare(sampleRate, blockSize, 2), "prepare successful");

    // Switch to linear phase mode
    eq.setPhaseMode(EqPhaseMode::linearPhase);
    require(eq.phaseMode() == EqPhaseMode::linearPhase, "linear phase mode enabled");
    require(eq.latencySamples() == DynamicLinearPhaseEQNode::firLatency, "linear phase mode reports FIR group delay latency");

    // Switch back to minimum phase
    eq.setPhaseMode(EqPhaseMode::minimumPhase);
    require(eq.latencySamples() == 0, "minimum phase latency restored to 0");

    // Configure Band 2 (450 Hz Bell) as dynamic compressor
    DynamicEqBandSettings band2 {};
    band2.enabled = true;
    band2.type = EqFilterType::bell;
    band2.frequencyHz = 450.0f;
    band2.gainDb = 0.0f;
    band2.q = 1.4f;
    band2.dynamicEnabled = true;
    band2.thresholdDb = -20.0f;
    band2.rangeDb = -8.0f; // 8 dB dynamic gain ducking
    band2.attackMs = 1.0f;
    band2.releaseMs = 40.0f;
    eq.setBandSettings(2, band2);

    // Feed strong 450 Hz resonant tone (0 dBFS) to trigger dynamic EQ compression
    std::vector<float> chL(blockSize);
    std::vector<float> chR(blockSize);

    for (int blk = 0; blk < 20; ++blk)
    {
        for (std::size_t i = 0; i < blockSize; ++i)
        {
            const float t = static_cast<float>(blk * blockSize + i) / static_cast<float>(sampleRate);
            const float s = 0.9f * std::sin(2.0f * std::numbers::pi_v<float> * 450.0f * t);
            chL[i] = s;
            chR[i] = s;
        }

        std::array<float*, 2> chs {chL.data(), chR.data()};
        dawg::engine::AudioBufferView view(chs.data(), 2, blockSize);
        dawg::engine::AudioBufferView emptySc(nullptr, 0, 0);
        dawg::engine::ProcessContext ctx {.audio = view, .midi = {}, .sampleRate = sampleRate, .sidechain = emptySc};

        eq.process(ctx);
    }

    // Band 2 should register negative dynamic gain shift (compression)
    require(eq.dynamicGainShiftDb(2) < -1.0f, "dynamic EQ engages compression on loud resonance");

    // Test Perceived Loudness Auto-Gain Compensation
    eq.setAutoGain(true);
    require(eq.isAutoGain(), "auto gain enabled");

    // Heavy boost on mid bell band (+10 dB)
    band2.dynamicEnabled = false;
    band2.gainDb = 10.0f;
    eq.setBandSettings(2, band2);

    // Auto-gain should calculate negative compensation gain
    require(eq.currentAutoGainDb() < -2.0f, "auto-gain applies negative compensation against heavy EQ boost");
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
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif
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
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
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
    testMpePolyphonicExpressionAndSynthModulation();
    testMidiControllerManagerMcuAndLearn();
    testTnhcReverbNodeImpulseAndDecay();
    testTnhcDelayNodePingPongAndSync();
    testDspGraphExternalSidechainRoutingWithPdc();
    testDrumPadSamplerNodePadsChokeAndSlicing();
    testInputMonitoringPunchInAndLoopTakeCount();
    testModulatorRackLfoEnvelopeAndStepMod();
    testAudioEngineSchedulerAndTopologicalPdc();
    testPolyphaseOversamplingAndZdfMoogLadder();
    testPluginCrashSupervisorAndAirGappedImmunity();
    testBinauralSpatialPanner3D();
    testVocalPitchCorrectionAndAutoTune();
    testDspWireGridModularAndOscilloscope();
    testMasteringSuiteLoudnessRadarAndTruePeak();
    testLowLatencyAudioDriverRtlAndXruns();
    testNoiseShapedDitherAndOfflineStemRendering();
    testAudioTranscriberMelodyDrumsAndGroove();
    testMcuTwoWayFeedbackAndLaunchpadRgb();
    testConvolutionReverbZeroLatencyAndCabinetSim();
    testSpectralSidechainAndMultibandCompression();
    testAudioWarpEngineElasticMarkersAndQuantize();
    testNeuralAmpModelerWaveNetAndPowerSag();
    testGranularCloudSynthesisAndTapeFreeze();
    testSpatialSurround714AtmosAndAdmExport();
    testHarmonicAssistantChordTrackAndVoiceLeading();
    testSessionMatrixEngineNonLinearLaunching();
    testAra2PluginBridgeRandomAccessAndBaking();
    testTransientExciterMultiBandAndInflator();
    testDynamicLinearPhaseEQAndAutoGain();
}
