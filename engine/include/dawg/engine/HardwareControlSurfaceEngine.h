#pragma once

#include <dawg/engine/MidiEvent.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dawg::engine {

enum class ControllerProfile : std::uint8_t
{
    launchpadPro,
    push2,
    apc40Mk2,
    genericRgbGrid
};

enum class GridPadState : std::uint8_t
{
    empty,
    playing,
    recording,
    queuedPlay,
    queuedRecord,
    selected
};

enum class GridOperatingMode : std::uint8_t
{
    clipLaunch,
    notePerformance,
    stepSequencer
};

enum class KnobBank : std::uint8_t
{
    trackVolume,
    trackPan,
    sends,
    macros
};

enum class ParameterPickupMode : std::uint8_t
{
    jump,       // Immediate snap to physical hardware position
    pickup,     // Catch mode: ignores motion until knob crosses existing value
    valueScale  // Smooth proportional scaling towards physical value
};

struct RgbColor
{
    std::uint8_t r {0};
    std::uint8_t g {0};
    std::uint8_t b {0};
};

struct HardwareEncoderState
{
    float virtualValue {0.5f};
    float physicalValue {0.5f};
    bool isCaught {true};
};

// Hardware Control Surface Scripting & Bi-Directional MIDI Engine (Launchpad Pro / Push 2 style)
class HardwareControlSurfaceEngine
{
public:
    static constexpr std::size_t GridRows = 8;
    static constexpr std::size_t GridCols = 8;
    static constexpr std::size_t TotalPads = GridRows * GridCols; // 64 Pads
    static constexpr std::size_t NumEncoders = 8;

    HardwareControlSurfaceEngine() noexcept;
    ~HardwareControlSurfaceEngine() = default;

    void setProfile(ControllerProfile profile) noexcept { m_profile = profile; }
    [[nodiscard]] ControllerProfile profile() const noexcept { return m_profile; }

    void setOperatingMode(GridOperatingMode mode) noexcept { m_operatingMode = mode; }
    [[nodiscard]] GridOperatingMode operatingMode() const noexcept { return m_operatingMode; }

    void setPickupMode(ParameterPickupMode mode) noexcept { m_pickupMode = mode; }
    [[nodiscard]] ParameterPickupMode pickupMode() const noexcept { return m_pickupMode; }

    void setEncoderBank(KnobBank bank) noexcept { m_activeBank = bank; }
    [[nodiscard]] KnobBank encoderBank() const noexcept { return m_activeBank; }

    // Pad Matrix State Management
    void setClipState(std::size_t row, std::size_t col, GridPadState state) noexcept;
    [[nodiscard]] GridPadState clipState(std::size_t row, std::size_t col) const noexcept;
    [[nodiscard]] RgbColor getPadLedColor(std::size_t row, std::size_t col) const noexcept;

    // SysEx & LED Output Packet Generation
    [[nodiscard]] std::vector<std::uint8_t> generatePadSysEx(std::size_t row, std::size_t col) const;
    [[nodiscard]] std::uint8_t getVelocityColorIndex(std::size_t row, std::size_t col) const noexcept;

    // Step Sequencer Playhead Sweep
    void setStepSequencerPlayhead(std::size_t step16) noexcept;
    [[nodiscard]] std::size_t stepSequencerPlayhead() const noexcept { return m_activeStep; }

    // Note Mode Scale Mapping
    [[nodiscard]] int getNoteForPad(std::size_t row, std::size_t col, int rootNote = 60) const noexcept;

    // Hardware Encoder Processing with Pickup Anti-Jump
    void setVirtualParameter(std::size_t encoderIdx, float val) noexcept;
    [[nodiscard]] float processPhysicalEncoderMove(std::size_t encoderIdx, float physical0to1) noexcept;
    [[nodiscard]] float getVirtualParameter(std::size_t encoderIdx) const noexcept;
    [[nodiscard]] bool isEncoderCaught(std::size_t encoderIdx) const noexcept;

    // Inbound Hardware Event Parsing
    struct HardwareEvent
    {
        enum class Type { padPress, padRelease, encoderTurn, sceneLaunch, bankChange };
        Type type {Type::padPress};
        std::size_t index {0};
        float value {0.0f};
    };
    [[nodiscard]] bool parseMidiInput(const MidiEvent& event, HardwareEvent& outEvent) noexcept;

private:
    ControllerProfile m_profile {ControllerProfile::launchpadPro};
    GridOperatingMode m_operatingMode {GridOperatingMode::clipLaunch};
    ParameterPickupMode m_pickupMode {ParameterPickupMode::pickup};
    KnobBank m_activeBank {KnobBank::trackVolume};

    // 64-Pad Grid
    std::array<GridPadState, TotalPads> m_padStates {};
    std::size_t m_activeStep {0};

    // 8 Hardware Encoders
    std::array<HardwareEncoderState, NumEncoders> m_encoders {};
};

} // namespace dawg::engine
