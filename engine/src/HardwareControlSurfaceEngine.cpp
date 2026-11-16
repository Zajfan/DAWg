#include <dawg/engine/HardwareControlSurfaceEngine.h>

#include <algorithm>
#include <cmath>

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

    const auto status = static_cast<std::uint8_t>(event.data[0]) & 0xF0;
    const auto d1 = static_cast<std::uint8_t>(event.data[1]) & 0x7F;
    const auto d2 = (event.size >= 3) ? (static_cast<std::uint8_t>(event.data[2]) & 0x7F) : 0;

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
    if (status == 0xB0) // Control Change (Encoders)
    {
        // Typical CC encoders 21..28
        if (d1 >= 21 && d1 <= 28)
        {
            outEvent.type = HardwareEvent::Type::encoderTurn;
            outEvent.index = d1 - 21;
            outEvent.value = static_cast<float>(d2) / 127.0f;
            return true;
        }
    }

    return false;
}

} // namespace dawg::engine
