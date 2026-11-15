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

    m_searchFilter.setTextToShowWhenEmpty("Search parameters...", juce::Colour::fromRGB(120, 130, 150));
    m_searchFilter.setColour(juce::TextEditor::backgroundColourId, juce::Colour::fromRGB(22, 24, 30));
    m_searchFilter.setColour(juce::TextEditor::outlineColourId, juce::Colour::fromRGB(45, 52, 65));
    m_searchFilter.onTextChange = [this] { rebuildControls(); };
    addAndMakeVisible(m_searchFilter);

    m_container = std::make_unique<juce::Component>();
    m_viewport.setViewedComponent(m_container.get(), false);
    m_viewport.setScrollBarsShown(true, false);
    addAndMakeVisible(m_viewport);

    rebuildControls();
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
    g.drawText("⚙ " + m_pluginName + " (Generic Parameter Host)", 12, 0, getWidth() - 220, 38, juce::Justification::centredLeft);
}

void GenericPluginEditorView::resized()
{
    auto bounds = getLocalBounds();
    auto header = bounds.removeFromTop(38).reduced(6);

    m_searchFilter.setBounds(header.removeFromRight(200));

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
