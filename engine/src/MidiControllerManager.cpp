#include <dawg/engine/MidiControllerManager.h>

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace dawg::engine {

void MidiControllerManager::startLearn(const std::uint32_t targetId,
                                       const float minValue, const float maxValue) noexcept
{
    m_learningActive = true;
    m_learningTargetId = targetId;
    m_learningMinValue = minValue;
    m_learningMaxValue = maxValue;
}

void MidiControllerManager::cancelLearn() noexcept
{
    m_learningActive = false;
    m_learningTargetId = 0;
}

std::uint32_t MidiControllerManager::addBinding(const std::uint8_t channel,
                                                const std::uint8_t ccNumber,
                                                const std::uint32_t targetId,
                                                const float minValue,
                                                const float maxValue) noexcept
{
    // Check if matching binding exists
    for (auto& b : m_bindings)
    {
        if (b.active && b.channel == channel && b.ccNumber == ccNumber)
        {
            b.targetId = targetId;
            b.minValue = minValue;
            b.maxValue = maxValue;
            return b.bindingId;
        }
    }

    // Find first empty slot
    for (auto& b : m_bindings)
    {
        if (! b.active)
        {
            b.bindingId = m_nextBindingId++;
            b.channel = channel;
            b.ccNumber = ccNumber;
            b.targetId = targetId;
            b.minValue = minValue;
            b.maxValue = maxValue;
            b.active = true;
            return b.bindingId;
        }
    }

    return 0; // Table full
}

bool MidiControllerManager::removeBinding(const std::uint32_t bindingId) noexcept
{
    for (auto& b : m_bindings)
    {
        if (b.active && b.bindingId == bindingId)
        {
            b.active = false;
            return true;
        }
    }
    return false;
}

void MidiControllerManager::clearBindings() noexcept
{
    for (auto& b : m_bindings)
        b.active = false;
}

std::size_t MidiControllerManager::bindingCount() const noexcept
{
    std::size_t count = 0;
    for (const auto& b : m_bindings)
    {
        if (b.active)
            ++count;
    }
    return count;
}

McuEvent MidiControllerManager::parseMcuEvent(const std::uint8_t statusType,
                                              const std::uint8_t channel,
                                              const std::uint8_t byte1,
                                              const std::uint8_t byte2) const noexcept
{
    // MCU Pitch Bend: motorized faders 1..8 (channels 0..7) and Master (channel 8)
    if (statusType == 0xE0)
    {
        if (channel <= 8)
        {
            const int raw = (static_cast<int>(byte2) << 7) | static_cast<int>(byte1);
            const float norm = std::clamp(static_cast<float>(raw) / 16383.0f, 0.0f, 1.0f);
            return McuEvent {
                .type = McuType::fader,
                .transport = McuTransport::none,
                .button = McuButton::none,
                .trackIndex = channel,
                .buttonState = false,
                .value = norm
            };
        }
        return {};
    }

    // MCU Note On/Off: buttons and fader touch
    if (statusType == 0x90 || statusType == 0x80)
    {
        const bool pressed = (statusType == 0x90 && byte2 > 0);
        const std::uint8_t note = byte1;

        // Transport
        if (note == 94) // Play
            return McuEvent {.type = McuType::transport, .transport = McuTransport::play, .buttonState = pressed};
        if (note == 93) // Stop
            return McuEvent {.type = McuType::transport, .transport = McuTransport::stop, .buttonState = pressed};
        if (note == 95) // Record
            return McuEvent {.type = McuType::transport, .transport = McuTransport::record, .buttonState = pressed};
        if (note == 91) // Rewind
            return McuEvent {.type = McuType::transport, .transport = McuTransport::rewind, .buttonState = pressed};
        if (note == 92) // Fast Forward
            return McuEvent {.type = McuType::transport, .transport = McuTransport::fastForward, .buttonState = pressed};
        if (note == 86) // Cycle / Loop
            return McuEvent {.type = McuType::transport, .transport = McuTransport::cycleLoop, .buttonState = pressed};

        // Fader Touch
        if (note >= 104 && note <= 111) // Tracks 0..7
        {
            return McuEvent {
                .type = McuType::faderTouch,
                .trackIndex = static_cast<std::uint8_t>(note - 104),
                .buttonState = pressed
            };
        }
        if (note == 112) // Master Fader
        {
            return McuEvent {
                .type = McuType::faderTouch,
                .trackIndex = 8,
                .buttonState = pressed
            };
        }

        // Channel Buttons
        if (note <= 7) // Rec Arm
        {
            return McuEvent {
                .type = McuType::button,
                .button = McuButton::arm,
                .trackIndex = note,
                .buttonState = pressed
            };
        }
        if (note >= 8 && note <= 15) // Solo
        {
            return McuEvent {
                .type = McuType::button,
                .button = McuButton::solo,
                .trackIndex = static_cast<std::uint8_t>(note - 8),
                .buttonState = pressed
            };
        }
        if (note >= 16 && note <= 23) // Mute
        {
            return McuEvent {
                .type = McuType::button,
                .button = McuButton::mute,
                .trackIndex = static_cast<std::uint8_t>(note - 16),
                .buttonState = pressed
            };
        }
        if (note >= 24 && note <= 31) // Select
        {
            return McuEvent {
                .type = McuType::button,
                .button = McuButton::select,
                .trackIndex = static_cast<std::uint8_t>(note - 24),
                .buttonState = pressed
            };
        }

        return {};
    }

    // MCU Control Change: V-Pots and Jog Wheel
    if (statusType == 0xB0)
    {
        // V-Pots (CC 16..23)
        if (byte1 >= 16 && byte1 <= 23)
        {
            const std::uint8_t track = byte1 - 16;
            const int step = byte2 & 0x3F;
            const float delta = (byte2 & 0x40) ? -static_cast<float>(step) : static_cast<float>(step);
            return McuEvent {
                .type = McuType::vpot,
                .trackIndex = track,
                .value = delta
            };
        }

        // Jog Wheel (CC 60)
        if (byte1 == 60)
        {
            const int step = byte2 & 0x3F;
            const float delta = (byte2 & 0x40) ? -static_cast<float>(step) : static_cast<float>(step);
            return McuEvent {
                .type = McuType::jogWheel,
                .value = delta
            };
        }
    }

    return {};
}

ProcessedMidiControl MidiControllerManager::processEvent(const MidiEvent& event) noexcept
{
    ProcessedMidiControl result {};
    if (event.size < 2)
        return result;

    const auto rawStatus = static_cast<std::uint8_t>(event.data[0]);
    const auto statusType = rawStatus & 0xF0;
    const auto channel = rawStatus & 0x0F;
    const auto byte1 = static_cast<std::uint8_t>(event.data[1]) & 0x7F;
    const auto byte2 = (event.size >= 3) ? (static_cast<std::uint8_t>(event.data[2]) & 0x7F) : std::uint8_t {0};

    if (m_mcuEnabled)
    {
        result.mcuEvent = parseMcuEvent(statusType, channel, byte1, byte2);
    }

    // CC handling: MIDI Learn and parameter bindings
    if (statusType == 0xB0 && event.size >= 3)
    {
        const std::uint8_t midiChan = channel + 1; // 1-indexed

        // If learn mode is active, bind this CC
        if (m_learningActive)
        {
            const std::uint32_t bId = addBinding(midiChan, byte1, m_learningTargetId,
                                                m_learningMinValue, m_learningMaxValue);
            result.learnedNewBinding = true;
            result.learnedBindingId = bId;
            m_learningActive = false;
        }

        // Evaluate against active bindings
        for (const auto& b : m_bindings)
        {
            if (b.active && (b.channel == 0 || b.channel == midiChan) && b.ccNumber == byte1)
            {
                const float norm = static_cast<float>(byte2) / 127.0f;
                result.learnResult = MidiLearnResult {
                    .triggered = true,
                    .targetId = b.targetId,
                    .value = b.minValue + norm * (b.maxValue - b.minValue)
                };
                break;
            }
        }
    }

    return result;
}

MidiEvent MidiControllerManager::makeFaderFeedback(const std::uint8_t trackIndex,
                                                   const float normalizedValue) noexcept
{
    MidiEvent ev {};
    ev.size = 3;
    ev.frameOffset = 0;
    const std::uint8_t ch = trackIndex & 0x0F;
    ev.data[0] = static_cast<std::byte>(0xE0 | ch);

    const int raw = std::clamp(static_cast<int>(normalizedValue * 16383.0f), 0, 16383);
    ev.data[1] = static_cast<std::byte>(raw & 0x7F);
    ev.data[2] = static_cast<std::byte>((raw >> 7) & 0x7F);
    return ev;
}

MidiEvent MidiControllerManager::makeButtonLed(const std::uint8_t note, const bool on) noexcept
{
    MidiEvent ev {};
    ev.size = 3;
    ev.frameOffset = 0;
    ev.data[0] = static_cast<std::byte>(0x90);
    ev.data[1] = static_cast<std::byte>(note & 0x7F);
    ev.data[2] = static_cast<std::byte>(on ? 0x7F : 0x00);
    return ev;
}

MidiEvent MidiControllerManager::makeVpotRing(
    const std::uint8_t trackIndex,
    const float normalizedValue,
    const VpotRingMode mode) noexcept
{
    MidiEvent ev {};
    ev.size = 3;
    ev.frameOffset = 0;
    ev.data[0] = static_cast<std::byte>(0xB0);
    ev.data[1] = static_cast<std::byte>(0x30 + (trackIndex & 0x07));
    const int val = std::clamp(static_cast<int>(normalizedValue * 11.0f), 0, 11);
    const int rawByte = static_cast<int>(mode) | (val & 0x0F);
    ev.data[2] = static_cast<std::byte>(rawByte & 0x7F);
    return ev;
}

MidiEvent MidiControllerManager::makeLaunchpadPad(
    const std::uint8_t trackIdx,
    const std::uint8_t sceneIdx,
    const LaunchpadColor color,
    const LaunchpadLedMode mode) noexcept
{
    MidiEvent ev {};
    ev.size = 3;
    ev.frameOffset = 0;

    // Launchpad note indexing: 11..18 for row 1, 21..28 for row 2, etc. (or row*10 + col)
    const std::uint8_t note = static_cast<std::uint8_t>((sceneIdx + 1) * 10 + (trackIdx + 1));

    if (mode == LaunchpadLedMode::flashing)
    {
        ev.data[0] = static_cast<std::byte>(0x91); // Channel 2 = flashing mode
    }
    else if (mode == LaunchpadLedMode::pulsing)
    {
        ev.data[0] = static_cast<std::byte>(0x92); // Channel 3 = pulsing mode
    }
    else
    {
        ev.data[0] = static_cast<std::byte>(0x90); // Channel 1 = static color
    }

    ev.data[1] = static_cast<std::byte>(note & 0x7F);
    ev.data[2] = static_cast<std::byte>(static_cast<std::uint8_t>(color) & 0x7F);
    return ev;
}

std::vector<std::byte> MidiControllerManager::makeScribbleStripSysEx(
    const std::uint8_t trackIndex,
    const std::string& line1,
    const std::string& line2)
{
    // MCU LCD SysEx: F0 00 00 66 14 12 <offset> <char0..char13> F7
    // Line 1 is at offset (trackIndex * 7), Line 2 is at offset (56 + trackIndex * 7)
    std::vector<std::byte> sysex;
    sysex.reserve(24);

    const std::uint8_t offset = (trackIndex & 0x07) * 7;
    sysex.push_back(std::byte {0xF0});
    sysex.push_back(std::byte {0x00});
    sysex.push_back(std::byte {0x00});
    sysex.push_back(std::byte {0x66});
    sysex.push_back(std::byte {0x14});
    sysex.push_back(std::byte {0x12});
    sysex.push_back(static_cast<std::byte>(offset));

    for (std::size_t i = 0; i < 7; ++i)
    {
        const char c = (i < line1.size()) ? line1[i] : ' ';
        sysex.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(c) & 0x7F));
    }
    sysex.push_back(std::byte {0xF7});

    if (!line2.empty())
    {
        const std::uint8_t line2Offset = 56 + (trackIndex & 0x07) * 7;
        sysex.push_back(std::byte {0xF0});
        sysex.push_back(std::byte {0x00});
        sysex.push_back(std::byte {0x00});
        sysex.push_back(std::byte {0x66});
        sysex.push_back(std::byte {0x14});
        sysex.push_back(std::byte {0x12});
        sysex.push_back(static_cast<std::byte>(line2Offset));
        for (std::size_t i = 0; i < 7; ++i)
        {
            const char c = (i < line2.size()) ? line2[i] : ' ';
            sysex.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(c) & 0x7F));
        }
        sysex.push_back(std::byte {0xF7});
    }

    return sysex;
}

std::vector<std::byte> MidiControllerManager::makeTimecodeDisplaySysEx(
    const int bars,
    const int beats,
    const int sixteenths)
{
    // MCU 7-segment display SysEx or CC message (0xB0 0x40..0x49)
    std::vector<std::byte> sysex;
    sysex.reserve(16);
    sysex.push_back(std::byte {0xF0});
    sysex.push_back(std::byte {0x00});
    sysex.push_back(std::byte {0x00});
    sysex.push_back(std::byte {0x66});
    sysex.push_back(std::byte {0x14});
    sysex.push_back(std::byte {0x10}); // Timecode display command

    std::ostringstream ss;
    ss << std::setfill('0') << std::setw(3) << bars
       << "." << std::setw(2) << beats
       << "." << std::setw(2) << sixteenths;
    const auto str = ss.str();

    for (const char c : str)
    {
        sysex.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(c) & 0x7F));
    }

    sysex.push_back(std::byte {0xF7});
    return sysex;
}

std::string MidiControllerManager::serializeBindings() const
{
    std::ostringstream ss;
    for (const auto& b : m_bindings)
    {
        if (b.active)
        {
            ss << b.bindingId << "," << static_cast<int>(b.channel) << ","
               << static_cast<int>(b.ccNumber) << "," << b.targetId << ","
               << b.minValue << "," << b.maxValue << ";";
        }
    }
    return ss.str();
}

bool MidiControllerManager::deserializeBindings(const std::string& serialized)
{
    clearBindings();
    if (serialized.empty())
    {
        return true;
    }

    std::istringstream stream(serialized);
    std::string item;
    while (std::getline(stream, item, ';'))
    {
        if (item.empty()) continue;
        std::istringstream lineStream(item);
        std::string token;

        std::uint32_t bId = 0;
        int ch = 0;
        int cc = 0;
        std::uint32_t targetId = 0;
        float minVal = 0.0f;
        float maxVal = 1.0f;

        if (std::getline(lineStream, token, ',')) bId = static_cast<std::uint32_t>(std::stoul(token));
        if (std::getline(lineStream, token, ',')) ch = std::stoi(token);
        if (std::getline(lineStream, token, ',')) cc = std::stoi(token);
        if (std::getline(lineStream, token, ',')) targetId = static_cast<std::uint32_t>(std::stoul(token));
        if (std::getline(lineStream, token, ',')) minVal = std::stof(token);
        if (std::getline(lineStream, token, ',')) maxVal = std::stof(token);

        (void)bId;
        static_cast<void>(addBinding(static_cast<std::uint8_t>(ch), static_cast<std::uint8_t>(cc), targetId, minVal, maxVal));
    }
    return true;
}

} // namespace dawg::engine
