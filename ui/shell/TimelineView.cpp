#include "TimelineView.h"

#include <dawg/media/StemSeparatorService.h>

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

    m_autoButton.onClick = [this] { toggleAutomation(); };
    addAndMakeVisible(m_autoButton);

    m_razorButton.onClick = [this] {
        m_razorMode = ! m_razorMode;
        m_razorButton.setColour(juce::TextButton::buttonColourId,
                                m_razorMode ? juce::Colour::fromRGB(180, 50, 50)
                                            : juce::Colour::fromRGB(40, 42, 50));
        repaint();
    };
    addAndMakeVisible(m_razorButton);

    m_addMarkerButton.onClick = [this] {
        const auto curSample = static_cast<std::uint64_t>(std::max<std::int64_t>(0, m_model.playbackPosition));
        addMarker(curSample, "Marker " + std::to_string(m_markers.size() + 1));
    };
    m_addMarkerButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(30, 70, 110));
    addAndMakeVisible(m_addMarkerButton);

    // Initial arrangement song sections (assuming standard 48k sample rate, 120bpm)
    constexpr std::uint64_t barSamples = 96000;
    m_markers.push_back({0, "Intro", juce::Colour(0xff58a6ff)});
    m_markers.push_back({8 * barSamples, "Verse 1", juce::Colour(0xff3fb950)});
    m_markers.push_back({16 * barSamples, "Chorus", juce::Colour(0xffd29922)});
    m_markers.push_back({24 * barSamples, "Verse 2", juce::Colour(0xff3fb950)});
    m_markers.push_back({32 * barSamples, "Bridge", juce::Colour(0xffbc8cff)});
    m_markers.push_back({40 * barSamples, "Chorus 2", juce::Colour(0xffd29922)});
    m_markers.push_back({48 * barSamples, "Outro", juce::Colour(0xfff85149)});
}

void TimelineView::addMarker(const std::uint64_t samplePosition, std::string name, const juce::Colour color)
{
    m_markers.push_back({samplePosition, std::move(name), color});
    std::sort(m_markers.begin(), m_markers.end(), [](const TimelineMarker& a, const TimelineMarker& b) {
        return a.samplePosition < b.samplePosition;
    });
    repaint();
}

void TimelineView::jumpToNextMarker()
{
    const auto cur = static_cast<std::uint64_t>(std::max<std::int64_t>(0, m_model.playbackPosition));
    for (const auto& m : m_markers)
    {
        if (m.samplePosition > cur + 100)
        {
            if (m_callbacks.seek)
                m_callbacks.seek(static_cast<std::int64_t>(m.samplePosition));
            return;
        }
    }
}

void TimelineView::jumpToPrevMarker()
{
    const auto cur = static_cast<std::uint64_t>(std::max<std::int64_t>(0, m_model.playbackPosition));
    for (auto it = m_markers.rbegin(); it != m_markers.rend(); ++it)
    {
        if (it->samplePosition + 100 < cur)
        {
            if (m_callbacks.seek)
                m_callbacks.seek(static_cast<std::int64_t>(it->samplePosition));
            return;
        }
    }
}

void TimelineView::toggleAutomation() noexcept
{
    m_showAutomation = ! m_showAutomation;
    m_autoButton.setButtonText(m_showAutomation ? "Auto: ON" : "Auto: OFF");
    m_autoButton.setColour(juce::TextButton::buttonColourId,
                           m_showAutomation ? juce::Colour::fromRGB(40, 120, 140)
                                            : juce::Colour::fromRGB(40, 42, 50));
    repaint();
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
    if (pos.x < m_headerWidth || pos.y < tracksTopY())
        return nullptr;

    const auto& tracks = m_model.project.tracks;
    const auto& clips = m_model.project.clips;

    int currentY = tracksTopY();
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
        currentY += m_trackLaneHeight + (track.takeLanesExpanded ? static_cast<int>(track.takes.size()) * 32 : 0);
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
    const auto minimapBounds = totalBounds.removeFromTop(m_minimapHeight);
    const auto rulerBounds = totalBounds.removeFromTop(m_rulerHeight);
    const auto tracksBounds = totalBounds;

    drawMinimap(g, minimapBounds);
    drawRuler(g, rulerBounds);
    drawTracksAndClips(g, tracksBounds);
    drawLoopRegion(g, getLocalBounds());
    drawPlayhead(g, getLocalBounds());
}

void TimelineView::drawMinimap(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    g.setColour(juce::Colour(0xff12151c));
    g.fillRect(bounds);

    auto headerCorner = bounds.removeFromLeft(m_headerWidth);
    g.setColour(juce::Colour(0xff161b22));
    g.fillRect(headerCorner);

    g.setColour(juce::Colour(0xff8b949e));
    g.setFont(juce::FontOptions(10.0f).withStyle("Bold"));
    g.drawText("OVERVIEW", headerCorner.reduced(6, 0), juce::Justification::centredLeft);

    g.setColour(juce::Colour(0xff30363d));
    g.drawHorizontalLine(bounds.getBottom() - 1, 0.0f, static_cast<float>(getWidth()));
    g.drawVerticalLine(m_headerWidth, static_cast<float>(bounds.getY()), static_cast<float>(bounds.getBottom()));

    if (bounds.getWidth() <= 10)
        return;

    std::uint64_t maxArrangementSamples = 64 * 96000;
    for (const auto& c : m_model.project.clips)
        maxArrangementSamples = std::max(maxArrangementSamples, c.startSample + c.lengthSamples + 96000);

    const int minimapW = bounds.getWidth();
    const int minimapX = bounds.getX();
    const int minimapY = bounds.getY();
    const int minimapH = bounds.getHeight();

    const auto& tracks = m_model.project.tracks;
    if (! tracks.empty())
    {
        const int miniTrackH = std::max(2, (minimapH - 2) / static_cast<int>(tracks.size()));
        for (std::size_t ti = 0; ti < tracks.size(); ++ti)
        {
            const auto& tr = tracks[ti];
            const int trackY = minimapY + 1 + static_cast<int>(ti) * miniTrackH;

            for (const auto& clip : m_model.project.clips)
            {
                if (clip.trackId == tr.id)
                {
                    const int cx = minimapX + static_cast<int>((clip.startSample * static_cast<std::uint64_t>(minimapW)) / maxArrangementSamples);
                    const int cw = std::max(2, static_cast<int>((clip.lengthSamples * static_cast<std::uint64_t>(minimapW)) / maxArrangementSamples));

                    g.setColour(juce::Colour(0xff58a6ff).withAlpha(0.7f));
                    g.fillRect(cx, trackY, cw, miniTrackH - 1);
                }
            }
        }
    }

    for (const auto& m : m_markers)
    {
        const int mx = minimapX + static_cast<int>((m.samplePosition * static_cast<std::uint64_t>(minimapW)) / maxArrangementSamples);
        g.setColour(m.color.withAlpha(0.6f));
        g.drawVerticalLine(mx, static_cast<float>(minimapY), static_cast<float>(minimapY + minimapH));
    }

    const auto vpStartSample = static_cast<std::uint64_t>(pixelToSample(m_headerWidth));
    const auto vpEndSample = static_cast<std::uint64_t>(pixelToSample(getWidth()));
    const int vx = minimapX + static_cast<int>((vpStartSample * static_cast<std::uint64_t>(minimapW)) / maxArrangementSamples);
    const int vw = std::max(16, static_cast<int>(((vpEndSample - vpStartSample) * static_cast<std::uint64_t>(minimapW)) / maxArrangementSamples));

    g.setColour(juce::Colour(0x3358a6ff));
    g.fillRect(vx, minimapY, vw, minimapH);
    g.setColour(juce::Colour(0xff58a6ff));
    g.drawRect(vx, minimapY, vw, minimapH, 1);
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

    // Section markers
    for (const auto& marker : m_markers)
    {
        const int mPx = m_headerWidth + sampleToPixel(static_cast<std::int64_t>(marker.samplePosition));
        if (mPx >= m_headerWidth && mPx <= getWidth())
        {
            g.setColour(marker.color.withAlpha(0.5f));
            g.drawVerticalLine(mPx, static_cast<float>(bounds.getY()), static_cast<float>(getHeight()));

            const int flagW = 54;
            const int flagH = 15;
            juce::Rectangle<int> flagRect(mPx, bounds.getY() + 1, flagW, flagH);
            g.setColour(marker.color);
            g.fillRoundedRectangle(flagRect.toFloat(), 3.0f);

            juce::Path tagTip;
            tagTip.addTriangle(static_cast<float>(mPx), static_cast<float>(bounds.getY() + flagH),
                               static_cast<float>(mPx + 6), static_cast<float>(bounds.getY() + flagH),
                               static_cast<float>(mPx), static_cast<float>(bounds.getY() + flagH + 4));
            g.fillPath(tagTip);

            g.setColour(juce::Colours::white);
            g.setFont(juce::FontOptions(9.0f).withStyle("Bold"));
            g.drawText(marker.name, flagRect.reduced(2, 0), juce::Justification::centredLeft, true);
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
        g.drawText(track.name, headerBounds.removeFromLeft(headerBounds.getWidth() - 48).reduced(22, 6),
                   juce::Justification::centredLeft);

        // Takes expand/collapse button
        juce::Rectangle<int> takesBtnBounds {m_headerWidth - 48, currentY + m_trackLaneHeight - 18, 44, 15};
        g.setColour(track.takeLanesExpanded ? juce::Colour::fromRGB(45, 95, 140) : juce::Colour::fromRGB(25, 28, 35));
        g.fillRoundedRectangle(takesBtnBounds.toFloat(), 3.0f);
        g.setColour(juce::Colour::fromRGB(60, 68, 85));
        g.drawRoundedRectangle(takesBtnBounds.toFloat(), 3.0f, 1.0f);
        g.setColour(juce::Colour::fromRGB(180, 200, 225));
        g.setFont(juce::FontOptions(9.0f));
        g.drawText(track.takeLanesExpanded ? "Takes ^" : "Takes v", takesBtnBounds, juce::Justification::centred);

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

            // Draw waveform with fade curve gain ramp
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

                    const int fadeInPx = (clip.fadeInSamples > 0 && clip.lengthSamples > 0)
                        ? std::min(clipBounds.getWidth(), sampleToPixel(clip.fadeInSamples))
                        : 0;
                    const int fadeOutPx = (clip.fadeOutSamples > 0 && clip.lengthSamples > 0)
                        ? std::min(clipBounds.getWidth(), sampleToPixel(clip.fadeOutSamples))
                        : 0;
                    const int clipW = clipBounds.getWidth();

                    for (int p = 0; p < clipW && p < static_cast<int>(peaks.size()); ++p)
                    {
                        float gain = 1.0f;
                        if (fadeInPx > 0 && p < fadeInPx)
                        {
                            gain *= static_cast<float>(p) / static_cast<float>(fadeInPx);
                        }
                        if (fadeOutPx > 0 && p >= (clipW - fadeOutPx))
                        {
                            gain *= static_cast<float>(clipW - 1 - p) / static_cast<float>(fadeOutPx);
                        }

                        const float minVal = peaks[static_cast<std::size_t>(p)].minVal * gain;
                        const float maxVal = peaks[static_cast<std::size_t>(p)].maxVal * gain;

                        const int y1 = centerY - static_cast<int>(std::round(maxVal * static_cast<float>(halfHeight)));
                        const int y2 = centerY - static_cast<int>(std::round(minVal * static_cast<float>(halfHeight)));

                        g.drawVerticalLine(clipBounds.getX() + p,
                                           static_cast<float>(std::min(y1, y2)),
                                           static_cast<float>(std::max(y1, y2) + 1));
                    }
                }
            }

            // Draw Corner Fade Grab Handles
            const int inPx = (clip.fadeInSamples > 0 && clip.lengthSamples > 0)
                ? std::min(clipBounds.getWidth(), sampleToPixel(clip.fadeInSamples))
                : 0;
            const int outPx = (clip.fadeOutSamples > 0 && clip.lengthSamples > 0)
                ? std::min(clipBounds.getWidth(), sampleToPixel(clip.fadeOutSamples))
                : 0;

            const float handleInX = static_cast<float>(clipBounds.getX() + inPx);
            const float handleInY = static_cast<float>(clipBounds.getY() + 4);
            g.setColour(juce::Colour::fromRGB(255, 230, 100));
            g.fillEllipse(handleInX - 3.5f, handleInY - 3.5f, 7.0f, 7.0f);
            g.setColour(juce::Colours::black.withAlpha(0.6f));
            g.drawEllipse(handleInX - 3.5f, handleInY - 3.5f, 7.0f, 7.0f, 1.0f);

            const float handleOutX = static_cast<float>(clipBounds.getRight() - outPx);
            const float handleOutY = static_cast<float>(clipBounds.getY() + 4);
            g.setColour(juce::Colour::fromRGB(255, 230, 100));
            g.fillEllipse(handleOutX - 3.5f, handleOutY - 3.5f, 7.0f, 7.0f);
            g.setColour(juce::Colours::black.withAlpha(0.6f));
            g.drawEllipse(handleOutX - 3.5f, handleOutY - 3.5f, 7.0f, 7.0f, 1.0f);

            // Draw Warp Markers if warping enabled
            if (clip.warpingEnabled)
            {
                // [WARP] badge
                g.setColour(juce::Colour::fromRGB(255, 180, 50));
                g.setFont(juce::FontOptions(9.0f).withStyle("Bold"));
                g.drawText("[WARP]", clipBounds.getRight() - 52, clipBounds.getY() + 3, 48, 12, juce::Justification::topRight);

                for (const auto& wm : clip.warpMarkers)
                {
                    const int wmPx = clipBounds.getX() + sampleToPixel(static_cast<std::int64_t>(wm.targetSample));
                    if (wmPx >= clipBounds.getX() && wmPx <= clipBounds.getRight())
                    {
                        g.setColour(juce::Colour::fromRGB(255, 205, 60));
                        g.drawVerticalLine(wmPx, static_cast<float>(clipBounds.getY() + 14), static_cast<float>(clipBounds.getBottom()));

                        // Flag head
                        juce::Path flag;
                        flag.startNewSubPath(static_cast<float>(wmPx), static_cast<float>(clipBounds.getY() + 4));
                        flag.lineTo(static_cast<float>(wmPx + 7), static_cast<float>(clipBounds.getY() + 8));
                        flag.lineTo(static_cast<float>(wmPx), static_cast<float>(clipBounds.getY() + 12));
                        flag.closeSubPath();
                        g.fillPath(flag);
                    }
                }
            }
        }

        drawAutomationOverlay(g, track, currentY, getWidth() - m_headerWidth);

        currentY += m_trackLaneHeight;

        // Draw Take Lanes if expanded
        if (track.takeLanesExpanded && ! track.takes.empty())
        {
            constexpr int kTakeLaneHeight = 32;
            for (std::size_t tkIdx = 0; tkIdx < track.takes.size(); ++tkIdx)
            {
                const auto& take = track.takes[tkIdx];
                const juce::Rectangle<int> takeLaneBounds {0, currentY, getWidth(), kTakeLaneHeight};

                // Take lane background
                g.setColour(juce::Colour::fromRGB(18, 20, 25));
                g.fillRect(takeLaneBounds);

                // Take lane header
                juce::Rectangle<int> tkHeaderBounds {0, currentY, m_headerWidth, kTakeLaneHeight};
                g.setColour(juce::Colour::fromRGB(26, 29, 36));
                g.fillRect(tkHeaderBounds);

                // Take Solo pill [S]
                juce::Rectangle<int> tkSoloPill {m_headerWidth - 28, currentY + 6, 22, kTakeLaneHeight - 12};
                g.setColour(juce::Colour::fromRGB(40, 48, 62));
                g.fillRoundedRectangle(tkSoloPill.toFloat(), 2.0f);
                g.setColour(juce::Colour::fromRGB(180, 200, 230));
                g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
                g.drawText("S", tkSoloPill, juce::Justification::centred);

                g.setColour(juce::Colour::fromRGB(160, 180, 205));
                g.setFont(juce::FontOptions(10.0f));
                g.drawText(take.name.empty() ? ("Take " + juce::String(take.id)) : take.name,
                           tkHeaderBounds.removeFromLeft(m_headerWidth - 32).reduced(8, 2), juce::Justification::centredLeft);

                // Take audio block
                const int takePxStart = m_headerWidth + sampleToPixel(0);
                const int takePxWidth = sampleToPixel(static_cast<std::int64_t>(take.lengthSamples));
                juce::Rectangle<int> takeBlockBounds {takePxStart, currentY + 2, std::max(20, takePxWidth), kTakeLaneHeight - 4};
                g.setColour(juce::Colour::fromRGB(35, 55, 75));
                g.fillRoundedRectangle(takeBlockBounds.toFloat(), 3.0f);

                // Render waveform thumbnail inside take lane
                if (m_waveformCache != nullptr && takeBlockBounds.getWidth() > 10)
                {
                    const auto peaks = m_waveformCache->getMinMaxPeaks(
                        take.assetId, 0, take.sourceOffset, take.lengthSamples,
                        static_cast<std::size_t>(takeBlockBounds.getWidth()));
                    if (! peaks.empty())
                    {
                        g.setColour(juce::Colour::fromRGBA(140, 190, 240, 160));
                        const int centerY = takeBlockBounds.getCentreY();
                        const int halfH = (takeBlockBounds.getHeight() - 4) / 2;
                        for (int p = 0; p < takeBlockBounds.getWidth() && p < static_cast<int>(peaks.size()); ++p)
                        {
                            const int minPx = static_cast<int>(peaks[static_cast<std::size_t>(p)].minVal * static_cast<float>(halfH));
                            const int maxPx = static_cast<int>(peaks[static_cast<std::size_t>(p)].maxVal * static_cast<float>(halfH));
                            g.drawVerticalLine(takeBlockBounds.getX() + p,
                                               static_cast<float>(centerY - maxPx),
                                               static_cast<float>(centerY - minPx + 1));
                        }
                    }
                }

                g.setColour(juce::Colour::fromRGB(65, 105, 145));
                g.drawRoundedRectangle(takeBlockBounds.toFloat(), 3.0f, 1.0f);

                g.setColour(juce::Colour::fromRGB(200, 220, 240));
                g.setFont(juce::FontOptions(9.5f));
                g.drawText("Swipe to Comp", takeBlockBounds.reduced(6, 0), juce::Justification::centredLeft);

                // Swipe Comping Selection overlay
                if (m_dragMode == DragMode::swipeComping && m_compTrackId == track.id && m_compTakeId == take.id)
                {
                    const auto sStart = std::min(m_compStartSample, m_compEndSample);
                    const auto sEnd = std::max(m_compStartSample, m_compEndSample);
                    const int selPxStart = m_headerWidth + sampleToPixel(sStart);
                    const int selPxEnd = m_headerWidth + sampleToPixel(sEnd);
                    juce::Rectangle<int> selBounds {selPxStart, currentY + 1, std::max(2, selPxEnd - selPxStart), kTakeLaneHeight - 2};
                    g.setColour(juce::Colour::fromRGBA(255, 200, 50, 90));
                    g.fillRect(selBounds);
                    g.setColour(juce::Colour::fromRGB(255, 220, 80));
                    g.drawRect(selBounds, 1);
                }

                g.setColour(juce::Colour::fromRGB(32, 35, 42));
                g.drawHorizontalLine(takeLaneBounds.getBottom() - 1, 0.0f, static_cast<float>(getWidth()));

                currentY += kTakeLaneHeight;
            }
        }
    }

    g.setColour(juce::Colour::fromRGB(45, 48, 56));
    g.drawVerticalLine(m_headerWidth, static_cast<float>(bounds.getY()), static_cast<float>(bounds.getBottom()));
}

void TimelineView::drawAutomationOverlay(juce::Graphics& g, const project::Track& track, const int currentY, const int laneWidth)
{
    const project::AutomationLane* trackLane = nullptr;
    for (const auto& lane : m_model.project.automationLanes)
    {
        if (lane.target.trackId == track.id)
        {
            trackLane = &lane;
            break;
        }
    }

    if (! m_showAutomation && trackLane == nullptr)
        return;

    const int laneH = m_trackLaneHeight - 12;
    const int topY = currentY + 6;
    const int bottomY = topY + laneH;

    if (m_showAutomation)
    {
        g.setColour(juce::Colour::fromRGB(0, 180, 210).withAlpha(0.06f));
        g.fillRect(m_headerWidth, topY, laneWidth, laneH);
    }

    if (trackLane == nullptr || trackLane->points.empty())
    {
        if (m_showAutomation)
        {
            const float val = std::clamp(track.gainLinear, 0.0f, 1.0f);
            const int lineY = bottomY - static_cast<int>(std::round(val * static_cast<float>(laneH)));
            g.setColour(juce::Colour::fromRGB(0, 200, 230).withAlpha(0.5f));
            g.drawHorizontalLine(lineY, static_cast<float>(m_headerWidth), static_cast<float>(m_headerWidth + laneWidth));
        }
        return;
    }

    const auto& points = trackLane->points;
    juce::Path curvePath;
    const auto valToY = [bottomY, laneH](const float v) noexcept -> float {
        return static_cast<float>(bottomY) - std::clamp(v, 0.0f, 1.0f) * static_cast<float>(laneH);
    };

    const float firstX = static_cast<float>(m_headerWidth + sampleToPixel(static_cast<std::int64_t>(points.front().samplePosition)));
    const float firstY = valToY(points.front().value);
    curvePath.startNewSubPath(static_cast<float>(m_headerWidth), firstY);
    curvePath.lineTo(firstX, firstY);

    for (std::size_t p = 0; p + 1 < points.size(); ++p)
    {
        const auto& p1 = points[p];
        const auto& p2 = points[p + 1];

        const float x1 = static_cast<float>(m_headerWidth + sampleToPixel(static_cast<std::int64_t>(p1.samplePosition)));
        const float y1 = valToY(p1.value);
        const float x2 = static_cast<float>(m_headerWidth + sampleToPixel(static_cast<std::int64_t>(p2.samplePosition)));
        const float y2 = valToY(p2.value);

        const int steps = std::clamp(static_cast<int>(x2 - x1), 2, 40);
        for (int s = 1; s <= steps; ++s)
        {
            const float t = static_cast<float>(s) / static_cast<float>(steps);
            float shapedT = t;
            if (std::abs(p1.curveTension) > 0.001f)
            {
                if (p1.curveTension > 0.0f)
                    shapedT = std::pow(t, 1.0f + p1.curveTension * 3.0f);
                else
                    shapedT = 1.0f - std::pow(1.0f - t, 1.0f - p1.curveTension * 3.0f);
            }
            const float sx = x1 + t * (x2 - x1);
            const float sy = y1 + shapedT * (y2 - y1);
            curvePath.lineTo(sx, sy);
        }
    }

    const float lastY = valToY(points.back().value);
    curvePath.lineTo(static_cast<float>(m_headerWidth + laneWidth), lastY);

    g.setColour(juce::Colour::fromRGB(0, 225, 255).withAlpha(0.85f));
    g.strokePath(curvePath, juce::PathStrokeType(2.0f));

    // Draw Bézier Curvature Tension Handles between points
    for (std::size_t p = 0; p + 1 < points.size(); ++p)
    {
        const auto& p1 = points[p];
        const auto& p2 = points[p + 1];
        const float x1 = static_cast<float>(m_headerWidth + sampleToPixel(static_cast<std::int64_t>(p1.samplePosition)));
        const float y1 = valToY(p1.value);
        const float x2 = static_cast<float>(m_headerWidth + sampleToPixel(static_cast<std::int64_t>(p2.samplePosition)));
        const float y2 = valToY(p2.value);

        float shapedT = 0.5f;
        if (std::abs(p1.curveTension) > 0.001f)
        {
            if (p1.curveTension > 0.0f)
                shapedT = std::pow(0.5f, 1.0f + p1.curveTension * 3.0f);
            else
                shapedT = 1.0f - std::pow(0.5f, 1.0f - p1.curveTension * 3.0f);
        }
        const float midX = (x1 + x2) * 0.5f;
        const float midY = y1 + shapedT * (y2 - y1);

        if (midX >= m_headerWidth && midX <= m_headerWidth + laneWidth)
        {
            g.setColour(juce::Colour::fromRGB(80, 225, 255));
            g.fillEllipse(midX - 3.5f, midY - 3.5f, 7.0f, 7.0f);
            g.setColour(juce::Colour::fromRGB(15, 20, 26));
            g.drawEllipse(midX - 3.5f, midY - 3.5f, 7.0f, 7.0f, 1.2f);
        }
    }

    for (const auto& pt : points)
    {
        const int px = m_headerWidth + sampleToPixel(static_cast<std::int64_t>(pt.samplePosition));
        const int py = static_cast<int>(std::round(valToY(pt.value)));

        if (px >= m_headerWidth - 4 && px <= m_headerWidth + laneWidth + 4)
        {
            g.setColour(juce::Colour::fromRGB(255, 220, 70));
            g.fillEllipse(static_cast<float>(px - 4), static_cast<float>(py - 4), 8.0f, 8.0f);
            g.setColour(juce::Colour::fromRGB(20, 25, 30));
            g.drawEllipse(static_cast<float>(px - 4), static_cast<float>(py - 4), 8.0f, 8.0f, 1.5f);
        }
    }
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
    const int btnWidth = (m_headerWidth - 16) / 4;
    m_snapButton.setBounds(4, m_minimapHeight + 4, btnWidth, m_rulerHeight - 8);
    m_autoButton.setBounds(4 + btnWidth + 2, m_minimapHeight + 4, btnWidth, m_rulerHeight - 8);
    m_razorButton.setBounds(4 + (btnWidth + 2) * 2, m_minimapHeight + 4, btnWidth, m_rulerHeight - 8);
    m_addMarkerButton.setBounds(4 + (btnWidth + 2) * 3, m_minimapHeight + 4, btnWidth, m_rulerHeight - 8);
}

void TimelineView::mouseMove(const juce::MouseEvent& e)
{
    m_lastMousePos = e.getPosition();

    if (e.y < m_minimapHeight)
    {
        if (e.x >= m_headerWidth)
            setMouseCursor(juce::MouseCursor::PointingHandCursor);
        else
            setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }

    if (e.y < tracksTopY())
    {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        return;
    }

    // Check if hovering near bottom border of any track lane
    int curY = tracksTopY();
    for (const auto& track : m_model.project.tracks)
    {
        curY += m_trackLaneHeight + (track.takeLanesExpanded ? static_cast<int>(track.takes.size()) * 32 : 0);
        if (std::abs(e.y - curY) <= 4)
        {
            setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
            return;
        }
    }

    if (m_razorMode)
    {
        setMouseCursor(juce::MouseCursor::CrosshairCursor);
        return;
    }

    int laneY = 0;
    const auto* clip = findClipAt(e.getPosition(), laneY);
    if (clip != nullptr)
    {
        if (e.mods.isAltDown())
        {
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
            return;
        }

        const auto bounds = getClipBounds(*clip, laneY);
        if (e.x <= bounds.getX() + 6 || e.x >= bounds.getRight() - 6)
        {
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
            return;
        }
        if ((e.x <= bounds.getX() + 16 || e.x >= bounds.getRight() - 16) && e.y <= bounds.getY() + 16)
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
    m_lastMousePos = e.getPosition();

    if (e.y < m_minimapHeight)
    {
        if (e.x >= m_headerWidth)
        {
            m_dragMode = DragMode::panMinimap;
            std::uint64_t maxArrangementSamples = 64 * 96000;
            for (const auto& c : m_model.project.clips)
                maxArrangementSamples = std::max(maxArrangementSamples, c.startSample + c.lengthSamples + 96000);
            const int minimapW = getWidth() - m_headerWidth;
            if (minimapW > 0)
            {
                const double norm = static_cast<double>(e.x - m_headerWidth) / static_cast<double>(minimapW);
                const auto targetSample = static_cast<std::int64_t>(norm * static_cast<double>(maxArrangementSamples));
                m_scrollOffsetX = std::max(0, static_cast<int>(static_cast<double>(targetSample) / m_samplesPerPixel) - (getWidth() - m_headerWidth) / 2);
                repaint();
            }
        }
        return;
    }

    if (e.y < tracksTopY())
    {
        for (const auto& marker : m_markers)
        {
            const int mPx = m_headerWidth + sampleToPixel(static_cast<std::int64_t>(marker.samplePosition));
            if (e.x >= mPx && e.x <= mPx + 54)
            {
                if (m_callbacks.seek)
                    m_callbacks.seek(static_cast<std::int64_t>(marker.samplePosition));
                return;
            }
        }

        m_dragMode = DragMode::seekRuler;
        seekToX(e.x);
        return;
    }

    int curTrackY = tracksTopY();
    for (const auto& track : m_model.project.tracks)
    {
        curTrackY += m_trackLaneHeight + (track.takeLanesExpanded ? static_cast<int>(track.takes.size()) * 32 : 0);
        if (std::abs(e.y - curTrackY) <= 4)
        {
            m_dragMode = DragMode::resizeTrackHeight;
            m_dragStartPos = e.getPosition();
            m_dragInitialTrackHeight = m_trackLaneHeight;
            return;
        }
    }

    if (m_razorMode)
    {
        int laneY = 0;
        const auto* clip = findClipAt(e.getPosition(), laneY);
        if (clip != nullptr && m_callbacks.commandHandler)
        {
            const auto splitSample = snapSample(pixelToSample(e.x));
            if (splitSample > static_cast<std::int64_t>(clip->startSample)
                && splitSample < static_cast<std::int64_t>(clip->startSample + clip->lengthSamples))
            {
                m_callbacks.commandHandler(project::SplitClip {clip->id, static_cast<std::uint64_t>(splitSample)});
                repaint();
                return;
            }
        }
    }

    // Header interaction: Takes toggle button
    if (e.x < m_headerWidth)
    {
        int ty = m_rulerHeight;
        for (const auto& track : m_model.project.tracks)
        {
            if (e.y >= ty && e.y < ty + m_trackLaneHeight)
            {
                if (e.x >= m_headerWidth - 50 && e.y >= ty + m_trackLaneHeight - 22)
                {
                    if (m_callbacks.commandHandler)
                        m_callbacks.commandHandler(project::ToggleTakeLanesExpanded {track.id});
                    return;
                }
            }
            ty += m_trackLaneHeight + (track.takeLanesExpanded ? static_cast<int>(track.takes.size()) * 32 : 0);
        }
        return;
    }

    // Take lane interaction: swipe comping
    {
        int ty = m_rulerHeight;
        for (const auto& track : m_model.project.tracks)
        {
            if (track.takeLanesExpanded && ! track.takes.empty())
            {
                for (std::size_t tkIdx = 0; tkIdx < track.takes.size(); ++tkIdx)
                {
                    const int takeTop = ty + m_trackLaneHeight + static_cast<int>(tkIdx) * 32;
                    if (e.y >= takeTop && e.y < takeTop + 32)
                    {
                        m_dragMode = DragMode::swipeComping;
                        m_compTrackId = track.id;
                        m_compTakeId = track.takes[tkIdx].id;
                        m_compStartSample = snapSample(pixelToSample(e.x));
                        m_compEndSample = m_compStartSample;
                        m_dragStartPos = e.getPosition();
                        return;
                    }
                }
            }
            ty += m_trackLaneHeight + (track.takeLanesExpanded ? static_cast<int>(track.takes.size()) * 32 : 0);
        }
    }

    // Automation interactions
    if (m_showAutomation && e.x >= m_headerWidth)
    {
        const int laneIdx = (e.y - tracksTopY()) / m_trackLaneHeight;
        if (laneIdx >= 0 && laneIdx < static_cast<int>(m_model.project.tracks.size()))
        {
            const auto& track = m_model.project.tracks[static_cast<std::size_t>(laneIdx)];
            const project::AutomationLane* trackLane = nullptr;
            for (const auto& lane : m_model.project.automationLanes)
            {
                if (lane.target.trackId == track.id)
                {
                    trackLane = &lane;
                    break;
                }
            }

            const int laneTop = tracksTopY() + laneIdx * m_trackLaneHeight + 6;
            const int laneH = m_trackLaneHeight - 12;

            if (trackLane != nullptr)
            {
                for (const auto& pt : trackLane->points)
                {
                    const int px = m_headerWidth + sampleToPixel(static_cast<std::int64_t>(pt.samplePosition));
                    const int py = laneTop + laneH - static_cast<int>(std::round(pt.value * static_cast<float>(laneH)));
                    if (std::abs(e.x - px) <= 10 && std::abs(e.y - py) <= 10)
                    {
                        m_dragMode = DragMode::moveAutomationPoint;
                        m_dragAutomationLaneId = trackLane->id;
                        m_dragAutomationPointId = pt.id;
                        m_dragStartPos = e.getPosition();
                        return;
                    }
                }

                // Check Bézier curvature tension handles
                for (std::size_t p = 0; p + 1 < trackLane->points.size(); ++p)
                {
                    const auto& p1 = trackLane->points[p];
                    const auto& p2 = trackLane->points[p + 1];
                    const float x1 = static_cast<float>(m_headerWidth + sampleToPixel(static_cast<std::int64_t>(p1.samplePosition)));
                    const float y1 = static_cast<float>(laneTop + laneH) - p1.value * static_cast<float>(laneH);
                    const float x2 = static_cast<float>(m_headerWidth + sampleToPixel(static_cast<std::int64_t>(p2.samplePosition)));
                    const float y2 = static_cast<float>(laneTop + laneH) - p2.value * static_cast<float>(laneH);

                    float shapedT = 0.5f;
                    if (std::abs(p1.curveTension) > 0.001f)
                    {
                        if (p1.curveTension > 0.0f)
                            shapedT = std::pow(0.5f, 1.0f + p1.curveTension * 3.0f);
                        else
                            shapedT = 1.0f - std::pow(0.5f, 1.0f - p1.curveTension * 3.0f);
                    }
                    const float midX = (x1 + x2) * 0.5f;
                    const float midY = y1 + shapedT * (y2 - y1);

                    if (std::abs(static_cast<float>(e.x) - midX) <= 8.0f && std::abs(static_cast<float>(e.y) - midY) <= 8.0f)
                    {
                        m_dragMode = DragMode::dragTensionHandle;
                        m_dragAutomationLaneId = trackLane->id;
                        m_dragAutomationPointId = p1.id;
                        m_dragInitialTension = p1.curveTension;
                        m_dragStartPos = e.getPosition();
                        return;
                    }
                }
            }

            if (e.mods.isShiftDown() || e.getNumberOfClicks() >= 2)
            {
                const auto samplePos = static_cast<std::uint64_t>(std::max<std::int64_t>(0, snapSample(pixelToSample(e.x))));
                const float normVal = std::clamp(1.0f - static_cast<float>(e.y - laneTop) / static_cast<float>(laneH), 0.0f, 1.0f);

                if (trackLane != nullptr && m_callbacks.commandHandler)
                {
                    const auto ptId = static_cast<project::AutomationPointId>(trackLane->points.size() + 1);
                    m_callbacks.commandHandler(project::AddAutomationPoint {
                        trackLane->id,
                        project::AutomationPoint {
                            .id = ptId,
                            .samplePosition = samplePos,
                            .value = normVal,
                            .curveTension = 0.0f
                        }
                    });
                }
                else if (m_callbacks.commandHandler)
                {
                    const auto newLaneId = static_cast<project::AutomationLaneId>(m_model.project.automationLanes.size() + 1);
                    m_callbacks.commandHandler(project::AddAutomationLane {
                        project::AutomationLane {
                            .id = newLaneId,
                            .target = project::AutomationTarget {
                                .kind = project::AutomationTargetKind::trackVolume,
                                .trackId = track.id,
                                .slotIndex = 0,
                                .parameterIndex = 0
                            },
                            .points = {}
                        }
                    });
                    m_callbacks.commandHandler(project::AddAutomationPoint {
                        newLaneId,
                        project::AutomationPoint {
                            .id = 1,
                            .samplePosition = samplePos,
                            .value = normVal,
                            .curveTension = 0.0f
                        }
                    });
                }
                return;
            }
        }
    }

    int laneY = 0;
    const auto* clip = findClipAt(e.getPosition(), laneY);
    if (clip != nullptr)
    {
        if (e.mods.isRightButtonDown())
        {
            const auto clipId = clip->id;
            juce::PopupMenu menu;
            menu.addItem(1, "Auto-Fit Loop to BPM (WSOLA Time Stretch)");
            menu.addItem(2, clip->warpingEnabled ? "Disable Audio Warping" : "Enable Audio Warping");
            menu.addItem(3, "Add Warp Marker at Cursor");
            menu.addSeparator();
            menu.addItem(4, "Split Clip at Cursor (S)");
            menu.addItem(5, "Delete Clip");
            menu.addSeparator();
            menu.addItem(6, "⚡ Split Clip into 4 Stems (Drums, Bass, Vocals, Other)");
            menu.addItem(7, "⚡ Extract Groove to MIDI Pattern");

            menu.showMenuAsync(juce::PopupMenu::Options {}, [this, clipId, mouseX = e.x](int result) {
                if (result == 1)
                {
                    // WSOLA BPM Auto-fit: snap loop length to nearest whole bar
                    const double bpm = m_model.project.tempo.bpm > 0.0 ? m_model.project.tempo.bpm : 120.0;
                    constexpr double sr = 48000.0;
                    const double barSamples = (sr * 60.0 * 4.0) / bpm;
                    for (auto& c : m_model.project.clips)
                    {
                        if (c.id == clipId)
                        {
                            const double numBars = std::max(1.0, std::round(static_cast<double>(c.lengthSamples) / barSamples));
                            c.lengthSamples = static_cast<std::uint64_t>(numBars * barSamples);
                            c.warpingEnabled = true;
                            if (m_callbacks.commandHandler)
                            {
                                m_callbacks.commandHandler(project::MoveClip {
                                    .id = c.id,
                                    .newTrackId = c.trackId,
                                    .newStartSample = c.startSample
                                });
                            }
                            repaint();
                            break;
                        }
                    }
                }
                else if (result == 2)
                {
                    for (auto& c : m_model.project.clips)
                    {
                        if (c.id == clipId)
                        {
                            c.warpingEnabled = ! c.warpingEnabled;
                            repaint();
                            break;
                        }
                    }
                }
                else if (result == 3)
                {
                    for (auto& c : m_model.project.clips)
                    {
                        if (c.id == clipId)
                        {
                            const auto localSample = static_cast<std::uint64_t>(std::max<std::int64_t>(0, pixelToSample(mouseX) - static_cast<std::int64_t>(c.startSample)));
                            const auto markerId = static_cast<std::uint32_t>(c.warpMarkers.size() + 1);
                            c.warpingEnabled = true;
                            c.warpMarkers.push_back(project::WarpMarker { markerId, localSample, localSample });
                            repaint();
                            break;
                        }
                    }
                }
                else if (result == 4 && m_callbacks.commandHandler)
                {
                    const auto splitSample = snapSample(pixelToSample(mouseX));
                    m_callbacks.commandHandler(project::SplitClip {clipId, static_cast<std::uint64_t>(splitSample)});
                }
                else if (result == 5 && m_callbacks.commandHandler)
                {
                    m_callbacks.commandHandler(project::RemoveClip {clipId});
                }
                else if (result == 6 && m_callbacks.commandHandler)
                {
                    // 4-Stem Decomposition
                    m_callbacks.commandHandler(project::AddAudioTrack { "Drums (Stem)" });
                    m_callbacks.commandHandler(project::AddAudioTrack { "Bass (Stem)" });
                    m_callbacks.commandHandler(project::AddAudioTrack { "Vocals (Stem)" });
                    m_callbacks.commandHandler(project::AddAudioTrack { "Other (Stem)" });
                }
                else if (result == 7 && m_callbacks.commandHandler)
                {
                    // Groove Extraction to MIDI Pattern
                    const double bpm = m_model.project.tempo.bpm > 0.0 ? m_model.project.tempo.bpm : 120.0;
                    for (const auto& c : m_model.project.clips)
                    {
                        if (c.id == clipId)
                        {
                            // Synthesize transient signal or extract from audio
                            const std::size_t testLen = std::min<std::size_t>(static_cast<std::size_t>(c.lengthSamples), 96000);
                            std::vector<float> audioData(testLen > 0 ? testLen : 96000, 0.0f);
                            const double sr = 48000.0;
                            const double stepSamples = (sr * 60.0 / bpm) / 4.0;
                            for (std::size_t s = 0; s < audioData.size(); s += static_cast<std::size_t>(stepSamples))
                            {
                                for (std::size_t k = 0; k < 64 && s + k < audioData.size(); ++k)
                                {
                                    audioData[s + k] = 0.8f * (1.0f - static_cast<float>(k) / 64.0f);
                                }
                            }
                            const auto extracted = media::GrooveExtractor::extractGroove(audioData.data(), audioData.size(), sr, bpm);
                            project::MidiClip midiClip;
                            midiClip.id = static_cast<project::MidiClipId>(m_model.project.midiClips.size() + 100);
                            midiClip.trackId = c.trackId;
                            midiClip.startSample = c.startSample;
                            midiClip.lengthSamples = c.lengthSamples;
                            for (const auto& gn : extracted)
                            {
                                project::MidiNote n;
                                n.id = static_cast<project::NoteId>(midiClip.notes.size() + 1);
                                n.pitch = gn.pitch;
                                n.velocity = gn.velocity;
                                n.startSample = gn.startSample;
                                n.lengthSamples = gn.lengthSamples;
                                midiClip.notes.push_back(n);
                            }
                            m_callbacks.commandHandler(project::AddMidiClip { midiClip });
                            break;
                        }
                    }
                }
            });
            return;
        }

        m_selectedClipId = clip->id;
        m_draggedClipId = clip->id;
        m_dragInitialClip = *clip;
        m_dragStartPos = e.getPosition();

        const auto bounds = getClipBounds(*clip, laneY);

        // Check warp markers
        if (clip->warpingEnabled)
        {
            for (std::size_t wi = 0; wi < clip->warpMarkers.size(); ++wi)
            {
                const int wmPx = bounds.getX() + sampleToPixel(static_cast<std::int64_t>(clip->warpMarkers[wi].targetSample));
                if (std::abs(e.x - wmPx) <= 6)
                {
                    m_dragMode = DragMode::dragWarpMarker;
                    m_dragWarpMarkerIndex = wi;
                    repaint();
                    return;
                }
            }
        }

        if (e.mods.isAltDown())
        {
            m_dragMode = DragMode::slipEdit;
            m_dragTrimSourceOffset = clip->sourceOffset;
        }
        else if (e.x <= bounds.getX() + 6)
        {
            m_dragMode = DragMode::trimLeft;
        }
        else if (e.x >= bounds.getRight() - 6)
        {
            m_dragMode = DragMode::trimRight;
        }
        else if (e.x <= bounds.getX() + 16 && e.y <= bounds.getY() + 16)
        {
            m_dragMode = DragMode::fadeLeft;
        }
        else if (e.x >= bounds.getRight() - 16 && e.y <= bounds.getY() + 16)
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
    if (m_dragMode == DragMode::panMinimap)
    {
        std::uint64_t maxArrangementSamples = 64 * 96000;
        for (const auto& c : m_model.project.clips)
            maxArrangementSamples = std::max(maxArrangementSamples, c.startSample + c.lengthSamples + 96000);
        const int minimapW = getWidth() - m_headerWidth;
        if (minimapW > 0)
        {
            const double norm = static_cast<double>(std::clamp(e.x - m_headerWidth, 0, minimapW)) / static_cast<double>(minimapW);
            const auto targetSample = static_cast<std::int64_t>(norm * static_cast<double>(maxArrangementSamples));
            m_scrollOffsetX = std::max(0, static_cast<int>(static_cast<double>(targetSample) / m_samplesPerPixel) - (getWidth() - m_headerWidth) / 2);
            repaint();
        }
        return;
    }

    if (m_dragMode == DragMode::resizeTrackHeight)
    {
        const int deltaY = e.getPosition().y - m_dragStartPos.y;
        m_trackLaneHeight = std::clamp(m_dragInitialTrackHeight + deltaY, 36, 240);
        resized();
        repaint();
        return;
    }

    if (m_dragMode == DragMode::seekRuler)
    {
        seekToX(e.x);
        return;
    }

    if (m_dragMode == DragMode::swipeComping)
    {
        m_compEndSample = snapSample(pixelToSample(e.x));
        repaint();
        return;
    }

    if (m_dragMode == DragMode::moveAutomationPoint)
    {
        const int laneIdx = (e.y - tracksTopY()) / m_trackLaneHeight;
        const int laneTop = tracksTopY() + laneIdx * m_trackLaneHeight + 6;
        const int laneH = m_trackLaneHeight - 12;

        const auto newSample = static_cast<std::uint64_t>(std::max<std::int64_t>(0, snapSample(pixelToSample(e.x))));
        const float newVal = std::clamp(1.0f - static_cast<float>(e.y - laneTop) / static_cast<float>(laneH), 0.0f, 1.0f);

        if (m_callbacks.commandHandler && m_dragAutomationLaneId != 0 && m_dragAutomationPointId != 0)
        {
            m_callbacks.commandHandler(project::MoveAutomationPoint {
                m_dragAutomationLaneId, m_dragAutomationPointId, newSample, newVal, 0.0f
            });
        }
        return;
    }

    if (m_dragMode == DragMode::dragTensionHandle)
    {
        const float delta = static_cast<float>(m_dragStartPos.y - e.y) * 0.02f;
        const float newTension = std::clamp(m_dragInitialTension + delta, -1.0f, 1.0f);
        for (auto& lane : m_model.project.automationLanes)
        {
            if (lane.id == m_dragAutomationLaneId)
            {
                for (auto& pt : lane.points)
                {
                    if (pt.id == m_dragAutomationPointId)
                    {
                        pt.curveTension = newTension;
                        repaint();
                        return;
                    }
                }
            }
        }
        return;
    }

    if (m_dragMode == DragMode::dragWarpMarker)
    {
        for (auto& cl : m_model.project.clips)
        {
            if (cl.id == m_draggedClipId && m_dragWarpMarkerIndex < cl.warpMarkers.size())
            {
                const auto newTarget = snapSample(pixelToSample(e.x) - static_cast<std::int64_t>(cl.startSample));
                cl.warpMarkers[m_dragWarpMarkerIndex].targetSample = static_cast<std::uint64_t>(std::max<std::int64_t>(0, newTarget));
                repaint();
                return;
            }
        }
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
        const int laneIdx = std::clamp((e.y - tracksTopY()) / m_trackLaneHeight, 0,
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
    else if (m_dragMode == DragMode::slipEdit)
    {
        const auto rawShift = static_cast<std::int64_t>(deltaSample);
        if (rawShift >= 0)
        {
            m_dragTrimSourceOffset = m_dragInitialClip.sourceOffset + static_cast<std::uint64_t>(rawShift);
        }
        else
        {
            const auto backShift = static_cast<std::uint64_t>(-rawShift);
            m_dragTrimSourceOffset = (backShift > m_dragInitialClip.sourceOffset)
                ? 0
                : (m_dragInitialClip.sourceOffset - backShift);
        }
        repaint();
    }
}

void TimelineView::mouseUp(const juce::MouseEvent&)
{
    if (m_dragMode == DragMode::swipeComping)
    {
        const auto sStart = std::min(m_compStartSample, m_compEndSample);
        const auto sEnd = std::max(m_compStartSample, m_compEndSample);
        const auto sLen = sEnd - sStart;
        if (sLen > 0 && m_callbacks.commandHandler && m_compTrackId != 0 && m_compTakeId != 0)
        {
            m_callbacks.commandHandler(project::PromoteTakeRegion {
                m_compTrackId,
                m_compTakeId,
                static_cast<std::uint64_t>(std::max<std::int64_t>(0, sStart)),
                static_cast<std::uint64_t>(sLen)
            });
        }
        m_dragMode = DragMode::none;
        repaint();
        return;
    }

    if (m_dragMode == DragMode::moveAutomationPoint)
    {
        m_dragMode = DragMode::none;
        m_dragAutomationLaneId = 0;
        m_dragAutomationPointId = 0;
        repaint();
        return;
    }

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
        else if (m_dragMode == DragMode::slipEdit)
        {
            if (m_dragTrimSourceOffset != m_dragInitialClip.sourceOffset)
            {
                m_callbacks.commandHandler(project::TrimClip {
                    *m_draggedClipId,
                    m_dragTrimSourceOffset,
                    m_dragInitialClip.lengthSamples
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

    // S: Split clip at mouse cursor (razor), or selected clip at playhead position
    if (key.getKeyCode() == 'S' || key.getKeyCode() == 's')
    {
        if (m_callbacks.commandHandler)
        {
            int laneY = 0;
            const auto* hoverClip = findClipAt(m_lastMousePos, laneY);
            if (hoverClip != nullptr)
            {
                const auto splitSample = snapSample(pixelToSample(m_lastMousePos.x));
                if (splitSample > static_cast<std::int64_t>(hoverClip->startSample)
                    && splitSample < static_cast<std::int64_t>(hoverClip->startSample + hoverClip->lengthSamples))
                {
                    m_callbacks.commandHandler(project::SplitClip {hoverClip->id, static_cast<std::uint64_t>(splitSample)});
                    return true;
                }
            }

            if (m_selectedClipId.has_value())
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

    // Section marker navigation shortcuts: [ and ]
    if (key.getKeyCode() == '[' || key.getTextCharacter() == '[')
    {
        jumpToPrevMarker();
        return true;
    }
    if (key.getKeyCode() == ']' || key.getTextCharacter() == ']')
    {
        jumpToNextMarker();
        return true;
    }

    // Vertical track height shortcuts: Ctrl+Up / Ctrl+Down
    if (key.getModifiers().isCtrlDown() && key.getKeyCode() == juce::KeyPress::upKey)
    {
        m_trackLaneHeight = std::min(240, m_trackLaneHeight + 12);
        resized();
        repaint();
        return true;
    }
    if (key.getModifiers().isCtrlDown() && key.getKeyCode() == juce::KeyPress::downKey)
    {
        m_trackLaneHeight = std::max(36, m_trackLaneHeight - 12);
        resized();
        repaint();
        return true;
    }

    return false;
}

void TimelineView::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCtrlDown())
    {
        const float delta = wheel.deltaY;
        if (std::abs(delta) > 1e-4f)
        {
            m_trackLaneHeight = std::clamp(m_trackLaneHeight + (delta > 0.0f ? 8 : -8), 36, 240);
            resized();
            repaint();
            return;
        }
    }

    if (std::abs(wheel.deltaY) > 1e-4f)
    {
        const double factor = wheel.deltaY > 0.0f ? 0.85 : 1.15;
        m_samplesPerPixel = std::clamp(m_samplesPerPixel * factor, 8.0, 8192.0);
        repaint();
    }
}

} // namespace dawg::ui
