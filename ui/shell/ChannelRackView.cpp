#include "ChannelRackView.h"

#include <cmath>

namespace dawg::ui {

ChannelRackView::ChannelRackView(CommandHandler handler, AuditionCallback auditionCb)
    : m_commandHandler(std::move(handler))
    , m_auditionCallback(std::move(auditionCb))
{
    // Populate default standard FL Studio style channels
    m_channels.push_back({ "Kick 808",     36, false,  0.0f, 0.85f, 1, {}, {} });
    m_channels.push_back({ "Kick Punch",   35, false,  0.0f, 0.80f, 1, {}, {} });
    m_channels.push_back({ "Snare Clap",   38, false,  0.0f, 0.82f, 2, {}, {} });
    m_channels.push_back({ "Snare Studio", 40, false,  0.0f, 0.78f, 2, {}, {} });
    m_channels.push_back({ "HiHat Closed", 42, false,  0.1f, 0.75f, 3, {}, {} });
    m_channels.push_back({ "HiHat Open",   46, false, -0.1f, 0.70f, 3, {}, {} });
    m_channels.push_back({ "Perc Shaker",  69, false,  0.2f, 0.65f, 4, {}, {} });
    m_channels.push_back({ "Synth Lead",   60, false,  0.0f, 0.80f, 5, {}, {} });

    // Initialize initial default pattern (Kick on 0, 8; Snare on 4, 12; Hat on 0, 2, 4, 6, 8, 10, 12, 14)
    for (auto& ch : m_channels)
    {
        ch.steps.fill(false);
        ch.velocities.fill(100);
    }
    m_channels[0].steps[0] = true;
    m_channels[0].steps[8] = true;
    m_channels[2].steps[4] = true;
    m_channels[2].steps[12] = true;
    for (int s = 0; s < 16; s += 2)
        m_channels[4].steps[static_cast<std::size_t>(s)] = true;

    // Header controls
    m_filterCombo.addItem("All", 1);
    m_filterCombo.addItem("Audio", 2);
    m_filterCombo.addItem("Instruments", 3);
    m_filterCombo.setSelectedId(1, juce::dontSendNotification);
    addAndMakeVisible(m_filterCombo);

    m_swingSlider.setRange(0.0, 100.0, 1.0);
    m_swingSlider.setValue(0.0, juce::dontSendNotification);
    m_swingSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    m_swingSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    addAndMakeVisible(m_swingSlider);

    m_swingLabel.setText("Swing", juce::dontSendNotification);
    m_swingLabel.setFont(juce::FontOptions(11.0f));
    m_swingLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(160, 165, 175));
    addAndMakeVisible(m_swingLabel);

    m_commitButton.onClick = [this] { commitPatternToTimeline(); };
    addAndMakeVisible(m_commitButton);

    m_clearButton.onClick = [this] { clearAllSteps(); };
    addAndMakeVisible(m_clearButton);

    m_addChannelButton.onClick = [this] {
        addChannel("Synth Voice " + std::to_string(m_channels.size() + 1), 60, 1);
    };
    addAndMakeVisible(m_addChannelButton);

    startTimerHz(30);
}

ChannelRackView::~ChannelRackView()
{
    stopTimer();
}

void ChannelRackView::setViewModel(const EditorViewModel& model)
{
    m_model = model;
    repaint();
}

void ChannelRackView::clearAllSteps()
{
    for (auto& ch : m_channels)
        ch.steps.fill(false);
    repaint();
}

void ChannelRackView::addChannel(const std::string& name, std::uint8_t rootPitch, int mixerTrack)
{
    ChannelRackRow row;
    row.name = name;
    row.rootPitch = rootPitch;
    row.mixerTrack = mixerTrack;
    row.steps.fill(false);
    row.velocities.fill(100);
    m_channels.push_back(std::move(row));
    resized();
    repaint();
}

void ChannelRackView::commitPatternToTimeline()
{
    if (! m_commandHandler || m_model.project.tracks.empty())
        return;

    const double bpm = m_model.project.tempo.bpm > 0.0 ? m_model.project.tempo.bpm : 120.0;
    constexpr double sampleRate = 48000.0;
    const double samplesPerBeat = (sampleRate * 60.0) / bpm;
    const auto stepLength = static_cast<std::uint64_t>(std::max(1.0, std::round(samplesPerBeat / 4.0)));
    const auto patternLength = stepLength * 16;
    const auto targetTrackId = m_model.project.tracks.front().id;

    std::vector<project::MidiNote> notes;
    for (const auto& ch : m_channels)
    {
        if (ch.muted)
            continue;

        for (int s = 0; s < 16; ++s)
        {
            if (ch.steps[static_cast<std::size_t>(s)])
            {
                notes.push_back(project::MidiNote {
                    .id = 0,
                    .pitch = ch.rootPitch,
                    .velocity = ch.velocities[static_cast<std::size_t>(s)],
                    .startSample = static_cast<std::uint64_t>(s) * stepLength,
                    .lengthSamples = static_cast<std::uint64_t>(static_cast<double>(stepLength) * 0.85)
                });
            }
        }
    }

    if (! notes.empty())
    {
        const auto startSamplePos = m_model.playbackPosition >= 0 ? static_cast<std::uint64_t>(m_model.playbackPosition) : 0ULL;
        m_commandHandler(project::AddMidiClip {
            .clip = project::MidiClip {
                .id = 0,
                .trackId = targetTrackId,
                .startSample = startSamplePos,
                .lengthSamples = patternLength,
                .notes = std::move(notes)
            }
        });
    }
}

void ChannelRackView::timerCallback()
{
    if (! m_model.playing)
    {
        if (m_lastPlayedStep != -1)
        {
            m_lastPlayedStep = -1;
            repaint();
        }
        return;
    }

    const double bpm = m_model.project.tempo.bpm > 0.0 ? m_model.project.tempo.bpm : 120.0;
    constexpr double sampleRate = 48000.0;
    const double samplesPerBeat = (sampleRate * 60.0) / bpm;
    const double samplesPerStep = samplesPerBeat / 4.0;

    const int currentStep = static_cast<int>(std::floor(static_cast<double>(std::max<std::int64_t>(0, m_model.playbackPosition)) / samplesPerStep)) % 16;
    if (currentStep != m_lastPlayedStep)
    {
        m_lastPlayedStep = currentStep;
        if (m_auditionCallback && currentStep >= 0 && currentStep < 16)
        {
            for (std::size_t c = 0; c < m_channels.size(); ++c)
            {
                const auto& ch = m_channels[c];
                if (! ch.muted && ch.steps[static_cast<std::size_t>(currentStep)])
                {
                    m_auditionCallback(ch.rootPitch, ch.velocities[static_cast<std::size_t>(currentStep)], true);
                }
            }
        }
        repaint();
    }
}

void ChannelRackView::triggerStepAudition(int channelIndex, int stepIndex)
{
    if (! m_auditionCallback || channelIndex < 0 || static_cast<std::size_t>(channelIndex) >= m_channels.size())
        return;

    const auto& ch = m_channels[static_cast<std::size_t>(channelIndex)];
    const std::uint8_t vel = ch.velocities[static_cast<std::size_t>(stepIndex)];
    m_auditionCallback(ch.rootPitch, vel, true);
}

void ChannelRackView::mouseDown(const juce::MouseEvent& e)
{
    int channelIdx = -1;
    const int stepIdx = getStepAt(e.getPosition(), channelIdx);

    if (channelIdx >= 0 && static_cast<std::size_t>(channelIdx) < m_channels.size())
    {
        m_selectedChannel = channelIdx;

        // Check if clicked in left controls (channel button or mute)
        const int yOffset = m_headerHeight + channelIdx * m_rowHeight;
        const int localY = e.getPosition().getY() - yOffset;

        if (e.getPosition().getX() < m_leftControlsWidth && localY >= 0 && localY < m_rowHeight)
        {
            // Mute LED circle at x in [6, 22]
            if (e.getPosition().getX() >= 6 && e.getPosition().getX() <= 22)
            {
                m_channels[static_cast<std::size_t>(channelIdx)].muted = ! m_channels[static_cast<std::size_t>(channelIdx)].muted;
                repaint();
                return;
            }

            // Channel name button at x in [100, 230]
            if (e.getPosition().getX() >= 100 && e.getPosition().getX() <= 230)
            {
                if (m_auditionCallback)
                {
                    m_auditionCallback(m_channels[static_cast<std::size_t>(channelIdx)].rootPitch, 100, true);
                }
                repaint();
                return;
            }
        }

        // Clicked a step button
        if (stepIdx >= 0 && stepIdx < 16)
        {
            const bool isRightClick = e.mods.isRightButtonDown();
            auto& step = m_channels[static_cast<std::size_t>(channelIdx)].steps[static_cast<std::size_t>(stepIdx)];
            if (isRightClick)
            {
                step = false;
                m_dragPaintingState = false;
            }
            else
            {
                step = ! step;
                m_dragPaintingState = step;
                if (step)
                    triggerStepAudition(channelIdx, stepIdx);
            }
            repaint();
        }
    }
}

void ChannelRackView::mouseDrag(const juce::MouseEvent& e)
{
    int channelIdx = -1;
    const int stepIdx = getStepAt(e.getPosition(), channelIdx);

    if (channelIdx >= 0 && static_cast<std::size_t>(channelIdx) < m_channels.size() && stepIdx >= 0 && stepIdx < 16)
    {
        auto& step = m_channels[static_cast<std::size_t>(channelIdx)].steps[static_cast<std::size_t>(stepIdx)];
        if (step != m_dragPaintingState)
        {
            step = m_dragPaintingState;
            if (step)
                triggerStepAudition(channelIdx, stepIdx);
            repaint();
        }
    }
}

void ChannelRackView::mouseUp(const juce::MouseEvent& e)
{
    int channelIdx = -1;
    (void) getStepAt(e.getPosition(), channelIdx);
    if (channelIdx >= 0 && static_cast<std::size_t>(channelIdx) < m_channels.size())
    {
        if (m_auditionCallback)
        {
            m_auditionCallback(m_channels[static_cast<std::size_t>(channelIdx)].rootPitch, 0, false);
        }
    }
}

int ChannelRackView::getStepAt(juce::Point<int> pos, int& outChannelIndex) const noexcept
{
    outChannelIndex = -1;
    if (pos.getY() < m_headerHeight)
        return -1;

    const int channel = (pos.getY() - m_headerHeight) / m_rowHeight;
    if (channel < 0 || static_cast<std::size_t>(channel) >= m_channels.size())
        return -1;

    outChannelIndex = channel;

    const int startX = m_leftControlsWidth + 10;
    if (pos.getX() < startX)
        return -1;

    const int stepSlot = (pos.getX() - startX) / (m_stepWidth + m_stepSpacing);
    if (stepSlot >= 0 && stepSlot < 16)
        return stepSlot;

    return -1;
}

void ChannelRackView::paint(juce::Graphics& g)
{
    // Background matching FL Studio dark charcoal slate
    g.fillAll(juce::Colour::fromRGB(43, 46, 52));

    auto bounds = getLocalBounds();
    auto headerBounds = bounds.removeFromTop(m_headerHeight);
    drawHeader(g, headerBounds);

    for (std::size_t i = 0; i < m_channels.size(); ++i)
    {
        auto rowBounds = bounds.removeFromTop(m_rowHeight);
        drawChannelRow(g, static_cast<int>(i), rowBounds);
    }
}

void ChannelRackView::drawHeader(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    g.setColour(juce::Colour::fromRGB(34, 37, 42));
    g.fillRect(bounds);

    g.setColour(juce::Colour::fromRGB(55, 59, 68));
    g.drawHorizontalLine(bounds.getBottom() - 1, 0.0f, static_cast<float>(bounds.getWidth()));

    // Title icon & text
    g.setColour(juce::Colour::fromRGB(210, 215, 225));
    g.setFont(juce::FontOptions(13.0f).withStyle("Bold"));
    g.drawText("Channel Rack", bounds.removeFromLeft(180).withTrimmedLeft(90), juce::Justification::centredLeft);
}

void ChannelRackView::drawChannelRow(juce::Graphics& g, int channelIndex, juce::Rectangle<int> bounds)
{
    if (static_cast<std::size_t>(channelIndex) >= m_channels.size())
        return;

    const auto& ch = m_channels[static_cast<std::size_t>(channelIndex)];
    const bool isSelected = (channelIndex == m_selectedChannel);

    // Row alternating background
    g.setColour((channelIndex % 2 == 0) ? juce::Colour::fromRGB(40, 43, 49) : juce::Colour::fromRGB(36, 39, 44));
    g.fillRect(bounds);

    // 1. Mute LED circle
    const auto ledRect = juce::Rectangle<float>(8.0f, static_cast<float>(bounds.getY() + 7), 12.0f, 12.0f);
    if (! ch.muted)
    {
        g.setColour(juce::Colour::fromRGB(75, 220, 80));
        g.fillEllipse(ledRect);
        g.setColour(juce::Colour::fromRGB(180, 255, 180));
        g.fillEllipse(ledRect.reduced(3.0f));
    }
    else
    {
        g.setColour(juce::Colour::fromRGB(30, 60, 32));
        g.fillEllipse(ledRect);
    }
    g.setColour(juce::Colour::fromRGB(20, 22, 25));
    g.drawEllipse(ledRect, 1.0f);

    // 2. Mini Pan Knob
    const auto panRect = juce::Rectangle<float>(26.0f, static_cast<float>(bounds.getY() + 6), 14.0f, 14.0f);
    g.setColour(juce::Colour::fromRGB(25, 27, 31));
    g.fillEllipse(panRect);
    g.setColour(juce::Colour::fromRGB(120, 125, 135));
    g.drawEllipse(panRect, 1.0f);
    g.setColour(juce::Colour::fromRGB(200, 205, 215));
    g.drawLine(panRect.getCentreX(), panRect.getCentreY(),
               panRect.getCentreX() + std::sin(ch.pan * 2.0f) * 5.0f,
               panRect.getCentreY() - std::cos(ch.pan * 2.0f) * 5.0f, 1.5f);

    // 3. Mini Vol Knob
    const auto volRect = juce::Rectangle<float>(46.0f, static_cast<float>(bounds.getY() + 6), 14.0f, 14.0f);
    g.setColour(juce::Colour::fromRGB(25, 27, 31));
    g.fillEllipse(volRect);
    g.setColour(juce::Colour::fromRGB(120, 125, 135));
    g.drawEllipse(volRect, 1.0f);
    g.setColour(juce::Colour::fromRGB(200, 205, 215));
    const float volAngle = (ch.volume - 0.5f) * 3.0f;
    g.drawLine(volRect.getCentreX(), volRect.getCentreY(),
               volRect.getCentreX() + std::sin(volAngle) * 5.0f,
               volRect.getCentreY() - std::cos(volAngle) * 5.0f, 1.5f);

    // 4. Mixer Track Box
    const auto mixerBox = juce::Rectangle<float>(66.0f, static_cast<float>(bounds.getY() + 4), 26.0f, 20.0f);
    g.setColour(juce::Colour::fromRGB(28, 30, 35));
    g.fillRoundedRectangle(mixerBox, 3.0f);
    g.setColour(juce::Colour::fromRGB(65, 70, 80));
    g.drawRoundedRectangle(mixerBox, 3.0f, 1.0f);
    g.setColour(juce::Colour::fromRGB(180, 185, 195));
    g.setFont(juce::FontOptions(10.0f).withStyle("Bold"));
    g.drawText(ch.mixerTrack > 0 ? std::to_string(ch.mixerTrack) : "--", mixerBox, juce::Justification::centred);

    // 5. Channel Name Button
    const auto nameBox = juce::Rectangle<float>(98.0f, static_cast<float>(bounds.getY() + 3), 132.0f, 22.0f);
    g.setColour(isSelected ? juce::Colour::fromRGB(55, 60, 70) : juce::Colour::fromRGB(38, 41, 48));
    g.fillRoundedRectangle(nameBox, 3.0f);
    g.setColour(isSelected ? juce::Colour::fromRGB(80, 140, 200) : juce::Colour::fromRGB(60, 64, 74));
    g.drawRoundedRectangle(nameBox, 3.0f, 1.0f);
    g.setColour(juce::Colour::fromRGB(225, 230, 240));
    g.setFont(juce::FontOptions(11.0f).withStyle("Bold"));
    g.drawText(ch.name, nameBox.reduced(6, 0), juce::Justification::centredLeft);

    // 6. Selected Channel Pill
    if (isSelected)
    {
        g.setColour(juce::Colour::fromRGB(80, 220, 90));
        g.fillRoundedRectangle(juce::Rectangle<float>(234.0f, static_cast<float>(bounds.getY() + 4), 3.0f, 20.0f), 1.5f);
    }

    // 7. Step Sequencer Grid (16 steps)
    const int startX = m_leftControlsWidth + 10;
    for (int s = 0; s < 16; ++s)
    {
        const int stepX = startX + s * (m_stepWidth + m_stepSpacing);
        const auto stepBounds = juce::Rectangle<int>(stepX, bounds.getY() + 4, m_stepWidth, 20);
        const bool active = ch.steps[static_cast<std::size_t>(s)];
        const bool isPlayhead = (m_lastPlayedStep == s);
        drawStepButton(g, channelIndex, s, stepBounds, active, isPlayhead);
    }
}

void ChannelRackView::drawStepButton(juce::Graphics& g, int, int stepIndex, juce::Rectangle<int> bounds, bool isActive, bool isPlayhead)
{
    // Signature FL Studio 4-step color alternating blocks:
    // Steps 0..3: Light Gray
    // Steps 4..7: Coral Red / Orange
    // Steps 8..11: Light Gray
    // Steps 12..15: Coral Red / Orange
    const bool isRedBlock = ((stepIndex / 4) % 2) == 1;

    const auto fBounds = bounds.toFloat();

    if (isActive)
    {
        // Lit step
        if (isRedBlock)
        {
            g.setColour(juce::Colour::fromRGB(255, 120, 120));
            g.fillRoundedRectangle(fBounds, 3.0f);
            g.setColour(juce::Colour::fromRGB(255, 200, 200));
            g.fillRoundedRectangle(fBounds.reduced(2.0f, 4.0f), 2.0f);
        }
        else
        {
            g.setColour(juce::Colour::fromRGB(235, 240, 248));
            g.fillRoundedRectangle(fBounds, 3.0f);
            g.setColour(juce::Colour::fromRGB(255, 255, 255));
            g.fillRoundedRectangle(fBounds.reduced(2.0f, 4.0f), 2.0f);
        }
        g.setColour(juce::Colour::fromRGB(20, 22, 26));
        g.drawRoundedRectangle(fBounds, 3.0f, 1.0f);
    }
    else
    {
        // Unlit step
        if (isRedBlock)
        {
            g.setColour(juce::Colour::fromRGB(80, 42, 44));
            g.fillRoundedRectangle(fBounds, 3.0f);
            // Inset notch
            g.setColour(juce::Colour::fromRGB(140, 60, 60));
            g.fillRect(fBounds.reduced(3.0f, 6.0f));
        }
        else
        {
            g.setColour(juce::Colour::fromRGB(56, 60, 68));
            g.fillRoundedRectangle(fBounds, 3.0f);
            // Inset notch
            g.setColour(juce::Colour::fromRGB(110, 116, 128));
            g.fillRect(fBounds.reduced(3.0f, 6.0f));
        }
        g.setColour(juce::Colour::fromRGB(28, 30, 36));
        g.drawRoundedRectangle(fBounds, 3.0f, 1.0f);
    }

    if (isPlayhead)
    {
        g.setColour(juce::Colour::fromRGB(255, 225, 90));
        g.drawRoundedRectangle(fBounds.expanded(1.0f), 3.5f, 1.8f);
    }
}

void ChannelRackView::resized()
{
    auto bounds = getLocalBounds();
    auto headerBounds = bounds.removeFromTop(m_headerHeight);

    // Left dropdown "All"
    m_filterCombo.setBounds(headerBounds.removeFromLeft(80).reduced(4, 4));

    // Right controls
    m_clearButton.setBounds(headerBounds.removeFromRight(60).reduced(3, 4));
    m_commitButton.setBounds(headerBounds.removeFromRight(180).reduced(3, 4));
    m_addChannelButton.setBounds(headerBounds.removeFromRight(100).reduced(3, 4));

    headerBounds.removeFromRight(10);
    m_swingSlider.setBounds(headerBounds.removeFromRight(70).reduced(0, 8));
    m_swingLabel.setBounds(headerBounds.removeFromRight(45).reduced(0, 4));
}

} // namespace dawg::ui
