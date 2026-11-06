#pragma once

#include <dawg/engine/AudioBufferView.h>
#include <dawg/engine/EngineCommand.h>
#include <dawg/engine/EngineConfig.h>
#include <dawg/engine/MidiEvent.h>
#include <dawg/engine/RenderSnapshotPublisher.h>
#include <dawg/engine/TransportSnapshot.h>
#include <dawg/engine/TransportState.h>

#include <memory>

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

    // Audio-thread observation, or read only while processing is stopped and
    // synchronized with the reader. This is not concurrent UI telemetry.
    [[nodiscard]] std::uint64_t renderSnapshotGeneration() const noexcept;

    // Audio-thread observation, or read only while processing is stopped and
    // synchronized with the reader. This is not a concurrent UI snapshot.
    [[nodiscard]] TransportState transportState() const noexcept;

    // Adopts the latest render snapshot, then applies at most 64 queued commands
    // in FIFO order before rendering.
    // Bounded by queue capacity and supplied view; no allocation, I/O, blocking.
    // The initial renderer clears output even when unprepared, skips null
    // channels, and leaves the supplied MIDI and TransportSnapshot untouched.
    void process(AudioBufferView output, MidiBufferView midi,
                 const TransportSnapshot& transport) noexcept;
    [[nodiscard]] bool isPrepared() const noexcept;

private:
    std::unique_ptr<TransportCommandQueue> m_commandQueue;
    RenderSnapshotPublisher m_snapshotPublisher;
    RenderSnapshot m_renderSnapshot {};
    TransportState m_transportState {};
    bool m_prepared {};
};

} // namespace dawg::engine
