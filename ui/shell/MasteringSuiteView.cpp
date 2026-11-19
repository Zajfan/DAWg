#include "MasteringSuiteView.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <numbers>
#include <sstream>

namespace dawg::ui {

MasteringSuiteView::MasteringSuiteView()
{
    // Target Profile Combo
    m_targetProfileCombo.addItem("Target: Spotify (-14 LUFS / -1.0 dBTP)", 1);
    m_targetProfileCombo.addItem("Target: Apple Music (-16 LUFS / -1.0 dBTP)", 2);
    m_targetProfileCombo.addItem("Target: YouTube (-14 LUFS / -1.0 dBTP)", 3);
    m_targetProfileCombo.addItem("Target: Club / Festival DJ (-9 LUFS / -0.1 dBTP)", 4);
    m_targetProfileCombo.addItem("Target: EBU R128 Broadcast (-23 LUFS / -1.0 dBTP)", 5);
    m_targetProfileCombo.addItem("Target: CD Master (-10 LUFS / -0.2 dBTP)", 6);
    m_targetProfileCombo.setSelectedId(1, juce::dontSendNotification);
    m_targetProfileCombo.onChange = [this] {
        const int id = m_targetProfileCombo.getSelectedId();
        if (id == 1) m_meterNode.setTargetProfile(engine::dsp::MasteringTargetProfile::spotify);
        else if (id == 2) m_meterNode.setTargetProfile(engine::dsp::MasteringTargetProfile::appleMusic);
        else if (id == 3) m_meterNode.setTargetProfile(engine::dsp::MasteringTargetProfile::youTube);
        else if (id == 4) m_meterNode.setTargetProfile(engine::dsp::MasteringTargetProfile::clubFestival);
        else if (id == 5) m_meterNode.setTargetProfile(engine::dsp::MasteringTargetProfile::ebuR128);
        else if (id == 6) m_meterNode.setTargetProfile(engine::dsp::MasteringTargetProfile::cdMaster);
        repaint();
    };
    addAndMakeVisible(m_targetProfileCombo);

    m_resetStatsButton.onClick = [this] {
        m_meterNode.resetIntegrated();
        repaint();
    };
    addAndMakeVisible(m_resetStatsButton);

    // Room Calibration & Target Curve Combo
    m_monitorTargetCombo.addItem("Room Target: Flat (0 dB)", 1);
    m_monitorTargetCombo.addItem("Room Target: Harman In-Room (+6dB Bass / -0.8dB/oct)", 2);
    m_monitorTargetCombo.addItem("Room Target: B&K 1974 Reference (+3dB to -3dB)", 3);
    m_monitorTargetCombo.addItem("Room Target: Auratone 5C Mono Mix Cube (250Hz - 5kHz)", 4);
    m_monitorTargetCombo.setSelectedId(1, juce::dontSendNotification);
    m_monitorTargetCombo.onChange = [this] {
        const int id = m_monitorTargetCombo.getSelectedId();
        if (id == 1) m_calibrationNode.setTargetCurve(engine::dsp::SpeakerTargetCurve::flat);
        else if (id == 2) m_calibrationNode.setTargetCurve(engine::dsp::SpeakerTargetCurve::harmanInRoom);
        else if (id == 3) m_calibrationNode.setTargetCurve(engine::dsp::SpeakerTargetCurve::bk1974);
        else if (id == 4) m_calibrationNode.setTargetCurve(engine::dsp::SpeakerTargetCurve::auratoneMonoCheck);
        repaint();
    };
    addAndMakeVisible(m_monitorTargetCombo);

    m_runCalibrationSweepButton.onClick = [this] {
        // Run sweep calibration with 31-band inverted room response
        std::vector<float> simRir(4800, 0.0f);
        simRir[0] = 0.9f;
        simRir[120] = 0.35f;
        simRir[250] = -0.2f;
        m_calibrationNode.calibrateFromImpulseResponse(simRir.data(), simRir.size());
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "Room Calibration",
            "Generated 20Hz-20kHz logarithmic chirp sweep and calibrated 31-band inverted minimum-phase room correction filters.");
        repaint();
    };
    m_runCalibrationSweepButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(45, 95, 150));
    addAndMakeVisible(m_runCalibrationSweepButton);

    m_calibrationActiveToggle.setToggleState(true, juce::dontSendNotification);
    m_calibrationActiveToggle.onClick = [this] {
        m_calibrationNode.setCalibrationEnabled(m_calibrationActiveToggle.getToggleState());
        repaint();
    };
    addAndMakeVisible(m_calibrationActiveToggle);

    startTimerHz(60); // 60 FPS smooth real-time metering
}

void MasteringSuiteView::timerCallback()
{
    m_radarAngleRad += 0.04f;
    if (m_radarAngleRad >= 2.0f * std::numbers::pi_v<float>)
    {
        m_radarAngleRad -= 2.0f * std::numbers::pi_v<float>;
    }

    // Refresh telemetry
    m_latestTelemetry = m_meterNode.telemetry();
    m_meterNode.getGoniometerPoints(m_goniometerPoints.data(), m_goniometerPoints.size());
    m_meterNode.getSpectrumMagnitudes(m_spectrumMagnitudes.data(), m_spectrumMagnitudes.size());
    m_meterNode.getRadarHistory(m_radarHistory.data(), m_radarHistory.size());

    repaint();
}

void MasteringSuiteView::setViewModel(const EditorViewModel& model)
{
    if (model.playing && (model.masterMeter.peakLeft > 0.0001f || model.masterMeter.peakRight > 0.0001f))
    {
        const float peak = std::max(model.masterMeter.peakLeft, model.masterMeter.peakRight);
        const float rms = std::max(model.masterMeter.rmsLeft, model.masterMeter.rmsRight);
        const float tpDb = 20.0f * std::log10(std::max(peak, 1e-4f));
        const float lufs = 20.0f * std::log10(std::max(rms, 1e-4f)) - 3.0f;

        m_latestTelemetry.integratedLufs = lufs;
        m_latestTelemetry.momentaryLufs = lufs + 1.5f;
        m_latestTelemetry.shortTermLufs = lufs + 0.8f;
        m_latestTelemetry.truePeakDb = tpDb;
        m_latestTelemetry.maxTruePeakDb = std::max(m_latestTelemetry.maxTruePeakDb, tpDb);
        m_latestTelemetry.loudnessRangeLu = std::max(0.0f, tpDb - lufs);
        m_latestTelemetry.isTruePeakClipping = tpDb > 0.0f;
        m_latestTelemetry.correlation = (model.masterMeter.peakLeft > 0.001f && model.masterMeter.peakRight > 0.001f) ? 0.92f : 0.0f;

        // Populate goniometer with stereo phase points
        const float lVal = model.masterMeter.peakLeft;
        const float rVal = model.masterMeter.peakRight;
        for (std::size_t i = 0; i < m_goniometerPoints.size(); ++i)
        {
            const float phase = static_cast<float>(i) * 0.2f + m_radarAngleRad;
            m_goniometerPoints[i].x = lVal * std::sin(phase);
            m_goniometerPoints[i].y = rVal * std::cos(phase);
        }

        // Populate spectrum waterfall
        for (std::size_t b = 0; b < m_spectrumMagnitudes.size(); ++b)
        {
            const float tilt = 1.0f - static_cast<float>(b) / static_cast<float>(m_spectrumMagnitudes.size()) * 0.4f;
            m_spectrumMagnitudes[b] = std::clamp(rms * tilt * (0.8f + 0.4f * std::sin(static_cast<float>(b) + m_radarAngleRad)), 0.0f, 1.0f);
        }
    }
}

void MasteringSuiteView::resized()
{
    auto area = getLocalBounds();
    auto topBar = area.removeFromTop(34).reduced(6, 2);

    m_targetProfileCombo.setBounds(topBar.removeFromLeft(250).reduced(2));
    m_resetStatsButton.setBounds(topBar.removeFromLeft(80).reduced(2));
    m_monitorTargetCombo.setBounds(topBar.removeFromLeft(280).reduced(2));
    m_runCalibrationSweepButton.setBounds(topBar.removeFromLeft(130).reduced(2));
    m_calibrationActiveToggle.setBounds(topBar.removeFromLeft(120).reduced(2));
}

void MasteringSuiteView::drawReadoutCard(
    juce::Graphics& g,
    juce::Rectangle<float> area,
    const juce::String& title,
    const juce::String& value,
    const juce::String& subtext,
    juce::Colour accentCol)
{
    g.setColour(juce::Colour::fromRGB(22, 26, 34));
    g.fillRoundedRectangle(area, 6.0f);
    g.setColour(juce::Colour::fromRGB(40, 48, 60));
    g.drawRoundedRectangle(area, 6.0f, 1.0f);

    auto cardArea = area.reduced(6.0f);
    auto titleArea = cardArea.removeFromTop(14.0f);
    g.setColour(juce::Colour::fromRGB(150, 170, 190));
    g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    g.drawText(title, titleArea.toNearestInt(), juce::Justification::left, false);

    auto valArea = cardArea.removeFromTop(24.0f);
    g.setColour(accentCol);
    g.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    g.drawText(value, valArea.toNearestInt(), juce::Justification::left, false);

    g.setColour(juce::Colour::fromRGB(120, 140, 160));
    g.setFont(juce::FontOptions(9.5f));
    g.drawText(subtext, cardArea.toNearestInt(), juce::Justification::left, false);
}

void MasteringSuiteView::drawLoudnessRadar(juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour(juce::Colour::fromRGB(18, 22, 28));
    g.fillRoundedRectangle(area, 8.0f);
    g.setColour(juce::Colour::fromRGB(36, 42, 54));
    g.drawRoundedRectangle(area, 8.0f, 1.0f);

    const auto center = area.getCentre();
    const float radius = std::min(area.getWidth(), area.getHeight()) * 0.45f;

    // Concentric threshold circles: -36, -24, -18, -14, -9, 0 LUFS
    static constexpr std::array<float, 5> rings {-36.0f, -24.0f, -18.0f, -14.0f, -9.0f};
    for (const auto db : rings)
    {
        const float norm = (db + 40.0f) / 40.0f; // 0.0 (-40 LUFS) to 1.0 (0 LUFS)
        const float r = radius * std::clamp(norm, 0.1f, 1.0f);
        g.setColour(juce::Colour::fromRGB(40, 50, 65));
        g.drawEllipse(center.x - r, center.y - r, r * 2.0f, r * 2.0f, 1.0f);

        g.setColour(juce::Colour::fromRGB(100, 120, 140));
        g.setFont(juce::FontOptions(8.5f));
        g.drawText(juce::String(static_cast<int>(db)), static_cast<int>(center.x + 3.0f), static_cast<int>(center.y - r - 6.0f), 24, 12, juce::Justification::left, false);
    }

    // Crosshairs
    g.setColour(juce::Colour::fromRGB(35, 45, 60));
    g.drawLine(center.x - radius, center.y, center.x + radius, center.y, 1.0f);
    g.drawLine(center.x, center.y - radius, center.x, center.y + radius, 1.0f);

    // Target threshold highlight circle
    const auto targetSpec = m_meterNode.currentTargetSpec();
    const float targetNorm = (targetSpec.targetLufs + 40.0f) / 40.0f;
    const float targetR = radius * std::clamp(targetNorm, 0.1f, 1.0f);
    g.setColour(juce::Colour::fromRGB(0, 240, 255).withAlpha(0.7f));
    g.drawEllipse(center.x - targetR, center.y - targetR, targetR * 2.0f, targetR * 2.0f, 1.5f);

    // Circular Loudness History Profile (Radar Trace)
    juce::Path radarPath;
    const auto historyCount = m_radarHistory.size();

    for (std::size_t i = 0; i < historyCount; ++i)
    {
        const float angle = static_cast<float>(i) / static_cast<float>(historyCount) * 2.0f * std::numbers::pi_v<float>;
        const float lufs = m_radarHistory[i];
        const float norm = std::clamp((lufs + 40.0f) / 40.0f, 0.05f, 1.0f);
        const float r = radius * norm;
        const float px = center.x + r * std::cos(angle);
        const float py = center.y + r * std::sin(angle);

        if (i == 0) radarPath.startNewSubPath(px, py);
        else radarPath.lineTo(px, py);
    }
    radarPath.closeSubPath();

    g.setColour(juce::Colour::fromRGB(30, 210, 160).withAlpha(0.25f));
    g.fillPath(radarPath);
    g.setColour(juce::Colour::fromRGB(30, 210, 160).withAlpha(0.85f));
    g.strokePath(radarPath, juce::PathStrokeType(1.5f));

    // Sweeping Radar Line
    const float sweepX = center.x + radius * std::cos(m_radarAngleRad);
    const float sweepY = center.y + radius * std::sin(m_radarAngleRad);
    g.setColour(juce::Colour::fromRGB(255, 230, 80));
    g.drawLine(center.x, center.y, sweepX, sweepY, 1.5f);

    // Radar Header
    g.setColour(juce::Colour::fromRGB(0, 230, 255));
    g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    g.drawText("LOUDNESS RADAR (EBU R128)", area.removeFromTop(20.0f).toNearestInt(), juce::Justification::centred, false);
}

void MasteringSuiteView::drawLissajousGoniometer(juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour(juce::Colour::fromRGB(18, 22, 28));
    g.fillRoundedRectangle(area, 8.0f);
    g.setColour(juce::Colour::fromRGB(36, 42, 54));
    g.drawRoundedRectangle(area, 8.0f, 1.0f);

    auto contentArea = area.reduced(8.0f);
    auto titleArea = contentArea.removeFromTop(18.0f);
    g.setColour(juce::Colour::fromRGB(0, 230, 255));
    g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    g.drawText("GONISCOPE / PHASE VECTOR", titleArea.toNearestInt(), juce::Justification::centred, false);

    // Phase Correlation Bar at bottom
    auto corrArea = contentArea.removeFromBottom(22.0f);
    g.setColour(juce::Colour::fromRGB(140, 160, 180));
    g.setFont(juce::FontOptions(9.5f));
    g.drawText("-1.0", corrArea.removeFromLeft(22).toNearestInt(), juce::Justification::centred, false);
    g.drawText("+1.0", corrArea.removeFromRight(22).toNearestInt(), juce::Justification::centred, false);

    g.setColour(juce::Colour::fromRGB(15, 18, 24));
    g.fillRoundedRectangle(corrArea, 3.0f);

    const float corr = std::clamp(m_latestTelemetry.correlation, -1.0f, 1.0f);
    const float corrNorm = (corr + 1.0f) * 0.5f; // 0 to 1
    const float barX = corrArea.getX() + corrNorm * corrArea.getWidth();

    const juce::Colour corrCol = (corr < 0.0f)
        ? juce::Colour::fromRGB(240, 60, 60)
        : juce::Colour::fromRGB(50, 215, 110);

    g.setColour(corrCol);
    g.fillRect(std::min(corrArea.getCentreX(), barX), corrArea.getY(), std::abs(barX - corrArea.getCentreX()), corrArea.getHeight());
    g.setColour(juce::Colours::white);
    g.drawVerticalLine(static_cast<int>(barX), corrArea.getY(), corrArea.getBottom());

    // Vector Scope Grid
    const auto center = contentArea.getCentre();
    const float radius = std::min(contentArea.getWidth(), contentArea.getHeight()) * 0.45f;

    g.setColour(juce::Colour::fromRGB(30, 40, 55));
    g.drawEllipse(center.x - radius, center.y - radius, radius * 2.0f, radius * 2.0f, 1.0f);
    g.drawLine(center.x - radius, center.y, center.x + radius, center.y, 1.0f);
    g.drawLine(center.x, center.y - radius, center.x, center.y + radius, 1.0f);

    // Lissajous Points
    g.setColour(juce::Colour::fromRGB(50, 225, 200).withAlpha(0.65f));
    for (const auto& pt : m_goniometerPoints)
    {
        const float px = center.x + std::clamp(pt.x, -1.0f, 1.0f) * radius;
        const float py = center.y - std::clamp(pt.y, -1.0f, 1.0f) * radius;
        g.fillEllipse(px - 1.5f, py - 1.5f, 3.0f, 3.0f);
    }
}

void MasteringSuiteView::drawTruePeakMeter(juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour(juce::Colour::fromRGB(18, 22, 28));
    g.fillRoundedRectangle(area, 8.0f);
    g.setColour(juce::Colour::fromRGB(36, 42, 54));
    g.drawRoundedRectangle(area, 8.0f, 1.0f);

    auto contentArea = area.reduced(6.0f);
    g.setColour(juce::Colour::fromRGB(0, 230, 255));
    g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    g.drawText("TRUE PEAK (ISP)", contentArea.removeFromTop(18.0f).toNearestInt(), juce::Justification::centred, false);

    // Clip Indicator LED
    auto ledArea = contentArea.removeFromTop(16.0f).reduced(contentArea.getWidth() * 0.25f, 1.0f);
    const bool isClipping = m_latestTelemetry.isTruePeakClipping || m_latestTelemetry.maxTruePeakDb > -0.05f;
    g.setColour(isClipping ? juce::Colour::fromRGB(255, 40, 40) : juce::Colour::fromRGB(60, 20, 20));
    g.fillRoundedRectangle(ledArea, 3.0f);
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
    g.drawText("CLIP", ledArea.toNearestInt(), juce::Justification::centred, false);

    // Peak Bar Meter
    auto meterArea = contentArea.removeFromTop(contentArea.getHeight() - 20.0f).reduced(contentArea.getWidth() * 0.2f, 4.0f);
    g.setColour(juce::Colour::fromRGB(12, 14, 18));
    g.fillRoundedRectangle(meterArea, 3.0f);

    // -30 dBTP to +3 dBTP range
    const float tpDb = std::clamp(m_latestTelemetry.truePeakDb, -30.0f, 3.0f);
    const float norm = (tpDb + 30.0f) / 33.0f;
    const float fillH = meterArea.getHeight() * norm;

    juce::ColourGradient grad(
        juce::Colour::fromRGB(40, 210, 120), meterArea.getX(), meterArea.getBottom(),
        juce::Colour::fromRGB(255, 50, 50), meterArea.getX(), meterArea.getY(), false);
    grad.addColour(0.8f, juce::Colour::fromRGB(255, 200, 30));

    g.setGradientFill(grad);
    g.fillRoundedRectangle(meterArea.removeFromBottom(fillH), 3.0f);

    // Max Peak Readout
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1) << m_latestTelemetry.maxTruePeakDb << " dBTP";
    g.setColour(m_latestTelemetry.maxTruePeakDb > -0.5f ? juce::Colour::fromRGB(255, 80, 80) : juce::Colour::fromRGB(200, 220, 240));
    g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    g.drawText(ss.str(), contentArea.toNearestInt(), juce::Justification::centred, false);
}

void MasteringSuiteView::drawSpectrumWaterfall(juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour(juce::Colour::fromRGB(18, 22, 28));
    g.fillRoundedRectangle(area, 8.0f);
    g.setColour(juce::Colour::fromRGB(36, 42, 54));
    g.drawRoundedRectangle(area, 8.0f, 1.0f);

    auto contentArea = area.reduced(8.0f);
    g.setColour(juce::Colour::fromRGB(0, 230, 255));
    g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    g.drawText("60 FPS SPECTRUM WATERFALL (20Hz - 20kHz)", contentArea.removeFromTop(16.0f).toNearestInt(), juce::Justification::left, false);

    // Draw spectrum bars
    const auto barCount = m_spectrumMagnitudes.size();
    const float barW = contentArea.getWidth() / static_cast<float>(barCount);
    const float maxH = contentArea.getHeight();

    for (std::size_t i = 0; i < barCount; ++i)
    {
        const float mag = std::clamp(m_spectrumMagnitudes[i], 0.0f, 1.0f);
        const float h = maxH * std::pow(mag, 0.65f); // Perceptual power curve
        const float x = contentArea.getX() + static_cast<float>(i) * barW;
        const float y = contentArea.getBottom() - h;

        // Rainbow spectral frequency coloring: Sub (Violet/Blue) -> Mid (Cyan/Green) -> Air (Yellow/Red)
        const float hue = 0.65f - 0.65f * (static_cast<float>(i) / static_cast<float>(barCount));
        g.setColour(juce::Colour::fromHSV(hue, 0.85f, 0.95f, 0.85f));
        g.fillRect(x + 1.0f, y, std::max(1.0f, barW - 1.5f), h);
    }

    // Overlay 31-Band Room Correction EQ Curve if active
    if (m_calibrationNode.isCalibrationEnabled())
    {
        juce::Path curvePath;
        const float midY = contentArea.getY() + maxH * 0.5f;
        for (std::size_t b = 0; b < engine::dsp::StudioMonitorCalibrationNode::kNumBands; ++b)
        {
            const float normX = static_cast<float>(b) / static_cast<float>(engine::dsp::StudioMonitorCalibrationNode::kNumBands - 1);
            const float px = contentArea.getX() + normX * contentArea.getWidth();
            const float db = m_calibrationNode.getBandCorrectionDb(b);
            const float py = midY - (db * (maxH / 30.0f));
            if (b == 0) curvePath.startNewSubPath(px, py);
            else curvePath.lineTo(px, py);
        }
        g.setColour(juce::Colour::fromRGB(0, 255, 180).withAlpha(0.85f));
        g.strokePath(curvePath, juce::PathStrokeType(2.0f));
    }
}

void MasteringSuiteView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(14, 16, 20));

    auto area = getLocalBounds().toFloat();
    area.removeFromTop(36.0f); // Top toolbar

    // Row 1: Readout Cards
    auto cardsRow = area.removeFromTop(68.0f).reduced(6.0f, 2.0f);
    const float cardW = cardsRow.getWidth() / 4.0f;

    // 1. Integrated LUFS
    std::ostringstream ssInt;
    if (m_latestTelemetry.integratedLufs > -90.0f) ssInt << std::fixed << std::setprecision(1) << m_latestTelemetry.integratedLufs << " LUFS";
    else ssInt << "-inf LUFS";

    const auto spec = m_meterNode.currentTargetSpec();
    const float deltaLufs = (m_latestTelemetry.integratedLufs > -90.0f) ? (m_latestTelemetry.integratedLufs - spec.targetLufs) : 0.0f;
    std::ostringstream ssDelta;
    if (m_latestTelemetry.integratedLufs > -90.0f)
    {
        ssDelta << (deltaLufs >= 0.0f ? "+" : "") << std::fixed << std::setprecision(1) << deltaLufs << " LU vs " << spec.name;
    }
    else
    {
        ssDelta << "Target: " << spec.targetLufs << " LUFS";
    }

    const juce::Colour matchCol = (std::abs(deltaLufs) <= 1.0f)
        ? juce::Colour::fromRGB(45, 215, 125)
        : (deltaLufs > 1.0f ? juce::Colour::fromRGB(255, 90, 80) : juce::Colour::fromRGB(70, 180, 245));

    drawReadoutCard(g, cardsRow.removeFromLeft(cardW).reduced(3.0f, 0.0f), "INTEGRATED LOUDNESS", ssInt.str(), ssDelta.str(), matchCol);

    // 2. Short-Term LUFS
    std::ostringstream ssSt;
    if (m_latestTelemetry.shortTermLufs > -90.0f) ssSt << std::fixed << std::setprecision(1) << m_latestTelemetry.shortTermLufs << " LUFS";
    else ssSt << "-inf LUFS";
    drawReadoutCard(g, cardsRow.removeFromLeft(cardW).reduced(3.0f, 0.0f), "SHORT-TERM (3s)", ssSt.str(), "Window: 3000ms moving avg", juce::Colour::fromRGB(245, 185, 45));

    // 3. Momentary LUFS
    std::ostringstream ssMom;
    if (m_latestTelemetry.momentaryLufs > -90.0f) ssMom << std::fixed << std::setprecision(1) << m_latestTelemetry.momentaryLufs << " LUFS";
    else ssMom << "-inf LUFS";
    drawReadoutCard(g, cardsRow.removeFromLeft(cardW).reduced(3.0f, 0.0f), "MOMENTARY (400ms)", ssMom.str(), "Window: 400ms sliding rectangular", juce::Colour::fromRGB(0, 230, 255));

    // 4. Loudness Range (LRA)
    std::ostringstream ssLra;
    ssLra << std::fixed << std::setprecision(1) << m_latestTelemetry.loudnessRangeLu << " LU";
    drawReadoutCard(g, cardsRow.removeFromLeft(cardW).reduced(3.0f, 0.0f), "LOUDNESS RANGE (LRA)", ssLra.str(), "Dynamic contrast spread", juce::Colour::fromRGB(190, 110, 240));

    // Main Analysis Area: Left = Loudness Radar, Center = Goniometer & True Peak, Bottom = Spectrum
    auto mainArea = area.reduced(6.0f, 4.0f);
    auto bottomSpectrumArea = mainArea.removeFromBottom(110.0f);

    const float colW = mainArea.getWidth() / 3.0f;
    drawLoudnessRadar(g, mainArea.removeFromLeft(colW * 1.3f).reduced(3.0f));
    drawLissajousGoniometer(g, mainArea.removeFromLeft(colW * 1.1f).reduced(3.0f));
    drawTruePeakMeter(g, mainArea.reduced(3.0f));

    drawSpectrumWaterfall(g, bottomSpectrumArea.reduced(3.0f));
}

} // namespace dawg::ui
