#include "GenericPluginEditorView.h"

#include <algorithm>

namespace dawg::ui {

GenericPluginEditorView::GenericPluginEditorView(
    std::string pluginName,
    std::vector<PluginParameterDesc> params,
    OnParameterChanged onParamChanged)
    : m_pluginName(std::move(pluginName))
    , m_params(std::move(params))
    , m_onParamChanged(std::move(onParamChanged))
{
    // Provide rich default parameter set if none passed
    if (m_params.empty())
    {
        m_params = {
            {1, "Master Gain", -60.0f, 12.0f, 0.0f, 0.0f, "dB", false},
            {2, "Cutoff Frequency", 20.0f, 20000.0f, 1000.0f, 2500.0f, "Hz", false},
            {3, "Resonance Q", 0.5f, 20.0f, 0.707f, 2.5f, "", false},
            {4, "Drive / Saturation", 0.0f, 100.0f, 0.0f, 25.0f, "%", false},
            {5, "Attack Time", 0.1f, 1000.0f, 10.0f, 15.0f, "ms", false},
            {6, "Release Time", 5.0f, 3000.0f, 150.0f, 220.0f, "ms", false},
            {7, "Mix Dry / Wet", 0.0f, 100.0f, 100.0f, 50.0f, "%", false},
            {8, "Oversampling", 1.0f, 4.0f, 1.0f, 2.0f, "x", true}
        };
    }

    m_searchFilter.setTextToShowWhenEmpty("Search...", juce::Colour::fromRGB(120, 130, 150));
    m_searchFilter.setColour(juce::TextEditor::backgroundColourId, juce::Colour::fromRGB(22, 24, 30));
    m_searchFilter.setColour(juce::TextEditor::outlineColourId, juce::Colour::fromRGB(45, 52, 65));
    m_searchFilter.onTextChange = [this] {
        populatePresets(m_searchFilter.getText().toStdString());
        rebuildControls();
    };
    addAndMakeVisible(m_searchFilter);

    // Preset Manager & Ribbon
    m_pluginId = m_pluginName;
    std::replace(m_pluginId.begin(), m_pluginId.end(), ' ', '_');
    m_presetManager = std::make_unique<plugins::PluginPresetManager>();

    m_presetCombo.setColour(juce::ComboBox::backgroundColourId, juce::Colour::fromRGB(24, 26, 33));
    m_presetCombo.setColour(juce::ComboBox::outlineColourId, juce::Colour::fromRGB(45, 52, 65));
    m_presetCombo.onChange = [this] { selectPreset(m_presetCombo.getSelectedId()); };
    addAndMakeVisible(m_presetCombo);

    for (int i = 0; i < 5; ++i)
    {
        m_starButtons[static_cast<std::size_t>(i)].setButtonText(juce::CharPointer_UTF8("\xe2\x98\x85"));
        m_starButtons[static_cast<std::size_t>(i)].setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        m_starButtons[static_cast<std::size_t>(i)].setColour(juce::TextButton::textColourOffId, juce::Colour::fromRGB(255, 205, 50));
        m_starButtons[static_cast<std::size_t>(i)].onClick = [this, star = i + 1] { updateStars(star); };
        addAndMakeVisible(m_starButtons[static_cast<std::size_t>(i)]);
    }

    m_abToggleBtn.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(32, 38, 48));
    m_abToggleBtn.setColour(juce::TextButton::textColourOffId, juce::Colour::fromRGB(140, 200, 255));
    m_abToggleBtn.onClick = [this] {
        m_isSlotA = ! m_isSlotA;
        m_abToggleBtn.setButtonText(m_isSlotA ? "[ A ]" : "[ B ]");
    };
    addAndMakeVisible(m_abToggleBtn);

    m_copyAbBtn.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(28, 32, 42));
    m_copyAbBtn.setColour(juce::TextButton::textColourOffId, juce::Colour::fromRGB(180, 190, 210));
    m_copyAbBtn.onClick = [this] {
        if (m_presetManager)
        {
            auto& ab = m_presetManager->getOrAllocABState(m_pluginId);
            if (m_isSlotA) ab.copyAToB(); else ab.copyBToA();
        }
    };
    addAndMakeVisible(m_copyAbBtn);

    m_savePresetBtn.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(35, 75, 45));
    m_savePresetBtn.setColour(juce::TextButton::textColourOffId, juce::Colour::fromRGB(200, 245, 210));
    m_savePresetBtn.onClick = [this] {
        if (! m_presetManager) return;
        plugins::PluginPreset p;
        p.presetId = "user_" + std::to_string(std::rand() % 10000);
        p.pluginId = m_pluginId;
        p.name = "Custom " + m_pluginName;
        p.author = "User";
        p.category = "User Presets";
        p.tags = {"user", "custom"};
        p.rating = m_currentRating;
        for (const auto& param : m_params)
            p.parameters.push_back(param.currentVal);
        m_presetManager->saveUserPreset(p);
        populatePresets(m_searchFilter.getText().toStdString());
    };
    addAndMakeVisible(m_savePresetBtn);

    populatePresets();
    updateStars(5);

    m_container = std::make_unique<juce::Component>();
    m_viewport.setViewedComponent(m_container.get(), false);
    m_viewport.setScrollBarsShown(true, false);
    addAndMakeVisible(m_viewport);

    rebuildControls();
}

void GenericPluginEditorView::populatePresets(const std::string& query)
{
    m_presetCombo.clear(juce::dontSendNotification);
    if (! m_presetManager) return;

    const auto presets = m_presetManager->searchPresets(m_pluginId, query);
    int id = 1;
    for (const auto& p : presets)
    {
        juce::String label = (p.isFactory ? "[Factory] " : "[User] ") + juce::String(p.name);
        m_presetCombo.addItem(label, id++);
    }
    if (m_presetCombo.getNumItems() > 0 && m_presetCombo.getSelectedId() == 0)
    {
        m_presetCombo.setSelectedId(1, juce::dontSendNotification);
    }
}

void GenericPluginEditorView::selectPreset(int index)
{
    if (index <= 0 || ! m_presetManager) return;

    const auto presets = m_presetManager->searchPresets(m_pluginId, m_searchFilter.getText().toStdString());
    if (static_cast<std::size_t>(index - 1) < presets.size())
    {
        const auto& p = presets[static_cast<std::size_t>(index - 1)];
        updateStars(p.rating > 0 ? p.rating : 5);
        for (std::size_t i = 0; i < p.parameters.size() && i < m_params.size(); ++i)
        {
            updateParameterValue(m_params[i].id, p.parameters[i]);
            if (m_onParamChanged)
                m_onParamChanged(m_params[i].id, p.parameters[i]);
        }
    }
}

void GenericPluginEditorView::updateStars(int rating)
{
    m_currentRating = std::clamp(rating, 1, 5);
    for (int i = 0; i < 5; ++i)
    {
        if (i < m_currentRating)
            m_starButtons[static_cast<std::size_t>(i)].setColour(juce::TextButton::textColourOffId, juce::Colour::fromRGB(255, 205, 50));
        else
            m_starButtons[static_cast<std::size_t>(i)].setColour(juce::TextButton::textColourOffId, juce::Colour::fromRGB(75, 80, 95));
    }
}

void GenericPluginEditorView::setParameters(std::vector<PluginParameterDesc> params)
{
    m_params = std::move(params);
    rebuildControls();
}

void GenericPluginEditorView::updateParameterValue(std::uint32_t paramId, float value)
{
    for (auto& row : m_rows)
    {
        if (row.desc.id == paramId)
        {
            row.desc.currentVal = value;
            if (row.slider)
            {
                row.slider->setValue(value, juce::dontSendNotification);
            }
            if (row.valueLabel)
            {
                row.valueLabel->setText(juce::String(value, 2) + " " + row.desc.unitLabel, juce::dontSendNotification);
            }
            break;
        }
    }
}

void GenericPluginEditorView::rebuildControls()
{
    m_rows.clear();
    m_container->removeAllChildren();

    const auto query = m_searchFilter.getText().toLowerCase();
    int currentY = 8;
    constexpr int rowH = 34;

    for (const auto& p : m_params)
    {
        if (query.isNotEmpty() && ! juce::String(p.name).toLowerCase().contains(query))
        {
            continue;
        }

        ParamRow row;
        row.desc = p;

        row.nameLabel = std::make_unique<juce::Label>("", p.name);
        row.nameLabel->setFont(juce::FontOptions(11.5f, juce::Font::bold));
        row.nameLabel->setColour(juce::Label::textColourId, juce::Colour::fromRGB(220, 230, 245));
        row.nameLabel->setJustificationType(juce::Justification::centredLeft);
        m_container->addAndMakeVisible(row.nameLabel.get());

        row.slider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
        row.slider->setRange(p.minVal, p.maxVal, p.isStepped ? 1.0 : 0.001);
        row.slider->setValue(p.currentVal, juce::dontSendNotification);
        row.slider->setColour(juce::Slider::thumbColourId, juce::Colour::fromRGB(70, 160, 240));
        row.slider->setColour(juce::Slider::trackColourId, juce::Colour::fromRGB(45, 95, 150));

        const auto paramId = p.id;
        row.slider->onValueChange = [this, paramId, rawSlider = row.slider.get()] {
            const float newVal = static_cast<float>(rawSlider->getValue());
            updateParameterValue(paramId, newVal);
            if (m_onParamChanged)
            {
                m_onParamChanged(paramId, newVal);
            }
        };
        m_container->addAndMakeVisible(row.slider.get());

        row.valueLabel = std::make_unique<juce::Label>("", juce::String(p.currentVal, 2) + " " + p.unitLabel);
        row.valueLabel->setFont(juce::FontOptions(11.0f));
        row.valueLabel->setColour(juce::Label::textColourId, juce::Colour::fromRGB(150, 175, 205));
        row.valueLabel->setJustificationType(juce::Justification::centredRight);
        m_container->addAndMakeVisible(row.valueLabel.get());

        m_rows.push_back(std::move(row));
        currentY += rowH;
    }

    m_container->setSize(getWidth() - 16, std::max(currentY + 12, getHeight() - 48));
    resized();
}

void GenericPluginEditorView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(18, 20, 26));

    // Header bar
    auto header = getLocalBounds().removeFromTop(38);
    g.setColour(juce::Colour::fromRGB(25, 28, 36));
    g.fillRect(header);

    g.setColour(juce::Colour::fromRGB(45, 52, 68));
    g.drawHorizontalLine(header.getBottom() - 1, 0.0f, static_cast<float>(getWidth()));

    g.setColour(juce::Colour::fromRGB(240, 245, 255));
    g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    g.drawText(m_pluginName, 12, 0, std::max(100, getWidth() - 480), 38, juce::Justification::centredLeft);
}

void GenericPluginEditorView::resized()
{
    auto bounds = getLocalBounds();
    auto header = bounds.removeFromTop(38).reduced(6, 4);

    m_searchFilter.setBounds(header.removeFromRight(100));
    header.removeFromRight(4);

    m_savePresetBtn.setBounds(header.removeFromRight(42));
    header.removeFromRight(3);

    m_copyAbBtn.setBounds(header.removeFromRight(40));
    header.removeFromRight(3);

    m_abToggleBtn.setBounds(header.removeFromRight(42));
    header.removeFromRight(6);

    for (int i = 4; i >= 0; --i)
    {
        m_starButtons[static_cast<std::size_t>(i)].setBounds(header.removeFromRight(16).reduced(0, 3));
    }
    header.removeFromRight(6);

    m_presetCombo.setBounds(header.removeFromRight(135));

    m_viewport.setBounds(bounds.reduced(8, 4));

    if (m_container)
    {
        const int w = m_viewport.getWidth() - 14;
        constexpr int rowH = 34;
        int curY = 4;

        for (auto& row : m_rows)
        {
            if (row.nameLabel && row.slider && row.valueLabel)
            {
                juce::Rectangle<int> rowRect(4, curY, w, rowH);
                row.nameLabel->setBounds(rowRect.removeFromLeft(140));
                row.valueLabel->setBounds(rowRect.removeFromRight(70));
                row.slider->setBounds(rowRect.reduced(6, 4));
            }
            curY += rowH;
        }
        m_container->setSize(w, std::max(curY + 8, m_viewport.getHeight()));
    }
}

} // namespace dawg::ui
