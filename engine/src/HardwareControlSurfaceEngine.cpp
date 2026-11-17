#include <dawg/engine/HardwareControlSurfaceEngine.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace dawg::engine {

HardwareControlSurfaceEngine::HardwareControlSurfaceEngine() noexcept
{
    m_padStates.fill(GridPadState::empty);
    for (auto& enc : m_encoders)
    {
        enc.virtualValue = 0.5f;
        enc.physicalValue = 0.5f;
        enc.isCaught = true;
    }
}

void HardwareControlSurfaceEngine::setClipState(std::size_t row, std::size_t col, GridPadState state) noexcept
{
    if (row < GridRows && col < GridCols)
    {
        m_padStates[row * GridCols + col] = state;
    }
}

GridPadState HardwareControlSurfaceEngine::clipState(std::size_t row, std::size_t col) const noexcept
{
    if (row < GridRows && col < GridCols)
    {
        return m_padStates[row * GridCols + col];
    }
    return GridPadState::empty;
}

RgbColor HardwareControlSurfaceEngine::getPadLedColor(std::size_t row, std::size_t col) const noexcept
{
    if (m_operatingMode == GridOperatingMode::stepSequencer)
    {
        // 16-Step view on top two rows
        if (row < 2)
        {
            const std::size_t stepIdx = row * 8 + col;
            if (stepIdx == m_activeStep)
            {
                return {255, 255, 255}; // Playhead cursor: Bright White
            }
            if (m_padStates[row * GridCols + col] != GridPadState::empty)
            {
                return {0, 200, 255}; // Active trigger: Cyan
            }
            return (stepIdx % 4 == 0) ? RgbColor {40, 40, 40} : RgbColor {15, 15, 15};
        }
    }

    const auto state = clipState(row, col);
    switch (state)
    {
    case GridPadState::empty:
        return {10, 10, 10}; // Dim dark gray
    case GridPadState::playing:
        return {0, 255, 30};  // Vibrant Green
    case GridPadState::recording:
        return {255, 20, 20}; // Vibrant Red
    case GridPadState::queuedPlay:
        return {255, 190, 0}; // Amber
    case GridPadState::queuedRecord:
        return {255, 0, 200}; // Magenta
    case GridPadState::selected:
        return {0, 220, 255}; // Cyan
    }
    return {0, 0, 0};
}

std::uint8_t HardwareControlSurfaceEngine::getVelocityColorIndex(std::size_t row, std::size_t col) const noexcept
{
    const auto state = clipState(row, col);
    switch (state)
    {
    case GridPadState::empty:        return 0;  // Off
    case GridPadState::playing:      return 21; // Green
    case GridPadState::recording:    return 5;  // Red
    case GridPadState::queuedPlay:   return 13; // Amber
    case GridPadState::queuedRecord: return 53; // Pink/Magenta
    case GridPadState::selected:     return 37; // Cyan
    }
    return 0;
}

std::vector<std::uint8_t> HardwareControlSurfaceEngine::generatePadSysEx(std::size_t row, std::size_t col) const
{
    const auto color = getPadLedColor(row, col);
    // Launchpad Pro Grid Pad Number: (8 - row) * 10 + (col + 1)
    const auto padNumber = static_cast<std::uint8_t>((8 - row) * 10 + (col + 1));

    // Convert 8-bit RGB to 7-bit MIDI SysEx values (0..127)
    const auto r7 = static_cast<std::uint8_t>(color.r >> 1);
    const auto g7 = static_cast<std::uint8_t>(color.g >> 1);
    const auto b7 = static_cast<std::uint8_t>(color.b >> 1);

    // Novation Launchpad Pro RGB LED SysEx spec: F0 00 20 29 02 10 0B <pad> <r> <g> <b> F7
    return {0xF0, 0x00, 0x20, 0x29, 0x02, 0x10, 0x0B, padNumber, r7, g7, b7, 0xF7};
}

void HardwareControlSurfaceEngine::setStepSequencerPlayhead(std::size_t step16) noexcept
{
    m_activeStep = step16 % 16;
}

int HardwareControlSurfaceEngine::getNoteForPad(std::size_t row, std::size_t col, int rootNote) const noexcept
{
    // Standard 4ths chromatic isomorphic layout (Launchpad / Push standard)
    // Row 7 (bottom) starts at rootNote, each row up is +5 semitones (perfect 4th)
    const int rowOffset = static_cast<int>(7 - row) * 5;
    return rootNote + rowOffset + static_cast<int>(col);
}

void HardwareControlSurfaceEngine::setVirtualParameter(std::size_t encoderIdx, float val) noexcept
{
    if (encoderIdx < NumEncoders)
    {
        m_encoders[encoderIdx].virtualValue = std::clamp(val, 0.0f, 1.0f);
        // If virtual value changes from DAW side, encoder loses catch lock
        if (std::abs(m_encoders[encoderIdx].physicalValue - m_encoders[encoderIdx].virtualValue) > 0.05f)
        {
            m_encoders[encoderIdx].isCaught = false;
        }
    }
}

float HardwareControlSurfaceEngine::processPhysicalEncoderMove(std::size_t encoderIdx, float physical0to1) noexcept
{
    if (encoderIdx >= NumEncoders)
    {
        return 0.0f;
    }

    auto& enc = m_encoders[encoderIdx];
    const float prevPhysical = enc.physicalValue;
    enc.physicalValue = std::clamp(physical0to1, 0.0f, 1.0f);

    if (m_pickupMode == ParameterPickupMode::jump)
    {
        enc.virtualValue = enc.physicalValue;
        enc.isCaught = true;
        return enc.virtualValue;
    }

    if (m_pickupMode == ParameterPickupMode::pickup)
    {
        if (enc.isCaught)
        {
            enc.virtualValue = enc.physicalValue;
        }
        else
        {
            // Check if physical crossed virtual value
            const bool crossedAscending = (prevPhysical <= enc.virtualValue && enc.physicalValue >= enc.virtualValue);
            const bool crossedDescending = (prevPhysical >= enc.virtualValue && enc.physicalValue <= enc.virtualValue);
            const bool closeEnough = std::abs(enc.physicalValue - enc.virtualValue) < 0.03f;

            if (crossedAscending || crossedDescending || closeEnough)
            {
                enc.isCaught = true;
                enc.virtualValue = enc.physicalValue;
            }
        }
        return enc.virtualValue;
    }

    // ParameterPickupMode::valueScale: smooth proportional approach
    const float diff = enc.physicalValue - enc.virtualValue;
    enc.virtualValue = std::clamp(enc.virtualValue + diff * 0.5f, 0.0f, 1.0f);
    if (std::abs(diff) < 0.01f)
    {
        enc.isCaught = true;
        enc.virtualValue = enc.physicalValue;
    }
    return enc.virtualValue;
}

float HardwareControlSurfaceEngine::getVirtualParameter(std::size_t encoderIdx) const noexcept
{
    return (encoderIdx < NumEncoders) ? m_encoders[encoderIdx].virtualValue : 0.0f;
}

bool HardwareControlSurfaceEngine::isEncoderCaught(std::size_t encoderIdx) const noexcept
{
    return (encoderIdx < NumEncoders) ? m_encoders[encoderIdx].isCaught : true;
}

bool HardwareControlSurfaceEngine::parseMidiInput(const MidiEvent& event, HardwareEvent& outEvent) noexcept
{
    if (event.size < 2)
    {
        return false;
    }

    if (m_profile == ControllerProfile::hui)
    {
        return parseHuiMidiInput(event, outEvent);
    }

    const auto rawStatus = static_cast<std::uint8_t>(event.data[0]);
    const auto status = rawStatus & 0xF0;
    const auto channel = rawStatus & 0x0F;
    const auto d1 = static_cast<std::uint8_t>(event.data[1]) & 0x7F;
    const auto d2 = (event.size >= 3) ? (static_cast<std::uint8_t>(event.data[2]) & 0x7F) : 0;

    if (m_profile == ControllerProfile::mackieControlUniversal)
    {
        // MCU Motorized Faders (Pitch Bend channels 0-8)
        if (status == 0xE0 && channel <= 8)
        {
            const int val14 = static_cast<int>(d1) | (static_cast<int>(d2) << 7);
            outEvent.type = HardwareEvent::Type::mcuFaderMove;
            outEvent.index = channel;
            outEvent.value = static_cast<float>(val14) / 16383.0f;
            return true;
        }

        // MCU Transport buttons (Channel 0 Note On/Off)
        if (channel == 0 && (status == 0x90 || status == 0x80))
        {
            if (d1 == static_cast<std::uint8_t>(McuTransportButton::play) ||
                d1 == static_cast<std::uint8_t>(McuTransportButton::stop) ||
                d1 == static_cast<std::uint8_t>(McuTransportButton::record) ||
                d1 == static_cast<std::uint8_t>(McuTransportButton::rewind) ||
                d1 == static_cast<std::uint8_t>(McuTransportButton::fastForward) ||
                d1 == static_cast<std::uint8_t>(McuTransportButton::cycleLoop))
            {
                outEvent.type = HardwareEvent::Type::mcuTransport;
                outEvent.index = d1;
                outEvent.value = (status == 0x90 && d2 > 0) ? 1.0f : 0.0f;
                return true;
            }
        }

        // Control Change
        if (status == 0xB0)
        {
            // MCU V-Pots (CC 0x10..0x17)
            if (d1 >= 0x10 && d1 <= 0x17)
            {
                const auto vpotIdx = static_cast<std::size_t>(d1 - 0x10);
                const float ticks = ((d2 & 0x40) != 0) ? -static_cast<float>(d2 & 0x3F) : static_cast<float>(d2 & 0x3F);
                outEvent.type = HardwareEvent::Type::mcuVpotTurn;
                outEvent.index = vpotIdx;
                outEvent.value = ticks;
                return true;
            }

            // MCU Jog Wheel (CC 0x3C)
            if (d1 == 0x3C)
            {
                const float ticks = ((d2 & 0x40) != 0) ? -static_cast<float>(d2 & 0x3F) : static_cast<float>(d2 & 0x3F);
                outEvent.type = HardwareEvent::Type::mcuJogWheel;
                outEvent.index = 0;
                outEvent.value = ticks;
                return true;
            }
        }
    }

    // Grid Pads (Note On/Off for Launchpad / Push / APC)
    if (status == 0x90 && d2 > 0)
    {
        outEvent.type = HardwareEvent::Type::padPress;
        outEvent.index = d1;
        outEvent.value = static_cast<float>(d2) / 127.0f;
        return true;
    }
    if (status == 0x80 || (status == 0x90 && d2 == 0))
    {
        outEvent.type = HardwareEvent::Type::padRelease;
        outEvent.index = d1;
        outEvent.value = 0.0f;
        return true;
    }

    // Grid Controllers Encoders CC 21..28
    if (status == 0xB0 && d1 >= 21 && d1 <= 28)
    {
        outEvent.type = HardwareEvent::Type::encoderTurn;
        outEvent.index = d1 - 21;
        outEvent.value = static_cast<float>(d2) / 127.0f;
        return true;
    }

    return false;
}

std::array<std::uint8_t, 3> HardwareControlSurfaceEngine::generateMcuFaderMessage(
    std::size_t channelIdx,
    float normalized0to1) noexcept
{
    const auto ch = static_cast<std::uint8_t>(channelIdx & 0x0F);
    const auto status = static_cast<std::uint8_t>(0xE0 | ch);
    const auto val14 = std::clamp(static_cast<int>(std::round(normalized0to1 * 16383.0f)), 0, 16383);
    const auto lsb = static_cast<std::uint8_t>(val14 & 0x7F);
    const auto msb = static_cast<std::uint8_t>((val14 >> 7) & 0x7F);
    return {status, lsb, msb};
}

std::array<std::uint8_t, 3> HardwareControlSurfaceEngine::generateMcuVpotLedMessage(
    std::size_t channelIdx,
    McuVpotLedMode mode,
    float normalized0to1) noexcept
{
    const auto ccNum = static_cast<std::uint8_t>(0x30 + (channelIdx & 0x07));
    const float clamped = std::clamp(normalized0to1, 0.0f, 1.0f);

    std::uint8_t ledValue = 1;
    switch (mode)
    {
    case McuVpotLedMode::singleDot:
        ledValue = static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::round(clamped * 10.0f)) + 1, 1, 11));
        break;
    case McuVpotLedMode::boostCut:
        // Bipolar: 6 is center (0 dB), 1 is -max, 11 is +max
        ledValue = static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::round(clamped * 10.0f)) + 1, 1, 11));
        break;
    case McuVpotLedMode::wrap:
        ledValue = static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::round(clamped * 11.0f)), 0, 11));
        break;
    case McuVpotLedMode::spread:
        ledValue = static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::round(clamped * 5.0f)), 0, 5));
        break;
    }

    const auto patternByte = static_cast<std::uint8_t>(static_cast<std::uint8_t>(mode) | (ledValue & 0x0F));
    return {0xB0, ccNum, patternByte};
}

std::vector<std::uint8_t> HardwareControlSurfaceEngine::generateMcuScribbleSysEx(
    std::uint8_t line,
    const std::string& text)
{
    // MCU LCD SysEx header: F0 00 00 66 14 12 <offset> <chars...> F7
    // Line 0 offset = 0x00, Line 1 offset = 0x38 (56 chars per line)
    const std::uint8_t offset = (line == 0) ? 0x00 : 0x38;
    std::vector<std::uint8_t> sysex = {0xF0, 0x00, 0x00, 0x66, 0x14, 0x12, offset};

    constexpr std::size_t lineLength = 56;
    sysex.reserve(sysex.size() + lineLength + 1);

    for (std::size_t i = 0; i < lineLength; ++i)
    {
        if (i < text.size())
        {
            sysex.push_back(static_cast<std::uint8_t>(text[i] & 0x7F));
        }
        else
        {
            sysex.push_back(0x20); // Space pad
        }
    }

    sysex.push_back(0xF7);
    return sysex;
}

std::array<std::uint8_t, 6> HardwareControlSurfaceEngine::generateHuiSwitchMessage(
    std::uint8_t zone,
    std::uint8_t port,
    bool pressed) noexcept
{
    // HUI switch state message: B0 0C <zone> B0 2C <port | (pressed ? 0x40 : 0x00)>
    const auto val = static_cast<std::uint8_t>((port & 0x0F) | (pressed ? 0x40 : 0x00));
    return {0xB0, 0x0C, zone, 0xB0, 0x2C, val};
}

std::array<std::uint8_t, 6> HardwareControlSurfaceEngine::generateHuiFaderMessage(
    std::size_t channelIdx,
    float normalized0to1) noexcept
{
    // HUI 10-bit fader move: B0 <ch> <MSB> B0 <ch+0x20> <LSB>
    const auto ch = static_cast<std::uint8_t>(channelIdx & 0x07);
    const auto val10 = std::clamp(static_cast<int>(std::round(normalized0to1 * 1023.0f)), 0, 1023);
    const auto msb = static_cast<std::uint8_t>((val10 >> 3) & 0x7F);
    const auto lsb = static_cast<std::uint8_t>((val10 & 0x07) << 4);
    return {0xB0, ch, msb, 0xB0, static_cast<std::uint8_t>(ch + 0x20), lsb};
}

bool HardwareControlSurfaceEngine::parseHuiMidiInput(const MidiEvent& event, HardwareEvent& outEvent) noexcept
{
    if (event.size < 3)
        return false;

    const auto status = static_cast<std::uint8_t>(event.data[0]);
    const auto d1 = static_cast<std::uint8_t>(event.data[1]) & 0x7F;
    const auto d2 = static_cast<std::uint8_t>(event.data[2]) & 0x7F;

    if (status == 0xB0)
    {
        // Channel 0-7 fader MSB
        if (d1 <= 0x07)
        {
            outEvent.type = HardwareEvent::Type::huiFaderMove;
            outEvent.index = d1;
            outEvent.value = static_cast<float>(d2) / 127.0f;
            return true;
        }

        // HUI switch / zone packet
        if (d1 == 0x2C)
        {
            outEvent.type = HardwareEvent::Type::huiSwitch;
            outEvent.index = d2 & 0x0F;
            outEvent.value = ((d2 & 0x40) != 0) ? 1.0f : 0.0f;
            return true;
        }
    }

    return false;
}

} // namespace dawg::engine
