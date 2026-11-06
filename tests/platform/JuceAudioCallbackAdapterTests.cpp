#include "audio/JuceAudioCallbackAdapter.h"

#include <dawg/engine/AudioEngine.h>

#include <array>
#include <cstdio>
#include <cstdlib>

namespace {
void require(const bool condition, const char* message)
{
    if (! condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(EXIT_FAILURE);
    }
}

void testPreparedEngineSilenceAndForwarding()
{
    dawg::engine::AudioEngine engine;
    require(engine.prepare({48000.0, 32, 2}), "engine prepares for callback");
    dawg::platform::juce::JuceAudioCallbackAdapter adapter {engine};
    std::array<std::array<float, 32>, 2> samples {};
    for (auto& channel : samples)
        channel.fill(1.0f);
    std::array<float*, 2> channels {samples[0].data(), samples[1].data()};
    require(engine.enqueueCommand({dawg::engine::EngineCommandType::play, 0}),
            "play queues before callback");
    require(engine.publishRenderSnapshot({7, 2}), "render snapshot publishes before callback");

    static_assert(noexcept(adapter.process(channels.data(), channels.size(), samples[0].size())));
    adapter.process(channels.data(), channels.size(), samples[0].size());

    for (const auto& channel : samples)
        for (const auto sample : channel)
            require(sample == 0.0f, "callback clears every supplied sample");
    require(engine.transportState().playing, "callback invokes the supplied engine");
    require(engine.renderSnapshotGeneration() == 7, "callback adopts the render snapshot");
}

void testSparseChannelsAndBorrowedBounds()
{
    dawg::engine::AudioEngine engine;
    require(engine.prepare({44100.0, 8, 2}), "engine prepares for sparse output");
    dawg::platform::juce::JuceAudioCallbackAdapter adapter {engine};
    std::array<float, 5> first {9.0f, 1.0f, 1.0f, 1.0f, 9.0f};
    std::array<float, 5> extra {8.0f, 2.0f, 2.0f, 2.0f, 8.0f};
    std::array<float, 3> outside {7.0f, 7.0f, 7.0f};
    std::array<float*, 4> channels {first.data() + 1, nullptr, extra.data() + 1,
                                   outside.data()};
    adapter.process(channels.data(), 3, 3);
    require(first == std::array<float, 5> {9.0f, 0.0f, 0.0f, 0.0f, 9.0f},
            "callback respects frame bounds");
    require(extra == std::array<float, 5> {8.0f, 0.0f, 0.0f, 0.0f, 8.0f},
            "extra output channels are silent even beyond prepared channel count");
    require(outside == std::array<float, 3> {7.0f, 7.0f, 7.0f},
            "callback respects channel bounds");

    adapter.process(nullptr, 2, 3);
    adapter.process(channels.data(), 0, 3);
    adapter.process(channels.data(), channels.size(), 0);
    require(outside == std::array<float, 3> {7.0f, 7.0f, 7.0f},
            "empty output never writes samples");

    engine.release();
    adapter.process(channels.data() + 3, 1, outside.size());
    require(outside == std::array<float, 3> {}, "stopped engine output stays silent");
    require(! engine.isPrepared(), "processing never prepares the engine");
}
}

int main()
{
    testPreparedEngineSilenceAndForwarding();
    testSparseChannelsAndBorrowedBounds();
    std::puts("PASS: JUCE-free audio callback adapter (2 cases)");
}
