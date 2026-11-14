#pragma once

#include <dawg/engine/MidiEvent.h>

#include <array>
#include <cstddef>
#include <cstdint>

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
    void startLearn(std::uint32_t targetId, float minValue = 0.0f, float maxValue = 1.0f) noexcept;
    void cancelLearn() noexcept;
    [[nodiscard]] bool isLearning() const noexcept { return m_learningActive; }
    [[nodiscard]] std::uint32_t learningTargetId() const noexcept { return m_learningTargetId; }

    [[nodiscard]] std::uint32_t addBinding(std::uint8_t channel, std::uint8_t ccNumber,
                                           std::uint32_t targetId, float minValue = 0.0f,
                                           float maxValue = 1.0f) noexcept;
    bool removeBinding(std::uint32_t bindingId) noexcept;
    void clearBindings() noexcept;
    [[nodiscard]] std::size_t bindingCount() const noexcept;
    [[nodiscard]] const std::array<MidiLearnBinding, maxBindings>& bindings() const noexcept { return m_bindings; }

    // Real-time event parsing (zero heap allocation)
    [[nodiscard]] ProcessedMidiControl processEvent(const MidiEvent& event) noexcept;

    // Hardware Feedback generators for motorized faders and LEDs
    [[nodiscard]] static MidiEvent makeFaderFeedback(std::uint8_t trackIndex, float normalizedValue) noexcept;
    [[nodiscard]] static MidiEvent makeButtonLed(std::uint8_t note, bool on) noexcept;
    [[nodiscard]] static MidiEvent makeVpotRing(std::uint8_t trackIndex, float normalizedValue) noexcept;

private:
    [[nodiscard]] McuEvent parseMcuEvent(std::uint8_t statusType, std::uint8_t channel,
                                         std::uint8_t byte1, std::uint8_t byte2) const noexcept;

    bool m_mcuEnabled {true};

    bool m_learningActive {false};
    std::uint32_t m_learningTargetId {0};
    float m_learningMinValue {0.0f};
    float m_learningMaxValue {1.0f};

    std::uint32_t m_nextBindingId {1};
    std::array<MidiLearnBinding, maxBindings> m_bindings {};
};

} // namespace dawg::engine
