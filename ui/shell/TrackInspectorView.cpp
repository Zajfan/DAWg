#include "TrackInspectorView.h"
#include "MidiLearnManager.h"

#include <cmath>

namespace dawg::ui {

namespace {
const project::Track* findTrackInModel(const EditorViewModel& model, std::optional<project::TrackId> trkId)
{
    if (! trkId.has_value()) return nullptr;
    for (const auto& t : model.project.tracks)
    {
        if (t.id == *trkId) return &t;
    }
    return nullptr;
}
} // namespace

TrackInspectorView::TrackInspectorView(CommandHandler commandHandler, OpenDspEditorCallback openDspEditorCb)
    : m_commandHandler(std::move(commandHandler))
    , m_openDspEditorCb(std::move(openDspEditorCb))
{
    // Track Name & Kind
    m_lblTrackName.setFont(juce::FontOptions {14.0f}.withStyle("Bold"));
    m_lblTrackName.setColour(juce::Label::textColourId, juce::Colours::white);
    m_lblTrackName.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(m_lblTrackName);

    m_lblTrackKind.setFont(juce::FontOptions {10.0f});
    m_lblTrackKind.setColour(juce::Label::textColourId, juce::Colour::fromRGB(160, 165, 175));
    m_lblTrackKind.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(m_lblTrackKind);

    m_lblTrackDspLoad.setFont(juce::FontOptions {10.0f});
    m_lblTrackDspLoad.setColour(juce::Label::textColourId, juce::Colour::fromRGB(120, 200, 160));
    m_lblTrackDspLoad.setJustificationType(juce::Justification::centredLeft);
    m_lblTrackDspLoad.setText("DSP: 0 us", juce::dontSendNotification);
    addAndMakeVisible(m_lblTrackDspLoad);

    // Input & Monitoring
    m_inputSelector.addItem("In: 1/2 Stereo", 1);
    m_inputSelector.addItem("In: 1 (L)", 2);
    m_inputSelector.addItem("In: 2 (R)", 3);
    m_inputSelector.addItem("In: MIDI All", 4);
    m_inputSelector.setSelectedId(1, juce::dontSendNotification);
    addAndMakeVisible(m_inputSelector);

    m_btnMonitor.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(36, 40, 50));
    m_btnMonitor.onClick = [this] {
        m_monitorMode = (m_monitorMode + 1) % 3;
        if (m_monitorMode == 0) m_btnMonitor.setButtonText("Mon: Off");
        else if (m_monitorMode == 1) m_btnMonitor.setButtonText("Mon: Auto");
        else m_btnMonitor.setButtonText("Mon: In");
    };
    addAndMakeVisible(m_btnMonitor);

    // Track Controls (REC, S, M, Phase)
    m_btnArm.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(45, 48, 56));
    m_btnArm.onClick = [this] {
        if (! m_selectedTrackId.has_value() || ! m_commandHandler || m_updatingControls) return;
        const auto* trk = findTrackInModel(m_model, m_selectedTrackId);
        if (trk != nullptr)
        {
            m_commandHandler(project::SetTrackRecordArm { *m_selectedTrackId, ! trk->recordArmed });
        }
    };
    addAndMakeVisible(m_btnArm);

    m_btnSolo.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(45, 48, 56));
    m_btnSolo.onClick = [this] {
        if (! m_selectedTrackId.has_value() || ! m_commandHandler || m_updatingControls) return;
        const auto* trk = findTrackInModel(m_model, m_selectedTrackId);
        if (trk != nullptr)
        {
            m_commandHandler(project::SetTrackSolo { *m_selectedTrackId, ! trk->solo });
        }
    };
    addAndMakeVisible(m_btnSolo);

    m_btnMute.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(45, 48, 56));
    m_btnMute.onClick = [this] {
        if (! m_selectedTrackId.has_value() || ! m_commandHandler || m_updatingControls) return;
        const auto* trk = findTrackInModel(m_model, m_selectedTrackId);
        if (trk != nullptr)
        {
            m_commandHandler(project::SetTrackMute { *m_selectedTrackId, ! trk->muted });
        }
    };
    addAndMakeVisible(m_btnMute);

    m_btnPolarity.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(45, 48, 56));
    m_btnPolarity.onClick = [this] {
        if (! m_selectedTrackId.has_value() || ! m_commandHandler || m_updatingControls) return;
        const auto* trk = findTrackInModel(m_model, m_selectedTrackId);
        if (trk != nullptr)
        {
            m_commandHandler(project::SetTrackGainPan { *m_selectedTrackId, trk->gainLinear, trk->pan, ! trk->phaseInvert });
        }
    };
    addAndMakeVisible(m_btnPolarity);

    // Volume Fader & DB Label
    m_faderVolume.setSliderStyle(juce::Slider::LinearVertical);
    m_faderVolume.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    m_faderVolume.setRange(0.0, 1.414, 0.001); // Up to +3 dB
    m_faderVolume.setValue(1.0, juce::dontSendNotification);
    m_faderVolume.onValueChange = [this] {
        if (! m_selectedTrackId.has_value() || ! m_commandHandler || m_updatingControls) return;
        const float linear = static_cast<float>(m_faderVolume.getValue());
        const auto* trk = findTrackInModel(m_model, m_selectedTrackId);
        const float pan = trk != nullptr ? trk->pan : 0.0f;
        const bool phase = trk != nullptr ? trk->phaseInvert : false;
        m_commandHandler(project::SetTrackGainPan { *m_selectedTrackId, linear, pan, phase });
        const float db = (linear > 0.0001f) ? (20.0f * std::log10(linear)) : -100.0f;
        m_lblVolumeDb.setText((db <= -60.0f) ? "-inf dB" : juce::String(db, 1) + " dB", juce::dontSendNotification);
    };
    m_faderVolume.onContextMenu = [this](const juce::MouseEvent&) {
        if (! m_selectedTrackId.has_value()) return;
        const auto trkId = *m_selectedTrackId;
        juce::PopupMenu menu;
        menu.addItem(1, "Learn MIDI CC (Volume)");
        menu.addItem(2, "Reset to 0 dB (1.0)");
        menu.showMenuAsync(juce::PopupMenu::Options {}, [this, trkId](int result) {
            if (result == 1)
            {
                const std::uint32_t targetId = static_cast<std::uint32_t>(trkId * 10 + 1);
                MidiLearnManager::instance().registerParameter(
                    targetId,
                    "Track " + std::to_string(trkId) + " Volume",
                    [this, trkId](float normVal) {
                        const float linear = normVal * 1.414f;
                        const auto* trk = findTrackInModel(m_model, trkId);
                        const float pan = trk != nullptr ? trk->pan : 0.0f;
                        const bool phase = trk != nullptr ? trk->phaseInvert : false;
                        if (m_commandHandler)
                        {
                            m_commandHandler(project::SetTrackGainPan { trkId, linear, pan, phase });
                        }
                    }
                );
                MidiLearnManager::instance().startLearning(targetId);
            }
            else if (result == 2)
            {
                const auto* trk = findTrackInModel(m_model, trkId);
                const float pan = trk != nullptr ? trk->pan : 0.0f;
                const bool phase = trk != nullptr ? trk->phaseInvert : false;
                if (m_commandHandler)
                {
                    m_commandHandler(project::SetTrackGainPan { trkId, 1.0f, pan, phase });
                }
            }
        });
    };
    addAndMakeVisible(m_faderVolume);

    m_lblVolumeDb.setFont(juce::FontOptions {10.0f});
    m_lblVolumeDb.setColour(juce::Label::textColourId, juce::Colour::fromRGB(180, 190, 205));
    m_lblVolumeDb.setJustificationType(juce::Justification::centred);
    m_lblVolumeDb.setText("0.0 dB", juce::dontSendNotification);
    addAndMakeVisible(m_lblVolumeDb);

    // Pan Knob & Label
    m_knobPan.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    m_knobPan.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    m_knobPan.setRange(-1.0, 1.0, 0.01);
    m_knobPan.setValue(0.0, juce::dontSendNotification);
    m_knobPan.onValueChange = [this] {
        if (! m_selectedTrackId.has_value() || ! m_commandHandler || m_updatingControls) return;
        const float pan = static_cast<float>(m_knobPan.getValue());
        const auto* trk = findTrackInModel(m_model, m_selectedTrackId);
        const float gain = trk != nullptr ? trk->gainLinear : 1.0f;
        const bool phase = trk != nullptr ? trk->phaseInvert : false;
        m_commandHandler(project::SetTrackGainPan { *m_selectedTrackId, gain, pan, phase });
        if (std::abs(pan) < 0.02f) m_lblPan.setText("Pan: C", juce::dontSendNotification);
        else if (pan < 0.0f) m_lblPan.setText("L " + juce::String(static_cast<int>(-pan * 100)), juce::dontSendNotification);
        else m_lblPan.setText("R " + juce::String(static_cast<int>(pan * 100)), juce::dontSendNotification);
    };
    m_knobPan.onContextMenu = [this](const juce::MouseEvent&) {
        if (! m_selectedTrackId.has_value()) return;
        const auto trkId = *m_selectedTrackId;
        juce::PopupMenu menu;
        menu.addItem(1, "Learn MIDI CC (Pan)");
        menu.addItem(2, "Reset to Center (0.0)");
        menu.showMenuAsync(juce::PopupMenu::Options {}, [this, trkId](int result) {
            if (result == 1)
            {
                const std::uint32_t targetId = static_cast<std::uint32_t>(trkId * 10 + 2);
                MidiLearnManager::instance().registerParameter(
                    targetId,
                    "Track " + std::to_string(trkId) + " Pan",
                    [this, trkId](float normVal) {
                        const float pan = normVal * 2.0f - 1.0f;
                        const auto* trk = findTrackInModel(m_model, trkId);
                        const float gain = trk != nullptr ? trk->gainLinear : 1.0f;
                        const bool phase = trk != nullptr ? trk->phaseInvert : false;
                        if (m_commandHandler)
                        {
                            m_commandHandler(project::SetTrackGainPan { trkId, gain, pan, phase });
                        }
                    }
                );
                MidiLearnManager::instance().startLearning(targetId);
            }
            else if (result == 2)
            {
                const auto* trk = findTrackInModel(m_model, trkId);
                const float gain = trk != nullptr ? trk->gainLinear : 1.0f;
                const bool phase = trk != nullptr ? trk->phaseInvert : false;
                if (m_commandHandler)
                {
                    m_commandHandler(project::SetTrackGainPan { trkId, gain, 0.0f, phase });
                }
            }
        });
    };
    addAndMakeVisible(m_knobPan);

    m_lblPan.setFont(juce::FontOptions {10.0f});
    m_lblPan.setColour(juce::Label::textColourId, juce::Colour::fromRGB(180, 190, 205));
    m_lblPan.setJustificationType(juce::Justification::centred);
    m_lblPan.setText("Pan: C", juce::dontSendNotification);
    addAndMakeVisible(m_lblPan);

    // Output Destination
    m_outputSelector.addItem("Out: Master Bus", 1);
    m_outputSelector.addItem("Out: Bus A (Drums)", 2);
    m_outputSelector.addItem("Out: Bus B (FX)", 3);
    m_outputSelector.addItem("Out: Direct HW 1/2", 4);
    m_outputSelector.setSelectedId(1, juce::dontSendNotification);
    addAndMakeVisible(m_outputSelector);

    // DSP Header & Add Button
    m_lblDspHeader.setFont(juce::FontOptions {10.0f}.withStyle("Bold"));
    m_lblDspHeader.setColour(juce::Label::textColourId, juce::Colour::fromRGB(140, 150, 165));
    m_lblDspHeader.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(m_lblDspHeader);

    m_btnAddDsp.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(50, 55, 68));
    m_btnAddDsp.onClick = [this] { showAddDspMenu(); };
    addAndMakeVisible(m_btnAddDsp);

    addAndMakeVisible(m_dspContainer);
}

void TrackInspectorView::setViewModel(const EditorViewModel& model)
{
    m_model = model;
    if (! m_selectedTrackId.has_value() && ! m_model.project.tracks.empty())
    {
        m_selectedTrackId = m_model.project.tracks.front().id;
    }
    updateFromSelectedTrack();
}

void TrackInspectorView::setSelectedTrackId(std::optional<project::TrackId> trackId)
{
    m_selectedTrackId = trackId;
    updateFromSelectedTrack();
}

void TrackInspectorView::updateFromSelectedTrack()
{
    m_updatingControls = true;

    if (! m_selectedTrackId.has_value())
    {
        m_lblTrackName.setText("No Track Selected", juce::dontSendNotification);
        m_lblTrackKind.setText("-", juce::dontSendNotification);
        m_updatingControls = false;
        repaint();
        return;
    }

    const auto* trk = findTrackInModel(m_model, m_selectedTrackId);
    if (trk == nullptr)
    {
        m_updatingControls = false;
        return;
    }

    m_lblTrackName.setText(trk->name.empty() ? ("Track " + juce::String(trk->id)) : juce::String(trk->name), juce::dontSendNotification);

    juce::String kindStr;
    switch (trk->kind)
    {
    case project::TrackKind::audio: kindStr = "AUDIO TRACK"; break;
    case project::TrackKind::instrument: kindStr = "INSTRUMENT / MIDI"; break;
    case project::TrackKind::auxReturn: kindStr = "AUX RETURN"; break;
    case project::TrackKind::submixGroup: kindStr = "SUBMIX GROUP"; break;
    case project::TrackKind::folder: kindStr = "FOLDER TRACK"; break;
    }
    m_lblTrackKind.setText(kindStr, juce::dontSendNotification);

    float trackUs = 0.0f;
    float trackPct = 0.0f;
    for (std::size_t i = 0; i < m_model.dspPerformance.activeTrackCount; ++i)
    {
        if (m_model.dspPerformance.tracks[i].trackId == trk->id)
        {
            trackUs = m_model.dspPerformance.tracks[i].executionUs;
            trackPct = m_model.dspPerformance.tracks[i].cpuPercent;
            break;
        }
    }
    m_lblTrackDspLoad.setText("DSP: " + juce::String(trackUs, 0) + " us (" + juce::String(trackPct, 1) + "%)", juce::dontSendNotification);

    // Track color (hash or default)
    const auto colIdx = static_cast<juce::uint8>((trk->id * 47) % 256);
    m_trackColor = juce::Colour::fromHSV(static_cast<float>(colIdx) / 256.0f, 0.75f, 0.9f, 1.0f);

    // Buttons
    m_btnArm.setColour(juce::TextButton::buttonColourId, trk->recordArmed ? juce::Colour::fromRGB(220, 40, 40) : juce::Colour::fromRGB(45, 48, 56));
    m_btnSolo.setColour(juce::TextButton::buttonColourId, trk->solo ? juce::Colour::fromRGB(230, 160, 20) : juce::Colour::fromRGB(45, 48, 56));
    m_btnMute.setColour(juce::TextButton::buttonColourId, trk->muted ? juce::Colour::fromRGB(60, 120, 220) : juce::Colour::fromRGB(45, 48, 56));
    m_btnPolarity.setColour(juce::TextButton::buttonColourId, trk->phaseInvert ? juce::Colour::fromRGB(180, 80, 220) : juce::Colour::fromRGB(45, 48, 56));

    // Volume & Pan
    m_faderVolume.setValue(trk->gainLinear, juce::dontSendNotification);
    const float db = (trk->gainLinear > 0.0001f) ? (20.0f * std::log10(trk->gainLinear)) : -100.0f;
    m_lblVolumeDb.setText((db <= -60.0f) ? "-inf dB" : juce::String(db, 1) + " dB", juce::dontSendNotification);

    m_knobPan.setValue(trk->pan, juce::dontSendNotification);
    if (std::abs(trk->pan) < 0.02f) m_lblPan.setText("Pan: C", juce::dontSendNotification);
    else if (trk->pan < 0.0f) m_lblPan.setText("L " + juce::String(static_cast<int>(-trk->pan * 100)), juce::dontSendNotification);
    else m_lblPan.setText("R " + juce::String(static_cast<int>(trk->pan * 100)), juce::dontSendNotification);

    // Update DSP rows
    m_dspRows.clear();
    m_dspContainer.removeAllChildren();

    for (std::size_t i = 0; i < trk->inserts.size(); ++i)
    {
        const auto& node = trk->inserts[i];
        DspSlotRow row;
        row.btnName = std::make_unique<juce::TextButton>(node.name.empty() ? ("Slot " + juce::String(i + 1)) : juce::String(node.name));
        row.btnName->setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(38, 42, 52));
        row.btnName->onClick = [this, trackId = trk->id, slotIdx = i] {
            if (m_openDspEditorCb) m_openDspEditorCb(trackId, slotIdx);
        };

        row.btnBypass = std::make_unique<juce::TextButton>(node.bypassed ? "BYP" : "ON");
        row.btnBypass->setColour(juce::TextButton::buttonColourId, node.bypassed ? juce::Colour::fromRGB(140, 50, 50) : juce::Colour::fromRGB(40, 160, 90));
        row.btnBypass->onClick = [this, trackId = trk->id, slotIdx = i, bypassed = node.bypassed] {
            if (m_commandHandler)
            {
                m_commandHandler(project::SetTrackInsertBypassed { trackId, static_cast<std::uint32_t>(slotIdx), ! bypassed });
            }
        };

        row.btnRemove = std::make_unique<juce::TextButton>("X");
        row.btnRemove->setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(55, 30, 35));
        row.btnRemove->setColour(juce::TextButton::textColourOffId, juce::Colour::fromRGB(220, 110, 110));
        row.btnRemove->onClick = [this, trackId = trk->id, slotIdx = i] {
            if (m_commandHandler)
            {
                m_commandHandler(project::RemoveTrackInsert { trackId, static_cast<std::uint32_t>(slotIdx) });
            }
        };

        m_dspContainer.addAndMakeVisible(*row.btnName);
        m_dspContainer.addAndMakeVisible(*row.btnBypass);
        m_dspContainer.addAndMakeVisible(*row.btnRemove);
        m_dspRows.push_back(std::move(row));
    }

    m_updatingControls = false;
    resized();
    repaint();
}

void TrackInspectorView::showAddDspMenu()
{
    if (! m_selectedTrackId.has_value() || ! m_commandHandler)
        return;

    juce::PopupMenu menu;
    menu.addItem(1, "Parametric EQ (6-Band)");
    menu.addItem(2, "Dynamics Compressor");
    menu.addItem(3, "Brickwall Limiter");
    menu.addItem(4, "TNHC Studio Reverb");
    menu.addItem(5, "TNHC Stereo Delay");
    menu.addItem(6, "Drum Pad Sampler");
    menu.addItem(7, "TNHC Poly Synth");
    menu.addItem(8, "ResoTamer Suppressor");
    menu.addItem(9, "StereoScope Imager");
    menu.addItem(10, "MasterPhase Dynamic EQ");
    menu.addItem(11, "Complex 259 Synth");
    menu.addItem(12, "Access Virus TI Polar Synth");
    menu.addItem(13, "Roland Juno-106 Analog Synth");
    menu.addItem(14, "Yamaha OmniFM 6-Op Synth");
    menu.addItem(15, "SoundFont / Multi-Sample Player");

    menu.showMenuAsync(juce::PopupMenu::Options {}, [this, trkId = *m_selectedTrackId](int res) {
        if (res <= 0 || ! m_commandHandler) return;
        project::DspNodeType type = project::DspNodeType::parametricEq;
        std::string name = "Parametric EQ";
        if (res == 2) { type = project::DspNodeType::dynamicsCompressor; name = "Compressor"; }
        else if (res == 3) { type = project::DspNodeType::brickwallLimiter; name = "Limiter"; }
        else if (res == 4) { type = project::DspNodeType::tnhcReverb; name = "Studio Reverb"; }
        else if (res == 5) { type = project::DspNodeType::tnhcDelay; name = "Stereo Delay"; }
        else if (res == 6) { type = project::DspNodeType::drumPadSampler; name = "Drum Sampler"; }
        else if (res == 7) { type = project::DspNodeType::tnhcSynth; name = "TNHC Synth"; }
        else if (res == 8) { type = project::DspNodeType::resonanceSuppressor; name = "ResoTamer Suppressor"; }
        else if (res == 9) { type = project::DspNodeType::multibandStereoImager; name = "StereoScope Imager"; }
        else if (res == 10) { type = project::DspNodeType::linearPhaseMasteringEq; name = "MasterPhase EQ"; }
        else if (res == 11) { type = project::DspNodeType::buchlaComplexSynth; name = "Complex 259 Synth"; }
        else if (res == 12) { type = project::DspNodeType::virusTiPolarSynth; name = "Access Virus TI Polar"; }
        else if (res == 13) { type = project::DspNodeType::juno106Synth; name = "Roland Juno-106"; }
        else if (res == 14) { type = project::DspNodeType::omniFmSynth; name = "Yamaha OmniFM"; }
        else if (res == 15) { type = project::DspNodeType::soundFontPlayer; name = "SoundFont Player"; }

        const auto* trk = findTrackInModel(m_model, trkId);
        const auto slot = static_cast<std::uint32_t>(trk != nullptr ? trk->inserts.size() : 0);
        m_commandHandler(project::AddTrackInsert {
            trkId,
            project::DspNodeDescriptor {
                .slotIndex = slot,
                .type = type,
                .bypassed = false,
                .name = name,
                .pluginId = "",
                .parameters = {}
            }
        });
    });
}

void TrackInspectorView::paint(juce::Graphics& g)
{
    // Background
    g.fillAll(juce::Colour::fromRGB(22, 24, 30));

    // Right border separator
    g.setColour(juce::Colour::fromRGB(36, 40, 50));
    g.drawVerticalLine(getWidth() - 1, 0.0f, static_cast<float>(getHeight()));

    // Track color header badge
    g.setColour(m_trackColor);
    g.fillRect(8, 8, 4, 30);

    // Meter bar next to fader
    const int faderX = m_faderVolume.getX();
    const int meterW = 6;
    const int meterX = faderX - meterW - 3;
    const int meterY = m_faderVolume.getY() + 4;
    const int meterH = m_faderVolume.getHeight() - 8;

    g.setColour(juce::Colour::fromRGB(15, 17, 22));
    g.fillRoundedRectangle(static_cast<float>(meterX), static_cast<float>(meterY), static_cast<float>(meterW), static_cast<float>(meterH), 1.5f);

    // Active level simulation
    const float faderNorm = std::clamp(static_cast<float>(m_faderVolume.getValue() / 1.414), 0.0f, 1.0f);
    const int fillH = static_cast<int>(faderNorm * static_cast<float>(meterH));
    if (fillH > 0)
    {
        const auto meterGrad = juce::ColourGradient::vertical(
            juce::Colour::fromRGB(240, 60, 60), static_cast<float>(meterY),
            juce::Colour::fromRGB(40, 200, 100), static_cast<float>(meterY + meterH));
        g.setGradientFill(meterGrad);
        g.fillRoundedRectangle(static_cast<float>(meterX), static_cast<float>(meterY + meterH - fillH), static_cast<float>(meterW), static_cast<float>(fillH), 1.5f);
    }
}

void TrackInspectorView::resized()
{
    const int w = getWidth();
    int y = 8;

    // Header (Track Name & Kind & DSP Load)
    m_lblTrackName.setBounds(18, y, w - 24, 18);
    y += 18;
    m_lblTrackKind.setBounds(18, y, w - 24, 14);
    y += 14;
    m_lblTrackDspLoad.setBounds(18, y, w - 24, 14);
    y += 18;

    // Input & Monitoring
    m_inputSelector.setBounds(8, y, w - 16, 20);
    y += 24;
    m_btnMonitor.setBounds(8, y, w - 16, 18);
    y += 24;

    // Track Controls (REC, S, M, Phase)
    const int btnW = (w - 28) / 4;
    m_btnArm.setBounds(8, y, btnW, 22);
    m_btnSolo.setBounds(8 + btnW + 4, y, btnW, 22);
    m_btnMute.setBounds(8 + (btnW + 4) * 2, y, btnW, 22);
    m_btnPolarity.setBounds(8 + (btnW + 4) * 3, y, btnW, 22);
    y += 28;

    // Output Destination
    m_outputSelector.setBounds(8, y, w - 16, 20);
    y += 26;

    // Pan & Volume Section
    m_knobPan.setBounds(w / 2 - 20, y, 40, 40);
    m_lblPan.setBounds(8, y + 40, w - 16, 14);
    y += 58;

    // Vertical Fader
    constexpr int faderHeight = 110;
    m_faderVolume.setBounds(w / 2 - 12, y, 32, faderHeight);
    m_lblVolumeDb.setBounds(8, y + faderHeight + 2, w - 16, 14);
    y += faderHeight + 20;

    // DSP Header & Add FX button
    m_lblDspHeader.setBounds(8, y, w - 50, 18);
    m_btnAddDsp.setBounds(w - 42, y, 34, 18);
    y += 22;

    // DSP Rows
    m_dspContainer.setBounds(8, y, w - 16, std::max(20, getHeight() - y - 8));
    int rowY = 0;
    const int contW = m_dspContainer.getWidth();
    for (auto& row : m_dspRows)
    {
        if (row.btnName && row.btnBypass)
        {
            if (row.btnRemove)
            {
                row.btnName->setBounds(0, rowY, contW - 56, 20);
                row.btnBypass->setBounds(contW - 52, rowY, 32, 20);
                row.btnRemove->setBounds(contW - 18, rowY, 18, 20);
            }
            else
            {
                row.btnName->setBounds(0, rowY, contW - 36, 20);
                row.btnBypass->setBounds(contW - 32, rowY, 32, 20);
            }
            rowY += 23;
        }
    }
}

} // namespace dawg::ui
