#include "PitchBlobCompingView.h"

#include <cmath>

namespace dawg::ui {

PitchBlobCompingView::PitchBlobCompingView(CommandHandler handler)
    : m_commandHandler(std::move(handler))
{
    // Seed initial mock takes for interactive comping workflow
    project::AudioClip c1 {}; c1.id = 1; c1.startSample = 0; c1.lengthSamples = 96000;
    project::AudioClip c2 {}; c2.id = 2; c2.startSample = 0; c2.lengthSamples = 96000;
    project::AudioClip c3 {}; c3.id = 3; c3.startSample = 0; c3.lengthSamples = 96000;
    m_compManager.addTake(c1, "Take 1", "#335577");
    m_compManager.addTake(c2, "Take 2", "#4477aa");
    m_compManager.addTake(c3, "Take 3", "#3388bb");
    m_compManager.selectCompSlice(1, 0, 24000);
    m_compManager.selectCompSlice(2, 24000, 60000);
    m_compManager.selectCompSlice(3, 60000, 96000);

    m_scaleCombo.addItem("Scale: Major", 1);
    m_scaleCombo.addItem("Scale: Natural Minor", 2);
    m_scaleCombo.addItem("Scale: Dorian", 3);
    m_scaleCombo.addItem("Scale: Pentatonic Minor", 4);
    m_scaleCombo.addItem("Scale: Blues", 5);
    m_scaleCombo.addItem("Scale: Chromatic", 6);
    m_scaleCombo.setSelectedId(1, juce::dontSendNotification);
    m_scaleCombo.onChange = [this] {
        const int id = m_scaleCombo.getSelectedId();
        if (id == 1) m_scaleClasses = {0, 2, 4, 5, 7, 9, 11};
        else if (id == 2) m_scaleClasses = {0, 2, 3, 5, 7, 8, 10};
        else if (id == 3) m_scaleClasses = {0, 2, 3, 5, 7, 9, 10};
        else if (id == 4) m_scaleClasses = {0, 3, 5, 7, 10};
        else if (id == 5) m_scaleClasses = {0, 3, 5, 6, 7, 10};
        else m_scaleClasses = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};

        if (m_snapToScale)
        {
            m_pitchEditor.quantizeToScale(m_scaleClasses, 1.0f);
            repaint();
        }
    };
    addAndMakeVisible(m_scaleCombo);

    m_snapButton.onClick = [this] {
        m_snapToScale = !m_snapToScale;
        m_snapButton.setButtonText(m_snapToScale ? "Snap Scale [ON]" : "Snap Scale [OFF]");
        if (m_snapToScale)
        {
            m_pitchEditor.quantizeToScale(m_scaleClasses, 1.0f);
            repaint();
        }
    };
    addAndMakeVisible(m_snapButton);

    m_toggleLanesButton.onClick = [this] {
        m_showTakeLanes = !m_showTakeLanes;
        m_toggleLanesButton.setButtonText(m_showTakeLanes ? "Take Lanes [Hide]" : "Take Lanes [Show]");
        resized();
        repaint();
    };
    addAndMakeVisible(m_toggleLanesButton);

    m_flattenCompButton.onClick = [this] {
        if (! m_model.project.tracks.empty() && m_commandHandler)
        {
            const auto targetTrackId = m_model.project.tracks.front().id;
            const auto newClip = m_compManager.flattenAndMergeComp(0, 1, "Vocal Master Comp");
            if (newClip.lengthSamples > 0)
            {
                auto c = newClip;
                c.trackId = targetTrackId;
                if (! m_model.project.assets.empty())
                    c.assetId = m_model.project.assets.front().id;
                m_commandHandler(project::AddAudioClip { c });
            }
        }
        repaint();
    };
    addAndMakeVisible(m_flattenCompButton);

    m_formantSlider.setRange(-12.0, 12.0, 0.1);
    m_formantSlider.setValue(0.0);
    m_formantSlider.onValueChange = [this] {
        if (m_selectedBlobIdx >= 0 && static_cast<std::size_t>(m_selectedBlobIdx) < m_pitchEditor.blobs().size())
        {
            const auto blobId = m_pitchEditor.blobs()[static_cast<std::size_t>(m_selectedBlobIdx)].blobId;
            m_pitchEditor.setBlobFormant(blobId, static_cast<float>(m_formantSlider.getValue()));
            m_formantLabel.setText("Formant: " + juce::String(m_formantSlider.getValue(), 1) + " st",
                                   juce::dontSendNotification);
            repaint();
        }
    };
    addAndMakeVisible(m_formantSlider);
    m_formantLabel.setFont(juce::FontOptions(11.0f));
    m_formantLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(180, 190, 205));
    addAndMakeVisible(m_formantLabel);

    // Prepopulate realistic mock vocal pitch trajectory and segmented blobs
    m_mockPitchTrajectory.resize(200);
    for (std::size_t i = 0; i < m_mockPitchTrajectory.size(); ++i)
    {
        const double t = static_cast<double>(i) / 200.0;
        float midiPitch = 57.0f; // A3

        if (t < 0.25) midiPitch = 57.0f + 0.2f * std::sin(t * 30.0);
        else if (t < 0.5) midiPitch = 60.0f + 0.3f * std::sin(t * 25.0); // C4
        else if (t < 0.75) midiPitch = 62.0f + 0.2f * std::sin(t * 28.0); // D4
        else midiPitch = 64.0f + 0.35f * std::sin(t * 32.0); // E4

        m_mockPitchTrajectory[i] = { static_cast<std::uint64_t>(i * 240), midiPitch };
    }

    // Seed mock segmented pitch blobs
    auto& blobs = m_pitchEditor.blobsRef();
    const float mockPitches[] = { 57.0f, 60.0f, 62.0f, 64.0f, 65.0f, 67.0f };
    const char* words[] = { "I (A3)", "wan- (C4)", "-na (D4)", "know (E4)", "your (F4)", "love (G4)" };

    for (std::size_t i = 0; i < 6; ++i)
    {
        project::PitchBlob b {};
        b.blobId = static_cast<std::uint32_t>(i + 1);
        b.startSample = static_cast<std::uint64_t>(i * 7500);
        b.endSample = static_cast<std::uint64_t>((i + 1) * 7500 - 500);
        b.detectedMidiPitch = mockPitches[i] + 0.15f;
        b.targetMidiPitch = mockPitches[i];
        b.fineTuneCents = 15.0f;
        b.formantShiftSemitones = 0.0f;
        b.vibratoDepthScale = 1.0f;
        b.noteName = words[i];
        b.isVoiced = true;
        blobs.push_back(b);
    }

    startTimerHz(20);
}

PitchBlobCompingView::~PitchBlobCompingView()
{
    stopTimer();
}

void PitchBlobCompingView::timerCallback()
{
}

void PitchBlobCompingView::setViewModel(const EditorViewModel& model)
{
    m_model = model;
    repaint();
}

void PitchBlobCompingView::loadVocalStem(const float* samples, const std::size_t sampleCount, const double sampleRate)
{
    if (samples != nullptr && sampleCount > 0)
    {
        m_pitchEditor.analyzeAudio(samples, sampleCount, sampleRate);
        if (m_snapToScale)
        {
            m_pitchEditor.quantizeToScale(m_scaleClasses, 1.0f);
        }
        repaint();
    }
}

void PitchBlobCompingView::resized()
{
    auto bounds = getLocalBounds();
    auto topBar = bounds.removeFromTop(m_topBarHeight);

    m_toggleLanesButton.setBounds(topBar.removeFromLeft(110).reduced(3, 4));
    m_scaleCombo.setBounds(topBar.removeFromLeft(150).reduced(3, 4));
    m_snapButton.setBounds(topBar.removeFromLeft(110).reduced(3, 4));
    m_flattenCompButton.setBounds(topBar.removeFromLeft(100).reduced(3, 4));
    m_formantLabel.setBounds(topBar.removeFromLeft(120).reduced(2, 4));
    m_formantSlider.setBounds(topBar.removeFromLeft(140).reduced(2, 4));
}

float PitchBlobCompingView::midiNoteToY(float midiPitch, juce::Rectangle<int> canvasBounds) const noexcept
{
    constexpr float minPitch = 48.0f; // C3
    constexpr float maxPitch = 72.0f; // C5
    const float normalized = (midiPitch - minPitch) / (maxPitch - minPitch);
    return static_cast<float>(canvasBounds.getBottom()) - normalized * static_cast<float>(canvasBounds.getHeight());
}

float PitchBlobCompingView::yToMidiNote(float y, juce::Rectangle<int> canvasBounds) const noexcept
{
    constexpr float minPitch = 48.0f;
    constexpr float maxPitch = 72.0f;
    const float normalized = (static_cast<float>(canvasBounds.getBottom()) - y) / static_cast<float>(canvasBounds.getHeight());
    return minPitch + normalized * (maxPitch - minPitch);
}

int PitchBlobCompingView::findBlobAt(juce::Point<int> pos, juce::Rectangle<int> canvasBounds) const noexcept
{
    const auto& blobs = m_pitchEditor.blobs();
    if (blobs.empty() || m_mockPitchTrajectory.empty())
        return -1;

    const std::uint64_t totalSamples = m_mockPitchTrajectory.back().first;
    if (totalSamples == 0) return -1;

    for (std::size_t i = 0; i < blobs.size(); ++i)
    {
        const auto& b = blobs[i];
        const float xStart = static_cast<float>(canvasBounds.getX()) +
            (static_cast<float>(b.startSample) / static_cast<float>(totalSamples)) * static_cast<float>(canvasBounds.getWidth());
        const float xEnd = static_cast<float>(canvasBounds.getX()) +
            (static_cast<float>(b.endSample) / static_cast<float>(totalSamples)) * static_cast<float>(canvasBounds.getWidth());

        const float y = midiNoteToY(b.targetMidiPitch, canvasBounds);
        const auto blobRect = juce::Rectangle<float>(xStart, y - 12.0f, std::max(20.0f, xEnd - xStart), 24.0f);

        if (blobRect.contains(pos.toFloat()))
            return static_cast<int>(i);
    }
    return -1;
}

void PitchBlobCompingView::mouseDown(const juce::MouseEvent& e)
{
    auto canvasBounds = getLocalBounds().withTrimmedTop(m_topBarHeight);
    if (m_showTakeLanes)
        canvasBounds = canvasBounds.withTrimmedTop(m_takeLanesHeight);
    canvasBounds = canvasBounds.withTrimmedLeft(m_pianoRulerWidth);

    const int blobIdx = findBlobAt(e.getPosition(), canvasBounds);
    if (blobIdx >= 0)
    {
        m_selectedBlobIdx = blobIdx;
        m_draggedBlobIdx = blobIdx;
        m_dragStartPos = e.getPosition();
        m_dragStartPitch = m_pitchEditor.blobs()[static_cast<std::size_t>(blobIdx)].targetMidiPitch;
        m_formantSlider.setValue(m_pitchEditor.blobs()[static_cast<std::size_t>(blobIdx)].formantShiftSemitones,
                                 juce::dontSendNotification);
        repaint();
    }
    else
    {
        m_selectedBlobIdx = -1;
        repaint();
    }
}

void PitchBlobCompingView::mouseDrag(const juce::MouseEvent& e)
{
    if (m_draggedBlobIdx < 0 || static_cast<std::size_t>(m_draggedBlobIdx) >= m_pitchEditor.blobs().size())
        return;

    auto canvasBounds = getLocalBounds().withTrimmedTop(m_topBarHeight);
    if (m_showTakeLanes)
        canvasBounds = canvasBounds.withTrimmedTop(m_takeLanesHeight);
    canvasBounds = canvasBounds.withTrimmedLeft(m_pianoRulerWidth);

    const float curPitch = yToMidiNote(static_cast<float>(e.getPosition().getY()), canvasBounds);
    const float pitchDelta = curPitch - yToMidiNote(static_cast<float>(m_dragStartPos.getY()), canvasBounds);
    const float newTargetPitch = std::clamp<float>(std::round(m_dragStartPitch + pitchDelta), 24.0f, 108.0f);

    auto& blob = m_pitchEditor.blobsRef()[static_cast<std::size_t>(m_draggedBlobIdx)];
    if (std::abs(blob.targetMidiPitch - newTargetPitch) > 0.01f)
    {
        blob.targetMidiPitch = newTargetPitch;
        blob.noteName = project::PitchBlobEditor::midiPitchToNoteName(newTargetPitch);
        if (m_noteCallback)
        {
            m_noteCallback(static_cast<std::size_t>(m_draggedBlobIdx),
                           static_cast<int>(newTargetPitch - m_dragStartPitch),
                           blob.fineTuneCents);
        }
        repaint();
    }
}

void PitchBlobCompingView::mouseUp(const juce::MouseEvent&)
{
    m_draggedBlobIdx = -1;
}

void PitchBlobCompingView::mouseDoubleClick(const juce::MouseEvent& e)
{
    auto canvasBounds = getLocalBounds().withTrimmedTop(m_topBarHeight);
    if (m_showTakeLanes)
        canvasBounds = canvasBounds.withTrimmedTop(m_takeLanesHeight);
    canvasBounds = canvasBounds.withTrimmedLeft(m_pianoRulerWidth);

    const int blobIdx = findBlobAt(e.getPosition(), canvasBounds);
    if (blobIdx >= 0)
    {
        m_pitchEditor.quantizeToScale(m_scaleClasses, 1.0f);
        repaint();
    }
}

void PitchBlobCompingView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(24, 26, 31));

    auto bounds = getLocalBounds();
    auto topBar = bounds.removeFromTop(m_topBarHeight);

    // Top control bar background
    g.setColour(juce::Colour::fromRGB(32, 35, 43));
    g.fillRect(topBar);
    g.setColour(juce::Colour::fromRGB(45, 50, 60));
    g.drawHorizontalLine(topBar.getBottom() - 1, 0.0f, static_cast<float>(getWidth()));

    // Take Lanes Area (Comping swipe lanes)
    if (m_showTakeLanes)
    {
        auto lanesBounds = bounds.removeFromTop(m_takeLanesHeight);
        drawTakeLanes(g, lanesBounds);
    }

    // Pitch Canvas Area
    auto rulerBounds = bounds.removeFromLeft(m_pianoRulerWidth);
    drawPianoRuler(g, rulerBounds);
    drawPitchBlobCanvas(g, bounds);
}

void PitchBlobCompingView::drawPianoRuler(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    g.setColour(juce::Colour::fromRGB(28, 30, 36));
    g.fillRect(bounds);
    g.setColour(juce::Colour::fromRGB(42, 46, 56));
    g.drawVerticalLine(bounds.getRight() - 1, static_cast<float>(bounds.getY()), static_cast<float>(bounds.getBottom()));

    constexpr int minPitch = 48;
    constexpr int maxPitch = 72;

    for (int note = minPitch; note <= maxPitch; ++note)
    {
        const float y = midiNoteToY(static_cast<float>(note), bounds);
        const bool isBlackKey = ((1 << (note % 12)) & 0x054A) != 0;

        if (isBlackKey)
        {
            g.setColour(juce::Colour::fromRGB(20, 22, 26));
            g.fillRect(juce::Rectangle<float>(static_cast<float>(bounds.getX()), y - 6.0f,
                                              static_cast<float>(bounds.getWidth() - 1), 12.0f));
        }

        if ((note % 12) == 0) // C note
        {
            g.setColour(juce::Colour::fromRGB(120, 180, 255));
            g.setFont(juce::FontOptions(10.0f).withStyle("Bold"));
            g.drawText("C" + std::to_string((note / 12) - 1), bounds.withY(static_cast<int>(y - 8)).withHeight(16),
                       juce::Justification::centredRight);
        }
    }
}

void PitchBlobCompingView::drawTakeLanes(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    g.setColour(juce::Colour::fromRGB(20, 22, 26));
    g.fillRect(bounds);
    g.setColour(juce::Colour::fromRGB(45, 50, 60));
    g.drawHorizontalLine(bounds.getBottom() - 1, 0.0f, static_cast<float>(bounds.getWidth()));

    constexpr int numTakes = 3;
    const int laneH = (bounds.getHeight() - 4) / numTakes;

    for (int t = 0; t < numTakes; ++t)
    {
        const auto laneRect = juce::Rectangle<int>(bounds.getX() + 4, bounds.getY() + 2 + t * laneH,
                                                  bounds.getWidth() - 8, laneH - 2);

        // Lane background
        g.setColour(juce::Colour::fromRGB(28, 31, 38));
        g.fillRoundedRectangle(laneRect.toFloat(), 3.0f);

        // Take tag
        g.setColour(juce::Colour::fromRGB(140, 150, 170));
        g.setFont(juce::FontOptions(10.0f).withStyle("Bold"));
        g.drawText("Take " + std::to_string(t + 1), laneRect.reduced(6, 4), juce::Justification::topLeft);

        // Mock waveform line in take lane
        g.setColour(juce::Colour::fromRGB(55, 90, 140).withAlpha(0.6f));
        juce::Path wavePath;
        const float midY = laneRect.toFloat().getCentreY();
        wavePath.startNewSubPath(static_cast<float>(laneRect.getX() + 60), midY);
        for (int x = laneRect.getX() + 60; x < laneRect.getRight() - 10; x += 4)
        {
            const float val = std::sin(static_cast<float>(x) * 0.15f + static_cast<float>(t)) *
                              std::cos(static_cast<float>(x) * 0.04f) * (laneH * 0.35f);
            wavePath.lineTo(static_cast<float>(x), midY + val);
        }
        g.strokePath(wavePath, juce::PathStrokeType(1.2f));

        // Active swipe comp highlight slice
        if (t == 1) // Take 2 comped
        {
            const auto compSlice = juce::Rectangle<float>(static_cast<float>(laneRect.getX() + 140),
                                                          static_cast<float>(laneRect.getY() + 2),
                                                          180.0f, static_cast<float>(laneRect.getHeight() - 4));
            g.setColour(juce::Colour::fromRGB(45, 140, 85).withAlpha(0.35f));
            g.fillRoundedRectangle(compSlice, 3.0f);
            g.setColour(juce::Colour::fromRGB(120, 255, 160));
            g.drawRoundedRectangle(compSlice, 3.0f, 1.5f);

            // Zero-crossing snap micro-fade marker
            g.setColour(juce::Colour::fromRGB(255, 220, 80));
            g.drawVerticalLine(static_cast<int>(compSlice.getX()), compSlice.getY(), compSlice.getBottom());
            g.drawVerticalLine(static_cast<int>(compSlice.getRight()), compSlice.getY(), compSlice.getBottom());
        }
    }
}

void PitchBlobCompingView::drawPitchBlobCanvas(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    // Background horizontal semitone grid lines
    constexpr int minPitch = 48;
    constexpr int maxPitch = 72;

    for (int note = minPitch; note <= maxPitch; ++note)
    {
        const float y = midiNoteToY(static_cast<float>(note), bounds);
        const bool isRoot = ((note % 12) == (m_rootNote % 12));
        g.setColour(isRoot ? juce::Colour::fromRGB(55, 65, 85) : juce::Colour::fromRGB(32, 36, 45));
        g.drawHorizontalLine(static_cast<int>(y), static_cast<float>(bounds.getX()), static_cast<float>(bounds.getRight()));
    }

    if (m_mockPitchTrajectory.empty())
        return;

    const std::uint64_t totalSamples = m_mockPitchTrajectory.back().first;
    if (totalSamples == 0) return;

    // Draw detected continuous pitch contour curve
    juce::Path pitchCurve;
    bool pathStarted = false;
    for (const auto& pt : m_mockPitchTrajectory)
    {
        const float x = static_cast<float>(bounds.getX()) +
            (static_cast<float>(pt.first) / static_cast<float>(totalSamples)) * static_cast<float>(bounds.getWidth());
        const float y = midiNoteToY(pt.second, bounds);

        if (!pathStarted)
        {
            pitchCurve.startNewSubPath(x, y);
            pathStarted = true;
        }
        else
        {
            pitchCurve.lineTo(x, y);
        }
    }
    g.setColour(juce::Colour::fromRGB(255, 140, 60).withAlpha(0.75f));
    g.strokePath(pitchCurve, juce::PathStrokeType(2.0f));

    // Draw segmented syllable pitch blobs
    const auto& blobs = m_pitchEditor.blobs();
    for (std::size_t i = 0; i < blobs.size(); ++i)
    {
        const auto& b = blobs[i];
        const float xStart = static_cast<float>(bounds.getX()) +
            (static_cast<float>(b.startSample) / static_cast<float>(totalSamples)) * static_cast<float>(bounds.getWidth());
        const float xEnd = static_cast<float>(bounds.getX()) +
            (static_cast<float>(b.endSample) / static_cast<float>(totalSamples)) * static_cast<float>(bounds.getWidth());

        const float y = midiNoteToY(b.targetMidiPitch, bounds);
        const auto blobRect = juce::Rectangle<float>(xStart, y - 11.0f, std::max(22.0f, xEnd - xStart), 22.0f);

        const bool isSelected = (static_cast<int>(i) == m_selectedBlobIdx);

        // Blob fill and border
        g.setColour(isSelected ? juce::Colour::fromRGB(65, 160, 240) : juce::Colour::fromRGB(40, 110, 190));
        g.fillRoundedRectangle(blobRect, 6.0f);

        g.setColour(isSelected ? juce::Colour::fromRGB(180, 230, 255) : juce::Colour::fromRGB(80, 160, 240));
        g.drawRoundedRectangle(blobRect, 6.0f, isSelected ? 2.0f : 1.2f);

        // Syllable text & Note name
        g.setColour(juce::Colour::fromRGB(245, 250, 255));
        g.setFont(juce::FontOptions(10.0f).withStyle("Bold"));
        g.drawText(b.noteName, blobRect.reduced(4, 2), juce::Justification::centred);

        // Cents offset badge
        if (std::abs(b.fineTuneCents) > 1.0f)
        {
            const auto centsRect = juce::Rectangle<float>(blobRect.getX(), blobRect.getBottom() + 1.0f,
                                                          blobRect.getWidth(), 10.0f);
            g.setColour(juce::Colour::fromRGB(255, 200, 100));
            g.setFont(juce::FontOptions(8.0f));
            const std::string centsStr = (b.fineTuneCents > 0.0f ? "+" : "") +
                                         std::to_string(static_cast<int>(b.fineTuneCents)) + "c";
            g.drawText(centsStr, centsRect, juce::Justification::centred);
        }
    }
}

} // namespace dawg::ui
