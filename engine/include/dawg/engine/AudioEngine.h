#pragma once

#include <dawg/engine/AudioBufferView.h>
#include <dawg/engine/EngineConfig.h>
#include <dawg/engine/MidiEvent.h>
#include <dawg/engine/TransportSnapshot.h>

namespace dawg::engine {

// The host serializes lifecycle calls with processing. Views are borrowed for
// one call only; no buffer, MIDI, or transport storage is retained by the engine.
class AudioEngine final
{
public:
    // Invalid configuration leaves the engine unprepared.
    [[nodiscard]] bool prepare(const EngineConfig& config) noexcept;
    void release() noexcept;
    // Clears render history while preserving preparation; silence has no history.
    void reset() noexcept;

    // Bounded by the supplied view, with no allocation, I/O, or blocking.
    // The initial renderer clears output even when unprepared, skips null
    // channels, and leaves MIDI and transport untouched.
    void process(AudioBufferView output, MidiBufferView midi,
                 const TransportSnapshot& transport) noexcept;
    [[nodiscard]] bool isPrepared() const noexcept;

private:
    bool m_prepared {};
};

} // namespace dawg::engine
