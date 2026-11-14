#include "TimelineView.h"

#include <algorithm>
#include <cmath>

namespace dawg::ui {

TimelineView::TimelineView(CommandHandler handler,
                           media::WaveformCache* waveformCache,
                           SeekCallback seekCallback)
    : m_handler(std::move(handler))
    , m_waveformCache(waveformCache)
    , m_seekCallback(std::move(seekCallback))
{
    setOpaque(true);
}

void TimelineView::setViewModel(const EditorViewModel& model)
{
    m_model = model;
    repaint();
}

int TimelineView::sampleToPixel(const std::int64_t sample) const noexcept
{
    if (m_samplesPerPixel <= 0.0)
        return 0;
    return static_cast<int>(std::round(static_cast<double>(sample) / m_samplesPerPixel)) - m_scrollOffsetX;
}

std::int64_t TimelineView::pixelToSample(const int x) const noexcept
{
    const int timelineX = x - m_headerWidth + m_scrollOffsetX;
    if (timelineX <= 0)
        return 0;
    return static_cast<std::int64_t>(std::round(static_cast<double>(timelineX) * m_samplesPerPixel));
}

void TimelineView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(20, 21, 25));

    auto totalBounds = getLocalBounds();
    const auto rulerBounds = totalBounds.removeFromTop(m_rulerHeight);
    const auto tracksBounds = totalBounds;

    drawRuler(g, rulerBounds);
    drawTracksAndClips(g, tracksBounds);
    drawLoopRegion(g, getLocalBounds());
    drawPlayhead(g, getLocalBounds());
}

void TimelineView::drawRuler(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    // Background
    g.setColour(juce::Colour::fromRGB(28, 30, 36));
    g.fillRect(bounds);

    // Header corner
    auto headerCorner = bounds.removeFromLeft(m_headerWidth);
    g.setColour(juce::Colour::fromRGB(36, 38, 46));
    g.fillRect(headerCorner);
    g.setColour(juce::Colour::fromRGB(150, 155, 170));
    g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    g.drawText("TRACKS", headerCorner.reduced(6, 0), juce::Justification::centredLeft);

    // Separators
    g.setColour(juce::Colour::fromRGB(45, 48, 56));
    g.drawVerticalLine(m_headerWidth, static_cast<float>(bounds.getY()), static_cast<float>(bounds.getBottom()));
    g.drawHorizontalLine(bounds.getBottom() - 1, 0.0f, static_cast<float>(getWidth()));

    // Ruler ticks based on BPM and Time Signature
    const double sampleRate = 48000.0;
    const double bpm = std::max(20.0, m_model.project.tempo.bpm);
    const double samplesPerBeat = (60.0 / bpm) * sampleRate;
    const int beatsPerBar = std::max<uint8_t>(1, m_model.project.tempo.timeSigNumerator);
    const double samplesPerBar = samplesPerBeat * beatsPerBar;

    if (samplesPerBar <= 0.0 || m_samplesPerPixel <= 0.0)
        return;

    const int timelineWidth = bounds.getWidth();
    const std::int64_t startSample = pixelToSample(m_headerWidth);
    const std::int64_t endSample = pixelToSample(m_headerWidth + timelineWidth);

    const int firstBar = static_cast<int>(startSample / samplesPerBar);
    const int lastBar = static_cast<int>(endSample / samplesPerBar) + 2;

    g.setFont(juce::FontOptions(11.0f));

    for (int barIdx = firstBar; barIdx <= lastBar; ++barIdx)
    {
        const auto barSample = static_cast<std::int64_t>(std::round(static_cast<double>(barIdx) * samplesPerBar));
        const int barPx = m_headerWidth + sampleToPixel(barSample);

        if (barPx >= m_headerWidth && barPx <= getWidth())
        {
            g.setColour(juce::Colour::fromRGB(80, 85, 100));
            g.drawVerticalLine(barPx, static_cast<float>(bounds.getY() + 10), static_cast<float>(bounds.getBottom() - 1));

            g.setColour(juce::Colour::fromRGB(180, 185, 200));
            g.drawText(juce::String(barIdx + 1), barPx + 4, bounds.getY() + 2, 40, 14, juce::Justification::left);
        }

        // Beat sub-ticks
        for (int b = 1; b < beatsPerBar; ++b)
        {
            const auto beatSample = static_cast<std::int64_t>(std::round(static_cast<double>(barSample) + static_cast<double>(b) * samplesPerBeat));
            const int beatPx = m_headerWidth + sampleToPixel(beatSample);
            if (beatPx >= m_headerWidth && beatPx <= getWidth())
            {
                g.setColour(juce::Colour::fromRGB(50, 54, 65));
                g.drawVerticalLine(beatPx, static_cast<float>(bounds.getY() + 18), static_cast<float>(bounds.getBottom() - 1));
            }
        }
    }
}

void TimelineView::drawTracksAndClips(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    const auto& tracks = m_model.project.tracks;
    const auto& clips = m_model.project.clips;

    int currentY = bounds.getY();

    for (std::size_t i = 0; i < tracks.size(); ++i)
    {
        const auto& track = tracks[i];
        const juce::Rectangle<int> laneBounds {0, currentY, getWidth(), m_trackLaneHeight};

        // Alternate lane background
        const juce::Colour bg = (i % 2 == 0)
            ? juce::Colour::fromRGB(24, 26, 32)
            : juce::Colour::fromRGB(21, 23, 28);
        g.setColour(bg);
        g.fillRect(laneBounds);

        // Track header
        juce::Rectangle<int> headerBounds {0, currentY, m_headerWidth, m_trackLaneHeight};
        g.setColour(juce::Colour::fromRGB(32, 34, 42));
        g.fillRect(headerBounds);

        // Record arm badge
        if (track.recordArmed)
        {
            g.setColour(juce::Colour::fromRGB(220, 50, 50));
            g.fillEllipse(static_cast<float>(headerBounds.getX() + 8),
                          static_cast<float>(headerBounds.getY() + 10),
                          8.0f, 8.0f);
        }

        // Track Name
        g.setColour(juce::Colour::fromRGB(220, 225, 235));
        g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        g.drawText(track.name, headerBounds.removeFromLeft(headerBounds.getWidth()).reduced(22, 6),
                   juce::Justification::centredLeft);

        // Lane separator line
        g.setColour(juce::Colour::fromRGB(40, 42, 50));
        g.drawHorizontalLine(laneBounds.getBottom() - 1, 0.0f, static_cast<float>(getWidth()));

        // Draw clips for this track
        for (const auto& clip : clips)
        {
            if (clip.trackId != track.id)
                continue;

            const int clipX = m_headerWidth + sampleToPixel(static_cast<std::int64_t>(clip.startSample));
            const int clipW = std::max(6, sampleToPixel(static_cast<std::int64_t>(clip.lengthSamples)));
            const auto clipBounds = juce::Rectangle<int> {clipX, currentY + 3, clipW, m_trackLaneHeight - 6};

            // Clip body background & border
            g.setColour(juce::Colour::fromRGB(45, 75, 115));
            g.fillRoundedRectangle(clipBounds.toFloat(), 4.0f);

            g.setColour(juce::Colour::fromRGB(80, 140, 210));
            g.drawRoundedRectangle(clipBounds.toFloat(), 4.0f, 1.0f);

            // Clip label
            g.setColour(juce::Colour::fromRGB(240, 245, 255));
            g.setFont(juce::FontOptions(11.0f));
            g.drawText("Clip #" + juce::String(clip.id),
                       clipBounds.getX() + 6, clipBounds.getY() + 3, clipBounds.getWidth() - 12, 14,
                       juce::Justification::topLeft);

            // Draw waveform from cache
            if (m_waveformCache != nullptr && clipBounds.getWidth() > 4)
            {
                const auto peaks = m_waveformCache->getMinMaxPeaks(
                    clip.assetId,
                    0,
                    clip.sourceOffset,
                    clip.lengthSamples,
                    static_cast<std::size_t>(clipBounds.getWidth()));

                if (! peaks.empty())
                {
                    g.setColour(juce::Colour::fromRGB(160, 215, 255));
                    const int centerY = clipBounds.getCentreY() + 4;
                    const int halfHeight = (clipBounds.getHeight() - 18) / 2;

                    for (int p = 0; p < clipBounds.getWidth() && p < static_cast<int>(peaks.size()); ++p)
                    {
                        const float minVal = peaks[static_cast<std::size_t>(p)].minVal;
                        const float maxVal = peaks[static_cast<std::size_t>(p)].maxVal;

                        const int y1 = centerY - static_cast<int>(std::round(maxVal * static_cast<float>(halfHeight)));
                        const int y2 = centerY - static_cast<int>(std::round(minVal * static_cast<float>(halfHeight)));

                        g.drawVerticalLine(clipBounds.getX() + p,
                                           static_cast<float>(std::min(y1, y2)),
                                           static_cast<float>(std::max(y1, y2) + 1));
                    }
                }
            }
        }

        currentY += m_trackLaneHeight;
    }

    // Header vertical divider
    g.setColour(juce::Colour::fromRGB(45, 48, 56));
    g.drawVerticalLine(m_headerWidth, static_cast<float>(bounds.getY()), static_cast<float>(bounds.getBottom()));
}

void TimelineView::drawLoopRegion(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    if (! m_model.project.loop.enabled)
        return;

    const int loopStartPx = m_headerWidth + sampleToPixel(static_cast<std::int64_t>(m_model.project.loop.startSample));
    const int loopEndPx = m_headerWidth + sampleToPixel(static_cast<std::int64_t>(m_model.project.loop.endSample));

    if (loopEndPx <= loopStartPx)
        return;

    const auto loopRect = juce::Rectangle<int> {
        loopStartPx,
        bounds.getY(),
        loopEndPx - loopStartPx,
        bounds.getHeight()
    };

    // Translucent loop tint
    g.setColour(juce::Colour::fromRGB(50, 130, 200).withAlpha(0.12f));
    g.fillRect(loopRect);

    // Top loop bracket on ruler
    g.setColour(juce::Colour::fromRGB(70, 160, 240));
    g.drawHorizontalLine(bounds.getY(), static_cast<float>(loopStartPx), static_cast<float>(loopEndPx));
    g.drawVerticalLine(loopStartPx, static_cast<float>(bounds.getY()), static_cast<float>(bounds.getY() + 10));
    g.drawVerticalLine(loopEndPx, static_cast<float>(bounds.getY()), static_cast<float>(bounds.getY() + 10));
}

void TimelineView::drawPlayhead(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    const int playheadPx = m_headerWidth + sampleToPixel(m_model.playbackPosition);
    if (playheadPx < m_headerWidth || playheadPx > getWidth())
        return;

    // Vertical cursor line
    g.setColour(juce::Colour::fromRGB(255, 170, 40));
    g.drawVerticalLine(playheadPx, static_cast<float>(bounds.getY()), static_cast<float>(bounds.getBottom()));

    // Cursor triangle head at top
    juce::Path head;
    head.addTriangle(
        static_cast<float>(playheadPx - 6), static_cast<float>(bounds.getY()),
        static_cast<float>(playheadPx + 6), static_cast<float>(bounds.getY()),
        static_cast<float>(playheadPx), static_cast<float>(bounds.getY() + 10));
    g.fillPath(head);
}

void TimelineView::resized()
{
}

void TimelineView::mouseDown(const juce::MouseEvent& e)
{
    if (e.x >= m_headerWidth && m_seekCallback)
    {
        const auto sample = pixelToSample(e.x);
        m_seekCallback(sample);
    }
}

void TimelineView::mouseDrag(const juce::MouseEvent& e)
{
    if (e.x >= m_headerWidth && m_seekCallback)
    {
        const auto sample = pixelToSample(e.x);
        m_seekCallback(sample);
    }
}

void TimelineView::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (std::abs(wheel.deltaY) > 1e-4f)
    {
        const double factor = wheel.deltaY > 0.0f ? 0.85 : 1.15;
        m_samplesPerPixel = std::clamp(m_samplesPerPixel * factor, 8.0, 8192.0);
        repaint();
    }
}

} // namespace dawg::ui
