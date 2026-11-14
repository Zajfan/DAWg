#include "TimelineView.h"

#include <algorithm>
#include <cmath>

namespace dawg::ui {

TimelineView::TimelineView(Callbacks callbacks, media::WaveformCache* waveformCache)
    : m_callbacks(std::move(callbacks))
    , m_waveformCache(waveformCache)
{
    setOpaque(true);
    setWantsKeyboardFocus(true);

    m_snapButton.onClick = [this] { cycleSnapMode(); };
    addAndMakeVisible(m_snapButton);
}

void TimelineView::setViewModel(const EditorViewModel& model)
{
    m_model = model;
    repaint();
}

void TimelineView::cycleSnapMode()
{
    switch (m_snapMode)
    {
    case SnapMode::sixteenth:
        m_snapMode = SnapMode::eighth;
        m_snapButton.setButtonText("Snap: 1/8");
        break;
    case SnapMode::eighth:
        m_snapMode = SnapMode::quarter;
        m_snapButton.setButtonText("Snap: 1/4");
        break;
    case SnapMode::quarter:
        m_snapMode = SnapMode::bar;
        m_snapButton.setButtonText("Snap: Bar");
        break;
    case SnapMode::bar:
        m_snapMode = SnapMode::off;
        m_snapButton.setButtonText("Snap: Off");
        break;
    case SnapMode::off:
        m_snapMode = SnapMode::sixteenth;
        m_snapButton.setButtonText("Snap: 1/16");
        break;
    }
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

std::int64_t TimelineView::snapSample(const std::int64_t sample) const noexcept
{
    if (m_snapMode == SnapMode::off)
        return std::max<std::int64_t>(0, sample);

    const double bpm = std::max(20.0, m_model.project.tempo.bpm);
    constexpr double sampleRate = 48000.0;
    const double samplesPerBeat = (60.0 / bpm) * sampleRate;
    const int beatsPerBar = std::max<uint8_t>(1, m_model.project.tempo.timeSigNumerator);
    const double samplesPerBar = samplesPerBeat * static_cast<double>(beatsPerBar);

    double snapInterval = samplesPerBeat / 4.0; // 1/16th note
    switch (m_snapMode)
    {
    case SnapMode::bar:
        snapInterval = samplesPerBar;
        break;
    case SnapMode::quarter:
        snapInterval = samplesPerBeat;
        break;
    case SnapMode::eighth:
        snapInterval = samplesPerBeat / 2.0;
        break;
    case SnapMode::sixteenth:
        snapInterval = samplesPerBeat / 4.0;
        break;
    case SnapMode::off:
        return std::max<std::int64_t>(0, sample);
    }

    if (snapInterval <= 0.0)
        return std::max<std::int64_t>(0, sample);

    const double snapped = std::round(static_cast<double>(sample) / snapInterval) * snapInterval;
    return std::max<std::int64_t>(0, static_cast<std::int64_t>(snapped));
}

juce::Rectangle<int> TimelineView::getClipBounds(const project::AudioClip& clip, const int laneY) const noexcept
{
    const int clipX = m_headerWidth + sampleToPixel(static_cast<std::int64_t>(clip.startSample));
    const int clipW = std::max(8, sampleToPixel(static_cast<std::int64_t>(clip.lengthSamples)));
    return juce::Rectangle<int> {clipX, laneY + 3, clipW, m_trackLaneHeight - 6};
}

const project::AudioClip* TimelineView::findClipAt(juce::Point<int> pos, int& outLaneY) const noexcept
{
    if (pos.x < m_headerWidth || pos.y < m_rulerHeight)
        return nullptr;

    const auto& tracks = m_model.project.tracks;
    const auto& clips = m_model.project.clips;

    int currentY = m_rulerHeight;
    for (const auto& track : tracks)
    {
        if (pos.y >= currentY && pos.y < currentY + m_trackLaneHeight)
        {
            outLaneY = currentY;
            for (const auto& clip : clips)
            {
                if (clip.trackId == track.id)
                {
                    const auto bounds = getClipBounds(clip, currentY);
                    if (bounds.contains(pos))
                        return &clip;
                }
            }
            return nullptr;
        }
        currentY += m_trackLaneHeight;
    }
    return nullptr;
}

void TimelineView::seekToX(const int x)
{
    if (m_callbacks.seek)
    {
        const auto sample = snapSample(pixelToSample(x));
        m_callbacks.seek(sample);
    }
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
    g.setColour(juce::Colour::fromRGB(28, 30, 36));
    g.fillRect(bounds);

    auto headerCorner = bounds.removeFromLeft(m_headerWidth);
    g.setColour(juce::Colour::fromRGB(36, 38, 46));
    g.fillRect(headerCorner);

    g.setColour(juce::Colour::fromRGB(45, 48, 56));
    g.drawVerticalLine(m_headerWidth, static_cast<float>(bounds.getY()), static_cast<float>(bounds.getBottom()));
    g.drawHorizontalLine(bounds.getBottom() - 1, 0.0f, static_cast<float>(getWidth()));

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

        const juce::Colour bg = (i % 2 == 0)
            ? juce::Colour::fromRGB(24, 26, 32)
            : juce::Colour::fromRGB(21, 23, 28);
        g.setColour(bg);
        g.fillRect(laneBounds);

        // Track header
        juce::Rectangle<int> headerBounds {0, currentY, m_headerWidth, m_trackLaneHeight};
        g.setColour(juce::Colour::fromRGB(32, 34, 42));
        g.fillRect(headerBounds);

        if (track.recordArmed)
        {
            g.setColour(juce::Colour::fromRGB(220, 50, 50));
            g.fillEllipse(static_cast<float>(headerBounds.getX() + 8),
                          static_cast<float>(headerBounds.getY() + 10),
                          8.0f, 8.0f);
        }

        g.setColour(juce::Colour::fromRGB(220, 225, 235));
        g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        g.drawText(track.name, headerBounds.removeFromLeft(headerBounds.getWidth()).reduced(22, 6),
                   juce::Justification::centredLeft);

        g.setColour(juce::Colour::fromRGB(40, 42, 50));
        g.drawHorizontalLine(laneBounds.getBottom() - 1, 0.0f, static_cast<float>(getWidth()));

        // Draw clips
        for (const auto& clip : clips)
        {
            if (clip.trackId != track.id)
                continue;

            auto clipBounds = getClipBounds(clip, currentY);
            const bool isSelected = (m_selectedClipId.has_value() && *m_selectedClipId == clip.id);

            // Clip background
            g.setColour(isSelected ? juce::Colour::fromRGB(55, 95, 145) : juce::Colour::fromRGB(45, 75, 115));
            g.fillRoundedRectangle(clipBounds.toFloat(), 4.0f);

            // Clip border
            g.setColour(isSelected ? juce::Colour::fromRGB(255, 220, 80) : juce::Colour::fromRGB(80, 140, 210));
            g.drawRoundedRectangle(clipBounds.toFloat(), 4.0f, isSelected ? 2.0f : 1.0f);

            // Clip label
            g.setColour(juce::Colour::fromRGB(240, 245, 255));
            g.setFont(juce::FontOptions(11.0f));
            g.drawText("Clip #" + juce::String(clip.id),
                       clipBounds.getX() + 6, clipBounds.getY() + 3, clipBounds.getWidth() - 12, 14,
                       juce::Justification::topLeft);

            // Draw Fades
            if (clip.fadeInSamples > 0 && clip.lengthSamples > 0)
            {
                const int fadePx = std::min(clipBounds.getWidth(), sampleToPixel(clip.fadeInSamples));
                juce::Path fadeWedge;
                fadeWedge.startNewSubPath(static_cast<float>(clipBounds.getX()), static_cast<float>(clipBounds.getY()));
                fadeWedge.lineTo(static_cast<float>(clipBounds.getX() + fadePx), static_cast<float>(clipBounds.getY()));
                fadeWedge.lineTo(static_cast<float>(clipBounds.getX()), static_cast<float>(clipBounds.getBottom()));
                fadeWedge.closeSubPath();
                g.setColour(juce::Colour::fromRGB(20, 25, 35).withAlpha(0.45f));
                g.fillPath(fadeWedge);
            }
            if (clip.fadeOutSamples > 0 && clip.lengthSamples > 0)
            {
                const int fadePx = std::min(clipBounds.getWidth(), sampleToPixel(clip.fadeOutSamples));
                juce::Path fadeWedge;
                fadeWedge.startNewSubPath(static_cast<float>(clipBounds.getRight()), static_cast<float>(clipBounds.getY()));
                fadeWedge.lineTo(static_cast<float>(clipBounds.getRight() - fadePx), static_cast<float>(clipBounds.getY()));
                fadeWedge.lineTo(static_cast<float>(clipBounds.getRight()), static_cast<float>(clipBounds.getBottom()));
                fadeWedge.closeSubPath();
                g.setColour(juce::Colour::fromRGB(20, 25, 35).withAlpha(0.45f));
                g.fillPath(fadeWedge);
            }

            // Draw waveform
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
                    g.setColour(isSelected ? juce::Colour::fromRGB(210, 235, 255) : juce::Colour::fromRGB(160, 215, 255));
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

    g.setColour(juce::Colour::fromRGB(50, 130, 200).withAlpha(0.12f));
    g.fillRect(loopRect);

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

    g.setColour(juce::Colour::fromRGB(255, 170, 40));
    g.drawVerticalLine(playheadPx, static_cast<float>(bounds.getY()), static_cast<float>(bounds.getBottom()));

    juce::Path head;
    head.addTriangle(
        static_cast<float>(playheadPx - 6), static_cast<float>(bounds.getY()),
        static_cast<float>(playheadPx + 6), static_cast<float>(bounds.getY()),
        static_cast<float>(playheadPx), static_cast<float>(bounds.getY() + 10));
    g.fillPath(head);
}

void TimelineView::resized()
{
    m_snapButton.setBounds(6, 4, m_headerWidth - 12, m_rulerHeight - 8);
}

void TimelineView::mouseMove(const juce::MouseEvent& e)
{
    if (e.y < m_rulerHeight)
    {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        return;
    }

    int laneY = 0;
    const auto* clip = findClipAt(e.getPosition(), laneY);
    if (clip != nullptr)
    {
        const auto bounds = getClipBounds(*clip, laneY);
        if (e.x <= bounds.getX() + 6 || e.x >= bounds.getRight() - 6)
        {
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
            return;
        }
        if ((e.x <= bounds.getX() + 16 || e.x >= bounds.getRight() - 16) && e.y <= bounds.getY() + 12)
        {
            setMouseCursor(juce::MouseCursor::UpDownLeftRightResizeCursor);
            return;
        }
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        return;
    }

    setMouseCursor(juce::MouseCursor::NormalCursor);
}

void TimelineView::mouseDown(const juce::MouseEvent& e)
{
    grabKeyboardFocus();

    if (e.y < m_rulerHeight)
    {
        m_dragMode = DragMode::seekRuler;
        seekToX(e.x);
        return;
    }

    int laneY = 0;
    const auto* clip = findClipAt(e.getPosition(), laneY);
    if (clip != nullptr)
    {
        m_selectedClipId = clip->id;
        m_draggedClipId = clip->id;
        m_dragInitialClip = *clip;
        m_dragStartPos = e.getPosition();

        const auto bounds = getClipBounds(*clip, laneY);
        if (e.x <= bounds.getX() + 6)
        {
            m_dragMode = DragMode::trimLeft;
        }
        else if (e.x >= bounds.getRight() - 6)
        {
            m_dragMode = DragMode::trimRight;
        }
        else if (e.x <= bounds.getX() + 16 && e.y <= bounds.getY() + 12)
        {
            m_dragMode = DragMode::fadeLeft;
        }
        else if (e.x >= bounds.getRight() - 16 && e.y <= bounds.getY() + 12)
        {
            m_dragMode = DragMode::fadeRight;
        }
        else
        {
            m_dragMode = DragMode::moveClip;
        }
        repaint();
        return;
    }

    // Empty space click
    m_selectedClipId = std::nullopt;
    m_dragMode = DragMode::seekRuler;
    seekToX(e.x);
    repaint();
}

void TimelineView::mouseDrag(const juce::MouseEvent& e)
{
    if (m_dragMode == DragMode::seekRuler)
    {
        seekToX(e.x);
        return;
    }

    if (! m_draggedClipId.has_value())
        return;

    const int deltaX = e.x - m_dragStartPos.x;
    const auto deltaSample = static_cast<std::int64_t>(std::round(static_cast<double>(deltaX) * m_samplesPerPixel));

    if (m_dragMode == DragMode::moveClip)
    {
        const auto rawStart = static_cast<std::int64_t>(m_dragInitialClip.startSample) + deltaSample;
        m_dragTargetStartSample = snapSample(rawStart);

        // Track reassignment based on Y
        const int laneIdx = std::clamp((e.y - m_rulerHeight) / m_trackLaneHeight, 0,
                                       static_cast<int>(m_model.project.tracks.size()) - 1);
        if (! m_model.project.tracks.empty())
        {
            m_dragTargetTrackId = m_model.project.tracks[static_cast<std::size_t>(laneIdx)].id;
        }
    }
    else if (m_dragMode == DragMode::trimLeft)
    {
        const auto rawTrim = static_cast<std::int64_t>(deltaSample);
        if (rawTrim >= 0)
        {
            const auto trimFrames = static_cast<std::uint64_t>(rawTrim);
            if (trimFrames < m_dragInitialClip.lengthSamples)
            {
                m_dragTrimSourceOffset = m_dragInitialClip.sourceOffset + trimFrames;
                m_dragTrimLengthSamples = m_dragInitialClip.lengthSamples - trimFrames;
            }
        }
        else
        {
            const auto expandFrames = static_cast<std::uint64_t>(-rawTrim);
            const auto safeExpand = std::min(expandFrames, m_dragInitialClip.sourceOffset);
            m_dragTrimSourceOffset = m_dragInitialClip.sourceOffset - safeExpand;
            m_dragTrimLengthSamples = m_dragInitialClip.lengthSamples + safeExpand;
        }
    }
    else if (m_dragMode == DragMode::trimRight)
    {
        const auto newLen = static_cast<std::int64_t>(m_dragInitialClip.lengthSamples) + deltaSample;
        m_dragTrimLengthSamples = static_cast<std::uint64_t>(std::max<std::int64_t>(128, newLen));
        m_dragTrimSourceOffset = m_dragInitialClip.sourceOffset;
    }
    else if (m_dragMode == DragMode::fadeLeft)
    {
        const int fadePx = std::max(0, e.x - (m_headerWidth + sampleToPixel(static_cast<std::int64_t>(m_dragInitialClip.startSample))));
        const auto maxFade = static_cast<std::int64_t>(m_dragInitialClip.lengthSamples / 2);
        m_dragFadeInSamples = static_cast<std::uint32_t>(
            std::min<std::int64_t>(maxFade,
                                  static_cast<std::int64_t>(static_cast<double>(fadePx) * m_samplesPerPixel)));
    }
    else if (m_dragMode == DragMode::fadeRight)
    {
        const int clipRightPx = m_headerWidth + sampleToPixel(static_cast<std::int64_t>(m_dragInitialClip.startSample + m_dragInitialClip.lengthSamples));
        const int fadePx = std::max(0, clipRightPx - e.x);
        const auto maxFade = static_cast<std::int64_t>(m_dragInitialClip.lengthSamples / 2);
        m_dragFadeOutSamples = static_cast<std::uint32_t>(
            std::min<std::int64_t>(maxFade,
                                  static_cast<std::int64_t>(static_cast<double>(fadePx) * m_samplesPerPixel)));
    }
}

void TimelineView::mouseUp(const juce::MouseEvent&)
{
    if (m_draggedClipId.has_value() && m_callbacks.commandHandler)
    {
        if (m_dragMode == DragMode::moveClip)
        {
            if (m_dragTargetTrackId != 0 && (m_dragTargetStartSample != static_cast<std::int64_t>(m_dragInitialClip.startSample)
                                             || m_dragTargetTrackId != m_dragInitialClip.trackId))
            {
                m_callbacks.commandHandler(project::MoveClip {
                    *m_draggedClipId,
                    m_dragTargetTrackId,
                    static_cast<std::uint64_t>(m_dragTargetStartSample)
                });
            }
        }
        else if (m_dragMode == DragMode::trimLeft || m_dragMode == DragMode::trimRight)
        {
            if (m_dragTrimLengthSamples > 0 && (m_dragTrimLengthSamples != m_dragInitialClip.lengthSamples
                                                || m_dragTrimSourceOffset != m_dragInitialClip.sourceOffset))
            {
                m_callbacks.commandHandler(project::TrimClip {
                    *m_draggedClipId,
                    m_dragTrimSourceOffset,
                    m_dragTrimLengthSamples
                });
            }
        }
        else if (m_dragMode == DragMode::fadeLeft || m_dragMode == DragMode::fadeRight)
        {
            m_callbacks.commandHandler(project::SetClipGainFade {
                *m_draggedClipId,
                m_dragInitialClip.gainLinear,
                m_dragFadeInSamples,
                m_dragFadeOutSamples
            });
        }
    }

    m_dragMode = DragMode::none;
    m_draggedClipId = std::nullopt;
}

bool TimelineView::keyPressed(const juce::KeyPress& key)
{
    // Space: Play / Stop toggle
    if (key.getKeyCode() == juce::KeyPress::spaceKey)
    {
        if (m_callbacks.togglePlay)
        {
            m_callbacks.togglePlay();
            return true;
        }
    }

    // Delete / Backspace: Remove selected clip
    if (key.getKeyCode() == juce::KeyPress::deleteKey || key.getKeyCode() == juce::KeyPress::backspaceKey)
    {
        if (m_selectedClipId.has_value() && m_callbacks.commandHandler)
        {
            m_callbacks.commandHandler(project::RemoveClip {*m_selectedClipId});
            m_selectedClipId = std::nullopt;
            return true;
        }
    }

    // S: Split selected clip at playhead position
    if (key.getKeyCode() == 'S' || key.getKeyCode() == 's')
    {
        if (m_selectedClipId.has_value() && m_callbacks.commandHandler)
        {
            const auto playhead = static_cast<std::uint64_t>(std::max<std::int64_t>(0, m_model.playbackPosition));
            for (const auto& clip : m_model.project.clips)
            {
                if (clip.id == *m_selectedClipId)
                {
                    if (playhead > clip.startSample && playhead < clip.startSample + clip.lengthSamples)
                    {
                        m_callbacks.commandHandler(project::SplitClip {clip.id, playhead});
                        return true;
                    }
                    break;
                }
            }
        }
    }

    // Ctrl+D: Duplicate selected clip
    if (key.getModifiers().isCtrlDown() && (key.getKeyCode() == 'D' || key.getKeyCode() == 'd'))
    {
        if (m_selectedClipId.has_value() && m_callbacks.commandHandler)
        {
            m_callbacks.commandHandler(project::DuplicateClip {*m_selectedClipId});
            return true;
        }
    }

    // Ctrl+Z: Undo
    if (key.getModifiers().isCtrlDown() && ! key.getModifiers().isShiftDown()
        && (key.getKeyCode() == 'Z' || key.getKeyCode() == 'z'))
    {
        if (m_callbacks.undo)
        {
            m_callbacks.undo();
            return true;
        }
    }

    // Ctrl+Y or Ctrl+Shift+Z: Redo
    if ((key.getModifiers().isCtrlDown() && (key.getKeyCode() == 'Y' || key.getKeyCode() == 'y'))
        || (key.getModifiers().isCtrlDown() && key.getModifiers().isShiftDown()
            && (key.getKeyCode() == 'Z' || key.getKeyCode() == 'z')))
    {
        if (m_callbacks.redo)
        {
            m_callbacks.redo();
            return true;
        }
    }

    return false;
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
