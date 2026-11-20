#include "ChannelRackView.h"

#include <cmath>

namespace dawg::ui {

ChannelRackView::ChannelRackView(CommandHandler handler, AuditionCallback auditionCb)
    : m_commandHandler(std::move(handler))
    , m_auditionCallback(std::move(auditionCb))
{
    // Populate default standard FL Studio style channels matching production trap/hip-hop kits
    m_channels.push_back({ "JJ - Kick5",     36, false,  0.0f, 0.85f, 1, "", {}, {} });
    m_channels.push_back({ "JJ - Kick2",     35, false,  0.0f, 0.80f, 1, "", {}, {} });
    m_channels.push_back({ "JJ - GoonSnclp", 38, false,  0.0f, 0.82f, 2, "", {}, {} });
    m_channels.push_back({ "JJ - Snare5",    40, false,  0.0f, 0.78f, 2, "", {}, {} });
    m_channels.push_back({ "JJ - Shaker 4",  69, false,  0.2f, 0.70f, 3, "", {}, {} });
    m_channels.push_back({ "JJ - Shaker",    70, false, -0.2f, 0.65f, 3, "", {}, {} });
    m_channels.push_back({ "JJ - Splash",    49, false,  0.1f, 0.72f, 4, "", {}, {} });
    m_channels.push_back({ "JJ - 808 Bass",  33, false,  0.0f, 0.90f, 5, "", {}, {} });

    // Initialize 32 steps pattern (matching classic FL pattern)
    for (auto& ch : m_channels)
    {
        ch.steps.fill(false);
        ch.velocities.fill(100);
    }

    // Kick 5 on 0, 10, 16, 26
    m_channels[0].steps[0] = true;
    m_channels[0].steps[10] = true;
    m_channels[0].steps[16] = true;
    m_channels[0].steps[26] = true;

    // Goon Snare on 8, 24
    m_channels[2].steps[8] = true;
    m_channels[2].steps[24] = true;

    // Snare 5 on 8, 20, 24
    m_channels[3].steps[8] = true;
    m_channels[3].steps[20] = true;
    m_channels[3].steps[24] = true;

    // Shaker on every 2nd 16th note
    for (int s = 0; s < 32; s += 2)
        m_channels[4].steps[static_cast<std::size_t>(s)] = true;

    // Splash on 0
    m_channels[6].steps[0] = true;

    // Multi-Pattern initialization: Pattern 1, Pattern 2, Pattern 3
    for (int p = 1; p <= 3; ++p)
    {
        ChannelRackPattern pat;
        pat.name = "Pattern " + std::to_string(p);
        pat.channelData.resize(m_channels.size());
        m_patterns.push_back(std::move(pat));
    }
    syncCurrentPatternToBank();

    // Pattern Selector
    for (std::size_t i = 0; i < m_patterns.size(); ++i)
    {
        m_patternCombo.addItem(m_patterns[i].name, static_cast<int>(i + 1));
    }
    m_patternCombo.setSelectedId(1, juce::dontSendNotification);
    m_patternCombo.onChange = [this] {
        const int id = m_patternCombo.getSelectedId();
        if (id > 0) switchPattern(id - 1);
    };
    addAndMakeVisible(m_patternCombo);

    m_addPatternButton.onClick = [this] { addNewPattern(); };
    m_addPatternButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(40, 44, 52));
    m_addPatternButton.setColour(juce::TextButton::textColourOffId, juce::Colour::fromRGB(220, 225, 235));
    addAndMakeVisible(m_addPatternButton);

    // Graph Editor toggle & mode
    m_graphToggleBtn.setClickingTogglesState(true);
    m_graphToggleBtn.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(40, 44, 52));
    m_graphToggleBtn.onClick = [this] {
        m_graphEditorOpen = m_graphToggleBtn.getToggleState();
        m_graphToggleBtn.setColour(juce::TextButton::buttonColourId,
            m_graphEditorOpen ? juce::Colour::fromRGB(45, 140, 220) : juce::Colour::fromRGB(40, 44, 52));
        resized();
        repaint();
    };
    addAndMakeVisible(m_graphToggleBtn);

    m_graphModeCombo.addItem("Velocity", 1);
    m_graphModeCombo.addItem("Pitch", 2);
    m_graphModeCombo.addItem("Pan", 3);
    m_graphModeCombo.setSelectedId(1, juce::dontSendNotification);
    m_graphModeCombo.onChange = [this] {
        const int id = m_graphModeCombo.getSelectedId();
        if (id == 1) m_graphMode = GraphMode::velocity;
        else if (id == 2) m_graphMode = GraphMode::pitch;
        else if (id == 3) m_graphMode = GraphMode::pan;
        repaint();
    };
    addAndMakeVisible(m_graphModeCombo);

    // Header controls
    m_filterCombo.addItem("All", 1);
    m_filterCombo.addItem("Audio", 2);
    m_filterCombo.addItem("Unsorted", 3);
    m_filterCombo.setSelectedId(1, juce::dontSendNotification);
    addAndMakeVisible(m_filterCombo);

    m_stepCountCombo.addItem("16 Steps", 16);
    m_stepCountCombo.addItem("32 Steps", 32);
    m_stepCountCombo.addItem("64 Steps", 64);
    m_stepCountCombo.setSelectedId(32, juce::dontSendNotification);
    m_stepCountCombo.onChange = [this] {
        setNumSteps(m_stepCountCombo.getSelectedId());
    };
    addAndMakeVisible(m_stepCountCombo);

    m_swingSlider.setRange(0.0, 100.0, 1.0);
    m_swingSlider.setValue(0.0, juce::dontSendNotification);
    m_swingSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    m_swingSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    addAndMakeVisible(m_swingSlider);

    m_swingLabel.setText("Swing", juce::dontSendNotification);
    m_swingLabel.setFont(juce::FontOptions(11.0f));
    m_swingLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(170, 175, 185));
    addAndMakeVisible(m_swingLabel);

    m_commitButton.onClick = [this] { commitPatternToTimeline(); };
    addAndMakeVisible(m_commitButton);

    m_clearButton.onClick = [this] { clearAllSteps(); };
    addAndMakeVisible(m_clearButton);

    m_addChannelButton.onClick = [this] {
        addChannel("Voice " + std::to_string(m_channels.size() + 1), 60, static_cast<int>(m_channels.size() + 1));
    };
    m_addChannelButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(38, 41, 48));
    m_addChannelButton.setColour(juce::TextButton::textColourOffId, juce::Colour::fromRGB(200, 205, 215));
    addAndMakeVisible(m_addChannelButton);

    m_selectedChannel = 4; // Select Shaker 4 initially like in the screenshot
    startTimerHz(30);
}

const std::string& ChannelRackView::currentPatternName() const noexcept
{
    static const std::string fallback = "Pattern 1";
    if (m_currentPatternIndex >= 0 && static_cast<std::size_t>(m_currentPatternIndex) < m_patterns.size())
        return m_patterns[static_cast<std::size_t>(m_currentPatternIndex)].name;
    return fallback;
}

void ChannelRackView::syncCurrentPatternToBank()
{
    if (m_currentPatternIndex < 0 || static_cast<std::size_t>(m_currentPatternIndex) >= m_patterns.size())
        return;

    auto& pat = m_patterns[static_cast<std::size_t>(m_currentPatternIndex)];
    pat.channelData.resize(m_channels.size());
    for (std::size_t c = 0; c < m_channels.size(); ++c)
    {
        pat.channelData[c].steps = m_channels[c].steps;
        pat.channelData[c].velocities = m_channels[c].velocities;
        pat.channelData[c].pitchOffsets = m_channels[c].pitchOffsets;
        pat.channelData[c].panOffsets = m_channels[c].panOffsets;
    }
}

void ChannelRackView::loadPatternFromBank(int patternIndex)
{
    if (patternIndex < 0 || static_cast<std::size_t>(patternIndex) >= m_patterns.size())
        return;

    m_currentPatternIndex = patternIndex;
    const auto& pat = m_patterns[static_cast<std::size_t>(patternIndex)];
    for (std::size_t c = 0; c < m_channels.size(); ++c)
    {
        if (c < pat.channelData.size())
        {
            m_channels[c].steps = pat.channelData[c].steps;
            m_channels[c].velocities = pat.channelData[c].velocities;
            m_channels[c].pitchOffsets = pat.channelData[c].pitchOffsets;
            m_channels[c].panOffsets = pat.channelData[c].panOffsets;
        }
        else
        {
            m_channels[c].steps.fill(false);
            m_channels[c].velocities.fill(100);
            m_channels[c].pitchOffsets.fill(0);
            m_channels[c].panOffsets.fill(0);
        }
    }
    m_patternCombo.setSelectedId(patternIndex + 1, juce::dontSendNotification);
    repaint();
}

void ChannelRackView::switchPattern(int patternIndex)
{
    if (patternIndex == m_currentPatternIndex)
        return;

    syncCurrentPatternToBank();
    loadPatternFromBank(patternIndex);
}

void ChannelRackView::addNewPattern()
{
    syncCurrentPatternToBank();
    ChannelRackPattern newPat;
    newPat.name = "Pattern " + std::to_string(m_patterns.size() + 1);
    newPat.channelData.resize(m_channels.size());
    for (auto& cd : newPat.channelData)
    {
        cd.steps.fill(false);
        cd.velocities.fill(100);
        cd.pitchOffsets.fill(0);
        cd.panOffsets.fill(0);
    }
    m_patterns.push_back(std::move(newPat));
    m_patternCombo.addItem(m_patterns.back().name, static_cast<int>(m_patterns.size()));
    switchPattern(static_cast<int>(m_patterns.size() - 1));
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

void ChannelRackView::setNumSteps(int steps)
{
    m_numSteps = std::clamp(steps, 16, 64);
    resized();
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
    m_selectedChannel = static_cast<int>(m_channels.size() - 1);
    resized();
    repaint();
}

bool ChannelRackView::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& file : files)
    {
        if (file.endsWithIgnoreCase(".wav") || file.endsWithIgnoreCase(".mp3") ||
            file.endsWithIgnoreCase(".flac") || file.endsWithIgnoreCase(".aif") ||
            file.endsWithIgnoreCase(".ogg"))
        {
            return true;
        }
    }
    return false;
}

void ChannelRackView::filesDropped(const juce::StringArray& files, int /*x*/, int y)
{
    if (files.isEmpty())
        return;

    const int targetChannel = (y - m_headerHeight) / m_rowHeight;
    const juce::File f(files[0]);
    const auto soundName = f.getFileNameWithoutExtension().toStdString();

    if (targetChannel >= 0 && static_cast<std::size_t>(targetChannel) < m_channels.size())
    {
        m_channels[static_cast<std::size_t>(targetChannel)].name = soundName;
        m_channels[static_cast<std::size_t>(targetChannel)].samplePath = f.getFullPathName().toStdString();
        m_selectedChannel = targetChannel;
    }
    else
    {
        // Add as a new channel
        addChannel(soundName, 36, static_cast<int>(m_channels.size() + 1));
        m_channels.back().samplePath = f.getFullPathName().toStdString();
    }
    repaint();
}

void ChannelRackView::commitPatternToTimeline()
{
    if (! m_commandHandler || m_model.project.tracks.empty())
        return;

    const double bpm = m_model.project.tempo.bpm > 0.0 ? m_model.project.tempo.bpm : 120.0;
    const double sampleRate = m_model.sampleRate > 0.0 ? m_model.sampleRate : 48000.0;
    const double samplesPerBeat = (sampleRate * 60.0) / bpm;
    const auto stepLength = static_cast<std::uint64_t>(std::max(1.0, std::round(samplesPerBeat / 4.0)));
    const auto patternLength = stepLength * static_cast<std::uint64_t>(m_numSteps);
    auto targetTrackId = m_model.project.tracks.front().id;
    if (m_selectedTrackId.has_value())
    {
        for (const auto& t : m_model.project.tracks)
        {
            if (t.id == *m_selectedTrackId)
            {
                targetTrackId = t.id;
                break;
            }
        }
    }

    std::vector<project::MidiNote> notes;
    const float swingVal = static_cast<float>(m_swingSlider.getValue()) / 100.0f;
    for (const auto& ch : m_channels)
    {
        if (ch.muted)
            continue;

        for (int s = 0; s < m_numSteps; ++s)
        {
            if (ch.steps[static_cast<std::size_t>(s)])
            {
                const double swingOffset = (s % 2 == 1) ? (static_cast<double>(swingVal) * static_cast<double>(stepLength) * 0.35) : 0.0;
                const auto noteStart = static_cast<std::uint64_t>(static_cast<double>(static_cast<std::uint64_t>(s) * stepLength) + swingOffset);
                const int effectivePitch = std::clamp<int>(static_cast<int>(ch.rootPitch) + static_cast<int>(ch.pitchOffsets[static_cast<std::size_t>(s)]), 0, 127);
                notes.push_back(project::MidiNote {
                    .id = 0,
                    .pitch = static_cast<std::uint8_t>(effectivePitch),
                    .velocity = ch.velocities[static_cast<std::size_t>(s)],
                    .startSample = noteStart,
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
            if (m_auditionCallback && m_lastPlayedStep >= 0 && m_lastPlayedStep < m_numSteps)
            {
                for (const auto& ch : m_channels)
                {
                    if (ch.steps[static_cast<std::size_t>(m_lastPlayedStep)])
                    {
                        m_auditionCallback(ch.rootPitch, 0, false);
                    }
                }
            }
            m_lastPlayedStep = -1;
            repaint();
        }
        return;
    }

    const double bpm = m_model.project.tempo.bpm > 0.0 ? m_model.project.tempo.bpm : 120.0;
    const double sampleRate = m_model.sampleRate > 0.0 ? m_model.sampleRate : 48000.0;
    const double samplesPerBeat = (sampleRate * 60.0) / bpm;
    const double samplesPerStep = samplesPerBeat / 4.0;

    const int currentStep = static_cast<int>(std::floor(static_cast<double>(std::max<std::int64_t>(0, m_model.playbackPosition)) / samplesPerStep)) % m_numSteps;
    if (currentStep != m_lastPlayedStep)
    {
        if (m_auditionCallback && m_lastPlayedStep >= 0 && m_lastPlayedStep < m_numSteps)
        {
            for (const auto& ch : m_channels)
            {
                if (ch.steps[static_cast<std::size_t>(m_lastPlayedStep)])
                {
                    m_auditionCallback(ch.rootPitch, 0, false);
                }
            }
        }

        m_lastPlayedStep = currentStep;
        if (m_auditionCallback && currentStep >= 0 && currentStep < m_numSteps)
        {
            for (std::size_t c = 0; c < m_channels.size(); ++c)
            {
                const auto& ch = m_channels[c];
                if (! ch.muted && ch.steps[static_cast<std::size_t>(currentStep)])
                {
                    const int effectivePitch = std::clamp<int>(static_cast<int>(ch.rootPitch) + static_cast<int>(ch.pitchOffsets[static_cast<std::size_t>(currentStep)]), 0, 127);
                    m_auditionCallback(static_cast<std::uint8_t>(effectivePitch), ch.velocities[static_cast<std::size_t>(currentStep)], true);
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
    const int effectivePitch = std::clamp<int>(static_cast<int>(ch.rootPitch) + static_cast<int>(ch.pitchOffsets[static_cast<std::size_t>(stepIndex)]), 0, 127);
    m_auditionCallback(static_cast<std::uint8_t>(effectivePitch), vel, true);
}

void ChannelRackView::showChannelContextMenu(int channelIndex)
{
    if (channelIndex < 0 || static_cast<std::size_t>(channelIndex) >= m_channels.size())
        return;

    juce::PopupMenu menu;
    menu.addItem(1, "Piano roll");
    menu.addItem(2, "Load Sample...");
    menu.addItem(3, "Assign to Next Free Mixer Track");
    menu.addSeparator();
    menu.addItem(4, "Rename Channel...");
    menu.addItem(5, "Clone Channel");
    menu.addItem(6, "Delete Channel");

    menu.showMenuAsync(juce::PopupMenu::Options {}, [this, channelIndex](int result) {
        if (channelIndex < 0 || static_cast<std::size_t>(channelIndex) >= m_channels.size())
            return;

        auto& ch = m_channels[static_cast<std::size_t>(channelIndex)];
        if (result == 1)
        {
            if (m_openPianoRollCb)
                m_openPianoRollCb();
        }
        else if (result == 2)
        {
            auto chooser = std::make_shared<juce::FileChooser>(
                "Select Audio Sample",
                juce::File::getSpecialLocation(juce::File::userHomeDirectory),
                "*.wav;*.flac;*.ogg;*.mp3;*.aif;*.aiff");

            chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [this, channelIndex, chooser](const juce::FileChooser& fc) {
                    const auto file = fc.getResult();
                    if (file.existsAsFile() && channelIndex >= 0 && channelIndex < static_cast<int>(m_channels.size()))
                    {
                        auto& channel = m_channels[static_cast<std::size_t>(channelIndex)];
                        channel.samplePath = file.getFullPathName().toStdString();
                        channel.name = file.getFileNameWithoutExtension().toStdString();
                        repaint();
                    }
                });
        }
        else if (result == 3)
        {
            int maxTrack = 1;
            for (const auto& row : m_channels)
                maxTrack = std::max(maxTrack, row.mixerTrack);
            ch.mixerTrack = maxTrack + 1;
            repaint();
        }
        else if (result == 4)
        {
            auto* alert = new juce::AlertWindow("Rename Channel", "Enter new channel name:", juce::AlertWindow::NoIcon);
            alert->addTextEditor("name", ch.name, "Channel Name");
            alert->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
            alert->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
            alert->enterModalState(true, juce::ModalCallbackFunction::create([this, channelIndex, alert](int res) {
                if (res == 1 && channelIndex >= 0 && channelIndex < static_cast<int>(m_channels.size()))
                {
                    const auto newName = alert->getTextEditorContents("name").trim().toStdString();
                    if (! newName.empty())
                    {
                        m_channels[static_cast<std::size_t>(channelIndex)].name = newName;
                        repaint();
                    }
                }
                delete alert;
            }));
        }
        else if (result == 5)
        {
            ChannelRackRow copy = ch;
            copy.name += " (Clone)";
            m_channels.insert(m_channels.begin() + channelIndex + 1, std::move(copy));
            resized();
            repaint();
        }
        else if (result == 6 && m_channels.size() > 1)
        {
            m_channels.erase(m_channels.begin() + channelIndex);
            if (m_selectedChannel >= static_cast<int>(m_channels.size()))
                m_selectedChannel = static_cast<int>(m_channels.size() - 1);
            resized();
            repaint();
        }
    });
}

void ChannelRackView::mouseDown(const juce::MouseEvent& e)
{
    int channelIdx = -1;
    const int stepIdx = getStepAt(e.getPosition(), channelIdx);

    // Check if clicked in Graph Editor
    if (m_graphEditorOpen)
    {
        auto bounds = getLocalBounds();
        bounds.removeFromTop(m_headerHeight);
        bounds.removeFromBottom(m_bottomBarHeight);
        auto graphBounds = bounds.removeFromBottom(m_graphEditorHeight);

        if (graphBounds.contains(e.getPosition()))
        {
            m_activeDragControl = DragControl::graphEditor;
            const int startX = m_leftControlsWidth + 8;
            const int stepSlot = (e.getPosition().getX() - startX) / (m_stepWidth + m_stepSpacing) + m_scrollOffsetSteps;
            if (stepSlot >= 0 && stepSlot < m_numSteps && m_selectedChannel >= 0 && static_cast<std::size_t>(m_selectedChannel) < m_channels.size())
            {
                auto& ch = m_channels[static_cast<std::size_t>(m_selectedChannel)];
                const float localY = static_cast<float>(e.getPosition().getY() - graphBounds.getY() - 4);
                const float height = static_cast<float>(graphBounds.getHeight() - 8);

                if (m_graphMode == GraphMode::velocity)
                {
                    const float norm = std::clamp(1.0f - (localY / height), 0.05f, 1.0f);
                    ch.velocities[static_cast<std::size_t>(stepSlot)] = static_cast<std::uint8_t>(norm * 127.0f);
                }
                else if (m_graphMode == GraphMode::pitch)
                {
                    const float norm = std::clamp(1.0f - (localY / height) * 2.0f, -1.0f, 1.0f);
                    ch.pitchOffsets[static_cast<std::size_t>(stepSlot)] = static_cast<int8_t>(norm * 24.0f);
                }
                else if (m_graphMode == GraphMode::pan)
                {
                    const float norm = std::clamp(1.0f - (localY / height) * 2.0f, -1.0f, 1.0f);
                    ch.panOffsets[static_cast<std::size_t>(stepSlot)] = static_cast<int8_t>(norm * 50.0f);
                }
                repaint();
                return;
            }
        }
    }

    if (channelIdx >= 0 && static_cast<std::size_t>(channelIdx) < m_channels.size())
    {
        m_selectedChannel = channelIdx;

        // Check if clicked in left channel controls
        const int yOffset = m_headerHeight + channelIdx * m_rowHeight;
        const int localY = e.getPosition().getY() - yOffset;

        if (e.getPosition().getX() < m_leftControlsWidth && localY >= 0 && localY < m_rowHeight)
        {
            auto& ch = m_channels[static_cast<std::size_t>(channelIdx)];

            // Mute LED circle at x in [6, 22]
            if (e.getPosition().getX() >= 6 && e.getPosition().getX() <= 22)
            {
                ch.muted = ! ch.muted;
                repaint();
                return;
            }

            // Pan Knob at x in [24, 42]
            if (e.getPosition().getX() >= 24 && e.getPosition().getX() <= 42)
            {
                m_activeDragControl = DragControl::pan;
                m_dragChannelIndex = channelIdx;
                m_dragStartPos = e.getPosition();
                m_dragStartValue = ch.pan;
                return;
            }

            // Volume Knob at x in [44, 62]
            if (e.getPosition().getX() >= 44 && e.getPosition().getX() <= 62)
            {
                m_activeDragControl = DragControl::volume;
                m_dragChannelIndex = channelIdx;
                m_dragStartPos = e.getPosition();
                m_dragStartValue = ch.volume;
                return;
            }

            // Mixer Track Box at x in [64, 94]
            if (e.getPosition().getX() >= 64 && e.getPosition().getX() <= 94)
            {
                m_activeDragControl = DragControl::mixerTrack;
                m_dragChannelIndex = channelIdx;
                m_dragStartPos = e.getPosition();
                m_dragStartValue = static_cast<float>(ch.mixerTrack);
                return;
            }

            // Channel Name Button at x in [96, 236]
            if (e.getPosition().getX() >= 96 && e.getPosition().getX() <= 236)
            {
                if (e.mods.isRightButtonDown())
                {
                    showChannelContextMenu(channelIdx);
                }
                else
                {
                    if (m_auditionCallback)
                    {
                        m_auditionCallback(ch.rootPitch, 100, true);
                    }
                }
                repaint();
                return;
            }
        }

        // Clicked a step button
        if (stepIdx >= 0 && stepIdx < m_numSteps)
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
    if (m_activeDragControl == DragControl::graphEditor && m_graphEditorOpen)
    {
        auto bounds = getLocalBounds();
        bounds.removeFromTop(m_headerHeight);
        bounds.removeFromBottom(m_bottomBarHeight);
        auto graphBounds = bounds.removeFromBottom(m_graphEditorHeight);

        const int startX = m_leftControlsWidth + 8;
        const int stepSlot = (e.getPosition().getX() - startX) / (m_stepWidth + m_stepSpacing) + m_scrollOffsetSteps;
        if (stepSlot >= 0 && stepSlot < m_numSteps && m_selectedChannel >= 0 && static_cast<std::size_t>(m_selectedChannel) < m_channels.size())
        {
            auto& ch = m_channels[static_cast<std::size_t>(m_selectedChannel)];
            const float localY = static_cast<float>(e.getPosition().getY() - graphBounds.getY() - 4);
            const float height = static_cast<float>(graphBounds.getHeight() - 8);

            if (m_graphMode == GraphMode::velocity)
            {
                const float norm = std::clamp(1.0f - (localY / height), 0.05f, 1.0f);
                ch.velocities[static_cast<std::size_t>(stepSlot)] = static_cast<std::uint8_t>(norm * 127.0f);
            }
            else if (m_graphMode == GraphMode::pitch)
            {
                const float norm = std::clamp(1.0f - (localY / height) * 2.0f, -1.0f, 1.0f);
                ch.pitchOffsets[static_cast<std::size_t>(stepSlot)] = static_cast<int8_t>(norm * 24.0f);
            }
            else if (m_graphMode == GraphMode::pan)
            {
                const float norm = std::clamp(1.0f - (localY / height) * 2.0f, -1.0f, 1.0f);
                ch.panOffsets[static_cast<std::size_t>(stepSlot)] = static_cast<int8_t>(norm * 50.0f);
            }
            repaint();
            return;
        }
    }

    if (m_activeDragControl != DragControl::none && m_dragChannelIndex >= 0 &&
        static_cast<std::size_t>(m_dragChannelIndex) < m_channels.size())
    {
        auto& ch = m_channels[static_cast<std::size_t>(m_dragChannelIndex)];
        const float dy = static_cast<float>(m_dragStartPos.getY() - e.getPosition().getY());

        if (m_activeDragControl == DragControl::pan)
        {
            ch.pan = std::clamp(m_dragStartValue + dy * 0.02f, -1.0f, 1.0f);
            repaint();
            return;
        }
        if (m_activeDragControl == DragControl::volume)
        {
            ch.volume = std::clamp(m_dragStartValue + dy * 0.015f, 0.0f, 1.0f);
            repaint();
            return;
        }
        if (m_activeDragControl == DragControl::mixerTrack)
        {
            const int delta = static_cast<int>(std::round(dy * 0.1f));
            ch.mixerTrack = std::clamp(static_cast<int>(m_dragStartValue) + delta, 1, 64);
            repaint();
            return;
        }
    }

    int channelIdx = -1;
    const int stepIdx = getStepAt(e.getPosition(), channelIdx);

    if (channelIdx >= 0 && static_cast<std::size_t>(channelIdx) < m_channels.size() && stepIdx >= 0 && stepIdx < m_numSteps)
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
    m_activeDragControl = DragControl::none;
    m_dragChannelIndex = -1;

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

void ChannelRackView::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    int channelIdx = -1;
    const int yOffset = e.getPosition().getY() - m_headerHeight;
    if (yOffset >= 0)
    {
        channelIdx = yOffset / m_rowHeight;
    }

    if (channelIdx >= 0 && static_cast<std::size_t>(channelIdx) < m_channels.size())
    {
        // If hovering over mixer track box
        if (e.getPosition().getX() >= 64 && e.getPosition().getX() <= 94)
        {
            auto& ch = m_channels[static_cast<std::size_t>(channelIdx)];
            const int delta = (wheel.deltaY > 0.0f) ? 1 : -1;
            ch.mixerTrack = std::clamp(ch.mixerTrack + delta, 1, 64);
            repaint();
            return;
        }
    }
}

int ChannelRackView::getStepAt(juce::Point<int> pos, int& outChannelIndex) const noexcept
{
    outChannelIndex = -1;
    if (pos.getY() < m_headerHeight || pos.getY() >= getHeight() - m_bottomBarHeight)
        return -1;

    const int channel = (pos.getY() - m_headerHeight) / m_rowHeight;
    if (channel < 0 || static_cast<std::size_t>(channel) >= m_channels.size())
        return -1;

    outChannelIndex = channel;

    const int startX = m_leftControlsWidth + 8;
    if (pos.getX() < startX)
        return -1;

    const int stepSlot = (pos.getX() - startX) / (m_stepWidth + m_stepSpacing) + m_scrollOffsetSteps;
    if (stepSlot >= 0 && stepSlot < m_numSteps)
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

    auto bottomBounds = bounds.removeFromBottom(m_bottomBarHeight);
    drawBottomBar(g, bottomBounds);

    if (m_graphEditorOpen)
    {
        auto graphBounds = bounds.removeFromBottom(m_graphEditorHeight);
        drawGraphEditor(g, graphBounds);
    }

    for (std::size_t i = 0; i < m_channels.size(); ++i)
    {
        auto rowBounds = bounds.removeFromTop(m_rowHeight);
        drawChannelRow(g, static_cast<int>(i), rowBounds);
    }
}

void ChannelRackView::drawGraphEditor(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    g.setColour(juce::Colour::fromRGB(28, 30, 36));
    g.fillRect(bounds);
    g.setColour(juce::Colour::fromRGB(55, 60, 70));
    g.drawHorizontalLine(bounds.getY(), 0.0f, static_cast<float>(bounds.getWidth()));

    if (m_selectedChannel < 0 || static_cast<std::size_t>(m_selectedChannel) >= m_channels.size())
        return;

    const auto& ch = m_channels[static_cast<std::size_t>(m_selectedChannel)];

    // Left info
    auto leftArea = bounds.removeFromLeft(m_leftControlsWidth);
    g.setColour(juce::Colour::fromRGB(180, 185, 200));
    g.setFont(juce::FontOptions(11.0f).withStyle("Bold"));
    juce::String modeName = "VELOCITY";
    if (m_graphMode == GraphMode::pitch) modeName = "PITCH TUNE";
    else if (m_graphMode == GraphMode::pan) modeName = "PAN";
    g.drawText(juce::String(ch.name) + " (" + modeName + ")", leftArea.reduced(8, 4), juce::Justification::left, true);

    const int startX = m_leftControlsWidth + 8;
    for (int s = 0; s < m_numSteps; ++s)
    {
        const int actualStep = s + m_scrollOffsetSteps;
        if (actualStep < 0 || actualStep >= m_numSteps)
            continue;

        const int x = startX + s * (m_stepWidth + m_stepSpacing);
        auto colBounds = juce::Rectangle<int>(x, bounds.getY() + 4, m_stepWidth, bounds.getHeight() - 8);

        // Background slot
        g.setColour(juce::Colour::fromRGB(36, 39, 46));
        g.fillRoundedRectangle(colBounds.toFloat(), 2.0f);

        const bool isStepActive = ch.steps[static_cast<std::size_t>(actualStep)];

        if (m_graphMode == GraphMode::velocity)
        {
            const float valNorm = static_cast<float>(ch.velocities[static_cast<std::size_t>(actualStep)]) / 127.0f;
            const int barH = static_cast<int>(static_cast<float>(colBounds.getHeight()) * valNorm);
            auto barRect = colBounds.removeFromBottom(barH);
            g.setColour(isStepActive ? juce::Colour::fromRGB(0, 220, 240) : juce::Colour::fromRGB(70, 90, 105));
            g.fillRoundedRectangle(barRect.toFloat(), 2.0f);
        }
        else if (m_graphMode == GraphMode::pitch)
        {
            const int pitchOff = ch.pitchOffsets[static_cast<std::size_t>(actualStep)];
            const int midY = colBounds.getCentreY();
            g.setColour(juce::Colour::fromRGB(60, 65, 75));
            g.drawHorizontalLine(midY, static_cast<float>(colBounds.getX()), static_cast<float>(colBounds.getRight()));

            const float norm = static_cast<float>(pitchOff) / 24.0f;
            const int barH = static_cast<int>(std::abs(norm) * static_cast<float>(colBounds.getHeight() / 2));
            juce::Rectangle<int> barRect;
            if (norm >= 0.0f)
                barRect = juce::Rectangle<int>(colBounds.getX(), midY - barH, colBounds.getWidth(), barH);
            else
                barRect = juce::Rectangle<int>(colBounds.getX(), midY, colBounds.getWidth(), barH);

            g.setColour(isStepActive ? juce::Colour::fromRGB(245, 170, 45) : juce::Colour::fromRGB(100, 85, 60));
            g.fillRoundedRectangle(barRect.toFloat(), 2.0f);
        }
        else if (m_graphMode == GraphMode::pan)
        {
            const int panOff = ch.panOffsets[static_cast<std::size_t>(actualStep)];
            const int midY = colBounds.getCentreY();
            g.setColour(juce::Colour::fromRGB(60, 65, 75));
            g.drawHorizontalLine(midY, static_cast<float>(colBounds.getX()), static_cast<float>(colBounds.getRight()));

            const float norm = static_cast<float>(panOff) / 50.0f;
            const int barH = static_cast<int>(std::abs(norm) * static_cast<float>(colBounds.getHeight() / 2));
            juce::Rectangle<int> barRect;
            if (norm >= 0.0f)
                barRect = juce::Rectangle<int>(colBounds.getX(), midY - barH, colBounds.getWidth(), barH);
            else
                barRect = juce::Rectangle<int>(colBounds.getX(), midY, colBounds.getWidth(), barH);

            g.setColour(isStepActive ? juce::Colour::fromRGB(160, 110, 240) : juce::Colour::fromRGB(80, 65, 110));
            g.fillRoundedRectangle(barRect.toFloat(), 2.0f);
        }
    }
}

void ChannelRackView::drawHeader(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    g.setColour(juce::Colour::fromRGB(34, 37, 42));
    g.fillRect(bounds);

    g.setColour(juce::Colour::fromRGB(55, 59, 68));
    g.drawHorizontalLine(bounds.getBottom() - 1, 0.0f, static_cast<float>(bounds.getWidth()));

    // Title & speaker icon
    g.setColour(juce::Colour::fromRGB(215, 220, 230));
    g.setFont(juce::FontOptions(13.0f).withStyle("Bold"));

    // Draw speaker icon
    const auto iconBounds = juce::Rectangle<float>(12.0f, static_cast<float>(bounds.getY() + 9), 16.0f, 16.0f);
    juce::Path spk;
    spk.startNewSubPath(iconBounds.getX() + 2.0f, iconBounds.getY() + 5.0f);
    spk.lineTo(iconBounds.getX() + 5.0f, iconBounds.getY() + 5.0f);
    spk.lineTo(iconBounds.getX() + 9.0f, iconBounds.getY() + 2.0f);
    spk.lineTo(iconBounds.getX() + 9.0f, iconBounds.getY() + 14.0f);
    spk.lineTo(iconBounds.getX() + 5.0f, iconBounds.getY() + 11.0f);
    spk.lineTo(iconBounds.getX() + 2.0f, iconBounds.getY() + 11.0f);
    spk.closeSubPath();
    g.setColour(juce::Colour::fromRGB(190, 195, 205));
    g.fillPath(spk);

    g.drawText("Channel rack", bounds.removeFromLeft(220).withTrimmedLeft(34), juce::Justification::centredLeft);
}

void ChannelRackView::drawBottomBar(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    g.setColour(juce::Colour::fromRGB(32, 35, 40));
    g.fillRect(bounds);

    g.setColour(juce::Colour::fromRGB(50, 54, 62));
    g.drawHorizontalLine(bounds.getY(), 0.0f, static_cast<float>(bounds.getWidth()));
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
    const auto ledRect = juce::Rectangle<float>(8.0f, static_cast<float>(bounds.getY() + 7), 13.0f, 13.0f);
    if (! ch.muted)
    {
        g.setColour(juce::Colour::fromRGB(65, 215, 75));
        g.fillEllipse(ledRect);
        g.setColour(juce::Colour::fromRGB(200, 255, 205));
        g.fillEllipse(ledRect.reduced(3.5f));
    }
    else
    {
        g.setColour(juce::Colour::fromRGB(25, 55, 28));
        g.fillEllipse(ledRect);
    }
    g.setColour(juce::Colour::fromRGB(18, 20, 24));
    g.drawEllipse(ledRect, 1.2f);

    // 2. Mini Pan Knob
    const auto panRect = juce::Rectangle<float>(26.0f, static_cast<float>(bounds.getY() + 6), 14.0f, 14.0f);
    g.setColour(juce::Colour::fromRGB(24, 26, 30));
    g.fillEllipse(panRect);
    g.setColour(juce::Colour::fromRGB(100, 105, 115));
    g.drawEllipse(panRect, 1.0f);
    g.setColour(juce::Colour::fromRGB(210, 215, 225));
    const float panAngle = ch.pan * 2.2f;
    g.drawLine(panRect.getCentreX(), panRect.getCentreY(),
               panRect.getCentreX() + std::sin(panAngle) * 5.0f,
               panRect.getCentreY() - std::cos(panAngle) * 5.0f, 1.5f);

    // 3. Mini Vol Knob
    const auto volRect = juce::Rectangle<float>(46.0f, static_cast<float>(bounds.getY() + 6), 14.0f, 14.0f);
    g.setColour(juce::Colour::fromRGB(24, 26, 30));
    g.fillEllipse(volRect);
    g.setColour(juce::Colour::fromRGB(100, 105, 115));
    g.drawEllipse(volRect, 1.0f);
    g.setColour(juce::Colour::fromRGB(210, 215, 225));
    const float volAngle = (ch.volume - 0.5f) * 3.2f;
    g.drawLine(volRect.getCentreX(), volRect.getCentreY(),
               volRect.getCentreX() + std::sin(volAngle) * 5.0f,
               volRect.getCentreY() - std::cos(volAngle) * 5.0f, 1.5f);

    // 4. Mixer Track Box
    const auto mixerBox = juce::Rectangle<float>(66.0f, static_cast<float>(bounds.getY() + 4), 26.0f, 20.0f);
    g.setColour(juce::Colour::fromRGB(28, 30, 35));
    g.fillRoundedRectangle(mixerBox, 3.0f);
    g.setColour(juce::Colour::fromRGB(65, 70, 80));
    g.drawRoundedRectangle(mixerBox, 3.0f, 1.0f);
    g.setColour(juce::Colour::fromRGB(190, 195, 205));
    g.setFont(juce::FontOptions(10.0f).withStyle("Bold"));
    g.drawText(ch.mixerTrack > 0 ? std::to_string(ch.mixerTrack) : "--", mixerBox, juce::Justification::centred);

    // 5. Channel Name Button
    const auto nameBox = juce::Rectangle<float>(96.0f, static_cast<float>(bounds.getY() + 3), 136.0f, 22.0f);
    juce::ColourGradient bgGrad(
        juce::Colour::fromRGB(52, 56, 64), nameBox.getX(), nameBox.getY(),
        juce::Colour::fromRGB(36, 39, 45), nameBox.getX(), nameBox.getBottom(), false);
    g.setGradientFill(bgGrad);
    g.fillRoundedRectangle(nameBox, 3.0f);
    g.setColour(isSelected ? juce::Colour::fromRGB(95, 150, 210) : juce::Colour::fromRGB(60, 64, 74));
    g.drawRoundedRectangle(nameBox, 3.0f, 1.0f);
    g.setColour(juce::Colour::fromRGB(230, 235, 245));
    g.setFont(juce::FontOptions(11.0f).withStyle("Bold"));
    g.drawText(ch.name, nameBox.reduced(6, 0), juce::Justification::centredLeft);

    // 6. Selected Channel Green Indicator Bar (matching the screenshot on JJ - Shaker 4)
    const auto selectPill = juce::Rectangle<float>(236.0f, static_cast<float>(bounds.getY() + 4), 4.0f, 20.0f);
    if (isSelected)
    {
        g.setColour(juce::Colour::fromRGB(95, 240, 105));
        g.fillRoundedRectangle(selectPill, 2.0f);
        g.setColour(juce::Colour::fromRGB(220, 255, 225));
        g.fillRoundedRectangle(selectPill.reduced(1.0f, 2.0f), 1.0f);
    }
    else
    {
        g.setColour(juce::Colour::fromRGB(45, 48, 54));
        g.drawRoundedRectangle(selectPill, 2.0f, 1.0f);
    }

    // 7. Step Sequencer Grid (Render visible steps)
    const int startX = m_leftControlsWidth + 8;
    const int maxVisible = std::min(m_numSteps, (getWidth() - startX) / (m_stepWidth + m_stepSpacing));

    for (int s = 0; s < maxVisible; ++s)
    {
        const int actualStep = s + m_scrollOffsetSteps;
        if (actualStep >= m_numSteps)
            break;

        const int stepX = startX + s * (m_stepWidth + m_stepSpacing);
        const auto stepBounds = juce::Rectangle<int>(stepX, bounds.getY() + 4, m_stepWidth, 20);
        const bool active = ch.steps[static_cast<std::size_t>(actualStep)];
        const bool isPlayhead = (m_lastPlayedStep == actualStep);
        drawStepButton(g, channelIndex, actualStep, stepBounds, active, isPlayhead);
    }
}

void ChannelRackView::drawStepButton(juce::Graphics& g, int, int stepIndex, juce::Rectangle<int> bounds, bool isActive, bool isPlayhead)
{
    // Signature FL Studio 4-step alternating blocks:
    // Steps 0..3: Light Gray
    // Steps 4..7: Warm Maroon / Dark Coral
    // Steps 8..11: Light Gray
    // Steps 12..15: Warm Maroon / Dark Coral
    // Steps 16..19: Light Gray
    // Steps 20..23: Warm Maroon / Dark Coral
    // Steps 24..27: Light Gray
    // Steps 28..31: Warm Maroon / Dark Coral
    const bool isMaroonBlock = ((stepIndex / 4) % 2) == 1;
    const auto fBounds = bounds.toFloat();

    if (isActive)
    {
        // Lit step button with illuminated center pill
        if (isMaroonBlock)
        {
            // Pinkish / coral illuminated button
            juce::ColourGradient grad(
                juce::Colour::fromRGB(245, 125, 125), fBounds.getX(), fBounds.getY(),
                juce::Colour::fromRGB(190, 85, 85), fBounds.getX(), fBounds.getBottom(), false);
            g.setGradientFill(grad);
            g.fillRoundedRectangle(fBounds, 3.0f);

            // Glowing center notch
            g.setColour(juce::Colour::fromRGB(255, 235, 235));
            g.fillRoundedRectangle(fBounds.reduced(2.5f, 4.0f), 2.0f);
        }
        else
        {
            // Bright silver-white illuminated button
            juce::ColourGradient grad(
                juce::Colour::fromRGB(245, 248, 252), fBounds.getX(), fBounds.getY(),
                juce::Colour::fromRGB(210, 215, 225), fBounds.getX(), fBounds.getBottom(), false);
            g.setGradientFill(grad);
            g.fillRoundedRectangle(fBounds, 3.0f);

            // Glowing center notch
            g.setColour(juce::Colour::fromRGB(255, 255, 255));
            g.fillRoundedRectangle(fBounds.reduced(2.5f, 4.0f), 2.0f);
        }

        g.setColour(juce::Colour::fromRGB(20, 22, 26));
        g.drawRoundedRectangle(fBounds, 3.0f, 1.0f);
    }
    else
    {
        // Inactive unlit button matching FL Studio screenshot
        if (isMaroonBlock)
        {
            // Dark reddish-brown metallic
            juce::ColourGradient grad(
                juce::Colour::fromRGB(74, 50, 52), fBounds.getX(), fBounds.getY(),
                juce::Colour::fromRGB(52, 34, 36), fBounds.getX(), fBounds.getBottom(), false);
            g.setGradientFill(grad);
            g.fillRoundedRectangle(fBounds, 3.0f);

            // Subtle center indented line
            g.setColour(juce::Colour::fromRGB(40, 25, 27));
            g.drawHorizontalLine(bounds.getCentreY(), fBounds.getX() + 2.0f, fBounds.getRight() - 2.0f);
        }
        else
        {
            // Slate / cool gray metallic
            juce::ColourGradient grad(
                juce::Colour::fromRGB(65, 70, 78), fBounds.getX(), fBounds.getY(),
                juce::Colour::fromRGB(45, 48, 55), fBounds.getX(), fBounds.getBottom(), false);
            g.setGradientFill(grad);
            g.fillRoundedRectangle(fBounds, 3.0f);

            // Subtle center indented line
            g.setColour(juce::Colour::fromRGB(32, 35, 40));
            g.drawHorizontalLine(bounds.getCentreY(), fBounds.getX() + 2.0f, fBounds.getRight() - 2.0f);
        }

        g.setColour(juce::Colour::fromRGB(26, 28, 33));
        g.drawRoundedRectangle(fBounds, 3.0f, 1.0f);
    }

    // Playhead sweep glow
    if (isPlayhead)
    {
        g.setColour(juce::Colour::fromRGBA(100, 200, 255, 140));
        g.drawRoundedRectangle(fBounds.expanded(1.0f), 3.0f, 2.0f);
    }
}

void ChannelRackView::resized()
{
    auto bounds = getLocalBounds();
    auto headerBounds = bounds.removeFromTop(m_headerHeight);

    // Left controls: filter preset + Pattern selector + Add pattern
    m_filterCombo.setBounds(headerBounds.removeFromLeft(75).reduced(2, 5));
    m_patternCombo.setBounds(headerBounds.removeFromLeft(110).reduced(2, 5));
    m_addPatternButton.setBounds(headerBounds.removeFromLeft(46).reduced(2, 5));

    // Right-aligned controls
    m_clearButton.setBounds(headerBounds.removeFromRight(60).reduced(2, 5));
    m_commitButton.setBounds(headerBounds.removeFromRight(130).reduced(2, 5));
    m_graphToggleBtn.setBounds(headerBounds.removeFromRight(55).reduced(2, 5));
    m_graphModeCombo.setBounds(headerBounds.removeFromRight(78).reduced(2, 5));
    m_stepCountCombo.setBounds(headerBounds.removeFromRight(85).reduced(2, 5));

    // Swing control (knob + label)
    auto swingArea = headerBounds.removeFromRight(80);
    m_swingSlider.setBounds(swingArea.removeFromRight(28).reduced(2, 4));
    m_swingLabel.setBounds(swingArea);

    // Bottom bar `+` button
    auto bottomBounds = getLocalBounds().removeFromBottom(m_bottomBarHeight);
    m_addChannelButton.setBounds(bottomBounds.removeFromLeft(28).reduced(2, 2));
}

} // namespace dawg::ui
