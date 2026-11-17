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
#include <dawg/engine/dsp/AnalogLadderRingModSynthNode.h>
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
#include <dawg/engine/dsp/HarmonizerVocoderNode.h>
#include <dawg/engine/dsp/MasterTapeVinylCuttingNode.h>
#include <dawg/engine/dsp/MasteringMeterNode.h>
#include <dawg/engine/dsp/ModulatorRack.h>
#include <dawg/engine/dsp/MultiSampleInstrumentNode.h>
#include <dawg/engine/dsp/MultibandCompressorNode.h>
#include <dawg/engine/dsp/NeuralAmpModelerNode.h>
#include <dawg/engine/dsp/ParametricEQNode.h>
#include <dawg/engine/dsp/PolyphaseOversampler.h>
#include <dawg/engine/dsp/SpatialSurroundPannerNode.h>
#include <dawg/engine/dsp/SpectralBalanceAssistant.h>
#include <dawg/engine/dsp/SpectralResonanceTamerNode.h>
#include <dawg/engine/dsp/SpectralSidechainNode.h>
#include <dawg/engine/dsp/StepSequencerEngine.h>
#include <dawg/engine/dsp/TapeConsoleSummingNode.h>
#include <dawg/engine/dsp/TnhcDelayNode.h>
#include <dawg/engine/MultiTrackRecordingEngine.h>
#include <dawg/engine/HardwareControlSurfaceEngine.h>
#include <dawg/engine/HardwareControlSurfaceManager.h>
#include <dawg/engine/dsp/AcousticDrumBleedCancellationNode.h>
#include <dawg/engine/dsp/AmbisonicsProcessorNode.h>
#include <dawg/engine/dsp/DynamicCabinetSimulatorNode.h>
#include <dawg/engine/dsp/LiveLooperNode.h>
#include <dawg/engine/dsp/MacroModulationMatrix.h>
#include <dawg/engine/dsp/MicrotonalTuningEngine.h>
#include <dawg/engine/dsp/OpticalVariMuCompressorNode.h>
#include <dawg/engine/dsp/PhysicalModelingSynthNode.h>
#include <dawg/engine/dsp/SpatialHeadTrackerBridge.h>
#include <dawg/engine/dsp/SpectralMorphCrossSynthNode.h>
#include <dawg/engine/dsp/SpectralVocoderNode.h>
#include <dawg/engine/dsp/SpringReverbMagneticDrumEchoNode.h>
#include <dawg/engine/dsp/SslChannelStripNode.h>
#include <dawg/engine/ConsoleRoutingMatrix.h>
#include <dawg/engine/dsp/MpeVoiceManager.h>
#include <dawg/engine/dsp/PolymetricSequencerEngine.h>
#include <dawg/engine/dsp/MasteringStemExportEngine.h>
#include <dawg/engine/dsp/StudioControlRoomSimulatorNode.h>
#include <dawg/engine/dsp/TnhcReverbNode.h>
#include <dawg/engine/dsp/TnhcSynthNode.h>
#include <dawg/engine/dsp/TransientExciterNode.h>
#include <dawg/engine/dsp/TransientSplitGranularResynthesizerNode.h>
#include <dawg/engine/dsp/VocalPitchCorrectorNode.h>
#include <dawg/engine/dsp/WavetableSynthNode.h>
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

    // 5. Macro Controls & Surface Bindings
    grid.setMacro(0, 0.85f);
    require(std::abs(grid.macro(0) - 0.85f) < 1e-4f, "Macro knob 0 updated");

    // 6. Single-Sample Feedback Loop & Physical Resonator
    grid.loadPresetFeedbackResonator();
    require(grid.modules().size() >= 5, "Feedback resonator preset loaded");
    std::fill(outL.begin(), outL.end(), 0.0f);
    std::fill(outR.begin(), outR.end(), 0.0f);
    grid.process(ctx);

    float fdbEnergy = 0.0f;
    for (float s : outL) { fdbEnergy += s * s; }
    require(fdbEnergy > 1e-5f, "Unit delay feedback loop generates resonant sound");

    // 7. Custom Macro Surface Binding
    const auto wavefolderModId = grid.addModule(GridModuleType::wavefolder, 200.0f, 200.0f, "Macro Fold");
    grid.bindMacro(3, wavefolderModId, 0, 0.2f, 0.95f, "Wavefold Drive");
    require(grid.macroBindings().size() >= 1, "Macro binding registered");
    grid.setMacro(3, 1.0f); // 100% -> should map to 0.95f
    const auto& fldMod = *std::find_if(grid.modules().begin(), grid.modules().end(), [=](const auto& m) { return m.id == wavefolderModId; });
    require(std::abs(fldMod.params[0] - 0.95f) < 1e-4f, "Macro scaled to custom max range");
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
    slot0.followActionA = FollowAction::nextClip;
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

    // 7. Test Launch Modes: Gate, Toggle, Repeat
    SessionClipSlot gateSlot {};
    gateSlot.state = ClipSlotState::stopped;
    gateSlot.launchMode = ClipLaunchMode::gate;
    gateSlot.launchQuantize = LaunchQuantize::none;
    session.setClip(1, 0, gateSlot);

    session.fireClip(1, 0, true);
    require(session.getClip(1, 0).state == ClipSlotState::playing, "Gate clip playing while pressed");
    session.releaseClip(1, 0);
    require(session.getClip(1, 0).state == ClipSlotState::stopped, "Gate clip stops on release");

    // 8. Test Legato Mode Playhead Transfer
    SessionClipSlot legatoSlotA {};
    legatoSlotA.state = ClipSlotState::stopped;
    legatoSlotA.lengthBeats = 8.0;
    legatoSlotA.launchQuantize = LaunchQuantize::none;
    session.setClip(2, 0, legatoSlotA);

    SessionClipSlot legatoSlotB {};
    legatoSlotB.state = ClipSlotState::stopped;
    legatoSlotB.lengthBeats = 8.0;
    legatoSlotB.isLegato = true;
    legatoSlotB.launchQuantize = LaunchQuantize::none;
    session.setClip(2, 1, legatoSlotB);

    session.fireClip(2, 0, true);
    session.advanceClock(3.5, 3.5, 120.0);
    require(std::abs(session.getClip(2, 0).playbackBeat - 3.5) < 0.01, "Clip A played 3.5 beats");

    session.fireClip(2, 1, true); // Legato switch to Clip B
    require(std::abs(session.getClip(2, 1).playbackBeat - 3.5) < 0.01, "Legato clip B inherited playhead at beat 3.5");

    // 9. Session Jam Recording & Timeline Consolidation
    session.startJamRecording(10.0);
    require(session.isJamRecording(), "Session jam recording active");
    session.fireClip(3, 0, true);
    session.recordMacroChange(0, 0.75f);
    session.advanceClock(4.0, 14.0, 120.0);
    session.stopClip(3, true);
    session.stopJamRecording();
    require(!session.isJamRecording(), "Jam recording stopped");

    const auto consolidated = session.consolidateRecordedClips();
    require(!consolidated.empty(), "Consolidated jam clips generated for timeline arrangement");
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

void testTapeConsoleSummingAndWowFlutter()
{
    using dawg::engine::dsp::TapeConsoleSummingNode;
    using dawg::engine::dsp::TapeSpeed;

    TapeConsoleSummingNode tape;
    constexpr double sampleRate = 48000.0;
    constexpr std::size_t blockSize = 128;

    require(tape.prepare(sampleRate, blockSize, 2), "TapeConsoleSummingNode prepares successfully");
    require(tape.latencySamples() == 0, "Tape machine has zero latency");

    // 1. Verify tape speed and head bump configuration
    tape.setTapeSpeed(TapeSpeed::ips15);
    require(tape.tapeSpeed() == TapeSpeed::ips15, "Tape speed set to 15 IPS");
    tape.setTapeSpeed(TapeSpeed::ips30);
    require(tape.tapeSpeed() == TapeSpeed::ips30, "Tape speed set to 30 IPS");
    tape.setTapeSpeed(TapeSpeed::ips15);

    tape.setDrive(0.8f);
    tape.setTransformerSaturation(0.5f);
    tape.setWow(0.2f);
    tape.setFlutter(0.2f);
    tape.setCrosstalkDb(-70.0f);

    // 2. Feed hot 50 Hz sine wave into left channel only
    std::vector<float> chL(blockSize);
    std::vector<float> chR(blockSize, 0.0f);

    for (std::size_t i = 0; i < blockSize; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(sampleRate);
        chL[i] = 1.4f * std::sin(2.0f * std::numbers::pi_v<float> * 50.0f * t);
    }

    std::array<float*, 2> channels {chL.data(), chR.data()};
    dawg::engine::AudioBufferView audioView(channels.data(), 2, blockSize);
    dawg::engine::AudioBufferView emptySidechain(nullptr, 0, 0);
    dawg::engine::ProcessContext ctx {
        .audio = audioView,
        .midi = {},
        .sampleRate = sampleRate,
        .sidechain = emptySidechain
    };

    tape.process(ctx);

    // Left channel should be smoothly saturated by magnetic hysteresis & transformer
    float maxL = 0.0f;
    for (float s : chL) {
        maxL = std::max(maxL, std::abs(s));
    }
    require(maxL > 0.5f && maxL < 1.15f, "Magnetic hysteresis & transformer compress hot signal");

    // Right channel should exhibit analog console crosstalk bleed from Left
    float maxR = 0.0f;
    for (float s : chR) {
        maxR = std::max(maxR, std::abs(s));
    }
    require(maxR > 0.0f && maxR < 0.05f, "Console summing introduces subtle inter-channel crosstalk");
}

void testMultiSampleInstrumentRoundRobinAndSfz()
{
    using dawg::engine::dsp::MultiSampleInstrumentNode;
    using dawg::engine::dsp::MultiSampleRegion;

    MultiSampleInstrumentNode sampler;
    constexpr double sampleRate = 48000.0;
    constexpr std::size_t blockSize = 128;

    require(sampler.prepare(sampleRate, blockSize, 2), "MultiSampleInstrumentNode prepares");
    require(sampler.latencySamples() == 0, "Zero latency instrument playback");

    // 1. Manually add two round-robin regions for middle C (MIDI 60)
    MultiSampleRegion reg1;
    reg1.rootNote = 60;
    reg1.lowNote = 58;
    reg1.highNote = 62;
    reg1.lowVel = 1;
    reg1.highVel = 127;
    reg1.sampleDataL.resize(512, 0.5f);
    sampler.addRegion(std::move(reg1));

    MultiSampleRegion reg2;
    reg2.rootNote = 60;
    reg2.lowNote = 58;
    reg2.highNote = 62;
    reg2.lowVel = 1;
    reg2.highVel = 127;
    reg2.sampleDataL.resize(512, 0.4f);
    sampler.addRegion(std::move(reg2));

    require(sampler.regionCount() == 2, "Instrument has 2 registered regions");

    // Trigger noteOn
    sampler.noteOn(60, 100);
    require(sampler.activeVoiceCount() == 1, "Voice active after noteOn");
    const auto rr0 = sampler.currentRoundRobin(60);

    // Trigger another noteOn: round robin should advance
    sampler.noteOn(60, 100);
    require(sampler.activeVoiceCount() == 2, "Two voices active");
    const auto rr1 = sampler.currentRoundRobin(60);
    require(rr0 != rr1, "Round robin alternates between sample takes");

    // Process audio block and verify voice playback
    std::vector<float> chL(blockSize, 0.0f);
    std::vector<float> chR(blockSize, 0.0f);
    std::array<float*, 2> channels {chL.data(), chR.data()};
    dawg::engine::AudioBufferView audioView(channels.data(), 2, blockSize);
    dawg::engine::AudioBufferView emptySidechain(nullptr, 0, 0);
    dawg::engine::ProcessContext ctx {
        .audio = audioView,
        .midi = {},
        .sampleRate = sampleRate,
        .sidechain = emptySidechain
    };

    sampler.process(ctx);
    require(std::abs(chL[10]) > 0.01f, "Voices produce audio during block processing");

    // Release note
    sampler.noteOff(60);

    // 2. Test SFZ format descriptor parser
    const std::string sfzData =
        "<region> key=36 lovel=1 hivel=64 seq_position=1\n"
        "<region> key=36 lovel=65 hivel=127 seq_position=1\n"
        "<region> key=38 lovel=1 hivel=127 seq_position=1\n";

    sampler.clearRegions();
    require(sampler.regionCount() == 0, "Cleared regions");
    require(sampler.loadFromSfzString(sfzData), "Loaded SFZ descriptor string");
    require(sampler.regionCount() == 3, "Parsed 3 regions from SFZ string");
}

void testSpectralBalanceAssistantAndSmartUnmasking()
{
    using dawg::engine::dsp::SpectralBalanceAssistant;
    using dawg::engine::dsp::GenreProfile;

    SpectralBalanceAssistant assistant;
    constexpr double sampleRate = 48000.0;
    assistant.prepare(sampleRate);

    // 1. Verify genre profile curves
    assistant.setGenreProfile(GenreProfile::pop);
    require(assistant.genreName() == "Pop", "Pop genre selected");

    assistant.setGenreProfile(GenreProfile::electronic);
    require(assistant.genreName() == "Electronic / EDM", "Electronic genre selected");

    // Sub-bass in Electronic is higher than mid band
    const auto& bands = assistant.bandAnalysis();
    // Band 4 is 50 Hz, Band 17 is 1000 Hz
    require(bands[4].targetDb > bands[17].targetDb, "EDM profile boosts sub-bass targets over mid-range");

    // 2. Analyze audio block with pure 1000 Hz tone
    constexpr std::size_t blockSize = 512;
    std::vector<float> chL(blockSize);
    std::vector<float> chR(blockSize);

    for (std::size_t i = 0; i < blockSize; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(sampleRate);
        const float s = 0.5f * std::sin(2.0f * std::numbers::pi_v<float> * 1000.0f * t);
        chL[i] = s;
        chR[i] = s;
    }

    std::array<float*, 2> channels {chL.data(), chR.data()};
    dawg::engine::AudioBufferView audioView(channels.data(), 2, blockSize);
    for (int rep = 0; rep < 5; ++rep) {
        assistant.analyzeAudioBlock(audioView);
    }

    require(assistant.mixBalanceScore() >= 0.0f && assistant.mixBalanceScore() <= 100.0f,
            "Mix balance score computed in valid percentage range");

    // 3. Multi-track inter-spectral masking collision detection
    std::array<float, SpectralBalanceAssistant::bandCount> trackA {};
    std::array<float, SpectralBalanceAssistant::bandCount> trackB {};
    trackA.fill(-60.0f);
    trackB.fill(-60.0f);

    // Kick drum peaking at 80 Hz (Band 6) at -10 dBFS
    trackA[6] = -10.0f;
    // Sub-bass peaking at 80 Hz (Band 6) at -11 dBFS
    trackB[6] = -11.0f;

    const auto collisions = assistant.detectCollisions(trackA, trackB, -36.0f);
    require(!collisions.empty(), "Detected masking collision between Kick and Bass");
    require(collisions[0].bandIndex == 6, "Collision detected at 80 Hz band");
    require(collisions[0].collisionSeverity > 0.3f, "High masking severity detected at clashing band");

    // 4. Smart dynamic unmasking filter recommendation
    const auto unmasking = assistant.calculateSmartUnmasking(collisions);
    require(!unmasking.empty(), "Generated dynamic unmasking recommendation");
    require(std::abs(unmasking[0].centerFreqHz - 80.0f) < 1.0f, "Unmasking recommendation centered at 80 Hz");
    require(unmasking[0].recommendedGainCutDb < -1.0f, "Recommended surgical notch gain cut");
    require(!unmasking[0].description.empty(), "Unmasking recommendation provides explanatory diagnosis");
}

void testHarmonizerVocoderEngine()
{
    using dawg::engine::dsp::HarmonizerVocoderNode;
    using dawg::engine::dsp::ChordQuality;
    using dawg::engine::dsp::HarmonyVoice;
    using dawg::engine::dsp::VocoderCarrierSource;

    HarmonizerVocoderNode node;
    constexpr double sampleRate = 48000.0;
    constexpr std::size_t blockSize = 256;

    require(node.prepare(sampleRate, blockSize, 2), "HarmonizerVocoderNode prepares");
    require(node.latencySamples() == 0, "Zero latency harmonizer & vocoder");

    // 1. Intelligent Polyphonic Pitch-Shifting Harmonizer
    node.setHarmonizerEnabled(true);
    node.setVocoderEnabled(false);
    node.setDryMix(0.7f);
    node.setHarmonizerWetMix(0.8f);
    node.setChordContext(60, ChordQuality::major); // C Major
    require(node.isHarmonizerEnabled(), "Harmonizer is enabled");
    require(node.isScaleSnapEnabled(), "Scale/Chord snap enabled");

    // Configure Voice 0: Major 3rd (+4 st), Voice 1: Perfect 5th (+7 st)
    HarmonyVoice v0 {.enabled = true, .semitones = 4.0f, .fineTuneCents = 0.0f, .gain = 0.8f, .pan = -0.3f, .formantMorph = 1.0f};
    HarmonyVoice v1 {.enabled = true, .semitones = 7.0f, .fineTuneCents = 0.0f, .gain = 0.8f, .pan = 0.3f, .formantMorph = 1.1f};
    node.setVoice(0, v0);
    node.setVoice(1, v1);

    require(node.voice(0).enabled && std::abs(node.voice(0).semitones - 4.0f) < 1e-4f, "Voice 0 configured to +4 semitones");
    require(node.voice(1).enabled && std::abs(node.voice(1).semitones - 7.0f) < 1e-4f, "Voice 1 configured to +7 semitones");

    node.setGlobalFormantMorph(1.15f);
    require(std::abs(node.globalFormantMorph() - 1.15f) < 1e-4f, "Formant morph configured for throat/gender shift");

    // Feed 220 Hz sine tone (vocal pitch fundamental)
    std::vector<float> chL(blockSize);
    std::vector<float> chR(blockSize);
    for (std::size_t i = 0; i < blockSize; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(sampleRate);
        const float s = 0.6f * std::sin(2.0f * std::numbers::pi_v<float> * 220.0f * t);
        chL[i] = s;
        chR[i] = s;
    }

    std::array<float*, 2> channels {chL.data(), chR.data()};
    dawg::engine::AudioBufferView audioView(channels.data(), 2, blockSize);
    dawg::engine::AudioBufferView emptySidechain(nullptr, 0, 0);
    dawg::engine::ProcessContext ctx {
        .audio = audioView,
        .midi = {},
        .sampleRate = sampleRate,
        .sidechain = emptySidechain
    };

    // Process multiple blocks to populate grain ring buffer
    for (int b = 0; b < 6; ++b) {
        node.process(ctx);
    }

    float maxHarmL = 0.0f;
    for (float s : chL) {
        maxHarmL = std::max(maxHarmL, std::abs(s));
    }
    require(maxHarmL > 0.1f && !std::isnan(maxHarmL) && !std::isinf(maxHarmL),
            "Harmonizer produces stable pitch-shifted polyphonic vocal harmonies");

    // 2. 32-Band Classic Analog Vocoder Engine
    node.setHarmonizerEnabled(false);
    node.setVocoderEnabled(true);
    node.setDryMix(0.0f);
    node.setVocoderWetMix(1.0f);
    node.setVocoderCarrierSource(VocoderCarrierSource::internalPulseSaw);
    node.setVocoderFormantShift(1.1f);
    require(node.isVocoderEnabled(), "Vocoder is enabled");
    require(node.vocoderCarrierSource() == VocoderCarrierSource::internalPulseSaw, "Pulse/saw carrier oscillator selected");
    require(std::abs(node.vocoderFormantShift() - 1.1f) < 1e-4f, "Vocoder formant shift set to 1.1x");

    // Process modulator audio block through vocoder filterbank
    for (int b = 0; b < 6; ++b) {
        node.process(ctx);
    }

    float maxVocL = 0.0f;
    for (float s : chL) {
        maxVocL = std::max(maxVocL, std::abs(s));
    }
    require(maxVocL > 0.001f && !std::isnan(maxVocL),
            "Vocoder processes speech modulator through 32-band analysis & synthesis filterbank");
}

void testStepSequencerAndEuclideanPolyrhythm()
{
    using dawg::engine::dsp::StepSequencerEngine;
    using dawg::engine::dsp::StepCondition;
    using dawg::engine::dsp::StepData;

    StepSequencerEngine seq;
    constexpr double sampleRate = 48000.0;
    seq.prepare(sampleRate);
    seq.setPlaybackBpm(120.0);

    // 1. Bjorklund Euclidean Polyrhythm Generation
    // 4 pulses over 16 steps = straight 4-on-the-floor
    const auto p4_16 = StepSequencerEngine::generateEuclideanPattern(16, 4, 0);
    require(p4_16.size() == 16, "Pattern length is 16");
    std::size_t activeCount4_16 = 0;
    for (bool bit : p4_16) {
        if (bit) ++activeCount4_16;
    }
    require(activeCount4_16 == 4, "4 pulses generated in 16 steps");
    require(p4_16[0] && p4_16[4] && p4_16[8] && p4_16[12], "4-on-the-floor evenly spaced at 0, 4, 8, 12");

    // 3 pulses over 8 steps = classic Cuban Tresillo [1, 0, 0, 1, 0, 0, 1, 0]
    const auto p3_8 = StepSequencerEngine::generateEuclideanPattern(8, 3, 0);
    require(p3_8.size() == 8, "Tresillo length is 8");
    require(p3_8[0] && !p3_8[1] && !p3_8[2] && p3_8[3] && !p3_8[4] && !p3_8[5] && p3_8[6] && !p3_8[7],
            "Bjorklund generates authentic Cuban Tresillo rhythm [1, 0, 0, 1, 0, 0, 1, 0]");

    // Rotation test
    const auto pRotated = StepSequencerEngine::generateEuclideanPattern(4, 1, 1);
    require(!pRotated[0] && pRotated[1] && !pRotated[2] && !pRotated[3], "Pattern rotated by 1 step offset");

    // 2. Track Setup & Parameter Locks (P-Locks)
    seq.applyEuclideanPattern(0, 16, 4, 0, 110); // Track 0 = Kick
    require(seq.trackStepCount(0) == 16, "Track 0 step count is 16");

    // P-Lock pitch transpose on Step 0 (+5 semitones)
    StepData lockStep = seq.step(0, 0);
    lockStep.pitchOffsetSemitones = 5.0f;
    lockStep.ratchets = 2; // 2x ratchet burst
    seq.setStep(0, 0, lockStep);
    require(std::abs(seq.step(0, 0).pitchOffsetSemitones - 5.0f) < 1e-4f, "P-Lock pitch stored on Step 0");
    require(seq.step(0, 0).ratchets == 2, "Step 0 configured with 2x ratchet roll");

    // Step 4 with cycle condition pass2of2 (should only fire on second pattern loop)
    StepData condStep = seq.step(0, 4);
    condStep.condition = StepCondition::pass2of2;
    seq.setStep(0, 4, condStep);

    // Step 12 with Fill Only condition
    StepData fillStep = seq.step(0, 12);
    fillStep.condition = StepCondition::fillOnly;
    seq.setStep(0, 12, fillStep);

    // 3. Step Advance & Real-Time MIDI Generation
    std::vector<dawg::engine::MidiEvent> midiEvents;
    // Step duration at 120 BPM: (15 / 120) * 48000 = 6000 samples per 16th step
    constexpr std::size_t stepSamples = 6000;

    // Advance first step (Step 0)
    seq.advance(stepSamples, 120.0, midiEvents);
    require(!midiEvents.empty(), "MIDI events generated on active step");
    require(midiEvents.size() >= 2, "Step 0 emitted 2 ratcheted note-on events");

    // Verify MIDI Note has P-Locked transposition (+5 semitones on note 36 = 41)
    const auto noteNum = static_cast<std::uint8_t>(midiEvents[0].data[1]);
    require(noteNum == 41, "Note event reflects P-Lock transposition (36 + 5 = 41)");

    // Test Fill Mode
    seq.setFillMode(true);
    require(seq.isFillMode(), "Fill mode active for live performance variations");
}

void testWavetableSynthAnd2DMorphingGrid()
{
    using dawg::engine::dsp::WavetableSynthNode;
    using dawg::engine::dsp::WavetableOscConfig;
    using dawg::engine::dsp::SpectralWarpMode;
    using dawg::engine::dsp::SubOscWaveform;
    using dawg::engine::dsp::WavetableFilterMode;

    WavetableSynthNode synth;
    constexpr double sampleRate = 48000.0;
    constexpr std::size_t blockSize = 128;

    require(synth.prepare(sampleRate, blockSize, 2), "WavetableSynthNode prepared");
    require(synth.latencySamples() == 0, "Zero latency wavetable synthesis");

    // 1. 2D Morphing Orbital Grid
    synth.setMorphCoordinates(0.4f, -0.6f);
    require(std::abs(synth.morphX() - 0.4f) < 1e-4f, "Morph X set to 0.4");
    require(std::abs(synth.morphY() - (-0.6f)) < 1e-4f, "Morph Y set to -0.6");

    synth.setOrbitalModulation(true, 1.5f, 0.35f);
    require(synth.isOrbitalModulationEnabled(), "Orbital trajectory modulation enabled");

    // 2. Configure Oscillators with Spectral Warp
    WavetableOscConfig cfgA;
    cfgA.tablePos = 0.5f;
    cfgA.warpMode = SpectralWarpMode::sync;
    cfgA.warpAmount = 0.4f;
    cfgA.level = 0.8f;
    synth.setOscConfig(0, cfgA);

    WavetableOscConfig cfgB;
    cfgB.tablePos = 0.7f;
    cfgB.warpMode = SpectralWarpMode::wavefolder;
    cfgB.warpAmount = 0.3f;
    cfgB.level = 0.8f;
    synth.setOscConfig(1, cfgB);

    // 3. Sub-Oscillator and SVF Filter
    synth.setSubOsc(true, SubOscWaveform::sine, 1, 0.35f);
    synth.setFilter(WavetableFilterMode::lowpass, 4500.0f, 1.8f, 0.25f);
    synth.setAmpAdsr(2.0f, 80.0f, 0.7f, 150.0f);

    // 4. Trigger Note and Process Audio
    synth.noteOn(60, 110);
    require(synth.activeVoiceCount() == 1, "Voice 1 active after noteOn(60)");

    std::vector<float> chL(blockSize, 0.0f);
    std::vector<float> chR(blockSize, 0.0f);
    std::array<float*, 2> channels {chL.data(), chR.data()};
    dawg::engine::AudioBufferView audioView(channels.data(), 2, blockSize);
    dawg::engine::AudioBufferView emptySidechain(nullptr, 0, 0);
    dawg::engine::ProcessContext ctx {
        .audio = audioView,
        .midi = {},
        .sampleRate = sampleRate,
        .sidechain = emptySidechain
    };

    synth.process(ctx);

    float maxSynth = 0.0f;
    for (float s : chL) {
        maxSynth = std::max(maxSynth, std::abs(s));
    }
    require(maxSynth > 0.05f && maxSynth < 2.0f && !std::isnan(maxSynth),
            "Wavetable synth generates rich anti-aliased output across 2D morph plane");

    // 5. Test MPE 5D Note Expressions (Glide/PitchBend, Press/Pressure, Slide/Timbre)
    synth.notePitchBend(60, 2.0f);
    synth.notePressure(60, 0.75f);
    synth.noteSlide(60, 0.6f);

    std::fill(chL.begin(), chL.end(), 0.0f);
    std::fill(chR.begin(), chR.end(), 0.0f);
    synth.process(ctx);

    float mpeMax = 0.0f;
    for (float s : chL) {
        mpeMax = std::max(mpeMax, std::abs(s));
    }
    require(mpeMax > 0.01f && !std::isnan(mpeMax), "MPE 5D modulated wavetable output verified");

    synth.noteOff(60);
}

void testSpectralResonanceTamerAndDeEsser()
{
    using dawg::engine::dsp::SpectralResonanceTamerNode;
    using dawg::engine::dsp::ResonanceProcessingMode;

    SpectralResonanceTamerNode tamer;
    constexpr double sampleRate = 48000.0;
    constexpr std::size_t blockSize = 128;

    require(tamer.prepare(sampleRate, blockSize, 2), "SpectralResonanceTamerNode prepared");
    require(tamer.latencySamples() > 0, "Lookahead delay latency reported correctly");

    tamer.setDepth(0.85f);
    tamer.setSharpness(2.0f);
    tamer.setThresholdDb(-26.0f);
    tamer.setFrequencyRange(1500.0f, 10000.0f);
    tamer.setProcessingMode(ResonanceProcessingMode::midOnly);

    require(tamer.processingMode() == ResonanceProcessingMode::midOnly, "Mid/Side de-essing mode active");

    // Feed harsh 3500 Hz sibilant resonance (amplitude 0.8) into audio block
    std::vector<float> chL(blockSize);
    std::vector<float> chR(blockSize);

    for (int blk = 0; blk < 10; ++blk) {
        for (std::size_t i = 0; i < blockSize; ++i) {
            const float t = static_cast<float>(blk * blockSize + i) / static_cast<float>(sampleRate);
            const float s = 0.8f * std::sin(2.0f * std::numbers::pi_v<float> * 3500.0f * t);
            chL[i] = s;
            chR[i] = s;
        }

        std::array<float*, 2> channels {chL.data(), chR.data()};
        dawg::engine::AudioBufferView audioView(channels.data(), 2, blockSize);
        dawg::engine::AudioBufferView emptySidechain(nullptr, 0, 0);
        dawg::engine::ProcessContext ctx {
            .audio = audioView,
            .midi = {},
            .sampleRate = sampleRate,
            .sidechain = emptySidechain
        };

        tamer.process(ctx);
    }

    // Dynamic suppression should engage on the 3500 Hz resonance
    require(tamer.currentSuppressionDb() < -1.0f, "Spectral resonance tamer dynamically suppresses harsh 3.5 kHz peak");

    // Test Delta Audition Mode
    tamer.setDeltaListen(true);
    require(tamer.isDeltaListen(), "Delta listen audition mode enabled");
}

void testAnalogLadderRingModSynthAndFeedback()
{
    using dawg::engine::dsp::AnalogLadderRingModSynthNode;
    using dawg::engine::dsp::AnalogOscConfig;
    using dawg::engine::dsp::AnalogOscWaveform;
    using dawg::engine::dsp::DualFilterRouting;

    AnalogLadderRingModSynthNode synth;
    constexpr double sampleRate = 48000.0;
    constexpr std::size_t blockSize = 128;

    require(synth.prepare(sampleRate, blockSize, 2), "AnalogLadderRingModSynthNode prepared");
    require(synth.latencySamples() == 0, "Zero latency analog synth");

    // 1. Oscillators Configuration
    AnalogOscConfig osc1 {.waveform = AnalogOscWaveform::sawtooth, .octave = 0, .semitones = 0.0f, .fineTuneCents = 0.0f, .pulseWidth = 0.5f, .level = 0.8f, .syncToOsc1 = false};
    AnalogOscConfig osc2 {.waveform = AnalogOscWaveform::square,   .octave = 0, .semitones = 7.0f, .fineTuneCents = 2.0f, .pulseWidth = 0.5f, .level = 0.7f, .syncToOsc1 = true};
    AnalogOscConfig osc3 {.waveform = AnalogOscWaveform::triangle, .octave = -1, .semitones = 0.0f, .fineTuneCents = -2.0f, .pulseWidth = 0.5f, .level = 0.6f, .syncToOsc1 = false};
    synth.setOscConfig(0, osc1);
    synth.setOscConfig(1, osc2);
    synth.setOscConfig(2, osc3);

    // 2. Germanium Diode Ring Modulation
    synth.setRingModMix(0.65f);
    synth.setRingModDiodeThreshold(0.5f);
    require(std::abs(synth.ringModMix() - 0.65f) < 1e-4f, "Ring mod mix set to 0.65");
    require(std::abs(synth.ringModDiodeThreshold() - 0.5f) < 1e-4f, "Diode threshold set to 0.5");

    // 3. Dual-Filter Architecture & Feedback Loop
    synth.setTransistorLadder(3000.0f, 2.2f, 0.4f);
    synth.setDiodeLadder(2200.0f, 1.8f);
    synth.setFilterRouting(DualFilterRouting::parallel);
    synth.setFilterBlend(0.5f);
    require(synth.filterRouting() == DualFilterRouting::parallel, "Parallel dual-filter routing active");

    synth.setFeedbackOverdrive(0.75f);
    require(std::abs(synth.feedbackOverdrive() - 0.75f) < 1e-4f, "Headphone feedback loop set to 0.75");

    // 4. Note trigger and audio block processing
    synth.noteOn(48, 120); // C3
    require(synth.activeVoiceCount() == 1, "Voice 1 active after noteOn(48)");

    std::vector<float> chL(blockSize, 0.0f);
    std::vector<float> chR(blockSize, 0.0f);
    std::array<float*, 2> channels {chL.data(), chR.data()};
    dawg::engine::AudioBufferView audioView(channels.data(), 2, blockSize);
    dawg::engine::AudioBufferView emptySidechain(nullptr, 0, 0);
    dawg::engine::ProcessContext ctx {
        .audio = audioView,
        .midi = {},
        .sampleRate = sampleRate,
        .sidechain = emptySidechain
    };

    synth.process(ctx);

    float maxSynth = 0.0f;
    for (float s : chL) {
        maxSynth = std::max(maxSynth, std::abs(s));
    }
    require(maxSynth > 0.05f && maxSynth < 2.5f && !std::isnan(maxSynth),
            "Analog synth with diode ring mod and dual ladder filters generates thick, stable output");

    // Test MPE 5D Note Expressions on Dual Ladder Filter
    synth.notePitchBend(48, -1.5f);
    synth.notePressure(48, 0.85f);
    synth.noteSlide(48, 0.70f);

    std::fill(chL.begin(), chL.end(), 0.0f);
    std::fill(chR.begin(), chR.end(), 0.0f);
    synth.process(ctx);

    float mpeMax = 0.0f;
    for (float s : chL) {
        mpeMax = std::max(mpeMax, std::abs(s));
    }
    require(mpeMax > 0.01f && !std::isnan(mpeMax), "MPE 5D modulated analog ladder output verified");

    synth.noteOff(48);
}

void testMasterTapeVinylCuttingSuite()
{
    using dawg::engine::dsp::MasterTapeVinylCuttingNode;
    using dawg::engine::dsp::EllipticalCrossoverFreq;

    MasterTapeVinylCuttingNode lathe;
    constexpr double sampleRate = 48000.0;
    constexpr std::size_t blockSize = 256;

    require(lathe.prepare(sampleRate, blockSize, 2), "MasterTapeVinylCuttingNode prepared");
    require(lathe.latencySamples() == 0, "Zero latency vinyl cutting suite");

    // 1. Vinyl Cutting Lathe: Elliptical Equalizer (EEQ) & Acceleration Limiter
    lathe.setVinylLatheEnabled(true);
    lathe.setEllipticalCrossover(EllipticalCrossoverFreq::hz150);
    lathe.setHfAccelerationThreshold(-12.0f);
    lathe.setDryMix(0.0f);
    lathe.setVinylMix(1.0f);
    lathe.setTapeFlangeMix(0.0f);

    require(lathe.ellipticalCrossover() == EllipticalCrossoverFreq::hz150, "EEQ crossover set to 150 Hz");

    // Feed completely out-of-phase stereo 50 Hz sub-bass:
    // Left = +0.8, Right = -0.8 (pure vertical needle-skipping groove excursion)
    std::vector<float> chL(blockSize);
    std::vector<float> chR(blockSize);

    for (int rep = 0; rep < 15; ++rep) {
        for (std::size_t i = 0; i < blockSize; ++i) {
            const float t = static_cast<float>(rep * blockSize + i) / static_cast<float>(sampleRate);
            const float s = 0.8f * std::sin(2.0f * std::numbers::pi_v<float> * 50.0f * t);
            chL[i] = s;
            chR[i] = -s; // Inverted phase on Right
        }

        std::array<float*, 2> channels {chL.data(), chR.data()};
        dawg::engine::AudioBufferView audioView(channels.data(), 2, blockSize);
        dawg::engine::AudioBufferView emptySidechain(nullptr, 0, 0);
        dawg::engine::ProcessContext ctx {
            .audio = audioView,
            .midi = {},
            .sampleRate = sampleRate,
            .sidechain = emptySidechain
        };

        lathe.process(ctx);
    }

    // Elliptical equalizer sums out-of-phase sub-bass below 150 Hz to mono,
    // which cancels out out-of-phase side energy (L - R -> 0)!
    float maxSubStereoDiff = 0.0f;
    for (std::size_t i = 0; i < blockSize; ++i) {
        maxSubStereoDiff = std::max(maxSubStereoDiff, std::abs(chL[i] - chR[i]));
    }
    require(maxSubStereoDiff < 0.25f, "Elliptical equalizer collapses out-of-phase sub-bass below 150 Hz to mono");

    // 2. Analog Tape Flanging & Through-Zero Phasing (TZF)
    lathe.setVinylLatheEnabled(false);
    lathe.setTapeFlangeEnabled(true);
    lathe.setThroughZeroDepth(0.95f);
    lathe.setTapeDragSlowdown(0.1f);
    lathe.setTapeFlangeMix(1.0f);

    require(lathe.isTapeFlangeEnabled(), "Tape flanger & through-zero phasing active");

    std::array<float*, 2> channels {chL.data(), chR.data()};
    dawg::engine::AudioBufferView audioView(channels.data(), 2, blockSize);
    dawg::engine::AudioBufferView emptySidechain(nullptr, 0, 0);
    dawg::engine::ProcessContext ctx {
        .audio = audioView,
        .midi = {},
        .sampleRate = sampleRate,
        .sidechain = emptySidechain
    };

    lathe.process(ctx);
    require(!std::isnan(chL[0]) && !std::isinf(chL[0]), "Through-zero tape flange processes cleanly without numerical instability");
}

void testOpticalVariMuCompressorSuite()
{
    using dawg::engine::dsp::OpticalVariMuCompressorNode;
    using dawg::engine::dsp::CompressorModel;
    using dawg::engine::dsp::SidechainFilterMode;

    OpticalVariMuCompressorNode comp;
    constexpr double sampleRate = 48000.0;
    constexpr std::size_t blockSize = 256;

    require(comp.prepare(sampleRate, blockSize, 2), "OpticalVariMuCompressorNode prepared");
    require(comp.latencySamples() == 0, "Zero latency mastering compressor");

    // 1. Teletronix LA-2A Style Electro-Optical T4 Cell Compression
    comp.setModel(CompressorModel::opticalT4);
    comp.setPeakReduction(0.85f);
    comp.setSidechainFilter(SidechainFilterMode::highpass80);
    comp.setMix(1.0f);

    require(comp.model() == CompressorModel::opticalT4, "Model set to optical T4");
    require(comp.sidechainFilter() == SidechainFilterMode::highpass80, "Sidechain HPF at 80 Hz");

    std::vector<float> chL(blockSize);
    std::vector<float> chR(blockSize);

    // Feed a loud 1 kHz test tone
    for (int block = 0; block < 10; ++block) {
        for (std::size_t i = 0; i < blockSize; ++i) {
            const float t = static_cast<float>(block * blockSize + i) / static_cast<float>(sampleRate);
            const float s = 0.85f * std::sin(2.0f * std::numbers::pi_v<float> * 1000.0f * t);
            chL[i] = s;
            chR[i] = s;
        }

        std::array<float*, 2> channels {chL.data(), chR.data()};
        dawg::engine::AudioBufferView audioView(channels.data(), 2, blockSize);
        dawg::engine::AudioBufferView emptySidechain(nullptr, 0, 0);
        dawg::engine::ProcessContext ctx {
            .audio = audioView,
            .midi = {},
            .sampleRate = sampleRate,
            .sidechain = emptySidechain
        };

        comp.process(ctx);
    }

    require(comp.currentGainReductionDb() > 3.0f, "T4 optical cell applies substantial gain reduction on loud signal");

    // 2. Fairchild 670 Style Vari-Mu Dynamic Transconductance & Tube Warmth
    comp.setModel(CompressorModel::variMu);
    comp.setPeakReduction(0.70f);
    comp.setTubeWarmth(0.50f);
    comp.setSidechainFilter(SidechainFilterMode::thrust);

    require(comp.model() == CompressorModel::variMu, "Model set to Vari-Mu tube limiter");
    require(comp.tubeWarmth() == 0.50f, "Tube warmth set to 50%");
    require(comp.sidechainFilter() == SidechainFilterMode::thrust, "API thrust sidechain active");

    for (int block = 0; block < 5; ++block) {
        for (std::size_t i = 0; i < blockSize; ++i) {
            const float t = static_cast<float>(block * blockSize + i) / static_cast<float>(sampleRate);
            const float s = 0.90f * std::sin(2.0f * std::numbers::pi_v<float> * 400.0f * t);
            chL[i] = s;
            chR[i] = s;
        }

        std::array<float*, 2> channels {chL.data(), chR.data()};
        dawg::engine::AudioBufferView audioView(channels.data(), 2, blockSize);
        dawg::engine::AudioBufferView emptySidechain(nullptr, 0, 0);
        dawg::engine::ProcessContext ctx {
            .audio = audioView,
            .midi = {},
            .sampleRate = sampleRate,
            .sidechain = emptySidechain
        };

        comp.process(ctx);
    }

    require(comp.currentGainReductionDb() > 1.0f, "Vari-Mu variable-mu compression active");
    require(!std::isnan(chL[0]) && !std::isinf(chL[0]), "Vari-Mu tube saturation produces numerical stability");
}

void testMicrotonalTuningEngineSuite()
{
    using dawg::engine::dsp::MicrotonalTuningEngine;
    using dawg::engine::dsp::FactoryScale;

    MicrotonalTuningEngine engine;

    // 1. Standard 12-TET verification
    require(std::abs(engine.frequencyForNote(69) - 440.0) < 0.001, "A4 is calibrated to exactly 440.0 Hz");
    require(std::abs(engine.frequencyForNote(60) - 261.6255) < 0.05, "Middle C is ~261.63 Hz");
    require(std::abs(engine.centsOffsetFrom12TET(69)) < 0.001, "Zero cents offset for 12-TET A4");

    // 2. 24-EDO (Quarter-Tone Arabic Maqam scale)
    engine.loadFactoryScale(FactoryScale::twentyFourEqual);
    require(engine.divisionsPerOctave() == 24, "24-EDO has 24 divisions per octave");
    require(std::abs(engine.centsForDegree(0) - 50.0) < 0.001, "Step 1 of 24-EDO is exactly 50 cents");
    require(std::abs(engine.centsForDegree(1) - 100.0) < 0.001, "Step 2 of 24-EDO is 100 cents");

    // 3. 5-Limit Just Intonation
    engine.loadFactoryScale(FactoryScale::justIntonation5L);
    require(engine.divisionsPerOctave() == 12, "5-Limit chromatic scale has 12 notes");
    // Degree 4 (index 3) is Major 3rd (5/4 = 386.314 cents)
    require(std::abs(engine.centsForDegree(3) - 386.314) < 0.1, "Pure 5/4 major third is 386.31 cents");
    // Degree 7 (index 6) is Perfect 5th (3/2 = 701.955 cents)
    require(std::abs(engine.centsForDegree(6) - 701.955) < 0.1, "Pure 3/2 perfect fifth is 701.96 cents");

    // 4. Scala (.scl) Parser
    const std::string_view ptolemyScala =
        "! ptolemy.scl\n"
        "Ptolemy's Intense Diatonic Scale (syntonic diatonic)\n"
        "7\n"
        "!\n"
        "9/8\n"
        "5/4\n"
        "4/3\n"
        "3/2\n"
        "5/3\n"
        "15/8\n"
        "2/1\n";

    require(engine.loadScalaString(ptolemyScala), "Parsed Ptolemy Scala scale successfully");
    require(engine.scaleDegreeCount() == 7, "Ptolemy scale has 7 scale degrees");
    require(engine.currentScale().formalOctaveCents == 1200.0, "Formal octave is 1200 cents");

    // 5. Keyboard Mapping (.kbm) Parser
    const std::string_view sampleKbm =
        "! sample.kbm\n"
        "7\n"
        "0\n"
        "127\n"
        "60\n"
        "69\n"
        "440.0\n"
        "5\n"
        "0\n"
        "1\n"
        "2\n"
        "3\n"
        "4\n"
        "5\n"
        "6\n";

    require(engine.loadMappingString(sampleKbm), "Parsed Keyboard Mapping successfully");
    require(engine.currentMapping().mapSize == 7, "Mapping has 7 keys per cycle");
    require(engine.currentMapping().referenceFrequencyHz == 440.0, "Ref freq is 440 Hz");

    const std::string noteLabel = engine.noteDegreeLabel(60);
    require(!noteLabel.empty(), "Note degree label generated");
}

void testStudioControlRoomSimulatorSuite()
{
    using dawg::engine::dsp::StudioControlRoomSimulatorNode;
    using dawg::engine::dsp::StudioEnvironment;

    StudioControlRoomSimulatorNode room;
    constexpr double sampleRate = 48000.0;
    constexpr std::size_t blockSize = 256;

    require(room.prepare(sampleRate, blockSize, 2), "StudioControlRoomSimulatorNode prepared");
    require(room.latencySamples() == 0, "Zero latency control room simulator");

    // 1. Environment and Position Configuration
    room.setEnvironment(StudioEnvironment::masteringSuite);
    room.setHeadYawDegrees(20.0f);
    room.setMonitorDistanceMeters(2.2f);
    room.setCrossfeedAmount(0.9f);
    room.setRoomAmbienceAmount(0.5f);
    room.setMix(1.0f);

    require(room.environment() == StudioEnvironment::masteringSuite, "Environment set to masteringSuite");
    require(room.headYawDegrees() == 20.0f, "Head yaw set to 20 degrees");
    require(room.monitorDistanceMeters() == 2.2f, "Distance set to 2.2 meters");

    // 2. Binaural Crossfeed Verification:
    // Feed sound strictly into the Left channel (Right = 0.0)
    std::vector<float> chL(blockSize, 0.0f);
    std::vector<float> chR(blockSize, 0.0f);

    for (int rep = 0; rep < 5; ++rep) {
        for (std::size_t i = 0; i < blockSize; ++i) {
            const float t = static_cast<float>(rep * blockSize + i) / static_cast<float>(sampleRate);
            chL[i] = 0.8f * std::sin(2.0f * std::numbers::pi_v<float> * 800.0f * t);
            chR[i] = 0.0f; // Silence on right
        }

        std::array<float*, 2> channels {chL.data(), chR.data()};
        dawg::engine::AudioBufferView audioView(channels.data(), 2, blockSize);
        dawg::engine::AudioBufferView emptySidechain(nullptr, 0, 0);
        dawg::engine::ProcessContext ctx {
            .audio = audioView,
            .midi = {},
            .sampleRate = sampleRate,
            .sidechain = emptySidechain
        };

        room.process(ctx);
    }

    // Right channel should now contain crossfeed & reflection signals arriving around the head
    float maxRightEnergy = 0.0f;
    for (std::size_t i = 0; i < blockSize; ++i) {
        maxRightEnergy = std::max(maxRightEnergy, std::abs(chR[i]));
    }
    require(maxRightEnergy > 0.05f, "Contralateral ear receives binaural crossfeed & head shadow from left speaker");

    // 3. Environment switching to Large Club PA
    room.setEnvironment(StudioEnvironment::largeClubPA);
    require(room.environment() == StudioEnvironment::largeClubPA, "Environment switched to largeClubPA");

    // 4. Dry/Wet Mix Bypass Check
    room.setMix(0.0f);
    for (std::size_t i = 0; i < blockSize; ++i) {
        chL[i] = 0.5f;
        chR[i] = 0.25f;
    }
    std::array<float*, 2> channels {chL.data(), chR.data()};
    dawg::engine::AudioBufferView audioView(channels.data(), 2, blockSize);
    dawg::engine::AudioBufferView emptySidechain(nullptr, 0, 0);
    dawg::engine::ProcessContext ctx {
        .audio = audioView,
        .midi = {},
        .sampleRate = sampleRate,
        .sidechain = emptySidechain
    };
    room.process(ctx);
    require(std::abs(chL[0] - 0.5f) < 1.0e-5f && std::abs(chR[0] - 0.25f) < 1.0e-5f, "Mix 0.0 gives bit-identical dry signal");
}

void testSpectralSidechainUnmaskerAndAuditionDelta()
{
    using dawg::engine::dsp::SpectralSidechainNode;

    SpectralSidechainNode unmasker;
    constexpr double sampleRate = 48000.0;
    constexpr std::size_t blockSize = 256;

    require(unmasker.prepare(sampleRate, blockSize, 2), "SpectralSidechainNode prepared");
    require(unmasker.latencySamples() == 0, "Zero latency spectral unmasker");

    unmasker.setDuckingAmount(0.85f);
    unmasker.setSensitivity(1.5f);
    unmasker.setAdaptiveQEnabled(true);
    unmasker.setAuditionDelta(false);

    require(unmasker.isAdaptiveQEnabled(), "Adaptive Q enabled");
    require(!unmasker.isAuditionDelta(), "Audition delta initially false");
    require(unmasker.sensitivity() == 1.5f, "Sensitivity set to 1.5");

    // 1. Bit-transparent pass-through when sidechain is inactive
    std::vector<float> mainL(blockSize, 0.4f);
    std::vector<float> mainR(blockSize, 0.4f);
    std::vector<float> sideL(blockSize, 0.0f);
    std::vector<float> sideR(blockSize, 0.0f);

    std::array<float*, 2> mainCh {mainL.data(), mainR.data()};
    std::array<float*, 2> sideCh {sideL.data(), sideR.data()};
    dawg::engine::AudioBufferView mainView(mainCh.data(), 2, blockSize);
    dawg::engine::AudioBufferView sideView(sideCh.data(), 2, blockSize);
    dawg::engine::ProcessContext ctx {
        .audio = mainView,
        .midi = {},
        .sampleRate = sampleRate,
        .sidechain = sideView
    };

    unmasker.process(ctx);
    require(std::abs(mainL[0] - 0.4f) < 1.0e-5f, "Zero sidechain yields bit-transparent dry audio");

    // 2. Frequency-masking collision: Feed 100 Hz kick in sidechain and 100 Hz bass in main
    for (int block = 0; block < 10; ++block) {
        for (std::size_t i = 0; i < blockSize; ++i) {
            const float t = static_cast<float>(block * blockSize + i) / static_cast<float>(sampleRate);
            mainL[i] = 0.8f * std::sin(2.0f * std::numbers::pi_v<float> * 100.0f * t);
            mainR[i] = mainL[i];
            sideL[i] = 0.9f * std::sin(2.0f * std::numbers::pi_v<float> * 100.0f * t);
            sideR[i] = sideL[i];
        }
        unmasker.process(ctx);
    }

    // Main signal at 100 Hz should be surgically ducked
    float duckedPeak = 0.0f;
    for (std::size_t i = 0; i < blockSize; ++i) {
        duckedPeak = std::max(duckedPeak, std::abs(mainL[i]));
    }
    require(duckedPeak < 0.75f, "Colliding 100 Hz frequency component dynamically ducked by sidechain");

    // 3. Audition Delta Mode: Solo strictly the carved-out difference signal
    unmasker.setAuditionDelta(true);
    require(unmasker.isAuditionDelta(), "Audition delta mode enabled");

    for (std::size_t i = 0; i < blockSize; ++i) {
        const float t = static_cast<float>(10 * blockSize + i) / static_cast<float>(sampleRate);
        mainL[i] = 0.8f * std::sin(2.0f * std::numbers::pi_v<float> * 100.0f * t);
        mainR[i] = mainL[i];
        sideL[i] = 0.9f * std::sin(2.0f * std::numbers::pi_v<float> * 100.0f * t);
        sideR[i] = sideL[i];
    }
    unmasker.process(ctx);

    float deltaPeak = 0.0f;
    for (std::size_t i = 0; i < blockSize; ++i) {
        deltaPeak = std::max(deltaPeak, std::abs(mainL[i]));
    }
    require(deltaPeak > 0.05f, "Audition delta mode solos carved-out frequency delta energy");
}

void testPhysicalModelingSynthSuite()
{
    using dawg::engine::dsp::PhysicalModelingSynthNode;
    using dawg::engine::dsp::PhysicalModelType;
    using dawg::engine::dsp::ExciterType;

    PhysicalModelingSynthNode synth;
    constexpr double sampleRate = 48000.0;
    constexpr std::size_t blockSize = 256;

    require(synth.prepare(sampleRate, blockSize, 2), "PhysicalModelingSynthNode prepared");
    require(synth.latencySamples() == 0, "Zero latency physical modeling synth");

    // 1. Karplus-Strong Waveguide String with Mallet Strike
    synth.setModelType(PhysicalModelType::waveguideString);
    synth.setExciterType(ExciterType::mallet);
    synth.setDecayTimeSeconds(1.2f);
    synth.setMalletHardness(0.6f);
    synth.setStrikePosition(0.2f);

    require(synth.modelType() == PhysicalModelType::waveguideString, "Model set to waveguideString");
    require(synth.exciterType() == ExciterType::mallet, "Exciter set to mallet");

    synth.noteOn(60, 0.85f);
    require(synth.activeVoiceCount() == 1, "Voice 1 active after noteOn");

    std::vector<float> chL(blockSize, 0.0f);
    std::vector<float> chR(blockSize, 0.0f);
    std::array<float*, 2> channels {chL.data(), chR.data()};
    dawg::engine::AudioBufferView audioView(channels.data(), 2, blockSize);
    dawg::engine::AudioBufferView emptySidechain(nullptr, 0, 0);
    dawg::engine::ProcessContext ctx {
        .audio = audioView,
        .midi = {},
        .sampleRate = sampleRate,
        .sidechain = emptySidechain
    };

    synth.process(ctx);

    float peakString = 0.0f;
    for (std::size_t i = 0; i < blockSize; ++i) {
        peakString = std::max(peakString, std::abs(chL[i]));
    }
    require(peakString > 0.05f, "Karplus-Strong waveguide produces ringing string tone");
    require(!std::isnan(chL[0]) && !std::isinf(chL[0]), "Waveguide audio is numerically stable");

    // 2. Modal Resonator Membrane (Drumhead physics)
    synth.allNotesOff();
    require(synth.activeVoiceCount() == 0, "All notes off clears voices");

    synth.setModelType(PhysicalModelType::modalMembrane);
    synth.setExciterType(ExciterType::mallet);
    synth.setDecayTimeSeconds(0.8f);

    synth.noteOn(48, 0.9f);
    require(synth.activeVoiceCount() == 1, "Membrane voice active");

    synth.process(ctx);
    float peakMembrane = 0.0f;
    for (std::size_t i = 0; i < blockSize; ++i) {
        peakMembrane = std::max(peakMembrane, std::abs(chL[i]));
    }
    require(peakMembrane > 0.05f, "Modal membrane resonator produces drum strike acoustics");

    // 3. Tuned Acoustic Marimba / Vibraphone Bar
    synth.setModelType(PhysicalModelType::modalBar);
    synth.noteOn(72, 0.95f);
    synth.process(ctx);
    require(!std::isnan(chL[0]), "Modal bar synthesis is numerically stable");

    synth.allNotesOff();
}

void testMacroModulationMatrixSuite()
{
    using dawg::engine::dsp::MacroModulationMatrix;
    using dawg::engine::dsp::ModSource;
    using dawg::engine::dsp::MsegNode;
    using dawg::engine::dsp::MsegLoopMode;
    using dawg::engine::dsp::ModulationRoute;

    MacroModulationMatrix matrix;
    matrix.prepare(48000.0);

    // 1. Macro knobs (8 macros)
    matrix.setMacro(0, 0.75f);
    matrix.setMacro(7, 0.25f);
    require(matrix.macro(0) == 0.75f, "Macro 0 set to 0.75");
    require(matrix.macro(7) == 0.25f, "Macro 7 set to 0.25");

    // 2. Multi-Stage Envelope (MSEG) with tension
    std::vector<MsegNode> customNodes = {
        {0.0f,  0.0f,  0.0f},
        {0.3f,  1.0f,  0.5f},  // Ease-out curve
        {0.7f,  0.2f, -0.5f},  // Ease-in curve
        {1.0f,  0.0f,  0.0f}
    };
    matrix.setMsegNodes(customNodes);
    matrix.setMsegLoopMode(MsegLoopMode::loop);
    matrix.setMsegRateHz(2.0f);
    matrix.triggerMseg();

    require(matrix.msegLoopMode() == MsegLoopMode::loop, "MSEG loop mode verified");
    require(matrix.msegRateHz() == 2.0f, "MSEG rate 2 Hz");

    // 3. Lorenz Strange Attractor
    matrix.setLorenzSpeed(2.5f);
    require(matrix.lorenzSpeed() == 2.5f, "Lorenz speed set to 2.5 Hz");

    // Process 256 frames of audio to update modulators
    std::vector<float> dummy(256, 0.5f);
    const float* const channels[1] = {dummy.data()};
    matrix.process(channels, 1, 256);

    const float lorenzX = matrix.lorenzX();
    const float lorenzY = matrix.lorenzY();
    require(lorenzX >= 0.0f && lorenzX <= 1.0f, "Lorenz X normalized within [0, 1]");
    require(lorenzY >= 0.0f && lorenzY <= 1.0f, "Lorenz Y normalized within [0, 1]");

    // 4. Bipolar Modulation Routing & Modulation Ring Visualizer
    matrix.clearRoutes();
    constexpr std::uint32_t filterCutoffParamId = 42;

    ModulationRoute r1 {
        .targetParamId = filterCutoffParamId,
        .source = ModSource::macro1,
        .depth = 0.40f, // +40% modulation
        .minClamp = 0.0f,
        .maxClamp = 1.0f,
        .smoothedValue = 0.0f,
        .slewMs = 1.0f
    };
    require(matrix.addRoute(r1), "Added Macro1 modulation route");
    require(matrix.routeCount() == 1, "Route count is 1");

    // Process a block to settle slew filter
    for (int i = 0; i < 5; ++i) {
        matrix.process(channels, 1, 256);
    }

    constexpr float baseCutoff = 0.50f;
    const float modulated = matrix.evaluateModulatedParam(filterCutoffParamId, baseCutoff);
    require(modulated > baseCutoff, "Macro1 positively modulates filter cutoff");

    const auto ring = matrix.getModulationRing(filterCutoffParamId, baseCutoff);
    require(ring.baseValue == baseCutoff, "Ring base value matches");
    require(ring.arcMax > baseCutoff, "Ring arc max covers positive modulation range");

    // 5. MIDI MPE Expression Routing
    std::array<dawg::engine::MidiEvent, 2> midiEvents {{
        dawg::engine::MidiEvent {
            .frameOffset = 0,
            .data = {std::byte {0xD0}, std::byte {96}, std::byte {0}}, // Channel pressure
            .size = 2
        },
        dawg::engine::MidiEvent {
            .frameOffset = 0,
            .data = {std::byte {0xE0}, std::byte {0x00}, std::byte {0x60}}, // Pitch bend
            .size = 3
        }
    }};
    dawg::engine::MidiBufferView midiView {
        .events = midiEvents.data(),
        .eventCount = midiEvents.size()
    };

    matrix.processMidi(midiView);
    require(matrix.getSourceValue(ModSource::mpePressure) > 0.7f, "MPE Pressure updated via MIDI event");
    require(matrix.getSourceValue(ModSource::mpePitchBend) > 0.1f, "MPE Pitch Bend updated via MIDI event");
}

void testLiveLooperSuite()
{
    using dawg::engine::dsp::LiveLooperNode;
    using dawg::engine::dsp::LooperState;
    using dawg::engine::dsp::OverdubMode;

    LiveLooperNode looper;
    constexpr double sampleRate = 48000.0;
    constexpr std::size_t blockSize = 256;

    require(looper.prepare(sampleRate, blockSize, 2), "LiveLooperNode prepared");
    require(looper.latencySamples() == 0, "Zero latency looper node");

    // 1. Initial Loop Recording
    looper.triggerRecord();
    require(looper.state() == LooperState::recording, "Looper in recording state");

    std::vector<float> chL(blockSize, 0.0f);
    std::vector<float> chR(blockSize, 0.0f);
    std::array<float*, 2> channels {chL.data(), chR.data()};
    dawg::engine::AudioBufferView audioView(channels.data(), 2, blockSize);
    dawg::engine::AudioBufferView emptySidechain(nullptr, 0, 0);
    dawg::engine::ProcessContext ctx {
        .audio = audioView,
        .midi = {},
        .sampleRate = sampleRate,
        .sidechain = emptySidechain
    };

    // Feed 4 blocks (1024 samples) of a 400 Hz test tone
    for (int b = 0; b < 4; ++b) {
        for (std::size_t i = 0; i < blockSize; ++i) {
            const float t = static_cast<float>(b * blockSize + i) / static_cast<float>(sampleRate);
            chL[i] = 0.7f * std::sin(2.0f * std::numbers::pi_v<float> * 400.0f * t);
            chR[i] = chL[i];
        }
        looper.process(ctx);
    }

    // 2. Close Loop and enter Playback
    looper.triggerPlay();
    require(looper.state() == LooperState::playing, "Looper switched to playing state");
    require(looper.loopLengthSamples() == 1024, "Loop length is exactly 1024 samples");
    require(looper.canUndo(), "Undo available for first recorded layer");

    // Feed silence: output should play back loop audio
    std::fill(chL.begin(), chL.end(), 0.0f);
    std::fill(chR.begin(), chR.end(), 0.0f);
    looper.process(ctx);

    float peakLoop = 0.0f;
    for (std::size_t i = 0; i < blockSize; ++i) {
        peakLoop = std::max(peakLoop, std::abs(chL[i]));
    }
    require(peakLoop > 0.4f, "Looper reproduces recorded audio from buffer");

    // 3. Sound-on-Sound Overdubbing
    looper.setOverdubMode(OverdubMode::add);
    looper.setTapeDecay(0.9f);
    looper.triggerOverdub();
    require(looper.state() == LooperState::overdubbing, "Looper in overdubbing state");

    // Add high frequency 2 kHz layer
    for (std::size_t i = 0; i < blockSize; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(sampleRate);
        chL[i] = 0.5f * std::sin(2.0f * std::numbers::pi_v<float> * 2000.0f * t);
        chR[i] = chL[i];
    }
    looper.process(ctx);
    looper.triggerPlay();

    // 4. Undo and Redo
    require(looper.canUndo(), "Undo available after overdub layer");
    looper.undo();
    require(looper.canRedo(), "Redo available after undo");
    looper.redo();
    require(!looper.canRedo(), "Redo consumed");

    // 5. Variable Speed, Reverse & Freeze
    looper.setSpeed(0.5f);
    require(looper.speed() == 0.5f, "Half-speed playback active");
    looper.setSpeed(-1.0f);
    require(looper.speed() == -1.0f, "Reverse playback active");
    looper.setFreeze(true);
    require(looper.isFrozen(), "Buffer freeze active");

    looper.clear();
    require(looper.state() == LooperState::stopped, "Looper cleared and stopped");
    require(looper.loopLengthSamples() == 0, "Loop length 0 after clear");
}

void testTransientSplitGranularResynthesizerSuite()
{
    using dawg::engine::dsp::TransientSplitGranularResynthesizerNode;
    using dawg::engine::dsp::GranularWindowType;
    using dawg::engine::dsp::PitchScatterMode;

    TransientSplitGranularResynthesizerNode gran;
    constexpr double sampleRate = 48000.0;
    constexpr std::size_t blockSize = 256;

    require(gran.prepare(sampleRate, blockSize, 2), "TransientSplitGranularResynthesizerNode prepared");
    require(gran.latencySamples() == 0, "Zero latency granular resynthesizer");

    // 1. Parameter configuration
    gran.setTransientSensitivity(0.7f);
    gran.setGrainSizeMs(40.0f);
    gran.setGrainDensity(45.0f);
    gran.setPitchScatterMode(PitchScatterMode::fifthsAndOctaves);
    gran.setPitchScatterAmount(0.8f);
    gran.setSpatialPanJitter(0.6f);
    gran.setWindowShape(GranularWindowType::blackman);
    gran.setFeedback(0.35f);
    gran.setMix(1.0f);

    require(gran.transientSensitivity() == 0.7f, "Transient sensitivity set");
    require(gran.grainSizeMs() == 40.0f, "Grain size set to 40 ms");
    require(gran.windowShape() == GranularWindowType::blackman, "Window set to Blackman");

    // 2. Processing with audio input
    std::vector<float> chL(blockSize, 0.0f);
    std::vector<float> chR(blockSize, 0.0f);
    std::array<float*, 2> channels {chL.data(), chR.data()};
    dawg::engine::AudioBufferView audioView(channels.data(), 2, blockSize);
    dawg::engine::AudioBufferView emptySidechain(nullptr, 0, 0);
    dawg::engine::ProcessContext ctx {
        .audio = audioView,
        .midi = {},
        .sampleRate = sampleRate,
        .sidechain = emptySidechain
    };

    // Feed a sharp percussive spike followed by sustained tone
    for (int b = 0; b < 10; ++b) {
        for (std::size_t i = 0; i < blockSize; ++i) {
            const float t = static_cast<float>(b * blockSize + i) / static_cast<float>(sampleRate);
            float s = 0.5f * std::sin(2.0f * std::numbers::pi_v<float> * 440.0f * t);
            if (b == 0 && i < 16) {
                s += 0.9f; // Fast transient attack
            }
            chL[i] = s;
            chR[i] = s;
        }
        gran.process(ctx);
    }

    require(!std::isnan(chL[0]) && !std::isinf(chL[0]), "Granular cloud resynthesis is numerically stable");
}

void testHardwareControlSurfaceEngineSuite()
{
    using dawg::engine::HardwareControlSurfaceEngine;
    using dawg::engine::ControllerProfile;
    using dawg::engine::GridPadState;
    using dawg::engine::GridOperatingMode;
    using dawg::engine::ParameterPickupMode;
    using dawg::engine::MidiEvent;

    HardwareControlSurfaceEngine surface;

    // 1. Controller Profile & Clip Matrix States
    surface.setProfile(ControllerProfile::launchpadPro);
    require(surface.profile() == ControllerProfile::launchpadPro, "Controller set to Launchpad Pro");

    surface.setClipState(0, 0, GridPadState::playing);
    surface.setClipState(0, 1, GridPadState::recording);
    surface.setClipState(0, 2, GridPadState::queuedPlay);
    surface.setClipState(0, 3, GridPadState::selected);

    require(surface.clipState(0, 0) == GridPadState::playing, "Pad 0,0 is playing");
    require(surface.getVelocityColorIndex(0, 0) == 21, "Playing pad has green color index 21");
    require(surface.getVelocityColorIndex(0, 1) == 5, "Recording pad has red color index 5");

    // 2. SysEx RGB Packet Generation (Novation Launchpad Pro Spec)
    const auto sysex = surface.generatePadSysEx(0, 0);
    require(sysex.size() == 12, "SysEx packet is 12 bytes");
    require(sysex.front() == 0xF0 && sysex.back() == 0xF7, "Valid SysEx delimiters F0..F7");
    require(sysex[1] == 0x00 && sysex[2] == 0x20 && sysex[3] == 0x29, "Novation manufacturer ID 00 20 29");
    require(sysex[6] == 0x0B, "RGB LED command 0B");

    // 3. Step Sequencer Mode & Playhead Sweep
    surface.setOperatingMode(GridOperatingMode::stepSequencer);
    require(surface.operatingMode() == GridOperatingMode::stepSequencer, "Step sequencer operating mode");

    surface.setStepSequencerPlayhead(4);
    require(surface.stepSequencerPlayhead() == 4, "Active step is 4");

    const auto cursorColor = surface.getPadLedColor(0, 4);
    require(cursorColor.r == 255 && cursorColor.g == 255 && cursorColor.b == 255, "Step cursor glows white");

    // 4. Note Performance Mode Isomorphic Layout
    surface.setOperatingMode(GridOperatingMode::notePerformance);
    const int rootPadNote = surface.getNoteForPad(7, 0, 60); // Bottom left pad
    require(rootPadNote == 60, "Bottom left pad is Middle C (60)");

    const int rowAboveNote = surface.getNoteForPad(6, 0, 60); // Row above (4th layout)
    require(rowAboveNote == 65, "Row above is a perfect 4th (+5 semitones)");

    // 5. Hardware Encoder Banking & Catch Pickup Mode
    surface.setPickupMode(ParameterPickupMode::pickup);
    surface.setVirtualParameter(0, 0.75f);
    require(!surface.isEncoderCaught(0), "Encoder lost catch lock after DAW virtual jump");

    // Moving physical knob at 0.20f should NOT alter the virtual 0.75f value
    float val = surface.processPhysicalEncoderMove(0, 0.20f);
    require(std::abs(val - 0.75f) < 0.001f, "Catch mode ignores hardware move before crossing virtual value");
    require(!surface.isEncoderCaught(0), "Encoder still uncaught");

    // Moving physical knob through 0.75f catches the parameter!
    val = surface.processPhysicalEncoderMove(0, 0.76f);
    require(surface.isEncoderCaught(0), "Encoder caught when physical value crosses virtual value");
    require(std::abs(val - 0.76f) < 0.001f, "Virtual value now follows physical knob seamlessly");

    // 6. Inbound MIDI Event Parsing
    MidiEvent padEv {
        .frameOffset = 0,
        .data = {std::byte {0x90}, std::byte {36}, std::byte {100}}, // Note On pad 36
        .size = 3
    };
    HardwareControlSurfaceEngine::HardwareEvent parsed;
    require(surface.parseMidiInput(padEv, parsed), "Parsed pad press MIDI message");
    require(parsed.type == HardwareControlSurfaceEngine::HardwareEvent::Type::padPress, "Type is padPress");
    require(parsed.index == 36, "Pad index 36");

    MidiEvent ccEv {
        .frameOffset = 0,
        .data = {std::byte {0xB0}, std::byte {21}, std::byte {64}}, // CC 21 encoder 0
        .size = 3
    };
    require(surface.parseMidiInput(ccEv, parsed), "Parsed encoder turn MIDI message");
    require(parsed.type == HardwareControlSurfaceEngine::HardwareEvent::Type::encoderTurn, "Type is encoderTurn");
    require(parsed.index == 0, "Encoder index 0");

    // 7. Mackie Control Universal (MCU) Protocol Tests
    surface.setProfile(ControllerProfile::mackieControlUniversal);
    // Outbound motorized fader (14-bit pitch bend)
    const auto mcuFader0 = HardwareControlSurfaceEngine::generateMcuFaderMessage(0, 0.5f);
    require(mcuFader0[0] == 0xE0, "Channel 0 pitch bend status 0xE0");
    const int val14 = static_cast<int>(mcuFader0[1]) | (static_cast<int>(mcuFader0[2]) << 7);
    require(std::abs(val14 - 8192) <= 1, "Fader 0.5 midpoint mapped to 14-bit center");

    // Inbound motorized fader parse
    MidiEvent mcuFaderInEv {
        .frameOffset = 0,
        .data = {std::byte {0xE2}, std::byte {0x00}, std::byte {0x40}}, // Ch 2 Pitch Bend 8192
        .size = 3
    };
    require(surface.parseMidiInput(mcuFaderInEv, parsed), "Parsed MCU fader move");
    require(parsed.type == HardwareControlSurfaceEngine::HardwareEvent::Type::mcuFaderMove, "Type is mcuFaderMove");
    require(parsed.index == 2, "Channel 2 fader index");
    require(std::abs(parsed.value - 0.5f) < 0.01f, "Fader value 0.5f");

    // MCU V-Pot LED Ring generation (boost/cut mode)
    const auto vpotLed = HardwareControlSurfaceEngine::generateMcuVpotLedMessage(3, dawg::engine::McuVpotLedMode::boostCut, 0.5f);
    require(vpotLed[0] == 0xB0 && vpotLed[1] == 0x33, "CC 0x33 for channel 3 V-pot LED ring");
    require((vpotLed[2] & 0x30) == 0x10, "Boost/Cut mode bits 0x10 set");

    // Inbound V-Pot relative turn (CC 0x10..0x17)
    MidiEvent vpotTurnEv {
        .frameOffset = 0,
        .data = {std::byte {0xB0}, std::byte {0x14}, std::byte {0x42}}, // Ch 4 V-Pot, -2 ticks
        .size = 3
    };
    require(surface.parseMidiInput(vpotTurnEv, parsed), "Parsed MCU V-Pot turn");
    require(parsed.type == HardwareControlSurfaceEngine::HardwareEvent::Type::mcuVpotTurn, "Type is mcuVpotTurn");
    require(parsed.index == 4, "V-Pot channel 4");
    require(parsed.value == -2.0f, "V-pot turn direction was -2 ticks");

    // MCU Jog Wheel (CC 0x3C)
    MidiEvent jogEv {
        .frameOffset = 0,
        .data = {std::byte {0xB0}, std::byte {0x3C}, std::byte {0x03}}, // +3 ticks
        .size = 3
    };
    require(surface.parseMidiInput(jogEv, parsed), "Parsed MCU jog wheel");
    require(parsed.type == HardwareControlSurfaceEngine::HardwareEvent::Type::mcuJogWheel, "Type is mcuJogWheel");
    require(parsed.value == 3.0f, "Jog wheel scrub was +3 ticks");

    // MCU Transport buttons
    MidiEvent playEv {
        .frameOffset = 0,
        .data = {std::byte {0x90}, std::byte {0x5E}, std::byte {127}}, // Play pressed
        .size = 3
    };
    require(surface.parseMidiInput(playEv, parsed), "Parsed MCU Play transport button");
    require(parsed.type == HardwareControlSurfaceEngine::HardwareEvent::Type::mcuTransport, "Type is mcuTransport");
    require(parsed.index == 0x5E && parsed.value == 1.0f, "Play button active");

    // MCU Scribble Strip SysEx (56 chars padded)
    const auto scribble = HardwareControlSurfaceEngine::generateMcuScribbleSysEx(0, "VOX 1  DRUMS  BASS");
    require(scribble.size() == 64, "SysEx length is 64 bytes (7 header + 56 text + 1 EOX)");
    require(scribble.front() == 0xF0 && scribble.back() == 0xF7, "Valid SysEx wrapper");
    require(scribble[6] == 0x00, "Line 0 offset is 0x00");

    // 8. HUI Protocol Tests
    const auto huiSwitch = HardwareControlSurfaceEngine::generateHuiSwitchMessage(2, 4, true);
    require(huiSwitch[0] == 0xB0 && huiSwitch[1] == 0x0C && huiSwitch[2] == 0x02, "HUI zone message header");
    require(huiSwitch[3] == 0xB0 && huiSwitch[4] == 0x2C && huiSwitch[5] == (0x04 | 0x40), "HUI port + pressed switch bit");

    const auto huiFader = HardwareControlSurfaceEngine::generateHuiFaderMessage(1, 0.5f);
    require(huiFader[0] == 0xB0 && huiFader[1] == 0x01, "HUI Fader 1 MSB CC header");
    require(huiFader[3] == 0xB0 && huiFader[4] == 0x21, "HUI Fader 1 LSB CC header");

    MidiEvent huiInEv {
        .frameOffset = 0,
        .data = {std::byte {0xB0}, std::byte {0x03}, std::byte {100}}, // Ch 3 MSB
        .size = 3
    };
    require(surface.parseHuiMidiInput(huiInEv, parsed), "Parsed HUI fader input");
    require(parsed.type == HardwareControlSurfaceEngine::HardwareEvent::Type::huiFaderMove, "Type is huiFaderMove");
    require(parsed.index == 3, "HUI fader channel 3");
}

void testHardwareControlSurfaceManagerSuite()
{
    using dawg::engine::HardwareControlSurfaceManager;
    using dawg::engine::ControllerProfile;
    using dawg::engine::SurfaceConfig;
    using dawg::engine::ChannelStripFeedback;
    using dawg::engine::McuTransportButton;

    // 1. Device Profile Auto-Detection
    require(HardwareControlSurfaceManager::autoDetectProfile("Behringer X-Touch Universal") == ControllerProfile::mackieControlUniversal, "Auto-detected X-Touch as MCU");
    require(HardwareControlSurfaceManager::autoDetectProfile("iCon QCon Pro G2") == ControllerProfile::mackieControlUniversal, "Auto-detected QCon as MCU");
    require(HardwareControlSurfaceManager::autoDetectProfile("Mackie Control Surface") == ControllerProfile::mackieControlUniversal, "Auto-detected Mackie Control");
    require(HardwareControlSurfaceManager::autoDetectProfile("Avid HUI") == ControllerProfile::hui, "Auto-detected HUI");
    require(HardwareControlSurfaceManager::autoDetectProfile("Novation Launchpad Pro MK3") == ControllerProfile::launchpadPro, "Auto-detected Launchpad Pro");
    require(HardwareControlSurfaceManager::autoDetectProfile("Ableton Push 2 User Port") == ControllerProfile::push2, "Auto-detected Push 2");
    require(HardwareControlSurfaceManager::autoDetectProfile("Akai APC40 mkII") == ControllerProfile::apc40Mk2, "Auto-detected APC40");

    // 2. Hardware Manager State Sync Loop
    HardwareControlSurfaceManager manager;
    SurfaceConfig cfg {};
    cfg.profile = ControllerProfile::mackieControlUniversal;
    cfg.enabled = true;
    manager.setConfig(cfg);

    std::vector<std::vector<std::uint8_t>> sentPackets;
    manager.setMidiSendCallback([&sentPackets](const std::uint8_t* data, std::size_t size) {
        sentPackets.emplace_back(data, data + size);
    });

    // Update track 0 state
    ChannelStripFeedback trk0 {};
    trk0.volume = 0.75f;
    trk0.pan = 0.5f;
    trk0.trackName = "Vocal";
    manager.updateChannelStrip(0, trk0);

    // Run sync cycle
    manager.syncCycle();

    require(!sentPackets.empty(), "Sync cycle transmitted outbound MIDI feedback");

    // Check fader packet
    bool foundFader = false;
    for (const auto& pkt : sentPackets)
    {
        if (pkt.size() == 3 && pkt[0] == 0xE0) // Pitch bend channel 0
        {
            foundFader = true;
            break;
        }
    }
    require(foundFader, "Fader motorized feedback packet sent for track 0");

    // Check scribble strip SysEx
    bool foundScribble = false;
    for (const auto& pkt : sentPackets)
    {
        if (pkt.size() >= 7 && pkt[0] == 0xF0 && pkt[1] == 0x00 && pkt[2] == 0x00 && pkt[3] == 0x66)
        {
            foundScribble = true;
            break;
        }
    }
    require(foundScribble, "Scribble strip LCD SysEx packet sent containing track name");

    // 3. Inbound MIDI event routing
    std::size_t movedFaderChannel = 999;
    float movedFaderVal = -1.0f;
    manager.setFaderMovedCallback([&](std::size_t ch, float val) {
        movedFaderChannel = ch;
        movedFaderVal = val;
    });

    // Simulated pitch bend for channel 1 fader (status 0xE1, MSB 0x40 -> midpoint ~0.5)
    const std::uint8_t faderMidi[3] = {0xE1, 0x00, 0x40};
    manager.handleIncomingMidi(faderMidi, 3);
    require(movedFaderChannel == 1, "Inbound MCU fader routed to channel 1 callback");
    require(std::abs(movedFaderVal - 0.5f) < 0.01f, "Fader value normalized correctly");

    // Simulated Transport button (Play Note On 0x5E)
    McuTransportButton transportBtn = McuTransportButton::stop;
    bool transportActive = false;
    manager.setTransportCallback([&](McuTransportButton btn, bool active) {
        transportBtn = btn;
        transportActive = active;
    });

    const std::uint8_t playMidi[3] = {0x90, 0x5E, 127};
    manager.handleIncomingMidi(playMidi, 3);
    require(transportBtn == McuTransportButton::play && transportActive, "Inbound Play button routed to transport callback");

    // 4. Background Sync Thread Lifecycle
    manager.startSyncLoop(std::chrono::milliseconds(10));
    require(manager.isSyncing(), "Sync loop thread active");
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    manager.stopSyncLoop();
    require(!manager.isSyncing(), "Sync loop thread safely terminated");
}

void testAcousticDrumBleedCancellationSuite()
{
    using dawg::engine::dsp::AcousticDrumBleedCancellationNode;
    using dawg::engine::dsp::DrumType;

    AcousticDrumBleedCancellationNode gate;
    constexpr double sampleRate = 48000.0;
    constexpr std::size_t blockSize = 256;

    require(gate.prepare(sampleRate, blockSize, 2), "AcousticDrumBleedCancellationNode prepared");

    // 1. Drum Type Profiles
    gate.setDrumType(DrumType::snare);
    require(gate.drumType() == DrumType::snare, "Drum type set to snare");
    gate.setDrumType(DrumType::kick);
    require(gate.drumType() == DrumType::kick, "Drum type set to kick");
    gate.setDrumType(DrumType::snare);

    // 2. Microphone Phase Alignment & Correlation
    gate.setPhaseAlignmentDelayMs(1.5f);
    gate.setPolarityInverted(true);
    require(gate.phaseAlignmentDelayMs() == 1.5f, "Phase delay set to 1.5 ms");
    require(gate.isPolarityInverted(), "Polarity inverted");

    std::vector<float> trkA(blockSize);
    std::vector<float> trkB(blockSize);
    std::vector<float> trkInv(blockSize);
    for (std::size_t i = 0; i < blockSize; ++i) {
        trkA[i] = std::sin(static_cast<float>(i) * 0.1f);
        trkB[i] = trkA[i];
        trkInv[i] = -trkA[i];
    }
    const float corrIdentical = gate.calculatePhaseCorrelation(trkA.data(), trkB.data(), blockSize);
    const float corrInverted = gate.calculatePhaseCorrelation(trkA.data(), trkInv.data(), blockSize);
    require(std::abs(corrIdentical - 1.0f) < 0.01f, "Identical tracks yield correlation +1.0");
    require(std::abs(corrInverted - (-1.0f)) < 0.01f, "Phase inverted tracks yield correlation -1.0");

    // 3. Transient Spill De-Bleeding
    gate.setPolarityInverted(false);
    gate.setPhaseAlignmentDelayMs(0.0f);
    gate.setGateThresholdDb(-20.0f);
    gate.setSpillReductionDb(-24.0f);

    std::vector<float> chL(blockSize, 0.0f);
    std::vector<float> chR(blockSize, 0.0f);
    std::array<float*, 2> channels {chL.data(), chR.data()};
    dawg::engine::AudioBufferView audioView(channels.data(), 2, blockSize);
    dawg::engine::AudioBufferView emptySidechain(nullptr, 0, 0);
    dawg::engine::ProcessContext ctx {
        .audio = audioView,
        .midi = {},
        .sampleRate = sampleRate,
        .sidechain = emptySidechain
    };

    // Feed quiet hi-hat cymbal spill (8 kHz at -35 dBFS)
    for (int b = 0; b < 4; ++b) {
        for (std::size_t i = 0; i < blockSize; ++i) {
            const float t = static_cast<float>(b * blockSize + i) / static_cast<float>(sampleRate);
            chL[i] = 0.02f * std::sin(2.0f * std::numbers::pi_v<float> * 8000.0f * t);
            chR[i] = chL[i];
        }
        gate.process(ctx);
    }
    require(!gate.isGateOpen(), "Gate remains closed on quiet cymbal spill");
    require(gate.currentGateAttenuationDb() < -10.0f, "Spill reduction active during bleed");

    // Feed loud snare hit (2.2 kHz at -6 dBFS)
    for (std::size_t i = 0; i < blockSize; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(sampleRate);
        chL[i] = 0.5f * std::sin(2.0f * std::numbers::pi_v<float> * 2200.0f * t);
        chR[i] = chL[i];
    }
    gate.process(ctx);
    require(gate.isGateOpen(), "Gate snaps open on snare attack transient");

    // 4. Sample Augmentation
    std::vector<float> sampleHit(1000, 0.3f);
    gate.loadAugmentationSample(sampleHit.data(), sampleHit.size());
    gate.setSampleBlend(0.5f);
    require(gate.sampleBlend() == 0.5f, "Sample blend set to 50%");
}

void testSpectralMorphCrossSynthSuite()
{
    using dawg::engine::dsp::SpectralMorphCrossSynthNode;
    using dawg::engine::dsp::SpectralMorphMode;

    SpectralMorphCrossSynthNode morph;
    constexpr double sampleRate = 48000.0;
    constexpr std::size_t blockSize = 256;

    require(morph.prepare(sampleRate, blockSize, 2), "SpectralMorphCrossSynthNode prepared");
    require(morph.latencySamples() == SpectralMorphCrossSynthNode::FftSize, "STFT latency reported");

    morph.setMorphX(0.7f);
    morph.setFormantWarpY(0.6f);
    morph.setMorphMode(SpectralMorphMode::crossMultiply);

    require(morph.morphX() == 0.7f, "Morph X set to 0.7");
    require(morph.formantWarpY() == 0.6f, "Formant warp Y set to 0.6");
    require(morph.morphMode() == SpectralMorphMode::crossMultiply, "Morph mode crossMultiply");

    // Feed Stream A (Main 400 Hz) and Stream B (Sidechain 1000 Hz)
    std::vector<float> mainL(blockSize);
    std::vector<float> mainR(blockSize);
    std::vector<float> scL(blockSize);
    std::vector<float> scR(blockSize);

    std::array<float*, 2> mainCh {mainL.data(), mainR.data()};
    std::array<float*, 2> scCh {scL.data(), scR.data()};
    dawg::engine::AudioBufferView mainView(mainCh.data(), 2, blockSize);
    dawg::engine::AudioBufferView scView(scCh.data(), 2, blockSize);
    dawg::engine::ProcessContext ctx {
        .audio = mainView,
        .midi = {},
        .sampleRate = sampleRate,
        .sidechain = scView
    };

    for (int b = 0; b < 8; ++b) {
        for (std::size_t i = 0; i < blockSize; ++i) {
            const float t = static_cast<float>(b * blockSize + i) / static_cast<float>(sampleRate);
            mainL[i] = 0.6f * std::sin(2.0f * std::numbers::pi_v<float> * 400.0f * t);
            mainR[i] = mainL[i];
            scL[i] = 0.6f * std::sin(2.0f * std::numbers::pi_v<float> * 1000.0f * t);
            scR[i] = scL[i];
        }
        morph.process(ctx);
    }

    require(!std::isnan(mainL[0]) && !std::isinf(mainL[0]), "STFT cross-synthesis produces stable output");
}

void testSpatialHeadTrackerBridgeSuite()
{
    using dawg::engine::dsp::SpatialHeadTrackerBridge;
    using dawg::engine::dsp::Vector3D;
    using dawg::engine::dsp::StudioControlRoomSimulatorNode;
    using dawg::engine::dsp::BinauralSpatialPannerNode;

    SpatialHeadTrackerBridge tracker;
    tracker.reset();

    // 1. Direct Orientation & Smoothing
    tracker.updateRawOrientation(45.0f, 10.0f, -5.0f);
    require(std::abs(tracker.rawOrientation().yawDeg - 45.0f) < 0.01f, "Raw yaw updated to 45 deg");

    // 2. OSC Message Parsing
    const float ypr[3] = {30.0f, -15.0f, 8.0f};
    require(tracker.parseOscMessage("/tracking/ypr", ypr, 3), "Parsed /tracking/ypr OSC message");

    const float singleYaw[1] = {-60.0f};
    require(tracker.parseOscMessage("/tracking/yaw", singleYaw, 1), "Parsed /tracking/yaw OSC message");

    // 3. Sweet-Spot Calibration
    tracker.calibrateZeroReference();
    const auto calib = tracker.calibratedOrientation();
    require(std::abs(calib.yawDeg) < 0.01f, "Calibrated yaw zeroed after sweet-spot calibration");
    require(std::abs(calib.pitchDeg) < 0.01f, "Calibrated pitch zeroed");

    // 4. 3D Coordinate Rotation Transform:
    // Listener turns head 90 degrees right (yaw = +90).
    // An object directly in front at (x: 0, y: 2, z: 0) should appear at left ear (x: -2, y: 0, z: 0)!
    tracker.reset();
    tracker.setSmoothingFactor(0.0f); // instant response for test
    tracker.updateRawOrientation(90.0f, 0.0f, 0.0f);

    Vector3D worldFront {0.0f, 2.0f, 0.0f};
    Vector3D headSpace = tracker.transformWorldToHeadSpace(worldFront);
    require(std::abs(headSpace.x - (-2.0f)) < 0.05f, "World front object rotates to left ear (-2.0) on 90 deg right head turn");
    require(std::abs(headSpace.y - 0.0f) < 0.05f, "Y coordinate is near zero");

    // 5. Direct Node Integration Bindings
    StudioControlRoomSimulatorNode room;
    require(room.prepare(48000.0, 256, 2), "Room prepared");
    tracker.updateRawOrientation(30.0f, 0.0f, 0.0f);
    tracker.applyToControlRoomSimulator(room);
    require(std::abs(room.headYawDegrees() - 30.0f) < 0.01f, "Head tracker yaw applied to control room simulator");

    BinauralSpatialPannerNode panner;
    require(panner.prepare(48000.0, 256, 2), "Panner prepared");
    tracker.updateRawOrientation(90.0f, 0.0f, 0.0f);
    tracker.applyToBinauralPanner(panner, worldFront);
    require(std::abs(panner.azimuthDeg() - (-90.0f)) < 1.0f, "Panner azimuth rotated to -90 deg");
}

void testSpringReverbMagneticDrumEchoSuite()
{
    using dawg::engine::dsp::SpringReverbMagneticDrumEchoNode;
    using dawg::engine::dsp::SpringDrumMode;

    SpringReverbMagneticDrumEchoNode node;
    require(node.prepare(48000.0, 512, 2), "SpringReverbMagneticDrumEchoNode prepared successfully");

    // 1. Verify default parameters & setters
    node.setMode(SpringDrumMode::seriesDrumToSpring);
    require(node.mode() == SpringDrumMode::seriesDrumToSpring, "Mode set to seriesDrumToSpring");
    node.setPreampDriveDb(12.0f);
    require(std::abs(node.preampDriveDb() - 12.0f) < 0.1f, "Preamp drive set to 12 dB");
    node.setSpringDecay(3.0f);
    require(std::abs(node.springDecay() - 3.0f) < 0.1f, "Spring decay set to 3.0s");
    node.setSpringDispersion(0.85f);
    require(std::abs(node.springDispersion() - 0.85f) < 0.05f, "Spring dispersion set to 0.85");
    node.setSpringRattle(0.5f);
    require(std::abs(node.springRattle() - 0.5f) < 0.05f, "Spring rattle set to 0.5");
    node.setDrumSpeedMs(250.0f);
    require(std::abs(node.drumSpeedMs() - 250.0f) < 0.1f, "Drum speed set to 250 ms");
    node.setFeedback(0.70f);
    require(std::abs(node.feedback() - 0.70f) < 0.05f, "Feedback set to 0.70");

    // Head config tests
    node.setHeadEnabled(0, true);
    node.setHeadEnabled(1, true);
    node.setHeadEnabled(2, false);
    node.setHeadEnabled(3, true);
    require(node.isHeadEnabled(0) && node.isHeadEnabled(1) && !node.isHeadEnabled(2) && node.isHeadEnabled(3), "Heads 1, 2, 4 enabled, head 3 disabled");

    // 2. Dispersive Spring Reverb chirp test (Impulse response)
    node.setMode(SpringDrumMode::springOnly);
    node.setMix(1.0f); // 100% wet
    node.setPreampDriveDb(0.0f); // Clean

    std::vector<float> impulseL(512, 0.0f);
    std::vector<float> impulseR(512, 0.0f);
    impulseL[0] = 1.0f; // Single impulse
    impulseR[0] = 1.0f;

    std::array<float*, 2> channels { impulseL.data(), impulseR.data() };
    dawg::engine::ProcessContext ctx {
        .audio = dawg::engine::AudioBufferView { channels.data(), 2, 512 },
        .midi = {},
        .sampleRate = 48000.0,
        .sidechain = {}
    };

    node.process(ctx);

    // Verify impulse was stretched into allpass dispersion tail
    float tailEnergy = 0.0f;
    for (std::size_t i = 1; i < 512; ++i)
    {
        tailEnergy += impulseL[i] * impulseL[i];
    }
    require(tailEnergy > 0.01f, "Dispersive transmission spring allpass stages dispersed single impulse into energy chirp");

    // 3. Multi-tap Magnetic Drum Echo test
    node.reset();
    node.setMode(SpringDrumMode::drumEchoOnly);
    node.setMix(1.0f);
    node.setDrumSpeedMs(100.0f); // 100ms drum revolution = 4800 samples @ 48kHz. Head 1 delay = 1200 samples
    node.setFeedback(0.5f);
    node.setWowFlutter(0.0f);

    std::vector<float> blockL(512, 0.0f);
    std::vector<float> blockR(512, 0.0f);
    blockL[0] = 1.0f;
    blockR[0] = 1.0f;
    channels[0] = blockL.data();
    channels[1] = blockR.data();

    node.process(ctx);

    std::fill(blockL.begin(), blockL.end(), 0.0f);
    std::fill(blockR.begin(), blockR.end(), 0.0f);

    float drumEchoEnergy = 0.0f;
    for (int b = 0; b < 10; ++b)
    {
        node.process(ctx);
        for (std::size_t i = 0; i < 512; ++i)
        {
            drumEchoEnergy += std::abs(blockL[i]);
        }
    }
    require(drumEchoEnergy > 0.05f, "Magnetic drum multi-tap delay heads produce repeated echoes");

    // 4. Runaway self-oscillation safety test (Feedback > 1.0 with magnetic saturation)
    node.reset();
    node.setFeedback(1.25f); // Runaway self-oscillation
    blockL[0] = 0.8f;
    blockR[0] = 0.8f;
    node.process(ctx);

    std::fill(blockL.begin(), blockL.end(), 0.0f);
    std::fill(blockR.begin(), blockR.end(), 0.0f);

    float maxSample = 0.0f;
    for (int b = 0; b < 30; ++b)
    {
        node.process(ctx);
        for (std::size_t i = 0; i < 512; ++i)
        {
            maxSample = std::max(maxSample, std::abs(blockL[i]));
        }
    }
    require(maxSample > 0.05f, "Runaway feedback maintains continuous self-oscillation");
    require(maxSample <= 1.5f, "Magnetic hysteresis saturation safely soft-clips runaway feedback below explosion threshold");
}

void testMasteringStemExportSuite()
{
    using dawg::engine::dsp::MasteringStemExportEngine;
    using dawg::engine::dsp::MasteringTargetProfile;

    MasteringStemExportEngine engine;
    engine.setTargetProfile(MasteringTargetProfile::spotify);
    require(std::abs(engine.targetLufs() - (-14.0f)) < 0.1f, "Spotify target is -14.0 LUFS");
    require(std::abs(engine.ceilingTruePeakDb() - (-1.0f)) < 0.1f, "Spotify ceiling is -1.0 dBTP");

    // 1. Inter-sample true-peak detection test
    constexpr double sampleRate = 48000.0;
    constexpr std::size_t frameCount = 1024;
    std::vector<float> testToneL(frameCount, 0.0f);
    std::vector<float> testToneR(frameCount, 0.0f);

    for (std::size_t i = 0; i < frameCount; ++i)
    {
        const double phase = 2.0 * std::numbers::pi * 12000.0 * static_cast<double>(i) / sampleRate + std::numbers::pi / 4.0;
        testToneL[i] = static_cast<float>(std::sin(phase) * 0.95);
        testToneR[i] = testToneL[i];
    }

    const float truePeak = MasteringStemExportEngine::computeInterSampleTruePeak(testToneL.data(), frameCount);
    require(truePeak > 0.85f, "4x polyphase FIR interpolator detects inter-sample peak beyond sample peak");

    // 2. Full buffer loudness analysis & gating test
    constexpr std::size_t numFrames2s = 96000;
    std::vector<float> calibL(numFrames2s, 0.0f);
    std::vector<float> calibR(numFrames2s, 0.0f);
    for (std::size_t i = 0; i < numFrames2s; ++i)
    {
        const float s = static_cast<float>(std::sin(2.0 * std::numbers::pi * 1000.0 * static_cast<double>(i) / sampleRate) * 0.5);
        calibL[i] = s;
        calibR[i] = s;
    }
    std::array<float*, 2> calibChannels { calibL.data(), calibR.data() };
    dawg::engine::AudioBufferView calibBuffer { calibChannels.data(), 2, numFrames2s };

    const auto telemetry = engine.analyzeBuffer(calibBuffer, sampleRate);
    require(telemetry.integratedLufs > -30.0f && telemetry.integratedLufs < -5.0f, "Measured integrated LUFS within reasonable calibration range");
    require(telemetry.truePeakDb > -10.0f && telemetry.truePeakDb < 0.0f, "True Peak dBTP accurately calculated");

    // 3. Automated Target Normalization to -14 LUFS with True Peak limiting
    const auto result = engine.normalizeBuffer(calibBuffer, sampleRate, "MasterMix");
    require(std::abs(result.finalIntegratedLufs - (-14.0f)) < 1.0f, "Normalized master buffer hits -14 LUFS Spotify target within 1 LU");
    require(result.finalTruePeakDb <= (-1.0f + 0.1f), "Final true peak respects -1.0 dBTP ceiling");

    // 4. Batch Multi-Stem Export with relative balance preservation
    std::vector<float> drumStemL(numFrames2s, 0.1f);
    std::vector<float> drumStemR(numFrames2s, 0.1f);
    std::array<float*, 2> drumCh { drumStemL.data(), drumStemR.data() };
    dawg::engine::AudioBufferView drumView { drumCh.data(), 2, numFrames2s };

    std::vector<float> bassStemL(numFrames2s, 0.2f);
    std::vector<float> bassStemR(numFrames2s, 0.2f);
    std::array<float*, 2> bassCh { bassStemL.data(), bassStemR.data() };
    dawg::engine::AudioBufferView bassView { bassCh.data(), 2, numFrames2s };

    std::vector<dawg::engine::AudioBufferView> stems { drumView, bassView };
    std::vector<std::string> stemNames { "Drums", "Bass" };

    const auto stemReports = engine.normalizeStems(stems, stemNames, sampleRate, true);
    require(stemReports.size() == 2, "2 stem normalization reports generated");
    require(std::abs(stemReports[0].appliedGainDb - stemReports[1].appliedGainDb) < 1e-4f, "Uniform gain applied to preserve relative mix balance across stems");
}

void testEnhancedVocalPitchCorrectionSuite()
{
    using dawg::engine::dsp::VocalPitchCorrectorNode;
    using dawg::engine::dsp::PitchTrajectoryPoint;
    using dawg::engine::dsp::MusicalScale;

    VocalPitchCorrectorNode node;
    require(node.prepare(48000.0, 512, 2), "VocalPitchCorrectorNode prepared");

    node.setScale(MusicalScale::minor);
    node.setRootNote(9); // A Minor
    node.setRetuneSpeed(0.05f); // Pop snappy
    node.setCorrectionAmount(1.0f);
    node.setVibratoProtection(0.85f);
    node.setHumanizeAmount(0.20f);
    node.setFormantPreservation(true);
    node.setFormantShiftSemitones(0.0f);

    require(std::abs(node.vibratoProtection() - 0.85f) < 0.01f, "Vibrato protection set to 0.85");
    require(std::abs(node.humanizeAmount() - 0.20f) < 0.01f, "Humanize amount set to 0.20");
    require(node.isFormantPreservationEnabled(), "Formant preservation enabled");

    // 1. Sibilance detection test
    std::vector<float> sibilantL(512, 0.0f);
    std::vector<float> sibilantR(512, 0.0f);
    float val = 0.5f;
    for (std::size_t i = 0; i < 512; ++i)
    {
        val = -val; // Alternating maximum zero crossings
        sibilantL[i] = val * 0.3f;
        sibilantR[i] = val * 0.3f;
    }
    std::array<float*, 2> channels { sibilantL.data(), sibilantR.data() };
    dawg::engine::ProcessContext ctx {
        .audio = dawg::engine::AudioBufferView { channels.data(), 2, 512 },
        .midi = {},
        .sampleRate = 48000.0,
        .sidechain = {}
    };

    node.process(ctx);
    node.process(ctx);

    const auto sibilantInfo = node.currentPitchInfo();
    require(sibilantInfo.isSibilant || !sibilantInfo.isVoiced, "High frequency aperiodic consonant detected as sibilant / unvoiced");

    // 2. Pitch Trajectory Telemetry Readout
    std::array<PitchTrajectoryPoint, 8> trajectoryPoints {};
    node.getPitchTrajectory(trajectoryPoints.data(), 8);
    require(trajectoryPoints[0].timestampSec >= 0.0f, "Pitch trajectory timestamp recorded");

    // 3. Natural Vibrato Intonation test
    node.reset();
    constexpr double sampleRate = 48000.0;
    std::vector<float> vocalBlockL(512, 0.0f);
    std::vector<float> vocalBlockR(512, 0.0f);
    channels[0] = vocalBlockL.data();
    channels[1] = vocalBlockR.data();

    double carrierPhase = 0.0;
    for (int b = 0; b < 10; ++b)
    {
        for (std::size_t i = 0; i < 512; ++i)
        {
            const double t = static_cast<double>(b * 512 + i) / sampleRate;
            const double vibratoSemitones = 0.30 * std::sin(2.0 * std::numbers::pi * 5.5 * t);
            const double instHz = 440.0 * std::pow(2.0, vibratoSemitones / 12.0);
            carrierPhase += 2.0 * std::numbers::pi * instHz / sampleRate;
            const float s = static_cast<float>(std::sin(carrierPhase) * 0.75);
            vocalBlockL[i] = s;
            vocalBlockR[i] = s;
        }
        node.process(ctx);
    }

    const auto voicedInfo = node.currentPitchInfo();
    require(voicedInfo.isVoiced, "Voiced tone tracked successfully with vibrato protection");
    require(std::abs(voicedInfo.targetMidiPitch - 69.0f) < 0.5f, "Target pitch quantized to A4 (note 69)");
}

void testSpectralVocoderSuite()
{
    using dawg::engine::dsp::SpectralVocoderNode;
    using dawg::engine::dsp::SpectralVocoderCarrierSource;

    SpectralVocoderNode node;
    require(node.prepare(48000.0, 512, 2), "SpectralVocoderNode prepared");
    require(node.latencySamples() == 0, "Vocoder is zero latency");

    // Configure carrier oscillator
    node.setCarrierSource(SpectralVocoderCarrierSource::internalPulse);
    node.setCarrierFrequencyHz(130.81f); // C3
    node.setCarrierPulseWidth(0.40f);
    node.setCarrierDetuneCents(10.0f);
    node.setCarrierSubLevel(0.30f);
    node.setFormantShiftSemitones(2.0f);
    node.setBandQ(6.0f);
    node.setBandSpacing(1.1f);
    node.setEnvelopeAttackMs(2.5f);
    node.setEnvelopeReleaseMs(20.0f);
    node.setMix(1.0f);

    require(std::abs(node.carrierFrequencyHz() - 130.81f) < 0.01f, "Carrier freq set");
    require(std::abs(node.formantShiftSemitones() - 2.0f) < 0.01f, "Formant shift set");
    require(std::abs(node.bandQ() - 6.0f) < 0.01f, "Band Q set");

    // 1. Process voiced speech simulation (harmonic fundamental + formants)
    std::vector<float> speechL(512, 0.0f);
    std::vector<float> speechR(512, 0.0f);
    for (std::size_t i = 0; i < 512; ++i)
    {
        const float t = static_cast<float>(i) / 48000.0f;
        const float s = 0.5f * std::sin(2.0f * static_cast<float>(std::numbers::pi) * 220.0f * t)
                      + 0.3f * std::sin(2.0f * static_cast<float>(std::numbers::pi) * 880.0f * t);
        speechL[i] = s;
        speechR[i] = s;
    }

    std::array<float*, 2> channels { speechL.data(), speechR.data() };
    dawg::engine::ProcessContext ctx {
        .audio = dawg::engine::AudioBufferView { channels.data(), 2, 512 },
        .midi = {},
        .sampleRate = 48000.0,
        .sidechain = {}
    };

    node.process(ctx);

    // Verify non-zero output produced
    float energy = 0.0f;
    for (float sample : speechL)
    {
        energy += sample * sample;
    }
    require(energy > 1e-4f, "Vocoded voiced speech produces audio output");

    // Telemetry band levels
    std::array<float, SpectralVocoderNode::numBands> bandLevels {};
    node.getBandLevels(bandLevels.data(), bandLevels.size());
    float sumLevels = 0.0f;
    for (float lvl : bandLevels) { sumLevels += lvl; }
    require(sumLevels > 0.0f, "Band envelope levels reported via telemetry");

    // 2. Unvoiced Consonant / Sibilance test
    float val = 0.6f;
    for (std::size_t i = 0; i < 512; ++i)
    {
        val = -val;
        speechL[i] = val;
        speechR[i] = val;
    }
    node.process(ctx);
    require(node.isSibilantActive(), "Aperiodic high zero-crossing frame triggers sibilant detector");

    // 3. External Sidechain carrier mode
    node.setCarrierSource(SpectralVocoderCarrierSource::externalSidechain);
    std::vector<float> extL(512, 0.4f);
    std::vector<float> extR(512, 0.4f);
    std::array<float*, 2> sidechainChannels { extL.data(), extR.data() };
    ctx.sidechain = dawg::engine::AudioBufferView { sidechainChannels.data(), 2, 512 };

    node.process(ctx);
    require(node.carrierSource() == SpectralVocoderCarrierSource::externalSidechain, "External sidechain carrier routed");
}

void testDynamicCabinetSimulatorSuite()
{
    using dawg::engine::dsp::DynamicCabinetSimulatorNode;
    using dawg::engine::dsp::CabinetModel;

    DynamicCabinetSimulatorNode node;
    require(node.prepare(48000.0, 512, 2), "DynamicCabinetSimulatorNode prepared");
    require(node.latencySamples() == 0, "Cabinet sim is zero latency");

    // Test model switching
    node.setCabinetModel(CabinetModel::vintage4x12);
    require(node.cabinetModel() == CabinetModel::vintage4x12, "Model set to vintage 4x12");

    node.setCabinetModel(CabinetModel::openBack2x12);
    require(node.cabinetModel() == CabinetModel::openBack2x12, "Model set to open back 2x12");

    node.setCabinetModel(CabinetModel::bass4x10);
    require(node.cabinetModel() == CabinetModel::bass4x10, "Model set to bass 4x10");

    // Parameter setters
    node.setMicrophoneDistance(0.25f);
    node.setMicrophoneAxis(0.30f);
    node.setTubeSag(0.60f);
    node.setSpeakerBreakup(0.40f);
    node.setImpedanceResonanceAmount(4.5f);
    node.setImpedanceInductiveAmount(5.0f);
    node.setOutputLevelDb(-1.0f);
    node.setMix(1.0f);

    require(std::abs(node.microphoneDistance() - 0.25f) < 0.01f, "Mic distance set");
    require(std::abs(node.microphoneAxis() - 0.30f) < 0.01f, "Mic axis set");
    require(std::abs(node.tubeSag() - 0.60f) < 0.01f, "Tube sag set");
    require(std::abs(node.speakerBreakup() - 0.40f) < 0.01f, "Speaker breakup set");

    // Process guitar-like transient burst
    std::vector<float> bufL(512, 0.0f);
    std::vector<float> bufR(512, 0.0f);
    for (std::size_t i = 0; i < 512; ++i)
    {
        const float t = static_cast<float>(i) / 48000.0f;
        bufL[i] = 1.2f * std::sin(2.0f * static_cast<float>(std::numbers::pi) * 110.0f * t);
        bufR[i] = bufL[i];
    }

    std::array<float*, 2> channels { bufL.data(), bufR.data() };
    dawg::engine::ProcessContext ctx {
        .audio = dawg::engine::AudioBufferView { channels.data(), 2, 512 },
        .midi = {},
        .sampleRate = 48000.0,
        .sidechain = {}
    };

    node.process(ctx);

    require(node.currentTubeSag() > 0.0f, "Power amp tube sag dynamically activated on high signal");
    require(node.currentConeExcursion() > 0.0f, "Speaker cone excursion dynamically measured");

    // Verify output integrity (no NaNs, no infinities)
    for (float sample : bufL)
    {
        require(!std::isnan(sample) && !std::isinf(sample), "Cabinet output samples are finite");
    }
}

void testAmbisonicsProcessorSuite()
{
    using dawg::engine::dsp::AmbisonicsProcessorNode;
    using dawg::engine::dsp::AmbisonicsDecoderMode;

    AmbisonicsProcessorNode node;
    require(node.prepare(48000.0, 512, 2), "AmbisonicsProcessorNode prepared");
    require(node.latencySamples() == 0, "Ambisonics processor is zero latency");

    // 1. Verify Spherical Harmonic Encoding weights (AmbiX SN3D)
    std::array<float, AmbisonicsProcessorNode::numHoaChannels> weights {};
    // Center-Front: Azimuth 0, Elevation 0 -> W = 1, X = 1, Y = 0, Z = 0
    AmbisonicsProcessorNode::computeHarmonics(0.0f, 0.0f, weights.data());
    require(std::abs(weights[0] - 1.0f) < 1e-4f, "W monopole is 1.0 at front");
    require(std::abs(weights[1] - 0.0f) < 1e-4f, "Y dipole is 0.0 at center");
    require(std::abs(weights[2] - 0.0f) < 1e-4f, "Z dipole is 0.0 at horizon");
    require(std::abs(weights[3] - 1.0f) < 1e-4f, "X dipole is 1.0 at front");

    // Left: Azimuth +pi/2, Elevation 0 -> Y = 1, X = 0
    AmbisonicsProcessorNode::computeHarmonics(static_cast<float>(std::numbers::pi * 0.5), 0.0f, weights.data());
    require(std::abs(weights[1] - 1.0f) < 1e-4f, "Y dipole is 1.0 at 90 deg left");
    require(std::abs(weights[3] - 0.0f) < 1e-4f, "X dipole is 0.0 at 90 deg left");

    // 2. Head Rotation and Directional Panning
    node.setSourceAzimuthDeg(-90.0f); // Panned to right
    node.setSourceElevationDeg(0.0f);
    node.setSourceSpread(0.1f);
    node.setHeadRotation(0.0f, 0.0f, 0.0f);
    node.setDecoderMode(AmbisonicsDecoderMode::binauralHeadphones);

    require(std::abs(node.sourceAzimuthDeg() - (-90.0f)) < 0.01f, "Azimuth set to -90 deg");
    require(node.decoderMode() == AmbisonicsDecoderMode::binauralHeadphones, "Decoder set to binaural headphones");

    // Process audio buffer
    std::vector<float> testL(512, 0.0f);
    std::vector<float> testR(512, 0.0f);
    for (std::size_t i = 0; i < 512; ++i)
    {
        const float t = static_cast<float>(i) / 48000.0f;
        const float s = 0.8f * std::sin(2.0f * static_cast<float>(std::numbers::pi) * 440.0f * t);
        testL[i] = s;
        testR[i] = s;
    }

    std::array<float*, 2> channels { testL.data(), testR.data() };
    dawg::engine::ProcessContext ctx {
        .audio = dawg::engine::AudioBufferView { channels.data(), 2, 512 },
        .midi = {},
        .sampleRate = 48000.0,
        .sidechain = {}
    };

    node.process(ctx);

    // Energy at right ear should exceed left ear because sound source is at -90 deg (Right)
    float energyL = 0.0f;
    float energyR = 0.0f;
    for (std::size_t i = 0; i < 512; ++i)
    {
        energyL += testL[i] * testL[i];
        energyR += testR[i] * testR[i];
    }
    require(energyR > energyL, "Binaural decoding places right source primarily in right headphone channel");

    // Telemetry: 16-channel HOA energies
    std::array<float, AmbisonicsProcessorNode::numHoaChannels> hoaLevels {};
    node.getHoaChannelLevels(hoaLevels.data(), hoaLevels.size());
    require(hoaLevels[0] > 0.0f, "HOA channel 0 (W) active in telemetry");

    // Multichannel decode test into 12-channel Atmos array
    std::vector<std::vector<float>> hoaInputs(16, std::vector<float>(64, 0.2f));
    std::vector<std::vector<float>> spkOutputs(12, std::vector<float>(64, 0.0f));

    std::vector<const float*> hoaPtrs(16);
    std::vector<float*> spkPtrs(12);
    for (std::size_t h = 0; h < 16; ++h) { hoaPtrs[h] = hoaInputs[h].data(); }
    for (std::size_t s = 0; s < 12; ++s) { spkPtrs[s] = spkOutputs[s].data(); }

    node.decodeMultichannel(hoaPtrs.data(), spkPtrs.data(), 12, 64);
    require(std::abs(spkOutputs[0][0]) > 0.0f, "Multichannel 7.1.4 decoder generates speaker outputs from HOA B-format");
}

void testSslChannelStripAndConsoleRoutingMatrixSuite()
{
    using namespace dawg::engine;
    using namespace dawg::engine::dsp;

    // 1. SSL Channel Strip Node Tests
    SslChannelStripNode ssl;
    require(ssl.prepare(48000.0, 256, 2), "SSL channel strip prepares cleanly at 48kHz stereo");

    std::vector<float> leftIn(256, 0.5f);
    std::vector<float> rightIn(256, 0.5f);
    std::array<float*, 2> chData {leftIn.data(), rightIn.data()};
    AudioBufferView buf {chData.data(), 2, 256};
    ProcessContext ctx {buf, {}, 48000.0, {}};

    // Verify unity fader and bypass EQ delivers transparent signal with constant 0dB pan law
    ssl.setPanLaw(PanLaw::constant0dB);
    ssl.process(ctx);
    require(std::abs(leftIn[10] - 0.5f) < 1e-3f, "Transparent pass-through when flat");

    // High Pass Filter activation
    ssl.setHpfEnabled(true);
    ssl.setHpfFrequency(100.0f);
    require(ssl.hpfEnabled(), "HPF enabled state confirmed");
    require(std::abs(ssl.hpfFrequency() - 100.0f) < 0.1f, "HPF frequency accurate");

    // Proportional Q EQ testing: boost LMF by 12dB
    ssl.setLmfGainDb(12.0f);
    ssl.setLmfFrequency(1000.0f);
    ssl.setLmfQ(1.5f);
    ssl.process(ctx);
    require(ssl.lmfGainDb() == 12.0f, "LMF gain updated");

    // Pan Law verification
    const auto panLaw3dB = calculatePanLawGains(0.0f, PanLaw::equalPower3dB);
    require(std::abs(panLaw3dB.gainL - panLaw3dB.gainR) < 1e-4f, "Center pan is symmetrical");
    require(std::abs(panLaw3dB.gainL - 0.7071f) < 0.01f, "-3dB center attenuation matches 1/sqrt(2)");

    const auto panLawLinear = calculatePanLawGains(0.0f, PanLaw::linear6dB);
    require(std::abs(panLawLinear.gainL - 0.5f) < 1e-4f, "-6dB linear center attenuation is 0.5");

    // Mute test
    ssl.setMute(true);
    ssl.process(ctx);
    require(leftIn[20] == 0.0f && rightIn[20] == 0.0f, "Muted channel outputs exact digital silence");

    // 2. Console Routing Matrix Tests
    ConsoleRoutingMatrix console;
    console.reset();

    // Set up 2 active input channels
    std::vector<float> ch1L(128, 0.4f), ch1R(128, 0.4f);
    std::vector<float> ch2L(128, 0.3f), ch2R(128, 0.3f);
    std::array<float*, 2> ch1Ptrs {ch1L.data(), ch1R.data()};
    std::array<float*, 2> ch2Ptrs {ch2L.data(), ch2R.data()};
    std::array<AudioBufferView, 2> chViews {
        AudioBufferView {ch1Ptrs.data(), 2, 128},
        AudioBufferView {ch2Ptrs.data(), 2, 128}
    };

    // Sub-bus buffers (8 stereo buses)
    std::vector<std::vector<float>> subBusData(16, std::vector<float>(128, 0.0f));
    std::vector<std::array<float*, 2>> subBusPtrs(8);
    std::vector<AudioBufferView> subViews(8);
    for (std::size_t b = 0; b < 8; ++b)
    {
        subBusPtrs[b] = {subBusData[b * 2].data(), subBusData[b * 2 + 1].data()};
        subViews[b] = AudioBufferView {subBusPtrs[b].data(), 2, 128};
    }

    // Aux buffers (8 stereo buses)
    std::vector<std::vector<float>> auxData(16, std::vector<float>(128, 0.0f));
    std::vector<std::array<float*, 2>> auxPtrs(8);
    std::vector<AudioBufferView> auxViews(8);
    for (std::size_t a = 0; a < 8; ++a)
    {
        auxPtrs[a] = {auxData[a * 2].data(), auxData[a * 2 + 1].data()};
        auxViews[a] = AudioBufferView {auxPtrs[a].data(), 2, 128};
    }

    // Master output
    std::vector<float> masterL(128, 0.0f), masterR(128, 0.0f);
    std::array<float*, 2> masterPtrs {masterL.data(), masterR.data()};
    AudioBufferView masterView {masterPtrs.data(), 2, 128};

    // Route Channel 1 to Sub-Bus 1, Channel 2 direct to Master
    ChannelRoutingConfig cfg1;
    cfg1.targetSubBus = 1; // Sub-Bus 1
    cfg1.faderGainDb = 0.0f;
    cfg1.pan = 0.0f;
    cfg1.vcaGroup = 1; // Assigned to VCA 1
    console.setChannelConfig(0, cfg1);

    ChannelRoutingConfig cfg2;
    cfg2.targetSubBus = 0; // Master direct
    cfg2.faderGainDb = -6.0f;
    cfg2.pan = 0.0f;
    console.setChannelConfig(1, cfg2);

    // Aux send on channel 1 to Aux 0 (Reverb)
    console.getChannelConfig(0).sends[0].enabled = true;
    console.getChannelConfig(0).sends[0].targetAuxIndex = 0;
    console.getChannelConfig(0).sends[0].sendGainDb = 0.0f;

    // VCA 1 master fader attenuation
    console.setVcaFader(1, -6.0f);

    console.processRouting(chViews.data(), 2, subViews.data(), auxViews.data(), masterView, 128);

    // Aux 0 must have received signal from channel 1
    require(std::abs(auxData[0][10]) > 0.0f, "Aux send buffer received post-fader send signal");

    // Master should have summed signal from Sub-Bus 1 and Channel 2
    require(std::abs(masterL[10]) > 0.0f, "Master output receives console summed signals");

    // Test VCA Mute
    console.setVcaMute(1, true);
    require(console.getVcaMute(1), "VCA 1 mute active");
    require(!console.isChannelAudible(0), "Channel 1 muted by VCA 1 master");
}

void testMpeVoiceManagerAndPolymetricSequencerSuite()
{
    using namespace dawg::engine;
    using namespace dawg::engine::dsp;

    // 1. MPE Voice Manager Tests
    MpeVoiceManager mpe;
    mpe.reset();
    require(mpe.activeVoiceCount() == 0, "No voices active after reset");

    // Strike: Note On ch 2, note 64, vel 0.9
    mpe.noteOn(2, 64, 0.9f);
    require(mpe.activeVoiceCount() == 1, "One active MPE voice");
    const auto* v1 = mpe.findVoice(2, 64);
    require(v1 != nullptr && v1->active, "Voice found for ch 2 note 64");
    require(std::abs(v1->strike - 0.9f) < 1e-4f, "Strike velocity matched");

    // Press: Polyphonic / channel pressure
    mpe.setPressure(2, 0.75f);
    require(std::abs(v1->press - 0.75f) < 1e-4f, "Press dimension tracked");

    // Glide: 14-bit pitchbend with standard 48-semitone range
    // 8192 + 4096 = 12288 -> +0.5 fraction -> +24 semitones
    mpe.setPitchBend(2, 12288);
    require(std::abs(v1->glide - 24.0f) < 0.1f, "Glide continuous semitones tracked");

    // Slide: CC74 Timbre
    mpe.setTimbreSlide(2, 0.85f);
    require(std::abs(v1->slide - 0.85f) < 1e-4f, "Slide CC74 dimension tracked");

    // Lift: Note Off release velocity
    mpe.noteOff(2, 64, 0.35f);
    require(mpe.activeVoiceCount() == 0, "Voice inactive after noteOff");

    // 2. Polymetric Sequencer Engine Tests
    PolymetricSequencerEngine seq;
    seq.reset();

    // Generate Bjorklund Euclidean rhythm: 5 hits over 8 steps (Tresillo / Afro-Cuban)
    seq.generateEuclidean(0, 5, 8, 0, 60, 0.8f);
    const auto& trk0 = seq.getTrack(0);
    require(trk0.stepCount == 8, "Track 0 step count is 8");

    std::size_t activeCount = 0;
    for (std::size_t i = 0; i < 8; ++i)
    {
        if (trk0.steps[i].active)
            ++activeCount;
    }
    require(activeCount == 5, "Bjorklund generated exactly 5 hits over 8 steps");

    // Independent polymetric length on track 1: 7 steps (polymetric 7 against 8)
    seq.generateEuclidean(1, 3, 7, 0, 48, 0.9f);
    const auto& trk1 = seq.getTrack(1);
    require(trk1.stepCount == 7, "Track 1 step count is 7");

    // Markov Harmonic Progression generation in D Dorian
    seq.generateMarkovProgression(0, 62, MusicalScaleType::dorian, 0.3f, 42);
    // Verify all generated active notes are valid notes
    for (std::size_t i = 0; i < 8; ++i)
    {
        if (trk0.steps[i].active)
        {
            require(trk0.steps[i].pitch >= 60 && trk0.steps[i].pitch <= 86, "Markov pitch in valid range");
        }
    }

    // Real-time block dispatch
    std::array<MidiEvent, 32> eventBuffer {};
    const std::size_t dispatched = seq.processBlock(120.0, 48000.0, 512, eventBuffer);
    (void)dispatched;
    require(seq.swingPercentage() == 50.0f, "Default straight swing is 50%");
    seq.setSwingPercentage(58.0f); // MPC 60 swing
    require(seq.swingPercentage() == 58.0f, "MPC 60 swing applied");
}

void testMultiTrackRecordingEngineSuite()
{
    using namespace dawg::engine;

    // 1. Latency Loopback Calibrator tests
    LatencyLoopbackCalibrator calibrator;
    std::array<float, 256> impulseBuffer {};
    calibrator.generateTestImpulse(impulseBuffer.data(), impulseBuffer.size());
    require(std::abs(impulseBuffer[64] - 1.0f) < 1e-4f, "Impulse peak at index 64");

    // Test measuring round-trip latency with known peak at index 120
    std::array<float, 256> testRecorded {};
    testRecorded[119] = 0.5f;
    testRecorded[120] = 1.0f;
    testRecorded[121] = 0.5f;
    const double measuredLatency = calibrator.measureRoundTripLatency(testRecorded.data(), testRecorded.size());
    require(std::abs(measuredLatency - 120.0) < 0.05, "Parabolic latency measurement accurate to sub-sample");

    // 2. Multi-Track Recording Engine tests
    MultiTrackRecordingEngine recEngine;
    recEngine.reset();

    // Track config
    TrackRecordConfig trk0Config {};
    trk0Config.armed = true;
    trk0Config.inputMonitoring = true;
    trk0Config.inputChannel = 0;
    trk0Config.monitorGainDb = 0.0f;
    trk0Config.monitorPan = 0.0f; // Center
    trk0Config.cueSends[0].enabled = true;
    trk0Config.cueSends[0].gainDb = 0.0f;
    trk0Config.cueSends[0].pan = -1.0f; // Pan Left in Cue A
    trk0Config.cueSends[1].enabled = true;
    trk0Config.cueSends[1].gainDb = -6.0f;
    trk0Config.cueSends[1].pan = 1.0f;  // Pan Right in Cue B
    recEngine.setTrackConfig(0, trk0Config);

    // Punch Points
    recEngine.setPunchEnabled(true);
    recEngine.setPunchPoints(1000, 2000);
    require(recEngine.punchEnabled(), "Punch enabled");
    require(recEngine.punchInSample() == 1000, "Punch in sample set");
    require(recEngine.punchOutSample() == 2000, "Punch out sample set");

    // Process block with audio input
    std::array<float, 256> inCh0 {};
    inCh0.fill(0.8f);
    const float* inChannels[1] = {inCh0.data()};
    const AudioBufferView hwInputs {
        .channels = const_cast<float**>(inChannels),
        .channelCount = 1,
        .frameCount = 256
    };

    std::array<float, 256> monL {};
    std::array<float, 256> monR {};
    float* monChannels[2] = {monL.data(), monR.data()};
    AudioBufferView monOut {
        .channels = monChannels,
        .channelCount = 2,
        .frameCount = 256
    };

    std::array<float, 256> cueAL {};
    std::array<float, 256> cueAR {};
    float* cueAChannels[2] = {cueAL.data(), cueAR.data()};
    AudioBufferView cueOutA {
        .channels = cueAChannels,
        .channelCount = 2,
        .frameCount = 256
    };

    std::array<float, 256> cueBL {};
    std::array<float, 256> cueBR {};
    float* cueBChannels[2] = {cueBL.data(), cueBR.data()};
    AudioBufferView cueOutB {
        .channels = cueBChannels,
        .channelCount = 2,
        .frameCount = 256
    };

    // Process block at timeline sample 1200 (inside punch region)
    recEngine.processRecording(hwInputs, monOut, &cueOutA, &cueOutB, 1200, 256);

    // Verify Direct Monitoring received audio
    require(monL[0] > 0.1f && monR[0] > 0.1f, "Direct input monitoring fed stereo master");

    // Verify Cue A (panned full left)
    require(cueAL[0] > 0.5f, "Cue Mix A Left received signal");
    require(std::abs(cueAR[0]) < 1e-4f, "Cue Mix A Right panned silent");

    // Verify Cue B (panned full right)
    require(std::abs(cueBL[0]) < 1e-4f, "Cue Mix B Left panned silent");
    require(cueBR[0] > 0.2f, "Cue Mix B Right received signal");

    // Loop Recording take increment
    recEngine.setLoopRecordingEnabled(true);
    recEngine.setLoopRegion(0, 500);
    require(recEngine.currentTakeIndex() == 1, "Initial take index is 1");
    // Advance timeline past loop end sample 500
    recEngine.processRecording(hwInputs, monOut, nullptr, nullptr, 550, 64);
    require(recEngine.currentTakeIndex() == 2, "Take index incremented after loop boundary");

    // 3. Broadcast WAV (BWF) Chunk generation
    const auto bwf = MultiTrackRecordingEngine::createBwfChunk(96000, "Vocal Track");
    require(bwf.timeReferenceSamples == 96000, "BWF time reference samples matched");
    require(!bwf.description.empty() && !bwf.originator.empty(), "BWF metadata populated");

    // 4. Waveform Peak Overview Caches
    std::array<float, 512> waveData {};
    std::fill_n(waveData.data(), 256, 0.5f);
    waveData[100] = 0.95f;
    waveData[200] = -0.75f;
    std::vector<WaveformPeakPoint> overview;
    MultiTrackRecordingEngine::generateWaveformOverview(waveData.data(), waveData.size(), overview);
    require(overview.size() == 2, "Two 256-sample overview blocks generated");
    require(std::abs(overview[0].max - 0.95f) < 1e-4f, "Block 0 max peak captured");
    require(std::abs(overview[0].min - (-0.75f)) < 1e-4f, "Block 0 min peak captured");
}

void testSidechainDuckingWarpMarkersAndMarkovChordsSuite()
{
    using dawg::engine::dsp::DynamicsCompressorNode;
    using dawg::engine::dsp::CompressorSettings;
    using dawg::engine::dsp::AudioWarpEngine;
    using dawg::engine::dsp::PolymetricSequencerEngine;
    using dawg::engine::dsp::MusicalScaleType;

    // 1. DynamicsCompressorNode: Sidechain Filtering, Sidechain Listen & Gate
    CompressorSettings cs {};
    cs.bypassed = false;
    cs.thresholdDb = -18.0f;
    cs.ratio = 4.0f;
    cs.attackMs = 0.5f;
    cs.releaseMs = 50.0f;
    cs.sidechainEnabled = true;
    cs.sidechainListen = true;
    cs.sidechainHpfHz = 100.0f;
    cs.sidechainLpfHz = 4000.0f;

    DynamicsCompressorNode comp {cs};
    require(comp.prepare(48000.0, 128, 2), "Compressor prepares at 48kHz");

    std::array<float, 128> mainAudioL {};
    std::array<float, 128> mainAudioR {};
    std::fill(mainAudioL.begin(), mainAudioL.end(), 0.1f);
    std::fill(mainAudioR.begin(), mainAudioR.end(), 0.1f);

    std::array<float, 128> scAudioL {};
    std::array<float, 128> scAudioR {};
    for (std::size_t i = 0; i < 128; ++i)
    {
        const float val = 0.8f * std::sin(2.0f * 3.14159265f * 1000.0f * static_cast<float>(i) / 48000.0f);
        scAudioL[i] = val;
        scAudioR[i] = val;
    }

    std::array<float*, 2> audioPlanes {mainAudioL.data(), mainAudioR.data()};
    std::array<float*, 2> scPlanes {scAudioL.data(), scAudioR.data()};

    dawg::engine::ProcessContext ctx {
        .audio = dawg::engine::AudioBufferView {audioPlanes.data(), 2, 128},
        .midi = {},
        .sampleRate = 48000.0,
        .sidechain = dawg::engine::AudioBufferView {scPlanes.data(), 2, 128}
    };

    {
        ScopedAllocationGuard guard;
        comp.process(ctx);
        require(g_allocationCount.load() == 0, "Sidechain listen process performs zero heap allocations");
    }

    // In sidechain listen mode, output audio channels are replaced by the filtered key signal (> 0.5)
    float maxListenPeak = 0.0f;
    for (const auto s : mainAudioL)
        maxListenPeak = std::max(maxListenPeak, std::abs(s));
    require(maxListenPeak > 0.6f, "Sidechain listen routed filtered key signal to main audio");

    // Test Gate and Ducking
    cs.sidechainListen = false;
    cs.gateEnabled = true;
    cs.gateThresholdDb = -40.0f;
    cs.gateRatio = 8.0f;
    comp.setSettings(cs);

    // Process quiet audio below gate threshold with quiet sidechain
    std::fill(mainAudioL.begin(), mainAudioL.end(), 0.0001f); // ~ -80 dBFS
    std::fill(mainAudioR.begin(), mainAudioR.end(), 0.0001f);
    std::fill(scAudioL.begin(), scAudioL.end(), 0.0001f);
    std::fill(scAudioR.begin(), scAudioR.end(), 0.0001f);

    comp.process(ctx);
    require(comp.gateReductionDb() > 5.0f, "Gate attenuated sub-threshold quiet signal");

    // Feed loud 1000 Hz sidechain signal to trigger compressor ducking
    for (std::size_t i = 0; i < 128; ++i)
    {
        const float val = 1.0f * std::sin(2.0f * 3.14159265f * 1000.0f * static_cast<float>(i) / 48000.0f);
        scAudioL[i] = val;
        scAudioR[i] = val;
    }
    std::fill(mainAudioL.begin(), mainAudioL.end(), 0.5f);
    std::fill(mainAudioR.begin(), mainAudioR.end(), 0.5f);

    comp.process(ctx);
    require(comp.gainReductionDb() > 2.0f, "External sidechain ducked main audio");

    // 2. AudioWarpEngine: Markers & Quantize
    AudioWarpEngine warpEngine;
    warpEngine.addMarker(0, 0.0, true);
    warpEngine.addMarker(24000, 1.05, true);
    warpEngine.addMarker(48000, 2.0, true);
    require(warpEngine.markerCount() == 3, "Warp markers registered");
    warpEngine.quantizeMarkers(0.25, 1.0f);
    require(std::abs(warpEngine.markers()[1].beatPosition - 1.0) < 0.01, "Marker quantized to beat grid");

    // 3. PolymetricSequencerEngine: Euclidean & Markov Chords
    PolymetricSequencerEngine seq;
    seq.generateEuclidean(0, 5, 16, 0, 60, 0.85f);
    const auto& trk = seq.getTrack(0);
    require(trk.stepCount == 16, "Track step count set to 16");

    std::size_t activeCount = 0;
    for (std::size_t s = 0; s < trk.stepCount; ++s)
    {
        if (trk.steps[s].active)
            ++activeCount;
    }
    require(activeCount == 5, "Euclidean algorithm generated exactly 5 hits in 16 steps");

    // Markov Chord Generation
    const auto chords = PolymetricSequencerEngine::generateMarkovChords(60, MusicalScaleType::major, 4, 0.2f);
    require(chords.size() == 4, "Generated 4 harmonic chords");
    for (const auto& ch : chords)
    {
        require(ch.voiceCount >= 3, "Chords have at least 3 voices (triad)");
        require(!ch.romanNumeral.empty(), "Roman numeral populated");
    }

    // MPC60 Swing Template
    seq.applySwingTemplate(PolymetricSequencerEngine::SwingTemplate::mpc60_58);
    require(std::abs(seq.swingPercentage() - 58.0f) < 1e-3f, "MPC60 58% swing template applied");
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
    testTapeConsoleSummingAndWowFlutter();
    testMultiSampleInstrumentRoundRobinAndSfz();
    testSpectralBalanceAssistantAndSmartUnmasking();
    testHarmonizerVocoderEngine();
    testStepSequencerAndEuclideanPolyrhythm();
    testWavetableSynthAnd2DMorphingGrid();
    testSpectralResonanceTamerAndDeEsser();
    testAnalogLadderRingModSynthAndFeedback();
    testMasterTapeVinylCuttingSuite();
    testOpticalVariMuCompressorSuite();
    testMicrotonalTuningEngineSuite();
    testStudioControlRoomSimulatorSuite();
    testSpectralSidechainUnmaskerAndAuditionDelta();
    testPhysicalModelingSynthSuite();
    testMacroModulationMatrixSuite();
    testLiveLooperSuite();
    testTransientSplitGranularResynthesizerSuite();
    testHardwareControlSurfaceEngineSuite();
    testHardwareControlSurfaceManagerSuite();
    testAcousticDrumBleedCancellationSuite();
    testSpectralMorphCrossSynthSuite();
    testSpatialHeadTrackerBridgeSuite();
    testSpringReverbMagneticDrumEchoSuite();
    testMasteringStemExportSuite();
    testEnhancedVocalPitchCorrectionSuite();
    testSpectralVocoderSuite();
    testDynamicCabinetSimulatorSuite();
    testAmbisonicsProcessorSuite();
    testSslChannelStripAndConsoleRoutingMatrixSuite();
    testMpeVoiceManagerAndPolymetricSequencerSuite();
    testMultiTrackRecordingEngineSuite();
    testSidechainDuckingWarpMarkersAndMarkovChordsSuite();
}
