#include "GenericPluginEditorView.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::ui {

// ============================================================================
// StudioRackLookAndFeel Implementation
// ============================================================================

void StudioRackLookAndFeel::drawRotarySlider(
    juce::Graphics& g, int x, int y, int width, int height,
    float sliderPosProportional, float rotaryStartAngle,
    float rotaryEndAngle, juce::Slider& /*slider*/)
{
    const auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                               static_cast<float>(width), static_cast<float>(height));
    const float radius = std::min(bounds.getWidth(), bounds.getHeight()) * 0.44f;
    if (radius < 5.0f)
        return;

    const auto center = bounds.getCentre();
    const float rx = center.x - radius;
    const float ry = center.y - radius;
    const float rw = radius * 2.0f;

    // 1. Recessed background well
    g.setColour(juce::Colour::fromRGB(14, 16, 21));
    g.fillEllipse(rx - 2.0f, ry - 2.0f, rw + 4.0f, rw + 4.0f);
    g.setColour(juce::Colour::fromRGB(34, 40, 52));
    g.drawEllipse(rx - 2.0f, ry - 2.0f, rw + 4.0f, rw + 4.0f, 1.2f);

    // 2. Arc Track
    const float currentAngle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
    const float arcRadius = radius * 0.88f;

    juce::Path trackPath;
    trackPath.addCentredArc(center.x, center.y, arcRadius, arcRadius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour(juce::Colour::fromRGB(28, 33, 44));
    g.strokePath(trackPath, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // 3. Illuminated Value Arc (with soft neon cyan bloom)
    if (sliderPosProportional > 0.001f)
    {
        juce::Path valuePath;
        valuePath.addCentredArc(center.x, center.y, arcRadius, arcRadius, 0.0f, rotaryStartAngle, currentAngle, true);

        // Bloom
        g.setColour(juce::Colour::fromRGBA(0, 220, 245, 65));
        g.strokePath(valuePath, juce::PathStrokeType(6.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Sharp core
        g.setColour(juce::Colour::fromRGB(0, 225, 245));
        g.strokePath(valuePath, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // 4. Dial Body (Metallic Knurled Dial)
    const float dialRadius = radius * 0.70f;
    const float dx = center.x - dialRadius;
    const float dy = center.y - dialRadius;
    const float dw = dialRadius * 2.0f;

    // Shadow behind dial cap
    g.setColour(juce::Colour::fromRGBA(0, 0, 0, 160));
    g.fillEllipse(dx + 1.0f, dy + 2.0f, dw, dw);

    // Knurling notches around perimeter
    g.setColour(juce::Colour::fromRGB(45, 52, 65));
    constexpr int numKnurls = 18;
    for (int i = 0; i < numKnurls; ++i)
    {
        const float a = static_cast<float>(i) * (2.0f * std::numbers::pi_v<float> / static_cast<float>(numKnurls));
        const auto p1 = center.getPointOnCircumference(dialRadius - 1.0f, a);
        const auto p2 = center.getPointOnCircumference(dialRadius + 2.0f, a);
        g.drawLine(p1.x, p1.y, p2.x, p2.y, 1.2f);
    }

    // Brushed metal gradient cap
    juce::ColourGradient grad(
        juce::Colour::fromRGB(58, 66, 82), center.x - dialRadius * 0.4f, center.y - dialRadius * 0.4f,
        juce::Colour::fromRGB(22, 25, 32), center.x + dialRadius * 0.6f, center.y + dialRadius * 0.6f,
        false);
    g.setGradientFill(grad);
    g.fillEllipse(dx, dy, dw, dw);

    // Bezel rim
    g.setColour(juce::Colour::fromRGB(75, 85, 105));
    g.drawEllipse(dx, dy, dw, dw, 1.0f);

    // Center recessed dimple
    const float dimpleRadius = dialRadius * 0.42f;
    g.setColour(juce::Colour::fromRGB(18, 20, 26));
    g.fillEllipse(center.x - dimpleRadius, center.y - dimpleRadius, dimpleRadius * 2.0f, dimpleRadius * 2.0f);
    g.setColour(juce::Colour::fromRGB(38, 44, 56));
    g.drawEllipse(center.x - dimpleRadius, center.y - dimpleRadius, dimpleRadius * 2.0f, dimpleRadius * 2.0f, 1.0f);

    // 5. Radial Pointer Needle & LED pip
    const auto pStart = center.getPointOnCircumference(dimpleRadius * 0.6f, currentAngle);
    const auto pEnd = center.getPointOnCircumference(dialRadius - 1.5f, currentAngle);

    // Needle line
    g.setColour(juce::Colour::fromRGB(200, 230, 255));
    g.drawLine(pStart.x, pStart.y, pEnd.x, pEnd.y, 2.0f);

    // Glowing LED Pip at tip
    g.setColour(juce::Colour::fromRGB(0, 235, 255));
    g.fillEllipse(pEnd.x - 2.0f, pEnd.y - 2.0f, 4.0f, 4.0f);
}

void StudioRackLookAndFeel::drawLinearSlider(
    juce::Graphics& g, int x, int /*y*/, int width, int /*height*/,
    float sliderPos, float minSliderPos, float maxSliderPos,
    const juce::Slider::SliderStyle /*style*/, juce::Slider& /*slider*/)
{
    const float trackX = static_cast<float>(x) + static_cast<float>(width) * 0.5f;
    const float trackTop = std::min(minSliderPos, maxSliderPos);
    const float trackBottom = std::max(minSliderPos, maxSliderPos);

    // Fader slot
    g.setColour(juce::Colour::fromRGB(12, 14, 18));
    g.fillRoundedRectangle(trackX - 3.5f, trackTop, 7.0f, trackBottom - trackTop, 3.5f);
    g.setColour(juce::Colour::fromRGB(38, 44, 56));
    g.drawRoundedRectangle(trackX - 3.5f, trackTop, 7.0f, trackBottom - trackTop, 3.5f, 1.0f);

    // Center line
    g.setColour(juce::Colour::fromRGB(25, 30, 40));
    g.drawLine(trackX, trackTop + 4.0f, trackX, trackBottom - 4.0f, 1.0f);

    // Calibration ticks
    g.setColour(juce::Colour::fromRGB(45, 52, 65));
    constexpr int numTicks = 8;
    for (int i = 0; i <= numTicks; ++i)
    {
        const float ty = trackTop + (static_cast<float>(i) / static_cast<float>(numTicks)) * (trackBottom - trackTop);
        const float tickLen = (i == numTicks / 2 || i == 0 || i == numTicks) ? 7.0f : 4.0f;
        g.drawLine(trackX - 5.0f - tickLen, ty, trackX - 5.0f, ty, 1.0f);
        g.drawLine(trackX + 5.0f, ty, trackX + 5.0f + tickLen, ty, 1.0f);
    }

    // Fader cap
    const float capW = 28.0f;
    const float capH = 16.0f;
    const float capX = trackX - capW * 0.5f;
    const float capY = std::clamp(sliderPos - capH * 0.5f, trackTop - capH * 0.5f, trackBottom - capH * 0.5f);

    // Cap shadow
    g.setColour(juce::Colour::fromRGBA(0, 0, 0, 140));
    g.fillRoundedRectangle(capX + 1.0f, capY + 2.0f, capW, capH, 2.5f);

    // Cap body gradient
    juce::ColourGradient capGrad(
        juce::Colour::fromRGB(72, 80, 96), capX, capY,
        juce::Colour::fromRGB(28, 32, 40), capX, capY + capH,
        false);
    g.setGradientFill(capGrad);
    g.fillRoundedRectangle(capX, capY, capW, capH, 2.5f);

    g.setColour(juce::Colour::fromRGB(95, 105, 125));
    g.drawRoundedRectangle(capX, capY, capW, capH, 2.5f, 1.0f);

    // Grip ridges
    g.setColour(juce::Colour::fromRGB(45, 50, 62));
    g.drawLine(capX + 5.0f, capY + 4.0f, capX + capW - 5.0f, capY + 4.0f, 1.0f);
    g.drawLine(capX + 5.0f, capY + capH - 4.0f, capX + capW - 5.0f, capY + capH - 4.0f, 1.0f);

    // Center indicator stripe
    g.setColour(juce::Colour::fromRGB(255, 120, 40));
    g.drawLine(capX + 3.0f, capY + capH * 0.5f, capX + capW - 3.0f, capY + capH * 0.5f, 1.5f);
}

// ============================================================================
// GenericPluginEditorView Implementation
// ============================================================================

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

    // Power / Bypass button
    m_pwrBtn.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(32, 140, 70));
    m_pwrBtn.setColour(juce::TextButton::textColourOffId, juce::Colour::fromRGB(220, 255, 230));
    m_pwrBtn.onClick = [this] {
        m_isPowered = ! m_isPowered;
        if (m_isPowered)
        {
            m_pwrBtn.setButtonText("PWR");
            m_pwrBtn.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(32, 140, 70));
        }
        else
        {
            m_pwrBtn.setButtonText("BYP");
            m_pwrBtn.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(150, 45, 45));
        }
        repaint();
    };
    addAndMakeVisible(m_pwrBtn);

    // Layout Toggle button (Dials vs Faders)
    m_layoutToggleBtn.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(36, 42, 54));
    m_layoutToggleBtn.setColour(juce::TextButton::textColourOffId, juce::Colour::fromRGB(160, 210, 255));
    m_layoutToggleBtn.onClick = [this] {
        if (m_layoutMode == RackLayoutMode::RotaryGrid)
        {
            m_layoutMode = RackLayoutMode::FaderRack;
            m_layoutToggleBtn.setButtonText("[ FADERS ]");
        }
        else
        {
            m_layoutMode = RackLayoutMode::RotaryGrid;
            m_layoutToggleBtn.setButtonText("[ DIALS ]");
        }
        rebuildControls();
    };
    addAndMakeVisible(m_layoutToggleBtn);

    m_searchFilter.setTextToShowWhenEmpty("Search...", juce::Colour::fromRGB(120, 130, 150));
    m_searchFilter.setColour(juce::TextEditor::backgroundColourId, juce::Colour::fromRGB(18, 21, 28));
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

    m_presetCombo.setColour(juce::ComboBox::backgroundColourId, juce::Colour::fromRGB(22, 26, 34));
    m_presetCombo.setColour(juce::ComboBox::outlineColourId, juce::Colour::fromRGB(45, 52, 65));
    m_presetCombo.onChange = [this] { selectPreset(m_presetCombo.getSelectedId()); };
    addAndMakeVisible(m_presetCombo);

    for (int i = 0; i < 5; ++i)
    {
        m_starButtons[static_cast<std::size_t>(i)].setButtonText(juce::String(i + 1));
        m_starButtons[static_cast<std::size_t>(i)].setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        m_starButtons[static_cast<std::size_t>(i)].setColour(juce::TextButton::textColourOffId, juce::Colour::fromRGB(255, 205, 50));
        m_starButtons[static_cast<std::size_t>(i)].onClick = [this, star = i + 1] { updateStars(star); };
        addAndMakeVisible(m_starButtons[static_cast<std::size_t>(i)]);
    }

    m_abToggleBtn.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(32, 38, 50));
    m_abToggleBtn.setColour(juce::TextButton::textColourOffId, juce::Colour::fromRGB(140, 205, 255));
    m_abToggleBtn.onClick = [this] {
        m_isSlotA = ! m_isSlotA;
        m_abToggleBtn.setButtonText(m_isSlotA ? "[ A ]" : "[ B ]");
    };
    addAndMakeVisible(m_abToggleBtn);

    m_copyAbBtn.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(28, 34, 44));
    m_copyAbBtn.setColour(juce::TextButton::textColourOffId, juce::Colour::fromRGB(180, 195, 215));
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

GenericPluginEditorView::~GenericPluginEditorView()
{
    for (auto& tile : m_tiles)
    {
        if (tile.slider)
            tile.slider->setLookAndFeel(nullptr);
    }
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
    for (auto& tile : m_tiles)
    {
        if (tile.desc.id == paramId)
        {
            tile.desc.currentVal = value;
            if (tile.slider)
            {
                tile.slider->setValue(value, juce::dontSendNotification);
            }
            if (tile.valueLabel)
            {
                juce::String valStr;
                if (std::abs(value) >= 100.0f)
                    valStr = juce::String(value, 1) + " " + tile.desc.unitLabel;
                else
                    valStr = juce::String(value, 2) + " " + tile.desc.unitLabel;
                tile.valueLabel->setText(valStr, juce::dontSendNotification);
            }
            break;
        }
    }
}

void GenericPluginEditorView::rebuildControls()
{
    for (auto& tile : m_tiles)
    {
        if (tile.slider)
            tile.slider->setLookAndFeel(nullptr);
    }
    m_tiles.clear();
    m_container->removeAllChildren();

    const auto query = m_searchFilter.getText().toLowerCase();

    for (const auto& p : m_params)
    {
        if (query.isNotEmpty() && ! juce::String(p.name).toLowerCase().contains(query))
        {
            continue;
        }

        ParamTile tile;
        tile.desc = p;

        // Name badge
        tile.nameLabel = std::make_unique<juce::Label>("", juce::String(p.name).toUpperCase());
        tile.nameLabel->setFont(juce::FontOptions(10.5f, juce::Font::bold));
        tile.nameLabel->setColour(juce::Label::textColourId, juce::Colour::fromRGB(205, 218, 235));
        tile.nameLabel->setJustificationType(juce::Justification::centred);
        m_container->addAndMakeVisible(tile.nameLabel.get());

        // Slider (Rotary or Linear based on layout mode)
        if (m_layoutMode == RackLayoutMode::RotaryGrid)
        {
            tile.slider = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox);
        }
        else
        {
            tile.slider = std::make_unique<juce::Slider>(juce::Slider::LinearVertical, juce::Slider::NoTextBox);
        }
        tile.slider->setLookAndFeel(&m_rackLookAndFeel);
        tile.slider->setRange(p.minVal, p.maxVal, p.isStepped ? 1.0 : 0.001);
        tile.slider->setValue(p.currentVal, juce::dontSendNotification);

        const auto paramId = p.id;
        tile.slider->onValueChange = [this, paramId, rawSlider = tile.slider.get()] {
            const float newVal = static_cast<float>(rawSlider->getValue());
            updateParameterValue(paramId, newVal);
            if (m_onParamChanged)
            {
                m_onParamChanged(paramId, newVal);
            }
        };
        m_container->addAndMakeVisible(tile.slider.get());

        // OLED Value badge
        juce::String valStr;
        if (std::abs(p.currentVal) >= 100.0f)
            valStr = juce::String(p.currentVal, 1) + " " + p.unitLabel;
        else
            valStr = juce::String(p.currentVal, 2) + " " + p.unitLabel;

        tile.valueLabel = std::make_unique<juce::Label>("", valStr);
        tile.valueLabel->setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::bold));
        tile.valueLabel->setColour(juce::Label::backgroundColourId, juce::Colour::fromRGB(12, 14, 18));
        tile.valueLabel->setColour(juce::Label::outlineColourId, juce::Colour::fromRGB(36, 44, 58));
        tile.valueLabel->setColour(juce::Label::textColourId, juce::Colour::fromRGB(0, 225, 245));
        tile.valueLabel->setJustificationType(juce::Justification::centred);
        m_container->addAndMakeVisible(tile.valueLabel.get());

        m_tiles.push_back(std::move(tile));
    }

    resized();
}

void GenericPluginEditorView::paint(juce::Graphics& g)
{
    // Deep obsidian brushed aluminum rack faceplate
    juce::ColourGradient bgGrad(
        juce::Colour::fromRGB(20, 23, 30), 0.0f, 0.0f,
        juce::Colour::fromRGB(14, 16, 21), 0.0f, static_cast<float>(getHeight()),
        false);
    g.setGradientFill(bgGrad);
    g.fillAll();

    // Studio Rack Header
    auto header = getLocalBounds().removeFromTop(44);
    g.setColour(juce::Colour::fromRGB(26, 30, 40));
    g.fillRect(header);

    // Bevel bottom line
    g.setColour(juce::Colour::fromRGB(45, 52, 68));
    g.drawHorizontalLine(header.getBottom() - 1, 0.0f, static_cast<float>(getWidth()));

    // Corner rack bolts
    const auto drawBolt = [&](int bx, int by) {
        g.setColour(juce::Colour::fromRGB(45, 52, 65));
        g.fillEllipse(static_cast<float>(bx), static_cast<float>(by), 10.0f, 10.0f);
        g.setColour(juce::Colour::fromRGB(20, 22, 28));
        g.fillEllipse(static_cast<float>(bx + 2), static_cast<float>(by + 2), 6.0f, 6.0f);
        g.setColour(juce::Colour::fromRGB(80, 90, 110));
        g.drawLine(static_cast<float>(bx + 3), static_cast<float>(by + 5), static_cast<float>(bx + 7), static_cast<float>(by + 5), 1.0f);
        g.drawLine(static_cast<float>(bx + 5), static_cast<float>(by + 3), static_cast<float>(bx + 5), static_cast<float>(by + 7), 1.0f);
    };
    drawBolt(4, 4);
    drawBolt(getWidth() - 14, 4);
    drawBolt(4, getHeight() - 14);
    drawBolt(getWidth() - 14, getHeight() - 14);

    // Studio Rack Badge & Title
    g.setColour(juce::Colour::fromRGB(120, 135, 160));
    g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
    g.drawText("STUDIO RACK", 20, 4, 90, 16, juce::Justification::centredLeft);

    g.setColour(juce::Colour::fromRGB(0, 225, 245));
    g.setFont(juce::FontOptions(13.5f, juce::Font::bold));
    g.drawText(m_pluginName, 20, 18, std::max(120, getWidth() - 560), 22, juce::Justification::centredLeft);

    // Tile modular backgrounds in Rotary Grid mode
    if (m_container && m_layoutMode == RackLayoutMode::RotaryGrid)
    {
        const int w = m_viewport.getWidth() - 12;
        const int cols = std::clamp(w / 125, 2, 8);
        const int tileW = w / cols;
        constexpr int tileH = 130;

        for (std::size_t i = 0; i < m_tiles.size(); ++i)
        {
            const int col = static_cast<int>(i) % cols;
            const int row = static_cast<int>(i) / cols;
            const int tx = col * tileW + 4;
            const int ty = row * tileH + 6 + 44 - m_viewport.getViewPositionY();

            if (ty + tileH < 44 || ty > getHeight())
                continue;

            juce::Rectangle<float> cardRect(static_cast<float>(tx), static_cast<float>(ty),
                                            static_cast<float>(tileW - 8), static_cast<float>(tileH - 6));
            g.setColour(juce::Colour::fromRGB(18, 21, 28));
            g.fillRoundedRectangle(cardRect, 4.0f);
            g.setColour(juce::Colour::fromRGB(32, 38, 50));
            g.drawRoundedRectangle(cardRect, 4.0f, 1.0f);
        }
    }
}

void GenericPluginEditorView::resized()
{
    auto bounds = getLocalBounds();
    auto header = bounds.removeFromTop(44).reduced(8, 6);

    // Left controls
    header.removeFromLeft(14); // rack bolt margin
    const int titleSpace = std::max(120, getWidth() - 560);
    header.removeFromLeft(titleSpace);

    m_pwrBtn.setBounds(header.removeFromLeft(48));
    header.removeFromLeft(6);
    m_layoutToggleBtn.setBounds(header.removeFromLeft(82));
    header.removeFromLeft(10);

    // Right controls
    m_searchFilter.setBounds(header.removeFromRight(100));
    header.removeFromRight(6);
    m_savePresetBtn.setBounds(header.removeFromRight(46));
    header.removeFromRight(4);
    m_copyAbBtn.setBounds(header.removeFromRight(42));
    header.removeFromRight(4);
    m_abToggleBtn.setBounds(header.removeFromRight(42));
    header.removeFromRight(6);

    for (int i = 4; i >= 0; --i)
    {
        m_starButtons[static_cast<std::size_t>(i)].setBounds(header.removeFromRight(18));
    }
    header.removeFromRight(6);
    m_presetCombo.setBounds(header.reduced(2, 0));

    m_viewport.setBounds(bounds.reduced(10, 8));

    if (m_container)
    {
        const int w = m_viewport.getWidth() - 12;
        if (m_layoutMode == RackLayoutMode::RotaryGrid)
        {
            const int cols = std::clamp(w / 125, 2, 8);
            const int tileW = w / cols;
            constexpr int tileH = 130;

            for (std::size_t i = 0; i < m_tiles.size(); ++i)
            {
                auto& tile = m_tiles[i];
                const int col = static_cast<int>(i) % cols;
                const int row = static_cast<int>(i) / cols;
                const int tx = col * tileW + 4;
                const int ty = row * tileH + 6;

                juce::Rectangle<int> tileRect(tx, ty, tileW - 8, tileH - 6);
                if (tile.nameLabel)
                    tile.nameLabel->setBounds(tileRect.removeFromTop(20));
                if (tile.valueLabel)
                    tile.valueLabel->setBounds(tileRect.removeFromBottom(22).reduced(10, 0));
                if (tile.slider)
                    tile.slider->setBounds(tileRect.reduced(4, 2));
            }

            const int totalRows = static_cast<int>((m_tiles.size() + static_cast<std::size_t>(cols) - 1) / static_cast<std::size_t>(cols));
            const int totalH = totalRows * tileH + 20;
            m_container->setSize(w, std::max(totalH, m_viewport.getHeight()));
        }
        else // FaderRack
        {
            constexpr int stripW = 72;
            const int totalW = static_cast<int>(m_tiles.size()) * stripW + 20;

            for (std::size_t i = 0; i < m_tiles.size(); ++i)
            {
                auto& tile = m_tiles[i];
                const int tx = static_cast<int>(i) * stripW + 6;
                juce::Rectangle<int> stripRect(tx, 6, stripW - 8, m_viewport.getHeight() - 16);

                if (tile.nameLabel)
                    tile.nameLabel->setBounds(stripRect.removeFromTop(22));
                if (tile.valueLabel)
                    tile.valueLabel->setBounds(stripRect.removeFromBottom(22).reduced(4, 0));
                if (tile.slider)
                    tile.slider->setBounds(stripRect.reduced(4, 4));
            }

            m_container->setSize(std::max(totalW, w), m_viewport.getHeight());
        }
    }
}

} // namespace dawg::ui
