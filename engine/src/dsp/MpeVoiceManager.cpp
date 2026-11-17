#include <dawg/engine/dsp/MpeVoiceManager.h>

namespace dawg::engine::dsp {

MpeVoiceManager::MpeVoiceManager() noexcept
{
    reset();
}

void MpeVoiceManager::reset() noexcept
{
    for (auto& v : m_voices)
    {
        v = MpeNoteState {};
    }
    m_channelPressure.fill(0.0f);
    m_channelSlide.fill(0.5f);
    m_channelGlide.fill(0.0f);
    m_ageCounter = 0;
}

int MpeVoiceManager::allocateVoice(std::uint8_t channel, std::uint8_t noteNumber) noexcept
{
    // Check if voice on same channel & note exists
    for (std::size_t i = 0; i < maxVoices; ++i)
    {
        if (m_voices[i].active && m_voices[i].channel == channel && m_voices[i].noteNumber == noteNumber)
            return static_cast<int>(i);
    }

    // Find free voice
    for (std::size_t i = 0; i < maxVoices; ++i)
    {
        if (!m_voices[i].active)
            return static_cast<int>(i);
    }

    // Voice stealing: steal oldest
    std::size_t oldestIdx = 0;
    std::uint64_t oldestAge = m_voices[0].noteAge;
    for (std::size_t i = 1; i < maxVoices; ++i)
    {
        if (m_voices[i].noteAge < oldestAge)
        {
            oldestAge = m_voices[i].noteAge;
            oldestIdx = i;
        }
    }
    return static_cast<int>(oldestIdx);
}

void MpeVoiceManager::noteOn(std::uint8_t channel, std::uint8_t noteNumber, float velocity) noexcept
{
    const int idx = allocateVoice(channel, noteNumber);
    if (idx < 0)
        return;

    auto& v = m_voices[static_cast<std::size_t>(idx)];
    v.active = true;
    v.channel = channel;
    v.noteNumber = noteNumber;
    v.strike = std::clamp(velocity, 0.0f, 1.0f);
    v.lift = 0.0f;
    v.noteAge = ++m_ageCounter;

    // Inherit channel's current expression state
    const std::size_t chIdx = (channel > 0 && channel <= 16) ? (channel - 1) : 0;
    v.press = m_channelPressure[chIdx];
    v.slide = m_channelSlide[chIdx];
    v.glide = m_channelGlide[chIdx];
}

void MpeVoiceManager::noteOff(std::uint8_t channel, std::uint8_t noteNumber, float releaseVelocity) noexcept
{
    for (auto& v : m_voices)
    {
        if (v.active && v.channel == channel && v.noteNumber == noteNumber)
        {
            v.active = false;
            v.lift = std::clamp(releaseVelocity, 0.0f, 1.0f);
            break;
        }
    }
}

void MpeVoiceManager::setPressure(std::uint8_t channel, float pressure) noexcept
{
    const float p = std::clamp(pressure, 0.0f, 1.0f);
    const std::size_t chIdx = (channel > 0 && channel <= 16) ? (channel - 1) : 0;
    m_channelPressure[chIdx] = p;

    for (auto& v : m_voices)
    {
        if (v.active && (v.channel == channel || channel == m_masterChannel))
        {
            v.press = p;
        }
    }
}

void MpeVoiceManager::setPitchBend(std::uint8_t channel, std::uint16_t raw14Bit) noexcept
{
    // 14-bit pitch bend: 8192 is center
    const float normalizedBend = (static_cast<float>(raw14Bit) - 8192.0f) / 8192.0f;
    const float semitones = normalizedBend * m_pitchBendRange;

    const std::size_t chIdx = (channel > 0 && channel <= 16) ? (channel - 1) : 0;
    m_channelGlide[chIdx] = semitones;

    for (auto& v : m_voices)
    {
        if (v.active && (v.channel == channel || channel == m_masterChannel))
        {
            v.glide = semitones;
        }
    }
}

void MpeVoiceManager::setTimbreSlide(std::uint8_t channel, float slide0To1) noexcept
{
    const float s = std::clamp(slide0To1, 0.0f, 1.0f);
    const std::size_t chIdx = (channel > 0 && channel <= 16) ? (channel - 1) : 0;
    m_channelSlide[chIdx] = s;

    for (auto& v : m_voices)
    {
        if (v.active && (v.channel == channel || channel == m_masterChannel))
        {
            v.slide = s;
        }
    }
}

void MpeVoiceManager::processMidiEvent(const MidiEvent& event) noexcept
{
    if (event.size < 2)
        return;

    const auto status = static_cast<std::uint8_t>(event.data[0]);
    const std::uint8_t command = status & 0xF0;
    const std::uint8_t channel = (status & 0x0F) + 1;

    switch (command)
    {
    case 0x90: // Note On
    {
        if (event.size < 3) return;
        const auto note = static_cast<std::uint8_t>(event.data[1]);
        const auto vel = static_cast<std::uint8_t>(event.data[2]);
        if (vel == 0)
        {
            noteOff(channel, note, 0.0f);
        }
        else
        {
            noteOn(channel, note, static_cast<float>(vel) / 127.0f);
        }
        break;
    }
    case 0x80: // Note Off
    {
        if (event.size < 3) return;
        const auto note = static_cast<std::uint8_t>(event.data[1]);
        const auto vel = static_cast<std::uint8_t>(event.data[2]);
        noteOff(channel, note, static_cast<float>(vel) / 127.0f);
        break;
    }
    case 0xD0: // Channel Pressure
    {
        const auto pressVal = static_cast<std::uint8_t>(event.data[1]);
        setPressure(channel, static_cast<float>(pressVal) / 127.0f);
        break;
    }
    case 0xA0: // Polyphonic Key Pressure
    {
        if (event.size < 3) return;
        const auto note = static_cast<std::uint8_t>(event.data[1]);
        const auto pressVal = static_cast<std::uint8_t>(event.data[2]);
        for (auto& v : m_voices)
        {
            if (v.active && v.channel == channel && v.noteNumber == note)
            {
                v.press = static_cast<float>(pressVal) / 127.0f;
            }
        }
        break;
    }
    case 0xE0: // Pitch Bend
    {
        if (event.size < 3) return;
        const auto lsb = static_cast<std::uint16_t>(event.data[1]);
        const auto msb = static_cast<std::uint16_t>(event.data[2]);
        const std::uint16_t raw14 = lsb | (msb << 7);
        setPitchBend(channel, raw14);
        break;
    }
    case 0xB0: // Control Change
    {
        if (event.size < 3) return;
        const auto ccNum = static_cast<std::uint8_t>(event.data[1]);
        const auto ccVal = static_cast<std::uint8_t>(event.data[2]);
        if (ccNum == 74) // CC 74: MPE Slide / Timbre
        {
            setTimbreSlide(channel, static_cast<float>(ccVal) / 127.0f);
        }
        break;
    }
    default:
        break;
    }
}

std::size_t MpeVoiceManager::activeVoiceCount() const noexcept
{
    std::size_t count = 0;
    for (const auto& v : m_voices)
    {
        if (v.active)
            ++count;
    }
    return count;
}

const MpeNoteState* MpeVoiceManager::findVoice(std::uint8_t channel, std::uint8_t noteNumber) const noexcept
{
    for (const auto& v : m_voices)
    {
        if (v.active && v.channel == channel && v.noteNumber == noteNumber)
            return &v;
    }
    return nullptr;
}

} // namespace dawg::engine::dsp
