#include <dawg/engine/MidiClockGenerator.h>

#include <algorithm>
#include <cmath>

namespace dawg::engine {

void MidiClockGenerator::reset() noexcept
{
    m_phaseInQuarterNote = 0.0;
    m_wasPlaying = false;
}

std::size_t MidiClockGenerator::process(
    std::size_t frameCount,
    double sampleRate,
    double bpm,
    std::int64_t /*timelineSample*/,
    bool isPlaying,
    std::array<MidiEvent, 32>& outEvents) noexcept
{
    std::size_t count = 0;

    // Detect play start / stop transition
    if (isPlaying && ! m_wasPlaying)
    {
        if (count < outEvents.size())
        {
            outEvents[count++] = makeStart();
        }
        m_phaseInQuarterNote = 0.0;
    }
    else if (! isPlaying && m_wasPlaying)
    {
        if (count < outEvents.size())
        {
            outEvents[count++] = makeStop();
        }
    }
    m_wasPlaying = isPlaying;

    if (! isPlaying || frameCount == 0 || sampleRate < 1000.0)
        return count;

    const double validBpm = (bpm > 10.0) ? bpm : 120.0;
    const double clocksPerSecond = (validBpm / 60.0) * 24.0; // 24 PPQN
    const double clockPeriodFrames = sampleRate / clocksPerSecond;

    // Emit 0xF8 timing clocks at exact sub-block sample offsets
    double remaining = static_cast<double>(frameCount);
    double curOffset = 0.0;

    while (count < outEvents.size())
    {
        const double framesUntilNextClock = (1.0 - m_phaseInQuarterNote) * clockPeriodFrames;
        if (framesUntilNextClock <= remaining)
        {
            curOffset += framesUntilNextClock;
            remaining -= framesUntilNextClock;
            m_phaseInQuarterNote = 0.0;

            MidiEvent clockEv {};
            clockEv.frameOffset = static_cast<std::uint32_t>(std::clamp(std::round(curOffset), 0.0, static_cast<double>(frameCount - 1)));
            clockEv.size = 1;
            clockEv.data[0] = static_cast<std::byte>(0xF8); // MIDI Timing Clock
            outEvents[count++] = clockEv;
        }
        else
        {
            m_phaseInQuarterNote += remaining / clockPeriodFrames;
            break;
        }
    }

    return count;
}

MidiEvent MidiClockGenerator::makeStart() noexcept
{
    MidiEvent ev {};
    ev.frameOffset = 0;
    ev.size = 1;
    ev.data[0] = static_cast<std::byte>(0xFA); // MIDI Start
    return ev;
}

MidiEvent MidiClockGenerator::makeStop() noexcept
{
    MidiEvent ev {};
    ev.frameOffset = 0;
    ev.size = 1;
    ev.data[0] = static_cast<std::byte>(0xFC); // MIDI Stop
    return ev;
}

MidiEvent MidiClockGenerator::makeContinue() noexcept
{
    MidiEvent ev {};
    ev.frameOffset = 0;
    ev.size = 1;
    ev.data[0] = static_cast<std::byte>(0xFB); // MIDI Continue
    return ev;
}

MidiEvent MidiClockGenerator::makeSongPositionPointer(std::uint16_t midiBeats) noexcept
{
    MidiEvent ev {};
    ev.frameOffset = 0;
    ev.size = 3;
    ev.data[0] = static_cast<std::byte>(0xF2); // Song Position Pointer
    ev.data[1] = static_cast<std::byte>(midiBeats & 0x7F);        // LSB (7 bits)
    ev.data[2] = static_cast<std::byte>((midiBeats >> 7) & 0x7F); // MSB (7 bits)
    return ev;
}

} // namespace dawg::engine
