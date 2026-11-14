#include "SessionMatrixView.h"

#include <cmath>

namespace dawg::ui {

SessionMatrixView::SessionMatrixView(CommandHandler handler, AuditionCallback auditionCb)
    : m_commandHandler(std::move(handler))
    , m_auditionCallback(std::move(auditionCb))
{
    m_quantizeCombo.addItem("Quantize: 1 Bar", 1);
    m_quantizeCombo.addItem("Quantize: 1/2 Bar", 2);
    m_quantizeCombo.addItem("Quantize: 1/4 Beat", 3);
    m_quantizeCombo.addItem("Quantize: None (Instant)", 4);
    m_quantizeCombo.setSelectedId(1, juce::dontSendNotification);
    addAndMakeVisible(m_quantizeCombo);

    m_stopAllButton.onClick = [this] { stopAllClips(); };
    m_stopAllButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(50, 24, 28));
    addAndMakeVisible(m_stopAllButton);

    startTimerHz(15);
}

SessionMatrixView::~SessionMatrixView()
{
    stopTimer();
}

void SessionMatrixView::setViewModel(const EditorViewModel& model)
{
    m_model = model;

    // Ensure slot grid tracks match project tracks
    const std::size_t numTracks = std::max<std::size_t>(1, m_model.project.tracks.size());
    if (m_trackSlots.size() != numTracks)
    {
        m_trackSlots.resize(numTracks);
        // Prepopulate standard clip slots for immediate prototyping
        for (std::size_t t = 0; t < numTracks; ++t)
        {
            m_trackSlots[t][0].hasClip = true;
            m_trackSlots[t][0].name = "Groove A";
            m_trackSlots[t][0].color = juce::Colour::fromRGB(45, 120, 185);

            m_trackSlots[t][1].hasClip = true;
            m_trackSlots[t][1].name = "Groove B";
            m_trackSlots[t][1].color = juce::Colour::fromRGB(140, 65, 175);

            m_trackSlots[t][2].hasClip = true;
            m_trackSlots[t][2].name = "Drop Hook";
            m_trackSlots[t][2].color = juce::Colour::fromRGB(195, 85, 45);

            m_trackSlots[t][3].hasClip = (t % 2 == 0);
            m_trackSlots[t][3].name = "Outro Variation";
            m_trackSlots[t][3].color = juce::Colour::fromRGB(50, 145, 95);
        }
    }

    repaint();
}

void SessionMatrixView::timerCallback()
{
    m_blinkState = ! m_blinkState;

    // If transport is running and clips are queued, launch them on bar boundaries
    bool needsRepaint = false;
    for (auto& trackSlots : m_trackSlots)
    {
        for (auto& slot : trackSlots)
        {
            if (slot.isQueued)
            {
                slot.isQueued = false;
                slot.isPlaying = true;
                needsRepaint = true;
            }
        }
    }

    if (needsRepaint || m_blinkState)
        repaint();
}

void SessionMatrixView::launchScene(int sceneIndex)
{
    if (sceneIndex < 0 || sceneIndex >= kNumScenes)
        return;

    for (std::size_t t = 0; t < m_trackSlots.size(); ++t)
    {
        // Stop any other playing clip on this track
        for (int s = 0; s < kNumScenes; ++s)
        {
            if (s != sceneIndex)
                m_trackSlots[t][static_cast<std::size_t>(s)].isPlaying = false;
        }

        if (m_trackSlots[t][static_cast<std::size_t>(sceneIndex)].hasClip)
        {
            m_trackSlots[t][static_cast<std::size_t>(sceneIndex)].isPlaying = true;
            m_trackSlots[t][static_cast<std::size_t>(sceneIndex)].isQueued = false;
        }
    }
    repaint();
}

void SessionMatrixView::launchClip(int trackIndex, int sceneIndex)
{
    if (trackIndex < 0 || static_cast<std::size_t>(trackIndex) >= m_trackSlots.size())
        return;
    if (sceneIndex < 0 || sceneIndex >= kNumScenes)
        return;

    auto& slot = m_trackSlots[static_cast<std::size_t>(trackIndex)][static_cast<std::size_t>(sceneIndex)];
    if (! slot.hasClip)
    {
        // Create new empty clip in this slot
        slot.hasClip = true;
        slot.name = "Clip " + std::to_string(sceneIndex + 1);
        slot.isPlaying = true;
        repaint();
        return;
    }

    // Toggle or start playing
    if (slot.isPlaying)
    {
        slot.isPlaying = false;
        slot.isQueued = false;
    }
    else
    {
        // Stop other clips on this track
        for (int s = 0; s < kNumScenes; ++s)
            m_trackSlots[static_cast<std::size_t>(trackIndex)][static_cast<std::size_t>(s)].isPlaying = false;

        slot.isPlaying = true;
    }
    repaint();
}

void SessionMatrixView::stopClip(int trackIndex, int sceneIndex)
{
    if (trackIndex < 0 || static_cast<std::size_t>(trackIndex) >= m_trackSlots.size())
        return;
    if (sceneIndex < 0 || sceneIndex >= kNumScenes)
        return;

    m_trackSlots[static_cast<std::size_t>(trackIndex)][static_cast<std::size_t>(sceneIndex)].isPlaying = false;
    m_trackSlots[static_cast<std::size_t>(trackIndex)][static_cast<std::size_t>(sceneIndex)].isQueued = false;
    repaint();
}

void SessionMatrixView::stopAllClips()
{
    for (auto& trackSlots : m_trackSlots)
    {
        for (auto& slot : trackSlots)
        {
            slot.isPlaying = false;
            slot.isQueued = false;
        }
    }
    repaint();
}

juce::Rectangle<int> SessionMatrixView::getSlotBounds(int trackIdx, int sceneIdx) const noexcept
{
    const int x = trackIdx * (m_slotWidth + 4) + 8;
    const int y = m_headerHeight + sceneIdx * (m_slotHeight + 4) + 6;
    return juce::Rectangle<int>(x, y, m_slotWidth, m_slotHeight);
}

void SessionMatrixView::mouseDown(const juce::MouseEvent& e)
{
    const auto pos = e.getPosition();

    // Check if clicked in Scene Triggers column (on the right of the matrix)
    const int matrixWidth = static_cast<int>(m_trackSlots.size()) * (m_slotWidth + 4) + 8;
    if (pos.getX() >= matrixWidth && pos.getX() <= matrixWidth + m_sceneColWidth)
    {
        for (int s = 0; s < kNumScenes; ++s)
        {
            const auto sceneBounds = juce::Rectangle<int>(matrixWidth + 6,
                                                         m_headerHeight + s * (m_slotHeight + 4) + 6,
                                                         m_sceneColWidth - 10,
                                                         m_slotHeight);
            if (sceneBounds.contains(pos))
            {
                launchScene(s);
                return;
            }
        }
    }

    // Check if clicked on a clip slot
    for (std::size_t t = 0; t < m_trackSlots.size(); ++t)
    {
        for (int s = 0; s < kNumScenes; ++s)
        {
            const auto bounds = getSlotBounds(static_cast<int>(t), s);
            if (bounds.contains(pos))
            {
                launchClip(static_cast<int>(t), s);
                return;
            }
        }
    }
}

void SessionMatrixView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(26, 28, 33));

    auto bounds = getLocalBounds();
    auto headerBounds = bounds.removeFromTop(m_headerHeight);

    drawTrackHeaders(g, headerBounds);
    drawGrid(g, bounds);
    drawSceneTriggers(g, bounds);
}

void SessionMatrixView::drawTrackHeaders(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    g.setColour(juce::Colour::fromRGB(32, 35, 42));
    g.fillRect(bounds);
    g.setColour(juce::Colour::fromRGB(50, 54, 64));
    g.drawHorizontalLine(bounds.getBottom() - 1, 0.0f, static_cast<float>(bounds.getWidth()));

    for (std::size_t t = 0; t < m_trackSlots.size(); ++t)
    {
        const int x = static_cast<int>(t) * (m_slotWidth + 4) + 8;
        const auto trackBounds = juce::Rectangle<int>(x, 2, m_slotWidth, bounds.getHeight() - 4);

        g.setColour(juce::Colour::fromRGB(40, 44, 52));
        g.fillRoundedRectangle(trackBounds.toFloat(), 3.0f);

        g.setColour(juce::Colour::fromRGB(215, 220, 230));
        g.setFont(juce::FontOptions(11.0f).withStyle("Bold"));

        std::string trackName = "Track " + std::to_string(t + 1);
        if (t < m_model.project.tracks.size())
            trackName = m_model.project.tracks[t].name;

        g.drawText(trackName, trackBounds.reduced(4, 0), juce::Justification::centred);
    }
}

void SessionMatrixView::drawGrid(juce::Graphics& g, juce::Rectangle<int>)
{
    for (std::size_t t = 0; t < m_trackSlots.size(); ++t)
    {
        for (int s = 0; s < kNumScenes; ++s)
        {
            const auto bounds = getSlotBounds(static_cast<int>(t), s);
            const auto& slot = m_trackSlots[t][static_cast<std::size_t>(s)];

            if (slot.hasClip)
            {
                // Filled Clip Slot
                if (slot.isPlaying)
                {
                    g.setColour(juce::Colour::fromRGB(45, 140, 75));
                    g.fillRoundedRectangle(bounds.toFloat(), 4.0f);
                    g.setColour(juce::Colour::fromRGB(120, 255, 150));
                    g.drawRoundedRectangle(bounds.toFloat(), 4.0f, 1.5f);
                }
                else
                {
                    g.setColour(slot.color.darker(0.35f));
                    g.fillRoundedRectangle(bounds.toFloat(), 4.0f);
                    g.setColour(slot.color.brighter(0.2f));
                    g.drawRoundedRectangle(bounds.toFloat(), 4.0f, 1.0f);
                }

                // Play / Stop Icon button
                const auto playBtn = juce::Rectangle<float>(static_cast<float>(bounds.getX() + 6),
                                                            static_cast<float>(bounds.getY() + 12),
                                                            20.0f, 20.0f);
                g.setColour(slot.isPlaying ? juce::Colour::fromRGB(20, 50, 25) : juce::Colour::fromRGB(30, 32, 38));
                g.fillRoundedRectangle(playBtn, 3.0f);

                if (slot.isPlaying)
                {
                    // Playing square [■]
                    g.setColour(juce::Colour::fromRGB(180, 255, 190));
                    g.fillRect(playBtn.reduced(5.0f));
                }
                else
                {
                    // Triangle [▶]
                    g.setColour(juce::Colour::fromRGB(220, 230, 240));
                    juce::Path p;
                    p.addTriangle(playBtn.getX() + 6.0f, playBtn.getY() + 5.0f,
                                 playBtn.getX() + 6.0f, playBtn.getBottom() - 5.0f,
                                 playBtn.getRight() - 5.0f, playBtn.getCentreY());
                    g.fillPath(p);
                }

                // Clip text
                g.setColour(juce::Colour::fromRGB(240, 245, 250));
                g.setFont(juce::FontOptions(11.0f).withStyle("Bold"));
                g.drawText(slot.name, bounds.withTrimmedLeft(32).withTrimmedBottom(14), juce::Justification::centredLeft);

                // Bar count
                g.setColour(juce::Colour::fromRGB(170, 180, 195));
                g.setFont(juce::FontOptions(9.5f));
                g.drawText(std::to_string(slot.lengthBars) + " Bars", bounds.withTrimmedLeft(32).removeFromBottom(16), juce::Justification::centredLeft);
            }
            else
            {
                // Empty Clip Slot
                g.setColour(juce::Colour::fromRGB(34, 37, 44));
                g.fillRoundedRectangle(bounds.toFloat(), 4.0f);
                g.setColour(juce::Colour::fromRGB(48, 52, 62));
                g.drawRoundedRectangle(bounds.toFloat(), 4.0f, 1.0f);

                // Faint [+] icon
                g.setColour(juce::Colour::fromRGB(70, 75, 88));
                g.setFont(juce::FontOptions(14.0f));
                g.drawText("+", bounds, juce::Justification::centred);
            }
        }
    }
}

void SessionMatrixView::drawSceneTriggers(juce::Graphics& g, juce::Rectangle<int>)
{
    const int matrixWidth = static_cast<int>(m_trackSlots.size()) * (m_slotWidth + 4) + 8;

    for (int s = 0; s < kNumScenes; ++s)
    {
        const auto bounds = juce::Rectangle<int>(matrixWidth + 6,
                                                 m_headerHeight + s * (m_slotHeight + 4) + 6,
                                                 m_sceneColWidth - 10,
                                                 m_slotHeight);

        g.setColour(juce::Colour::fromRGB(38, 42, 50));
        g.fillRoundedRectangle(bounds.toFloat(), 4.0f);
        g.setColour(juce::Colour::fromRGB(65, 70, 82));
        g.drawRoundedRectangle(bounds.toFloat(), 4.0f, 1.0f);

        // Play triangle
        g.setColour(juce::Colour::fromRGB(90, 200, 110));
        juce::Path p;
        p.addTriangle(static_cast<float>(bounds.getX() + 8), static_cast<float>(bounds.getY() + 15),
                     static_cast<float>(bounds.getX() + 8), static_cast<float>(bounds.getBottom() - 15),
                     static_cast<float>(bounds.getX() + 18), static_cast<float>(bounds.getCentreY()));
        g.fillPath(p);

        g.setColour(juce::Colour::fromRGB(220, 225, 235));
        g.setFont(juce::FontOptions(11.0f).withStyle("Bold"));
        g.drawText("Scene " + std::to_string(s + 1), bounds.withTrimmedLeft(24), juce::Justification::centredLeft);
    }
}

void SessionMatrixView::resized()
{
    auto bounds = getLocalBounds();
    auto topArea = bounds.removeFromTop(m_headerHeight);

    const int matrixWidth = static_cast<int>(m_trackSlots.size()) * (m_slotWidth + 4) + 8;
    m_quantizeCombo.setBounds(matrixWidth + 6, 4, 150, 24);
    m_stopAllButton.setBounds(matrixWidth + 162, 4, 130, 24);
}

} // namespace dawg::ui
