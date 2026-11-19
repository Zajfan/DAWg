#include "DeviceRackView.h"
#include "DspEditorDialog.h"
#include "VirusTiPolarEditorView.h"
#include "Juno106EditorView.h"
#include "OmniFmEditorView.h"

#include <algorithm>

namespace dawg::ui {

// ============================================================================
// DeviceCardComponent implementation
// ============================================================================
DeviceCardComponent::DeviceCardComponent(
    project::TrackId trackId,
    std::uint32_t slotIndex,
    project::DspNodeDescriptor descriptor,
    CommandHandler cmdHandler,
    OpenEditorCallback openEditorCb)
    : m_trackId(trackId)
    , m_slotIndex(slotIndex)
    , m_descriptor(std::move(descriptor))
    , m_cmdHandler(std::move(cmdHandler))
    , m_openEditorCb(std::move(openEditorCb))
{
    // Bypass Button
    m_btnBypass.setButtonText(m_descriptor.bypassed ? "BYP" : "ON");
    m_btnBypass.setColour(juce::TextButton::buttonColourId,
                          m_descriptor.bypassed ? juce::Colour::fromRGB(150, 45, 45)
                                                : juce::Colour::fromRGB(35, 140, 75));
    m_btnBypass.onClick = [this] {
        if (! m_cmdHandler) return;
        m_descriptor.bypassed = ! m_descriptor.bypassed;
        m_btnBypass.setButtonText(m_descriptor.bypassed ? "BYP" : "ON");
        m_btnBypass.setColour(juce::TextButton::buttonColourId,
                              m_descriptor.bypassed ? juce::Colour::fromRGB(150, 45, 45)
                                                    : juce::Colour::fromRGB(35, 140, 75));
        m_cmdHandler(project::SetTrackInsertBypassed {
            m_trackId,
            m_slotIndex,
            m_descriptor.bypassed
        });
        repaint();
    };
    addAndMakeVisible(m_btnBypass);

    // Title
    std::string title = m_descriptor.name.empty() ? ("Slot " + std::to_string(m_slotIndex + 1)) : m_descriptor.name;
    m_lblTitle.setText(title, juce::dontSendNotification);
    m_lblTitle.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    m_lblTitle.setColour(juce::Label::textColourId, juce::Colours::white);
    m_lblTitle.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(m_lblTitle);

    // Edit Button
    m_btnEdit.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(45, 75, 120));
    m_btnEdit.onClick = [this] {
        if (m_openEditorCb)
            m_openEditorCb(m_descriptor.type, m_lblTitle.getText().toStdString());
    };
    addAndMakeVisible(m_btnEdit);

    // Remove Button
    m_btnRemove.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(55, 30, 35));
    m_btnRemove.onClick = [this] {
        if (! m_cmdHandler) return;
        m_cmdHandler(project::RemoveTrackInsert { m_trackId, m_slotIndex });
    };
    addAndMakeVisible(m_btnRemove);

    // Configure 4 Quick-Tweak Parameter knobs
    struct ParamDef { const char* name; double minVal; double maxVal; double defVal; };
    std::vector<ParamDef> paramDefs;

    switch (m_descriptor.type)
    {
    case project::DspNodeType::parametricEq:
        paramDefs = { {"Low", -12.0, 12.0, 0.0}, {"Mid", -12.0, 12.0, 0.0}, {"High", -12.0, 12.0, 0.0}, {"Q", 0.5, 4.0, 1.0} };
        break;
    case project::DspNodeType::dynamicsCompressor:
        paramDefs = { {"Thresh", -40.0, 0.0, -14.0}, {"Ratio", 1.0, 20.0, 4.0}, {"Attack", 1.0, 100.0, 15.0}, {"Release", 10.0, 500.0, 120.0} };
        break;
    case project::DspNodeType::brickwallLimiter:
        paramDefs = { {"Ceiling", -12.0, 0.0, -0.3}, {"Release", 10.0, 500.0, 100.0}, {"Lookahead", 0.0, 10.0, 2.0}, {"Makeup", 0.0, 18.0, 0.0} };
        break;
    case project::DspNodeType::tnhcSynth:
    case project::DspNodeType::analogLadderSynth:
    case project::DspNodeType::vectorSequencer:
    case project::DspNodeType::physicalModelSynth:
    case project::DspNodeType::granularCloud:
    case project::DspNodeType::virusTiPolarSynth:
    case project::DspNodeType::juno106Synth:
    case project::DspNodeType::omniFmSynth:
    case project::DspNodeType::soundFontPlayer:
        paramDefs = { {"Cutoff", 20.0, 20000.0, 2500.0}, {"Reso", 0.0, 100.0, 30.0}, {"Attack", 1.0, 1000.0, 15.0}, {"Release", 10.0, 3000.0, 400.0} };
        break;
    case project::DspNodeType::resonanceSuppressor:
        paramDefs = { {"Depth", 0.0, 24.0, 6.0}, {"Sharp", 0.5, 5.0, 2.0}, {"ERB", 20.0, 20000.0, 2500.0}, {"Mix", 0.0, 100.0, 100.0} };
        break;
    case project::DspNodeType::multibandStereoImager:
        paramDefs = { {"Low", 0.0, 200.0, 100.0}, {"Mid", 0.0, 200.0, 100.0}, {"High", 0.0, 200.0, 130.0}, {"Width", 0.0, 200.0, 100.0} };
        break;
    case project::DspNodeType::linearPhaseMasteringEq:
        paramDefs = { {"Sub", -6.0, 6.0, 0.0}, {"Duck", 0.0, 12.0, 3.0}, {"Air", -6.0, 6.0, 1.0}, {"Ceiling", -12.0, 0.0, -0.3} };
        break;
    case project::DspNodeType::buchlaComplexSynth:
        paramDefs = { {"Fold", 0.0, 100.0, 40.0}, {"Timbre", 0.0, 100.0, 50.0}, {"LPG", 10.0, 1000.0, 250.0}, {"Warp", 0.0, 100.0, 20.0} };
        break;
    case project::DspNodeType::tnhcReverb:
    case project::DspNodeType::rayTracingReverb:
        paramDefs = { {"Decay", 0.2, 8.0, 2.2}, {"Damp", 0.0, 100.0, 30.0}, {"PreDly", 0.0, 100.0, 15.0}, {"Mix", 0.0, 100.0, 35.0} };
        break;
    case project::DspNodeType::tnhcDelay:
        paramDefs = { {"Time", 20.0, 1500.0, 375.0}, {"Fback", 0.0, 95.0, 45.0}, {"Tone", 500.0, 12000.0, 4500.0}, {"Mix", 0.0, 100.0, 30.0} };
        break;
    case project::DspNodeType::tubeTransformer:
    case project::DspNodeType::analogDegrader:
        paramDefs = { {"Drive", 0.0, 100.0, 25.0}, {"Bias", 0.0, 100.0, 50.0}, {"Tone", 1000.0, 15000.0, 8000.0}, {"Mix", 0.0, 100.0, 100.0} };
        break;
    case project::DspNodeType::drumPadSampler:
    case project::DspNodeType::clapPlugin:
        paramDefs = { {"Pitch", -24.0, 24.0, 0.0}, {"Decay", 10.0, 2000.0, 500.0}, {"Filter", 100.0, 20000.0, 20000.0}, {"Level", -30.0, 6.0, 0.0} };
        break;
    }

    for (std::size_t i = 0; i < paramDefs.size(); ++i)
    {
        const auto& def = paramDefs[i];
        auto qp = std::make_unique<QuickParam>();
        qp->label.setText(def.name, juce::dontSendNotification);
        qp->label.setFont(juce::FontOptions(9.0f));
        qp->label.setColour(juce::Label::textColourId, juce::Colour::fromRGB(160, 170, 185));
        qp->label.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(qp->label);

        qp->slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        qp->slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        qp->slider.setRange(def.minVal, def.maxVal, 0.1);

        double initialVal = def.defVal;
        if (i < m_descriptor.parameters.size())
            initialVal = static_cast<double>(m_descriptor.parameters[i]);
        qp->slider.setValue(initialVal, juce::dontSendNotification);

        qp->slider.onValueChange = [this, paramIdx = i] {
            if (paramIdx >= m_params.size()) return;
            const float val = static_cast<float>(m_params[paramIdx]->slider.getValue());
            if (m_descriptor.parameters.size() <= paramIdx)
                m_descriptor.parameters.resize(paramIdx + 1, 0.0f);
            m_descriptor.parameters[paramIdx] = val;
            m_descriptor.slotIndex = m_slotIndex;
            if (m_cmdHandler)
            {
                m_cmdHandler(project::AddTrackInsert {
                    m_trackId,
                    m_descriptor
                });
            }
        };
        addAndMakeVisible(qp->slider);

        m_params.push_back(std::move(qp));
    }
}

void DeviceCardComponent::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour(m_descriptor.bypassed ? juce::Colour::fromRGB(26, 28, 34) : juce::Colour::fromRGB(32, 36, 46));
    g.fillRoundedRectangle(b, 5.0f);

    g.setColour(juce::Colour::fromRGB(50, 56, 70));
    g.drawRoundedRectangle(b, 5.0f, 1.0f);

    // Header divider
    g.setColour(juce::Colour::fromRGB(45, 50, 62));
    g.drawHorizontalLine(26, 0.0f, static_cast<float>(getWidth()));
}

void DeviceCardComponent::resized()
{
    auto area = getLocalBounds().reduced(4);

    // Header bar
    auto hdr = area.removeFromTop(22);
    m_btnBypass.setBounds(hdr.removeFromLeft(28).reduced(1));
    hdr.removeFromLeft(4);
    m_btnRemove.setBounds(hdr.removeFromRight(20).reduced(1));
    hdr.removeFromRight(2);
    m_btnEdit.setBounds(hdr.removeFromRight(36).reduced(1));
    hdr.removeFromRight(4);
    m_lblTitle.setBounds(hdr);

    area.removeFromTop(4);

    // 4 Quick Params
    if (! m_params.empty())
    {
        const int colW = area.getWidth() / static_cast<int>(m_params.size());
        for (std::size_t i = 0; i < m_params.size(); ++i)
        {
            auto col = area.removeFromLeft(colW).reduced(2, 0);
            m_params[i]->slider.setBounds(col.removeFromTop(col.getHeight() - 14));
            m_params[i]->label.setBounds(col);
        }
    }
}

// ============================================================================
// DeviceRackView implementation
// ============================================================================
DeviceRackView::DeviceRackView(CommandHandler cmdHandler)
    : m_cmdHandler(std::move(cmdHandler))
{
    m_lblTrackTitle.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    m_lblTrackTitle.setColour(juce::Label::textColourId, juce::Colour::fromRGB(0, 220, 255));
    m_lblTrackTitle.setText("DEVICE RACK - No Track Selected", juce::dontSendNotification);
    addAndMakeVisible(m_lblTrackTitle);

    m_btnAddDevice.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(40, 80, 130));
    m_btnAddDevice.onClick = [this] { showAddDeviceMenu(); };
    addAndMakeVisible(m_btnAddDevice);

    m_emptyLabel.setText("No devices on this track. Click '+ Add Device' to insert native synthesizers, dynamics, or spatial effects.", juce::dontSendNotification);
    m_emptyLabel.setFont(juce::FontOptions(12.0f));
    m_emptyLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(130, 140, 160));
    m_emptyLabel.setJustificationType(juce::Justification::centred);
    m_rackContainer.addAndMakeVisible(m_emptyLabel);

    m_viewport.setViewedComponent(&m_rackContainer, false);
    m_viewport.setScrollBarsShown(false, true); // Horizontal scrollbar only
    addAndMakeVisible(m_viewport);
}

void DeviceRackView::setViewModel(const EditorViewModel& model)
{
    m_model = model;
    if (! m_selectedTrackId.has_value() && ! m_model.project.tracks.empty())
    {
        m_selectedTrackId = m_model.project.tracks.front().id;
    }
    rebuildRack();
}

void DeviceRackView::setSelectedTrackId(std::optional<project::TrackId> trackId)
{
    m_selectedTrackId = trackId;
    rebuildRack();
}

void DeviceRackView::rebuildRack()
{
    m_cards.clear();
    m_rackContainer.removeAllChildren();

    if (! m_selectedTrackId.has_value())
    {
        m_lblTrackTitle.setText("DEVICE RACK - No Track Selected", juce::dontSendNotification);
        m_emptyLabel.setVisible(true);
        m_rackContainer.addAndMakeVisible(m_emptyLabel);
        resized();
        return;
    }

    const project::Track* trk = nullptr;
    for (const auto& t : m_model.project.tracks)
    {
        if (t.id == *m_selectedTrackId)
        {
            trk = &t;
            break;
        }
    }

    if (trk == nullptr)
    {
        m_lblTrackTitle.setText("DEVICE RACK - Track Not Found", juce::dontSendNotification);
        m_emptyLabel.setVisible(true);
        m_rackContainer.addAndMakeVisible(m_emptyLabel);
        resized();
        return;
    }

    m_lblTrackTitle.setText("DEVICE RACK - " + (trk->name.empty() ? ("Track " + juce::String(trk->id)) : juce::String(trk->name)), juce::dontSendNotification);

    if (trk->inserts.empty())
    {
        m_emptyLabel.setVisible(true);
        m_rackContainer.addAndMakeVisible(m_emptyLabel);
    }
    else
    {
        m_emptyLabel.setVisible(false);

        auto openEditorCb = [](project::DspNodeType type, const std::string& name) {
            DspEditorKind kind = DspEditorKind::parametricEq;
            switch (type)
            {
            case project::DspNodeType::parametricEq: kind = DspEditorKind::parametricEq; break;
            case project::DspNodeType::dynamicsCompressor:
            case project::DspNodeType::brickwallLimiter: kind = DspEditorKind::compressor; break;
            case project::DspNodeType::tnhcSynth:
            case project::DspNodeType::analogLadderSynth:
            case project::DspNodeType::vectorSequencer:
            case project::DspNodeType::physicalModelSynth:
            case project::DspNodeType::granularCloud: kind = DspEditorKind::synth; break;
            case project::DspNodeType::tnhcReverb:
            case project::DspNodeType::rayTracingReverb: kind = DspEditorKind::reverb; break;
            case project::DspNodeType::tnhcDelay: kind = DspEditorKind::delay; break;
            case project::DspNodeType::resonanceSuppressor: kind = DspEditorKind::resonanceSuppressor; break;
            case project::DspNodeType::multibandStereoImager: kind = DspEditorKind::multibandImager; break;
            case project::DspNodeType::linearPhaseMasteringEq: kind = DspEditorKind::linearPhaseEq; break;
            case project::DspNodeType::buchlaComplexSynth: kind = DspEditorKind::buchlaComplexSynth; break;
            case project::DspNodeType::tubeTransformer:
            case project::DspNodeType::analogDegrader: kind = DspEditorKind::compressor; break;
            case project::DspNodeType::drumPadSampler:
            case project::DspNodeType::clapPlugin: kind = DspEditorKind::synth; break;
            case project::DspNodeType::virusTiPolarSynth:
                VirusTiPolarWindow::open();
                return;
            case project::DspNodeType::juno106Synth:
                Juno106Window::open();
                return;
            case project::DspNodeType::omniFmSynth:
                OmniFmWindow::open();
                return;
            case project::DspNodeType::soundFontPlayer:
                kind = DspEditorKind::synth;
                break;
            }
            DspEditorWindow::open(kind, name);
        };

        for (std::size_t i = 0; i < trk->inserts.size(); ++i)
        {
            auto card = std::make_unique<DeviceCardComponent>(
                trk->id,
                static_cast<std::uint32_t>(i),
                trk->inserts[i],
                m_cmdHandler,
                openEditorCb
            );
            m_rackContainer.addAndMakeVisible(*card);
            m_cards.push_back(std::move(card));
        }
    }

    resized();
}

void DeviceRackView::showAddDeviceMenu()
{
    if (! m_selectedTrackId.has_value() || ! m_cmdHandler)
        return;

    juce::PopupMenu menu;
    menu.addItem(1, "Parametric EQ (6-Band)");
    menu.addItem(2, "Dynamics Compressor");
    menu.addItem(3, "ResoTamer Dynamic Resonance Suppressor");
    menu.addItem(4, "StereoScope Multiband Stereo Imager");
    menu.addItem(5, "MasterPhase Linear-Phase Dynamic EQ");
    menu.addItem(6, "Complex 259 Buchla West-Coast Synth");
    menu.addItem(7, "TNHC Studio Reverb");
    menu.addItem(8, "TNHC Stereo Delay");
    menu.addItem(9, "Brickwall Peak Limiter");
    menu.addItem(10, "Drum Pad MPC Sampler");
    menu.addItem(11, "Access Virus TI Polar Synth");
    menu.addItem(12, "Roland Juno-106 Analog Poly Synth");
    menu.addItem(13, "Yamaha OmniFM 6-Operator FM Synth");
    menu.addItem(14, "SoundFont / SFZ Instrument Player");
    menu.addItem(15, "Vintage Tube & Transformer Rack");
    menu.addItem(16, "Analog Tape & Vinyl Degrader");
    menu.addItem(17, "3D Ray-Tracing Acoustic Room Reverb");

    menu.showMenuAsync(juce::PopupMenu::Options {}, [this, trkId = *m_selectedTrackId](int res) {
        if (res <= 0 || ! m_cmdHandler) return;

        project::DspNodeType type = project::DspNodeType::parametricEq;
        std::string name = "Parametric EQ";
        if (res == 2) { type = project::DspNodeType::dynamicsCompressor; name = "Compressor"; }
        else if (res == 3) { type = project::DspNodeType::resonanceSuppressor; name = "ResoTamer Suppressor"; }
        else if (res == 4) { type = project::DspNodeType::multibandStereoImager; name = "StereoScope Imager"; }
        else if (res == 5) { type = project::DspNodeType::linearPhaseMasteringEq; name = "MasterPhase EQ"; }
        else if (res == 6) { type = project::DspNodeType::buchlaComplexSynth; name = "Complex 259 Synth"; }
        else if (res == 7) { type = project::DspNodeType::tnhcReverb; name = "Studio Reverb"; }
        else if (res == 8) { type = project::DspNodeType::tnhcDelay; name = "Stereo Delay"; }
        else if (res == 9) { type = project::DspNodeType::brickwallLimiter; name = "Peak Limiter"; }
        else if (res == 10) { type = project::DspNodeType::drumPadSampler; name = "Drum Pad Sampler"; }
        else if (res == 11) { type = project::DspNodeType::virusTiPolarSynth; name = "Access Virus TI Polar"; }
        else if (res == 12) { type = project::DspNodeType::juno106Synth; name = "Roland Juno-106"; }
        else if (res == 13) { type = project::DspNodeType::omniFmSynth; name = "Yamaha OmniFM 6-Op Synth"; }
        else if (res == 14) { type = project::DspNodeType::soundFontPlayer; name = "SoundFont / SFZ Player"; }
        else if (res == 15) { type = project::DspNodeType::tubeTransformer; name = "Vintage Tube & Transformer"; }
        else if (res == 16) { type = project::DspNodeType::analogDegrader; name = "Analog Tape Degrader"; }
        else if (res == 17) { type = project::DspNodeType::rayTracingReverb; name = "3D Ray-Tracing Reverb"; }

        const project::Track* trk = nullptr;
        for (const auto& t : m_model.project.tracks)
        {
            if (t.id == trkId) { trk = &t; break; }
        }
        const auto slot = static_cast<std::uint32_t>(trk != nullptr ? trk->inserts.size() : 0);

        m_cmdHandler(project::AddTrackInsert {
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

void DeviceRackView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(18, 20, 26));

    // Top Header border
    g.setColour(juce::Colour::fromRGB(35, 38, 48));
    g.drawHorizontalLine(32, 0.0f, static_cast<float>(getWidth()));
}

void DeviceRackView::resized()
{
    auto area = getLocalBounds().reduced(6, 4);

    // Top Header
    auto hdr = area.removeFromTop(24);
    m_btnAddDevice.setBounds(hdr.removeFromRight(100));
    hdr.removeFromRight(10);
    m_lblTrackTitle.setBounds(hdr);

    area.removeFromTop(4);
    m_viewport.setBounds(area);

    if (m_cards.empty())
    {
        m_rackContainer.setBounds(0, 0, area.getWidth(), area.getHeight());
        m_emptyLabel.setBounds(0, 0, area.getWidth(), area.getHeight());
    }
    else
    {
        constexpr int cardW = 210;
        const int cardH = std::max(60, area.getHeight() - 4);
        const int totalW = std::max(area.getWidth(), static_cast<int>(m_cards.size()) * (cardW + 8) + 8);

        m_rackContainer.setBounds(0, 0, totalW, area.getHeight());

        int curX = 4;
        for (auto& card : m_cards)
        {
            card->setBounds(curX, 2, cardW, cardH);
            curX += cardW + 8;
        }
    }
}

} // namespace dawg::ui
