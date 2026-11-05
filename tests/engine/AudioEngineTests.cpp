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
    testLifecycleAndValidation();
    testSilentOutput();
    testViewBoundsAndEmptyOutput();
}
