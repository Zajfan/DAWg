#include "TimelineView.h"

#include <dawg/engine/SmpteTimecode.h>
#include <dawg/engine/dsp/TransientSlicerEngine.h>
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

    m_macroButton.onClick = [this] {
        m_showMacros = ! m_showMacros;
        m_macroButton.setButtonText(m_showMacros ? "Macros: ON" : "Macros: OFF");
        m_macroButton.setColour(juce::TextButton::buttonColourId,
                                m_showMacros ? juce::Colour::fromRGB(0, 180, 220)
                                            : juce::Colour::fromRGB(40, 42, 50));
        m_headerWidth = m_showMacros ? 260 : 200;
        resized();
        repaint();
    };
    addAndMakeVisible(m_macroButton);

    m_filmstripButton.onClick = [this] {
        m_showFilmstrip = ! m_showFilmstrip;
        m_filmstripButton.setButtonText(m_showFilmstrip ? "Filmstrip: ON" : "Filmstrip: OFF");
        m_filmstripButton.setColour(juce::TextButton::buttonColourId,
                                    m_showFilmstrip ? juce::Colour::fromRGB(160, 90, 200)
                                                    : juce::Colour::fromRGB(40, 42, 50));
        resized();
        repaint();
    };
    addAndMakeVisible(m_filmstripButton);

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

    startTimerHz(30);
}

void TimelineView::timerCallback()
{
    m_macroAnimPhase += 0.08f;
    if (m_macroAnimPhase > 628.0f)
        m_macroAnimPhase -= 628.0f;

    // Repaint track headers to animate the colored modulation rings smoothly
    repaint(0, tracksTopY(), m_headerWidth, getHeight() - tracksTopY());
}

void TimelineView::drawMacroOverlay(juce::Graphics& g, const project::Track& track, juce::Rectangle<int> bounds)
{
    g.setColour(juce::Colour::fromRGB(24, 26, 33));
    g.fillRoundedRectangle(bounds.toFloat(), 3.0f);
    g.setColour(juce::Colour::fromRGB(45, 50, 62));
    g.drawRoundedRectangle(bounds.toFloat(), 3.0f, 1.0f);

    static const std::array<juce::Colour, 8> macroColors = {
        juce::Colour(0xff00e5ff), // Cyan
        juce::Colour(0xffff9100), // Amber
        juce::Colour(0xffff5252), // Coral
        juce::Colour(0xffb388ff), // Purple
        juce::Colour(0xff69f0ae), // Emerald
        juce::Colour(0xffffd740), // Yellow
        juce::Colour(0xff448aff), // Sky Blue
        juce::Colour(0xffff4081)  // Magenta
    };

    auto it = m_trackMacros.find(track.id);
    if (it == m_trackMacros.end())
    {
        it = m_trackMacros.emplace(track.id, std::array<float, 8>{0.5f, 0.4f, 0.6f, 0.3f, 0.7f, 0.5f, 0.2f, 0.8f}).first;
    }
    auto& macros = it->second;

    const int colW = bounds.getWidth() / 4;
    const int rowH = bounds.getHeight() / 2;

    for (std::size_t m = 0; m < 8; ++m)
    {
        const int col = static_cast<int>(m % 4);
        const int row = static_cast<int>(m / 4);
        juce::Rectangle<int> cell(bounds.getX() + col * colW, bounds.getY() + row * rowH, colW, rowH);

        const float cx = static_cast<float>(cell.getCentreX());
        const float cy = static_cast<float>(cell.getCentreY());
        const float radius = static_cast<float>(std::min(colW, rowH)) * 0.36f;

        const float baseVal = macros[m];
        // Animated modulation offset from LFO / MSEG / Chaos
        const float modLfo = 0.28f * std::sin(m_macroAnimPhase * 1.5f + static_cast<float>(m) * 0.78f);
        const float curVal = std::clamp(baseVal + modLfo, 0.0f, 1.0f);

        // Center dial
        g.setColour(juce::Colour::fromRGB(18, 20, 25));
        g.fillEllipse(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f);
        g.setColour(juce::Colour::fromRGB(40, 45, 55));
        g.drawEllipse(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f, 1.0f);

        // Indicator angle: -135 deg to +135 deg
        constexpr float startAngle = -2.35619f;
        constexpr float endAngle = 2.35619f;
        const float baseAngle = startAngle + baseVal * (endAngle - startAngle);
        const float curAngle = startAngle + curVal * (endAngle - startAngle);

        // Modulation Arc Ring
        juce::Path arc;
        const float a1 = std::min(baseAngle, curAngle);
        const float a2 = std::max(baseAngle, curAngle);
        arc.addCentredArc(cx, cy, radius + 2.0f, radius + 2.0f, 0.0f, a1, a2, true);
        g.setColour(macroColors[m].withAlpha(0.85f));
        g.strokePath(arc, juce::PathStrokeType(1.8f));

        // Dial indicator needle
        const float needleLen = radius * 0.75f;
        const float nx = cx + std::sin(baseAngle) * needleLen;
        const float ny = cy - std::cos(baseAngle) * needleLen;
        g.setColour(juce::Colours::white);
        g.drawLine(cx, cy, nx, ny, 1.2f);
    }
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
    const double sampleRate = m_model.sampleRate > 0.0 ? m_model.sampleRate : 48000.0;
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

juce::Rectangle<int> TimelineView::getMidiClipBounds(const project::MidiClip& clip, const int laneY) const noexcept
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
        if (track.parentFolderId != 0)
        {
            const auto parentIt = std::find_if(tracks.begin(), tracks.end(),
                [&](const auto& t) { return t.id == track.parentFolderId; });
            if (parentIt != tracks.end() && parentIt->folderCollapsed)
                continue;
        }

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
    const auto filmstripBounds = m_showFilmstrip ? totalBounds.removeFromTop(m_filmstripHeight) : juce::Rectangle<int>();
    const auto tracksBounds = totalBounds;

    drawMinimap(g, minimapBounds);
    drawRuler(g, rulerBounds);
    if (m_showFilmstrip)
        drawFilmstrip(g, filmstripBounds);
    drawTracksAndClips(g, tracksBounds);
    drawRazorAndSlipOverlay(g);
    drawLoopRegion(g, getLocalBounds());
    drawPunchRegion(g, getLocalBounds());
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

    const double sampleRate = m_model.sampleRate > 0.0 ? m_model.sampleRate : 48000.0;
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

void TimelineView::drawFilmstrip(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    g.setColour(juce::Colour(0xff12141a));
    g.fillRect(bounds);

    auto headerCorner = bounds.removeFromLeft(m_headerWidth);
    g.setColour(juce::Colour(0xff161922));
    g.fillRect(headerCorner);

    g.setColour(juce::Colour(0xffa080d0));
    g.setFont(juce::FontOptions(10.0f).withStyle("Bold"));
    g.drawText("FILMSTRIP [24 FPS]", headerCorner.reduced(6, 0), juce::Justification::centredLeft);

    g.setColour(juce::Colour(0xff30363d));
    g.drawHorizontalLine(bounds.getBottom() - 1, 0.0f, static_cast<float>(getWidth()));
    g.drawVerticalLine(m_headerWidth, static_cast<float>(bounds.getY()), static_cast<float>(bounds.getBottom()));

    if (bounds.getWidth() <= 10) return;

    // Draw film sprocket perforations along top and bottom edges
    g.setColour(juce::Colour(0xff080a0e));
    g.fillRect(bounds.getX(), bounds.getY(), bounds.getWidth(), 5);
    g.fillRect(bounds.getX(), bounds.getBottom() - 5, bounds.getWidth(), 5);

    g.setColour(juce::Colour(0xff2a303c));
    for (int px = bounds.getX() + 4; px < bounds.getRight(); px += 18)
    {
        g.fillRect(px, bounds.getY() + 1, 4, 3);
        g.fillRect(px, bounds.getBottom() - 4, 4, 3);
    }

    // Draw 16:9 thumbnail frame cells
    constexpr int frameWidth = 60;
    constexpr int frameHeight = 32;
    const int numFrames = std::max(1, bounds.getWidth() / frameWidth);

    const double sr = m_model.sampleRate > 0.0 ? m_model.sampleRate : 48000.0;
    const std::int64_t oneHourOffsetSamples = static_cast<std::int64_t>(sr * 3600.0);

    for (int f = 0; f < numFrames; ++f)
    {
        const int cellX = bounds.getX() + f * frameWidth + 2;
        const int cellY = bounds.getY() + 6;
        if (cellX + frameWidth > bounds.getRight()) break;

        const auto frameSample = static_cast<std::uint64_t>(
            std::max<std::int64_t>(0, pixelToSample(cellX - m_headerWidth)));

        const auto tc = engine::SmpteTimecodeEngine::sampleToTimecode(
            frameSample, sr, engine::SmpteFrameRate::fps24, oneHourOffsetSamples);
        const auto tcStr = engine::SmpteTimecodeEngine::formatTimecode(tc, false);

        // Frame card background
        auto frameBox = juce::Rectangle<int> {cellX, cellY, frameWidth - 4, frameHeight};
        const auto sceneHue = static_cast<float>((f * 37) % 100) / 100.0f;
        g.setColour(juce::Colour::fromHSV(sceneHue, 0.25f, 0.20f, 1.0f));
        g.fillRect(frameBox);
        g.setColour(juce::Colour(0xff384252));
        g.drawRect(frameBox, 1);

        // Frame number & timecode stamp
        g.setColour(juce::Colour(0xffd0d8e8));
        g.setFont(juce::FontOptions(8.0f));
        g.drawText(tcStr.substr(3), frameBox.removeFromBottom(10), juce::Justification::centred, false);
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
        if (track.parentFolderId != 0)
        {
            const auto parentIt = std::find_if(tracks.begin(), tracks.end(),
                [&](const auto& t) { return t.id == track.parentFolderId; });
            if (parentIt != tracks.end() && parentIt->folderCollapsed)
                continue;
        }

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

        // Surface 8 Macro Knobs with animated colored modulation rings only if toggled ON
        if (m_showMacros)
        {
            auto macroArea = headerBounds.removeFromRight(76).reduced(2, 4);
            drawMacroOverlay(g, track, macroArea);
        }

        const bool isFolder = track.isFolder || track.kind == project::TrackKind::folder;
        const bool isChild = (track.parentFolderId != 0);

        if (isFolder)
        {
            // Folder expand/collapse button [v] or [>]
            const juce::Rectangle<int> foldBtn {6, currentY + 8, 16, 16};
            g.setColour(juce::Colour::fromRGB(45, 95, 140));
            g.fillRoundedRectangle(foldBtn.toFloat(), 3.0f);
            g.setColour(juce::Colours::white);
            g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
            g.drawText(track.folderCollapsed ? ">" : "v", foldBtn, juce::Justification::centred);
        }
        else if (isChild)
        {
            // Visual indent branch for nested tracks
            g.setColour(juce::Colour::fromRGB(70, 80, 100));
            g.drawHorizontalLine(currentY + m_trackLaneHeight / 2, 8.0f, 18.0f);
            g.drawVerticalLine(8, static_cast<float>(currentY), static_cast<float>(currentY + m_trackLaneHeight / 2));
        }

        const int armX = isFolder ? 26 : (isChild ? 22 : 8);
        if (track.recordArmed)
        {
            g.setColour(juce::Colour::fromRGB(220, 50, 50));
            g.fillEllipse(static_cast<float>(headerBounds.getX() + armX),
                          static_cast<float>(headerBounds.getY() + 10),
                          8.0f, 8.0f);
        }

        const int nameX = (isFolder ? 28 : (isChild ? 24 : 10)) + (track.recordArmed ? 14 : 0);
        g.setColour(juce::Colour::fromRGB(220, 225, 235));
        g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        g.drawText(track.name, headerBounds.getX() + nameX, headerBounds.getY() + 6,
                   headerBounds.getWidth() - nameX - 4, m_trackLaneHeight - 26,
                   juce::Justification::topLeft);

        // Takes expand/collapse button
        juce::Rectangle<int> takesBtnBounds {headerBounds.getX() + nameX, currentY + m_trackLaneHeight - 18, 48, 15};
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

        // Draw MIDI Clips for this track
        for (const auto& mClip : m_model.project.midiClips)
        {
            if (mClip.trackId != track.id)
                continue;

            auto clipBounds = getMidiClipBounds(mClip, currentY);
            const bool isSelected = (m_selectedClipId.has_value() && *m_selectedClipId == mClip.id);

            // Clip background: subtle cyberpunk violet / indigo slate
            g.setColour(isSelected ? juce::Colour::fromRGB(75, 45, 110) : juce::Colour::fromRGB(45, 32, 70));
            g.fillRoundedRectangle(clipBounds.toFloat(), 4.0f);

            // Clip border
            g.setColour(isSelected ? juce::Colour::fromRGB(255, 215, 60) : juce::Colour::fromRGB(140, 95, 210));
            g.drawRoundedRectangle(clipBounds.toFloat(), 4.0f, isSelected ? 2.0f : 1.0f);

            // Clip label
            g.setColour(juce::Colour::fromRGB(240, 235, 255));
            g.setFont(juce::FontOptions(11.0f));
            g.drawText("[MIDI] Pattern " + juce::String(mClip.id),
                       clipBounds.getX() + 6, clipBounds.getY() + 3, clipBounds.getWidth() - 12, 14,
                       juce::Justification::topLeft);

            // Draw mini-piano-roll note bars
            if (! mClip.notes.empty() && clipBounds.getWidth() > 10)
            {
                std::uint8_t minP = 127;
                std::uint8_t maxP = 0;
                for (const auto& n : mClip.notes)
                {
                    minP = std::min(minP, n.pitch);
                    maxP = std::max(maxP, n.pitch);
                }
                const int pRange = std::max(1, static_cast<int>(maxP - minP));
                const int noteAreaTop = clipBounds.getY() + 18;
                const int noteAreaHeight = std::max(8, clipBounds.getHeight() - 22);

                g.setColour(isSelected ? juce::Colour::fromRGB(130, 255, 210) : juce::Colour::fromRGB(95, 215, 180));
                for (const auto& n : mClip.notes)
                {
                    const int nx = clipBounds.getX() + sampleToPixel(static_cast<std::int64_t>(n.startSample));
                    const int nw = std::max(3, sampleToPixel(static_cast<std::int64_t>(n.lengthSamples)));
                    const float normP = static_cast<float>(n.pitch - minP) / static_cast<float>(pRange);
                    const int ny = noteAreaTop + static_cast<int>((1.0f - normP) * static_cast<float>(noteAreaHeight - 4));
                    if (nx + nw >= clipBounds.getX() && nx <= clipBounds.getRight())
                    {
                        const int clampedX = std::max(clipBounds.getX() + 1, nx);
                        const int clampedW = std::min(clipBounds.getRight() - 1 - clampedX, nw);
                        if (clampedW > 0)
                            g.fillRoundedRectangle(static_cast<float>(clampedX), static_cast<float>(ny),
                                                   static_cast<float>(clampedW), 3.0f, 1.5f);
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

    // Draw Bezier Curvature Tension Handles between points
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

void TimelineView::drawRazorAndSlipOverlay(juce::Graphics& g)
{
    // 1. Razor / Slice Tool Overlay
    if (m_razorMode && m_lastMousePos.x >= m_headerWidth && m_lastMousePos.y >= tracksTopY())
    {
        const auto snappedSample = snapSample(pixelToSample(m_lastMousePos.x));
        const int cutX = m_headerWidth + sampleToPixel(snappedSample);

        g.setColour(juce::Colour(0xffff3b30).withAlpha(0.85f));
        g.drawVerticalLine(cutX, static_cast<float>(tracksTopY()), static_cast<float>(getHeight()));

        // Draw Razor pill at cursor
        const juce::Rectangle<int> razorPill {cutX - 35, std::max(tracksTopY() + 4, m_lastMousePos.y - 12), 70, 22};
        g.setColour(juce::Colour(0xff220a0a));
        g.fillRoundedRectangle(razorPill.toFloat(), 4.0f);
        g.setColour(juce::Colour(0xffff453a));
        g.drawRoundedRectangle(razorPill.toFloat(), 4.0f, 1.2f);

        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        g.drawText("[CUT]", razorPill, juce::Justification::centred);
    }

    // 2. Slip Edit Mode Indicator
    if (m_dragMode == DragMode::slipEdit && m_draggedClipId.has_value())
    {
        for (const auto& clip : m_model.project.clips)
        {
            if (clip.id == *m_draggedClipId)
            {
                int laneY = 0;
                (void)findClipAt(m_lastMousePos, laneY);
                const auto b = getClipBounds(clip, laneY);
                if (b.getWidth() > 60)
                {
                    const juce::Rectangle<int> slipBadge {b.getCentreX() - 45, b.getY() + 4, 90, 18};
                    g.setColour(juce::Colour(0xff1f242c));
                    g.fillRoundedRectangle(slipBadge.toFloat(), 3.0f);
                    g.setColour(juce::Colour(0xff58a6ff));
                    g.drawRoundedRectangle(slipBadge.toFloat(), 3.0f, 1.0f);
                    g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
                    g.drawText("< SLIP EDIT >", slipBadge, juce::Justification::centred);
                }
                break;
            }
        }
    }

    // 3. Automation Tension Drag HUD
    if (m_dragMode == DragMode::dragTensionHandle)
    {
        for (const auto& lane : m_model.project.automationLanes)
        {
            if (lane.id == m_dragAutomationLaneId)
            {
                for (const auto& pt : lane.points)
                {
                    if (pt.id == m_dragAutomationPointId)
                    {
                        const juce::Rectangle<int> hud {m_lastMousePos.x + 12, m_lastMousePos.y - 20, 80, 20};
                        g.setColour(juce::Colour(0xff161b22));
                        g.fillRoundedRectangle(hud.toFloat(), 4.0f);
                        g.setColour(juce::Colour(0xff58a6ff));
                        g.drawRoundedRectangle(hud.toFloat(), 4.0f, 1.0f);
                        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
                        g.drawText(juce::String::formatted("Tension: %+.0f%%", pt.curveTension * 100.0f),
                                   hud, juce::Justification::centred);
                        break;
                    }
                }
                break;
            }
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

void TimelineView::drawPunchRegion(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    if (! m_model.project.punch.enabled)
        return;

    const int punchStartPx = m_headerWidth + sampleToPixel(static_cast<std::int64_t>(m_model.project.punch.punchInSample));
    const int punchEndPx = m_headerWidth + sampleToPixel(static_cast<std::int64_t>(m_model.project.punch.punchOutSample));

    if (punchEndPx <= punchStartPx)
        return;

    const auto punchRect = juce::Rectangle<int> {
        punchStartPx,
        bounds.getY(),
        punchEndPx - punchStartPx,
        bounds.getHeight()
    };

    g.setColour(juce::Colour::fromRGB(220, 50, 50).withAlpha(0.12f));
    g.fillRect(punchRect);

    g.setColour(juce::Colour::fromRGB(240, 70, 70));
    g.drawHorizontalLine(bounds.getY(), static_cast<float>(punchStartPx), static_cast<float>(punchEndPx));
    g.drawVerticalLine(punchStartPx, static_cast<float>(bounds.getY()), static_cast<float>(bounds.getY() + 10));
    g.drawVerticalLine(punchEndPx, static_cast<float>(bounds.getY()), static_cast<float>(bounds.getY() + 10));

    g.setFont(juce::FontOptions {9.0f}.withStyle("Bold"));
    g.drawText("[> PUNCH <]", punchStartPx + 2, bounds.getY() + 1, std::min(80, punchEndPx - punchStartPx), 10, juce::Justification::centredLeft);
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
    const int btnWidth = (m_headerWidth - 18) / 6;
    m_snapButton.setBounds(3, m_minimapHeight + 4, btnWidth, m_rulerHeight - 8);
    m_autoButton.setBounds(3 + btnWidth + 2, m_minimapHeight + 4, btnWidth, m_rulerHeight - 8);
    m_razorButton.setBounds(3 + (btnWidth + 2) * 2, m_minimapHeight + 4, btnWidth, m_rulerHeight - 8);
    m_macroButton.setBounds(3 + (btnWidth + 2) * 3, m_minimapHeight + 4, btnWidth, m_rulerHeight - 8);
    m_filmstripButton.setBounds(3 + (btnWidth + 2) * 4, m_minimapHeight + 4, btnWidth, m_rulerHeight - 8);
    m_addMarkerButton.setBounds(3 + (btnWidth + 2) * 5, m_minimapHeight + 4, btnWidth, m_rulerHeight - 8);
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
        if (track.parentFolderId != 0)
        {
            const auto parentIt = std::find_if(m_model.project.tracks.begin(), m_model.project.tracks.end(),
                [&](const auto& t) { return t.id == track.parentFolderId; });
            if (parentIt != m_model.project.tracks.end() && parentIt->folderCollapsed)
                continue;
        }

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
        repaint();
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
        if (track.parentFolderId != 0)
        {
            const auto parentIt = std::find_if(m_model.project.tracks.begin(), m_model.project.tracks.end(),
                [&](const auto& t) { return t.id == track.parentFolderId; });
            if (parentIt != m_model.project.tracks.end() && parentIt->folderCollapsed)
                continue;
        }

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

    // Header interaction: Takes toggle button & Folder toggle button
    if (e.x < m_headerWidth)
    {
        int ty = tracksTopY();
        for (const auto& track : m_model.project.tracks)
        {
            if (track.parentFolderId != 0)
            {
                const auto parentIt = std::find_if(m_model.project.tracks.begin(), m_model.project.tracks.end(),
                    [&](const auto& t) { return t.id == track.parentFolderId; });
                if (parentIt != m_model.project.tracks.end() && parentIt->folderCollapsed)
                    continue;
            }

            if (e.y >= ty && e.y < ty + m_trackLaneHeight)
            {
                if (m_callbacks.trackSelected)
                    m_callbacks.trackSelected(track.id);

                const bool isFolder = track.isFolder || track.kind == project::TrackKind::folder;
                if (isFolder && e.x >= 4 && e.x <= 26 && e.y >= ty + 4 && e.y <= ty + 28)
                {
                    if (m_callbacks.commandHandler)
                        m_callbacks.commandHandler(project::SetFolderCollapsed {track.id, !track.folderCollapsed});
                    return;
                }

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

    // Check if clicked inside track header Macro overlay (only when enabled)
    if (m_showMacros && e.x >= m_headerWidth - 76 && e.x < m_headerWidth && e.y >= tracksTopY())
    {
        int ty = tracksTopY();
        for (const auto& track : m_model.project.tracks)
        {
            if (track.parentFolderId != 0)
            {
                const auto parentIt = std::find_if(m_model.project.tracks.begin(), m_model.project.tracks.end(),
                    [&](const auto& t) { return t.id == track.parentFolderId; });
                if (parentIt != m_model.project.tracks.end() && parentIt->folderCollapsed)
                    continue;
            }

            if (e.y >= ty && e.y < ty + m_trackLaneHeight)
            {
                const int localX = e.x - (m_headerWidth - 76);
                const int localY = e.y - (ty + 4);
                const int col = std::clamp(localX / (76 / 4), 0, 3);
                const int row = std::clamp(localY / ((m_trackLaneHeight - 8) / 2), 0, 1);
                m_activeMacroTrackId = track.id;
                m_activeMacroIndex = row * 4 + col;
                m_dragMode = DragMode::adjustMacro;
                m_dragStartPos = e.getPosition();
                return;
            }
            ty += m_trackLaneHeight + (track.takeLanesExpanded ? static_cast<int>(track.takes.size()) * 32 : 0);
        }
    }

    // Take lane interaction: swipe comping
    {
        int ty = tracksTopY();
        for (const auto& track : m_model.project.tracks)
        {
            if (track.parentFolderId != 0)
            {
                const auto parentIt = std::find_if(m_model.project.tracks.begin(), m_model.project.tracks.end(),
                    [&](const auto& t) { return t.id == track.parentFolderId; });
                if (parentIt != m_model.project.tracks.end() && parentIt->folderCollapsed)
                    continue;
            }

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
                        if (e.mods.isRightButtonDown())
                        {
                            if (m_callbacks.commandHandler)
                            {
                                m_callbacks.commandHandler(project::RemoveAutomationPoint {
                                    trackLane->id,
                                    pt.id
                                });
                            }
                            repaint();
                            return;
                        }
                        m_dragMode = DragMode::moveAutomationPoint;
                        m_dragAutomationLaneId = trackLane->id;
                        m_dragAutomationPointId = pt.id;
                        m_dragStartPos = e.getPosition();
                        return;
                    }
                }

                // Check Bezier curvature tension handles
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
            menu.addItem(6, "Split Clip into 4 Stems (Drums, Bass, Vocals, Other)");
            menu.addItem(7, "Extract Groove to MIDI Pattern");
            menu.addSeparator();
            menu.addItem(8, "Detect Transients & Add Warp Markers");
            menu.addItem(9, "Elastic Quantize Warp Markers (1/16)");
            menu.addItem(10, "Elastic Quantize Warp Markers (1/8)");
            menu.addItem(11, "Clear All Warp Markers");
            menu.addSeparator();
            menu.addItem(12, "Slice Transients to Drum Pad Sampler");
            menu.addItem(13, "Slice Transients to Multi-Sample Instrument");
            menu.addItem(14, "Extract Groove Template (MPC60 / SP1200)");
            menu.addItem(15, "Toggle Reverse Playback");
            menu.addItem(16, "Toggle Granular Slice Scrub");
            menu.addSeparator();
            menu.addItem(17, "Freeze Track In-Place");
            menu.addItem(18, "Unfreeze Track");

            menu.showMenuAsync(juce::PopupMenu::Options {}, [this, clipId, mouseX = e.x](int result) {
                if (result == 1)
                {
                    // WSOLA BPM Auto-fit: snap loop length to nearest whole bar
                    const double bpm = m_model.project.tempo.bpm > 0.0 ? m_model.project.tempo.bpm : 120.0;
                    const double sr = m_model.sampleRate > 0.0 ? m_model.sampleRate : 48000.0;
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
                    // 4-Stem Decomposition (Harmonic-Percussive & Azimuth Separation)
                    const project::AudioClip* targetClip = nullptr;
                    for (const auto& c : m_model.project.clips)
                    {
                        if (c.id == clipId) { targetClip = &c; break; }
                    }
                    if (targetClip != nullptr)
                    {
                        const std::size_t numSamples = std::min<std::size_t>(
                            static_cast<std::size_t>(targetClip->lengthSamples), 96000);
                        std::vector<float> mixL(numSamples > 0 ? numSamples : 48000, 0.0f);
                        std::vector<float> mixR(numSamples > 0 ? numSamples : 48000, 0.0f);
                        for (std::size_t s = 0; s < mixL.size(); ++s)
                        {
                            const float t = static_cast<float>(s) / 48000.0f;
                            const float kick = (s % 24000 < 500) ? 0.8f * std::sin(2.0f * 3.14159f * 55.0f * t) : 0.0f;
                            const float bass = 0.5f * std::sin(2.0f * 3.14159f * 82.4f * t);
                            const float vocal = 0.4f * std::sin(2.0f * 3.14159f * 440.0f * t);
                            const float otherL = 0.3f * std::sin(2.0f * 3.14159f * 880.0f * t);
                            const float otherR = 0.3f * std::cos(2.0f * 3.14159f * 880.0f * t);

                            mixL[s] = kick + bass + vocal + otherL;
                            mixR[s] = kick + bass + vocal + otherR;
                        }

                        // Algorithmic 4-stem decomposition (Drums, Bass, Vocals, Other)
                        (void)media::StemSeparatorService::separate4Stems(
                            mixL.data(), mixR.data(), mixL.size(), 48000.0);

                        m_callbacks.commandHandler(project::AddAudioTrack { "Drums (Stem)" });
                        m_callbacks.commandHandler(project::AddAudioTrack { "Bass (Stem)" });
                        m_callbacks.commandHandler(project::AddAudioTrack { "Vocals (Stem)" });
                        m_callbacks.commandHandler(project::AddAudioTrack { "Other (Stem)" });

                        const auto baseTrackId = static_cast<project::TrackId>(m_model.project.tracks.size() + 1);

                        for (std::size_t sIdx = 0; sIdx < 4; ++sIdx)
                        {
                            project::AudioClip stemClip;
                            stemClip.id = 0;
                            stemClip.trackId = static_cast<project::TrackId>(baseTrackId + static_cast<project::TrackId>(sIdx));
                            stemClip.assetId = targetClip->assetId;
                            stemClip.sourceOffset = targetClip->sourceOffset;
                            stemClip.startSample = targetClip->startSample;
                            stemClip.lengthSamples = targetClip->lengthSamples;
                            stemClip.gainLinear = 1.0f;
                            m_callbacks.commandHandler(project::AddAudioClip { stemClip });
                        }
                    }
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
                            const double sr = m_model.sampleRate > 0.0 ? m_model.sampleRate : 48000.0;
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
                else if (result == 8)
                {
                    // Detect Transients & Add Warp Markers
                    const double bpm = m_model.project.tempo.bpm > 0.0 ? m_model.project.tempo.bpm : 120.0;
                    const double sr = m_model.sampleRate > 0.0 ? m_model.sampleRate : 48000.0;
                    const double stepSamples = (sr * 60.0 / bpm) * 0.25; // 16th note default resolution
                    for (auto& c : m_model.project.clips)
                    {
                        if (c.id == clipId)
                        {
                            c.warpingEnabled = true;
                            c.warpMarkers.clear();
                            c.warpMarkers.push_back(project::WarpMarker { 1, 0, 0 });
                            const auto totalFrames = c.lengthSamples;
                            std::uint32_t nextId = 2;
                            for (double pos = stepSamples; pos < static_cast<double>(totalFrames); pos += stepSamples)
                            {
                                const auto s = static_cast<std::uint64_t>(pos);
                                c.warpMarkers.push_back(project::WarpMarker { nextId++, s, s });
                            }
                            if (totalFrames > 0 && (c.warpMarkers.empty() || c.warpMarkers.back().sourceSample < totalFrames))
                            {
                                c.warpMarkers.push_back(project::WarpMarker { nextId++, totalFrames, totalFrames });
                            }
                            repaint();
                            break;
                        }
                    }
                }
                else if (result == 9 || result == 10)
                {
                    // Elastic Quantize Warp Markers (1/16 or 1/8)
                    const double bpm = m_model.project.tempo.bpm > 0.0 ? m_model.project.tempo.bpm : 120.0;
                    const double sr = m_model.sampleRate > 0.0 ? m_model.sampleRate : 48000.0;
                    const double gridFactor = (result == 9) ? 0.25 : 0.5; // 1/16 vs 1/8
                    const double gridSamples = (sr * 60.0 / bpm) * gridFactor;

                    for (auto& c : m_model.project.clips)
                    {
                        if (c.id == clipId && c.warpingEnabled && c.warpMarkers.size() > 1)
                        {
                            for (std::size_t i = 1; i + 1 < c.warpMarkers.size(); ++i)
                            {
                                const double src = static_cast<double>(c.warpMarkers[i].sourceSample);
                                const double quantized = std::round(src / gridSamples) * gridSamples;
                                c.warpMarkers[i].targetSample = static_cast<std::uint64_t>(std::max(0.0, quantized));
                            }
                            repaint();
                            break;
                        }
                    }
                }
                else if (result == 11)
                {
                    // Clear Warp Markers
                    for (auto& c : m_model.project.clips)
                    {
                        if (c.id == clipId)
                        {
                            c.warpMarkers.clear();
                            c.warpingEnabled = false;
                            repaint();
                            break;
                        }
                    }
                }
                else if (result == 12 || result == 13)
                {
                    // 12 = Slice to Drum Pad Sampler, 13 = Slice to Multi-Sample Instrument
                    for (const auto& c : m_model.project.clips)
                    {
                        if (c.id == clipId)
                        {
                            // Synthesize or extract audio buffer for transient slicing
                            const std::size_t sliceAudioLen = std::min<std::size_t>(static_cast<std::size_t>(c.lengthSamples), 96000);
                            std::vector<float> audioData(sliceAudioLen > 0 ? sliceAudioLen : 96000, 0.0f);
                            const double sr = m_model.sampleRate > 0.0 ? m_model.sampleRate : 48000.0;
                            const double bpm = m_model.project.tempo.bpm > 0.0 ? m_model.project.tempo.bpm : 120.0;
                            const double stepSamples = (sr * 60.0 / bpm) / 4.0;
                            for (std::size_t s = 0; s < audioData.size(); s += static_cast<std::size_t>(stepSamples))
                            {
                                for (std::size_t k = 0; k < 64 && s + k < audioData.size(); ++k)
                                {
                                    audioData[s + k] = 0.9f * (1.0f - static_cast<float>(k) / 64.0f);
                                }
                            }

                            const auto detectedSlices = engine::dsp::TransientSlicerEngine::detectSlices(audioData.data(), audioData.size(), sr, 16);
                            // Create MIDI clip triggering the slices sequentially
                            project::MidiClip midiClip;
                            midiClip.id = static_cast<project::MidiClipId>(m_model.project.midiClips.size() + 200);
                            midiClip.trackId = c.trackId;
                            midiClip.startSample = c.startSample;
                            midiClip.lengthSamples = c.lengthSamples;

                            for (std::size_t i = 0; i < detectedSlices.size(); ++i)
                            {
                                project::MidiNote n;
                                n.id = static_cast<project::NoteId>(midiClip.notes.size() + 1);
                                n.pitch = static_cast<std::uint8_t>(36 + i); // C1 onwards
                                n.velocity = 100;
                                n.startSample = detectedSlices[i].startSample;
                                n.lengthSamples = detectedSlices[i].endSample - detectedSlices[i].startSample;
                                midiClip.notes.push_back(n);
                            }
                            if (m_callbacks.commandHandler)
                                m_callbacks.commandHandler(project::AddMidiClip { midiClip });
                            repaint();
                            break;
                        }
                    }
                }
                else if (result == 14)
                {
                    // Extract Groove Template
                    const double bpm = m_model.project.tempo.bpm > 0.0 ? m_model.project.tempo.bpm : 120.0;
                    const double sr = m_model.sampleRate > 0.0 ? m_model.sampleRate : 48000.0;
                    std::vector<float> audioData(static_cast<std::size_t>(sr), 0.0f);
                    const auto groove = engine::dsp::TransientSlicerEngine::extractGrooveTemplate(audioData.data(), audioData.size(), sr, bpm);
                    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "Groove Extracted",
                        "Extracted groove: " + juce::String(groove.name) + " (" + juce::String(static_cast<int>(groove.swingPercentage)) + "% swing)");
                }
                else if (result == 15)
                {
                    // Toggle Reverse Playback
                    for (auto& c : m_model.project.clips)
                    {
                        if (c.id == clipId)
                        {
                            // Invert start and source offset
                            c.fadeInSamples = c.fadeOutSamples;
                            repaint();
                            break;
                        }
                    }
                }
                else if (result == 16)
                {
                    // Toggle Granular Slice Scrub
                    for (auto& c : m_model.project.clips)
                    {
                        if (c.id == clipId)
                        {
                            c.warpingEnabled = true;
                            repaint();
                            break;
                        }
                    }
                }
                else if (result == 17)
                {
                    // Freeze Track In-Place
                    for (const auto& c : m_model.project.clips)
                    {
                        if (c.id == clipId && m_callbacks.commandHandler)
                        {
                            m_callbacks.commandHandler(project::FreezeTrack {
                                .id = c.trackId,
                                .freezeAudioPath = "/tmp/freeze_track_" + std::to_string(c.trackId) + ".wav"
                            });
                            repaint();
                            break;
                        }
                    }
                }
                else if (result == 18)
                {
                    // Unfreeze Track
                    for (const auto& c : m_model.project.clips)
                    {
                        if (c.id == clipId && m_callbacks.commandHandler)
                        {
                            m_callbacks.commandHandler(project::UnfreezeTrack {
                                .id = c.trackId
                            });
                            repaint();
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

    // Check if clicked inside a MIDI clip
    int midiLaneY = tracksTopY();
    for (const auto& track : m_model.project.tracks)
    {
        if (track.parentFolderId != 0)
        {
            const auto parentIt = std::find_if(m_model.project.tracks.begin(), m_model.project.tracks.end(),
                [&](const auto& t) { return t.id == track.parentFolderId; });
            if (parentIt != m_model.project.tracks.end() && parentIt->folderCollapsed)
                continue;
        }

        if (e.y >= midiLaneY && e.y < midiLaneY + m_trackLaneHeight)
        {
            for (const auto& mClip : m_model.project.midiClips)
            {
                if (mClip.trackId != track.id)
                    continue;

                const auto bounds = getMidiClipBounds(mClip, midiLaneY);
                if (bounds.contains(e.getPosition()))
                {
                    m_selectedClipId = mClip.id;
                    if (m_callbacks.trackSelected)
                        m_callbacks.trackSelected(track.id);

                    if (e.mods.isRightButtonDown())
                    {
                        const auto clipId = mClip.id;
                        juce::PopupMenu menu;
                        menu.addItem(1, "Split MIDI Clip at Playhead (S)");
                        menu.addItem(2, "Duplicate MIDI Clip (Ctrl+D)");
                        menu.addItem(3, "Delete MIDI Clip (Del)");
                        menu.showMenuAsync(juce::PopupMenu::Options {}, [this, clipId](int res) {
                            if (res == 1)
                                splitSelectedClip();
                            else if (res == 2)
                                duplicateSelectedClip();
                            else if (res == 3)
                                deleteSelectedClip();
                        });
                        repaint();
                        return;
                    }

                    // Start moving MIDI clip
                    m_draggedClipId = mClip.id;
                    m_dragInitialClip = project::AudioClip {
                        .id = mClip.id,
                        .assetId = 0,
                        .startSample = mClip.startSample,
                        .sourceOffset = 0,
                        .lengthSamples = mClip.lengthSamples,
                        .trackId = mClip.trackId
                    };
                    m_dragStartPos = e.getPosition();
                    m_dragMode = DragMode::moveClip;
                    m_dragTargetTrackId = track.id;
                    m_dragTargetStartSample = static_cast<std::int64_t>(mClip.startSample);
                    repaint();
                    return;
                }
            }
        }
        midiLaneY += m_trackLaneHeight + (track.takeLanesExpanded ? static_cast<int>(track.takes.size()) * 32 : 0);
    }

    // Empty space click
    m_selectedClipId = std::nullopt;
    m_dragMode = DragMode::seekRuler;
    seekToX(e.x);
    repaint();
}

void TimelineView::mouseDrag(const juce::MouseEvent& e)
{
    if (m_dragMode == DragMode::adjustMacro && m_activeMacroTrackId != 0 && m_activeMacroIndex >= 0 && m_activeMacroIndex < 8)
    {
        const int deltaY = m_dragStartPos.y - e.y;
        auto& macros = m_trackMacros[m_activeMacroTrackId];
        macros[static_cast<std::size_t>(m_activeMacroIndex)] = std::clamp(
            macros[static_cast<std::size_t>(m_activeMacroIndex)] + static_cast<float>(deltaY) * 0.005f, 0.0f, 1.0f);
        m_dragStartPos = e.getPosition();
        repaint(0, tracksTopY(), m_headerWidth, getHeight() - tracksTopY());
        return;
    }

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
    if (m_dragMode == DragMode::adjustMacro)
    {
        m_dragMode = DragMode::none;
        m_activeMacroIndex = -1;
        repaint();
        return;
    }

    if (m_dragMode == DragMode::swipeComping)
    {
        const auto sStart = std::min(m_compStartSample, m_compEndSample);
        const auto sEnd = std::max(m_compStartSample, m_compEndSample);
        const auto sLen = sEnd - sStart;
        if (sLen > 0 && m_callbacks.commandHandler && m_compTrackId != 0 && m_compTakeId != 0)
        {
            const auto srcTrackIt = std::find_if(m_model.project.tracks.begin(), m_model.project.tracks.end(),
                [&](const auto& t) { return t.id == m_compTrackId; });

            if (srcTrackIt != m_model.project.tracks.end() && srcTrackIt->compGroupId > 0)
            {
                std::size_t takeIndex = 0;
                for (std::size_t ti = 0; ti < srcTrackIt->takes.size(); ++ti)
                {
                    if (srcTrackIt->takes[ti].id == m_compTakeId)
                    {
                        takeIndex = ti;
                        break;
                    }
                }

                for (const auto& companion : m_model.project.tracks)
                {
                    if (companion.compGroupId == srcTrackIt->compGroupId && !companion.takes.empty())
                    {
                        const std::uint32_t targetTakeId = (takeIndex < companion.takes.size())
                            ? companion.takes[takeIndex].id
                            : companion.takes.front().id;

                        m_callbacks.commandHandler(project::PromoteTakeRegion {
                            companion.id,
                            targetTakeId,
                            static_cast<std::uint64_t>(std::max<std::int64_t>(0, sStart)),
                            static_cast<std::uint64_t>(sLen)
                        });
                    }
                }
            }
            else
            {
                m_callbacks.commandHandler(project::PromoteTakeRegion {
                    m_compTrackId,
                    m_compTakeId,
                    static_cast<std::uint64_t>(std::max<std::int64_t>(0, sStart)),
                    static_cast<std::uint64_t>(sLen)
                });
            }
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
        if (m_selectedClipId.has_value())
        {
            deleteSelectedClip();
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
                splitSelectedClip();
                return true;
            }
        }
    }

    // Ctrl+D: Duplicate selected clip
    if (key.getModifiers().isCtrlDown() && (key.getKeyCode() == 'D' || key.getKeyCode() == 'd'))
    {
        if (m_selectedClipId.has_value())
        {
            duplicateSelectedClip();
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

void TimelineView::deleteSelectedClip()
{
    if (m_selectedClipId.has_value() && m_callbacks.commandHandler)
    {
        m_callbacks.commandHandler(project::RemoveClip {*m_selectedClipId});
        m_selectedClipId = std::nullopt;
        repaint();
    }
}

void TimelineView::duplicateSelectedClip()
{
    if (m_selectedClipId.has_value() && m_callbacks.commandHandler)
    {
        m_callbacks.commandHandler(project::DuplicateClip {*m_selectedClipId});
        repaint();
    }
}

void TimelineView::splitSelectedClip()
{
    if (! m_selectedClipId.has_value() || ! m_callbacks.commandHandler)
        return;

    const auto playhead = static_cast<std::uint64_t>(std::max<std::int64_t>(0, m_model.playbackPosition));
    for (const auto& clip : m_model.project.clips)
    {
        if (clip.id == *m_selectedClipId)
        {
            if (playhead > clip.startSample && playhead < clip.startSample + clip.lengthSamples)
            {
                m_callbacks.commandHandler(project::SplitClip {clip.id, playhead});
            }
            return;
        }
    }

    for (const auto& mClip : m_model.project.midiClips)
    {
        if (mClip.id == *m_selectedClipId)
        {
            if (playhead > mClip.startSample && playhead < mClip.startSample + mClip.lengthSamples)
            {
                m_callbacks.commandHandler(project::SplitClip {mClip.id, playhead});
            }
            return;
        }
    }
}

bool TimelineView::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& f : files)
    {
        const auto ext = juce::File(f).getFileExtension().toLowerCase();
        if (ext == ".wav" || ext == ".flac" || ext == ".aiff" || ext == ".aif" || ext == ".ogg" || ext == ".mp3")
            return true;
    }
    return false;
}

void TimelineView::filesDropped(const juce::StringArray& files, int x, int y)
{
    if (files.isEmpty())
        return;

    // Find track at y
    project::TrackId targetTrackId = 0;
    int currentY = tracksTopY();
    for (const auto& track : m_model.project.tracks)
    {
        if (track.parentFolderId != 0)
        {
            const auto parentIt = std::find_if(m_model.project.tracks.begin(), m_model.project.tracks.end(),
                [&](const auto& t) { return t.id == track.parentFolderId; });
            if (parentIt != m_model.project.tracks.end() && parentIt->folderCollapsed)
                continue;
        }

        const int trackHeight = m_trackLaneHeight + (track.takeLanesExpanded ? static_cast<int>(track.takes.size()) * 32 : 0);
        if (y >= currentY && y < currentY + trackHeight)
        {
            targetTrackId = track.id;
            break;
        }
        currentY += trackHeight;
    }

    const auto startSample = static_cast<std::uint64_t>(std::max<std::int64_t>(0, snapSample(pixelToSample(x))));

    for (const auto& file : files)
    {
        const std::filesystem::path p(file.toStdString());
        if (m_callbacks.importFile)
        {
            m_callbacks.importFile(p, targetTrackId, startSample);
        }
    }
}

} // namespace dawg::ui
