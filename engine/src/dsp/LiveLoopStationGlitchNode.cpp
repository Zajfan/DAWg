#include <dawg/engine/dsp/LiveLoopStationGlitchNode.h>

#include <algorithm>
#include <cmath>

namespace dawg::engine::dsp {

LiveLoopStationGlitchNode::LiveLoopStationGlitchNode() noexcept
{
    m_glitchL.resize(maxGlitchBufferSamples, 0.0f);
    m_glitchR.resize(maxGlitchBufferSamples, 0.0f);

    for (auto& d : m_decks)
    {
        d.bufferL.resize(LooperDeck::maxLoopSamples, 0.0f);
        d.bufferR.resize(LooperDeck::maxLoopSamples, 0.0f);
        d.undoL.resize(LooperDeck::maxLoopSamples, 0.0f);
        d.undoR.resize(LooperDeck::maxLoopSamples, 0.0f);
    }
}

bool LiveLoopStationGlitchNode::prepare(double sampleRate, std::size_t /*maxBlockSize*/, std::uint16_t /*channelCount*/)
{
    m_sampleRate = (sampleRate > 1000.0) ? sampleRate : 48000.0;
    reset();
    return true;
}

void LiveLoopStationGlitchNode::reset() noexcept
{
    for (auto& d : m_decks)
    {
        d.state = DeckState::empty;
        d.loopLength = 0;
        d.playhead = 0.0;
        d.isHalfSpeed = false;
        d.isReversed = false;
    }
    m_glitchEnabled.store(false, std::memory_order_release);
    m_glitchWritePos = 0;
    m_glitchReadPos = 0.0;
    m_tapeStopping.store(false, std::memory_order_release);
    m_tapeSpeed = 1.0;
}

void LiveLoopStationGlitchNode::triggerDeckAction(const std::size_t deckIndex) noexcept
{
    if (deckIndex >= numDecks) return;
    auto& deck = m_decks[deckIndex];

    switch (deck.state)
    {
    case DeckState::empty:
        deck.state = DeckState::recording;
        deck.loopLength = 0;
        deck.playhead = 0.0;
        break;

    case DeckState::recording:
        deck.state = DeckState::playing;
        deck.playhead = 0.0;
        // Snapshot undo buffer
        std::copy(deck.bufferL.begin(), deck.bufferL.begin() + deck.loopLength, deck.undoL.begin());
        std::copy(deck.bufferR.begin(), deck.bufferR.begin() + deck.loopLength, deck.undoR.begin());
        break;

    case DeckState::playing:
        deck.state = DeckState::overdubbing;
        // Save current layer for undo
        std::copy(deck.bufferL.begin(), deck.bufferL.begin() + deck.loopLength, deck.undoL.begin());
        std::copy(deck.bufferR.begin(), deck.bufferR.begin() + deck.loopLength, deck.undoR.begin());
        break;

    case DeckState::overdubbing:
        deck.state = DeckState::playing;
        break;

    case DeckState::stopped:
        deck.state = DeckState::playing;
        deck.playhead = 0.0;
        break;
    }
}

void LiveLoopStationGlitchNode::stopDeck(const std::size_t deckIndex) noexcept
{
    if (deckIndex < numDecks && m_decks[deckIndex].state != DeckState::empty)
    {
        m_decks[deckIndex].state = DeckState::stopped;
    }
}

void LiveLoopStationGlitchNode::clearDeck(const std::size_t deckIndex) noexcept
{
    if (deckIndex < numDecks)
    {
        auto& d = m_decks[deckIndex];
        d.state = DeckState::empty;
        d.loopLength = 0;
        d.playhead = 0.0;
    }
}

void LiveLoopStationGlitchNode::setDeckHalfSpeed(const std::size_t deckIndex, const bool halfSpeed) noexcept
{
    if (deckIndex < numDecks)
        m_decks[deckIndex].isHalfSpeed = halfSpeed;
}

void LiveLoopStationGlitchNode::setDeckReverse(const std::size_t deckIndex, const bool reverse) noexcept
{
    if (deckIndex < numDecks)
        m_decks[deckIndex].isReversed = reverse;
}

void LiveLoopStationGlitchNode::multiplyDeck(const std::size_t deckIndex, const std::size_t factor) noexcept
{
    if (deckIndex >= numDecks || factor <= 1) return;
    auto& d = m_decks[deckIndex];
    if (d.loopLength == 0) return;

    const std::size_t oldLen = d.loopLength;
    const std::size_t newLen = std::min(oldLen * factor, LooperDeck::maxLoopSamples);

    for (std::size_t copy = 1; copy < factor && oldLen * (copy + 1) <= newLen; ++copy)
    {
        std::copy(d.bufferL.begin(), d.bufferL.begin() + oldLen, d.bufferL.begin() + (oldLen * copy));
        std::copy(d.bufferR.begin(), d.bufferR.begin() + oldLen, d.bufferR.begin() + (oldLen * copy));
    }
    d.loopLength = newLen;
}

void LiveLoopStationGlitchNode::undoDeck(const std::size_t deckIndex) noexcept
{
    if (deckIndex < numDecks && m_decks[deckIndex].loopLength > 0)
    {
        auto& d = m_decks[deckIndex];
        std::swap(d.bufferL, d.undoL);
        std::swap(d.bufferR, d.undoR);
    }
}

DeckState LiveLoopStationGlitchNode::deckState(const std::size_t deckIndex) const noexcept
{
    if (deckIndex < numDecks) return m_decks[deckIndex].state;
    return DeckState::empty;
}

std::size_t LiveLoopStationGlitchNode::deckLoopLength(const std::size_t deckIndex) const noexcept
{
    if (deckIndex < numDecks) return m_decks[deckIndex].loopLength;
    return 0;
}

void LiveLoopStationGlitchNode::setGlitchEnabled(const bool enabled) noexcept
{
    m_glitchEnabled.store(enabled, std::memory_order_release);
    if (enabled)
    {
        m_glitchReadPos = static_cast<double>(m_glitchWritePos);
    }
}

bool LiveLoopStationGlitchNode::isGlitchEnabled() const noexcept
{
    return m_glitchEnabled.load(std::memory_order_acquire);
}

void LiveLoopStationGlitchNode::setRepeatDivision(const GlitchRepeatDivision div) noexcept
{
    m_glitchDiv.store(div, std::memory_order_release);
}

GlitchRepeatDivision LiveLoopStationGlitchNode::repeatDivision() const noexcept
{
    return m_glitchDiv.load(std::memory_order_acquire);
}

void LiveLoopStationGlitchNode::setTempoBpm(const double bpm) noexcept
{
    m_bpm.store(std::clamp(bpm, 20.0, 400.0), std::memory_order_release);
}

double LiveLoopStationGlitchNode::tempoBpm() const noexcept
{
    return m_bpm.load(std::memory_order_acquire);
}

void LiveLoopStationGlitchNode::triggerTapeStop(const float stopDurationMs) noexcept
{
    const double dur = std::clamp(static_cast<double>(stopDurationMs), 50.0, 3000.0);
    const double samples = m_sampleRate * (dur * 0.001);
    m_tapeDecayCoeff = std::exp(-5.0 / samples); // Drops to near zero over duration
    m_tapeSpeed = 1.0;
    m_tapeStopping.store(true, std::memory_order_release);
}

bool LiveLoopStationGlitchNode::isTapeStopping() const noexcept
{
    return m_tapeStopping.load(std::memory_order_acquire);
}

void LiveLoopStationGlitchNode::handleMidiEvent(const MidiEvent& event) noexcept
{
    if (event.size < 3) return;
    const auto status = static_cast<std::uint8_t>(event.data[0]) & 0xF0;
    const auto d1 = static_cast<std::uint8_t>(event.data[1]) & 0x7F;
    const auto d2 = static_cast<std::uint8_t>(event.data[2]) & 0x7F;

    if (status == 0x90 && d2 > 0) // Note On
    {
        // Notes 36..39 trigger Deck 1..4 Actions
        if (d1 >= 36 && d1 < static_cast<std::uint8_t>(36 + numDecks))
        {
            triggerDeckAction(static_cast<std::size_t>(d1 - 36));
        }
        else if (d1 == 48) // Note 48 triggers Glitch On
        {
            setGlitchEnabled(true);
        }
        else if (d1 == 49) // Note 49 triggers Tape Stop
        {
            triggerTapeStop(400.0f);
        }
    }
    else if (status == 0x80 || (status == 0x90 && d2 == 0)) // Note Off
    {
        if (d1 == 48)
        {
            setGlitchEnabled(false);
        }
    }
}

void LiveLoopStationGlitchNode::process(ProcessContext& context) noexcept
{
    auto& audio = context.audio;
    if (audio.channels == nullptr || audio.channelCount == 0 || audio.frameCount == 0)
        return;

    float* inL = audio.channels[0];
    float* inR = (audio.channelCount > 1 && audio.channels[1] != nullptr) ? audio.channels[1] : inL;

    const bool glitch = m_glitchEnabled.load(std::memory_order_relaxed);
    const auto div = m_glitchDiv.load(std::memory_order_relaxed);
    const double bpm = m_bpm.load(std::memory_order_relaxed);

    // Compute repeat slice length based on tempo division
    const double beatSamples = (60.0 / bpm) * m_sampleRate;
    double divFactor = 0.25; // default 1/16
    switch (div)
    {
    case GlitchRepeatDivision::divQuarter:      divFactor = 1.0; break;
    case GlitchRepeatDivision::divEighth:       divFactor = 0.5; break;
    case GlitchRepeatDivision::divSixteenth:    divFactor = 0.25; break;
    case GlitchRepeatDivision::divThirtySecond: divFactor = 0.125; break;
    case GlitchRepeatDivision::divSixtyFourth:   divFactor = 0.0625; break;
    }
    const auto repeatSliceSamples = static_cast<std::size_t>(std::max(16.0, beatSamples * divFactor));

    const bool tapeStopping = m_tapeStopping.load(std::memory_order_relaxed);

    for (std::size_t i = 0; i < audio.frameCount; ++i)
    {
        const float liveInL = inL[i];
        const float liveInR = inR[i];

        // 1. Write to Glitch ring buffer
        m_glitchL[m_glitchWritePos] = liveInL;
        m_glitchR[m_glitchWritePos] = liveInR;
        m_glitchWritePos = (m_glitchWritePos + 1) % maxGlitchBufferSamples;

        // 2. Process Looper Decks
        float looperSumL = liveInL;
        float looperSumR = liveInR;

        for (auto& deck : m_decks)
        {
            if (deck.state == DeckState::empty || deck.state == DeckState::stopped)
                continue;

            if (deck.state == DeckState::recording)
            {
                if (deck.loopLength < LooperDeck::maxLoopSamples)
                {
                    deck.bufferL[deck.loopLength] = liveInL;
                    deck.bufferR[deck.loopLength] = liveInR;
                    ++deck.loopLength;
                }
            }
            else if (deck.state == DeckState::playing || deck.state == DeckState::overdubbing)
            {
                if (deck.loopLength > 0)
                {
                    const auto idx = static_cast<std::size_t>(deck.playhead) % deck.loopLength;
                    looperSumL += deck.bufferL[idx] * deck.level;
                    looperSumR += deck.bufferR[idx] * deck.level;

                    if (deck.state == DeckState::overdubbing)
                    {
                        deck.bufferL[idx] = deck.bufferL[idx] * deck.feedback + liveInL;
                        deck.bufferR[idx] = deck.bufferR[idx] * deck.feedback + liveInR;
                    }

                    // Advance playhead
                    const double speed = deck.isHalfSpeed ? 0.5 : 1.0;
                    if (deck.isReversed)
                    {
                        deck.playhead -= speed;
                        if (deck.playhead < 0.0) deck.playhead += static_cast<double>(deck.loopLength);
                    }
                    else
                    {
                        deck.playhead += speed;
                        if (deck.playhead >= static_cast<double>(deck.loopLength)) deck.playhead -= static_cast<double>(deck.loopLength);
                    }
                }
            }
        }

        // 3. Process Glitch Stutter / Beat Repeater
        float finalL = looperSumL;
        float finalR = looperSumR;

        if (glitch)
        {
            auto rIdx = static_cast<std::size_t>(m_glitchReadPos) % maxGlitchBufferSamples;
            finalL = m_glitchL[rIdx];
            finalR = m_glitchR[rIdx];

            m_glitchReadPos += 1.0;
            // Wrap within repeat slice window
            const double sliceOffset = std::fmod(m_glitchReadPos, static_cast<double>(repeatSliceSamples));
            if (sliceOffset >= static_cast<double>(repeatSliceSamples) - 1.0)
            {
                m_glitchReadPos -= static_cast<double>(repeatSliceSamples);
                if (m_glitchReadPos < 0.0) m_glitchReadPos += static_cast<double>(maxGlitchBufferSamples);
            }
        }

        // 4. Process Tape Stop
        if (tapeStopping)
        {
            finalL *= static_cast<float>(m_tapeSpeed);
            finalR *= static_cast<float>(m_tapeSpeed);
            m_tapeSpeed *= m_tapeDecayCoeff;
            if (m_tapeSpeed < 0.005)
            {
                m_tapeSpeed = 0.0;
                m_tapeStopping.store(false, std::memory_order_release);
            }
        }

        inL[i] = finalL;
        inR[i] = finalR;
    }
}

} // namespace dawg::engine::dsp
