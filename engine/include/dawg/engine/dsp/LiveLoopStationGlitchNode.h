#pragma once

#include <dawg/engine/AudioBufferView.h>
#include <dawg/engine/MidiEvent.h>
#include <dawg/engine/ProcessorNode.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace dawg::engine::dsp {

enum class DeckState : std::uint8_t
{
    empty,
    recording,
    playing,
    overdubbing,
    stopped
};

enum class GlitchRepeatDivision : std::uint8_t
{
    divQuarter,
    divEighth,
    divSixteenth,
    divThirtySecond,
    divSixtyFourth
};

struct LooperDeck
{
    static constexpr std::size_t maxLoopSamples = 48000 * 30; // 30 seconds max per deck
    std::vector<float> bufferL;
    std::vector<float> bufferR;
    std::vector<float> undoL;
    std::vector<float> undoR;

    DeckState state {DeckState::empty};
    std::size_t loopLength {0};
    double playhead {0.0};
    bool isHalfSpeed {false};
    bool isReversed {false};
    float level {1.0f};
    float feedback {0.85f}; // Overdub decay feedback
};

class LiveLoopStationGlitchNode final : public ProcessorNode
{
public:
    static constexpr std::size_t numDecks = 4;
    static constexpr std::size_t maxGlitchBufferSamples = 48000 * 2; // 2 second stutter buffer

    LiveLoopStationGlitchNode() noexcept;
    ~LiveLoopStationGlitchNode() override = default;

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void process(ProcessContext& context) noexcept override;
    void reset() noexcept override;

    [[nodiscard]] std::string_view name() const noexcept { return "Live Loop Station & Buffer Glitch FX"; }

    // =========================================================================
    // 4-Deck Multi-Track Looper
    // =========================================================================
    void triggerDeckAction(std::size_t deckIndex) noexcept; // Empty->Rec->Play->Overdub->Play cycle
    void stopDeck(std::size_t deckIndex) noexcept;
    void clearDeck(std::size_t deckIndex) noexcept;
    void setDeckHalfSpeed(std::size_t deckIndex, bool halfSpeed) noexcept;
    void setDeckReverse(std::size_t deckIndex, bool reverse) noexcept;
    void multiplyDeck(std::size_t deckIndex, std::size_t factor) noexcept; // 2x or 4x
    void undoDeck(std::size_t deckIndex) noexcept;

    [[nodiscard]] DeckState deckState(std::size_t deckIndex) const noexcept;
    [[nodiscard]] std::size_t deckLoopLength(std::size_t deckIndex) const noexcept;

    // =========================================================================
    // Real-Time Beat Repeater & Buffer Glitch FX
    // =========================================================================
    void setGlitchEnabled(bool enabled) noexcept;
    [[nodiscard]] bool isGlitchEnabled() const noexcept;

    void setRepeatDivision(GlitchRepeatDivision div) noexcept;
    [[nodiscard]] GlitchRepeatDivision repeatDivision() const noexcept;

    void setTempoBpm(double bpm) noexcept;
    [[nodiscard]] double tempoBpm() const noexcept;

    void triggerTapeStop(float stopDurationMs) noexcept;
    [[nodiscard]] bool isTapeStopping() const noexcept;

    // MIDI Foot Pedal Action Binding
    void handleMidiEvent(const MidiEvent& event) noexcept;

private:
    double m_sampleRate {48000.0};
    std::atomic<double> m_bpm {120.0};

    std::array<LooperDeck, numDecks> m_decks {};

    // Glitch Beat Repeater state
    std::atomic<bool> m_glitchEnabled {false};
    std::atomic<GlitchRepeatDivision> m_glitchDiv {GlitchRepeatDivision::divSixteenth};
    std::vector<float> m_glitchL;
    std::vector<float> m_glitchR;
    std::size_t m_glitchWritePos {0};
    double m_glitchReadPos {0.0};

    // Tape Stop state
    std::atomic<bool> m_tapeStopping {false};
    double m_tapeSpeed {1.0};
    double m_tapeDecayCoeff {0.999};
};

} // namespace dawg::engine::dsp
