#include "VideoPlaybackWindow.h"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace dawg::ui {

std::unique_ptr<VideoPlaybackWindow> VideoPlaybackWindow::s_instance = nullptr;

void VideoPlaybackWindow::open(const EditorViewModel& model)
{
    if (s_instance == nullptr)
    {
        s_instance = std::unique_ptr<VideoPlaybackWindow>(new VideoPlaybackWindow(model));
    }
    s_instance->setVisible(true);
    s_instance->toFront(true);
}

void VideoPlaybackWindow::close()
{
    s_instance.reset();
}

void VideoPlaybackWindow::updatePlayhead(const EditorViewModel& model)
{
    if (s_instance != nullptr)
    {
        s_instance->m_content.setViewModel(model);
    }
}

bool VideoPlaybackWindow::isOpen() noexcept
{
    return s_instance != nullptr && s_instance->isVisible();
}

VideoPlaybackWindow::VideoPlaybackWindow(const EditorViewModel& model)
    : juce::DocumentWindow("DAWg Video Sync & Picture Monitor (SMPTE)",
                           juce::Colour::fromRGB(18, 20, 26),
                           juce::DocumentWindow::closeButton)
    , m_content(model)
{
    setUsingNativeTitleBar(true);
    setContentNonOwned(&m_content, true);
    setResizable(true, true);
    setResizeLimits(400, 250, 1920, 1080);
    centreWithSize(640, 390);
}

VideoPlaybackWindow::~VideoPlaybackWindow() = default;

void VideoPlaybackWindow::closeButtonPressed()
{
    setVisible(false);
}

// ============================================================================
// ContentComponent implementation
// ============================================================================

VideoPlaybackWindow::ContentComponent::ContentComponent(const EditorViewModel& model)
    : m_model(model)
{
    m_fpsSelector.addItem("23.976 fps (Film HD)", 1);
    m_fpsSelector.addItem("24.0 fps (Cinema Standard)", 2);
    m_fpsSelector.addItem("25.0 fps (PAL / European TV)", 3);
    m_fpsSelector.addItem("29.97 fps (NTSC Drop-Frame)", 4);
    m_fpsSelector.addItem("29.97 fps (NTSC Non-Drop)", 5);
    m_fpsSelector.addItem("30.0 fps (Broadcast)", 6);
    m_fpsSelector.addItem("60.0 fps (High Frame Rate)", 7);
    m_fpsSelector.setSelectedId(2);
    m_fpsSelector.onChange = [this] {
        switch (m_fpsSelector.getSelectedId())
        {
        case 1: m_frameRate = engine::SmpteFrameRate::fps23_976; break;
        case 2: m_frameRate = engine::SmpteFrameRate::fps24; break;
        case 3: m_frameRate = engine::SmpteFrameRate::fps25; break;
        case 4: m_frameRate = engine::SmpteFrameRate::fps29_97_df; break;
        case 5: m_frameRate = engine::SmpteFrameRate::fps29_97_ndf; break;
        case 6: m_frameRate = engine::SmpteFrameRate::fps30; break;
        case 7: m_frameRate = engine::SmpteFrameRate::fps60; break;
        default: m_frameRate = engine::SmpteFrameRate::fps24; break;
        }
        repaint();
    };
    addAndMakeVisible(m_fpsSelector);

    m_lblTimecode.setFont(juce::FontOptions(26.0f, juce::Font::bold));
    m_lblTimecode.setColour(juce::Label::textColourId, juce::Colour::fromRGB(100, 220, 255));
    m_lblTimecode.setJustificationType(juce::Justification::centred);
    m_lblTimecode.setText("01:00:00:00", juce::dontSendNotification);
    addAndMakeVisible(m_lblTimecode);

    m_lblFrameNumber.setFont(juce::FontOptions(12.0f));
    m_lblFrameNumber.setColour(juce::Label::textColourId, juce::Colour::fromRGB(170, 180, 200));
    m_lblFrameNumber.setJustificationType(juce::Justification::centred);
    m_lblFrameNumber.setText("Frame: 86400", juce::dontSendNotification);
    addAndMakeVisible(m_lblFrameNumber);

    m_lblSyncStatus.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    m_lblSyncStatus.setColour(juce::Label::textColourId, juce::Colour::fromRGB(80, 220, 120));
    m_lblSyncStatus.setJustificationType(juce::Justification::centred);
    m_lblSyncStatus.setText("[SYNC] LOCKED TO TIMELINE PLAYHEAD", juce::dontSendNotification);
    addAndMakeVisible(m_lblSyncStatus);
}

void VideoPlaybackWindow::ContentComponent::setViewModel(const EditorViewModel& model)
{
    m_model = model;
    const double sr = m_model.sampleRate > 0.0 ? m_model.sampleRate : 48000.0;
    const auto curSample = static_cast<std::uint64_t>(std::max<std::int64_t>(0, m_model.playbackPosition));

    // Calculate SMPTE timecode (1 hour nominal timeline start offset standard for film reels)
    const std::int64_t oneHourOffsetSamples = static_cast<std::int64_t>(sr * 3600.0);
    const auto tc = engine::SmpteTimecodeEngine::sampleToTimecode(curSample, sr, m_frameRate, oneHourOffsetSamples);
    const auto tcStr = engine::SmpteTimecodeEngine::formatTimecode(tc, true);
    m_lblTimecode.setText(tcStr, juce::dontSendNotification);

    const double fps = engine::SmpteTimecodeEngine::getFramesPerSecond(m_frameRate);
    const auto totalFrames = static_cast<std::uint64_t>((static_cast<double>(curSample + static_cast<std::uint64_t>(std::max<std::int64_t>(0, oneHourOffsetSamples))) / sr) * fps);
    m_lblFrameNumber.setText("Frame: " + juce::String(totalFrames) + "  |  Time: " + juce::String(static_cast<double>(curSample) / sr, 2) + "s", juce::dontSendNotification);

    m_lblSyncStatus.setText(m_model.playing ? "[PLAY] RUNNING (SAMPLE-ACCURATE SYNC)" : "[STOP] PARKED (FRAME-ACCURATE SCRUB)", juce::dontSendNotification);
    m_lblSyncStatus.setColour(juce::Label::textColourId, m_model.playing ? juce::Colour::fromRGB(80, 220, 120) : juce::Colour::fromRGB(255, 180, 50));
    repaint();
}

void VideoPlaybackWindow::ContentComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(14, 16, 22));

    auto screenArea = getLocalBounds().reduced(16, 48);
    // Draw 16:9 Cinema Letterbox frame
    g.setColour(juce::Colours::black);
    g.fillRect(screenArea);
    g.setColour(juce::Colour::fromRGB(45, 50, 65));
    g.drawRect(screenArea, 1);

    // Crosshairs and safe title area guide (SMPTE action safe rectangle)
    auto safeArea = screenArea.reduced(screenArea.getWidth() / 10, screenArea.getHeight() / 10);
    g.setColour(juce::Colour::fromRGBA(100, 150, 220, 60));
    g.drawRect(safeArea, 1.0f);

    // Center crosshair
    const int cx = screenArea.getCentreX();
    const int cy = screenArea.getCentreY();
    g.drawHorizontalLine(cy, static_cast<float>(cx - 15), static_cast<float>(cx + 15));
    g.drawVerticalLine(cx, static_cast<float>(cy - 15), static_cast<float>(cy + 15));

    // Cinema aspect ratio watermark
    g.setFont(juce::FontOptions(13.0f));
    g.setColour(juce::Colour::fromRGB(70, 75, 90));
    g.drawText("PICTURE MONITOR [16:9 DCI] - NO VIDEO ASSET ATTACHED", screenArea, juce::Justification::centred, false);
}

void VideoPlaybackWindow::ContentComponent::resized()
{
    auto area = getLocalBounds();
    auto topBar = area.removeFromTop(36).reduced(8, 4);
    m_fpsSelector.setBounds(topBar.removeFromLeft(200));
    m_lblSyncStatus.setBounds(topBar);

    auto bottomBar = area.removeFromBottom(48);
    m_lblTimecode.setBounds(bottomBar.removeFromTop(30));
    m_lblFrameNumber.setBounds(bottomBar);
}

} // namespace dawg::ui
