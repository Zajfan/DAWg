#include "ClapParameterEditorDialog.h"

namespace dawg::ui {

void ClapParameterEditorComponent::ParameterContainer::resized()
{
    const int rowHeight = 36;
    const int width = getWidth();
    int y = 5;

    for (auto& row : rows)
    {
        if (row->label != nullptr && row->label->isVisible())
        {
            const int labelWidth = 180;
            const int valueWidth = 80;
            const int sliderWidth = std::max(100, width - labelWidth - valueWidth - 30);

            row->label->setBounds(10, y, labelWidth, 28);
            row->slider->setBounds(10 + labelWidth + 5, y, sliderWidth, 28);
            row->valueDisplay->setBounds(10 + labelWidth + 5 + sliderWidth + 5, y, valueWidth, 28);

            y += rowHeight;
        }
    }

    setSize(width, std::max(y + 10, 100));
}

ClapParameterEditorComponent::ClapParameterEditorComponent(
    std::string pluginName,
    std::string pluginVendor,
    std::vector<plugins::ClapParamDescriptor> params,
    const bool isCrashed,
    ParamChangeCallback paramChangeCb,
    RestartCallback restartCb)
    : m_pluginName(std::move(pluginName))
    , m_pluginVendor(std::move(pluginVendor))
    , m_descriptors(std::move(params))
    , m_crashed(isCrashed)
    , m_paramChangeCb(std::move(paramChangeCb))
    , m_restartCb(std::move(restartCb))
{
    m_titleLabel.setText(m_pluginName, juce::dontSendNotification);
    m_titleLabel.setFont(juce::FontOptions(18.0f).withStyle("Bold"));
    m_titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xfff0f6fc));
    addAndMakeVisible(m_titleLabel);

    m_vendorLabel.setText("by " + m_pluginVendor, juce::dontSendNotification);
    m_vendorLabel.setFont(juce::FontOptions(12.0f).withStyle("Italic"));
    m_vendorLabel.setColour(juce::Label::textColourId, juce::Colour(0xff8b949e));
    addAndMakeVisible(m_vendorLabel);

    m_searchFilter.setTextToShowWhenEmpty("Filter parameters...", juce::Colour(0xff8b949e));
    m_searchFilter.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff161b22));
    m_searchFilter.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff30363d));
    m_searchFilter.setColour(juce::TextEditor::textColourId, juce::Colour(0xffc9d1d9));
    m_searchFilter.onTextChange = [this] {
        filterParameters(m_searchFilter.getText());
    };
    addAndMakeVisible(m_searchFilter);

    m_restartButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffda3633));
    m_restartButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    m_restartButton.onClick = [this] {
        if (m_restartCb)
            m_restartCb();
        setCrashed(false);
    };
    addChildComponent(m_restartButton);
    m_restartButton.setVisible(m_crashed);

    // Build parameter rows
    for (const auto& desc : m_descriptors)
    {
        auto row = std::make_unique<ParamRow>();
        row->id = desc.id;

        auto label = std::make_unique<juce::Label>();
        const juce::String displayName = desc.module.empty()
            ? juce::String(desc.name)
            : juce::String(desc.module) + " / " + juce::String(desc.name);
        label->setText(displayName, juce::dontSendNotification);
        label->setFont(juce::FontOptions(12.0f));
        m_container.addAndMakeVisible(label.get());

        auto slider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
        slider->setRange(desc.minValue, desc.maxValue, 0.001);
        slider->setValue(desc.currentValue, juce::dontSendNotification);
        slider->setColour(juce::Slider::thumbColourId, juce::Colour(0xff58a6ff));
        slider->setColour(juce::Slider::trackColourId, juce::Colour(0xff1f6feb));
        slider->setColour(juce::Slider::backgroundColourId, juce::Colour(0xff21262d));
        m_container.addAndMakeVisible(slider.get());

        auto valDisplay = std::make_unique<juce::Label>();
        valDisplay->setText(juce::String(desc.currentValue, 2), juce::dontSendNotification);
        valDisplay->setFont(juce::FontOptions(11.0f));
        valDisplay->setColour(juce::Label::textColourId, juce::Colour(0xff8b949e));
        m_container.addAndMakeVisible(valDisplay.get());

        const auto paramId = desc.id;
        auto* sldPtr = slider.get();
        auto* valPtr = valDisplay.get();
        sldPtr->onValueChange = [this, paramId, sldPtr, valPtr] {
            const double v = sldPtr->getValue();
            valPtr->setText(juce::String(v, 2), juce::dontSendNotification);
            if (m_paramChangeCb)
                m_paramChangeCb(paramId, v);
        };

        row->label = std::move(label);
        row->slider = std::move(slider);
        row->valueDisplay = std::move(valDisplay);
        m_container.rows.push_back(std::move(row));
    }

    m_viewport.setViewedComponent(&m_container, false);
    m_viewport.setScrollBarsShown(true, false);
    m_viewport.setScrollBarThickness(10);
    addAndMakeVisible(m_viewport);

    setSize(520, 480);
}

void ClapParameterEditorComponent::setCrashed(const bool crashed)
{
    m_crashed = crashed;
    m_restartButton.setVisible(m_crashed);
    repaint();
}

void ClapParameterEditorComponent::filterParameters(const juce::String& filterText)
{
    const auto lowerFilter = filterText.toLowerCase();
    for (auto& row : m_container.rows)
    {
        const bool match = filterText.isEmpty()
            || row->label->getText().toLowerCase().contains(lowerFilter);
        row->label->setVisible(match);
        row->slider->setVisible(match);
        row->valueDisplay->setVisible(match);
    }
    m_container.resized();
}

void ClapParameterEditorComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff0d1117));

    // Top header background
    g.setColour(juce::Colour(0xff161b22));
    g.fillRect(0, 0, getWidth(), 80);

    g.setColour(juce::Colour(0xff30363d));
    g.drawHorizontalLine(80, 0.0f, static_cast<float>(getWidth()));

    if (m_crashed)
    {
        g.setColour(juce::Colour(0x33da3633));
        g.fillRect(0, 81, getWidth(), 36);

        g.setColour(juce::Colour(0xffda3633));
        g.drawHorizontalLine(117, 0.0f, static_cast<float>(getWidth()));

        g.setFont(juce::FontOptions(12.0f).withStyle("Bold"));
        g.drawText("CRASH IMMUNITY: Plugin crashed. Audio passthrough active.",
                   15, 81, getWidth() - 170, 36, juce::Justification::centredLeft);
    }
}

void ClapParameterEditorComponent::resized()
{
    const int pad = 12;
    m_titleLabel.setBounds(pad, 10, getWidth() - 200, 24);
    m_vendorLabel.setBounds(pad, 34, getWidth() - 200, 18);
    m_searchFilter.setBounds(getWidth() - 180 - pad, 20, 180, 26);

    int contentY = 85;
    if (m_crashed)
    {
        m_restartButton.setBounds(getWidth() - 150 - pad, 86, 140, 26);
        contentY = 122;
    }

    m_viewport.setBounds(pad, contentY, getWidth() - 2 * pad, getHeight() - contentY - pad);
    m_container.setSize(m_viewport.getWidth() - 15, m_container.getHeight());
    m_container.resized();
}

ClapParameterEditorWindow::ClapParameterEditorWindow(
    const juce::String& title,
    std::unique_ptr<ClapParameterEditorComponent> content,
    std::function<void()> onClose)
    : juce::DocumentWindow(title,
                           juce::Colour(0xff0d1117),
                           juce::DocumentWindow::closeButton)
    , m_onClose(std::move(onClose))
{
    setUsingNativeTitleBar(false);
    setContentOwned(content.release(), true);
    setResizable(true, true);
    setResizeLimits(400, 300, 1000, 900);
    centreWithSize(520, 480);
    setVisible(true);
}

void ClapParameterEditorWindow::closeButtonPressed()
{
    if (m_onClose)
        m_onClose();
    delete this;
}

void ClapParameterEditorWindow::open(
    const std::string& pluginName,
    const std::string& pluginVendor,
    std::vector<plugins::ClapParamDescriptor> params,
    const bool isCrashed,
    ClapParameterEditorComponent::ParamChangeCallback paramChangeCb,
    ClapParameterEditorComponent::RestartCallback restartCb,
    std::function<void()> onClose)
{
    static std::map<std::string, juce::Component::SafePointer<ClapParameterEditorWindow>> s_activeClapWindows;
    auto it = s_activeClapWindows.find(pluginName);
    if (it != s_activeClapWindows.end() && it->second != nullptr)
    {
        it->second->toFront(true);
        return;
    }

    auto comp = std::make_unique<ClapParameterEditorComponent>(
        pluginName, pluginVendor, std::move(params), isCrashed,
        std::move(paramChangeCb), std::move(restartCb));

    auto* win = new ClapParameterEditorWindow(
        juce::String(pluginName) + " - CLAP Host",
        std::move(comp),
        [pluginName, userOnClose = std::move(onClose)] {
            s_activeClapWindows.erase(pluginName);
            if (userOnClose)
                userOnClose();
        });
    s_activeClapWindows[pluginName] = win;
}

} // namespace dawg::ui
