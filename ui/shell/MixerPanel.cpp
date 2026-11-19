#include "MixerPanel.h"
#include "DspEditorDialog.h"
#include "VirusTiPolarEditorView.h"

#include <algorithm>
#include <cmath>

namespace dawg::ui {

// ============================================================================
// ChannelStripComponent
// ============================================================================

ChannelStripComponent::ChannelStripComponent(CommandHandler handler)
    : m_handler(std::move(handler))
{
    m_nameLabel.setJustificationType(juce::Justification::centred);
    m_nameLabel.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    addAndMakeVisible(m_nameLabel);

    m_panKnob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    m_panKnob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    m_panKnob.setRange(-1.0, 1.0, 0.01);
    m_panKnob.setValue(0.0, juce::dontSendNotification);
    addAndMakeVisible(m_panKnob);

    m_fader.setSliderStyle(juce::Slider::LinearVertical);
    m_fader.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 48, 16);
    m_fader.setRange(0.0, 2.0, 0.01);
    m_fader.setValue(1.0, juce::dontSendNotification);
    addAndMakeVisible(m_fader);

    addAndMakeVisible(m_muteButton);
    addAndMakeVisible(m_soloButton);
    addAndMakeVisible(m_phaseButton);
    addAndMakeVisible(m_armButton);

    m_panLawCombo.addItem("-3dB", 1);
    m_panLawCombo.addItem("-4.5dB", 2);
    m_panLawCombo.addItem("-6dB", 3);
    m_panLawCombo.addItem("0dB", 4);
    m_panLawCombo.setSelectedId(1, juce::dontSendNotification);
    addAndMakeVisible(m_panLawCombo);

    m_vcaCombo.addItem("VCA -", 1);
    for (int v = 1; v <= 8; ++v)
        m_vcaCombo.addItem("VCA " + std::to_string(v), v + 1);
    m_vcaCombo.setSelectedId(1, juce::dontSendNotification);
    addAndMakeVisible(m_vcaCombo);

    m_addInsertBtn.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(40, 52, 70));
    m_addInsertBtn.onClick = [this] {
        juce::PopupMenu menu;
        menu.addItem(1, "Parametric EQ (6-Band)");
        menu.addItem(2, "Dynamics Compressor");
        menu.addItem(3, "ResoTamer Suppressor");
        menu.addItem(4, "StereoScope Imager");
        menu.addItem(5, "MasterPhase Dynamic EQ");
        menu.addItem(6, "Complex 259 Synth");
        menu.addItem(7, "Studio Reverb");
        menu.addItem(8, "Stereo Delay");
        menu.addItem(9, "Preamp: SSL 4k Line");
        menu.addItem(11, "Access Virus TI Polar Synth");
        menu.addSeparator();
        menu.addItem(10, "Sidechain Listen", true, m_sidechainListen);

        menu.showMenuAsync(juce::PopupMenu::Options {}, [this](int result) {
            const std::string trkName = m_nameLabel.getText().toStdString();
            if (result == 1) {
                m_slotNames[0] = "EQ";
                m_slotButtons[0].setButtonText("EQ");
                DspEditorWindow::open(DspEditorKind::parametricEq, trkName);
            }
            else if (result == 2) {
                m_slotNames[1] = "COMP";
                m_slotButtons[1].setButtonText("COMP");
                DspEditorWindow::open(DspEditorKind::compressor, trkName);
            }
            else if (result == 3) {
                m_slotNames[0] = "RESO";
                m_slotButtons[0].setButtonText("RESO");
                DspEditorWindow::open(DspEditorKind::resonanceSuppressor, trkName);
            }
            else if (result == 4) {
                m_slotNames[1] = "SCOPE";
                m_slotButtons[1].setButtonText("SCOPE");
                DspEditorWindow::open(DspEditorKind::multibandImager, trkName);
            }
            else if (result == 5) {
                m_slotNames[0] = "MPHASE";
                m_slotButtons[0].setButtonText("MPHASE");
                DspEditorWindow::open(DspEditorKind::linearPhaseEq, trkName);
            }
            else if (result == 6) {
                m_slotNames[0] = "259";
                m_slotButtons[0].setButtonText("259");
                DspEditorWindow::open(DspEditorKind::buchlaComplexSynth, trkName);
            }
            else if (result == 7) {
                m_slotNames[1] = "REV";
                m_slotButtons[1].setButtonText("REV");
                DspEditorWindow::open(DspEditorKind::reverb, trkName);
            }
            else if (result == 8) {
                m_slotNames[1] = "DLY";
                m_slotButtons[1].setButtonText("DLY");
                DspEditorWindow::open(DspEditorKind::delay, trkName);
            }
            else if (result == 9) {
                m_preampModel = 0;
                m_slotNames[0] = "SSL";
                m_slotButtons[0].setButtonText("SSL");
            }
            else if (result == 10) {
                m_sidechainListen = ! m_sidechainListen;
            }
            else if (result == 11) {
                m_slotNames[0] = "VIRUS";
                m_slotButtons[0].setButtonText("VIRUS");
                VirusTiPolarWindow::open();
            }
            repaint();
        });
    };
    addAndMakeVisible(m_addInsertBtn);

    for (std::size_t i = 0; i < 2; ++i)
    {
        m_slotButtons[i].setButtonText(m_slotNames[i]);
        m_slotButtons[i].setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(36, 42, 54));
        m_slotButtons[i].onClick = [this, i] {
            openInsertEditor(i);
        };
        addAndMakeVisible(m_slotButtons[i]);
    }

    m_macroToggleBtn.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(40, 46, 58));
    m_macroToggleBtn.onClick = [this] {
        m_showMacros = ! m_showMacros;
        m_macroToggleBtn.setColour(juce::TextButton::buttonColourId,
            m_showMacros ? juce::Colour::fromRGB(0, 180, 220) : juce::Colour::fromRGB(40, 46, 58));
        resized();
        repaint();
    };
    addAndMakeVisible(m_macroToggleBtn);

    m_panKnob.onValueChange = [this] {
        if (! m_updatingFromModel)
            sendGainPan();
    };

    m_fader.onValueChange = [this] {
        if (! m_updatingFromModel)
            sendGainPan();
    };

    m_muteButton.onClick = [this] {
        if (m_updatingFromModel || ! m_handler || m_trackId == 0)
            return;
        m_handler(project::SetTrackMute {
            m_trackId,
            m_muteButton.getToggleState()
        });
    };

    m_soloButton.onClick = [this] {
        if (m_updatingFromModel || ! m_handler || m_trackId == 0)
            return;
        m_handler(project::SetTrackSolo {
            m_trackId,
            m_soloButton.getToggleState()
        });
    };

    m_phaseButton.onClick = [this] {
        if (! m_updatingFromModel)
            sendGainPan();
    };

    m_armButton.onClick = [this] {
        if (m_updatingFromModel || ! m_handler || m_trackId == 0)
            return;
        m_handler(project::SetTrackRecordArm {
            m_trackId,
            m_armButton.getToggleState()
        });
    };

    m_muteButton.setClickingTogglesState(true);
    m_soloButton.setClickingTogglesState(true);
    m_phaseButton.setClickingTogglesState(true);
    m_armButton.setClickingTogglesState(true);

    startTimerHz(30);
}

void ChannelStripComponent::openInsertEditor(std::size_t slotIdx)
{
    const std::string trkName = m_nameLabel.getText().toStdString();
    const auto& name = m_slotNames[slotIdx];
    if (name == "EQ") DspEditorWindow::open(DspEditorKind::parametricEq, trkName);
    else if (name == "COMP") DspEditorWindow::open(DspEditorKind::compressor, trkName);
    else if (name == "RESO") DspEditorWindow::open(DspEditorKind::resonanceSuppressor, trkName);
    else if (name == "SCOPE") DspEditorWindow::open(DspEditorKind::multibandImager, trkName);
    else if (name == "MPHASE") DspEditorWindow::open(DspEditorKind::linearPhaseEq, trkName);
    else if (name == "259") DspEditorWindow::open(DspEditorKind::buchlaComplexSynth, trkName);
    else if (name == "REV") DspEditorWindow::open(DspEditorKind::reverb, trkName);
    else if (name == "DLY") DspEditorWindow::open(DspEditorKind::delay, trkName);
    else if (name == "VIRUS") VirusTiPolarWindow::open();
    else DspEditorWindow::open(DspEditorKind::parametricEq, trkName);
}

ChannelStripComponent::~ChannelStripComponent()
{
    stopTimer();
}

void ChannelStripComponent::timerCallback()
{
    m_macroAnimPhase += 0.07f;
    if (m_macroAnimPhase > juce::MathConstants<float>::twoPi)
        m_macroAnimPhase -= juce::MathConstants<float>::twoPi;
    repaint(m_macroBounds);
}

void ChannelStripComponent::drawMacroOverlay(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    if (bounds.isEmpty())
        return;

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

    const int colW = bounds.getWidth() / 4;
    const int rowH = bounds.getHeight() / 2;

    for (std::size_t m = 0; m < 8; ++m)
    {
        const int col = static_cast<int>(m % 4);
        const int row = static_cast<int>(m / 4);
        juce::Rectangle<int> cell(bounds.getX() + col * colW, bounds.getY() + row * rowH, colW, rowH);

        const float cx = static_cast<float>(cell.getCentreX());
        const float cy = static_cast<float>(cell.getCentreY());
        const float radius = static_cast<float>(std::min(colW, rowH)) * 0.35f;

        const float baseVal = m_macros[m];
        const float modLfo = 0.28f * std::sin(m_macroAnimPhase * 1.5f + static_cast<float>(m) * 0.78f);
        const float curVal = std::clamp(baseVal + modLfo, 0.0f, 1.0f);

        // Dial base
        const bool isDragged = (m_draggedMacroIdx == static_cast<int>(m));
        g.setColour(isDragged ? juce::Colour::fromRGB(35, 42, 55) : juce::Colour::fromRGB(18, 20, 25));
        g.fillEllipse(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f);
        g.setColour(isDragged ? macroColors[m] : juce::Colour::fromRGB(40, 45, 55));
        g.drawEllipse(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f, 1.0f);

        // Arc angles
        constexpr float startAngle = -2.35619f;
        constexpr float endAngle = 2.35619f;
        const float baseAngle = startAngle + baseVal * (endAngle - startAngle);
        const float curAngle = startAngle + curVal * (endAngle - startAngle);

        juce::Path arc;
        const float a1 = std::min(baseAngle, curAngle);
        const float a2 = std::max(baseAngle, curAngle);
        arc.addCentredArc(cx, cy, radius + 1.6f, radius + 1.6f, 0.0f, a1, a2, true);
        g.setColour(macroColors[m].withAlpha(0.85f));
        g.strokePath(arc, juce::PathStrokeType(1.6f));

        // Dial indicator needle
        const float needleLen = radius * 0.75f;
        const float nx = cx + std::sin(baseAngle) * needleLen;
        const float ny = cy - std::cos(baseAngle) * needleLen;
        g.setColour(juce::Colours::white);
        g.drawLine(cx, cy, nx, ny, 1.1f);
    }
}

void ChannelStripComponent::mouseDown(const juce::MouseEvent& e)
{
    if (m_macroBounds.contains(e.getPosition()) && m_macroBounds.getWidth() > 0 && m_macroBounds.getHeight() > 0)
    {
        const int colW = std::max(1, m_macroBounds.getWidth() / 4);
        const int rowH = std::max(1, m_macroBounds.getHeight() / 2);
        const int col = std::clamp((e.x - m_macroBounds.getX()) / colW, 0, 3);
        const int row = std::clamp((e.y - m_macroBounds.getY()) / rowH, 0, 1);
        const int idx = row * 4 + col;
        if (idx >= 0 && idx < 8)
        {
            m_draggedMacroIdx = idx;
            m_dragStartPos = e.getPosition();
            m_dragStartVal = m_macros[static_cast<std::size_t>(idx)];
            repaint(m_macroBounds);
        }
    }
}

void ChannelStripComponent::mouseDrag(const juce::MouseEvent& e)
{
    if (m_draggedMacroIdx >= 0 && m_draggedMacroIdx < 8)
    {
        const int deltaY = m_dragStartPos.y - e.y;
        const float nextVal = std::clamp(m_dragStartVal + static_cast<float>(deltaY) * 0.008f, 0.0f, 1.0f);
        m_macros[static_cast<std::size_t>(m_draggedMacroIdx)] = nextVal;
        repaint(m_macroBounds);
    }
}

void ChannelStripComponent::mouseUp(const juce::MouseEvent&)
{
    if (m_draggedMacroIdx >= 0)
    {
        m_draggedMacroIdx = -1;
        repaint(m_macroBounds);
    }
}

void ChannelStripComponent::sendGainPan()
{
    if (! m_handler || m_trackId == 0)
        return;
    m_handler(project::SetTrackGainPan {
        m_trackId,
        static_cast<float>(m_fader.getValue()),
        static_cast<float>(m_panKnob.getValue()),
        m_phaseButton.getToggleState()
    });
}

void ChannelStripComponent::updateTrack(const project::Track& track)
{
    m_trackId = track.id;
    m_updatingFromModel = true;

    m_nameLabel.setText(track.name, juce::dontSendNotification);
    m_fader.setValue(track.gainLinear, juce::dontSendNotification);
    m_panKnob.setValue(track.pan, juce::dontSendNotification);
    m_muteButton.setToggleState(track.muted, juce::dontSendNotification);
    m_soloButton.setToggleState(track.solo, juce::dontSendNotification);
    m_phaseButton.setToggleState(track.phaseInvert, juce::dontSendNotification);
    m_armButton.setToggleState(track.recordArmed, juce::dontSendNotification);

    if (track.muted)
        m_muteButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour::fromRGB(200, 140, 30));
    if (track.solo)
        m_soloButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour::fromRGB(220, 200, 40));
    if (track.phaseInvert)
        m_phaseButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour::fromRGB(140, 90, 220));
    if (track.recordArmed)
        m_armButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour::fromRGB(220, 40, 40));

    if (! track.inserts.empty())
    {
        for (std::size_t i = 0; i < std::min<std::size_t>(2, track.inserts.size()); ++i)
        {
            const auto& ins = track.inserts[i];
            m_slotNames[i] = ins.name.empty() ? ("Slot " + std::to_string(i + 1)) : ins.name;
            m_slotButtons[i].setButtonText(m_slotNames[i]);
        }
    }

    m_updatingFromModel = false;
}

void ChannelStripComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(36, 38, 44));
    g.setColour(juce::Colour::fromRGB(50, 54, 62));
    g.drawRect(getLocalBounds(), 1);

    // 8-Macro Modulation Overlay (only when drawer is expanded)
    if (m_showMacros && ! m_macroBounds.isEmpty())
    {
        drawMacroOverlay(g, m_macroBounds);
    }

    // Visual Gain Reduction (GR) Overlay
    if (m_gainReductionDb > 0.1f)
    {
        const auto faderBounds = m_fader.getBounds();
        constexpr int grWidth = 4;
        const int grX = faderBounds.getRight() - grWidth - 2;
        const int grMaxHeight = std::max(10, faderBounds.getHeight() - 8);
        const float clampedGr = std::clamp(m_gainReductionDb, 0.0f, 24.0f);
        const int grHeight = static_cast<int>((clampedGr / 24.0f) * static_cast<float>(grMaxHeight));

        const juce::Rectangle<int> grRect(grX, faderBounds.getY() + 4, grWidth, grHeight);
        g.setColour(juce::Colour::fromRGB(255, 95, 45));
        g.fillRect(grRect);

        // Numeric GR Readout
        g.setColour(juce::Colour::fromRGB(255, 140, 70));
        g.setFont(juce::FontOptions(8.0f));
        g.drawText("-" + juce::String(clampedGr, 1) + "dB", faderBounds.getX(), faderBounds.getY() - 10, faderBounds.getWidth(), 10, juce::Justification::centredRight);
    }
}

void ChannelStripComponent::resized()
{
    auto area = getLocalBounds().reduced(3, 4);

    m_nameLabel.setBounds(area.removeFromTop(20));
    area.removeFromTop(2);

    // Dynamic Insert Slots Header (Progressive Disclosure)
    auto insertHdr = area.removeFromTop(18);
    m_addInsertBtn.setBounds(insertHdr.removeFromLeft(insertHdr.getWidth() / 2).reduced(1));
    m_macroToggleBtn.setBounds(insertHdr.reduced(1));
    area.removeFromTop(2);

    // Slot buttons (2 active slots)
    auto slotRow = area.removeFromTop(16);
    m_slotButtons[0].setBounds(slotRow.removeFromLeft(slotRow.getWidth() / 2).reduced(1));
    m_slotButtons[1].setBounds(slotRow.reduced(1));
    area.removeFromTop(2);

    // Routing Dropdowns: Pan Law & VCA
    auto routeRow = area.removeFromTop(18);
    m_panLawCombo.setBounds(routeRow.removeFromLeft(routeRow.getWidth() / 2).reduced(1));
    m_vcaCombo.setBounds(routeRow.reduced(1));
    area.removeFromTop(2);

    m_panKnob.setBounds(area.removeFromTop(24).reduced(6, 0));
    area.removeFromTop(2);

    // Button row: M, S, POL, REC
    auto buttonRow1 = area.removeFromTop(20);
    m_muteButton.setBounds(buttonRow1.removeFromLeft(buttonRow1.getWidth() / 2).reduced(1));
    m_soloButton.setBounds(buttonRow1.reduced(1));
    area.removeFromTop(2);

    auto buttonRow2 = area.removeFromTop(20);
    m_phaseButton.setBounds(buttonRow2.removeFromLeft(buttonRow2.getWidth() / 2).reduced(1));
    m_armButton.setBounds(buttonRow2.reduced(1));
    area.removeFromTop(3);

    // 8-Macro Modulation Mini-Matrix (Collapsible Drawer)
    if (m_showMacros)
    {
        m_macroBounds = area.removeFromTop(28).reduced(1, 0);
        area.removeFromTop(3);
    }
    else
    {
        m_macroBounds = {};
    }

    m_fader.setBounds(area);
}

// ============================================================================
// MasterStripComponent
// ============================================================================

MasterStripComponent::MasterStripComponent(LimiterCallback limiterCb)
    : m_limiterCallback(std::move(limiterCb))
{
    m_nameLabel.setJustificationType(juce::Justification::centred);
    m_nameLabel.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    addAndMakeVisible(m_nameLabel);

    m_limiterButton.setClickingTogglesState(true);
    m_limiterButton.onClick = [this] {
        const bool active = m_limiterButton.getToggleState();
        if (m_limiterCallback)
            m_limiterCallback(active);
        if (active)
            m_limiterButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour::fromRGB(40, 160, 220));
    };
    addAndMakeVisible(m_limiterButton);

    m_cueLabel.setJustificationType(juce::Justification::centred);
    m_cueLabel.setFont(juce::FontOptions(9.0f, juce::Font::bold));
    m_cueLabel.setColour(juce::Label::textColourId, juce::Colour(0xff58a6ff));
    addAndMakeVisible(m_cueLabel);

    m_cueAFader.setSliderStyle(juce::Slider::LinearBar);
    m_cueAFader.setRange(0.0, 1.2, 0.01);
    m_cueAFader.setValue(1.0, juce::dontSendNotification);
    m_cueAFader.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    m_cueAFader.setColour(juce::Slider::trackColourId, juce::Colour(0xff238636));
    addAndMakeVisible(m_cueAFader);

    m_cueALabel.setFont(juce::FontOptions(9.0f));
    m_cueALabel.setColour(juce::Label::textColourId, juce::Colour(0xff8b949e));
    addAndMakeVisible(m_cueALabel);

    m_cueBFader.setSliderStyle(juce::Slider::LinearBar);
    m_cueBFader.setRange(0.0, 1.2, 0.01);
    m_cueBFader.setValue(0.8, juce::dontSendNotification);
    m_cueBFader.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    m_cueBFader.setColour(juce::Slider::trackColourId, juce::Colour(0xff1f6feb));
    addAndMakeVisible(m_cueBFader);

    m_cueBLabel.setFont(juce::FontOptions(9.0f));
    m_cueBLabel.setColour(juce::Label::textColourId, juce::Colour(0xff8b949e));
    addAndMakeVisible(m_cueBLabel);
}

void MasterStripComponent::updateMeter(const engine::MeterReading& meter, const bool limiterActive)
{
    m_meter = meter;
    m_limiterActive = limiterActive;
    m_limiterButton.setToggleState(limiterActive, juce::dontSendNotification);
    if (limiterActive)
        m_limiterButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour::fromRGB(40, 160, 220));

    // Decay falloff
    constexpr float decay = 0.85f;
    m_displayPeakL = std::max(meter.peakLeft, m_displayPeakL * decay);
    m_displayPeakR = std::max(meter.peakRight, m_displayPeakR * decay);
    m_displayRmsL = std::max(meter.rmsLeft, m_displayRmsL * decay);
    m_displayRmsR = std::max(meter.rmsRight, m_displayRmsR * decay);

    repaint();
}

void MasterStripComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(28, 30, 36));
    g.setColour(juce::Colour::fromRGB(60, 64, 74));
    g.drawRect(getLocalBounds(), 1);

    auto meterArea = getLocalBounds();
    meterArea.removeFromTop(44); // room for label and limiter button

    // Bottom section: Phase Correlation & LUFS Radar Readout
    auto bottomArea = meterArea.removeFromBottom(36).reduced(4, 2);
    auto lufsBadge = bottomArea.removeFromTop(16);
    g.setColour(juce::Colour::fromRGB(36, 40, 50));
    g.fillRoundedRectangle(lufsBadge.toFloat(), 2.0f);
    g.setColour(juce::Colour::fromRGB(120, 220, 255));
    g.setFont(juce::FontOptions(9.5f).withStyle("Bold"));
    g.drawText(juce::String(m_lufs, 1) + " LUFS", lufsBadge, juce::Justification::centred);

    // Phase Correlation Bar (-1 to +1)
    auto corrBar = bottomArea.reduced(2, 2);
    g.setColour(juce::Colour::fromRGB(20, 22, 28));
    g.fillRect(corrBar);
    const float midX = corrBar.toFloat().getCentreX();
    const float corrNorm = std::clamp(m_correlation, -1.0f, 1.0f);
    const float corrWidth = (corrBar.getWidth() * 0.48f) * corrNorm;
    g.setColour(corrNorm >= 0.0f ? juce::Colour::fromRGB(50, 200, 100) : juce::Colour::fromRGB(240, 60, 60));
    if (corrNorm >= 0.0f)
        g.fillRect(juce::Rectangle<float>(midX, static_cast<float>(corrBar.getY()), corrWidth, static_cast<float>(corrBar.getHeight())));
    else
        g.fillRect(juce::Rectangle<float>(midX + corrWidth, static_cast<float>(corrBar.getY()), -corrWidth, static_cast<float>(corrBar.getHeight())));

    meterArea.reduce(8, 4);

    const int grWidth = (m_meter.gainReductionDb > 0.1f) ? 4 : 0;
    const int barWidth = (meterArea.getWidth() - 4 - grWidth) / 2;

    auto leftBar = meterArea.removeFromLeft(barWidth);
    meterArea.removeFromLeft(2);
    if (grWidth > 0)
    {
        auto grBar = meterArea.removeFromLeft(grWidth);
        meterArea.removeFromLeft(2);
        const float clampedGr = std::clamp(m_meter.gainReductionDb, 0.0f, 24.0f);
        const int grH = static_cast<int>((clampedGr / 24.0f) * static_cast<float>(grBar.getHeight()));
        g.setColour(juce::Colour::fromRGB(255, 90, 40));
        g.fillRect(grBar.getX(), grBar.getY(), grBar.getWidth(), grH);
    }
    else
    {
        meterArea.removeFromLeft(2);
    }
    auto rightBar = meterArea.removeFromLeft(barWidth);

    drawMeterBar(g, leftBar, m_displayPeakL, m_displayRmsL);
    drawMeterBar(g, rightBar, m_displayPeakR, m_displayRmsR);
}

void MasterStripComponent::drawMeterBar(juce::Graphics& g, juce::Rectangle<int> barBounds, float peak, float rms)
{
    g.setColour(juce::Colour::fromRGB(18, 19, 22));
    g.fillRect(barBounds);

    const int totalHeight = barBounds.getHeight();
    if (totalHeight <= 0)
        return;

    const float clampedRms = std::clamp(rms, 0.0f, 1.5f);
    const float clampedPeak = std::clamp(peak, 0.0f, 1.5f);

    const int rmsHeight = static_cast<int>(std::round((clampedRms / 1.0f) * static_cast<float>(totalHeight)));
    const int peakY = barBounds.getBottom() - static_cast<int>(std::round((clampedPeak / 1.0f) * static_cast<float>(totalHeight)));

    auto rmsFill = barBounds.removeFromBottom(std::min(totalHeight, rmsHeight));
    if (! rmsFill.isEmpty())
    {
        if (clampedRms > 1.0f)
            g.setColour(juce::Colour::fromRGB(220, 50, 50));
        else if (clampedRms > 0.5f)
            g.setColour(juce::Colour::fromRGB(220, 180, 40));
        else
            g.setColour(juce::Colour::fromRGB(50, 190, 80));

        g.fillRect(rmsFill);
    }

    if (peakY >= barBounds.getY() && peakY <= barBounds.getBottom())
    {
        g.setColour(clampedPeak > 1.0f ? juce::Colour::fromRGB(255, 60, 60) : juce::Colours::white);
        g.drawHorizontalLine(peakY, static_cast<float>(barBounds.getX()), static_cast<float>(barBounds.getRight()));
    }
}

void MasterStripComponent::resized()
{
    auto area = getLocalBounds().reduced(3, 4);
    m_nameLabel.setBounds(area.removeFromTop(18));
    area.removeFromTop(2);
    m_limiterButton.setBounds(area.removeFromTop(20).reduced(2, 0));
    area.removeFromTop(6);

    // Cue Mix Section at bottom of master strip
    auto cueArea = area.removeFromBottom(62);
    m_cueLabel.setBounds(cueArea.removeFromTop(14));
    auto cueARow = cueArea.removeFromTop(22);
    m_cueALabel.setBounds(cueARow.removeFromLeft(28));
    m_cueAFader.setBounds(cueARow);
    auto cueBRow = cueArea.removeFromTop(22);
    m_cueBLabel.setBounds(cueBRow.removeFromLeft(28));
    m_cueBFader.setBounds(cueBRow);
}

// ============================================================================
// ModulatorRackComponent
// ============================================================================

ModulatorRackComponent::ModulatorRackComponent()
{
    const std::array<std::pair<std::string, juce::Colour>, 8> config {{
        {"Cutoff", juce::Colour::fromRGB(0, 229, 255)},
        {"Res",    juce::Colour::fromRGB(255, 145, 0)},
        {"Drive",  juce::Colour::fromRGB(255, 82, 82)},
        {"Space",  juce::Colour::fromRGB(179, 136, 255)},
        {"Decay",  juce::Colour::fromRGB(105, 240, 174)},
        {"Tone",   juce::Colour::fromRGB(255, 215, 64)},
        {"Width",  juce::Colour::fromRGB(68, 138, 255)},
        {"Morph",  juce::Colour::fromRGB(255, 64, 129)}
    }};

    for (std::size_t i = 0; i < 8; ++i)
    {
        auto& m = m_macros[i];
        m.name = config[i].first;
        m.color = config[i].second;

        m.slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        m.slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        m.slider.setRange(0.0, 1.0, 0.01);
        m.slider.setValue(0.5, juce::dontSendNotification);
        addAndMakeVisible(m.slider);

        m.label.setText(m.name, juce::dontSendNotification);
        m.label.setFont(juce::FontOptions(9.5f).withStyle("Bold"));
        m.label.setJustificationType(juce::Justification::centred);
        m.label.setColour(juce::Label::textColourId, juce::Colour::fromRGB(160, 165, 175));
        addAndMakeVisible(m.label);
    }

    m_lfoShapeCombo.addItem("LFO: Sine", 1);
    m_lfoShapeCombo.addItem("LFO: Triangle", 2);
    m_lfoShapeCombo.addItem("LFO: Saw", 3);
    m_lfoShapeCombo.addItem("LFO: Square", 4);
    m_lfoShapeCombo.addItem("LFO: S&H", 5);
    m_lfoShapeCombo.setSelectedId(1, juce::dontSendNotification);
    addAndMakeVisible(m_lfoShapeCombo);

    m_lfoRateCombo.addItem("1/4", 1);
    m_lfoRateCombo.addItem("1/8", 2);
    m_lfoRateCombo.addItem("1/16", 3);
    m_lfoRateCombo.setSelectedId(2, juce::dontSendNotification);
    addAndMakeVisible(m_lfoRateCombo);

    startTimerHz(25);
}

ModulatorRackComponent::~ModulatorRackComponent()
{
    stopTimer();
}

void ModulatorRackComponent::timerCallback()
{
    m_lfoPhase += 0.08f;
    if (m_lfoPhase > juce::MathConstants<float>::twoPi)
        m_lfoPhase -= juce::MathConstants<float>::twoPi;
    repaint();
}

void ModulatorRackComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(22, 23, 27));
    g.setColour(juce::Colour::fromRGB(45, 48, 56));
    g.drawRect(getLocalBounds(), 1);

    auto bounds = getLocalBounds();
    auto titleArea = bounds.removeFromTop(20);
    g.setColour(juce::Colour::fromRGB(30, 33, 38));
    g.fillRect(titleArea);
    g.setColour(juce::Colour::fromRGB(200, 205, 215));
    g.setFont(juce::FontOptions(10.0f).withStyle("Bold"));
    g.drawText("MACROS & MODULATORS", titleArea, juce::Justification::centred);

    // Draw animated LFO visualizer waveform at the bottom
    const auto lfoBox = bounds.removeFromBottom(34).reduced(6, 2);
    g.setColour(juce::Colour::fromRGB(16, 17, 20));
    g.fillRoundedRectangle(lfoBox.toFloat(), 3.0f);
    g.setColour(juce::Colour::fromRGB(40, 44, 52));
    g.drawRoundedRectangle(lfoBox.toFloat(), 3.0f, 1.0f);

    juce::Path p;
    const float midY = static_cast<float>(lfoBox.getCentreY());
    const float amp = static_cast<float>(lfoBox.getHeight()) * 0.38f;
    bool first = true;
    for (int x = lfoBox.getX(); x < lfoBox.getRight(); ++x)
    {
        const float t = static_cast<float>(x - lfoBox.getX()) / static_cast<float>(lfoBox.getWidth());
        const float y = midY + std::sin(t * juce::MathConstants<float>::twoPi * 2.0f + m_lfoPhase) * amp;
        if (first) { p.startNewSubPath(static_cast<float>(x), y); first = false; }
        else p.lineTo(static_cast<float>(x), y);
    }
    g.setColour(juce::Colour::fromRGB(0, 229, 255));
    g.strokePath(p, juce::PathStrokeType(1.5f));

    // Animated traveling phase dot
    const float dotX = static_cast<float>(lfoBox.getX()) + (m_lfoPhase / juce::MathConstants<float>::twoPi) * static_cast<float>(lfoBox.getWidth());
    const float dotY = midY + std::sin((dotX - static_cast<float>(lfoBox.getX())) / static_cast<float>(lfoBox.getWidth()) * juce::MathConstants<float>::twoPi * 2.0f + m_lfoPhase) * amp;
    g.setColour(juce::Colour::fromRGB(255, 235, 100));
    g.fillEllipse(dotX - 2.5f, dotY - 2.5f, 5.0f, 5.0f);
}

void ModulatorRackComponent::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop(20); // title
    auto lfoArea = area.removeFromBottom(58);

    // LFO controls row
    auto lfoControls = lfoArea.removeFromTop(22).reduced(4, 1);
    m_lfoShapeCombo.setBounds(lfoControls.removeFromLeft(lfoControls.getWidth() - 60));
    m_lfoRateCombo.setBounds(lfoControls.withTrimmedLeft(4));

    // 8 macros in 2 rows of 4
    const int colW = area.getWidth() / 4;
    const int rowH = area.getHeight() / 2;

    for (std::size_t i = 0; i < 8; ++i)
    {
        const int r = static_cast<int>(i / 4);
        const int c = static_cast<int>(i % 4);
        auto cell = juce::Rectangle<int>(c * colW, area.getY() + r * rowH, colW, rowH).reduced(2, 1);
        m_macros[i].label.setBounds(cell.removeFromBottom(12));
        m_macros[i].slider.setBounds(cell);
    }
}

// ============================================================================
// MixerPanel
// ============================================================================

MixerPanel::MixerPanel(CommandHandler handler, LimiterCallback limiterCb)
    : m_handler(std::move(handler))
    , m_masterStrip(std::move(limiterCb))
{
    m_viewport.setViewedComponent(&m_tracksContainer, false);
    m_viewport.setScrollBarsShown(false, true);
    addAndMakeVisible(m_viewport);
    addAndMakeVisible(m_masterStrip);

    m_toggleModulatorsBtn.onClick = [this] { toggleModulatorRack(); };
    m_toggleModulatorsBtn.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(38, 42, 50));
    addAndMakeVisible(m_toggleModulatorsBtn);
    addAndMakeVisible(m_modulatorRack);

    // Mixer Scene Snapshots (Mix A vs Mix B)
    m_sceneAButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff238636));
    m_sceneAButton.onClick = [this] { switchScene(false); };
    addAndMakeVisible(m_sceneAButton);

    m_sceneBButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff21262d));
    m_sceneBButton.onClick = [this] { switchScene(true); };
    addAndMakeVisible(m_sceneBButton);

    m_copySceneButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff30363d));
    m_copySceneButton.onClick = [this] { copyActiveSceneToOpposite(); };
    addAndMakeVisible(m_copySceneButton);

    // VCA Spill Controls
    m_vcaAllButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1f6feb));
    m_vcaAllButton.onClick = [this] { setVcaSpill(0); };
    addAndMakeVisible(m_vcaAllButton);

    for (int v = 0; v < 8; ++v)
    {
        m_vcaSpillButtons[static_cast<std::size_t>(v)].setButtonText("V" + std::to_string(v + 1));
        m_vcaSpillButtons[static_cast<std::size_t>(v)].setColour(juce::TextButton::buttonColourId, juce::Colour(0xff21262d));
        m_vcaSpillButtons[static_cast<std::size_t>(v)].onClick = [this, v] { setVcaSpill(v + 1); };
        addAndMakeVisible(m_vcaSpillButtons[static_cast<std::size_t>(v)]);
    }

    m_spillStatusLabel.setFont(juce::FontOptions(10.0f).withStyle("Bold"));
    m_spillStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffa371f7));
    addAndMakeVisible(m_spillStatusLabel);
}

void MixerPanel::captureCurrentScene(MixerScene& target)
{
    target.channels.clear();
    target.channels.reserve(m_strips.size());
    for (const auto& strip : m_strips)
    {
        target.channels.push_back(ChannelMixSnapshot {
            .trackId = strip->trackId(),
            .gainLinear = strip->gainLinear(),
            .pan = strip->pan(),
            .muted = strip->isMuted(),
            .solo = strip->isSolo(),
            .phaseInvert = strip->isPhaseInvert()
        });
    }
}

void MixerPanel::applyScene(const MixerScene& scene)
{
    if (! m_handler) return;

    for (const auto& ch : scene.channels)
    {
        m_handler(project::SetTrackGainPan { ch.trackId, ch.gainLinear, ch.pan, ch.phaseInvert });
        m_handler(project::SetTrackMute { ch.trackId, ch.muted });
        m_handler(project::SetTrackSolo { ch.trackId, ch.solo });
    }
}

void MixerPanel::switchScene(const bool toSceneB)
{
    if (toSceneB == m_activeSceneIsB) return;

    // Capture current active scene first
    if (m_activeSceneIsB) {
        captureCurrentScene(m_sceneB);
    } else {
        captureCurrentScene(m_sceneA);
    }

    m_activeSceneIsB = toSceneB;

    if (m_activeSceneIsB) {
        if (!m_sceneB.channels.empty()) applyScene(m_sceneB);
    } else {
        if (!m_sceneA.channels.empty()) applyScene(m_sceneA);
    }

    m_sceneAButton.setColour(juce::TextButton::buttonColourId, !m_activeSceneIsB ? juce::Colour(0xff238636) : juce::Colour(0xff21262d));
    m_sceneBButton.setColour(juce::TextButton::buttonColourId, m_activeSceneIsB ? juce::Colour(0xff238636) : juce::Colour(0xff21262d));
    m_copySceneButton.setButtonText(m_activeSceneIsB ? "B -> A" : "A -> B");
}

void MixerPanel::copyActiveSceneToOpposite()
{
    if (m_activeSceneIsB) {
        captureCurrentScene(m_sceneB);
        m_sceneA = m_sceneB;
    } else {
        captureCurrentScene(m_sceneA);
        m_sceneB = m_sceneA;
    }
}

void MixerPanel::setVcaSpill(const int vcaGroup)
{
    m_vcaSpillGroup = std::clamp(vcaGroup, 0, 8);

    m_vcaAllButton.setColour(juce::TextButton::buttonColourId,
                             m_vcaSpillGroup == 0 ? juce::Colour(0xff1f6feb) : juce::Colour(0xff21262d));

    for (int v = 0; v < 8; ++v)
    {
        const bool active = (m_vcaSpillGroup == (v + 1));
        m_vcaSpillButtons[static_cast<std::size_t>(v)].setColour(
            juce::TextButton::buttonColourId,
            active ? juce::Colour(0xffa371f7) : juce::Colour(0xff21262d));
    }

    if (m_vcaSpillGroup == 0)
    {
        m_spillStatusLabel.setText("", juce::dontSendNotification);
    }
    else
    {
        m_spillStatusLabel.setText("SPILL: VCA " + juce::String(m_vcaSpillGroup), juce::dontSendNotification);
    }

    resized();
}

void MixerPanel::toggleModulatorRack()
{
    m_showModulators = ! m_showModulators;
    m_modulatorRack.setVisible(m_showModulators);
    m_toggleModulatorsBtn.setColour(juce::TextButton::buttonColourId,
                                   m_showModulators ? juce::Colour::fromRGB(45, 95, 140) : juce::Colour::fromRGB(38, 42, 50));
    resized();
}

void MixerPanel::setViewModel(const EditorViewModel& model)
{
    const auto& tracks = model.project.tracks;
    while (m_strips.size() < tracks.size())
    {
        auto strip = std::make_unique<ChannelStripComponent>(m_handler);
        m_tracksContainer.addAndMakeVisible(strip.get());
        m_strips.push_back(std::move(strip));
    }
    while (m_strips.size() > tracks.size())
    {
        m_strips.pop_back();
    }

    for (std::size_t i = 0; i < tracks.size(); ++i)
    {
        m_strips[i]->updateTrack(tracks[i]);
    }

    m_masterStrip.updateMeter(model.masterMeter, model.masterLimiterEnabled);
    resized();
}

void MixerPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(24, 25, 29));
    g.setColour(juce::Colour::fromRGB(40, 42, 50));
    g.drawHorizontalLine(0, 0.0f, static_cast<float>(getWidth()));
}

void MixerPanel::resized()
{
    auto area = getLocalBounds();

    // Scene & VCA Spill Top Toolbar
    auto toolbar = area.removeFromTop(24).reduced(4, 2);
    m_sceneAButton.setBounds(toolbar.removeFromLeft(48).reduced(1));
    m_sceneBButton.setBounds(toolbar.removeFromLeft(48).reduced(1));
    m_copySceneButton.setBounds(toolbar.removeFromLeft(54).reduced(1));
    toolbar.removeFromLeft(8);

    m_vcaAllButton.setBounds(toolbar.removeFromLeft(36).reduced(1));
    for (int v = 0; v < 8; ++v)
    {
        m_vcaSpillButtons[static_cast<std::size_t>(v)].setBounds(toolbar.removeFromLeft(28).reduced(1));
    }
    m_spillStatusLabel.setBounds(toolbar.removeFromLeft(120).reduced(2, 0));

    constexpr int masterWidth = 84;
    auto masterArea = area.removeFromRight(masterWidth);
    m_toggleModulatorsBtn.setBounds(masterArea.removeFromTop(20).reduced(2, 1));
    m_masterStrip.setBounds(masterArea);

    if (m_showModulators)
    {
        constexpr int modWidth = 220;
        m_modulatorRack.setBounds(area.removeFromRight(modWidth));
    }

    m_viewport.setBounds(area);

    constexpr int stripWidth = 76;
    int visibleCount = 0;
    for (auto& strip : m_strips)
    {
        const bool visible = (m_vcaSpillGroup == 0 || strip->vcaGroup() == m_vcaSpillGroup);
        strip->setVisible(visible);
        if (visible)
        {
            strip->setBounds(visibleCount * stripWidth, 0, stripWidth, m_tracksContainer.getHeight());
            ++visibleCount;
        }
    }

    const int totalWidth = visibleCount * stripWidth;
    m_tracksContainer.setBounds(0, 0, std::max(totalWidth, m_viewport.getWidth()), m_viewport.getHeight());
}

} // namespace dawg::ui
