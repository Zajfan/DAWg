#pragma once

#include <dawg/engine/MidiEvent.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace dawg::engine {

enum class McuType : std::uint8_t
{
    none,
    transport,
    fader,
    faderTouch,
    vpot,
    button,
    jogWheel
};

enum class McuTransport : std::uint8_t
{
    none,
    play,
    stop,
    record,
    rewind,
    fastForward,
    cycleLoop
};

enum class McuButton : std::uint8_t
{
    none,
    arm,
    solo,
    mute,
    select
};

enum class VpotRingMode : std::uint8_t
{
    singleDot = 0x00,
    boostCut  = 0x10,
    wrap      = 0x20,
    spread    = 0x30
};

enum class LaunchpadColor : std::uint8_t
{
    off = 0,
    red = 5,
    orange = 9,
    yellow = 13,
    amber = 17,
    green = 21,
    cyan = 37,
    blue = 45,
    purple = 53,
    white = 3
};

enum class LaunchpadLedMode : std::uint8_t
{
    staticColor,
    flashing,
    pulsing
};

enum class MidiCurveTaper : std::uint8_t
{
    linear = 0,
    exponential = 1,
    logarithmic = 2
};

struct McuEvent
{
    McuType type {McuType::none};
    McuTransport transport {McuTransport::none};
    McuButton button {McuButton::none};
    std::uint8_t trackIndex {0}; // 0..7 for tracks 1..8, 8 for master
    bool buttonState {false};
    float value {0.0f}; // 0.0..1.0 for fader, step delta for vpot / jog wheel
};

struct MidiLearnBinding
{
    std::uint32_t bindingId {0};
    std::uint8_t channel {0}; // 0 = any channel, 1..16
    std::uint8_t ccNumber {0};
    std::uint32_t targetId {0};
    float minValue {0.0f};
    float maxValue {1.0f};
    MidiCurveTaper taper {MidiCurveTaper::linear};
    bool softTakeover {false};
    float currentTargetValue {0.5f};
    bool isCaught {true};
    bool active {false};
};

struct MidiLearnResult
{
    bool triggered {false};
    std::uint32_t targetId {0};
    float value {0.0f};
};

struct ProcessedMidiControl
{
    McuEvent mcuEvent {};
    MidiLearnResult learnResult {};
    bool learnedNewBinding {false};
    std::uint32_t learnedBindingId {0};
};

class MidiControllerManager
{
public:
    static constexpr std::size_t maxBindings = 128;

    MidiControllerManager() noexcept = default;
    ~MidiControllerManager() = default;

    // MCU configuration
    void setMcuEnabled(bool enabled) noexcept { m_mcuEnabled = enabled; }
    [[nodiscard]] bool isMcuEnabled() const noexcept { return m_mcuEnabled; }

    // MIDI Learn API
    void startLearn(std::uint32_t targetId, float minValue = 0.0f, float maxValue = 1.0f,
                    MidiCurveTaper taper = MidiCurveTaper::linear, bool softTakeover = false) noexcept;
    void cancelLearn() noexcept;
    [[nodiscard]] bool isLearning() const noexcept { return m_learningActive; }
    [[nodiscard]] std::uint32_t learningTargetId() const noexcept { return m_learningTargetId; }

    [[nodiscard]] std::uint32_t addBinding(std::uint8_t channel, std::uint8_t ccNumber,
                                           std::uint32_t targetId, float minValue = 0.0f,
                                           float maxValue = 1.0f,
                                           MidiCurveTaper taper = MidiCurveTaper::linear,
                                           bool softTakeover = false) noexcept;
    bool removeBinding(std::uint32_t bindingId) noexcept;
    void clearBindings() noexcept;
    void updateTargetValue(std::uint32_t targetId, float currentValue) noexcept;
    [[nodiscard]] std::size_t bindingCount() const noexcept;
    [[nodiscard]] const std::array<MidiLearnBinding, maxBindings>& bindings() const noexcept { return m_bindings; }

    // Real-time event parsing (zero heap allocation)
    [[nodiscard]] ProcessedMidiControl processEvent(const MidiEvent& event) noexcept;

    // Bank navigation (8-channel banks)
    void setBank(std::uint8_t bank) noexcept { m_activeBank = bank; }
    [[nodiscard]] std::uint8_t bank() const noexcept { return m_activeBank; }
    void bankUp() noexcept { ++m_activeBank; }
    void bankDown() noexcept { if (m_activeBank > 0) --m_activeBank; }

    // Serialization for persistent project mappings
    [[nodiscard]] std::string serializeBindings() const;
    bool deserializeBindings(const std::string& serialized);

    // Hardware Feedback generators for motorized faders, LEDs, scribble strips & Launchpad RGB
    [[nodiscard]] static MidiEvent makeFaderFeedback(std::uint8_t trackIndex, float normalizedValue) noexcept;
    [[nodiscard]] static MidiEvent makeButtonLed(std::uint8_t note, bool on) noexcept;
    [[nodiscard]] static MidiEvent makeVpotRing(std::uint8_t trackIndex, float normalizedValue, VpotRingMode mode = VpotRingMode::singleDot) noexcept;

    // Launchpad & APC RGB matrix pad output
    [[nodiscard]] static MidiEvent makeLaunchpadPad(std::uint8_t trackIdx, std::uint8_t sceneIdx, LaunchpadColor color, LaunchpadLedMode mode = LaunchpadLedMode::staticColor) noexcept;

    // MCU Scribble Strip & Timecode SysEx generators
    [[nodiscard]] static std::vector<std::byte> makeScribbleStripSysEx(std::uint8_t trackIndex, const std::string& line1, const std::string& line2);
    [[nodiscard]] static std::vector<std::byte> makeTimecodeDisplaySysEx(int bars, int beats, int sixteenths);

private:
    [[nodiscard]] McuEvent parseMcuEvent(std::uint8_t statusType, std::uint8_t channel,
                                         std::uint8_t byte1, std::uint8_t byte2) const noexcept;

    bool m_mcuEnabled {true};
    std::uint8_t m_activeBank {0};

    bool m_learningActive {false};
    std::uint32_t m_learningTargetId {0};
    float m_learningMinValue {0.0f};
    float m_learningMaxValue {1.0f};
    MidiCurveTaper m_learningTaper {MidiCurveTaper::linear};
    bool m_learningSoftTakeover {false};

    std::uint32_t m_nextBindingId {1};
    std::array<MidiLearnBinding, maxBindings> m_bindings {};
};

} // namespace dawg::engine
