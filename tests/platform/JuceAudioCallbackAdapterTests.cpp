#include "audio/JuceAudioCallbackAdapter.h"

#include <dawg/engine/AudioEngine.h>
#include <dawg/engine/PlaybackRingBuffer.h>

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

void testCallbackRendersPrefilledClipPcm()
{
    dawg::engine::AudioEngine engine;
    require(engine.prepare({48000.0, 8, 2}), "engine prepares for clip callback");
    dawg::engine::PlaybackRingBuffer ring;
    require(ring.prepare(2, 16), "stereo ring prepares");

    constexpr std::array<float, 4> leftSource {0.1f, 0.2f, 0.3f, 0.4f};
    constexpr std::array<float, 4> rightSource {-0.1f, -0.2f, -0.3f, -0.4f};
    const std::array<const float*, 2> sourcePlanes {leftSource.data(), rightSource.data()};
    require(ring.write(0, sourcePlanes, leftSource.size()) == leftSource.size(),
            "prefill stereo ring");

    dawg::engine::RenderSnapshot snapshot {
        .generation = 11,
        .trackCount = 1,
        .playback = {
            .buffer = &ring,
            .clipStartSample = 2,
            .clipLengthSamples = leftSource.size(),
            .sourceOffset = 0,
            .channelCount = 2
        }
    };
    require(engine.publishRenderSnapshot(snapshot), "publish clip render snapshot");
    require(engine.enqueueCommand({dawg::engine::EngineCommandType::play, 0}),
            "queue play command");

    dawg::platform::juce::JuceAudioCallbackAdapter adapter {engine};
    std::array<float, 8> outLeft {};
    std::array<float, 8> outRight {};
    outLeft.fill(9.0f);
    outRight.fill(9.0f);
    std::array<float*, 2> channels {outLeft.data(), outRight.data()};

    adapter.process(channels.data(), channels.size(), outLeft.size());

    require(outLeft[0] == 0.0f && outLeft[1] == 0.0f, "pre-clip left samples are silent");
    require(outRight[0] == 0.0f && outRight[1] == 0.0f, "pre-clip right samples are silent");
    for (std::size_t i = 0; i < leftSource.size(); ++i)
    {
        require(outLeft[2 + i] == leftSource[i], "adapter forwards left clip PCM");
        require(outRight[2 + i] == rightSource[i], "adapter forwards right clip PCM");
    }
    require(outLeft[6] == 0.0f && outLeft[7] == 0.0f, "post-clip left samples are silent");
    require(outRight[6] == 0.0f && outRight[7] == 0.0f, "post-clip right samples are silent");
    require(engine.transportState().samplePosition == 8, "callback advances by full block");
    require(engine.underrunCount() == 0, "callback renders without underruns");
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

void testInputChannelCaptureForwarding()
{
    dawg::engine::AudioEngine engine;
    require(engine.prepare({48000.0, 8, 2}), "engine prepares for input capture");
    dawg::engine::RecordRingBuffer recordRing;
    require(recordRing.prepare(2, 16), "record ring prepares");
    engine.setRecordBuffer(&recordRing);
    engine.setRecording(true);

    dawg::platform::juce::JuceAudioCallbackAdapter adapter {engine};
    constexpr std::array<float, 4> inLeft {0.25f, 0.5f, -0.25f, -0.5f};
    constexpr std::array<float, 4> inRight {-0.125f, 0.125f, 0.75f, -0.75f};
    const std::array<const float*, 2> inputChannels {inLeft.data(), inRight.data()};

    std::array<float, 4> outLeft {1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 4> outRight {1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float*, 2> outputChannels {outLeft.data(), outRight.data()};

    adapter.process(
        inputChannels.data(), inputChannels.size(),
        outputChannels.data(), outputChannels.size(),
        inLeft.size());

    require(recordRing.availableFrames() == 4, "adapter forwards const input channels into RecordRingBuffer");
    std::array<float, 4> readLeft {};
    std::array<float, 4> readRight {};
    std::array<float*, 2> readPlanes {readLeft.data(), readRight.data()};
    require(recordRing.read(readPlanes, 4) == 4, "read captured frames from RecordRingBuffer");
    require(readLeft == inLeft && readRight == inRight,
            "captured frames match hardware input channels bit-for-bit");
}

void testLiveSynthAuditionNote()
{
    dawg::engine::AudioEngine engine;
    require(engine.prepare({48000.0, 64, 2}), "engine prepares for live synth audition");
    dawg::platform::juce::JuceAudioCallbackAdapter adapter {engine};
    adapter.prepare(48000.0, 64);

    // Trigger note-on C4 (pitch 60)
    adapter.triggerLiveSynthNote(60, 100, true);

    std::array<std::array<float, 64>, 2> samples {};
    std::array<float*, 2> channels {samples[0].data(), samples[1].data()};

    adapter.process(channels.data(), channels.size(), samples[0].size());

    bool hasNonZeroSample = false;
    for (const auto sample : samples[0])
    {
        if (std::abs(sample) > 1e-4f)
        {
            hasNonZeroSample = true;
            break;
        }
    }
    require(hasNonZeroSample, "live synth produces audible waveform during audition");

    // Trigger note-off
    adapter.triggerLiveSynthNote(60, 0, false);
}
}

int main()
{
    testPreparedEngineSilenceAndForwarding();
    testCallbackRendersPrefilledClipPcm();
    testSparseChannelsAndBorrowedBounds();
    testInputChannelCaptureForwarding();
    testLiveSynthAuditionNote();
    std::puts("PASS: JUCE-free audio callback adapter (5 cases)");
}
