#include <dawg/engine/AudioEngine.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>

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
    for (const auto sample : samples)
        require(sample == 0.0f, "transport processing preserves silent output");

    require(engine.enqueueCommand({EngineCommandType::seekSamples, 96000}),
            "seek command is accepted");
    require(engine.transportState().samplePosition == 0, "seek waits for processing");
    engine.process(output, {}, {});
    require(engine.transportState().samplePosition == 96000, "audio thread applies seek command");
    require(engine.transportState().playing, "seek preserves playing state");

    require(engine.enqueueCommand({EngineCommandType::stop, 0}), "stop command is accepted");
    engine.process(output, {}, {});
    require(! engine.transportState().playing, "audio thread applies stop command");
    require(engine.transportState().samplePosition == 96000, "stop preserves sample position");
    engine.process(output, {}, {true, 123, 90.0});
    require(! engine.transportState().playing && engine.transportState().samplePosition == 96000,
            "empty queue and supplied snapshot do not change command-owned state");
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
}

int main()
{
    testTransportCommands();
    testTransportFifoAndEmptyBlocks();
    testTransportCapacityAndReuse();
    testLifecycleAndValidation();
    testSilentOutput();
    testViewBoundsAndEmptyOutput();
}
