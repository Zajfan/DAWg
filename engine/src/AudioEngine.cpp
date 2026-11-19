#include <dawg/engine/AudioEngine.h>
#include <dawg/engine/PlaybackRingBuffer.h>
#include <dawg/engine/SimdMath.h>
#include <dawg/engine/dsp/ParametricEQNode.h>
#include <dawg/engine/dsp/DynamicsCompressorNode.h>
#include <dawg/engine/dsp/BrickwallLimiterNode.h>
#include <dawg/engine/dsp/TnhcSynthNode.h>
#include <dawg/engine/dsp/TnhcReverbNode.h>
#include <dawg/engine/dsp/TnhcDelayNode.h>
#include <dawg/engine/dsp/DrumPadSamplerNode.h>
#include <dawg/engine/dsp/AdaptiveResonanceSuppressorNode.h>
#include <dawg/engine/dsp/MultibandStereoImagerNode.h>
#include <dawg/engine/dsp/DynamicLinearPhaseMasteringEQNode.h>
#include <dawg/engine/dsp/ComplexOscillatorWavefolderNode.h>
#include <dawg/engine/dsp/TubeTransformerSaturationNode.h>
#include <dawg/engine/dsp/AnalogDegraderSuiteNode.h>
#include <dawg/engine/dsp/RayTracingAcousticRoomNode.h>
#include <dawg/engine/dsp/VirusTiPolarSynthNode.h>
#include <dawg/engine/dsp/Juno106SynthNode.h>
#include <dawg/engine/dsp/OmniFmSynthNode.h>
#include <dawg/engine/dsp/SoundFontPlayerNode.h>

#include "TransportCommandQueue.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <span>

namespace dawg::engine {
namespace {
std::unique_ptr<ProcessorNode> createDspNodeForType(const std::uint8_t dspType)
{
    switch (dspType)
    {
    case 0: return std::make_unique<dsp::ParametricEQNode>();
    case 1: return std::make_unique<dsp::DynamicsCompressorNode>();
    case 2: return std::make_unique<dsp::BrickwallLimiterNode>();
    case 3: return std::make_unique<TnhcSynthNode>();
    case 4: return std::make_unique<TnhcReverbNode>();
    case 5: return std::make_unique<TnhcDelayNode>();
    case 6: return std::make_unique<DrumPadSamplerNode>();
    case 8: return std::make_unique<dsp::AdaptiveResonanceSuppressorNode>();
    case 9: return std::make_unique<dsp::MultibandStereoImagerNode>();
    case 10: return std::make_unique<dsp::DynamicLinearPhaseMasteringEQNode>();
    case 11: return std::make_unique<dsp::ComplexOscillatorWavefolderNode>();
    case 16: return std::make_unique<dsp::TubeTransformerSaturationNode>();
    case 17: return std::make_unique<dsp::AnalogDegraderSuiteNode>();
    case 18: return std::make_unique<dsp::RayTracingAcousticRoomNode>();
    case 19: return std::make_unique<VirusTiPolarSynthNode>();
    case 20: return std::make_unique<Juno106SynthNode>();
    case 21: return std::make_unique<OmniFmSynthNode>();
    case 22: return std::make_unique<SoundFontPlayerNode>();
    default: return nullptr;
    }
}

constexpr bool isInstrumentType(const std::uint8_t dspType) noexcept
{
    return dspType == 3
        || dspType == 6
        || dspType == 11
        || dspType == 12
        || dspType == 13
        || dspType == 14
        || dspType == 15
        || dspType == 19
        || dspType == 20
        || dspType == 21
        || dspType == 22;
}

std::uint64_t saturatingAdd(const std::uint64_t lhs, const std::uint64_t rhs) noexcept
{
    return lhs > std::numeric_limits<std::uint64_t>::max() - rhs
        ? std::numeric_limits<std::uint64_t>::max()
        : lhs + rhs;
}

std::int64_t saturatingAdvance(const std::int64_t current, const std::size_t frames) noexcept
{
    const auto clampedCurrent = std::max<std::int64_t>(0, current);
    const auto maxAdvance = static_cast<std::uint64_t>(
        std::numeric_limits<std::int64_t>::max() - clampedCurrent);
    if (frames >= maxAdvance)
        return std::numeric_limits<std::int64_t>::max();
    return clampedCurrent + static_cast<std::int64_t>(frames);
}
} // namespace

AudioEngine::AudioEngine()
    : m_commandQueue(std::make_unique<TransportCommandQueue>())
{
}

AudioEngine::~AudioEngine() = default;

bool AudioEngine::enqueueCommand(const EngineCommand& command) noexcept
{
    return m_commandQueue->enqueue(command);
}

bool AudioEngine::publishRenderSnapshot(const RenderSnapshot snapshot) noexcept
{
    return m_snapshotPublisher.publish(snapshot);
}

AudioEngine::TrackRuntime* AudioEngine::findTrackRuntime(const std::uint64_t trackId) noexcept
{
    for (auto& tr : m_trackRuntimes)
    {
        if (tr.trackId == trackId)
            return &tr;
    }
    return nullptr;
}

void AudioEngine::triggerAuditionNote(const std::uint8_t pitch,
                                      const std::uint8_t velocity,
                                      const bool isNoteOn) noexcept
{
    const auto selId = m_selectedTrackId.load(std::memory_order_relaxed);
    for (auto& tr : m_trackRuntimes)
    {
        if (selId == 0 || tr.trackId == selId)
        {
            auto* inst = tr.activeInstrument != nullptr ? tr.activeInstrument : tr.fallbackInstrument.get();
            if (inst != nullptr)
            {
                MidiEvent ev {};
                ev.size = 3;
                ev.data[0] = static_cast<std::byte>(isNoteOn ? 0x90 : 0x80);
                ev.data[1] = static_cast<std::byte>(pitch);
                ev.data[2] = static_cast<std::byte>(velocity);
                std::array<MidiEvent, 1> evArr {ev};
                ProcessContext ctx {
                    .audio = {},
                    .midi = MidiBufferView {evArr.data(), 1},
                    .sampleRate = m_sampleRate
                };
                inst->process(ctx);
                break;
            }
        }
    }
}

void AudioEngine::publishCompiledGraph(std::unique_ptr<const CompiledGraph> graph) noexcept
{
    if (graph != nullptr)
    {
        std::array<TempoEvent, 1> tEvents {{{0, graph->bpm}}};
        std::array<TimeSignatureEvent, 1> tsEvents {{{0, static_cast<std::uint16_t>(graph->timeSigNumerator), static_cast<std::uint16_t>(graph->timeSigDenominator)}}};
        static_cast<void>(m_tempoMap.setEvents(tEvents, tsEvents));

        for (const auto& track : graph->tracks)
        {
            auto it = std::find_if(m_trackRuntimes.begin(), m_trackRuntimes.end(),
                                   [&](const TrackRuntime& tr) { return tr.trackId == track.trackId; });
            if (it == m_trackRuntimes.end())
            {
                TrackRuntime tr;
                tr.trackId = track.trackId;
                tr.trackKind = track.trackKind;
                m_trackRuntimes.push_back(std::move(tr));
                it = m_trackRuntimes.end() - 1;
            }
            it->trackKind = track.trackKind;

            if (track.trackKind == 1 && it->fallbackInstrument == nullptr)
            {
                it->fallbackInstrument = std::make_unique<TnhcSynthNode>();
                if (m_prepared)
                    it->fallbackInstrument->prepare(m_sampleRate, m_scratchLeft.size(), 2);
            }

            it->activeInstrument = nullptr;
            for (const auto& ins : track.inserts)
            {
                auto insIt = std::find_if(it->inserts.begin(), it->inserts.end(),
                                          [&](const TrackDspInstance& di) { return di.slotIndex == ins.slotIndex; });
                if (insIt == it->inserts.end())
                {
                    auto node = createDspNodeForType(ins.dspType);
                    if (node && m_prepared)
                        node->prepare(m_sampleRate, m_scratchLeft.size(), 2);
                    it->inserts.push_back(TrackDspInstance {
                        .slotIndex = ins.slotIndex,
                        .dspType = ins.dspType,
                        .bypassed = ins.bypassed,
                        .node = std::move(node)
                    });
                    insIt = it->inserts.end() - 1;
                }
                else
                {
                    insIt->bypassed = ins.bypassed;
                    if (insIt->dspType != ins.dspType)
                    {
                        insIt->dspType = ins.dspType;
                        insIt->node = createDspNodeForType(ins.dspType);
                        if (insIt->node && m_prepared)
                            insIt->node->prepare(m_sampleRate, m_scratchLeft.size(), 2);
                    }
                }

                if (isInstrumentType(ins.dspType) && it->activeInstrument == nullptr)
                {
                    it->activeInstrument = insIt->node.get();
                }
            }
        }
    }

    m_graphHandoff.publish(std::move(graph));
}

std::size_t AudioEngine::reclaimRetiredGraphs() noexcept
{
    return m_graphHandoff.reclaimRetired();
}

std::uint64_t AudioEngine::renderSnapshotGeneration() const noexcept
{
    return m_renderSnapshot.generation;
}

TransportState AudioEngine::transportState() const noexcept
{
    return m_transportState;
}

std::uint64_t AudioEngine::underrunCount() const noexcept
{
    return m_transportState.underrunCount;
}

MeterReading AudioEngine::meterReading() const noexcept
{
    return m_meterPublisher.read();
}

void AudioEngine::setRecordBuffer(RecordRingBuffer* buffer) noexcept
{
    m_recordBuffer.store(buffer, std::memory_order_release);
}

void AudioEngine::setRecording(const bool recording) noexcept
{
    m_recording.store(recording, std::memory_order_release);
}

void AudioEngine::setPunchRange(const bool enabled, const std::uint64_t inSample, const std::uint64_t outSample) noexcept
{
    m_punchInSample.store(inSample, std::memory_order_relaxed);
    m_punchOutSample.store(outSample, std::memory_order_relaxed);
    m_punchEnabled.store(enabled, std::memory_order_release);
}

bool AudioEngine::isRecording() const noexcept
{
    return m_recording.load(std::memory_order_acquire);
}

void AudioEngine::setMasterLimiterEnabled(const bool enabled) noexcept
{
    m_masterLimiterEnabled.store(enabled, std::memory_order_relaxed);
}

bool AudioEngine::isMasterLimiterEnabled() const noexcept
{
    return m_masterLimiterEnabled.load(std::memory_order_relaxed);
}

bool AudioEngine::prepare(const EngineConfig& config) noexcept
{
    if (! std::isfinite(config.sampleRate) || config.sampleRate <= 0.0
        || config.maximumBlockSize == 0 || config.outputChannels == 0)
    {
        m_discardChannel.clear();
        m_prepared = false;
        return false;
    }

    try
    {
        m_discardChannel.assign(config.maximumBlockSize, 0.0f);
        m_scratchLeft.assign(config.maximumBlockSize, 0.0f);
        m_scratchRight.assign(config.maximumBlockSize, 0.0f);
        m_trackScratchLeft.assign(config.maximumBlockSize, 0.0f);
        m_trackScratchRight.assign(config.maximumBlockSize, 0.0f);
    }
    catch (...)
    {
        m_discardChannel.clear();
        m_scratchLeft.clear();
        m_scratchRight.clear();
        m_trackScratchLeft.clear();
        m_trackScratchRight.clear();
        m_prepared = false;
        return false;
    }

    m_sampleRate = config.sampleRate;
    static_cast<void>(m_masterLimiter.prepare(config.sampleRate, config.maximumBlockSize, config.outputChannels));
    m_metronome.reset();

    for (auto& tr : m_trackRuntimes)
    {
        if (tr.fallbackInstrument)
            static_cast<void>(tr.fallbackInstrument->prepare(m_sampleRate, config.maximumBlockSize, 2));
        for (auto& ins : tr.inserts)
        {
            if (ins.node)
                static_cast<void>(ins.node->prepare(m_sampleRate, config.maximumBlockSize, 2));
        }
    }

    m_prepared = true;
    return true;
}

void AudioEngine::release() noexcept
{
    m_scheduler.shutdown();
    m_prepared = false;
}

void AudioEngine::reset() noexcept
{
    m_transportState = {};
    m_meterPublisher.reset();
    m_metronome.reset();
    for (auto& tr : m_trackRuntimes)
    {
        if (tr.fallbackInstrument)
            tr.fallbackInstrument->reset();
        for (auto& ins : tr.inserts)
        {
            if (ins.node)
                ins.node->reset();
        }
    }
}

void AudioEngine::process(const AudioBufferView output, const MidiBufferView midi,
                          const TransportSnapshot& transport) noexcept
{
    process(AudioInputBufferView {}, output, midi, transport);
}

void AudioEngine::process(const AudioInputBufferView input, const AudioBufferView output,
                          const MidiBufferView, const TransportSnapshot&) noexcept
{
    if (m_prepared && m_recording.load(std::memory_order_acquire))
    {
        bool inPunch = true;
        if (m_punchEnabled.load(std::memory_order_relaxed))
        {
            const auto curSample = static_cast<std::uint64_t>(std::max<std::int64_t>(0, m_transportState.samplePosition));
            inPunch = (curSample >= m_punchInSample.load(std::memory_order_relaxed) &&
                       curSample < m_punchOutSample.load(std::memory_order_relaxed));
        }

        if (inPunch)
        {
            if (auto* recBuf = m_recordBuffer.load(std::memory_order_acquire);
                recBuf != nullptr && input.channels != nullptr && input.frameCount > 0)
            {
                const std::span<const float* const> inPlanes {input.channels, input.channelCount};
                static_cast<void>(recBuf->write(inPlanes, input.frameCount));
            }
        }
    }

    const auto snapshot = m_snapshotPublisher.consume();
    if (snapshot.generation >= m_renderSnapshot.generation)
        m_renderSnapshot = snapshot;

    const CompiledGraph* compiledGraph = m_graphHandoff.adopt();
    if (compiledGraph != nullptr && compiledGraph->generation >= m_renderSnapshot.generation)
        m_renderSnapshot.generation = compiledGraph->generation;

    EngineCommand command {};
    for (std::size_t count = 0; count < TransportCommandQueue::capacity; ++count)
    {
        if (! m_commandQueue->dequeue(command))
            break;

        switch (command.type)
        {
            case EngineCommandType::play:
                m_transportState.playing = true;
                break;
            case EngineCommandType::stop:
                m_transportState.playing = false;
                break;
            case EngineCommandType::seekSamples:
                m_transportState.samplePosition = std::max<std::int64_t>(0, command.samplePosition);
                if (m_transportState.samplePosition == 0)
                {
                    if (m_renderSnapshot.playback.buffer != nullptr)
                    {
                        m_renderSnapshot.playback.buffer->requestSourceFrame(
                            m_renderSnapshot.playback.sourceOffset);
                    }
                    if (compiledGraph != nullptr)
                    {
                        for (const auto& clip : compiledGraph->clips)
                        {
                            if (clip.buffer != nullptr && clip.startSample == 0)
                                clip.buffer->requestSourceFrame(clip.sourceOffset);
                        }
                    }
                }
                break;
        }
    }

    if (output.channels == nullptr || output.frameCount == 0)
        return;

    m_profiler.startBlock(output.frameCount, m_sampleRate);

    for (std::size_t channel = 0; channel < output.channelCount; ++channel)
        if (output.channels[channel] != nullptr)
            std::fill_n(output.channels[channel], output.frameCount, 0.0f);

    if (! m_prepared || ! m_transportState.playing)
    {
        if (m_prepared && m_inputMonitoring.load(std::memory_order_relaxed) && input.channels != nullptr && output.channels != nullptr)
        {
            const auto monChans = std::min(input.channelCount, output.channelCount);
            for (std::size_t ch = 0; ch < monChans; ++ch)
            {
                if (input.channels[ch] != nullptr && output.channels[ch] != nullptr)
                {
                    for (std::size_t f = 0; f < output.frameCount; ++f)
                    {
                        output.channels[ch][f] += input.channels[ch][f];
                    }
                }
            }
        }
        m_meterPublisher.publish({});
        m_profiler.endBlock();
        return;
    }

    const auto publishMasterMeters = [this, &output]() noexcept {
        MeterReading reading {};
        if (output.channelCount >= 1 && output.channels[0] != nullptr)
        {
            float peak = 0.0f;
            float sumSq = 0.0f;
            for (std::size_t i = 0; i < output.frameCount; ++i)
            {
                const float s = output.channels[0][i];
                peak = std::max(peak, std::fabs(s));
                sumSq += s * s;
            }
            reading.peakLeft = peak;
            reading.rmsLeft = std::sqrt(sumSq / static_cast<float>(output.frameCount));
        }
        if (output.channelCount >= 2 && output.channels[1] != nullptr)
        {
            float peak = 0.0f;
            float sumSq = 0.0f;
            for (std::size_t i = 0; i < output.frameCount; ++i)
            {
                const float s = output.channels[1][i];
                peak = std::max(peak, std::fabs(s));
                sumSq += s * s;
            }
            reading.peakRight = peak;
            reading.rmsRight = std::sqrt(sumSq / static_cast<float>(output.frameCount));
        }
        else
        {
            reading.peakRight = reading.peakLeft;
            reading.rmsRight = reading.rmsLeft;
        }
        m_meterPublisher.publish(reading);
    };

    const auto blockStart = static_cast<std::uint64_t>(
        std::max<std::int64_t>(0, m_transportState.samplePosition));
    const auto blockEnd = saturatingAdd(blockStart, output.frameCount);

    if (compiledGraph != nullptr)
    {
        const auto scratchCapacity = std::min(m_scratchLeft.size(), m_scratchRight.size());
        bool anyTrackSoloed = false;
        for (const auto& track : compiledGraph->tracks)
        {
            if (track.solo)
            {
                anyTrackSoloed = true;
                break;
            }
        }

        const auto renderCompiledSlice = [&](const std::uint64_t sliceStartSample,
                                             const std::size_t destOffset,
                                             const std::size_t sliceFrames) noexcept {
            const auto sliceEndSample = saturatingAdd(sliceStartSample, sliceFrames);
            for (const auto& track : compiledGraph->tracks)
            {
                if (! isTrackAudible(track, anyTrackSoloed))
                    continue;

                const auto trackStartTime = std::chrono::steady_clock::now();

                // 1. Clear track scratch buffer
                std::fill_n(m_trackScratchLeft.begin(), sliceFrames, 0.0f);
                std::fill_n(m_trackScratchRight.begin(), sliceFrames, 0.0f);

                auto* trackRuntime = findTrackRuntime(track.trackId);

                // 2. Instrument track MIDI synthesis
                if (trackRuntime != nullptr && (track.trackKind == 1 || trackRuntime->activeInstrument != nullptr))
                {
                    auto* inst = trackRuntime->activeInstrument != nullptr ? trackRuntime->activeInstrument : trackRuntime->fallbackInstrument.get();
                    if (inst != nullptr)
                    {
                        std::size_t midiCount = 0;
                        for (const auto& mClip : compiledGraph->midiClips)
                        {
                            if (mClip.trackId != track.trackId)
                                continue;

                            const auto clipStart = mClip.startSample;
                            const auto clipEnd = saturatingAdd(clipStart, mClip.lengthSamples);
                            if (sliceStartSample >= clipEnd || sliceEndSample <= clipStart)
                                continue;

                            for (const auto& note : mClip.notes)
                            {
                                const auto noteStart = saturatingAdd(clipStart, note.startSample);
                                const auto noteEnd = saturatingAdd(noteStart, note.lengthSamples);

                                if (noteStart >= sliceStartSample && noteStart < sliceEndSample)
                                {
                                    if (midiCount < m_midiScratchEvents.size())
                                    {
                                        auto& ev = m_midiScratchEvents[midiCount++];
                                        ev.size = 3;
                                        ev.data[0] = static_cast<std::byte>(0x90);
                                        ev.data[1] = static_cast<std::byte>(note.pitch);
                                        ev.data[2] = static_cast<std::byte>(note.velocity);
                                    }
                                }
                                else if (noteEnd >= sliceStartSample && noteEnd < sliceEndSample)
                                {
                                    if (midiCount < m_midiScratchEvents.size())
                                    {
                                        auto& ev = m_midiScratchEvents[midiCount++];
                                        ev.size = 3;
                                        ev.data[0] = static_cast<std::byte>(0x80);
                                        ev.data[1] = static_cast<std::byte>(note.pitch);
                                        ev.data[2] = static_cast<std::byte>(0);
                                    }
                                }
                            }
                        }

                        std::array<float*, 2> synthChans { m_trackScratchLeft.data(), m_trackScratchRight.data() };
                        ProcessContext synthCtx {
                            .audio = AudioBufferView { synthChans.data(), 2, sliceFrames },
                            .midi = MidiBufferView { m_midiScratchEvents.data(), midiCount },
                            .sampleRate = m_sampleRate
                        };
                        inst->process(synthCtx);
                    }
                }

                // 3. Audio clips rendering into track scratch buffer
                const auto endClip = std::min<std::size_t>(
                    compiledGraph->clips.size(),
                    static_cast<std::size_t>(track.clipOffset) + track.clipCount);
                for (std::size_t idx = track.clipOffset; idx < endClip; ++idx)
                {
                    const auto& clip = compiledGraph->clips[idx];
                    if (clip.lengthSamples == 0 || clip.channelCount == 0)
                        continue;

                    const auto clipStart = clip.startSample;
                    const auto clipEnd = saturatingAdd(clipStart, clip.lengthSamples);
                    const auto overlapStart = std::max(sliceStartSample, clipStart);
                    const auto overlapEnd = std::min(sliceEndSample, clipEnd);
                    if (overlapStart >= overlapEnd)
                        continue;

                    const auto overlapFrames = static_cast<std::size_t>(overlapEnd - overlapStart);
                    const auto blockOffset =
                        static_cast<std::size_t>(overlapStart - sliceStartSample);
                    const auto clipOffset = overlapStart - clipStart;
                    const auto sourceFrame = saturatingAdd(clip.sourceOffset, clipOffset);

                    if (clip.buffer == nullptr || overlapFrames > scratchCapacity)
                    {
                        ++m_transportState.underrunCount;
                        continue;
                    }

                    clip.buffer->requestSourceFrame(sourceFrame);
                    std::size_t framesRead = 0;
                    if (clip.channelCount == 1)
                    {
                        std::array<float*, 1> dest {m_scratchLeft.data()};
                        framesRead = clip.buffer->read(sourceFrame, dest, overlapFrames);
                        for (std::size_t f = 0; f < framesRead; ++f)
                        {
                            const float env = clipEnvelopeGain(
                                clipOffset + f, clip.lengthSamples,
                                clip.fadeInSamples, clip.fadeOutSamples);
                            const float s = m_scratchLeft[f] * clip.gainLinear * env;
                            m_trackScratchLeft[blockOffset + f] += s;
                            m_trackScratchRight[blockOffset + f] += s;
                        }
                    }
                    else if (clip.channelCount >= 2)
                    {
                        std::array<float*, 2> dest {m_scratchLeft.data(), m_scratchRight.data()};
                        framesRead = clip.buffer->read(sourceFrame, dest, overlapFrames);
                        for (std::size_t f = 0; f < framesRead; ++f)
                        {
                            const float env = clipEnvelopeGain(
                                clipOffset + f, clip.lengthSamples,
                                clip.fadeInSamples, clip.fadeOutSamples);
                            const float cGain = clip.gainLinear * env;
                            m_trackScratchLeft[blockOffset + f] += m_scratchLeft[f] * cGain;
                            m_trackScratchRight[blockOffset + f] += m_scratchRight[f] * cGain;
                        }
                    }

                    if (framesRead < overlapFrames)
                        ++m_transportState.underrunCount;
                }

                // 4. Run active inserts on track
                if (trackRuntime != nullptr)
                {
                    for (auto& ins : trackRuntime->inserts)
                    {
                        if (ins.node.get() == trackRuntime->activeInstrument)
                            continue;

                        if (! ins.bypassed && ins.node != nullptr)
                        {
                            std::array<float*, 2> insChans { m_trackScratchLeft.data(), m_trackScratchRight.data() };
                            ProcessContext insCtx {
                                .audio = AudioBufferView { insChans.data(), 2, sliceFrames },
                                .midi = {},
                                .sampleRate = m_sampleRate
                            };
                            ins.node->process(insCtx);
                        }
                    }
                }

                // 5. Apply track volume, pan law, phase invert, and accumulate into master output
                const float trackPolarity = track.phaseInvert ? -1.0f : 1.0f;
                const float trackScale = track.gainLinear * trackPolarity;
                const auto panGains = equalPowerPanGains(track.pan);
                const float trackLeftGain = trackScale * panGains.left;
                const float trackRightGain = trackScale * panGains.right;

                if (output.channelCount == 1 && output.channels[0] != nullptr)
                {
                    for (std::size_t f = 0; f < sliceFrames; ++f)
                        output.channels[0][destOffset + f] += 0.5f * (m_trackScratchLeft[f] + m_trackScratchRight[f]) * trackScale;
                }
                else
                {
                    if (output.channelCount >= 1 && output.channels[0] != nullptr)
                    {
                        for (std::size_t f = 0; f < sliceFrames; ++f)
                            output.channels[0][destOffset + f] += m_trackScratchLeft[f] * trackLeftGain;
                    }
                    if (output.channelCount >= 2 && output.channels[1] != nullptr)
                    {
                        for (std::size_t f = 0; f < sliceFrames; ++f)
                            output.channels[1][destOffset + f] += m_trackScratchRight[f] * trackRightGain;
                    }
                    for (std::size_t ch = 2; ch < output.channelCount; ++ch)
                    {
                        if (output.channels[ch] != nullptr)
                        {
                            for (std::size_t f = 0; f < sliceFrames; ++f)
                                output.channels[ch][destOffset + f] += 0.5f * (m_trackScratchLeft[f] + m_trackScratchRight[f]) * trackScale;
                        }
                    }
                }

                const auto trackEndTime = std::chrono::steady_clock::now();
                const float trackUs = std::chrono::duration<float, std::micro>(trackEndTime - trackStartTime).count();
                m_profiler.recordTrackLoad(track.trackId, trackUs);
            }

            // 6. Metronome click rendering
            if (m_metronomeEnabled.load(std::memory_order_relaxed))
            {
                m_metronome.setEnabled(true);
                m_metronome.renderSlice(output, destOffset, sliceFrames, sliceStartSample, m_sampleRate, m_tempoMap);
            }
        };

        if (compiledGraph->loop.isValid())
        {
            const auto loopStart = compiledGraph->loop.startSample;
            const auto loopEnd = compiledGraph->loop.endSample;
            std::size_t remainingFrames = output.frameCount;
            std::size_t destOffset = 0;
            auto currentTimeline = blockStart;

            while (remainingFrames > 0)
            {
                if (currentTimeline >= loopStart && currentTimeline < loopEnd)
                {
                    const auto untilWrap = static_cast<std::size_t>(
                        std::min<std::uint64_t>(remainingFrames, loopEnd - currentTimeline));
                    renderCompiledSlice(currentTimeline, destOffset, untilWrap);
                    destOffset += untilWrap;
                    remainingFrames -= untilWrap;
                    currentTimeline += untilWrap;
                    if (currentTimeline == loopEnd)
                    {
                        currentTimeline = loopStart;
                        if (m_recording.load(std::memory_order_relaxed))
                        {
                            m_loopTakeCount.fetch_add(1, std::memory_order_relaxed);
                        }
                    }
                }
                else
                {
                    renderCompiledSlice(currentTimeline, destOffset, remainingFrames);
                    currentTimeline = saturatingAdd(currentTimeline, remainingFrames);
                    remainingFrames = 0;
                }
            }

            if (m_masterLimiterEnabled.load(std::memory_order_relaxed)
        && output.channels != nullptr && output.frameCount > 0)
    {
        ProcessContext ctx {
            .audio = output,
            .midi = {},
            .sampleRate = m_sampleRate
        };
        m_masterLimiter.process(ctx);
    }

    publishMasterMeters();

    if (m_inputMonitoring.load(std::memory_order_relaxed) && input.channels != nullptr && output.channels != nullptr)
    {
        const auto monChans = std::min(input.channelCount, output.channelCount);
        for (std::size_t ch = 0; ch < monChans; ++ch)
        {
            if (input.channels[ch] != nullptr && output.channels[ch] != nullptr)
            {
                for (std::size_t f = 0; f < output.frameCount; ++f)
                {
                    output.channels[ch][f] += input.channels[ch][f];
                }
            }
        }
    }
            m_transportState.samplePosition = static_cast<std::int64_t>(
                std::min<std::uint64_t>(
                    currentTimeline,
                    static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())));
            m_profiler.endBlock();
            return;
        }

        renderCompiledSlice(blockStart, 0, output.frameCount);
        if (m_masterLimiterEnabled.load(std::memory_order_relaxed)
        && output.channels != nullptr && output.frameCount > 0)
    {
        ProcessContext ctx {
            .audio = output,
            .midi = {},
            .sampleRate = m_sampleRate
        };
        m_masterLimiter.process(ctx);
    }

    publishMasterMeters();

    if (m_inputMonitoring.load(std::memory_order_relaxed) && input.channels != nullptr && output.channels != nullptr)
    {
        const auto monChans = std::min(input.channelCount, output.channelCount);
        for (std::size_t ch = 0; ch < monChans; ++ch)
        {
            if (input.channels[ch] != nullptr && output.channels[ch] != nullptr)
            {
                for (std::size_t f = 0; f < output.frameCount; ++f)
                {
                    output.channels[ch][f] += input.channels[ch][f];
                }
            }
        }
    }
        m_transportState.samplePosition = saturatingAdvance(
            m_transportState.samplePosition, output.frameCount);
        m_profiler.endBlock();
        return;
    }

    const auto& playback = m_renderSnapshot.playback;

    if (playback.clipLengthSamples > 0 && playback.channelCount > 0)
    {
        const auto clipStart = playback.clipStartSample;
        const auto clipEnd = saturatingAdd(clipStart, playback.clipLengthSamples);
        const auto overlapStart = std::max(blockStart, clipStart);
        const auto overlapEnd = std::min(blockEnd, clipEnd);

        if (overlapStart < overlapEnd)
        {
            const auto overlapFrames = static_cast<std::size_t>(overlapEnd - overlapStart);
            const auto blockOffset = static_cast<std::size_t>(overlapStart - blockStart);
            const auto clipOffset = overlapStart - clipStart;
            const auto sourceFrame = saturatingAdd(playback.sourceOffset, clipOffset);

            if (playback.buffer == nullptr)
            {
                ++m_transportState.underrunCount;
            }
            else
            {
                playback.buffer->requestSourceFrame(sourceFrame);
                std::size_t framesRead = 0;

                if (playback.channelCount == 1)
                {
                    std::size_t primaryChannel = output.channelCount;
                    for (std::size_t channel = 0; channel < output.channelCount; ++channel)
                    {
                        if (output.channels[channel] != nullptr)
                        {
                            primaryChannel = channel;
                            break;
                        }
                    }

                    if (primaryChannel < output.channelCount)
                    {
                        float* primaryDest = output.channels[primaryChannel] + blockOffset;
                        std::array<float*, 1> dest {primaryDest};
                        framesRead = playback.buffer->read(sourceFrame, dest, overlapFrames);
                        for (std::size_t channel = primaryChannel + 1;
                             channel < output.channelCount; ++channel)
                        {
                            if (output.channels[channel] != nullptr)
                            {
                                std::copy_n(primaryDest, framesRead,
                                            output.channels[channel] + blockOffset);
                            }
                        }
                    }
                }
                else if (playback.channelCount == 2)
                {
                    float* const leftDest =
                        (output.channelCount >= 1 && output.channels[0] != nullptr)
                            ? output.channels[0] + blockOffset
                            : (overlapFrames <= m_discardChannel.size()
                                   ? m_discardChannel.data()
                                   : nullptr);
                    float* const rightDest =
                        (output.channelCount >= 2 && output.channels[1] != nullptr)
                            ? output.channels[1] + blockOffset
                            : (overlapFrames <= m_discardChannel.size()
                                   ? m_discardChannel.data()
                                   : nullptr);
                    if (leftDest != nullptr && rightDest != nullptr
                        && (leftDest != m_discardChannel.data()
                            || rightDest != m_discardChannel.data()))
                    {
                        std::array<float*, 2> dest {leftDest, rightDest};
                        framesRead = playback.buffer->read(sourceFrame, dest, overlapFrames);
                    }
                }

                if (framesRead < overlapFrames)
                    ++m_transportState.underrunCount;
            }
        }
    }

    if (m_masterLimiterEnabled.load(std::memory_order_relaxed)
        && output.channels != nullptr && output.frameCount > 0)
    {
        ProcessContext ctx {
            .audio = output,
            .midi = {},
            .sampleRate = m_sampleRate
        };
        m_masterLimiter.process(ctx);
    }

    publishMasterMeters();

    if (m_inputMonitoring.load(std::memory_order_relaxed) && input.channels != nullptr && output.channels != nullptr)
    {
        const auto monChans = std::min(input.channelCount, output.channelCount);
        for (std::size_t ch = 0; ch < monChans; ++ch)
        {
            if (input.channels[ch] != nullptr && output.channels[ch] != nullptr)
            {
                for (std::size_t f = 0; f < output.frameCount; ++f)
                {
                    output.channels[ch][f] += input.channels[ch][f];
                }
            }
        }
    }
    m_transportState.samplePosition = saturatingAdvance(
        m_transportState.samplePosition, output.frameCount);
    m_profiler.endBlock();
}

bool AudioEngine::isPrepared() const noexcept
{
    return m_prepared;
}

} // namespace dawg::engine
