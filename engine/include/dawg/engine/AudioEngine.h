#pragma once

#include <dawg/engine/AudioBufferView.h>
#include <dawg/engine/ChannelStrip.h>
#include <dawg/engine/CompiledGraph.h>
#include <dawg/engine/DspPerformanceProfiler.h>
#include <dawg/engine/EngineCommand.h>
#include <dawg/engine/EngineConfig.h>
#include <dawg/engine/GraphHandoff.h>
#include <dawg/engine/MeterTelemetry.h>
#include <dawg/engine/Metronome.h>
#include <dawg/engine/MidiEvent.h>
#include <dawg/engine/ProcessorNode.h>
#include <dawg/engine/RecordRingBuffer.h>
#include <dawg/engine/RenderSnapshotPublisher.h>
#include <dawg/engine/TempoMap.h>
#include <dawg/engine/TransportSnapshot.h>
#include <dawg/engine/TransportState.h>
#include <dawg/engine/WorkStealingScheduler.h>
#include <dawg/engine/dsp/BrickwallLimiterNode.h>

#include <memory>
#include <vector>

namespace dawg::engine {

class TransportCommandQueue;

// The host serializes lifecycle calls with processing. Views are borrowed for
// one call only; no buffer, MIDI, or transport storage is retained by the engine.
class AudioEngine final
{
public:
    // Queue storage is allocated once at construction, off the audio thread.
    // Destroy only after the producer and consumer have stopped.
    AudioEngine();
    ~AudioEngine();

    // Invalid configuration leaves the engine unprepared.
    [[nodiscard]] bool prepare(const EngineConfig& config) noexcept;
    void release() noexcept;
    // Clears render history while preserving preparation; silence has no history.
    void reset() noexcept;

    // Single message-thread producer; false means full, so the caller may
    // retry later. Enqueue never mutates transport state, waits, or allocates.
    [[nodiscard]] bool enqueueCommand(const EngineCommand& command) noexcept;

    // Single message-thread producer, independent of preparation. False means
    // the caller should retry later; accepted edits coalesce until process().
    [[nodiscard]] bool publishRenderSnapshot(RenderSnapshot snapshot) noexcept;

    // Single message-thread producer for compiled N-track execution plans.
    // Reclaims retired graphs on the calling thread and never blocks or fails.
    void publishCompiledGraph(std::unique_ptr<const CompiledGraph> graph) noexcept;
    std::size_t reclaimRetiredGraphs() noexcept;

    // Audio-thread observation, or read only while processing is stopped and
    // synchronized with the reader. This is not concurrent UI telemetry.
    [[nodiscard]] std::uint64_t renderSnapshotGeneration() const noexcept;

    // Audio-thread observation, or read only while processing is stopped and
    // synchronized with the reader. This is not a concurrent UI snapshot.
    [[nodiscard]] TransportState transportState() const noexcept;
    [[nodiscard]] std::uint64_t underrunCount() const noexcept;
    [[nodiscard]] MeterReading meterReading() const noexcept;
    [[nodiscard]] DspPerformanceSnapshot performanceSnapshot() const noexcept { return m_profiler.readSnapshot(); }
    [[nodiscard]] DspPerformanceProfiler& profiler() noexcept { return m_profiler; }

    void setRecordBuffer(RecordRingBuffer* buffer) noexcept;
    void setRecording(bool recording) noexcept;
    [[nodiscard]] bool isRecording() const noexcept;

    // Input monitoring & Punch In/Out (Option 1)
    void setInputMonitoring(bool enabled) noexcept { m_inputMonitoring.store(enabled, std::memory_order_relaxed); }
    [[nodiscard]] bool isInputMonitoring() const noexcept { return m_inputMonitoring.load(std::memory_order_relaxed); }
    void setPunchRange(bool enabled, std::uint64_t inSample, std::uint64_t outSample) noexcept;
    [[nodiscard]] bool isPunchEnabled() const noexcept { return m_punchEnabled.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint32_t loopTakeCount() const noexcept { return m_loopTakeCount.load(std::memory_order_relaxed); }
    void resetLoopTakeCount() noexcept { m_loopTakeCount.store(0, std::memory_order_relaxed); }

    void setMasterLimiterEnabled(bool enabled) noexcept;
    [[nodiscard]] bool isMasterLimiterEnabled() const noexcept;

    void setMetronomeEnabled(const bool enabled) noexcept { m_metronomeEnabled.store(enabled, std::memory_order_relaxed); }
    [[nodiscard]] bool isMetronomeEnabled() const noexcept { return m_metronomeEnabled.load(std::memory_order_relaxed); }

    // Per-track live auditioning & routing
    void triggerAuditionNote(std::uint8_t pitch, std::uint8_t velocity, bool isNoteOn) noexcept;
    void setSelectedTrackId(std::uint64_t trackId) noexcept { m_selectedTrackId.store(trackId, std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t selectedTrackId() const noexcept { return m_selectedTrackId.load(std::memory_order_relaxed); }

    // Multi-Core Topological Work-Stealing Scheduler (Frontier A)
    bool prepareScheduler(std::size_t workerCount) noexcept { return m_scheduler.prepare(workerCount); }
    void shutdownScheduler() noexcept { m_scheduler.shutdown(); }
    [[nodiscard]] WorkStealingScheduler* scheduler() noexcept { return &m_scheduler; }
    [[nodiscard]] std::size_t schedulerWorkerCount() const noexcept { return m_scheduler.workerCount(); }

    // Adopts the latest render snapshot, then applies at most 64 queued commands
    // in FIFO order before rendering.
    // Bounded by queue capacity and supplied view; no allocation, I/O, blocking.
    // The initial renderer clears output even when unprepared, skips null
    // channels, and leaves the supplied MIDI and TransportSnapshot untouched.
    void process(AudioBufferView output, MidiBufferView midi,
                 const TransportSnapshot& transport) noexcept;
    void process(AudioInputBufferView input, AudioBufferView output, MidiBufferView midi,
                 const TransportSnapshot& transport) noexcept;
    [[nodiscard]] bool isPrepared() const noexcept;
    [[nodiscard]] double sampleRate() const noexcept { return m_sampleRate; }

private:
    std::unique_ptr<TransportCommandQueue> m_commandQueue;
    RenderSnapshotPublisher m_snapshotPublisher;
    GraphHandoff m_graphHandoff;
    MeterPublisher m_meterPublisher;
    std::vector<float> m_discardChannel;
    std::vector<float> m_scratchLeft;
    std::vector<float> m_scratchRight;
    RenderSnapshot m_renderSnapshot {};
    TransportState m_transportState {};
    std::atomic<RecordRingBuffer*> m_recordBuffer {nullptr};
    std::atomic<bool> m_recording {false};
    std::atomic<bool> m_inputMonitoring {false};
    std::atomic<bool> m_punchEnabled {false};
    std::atomic<std::uint64_t> m_punchInSample {0};
    std::atomic<std::uint64_t> m_punchOutSample {0};
    std::atomic<std::uint32_t> m_loopTakeCount {0};
    std::atomic<bool> m_masterLimiterEnabled {false};
    std::atomic<bool> m_metronomeEnabled {false};
    dsp::BrickwallLimiterNode m_masterLimiter;
    WorkStealingScheduler m_scheduler;
    DspPerformanceProfiler m_profiler;
    double m_sampleRate {48000.0};
    bool m_prepared {};

    Metronome m_metronome;
    TempoMap m_tempoMap;
    std::vector<float> m_trackScratchLeft;
    std::vector<float> m_trackScratchRight;
    std::array<MidiEvent, 128> m_midiScratchEvents {};
    std::atomic<std::uint64_t> m_selectedTrackId {0};

    struct TrackDspInstance
    {
        std::uint32_t slotIndex {0};
        std::uint8_t dspType {0};
        bool bypassed {false};
        std::unique_ptr<ProcessorNode> node;
    };

    struct TrackRuntime
    {
        std::uint64_t trackId {0};
        std::uint8_t trackKind {0};
        std::unique_ptr<ProcessorNode> fallbackInstrument;
        ProcessorNode* activeInstrument {nullptr};
        std::vector<TrackDspInstance> inserts;
        std::vector<float> inputBufferLeft;
        std::vector<float> inputBufferRight;
    };

    std::vector<TrackRuntime> m_trackRuntimes;
    [[nodiscard]] TrackRuntime* findTrackRuntime(std::uint64_t trackId) noexcept;
};

} // namespace dawg::engine
