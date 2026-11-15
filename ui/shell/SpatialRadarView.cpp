#include "SpatialRadarView.h"

#include <algorithm>
#include <numbers>

namespace dawg::ui {

SpatialRadarView::SpatialRadarView(OnPositionChanged onPositionChanged)
    : m_onPositionChanged(std::move(onPositionChanged))
{
    // Default factory spatial track objects for immediate mixing
    m_objects = {
        {1, "Vocals", juce::Colour::fromRGB(220, 90, 70), 0.0f, 0.0f, 1.2f, 0.4f},
        {2, "Kick & Bass", juce::Colour::fromRGB(225, 175, 50), 0.0f, -15.0f, 1.0f, 0.7f},
        {3, "Lead Synth", juce::Colour::fromRGB(180, 80, 220), -35.0f, 10.0f, 2.5f, 0.35f},
        {4, "Hi-Hats", juce::Colour::fromRGB(80, 200, 230), 45.0f, 20.0f, 1.8f, 0.25f},
        {5, "Atmosphere FX", juce::Colour::fromRGB(60, 210, 140), 120.0f, 30.0f, 4.0f, 0.2f}
    };
    m_selectedIdx = 0;

    m_elevationSlider.setSliderStyle(juce::Slider::LinearVertical);
    m_elevationSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 48, 16);
    m_elevationSlider.setRange(-90.0, 90.0, 1.0);
    m_elevationSlider.setValue(0.0, juce::dontSendNotification);
    m_elevationSlider.onValueChange = [this] {
        if (m_selectedIdx >= 0 && m_selectedIdx < static_cast<int>(m_objects.size()))
        {
            m_objects[static_cast<std::size_t>(m_selectedIdx)].elevationDeg = static_cast<float>(m_elevationSlider.getValue());
            if (m_onPositionChanged)
            {
                const auto& obj = m_objects[static_cast<std::size_t>(m_selectedIdx)];
                m_onPositionChanged(obj.trackId, obj.azimuthDeg, obj.elevationDeg, obj.distanceMeters);
            }
            repaint();
        }
    };
    addAndMakeVisible(m_elevationSlider);

    m_elevationLabel.setText("Elevation", juce::dontSendNotification);
    m_elevationLabel.setFont(juce::FontOptions(11.0f));
    m_elevationLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(m_elevationLabel);

    m_azimuthLabel.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    m_azimuthLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(200, 225, 255));
    addAndMakeVisible(m_azimuthLabel);

    m_distanceLabel.setFont(juce::FontOptions(11.0f));
    m_distanceLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(150, 175, 205));
    addAndMakeVisible(m_distanceLabel);

    startTimerHz(30); // 30 FPS pulse animation
}

void SpatialRadarView::setViewModel(const EditorViewModel& model)
{
    // Synchronize tracks with radar objects
    if (! model.project.tracks.empty())
    {
        for (const auto& track : model.project.tracks)
        {
            auto it = std::find_if(m_objects.begin(), m_objects.end(),
                                   [id = track.id](const SpatialTrackObject& o) { return o.trackId == id; });
            if (it == m_objects.end())
            {
                // Assign new track puck
                const float defaultAz = (static_cast<float>(m_objects.size() % 6) - 2.5f) * 35.0f;
                m_objects.push_back({
                    track.id,
                    track.name,
                    juce::Colour::fromHSV(std::fmod(static_cast<float>(m_objects.size()) * 0.23f, 1.0f), 0.75f, 0.9f, 1.0f),
                    defaultAz,
                    0.0f,
                    1.5f + static_cast<float>(m_objects.size() % 4) * 0.5f,
                    0.0f
                });
            }
            else
            {
                it->name = track.name;
            }
        }
    }
    repaint();
}

void SpatialRadarView::updateTrackPosition(std::uint64_t trackId, float az, float el, float dist)
{
    for (auto& obj : m_objects)
    {
        if (obj.trackId == trackId)
        {
            obj.azimuthDeg = az;
            obj.elevationDeg = el;
            obj.distanceMeters = dist;
            repaint();
            break;
        }
    }
}

juce::Point<float> SpatialRadarView::polarToScreen(float azDeg, float distMeters, juce::Rectangle<float> radarArea) const noexcept
{
    const auto center = radarArea.getCentre();
    const float maxRadius = std::min(radarArea.getWidth(), radarArea.getHeight()) * 0.46f;
    // Map 0..10m to 0..maxRadius (logarithmic/sqrt scale for near-field resolution)
    const float normDist = std::clamp(std::sqrt(distMeters / 10.0f), 0.05f, 1.0f);
    const float radius = normDist * maxRadius;

    // 0 deg = Front (up, -Y), +90 deg = Right (+X), -90 deg = Left (-X)
    const float rad = (azDeg - 90.0f) * static_cast<float>(std::numbers::pi) / 180.0f;
    const float x = center.x + radius * std::cos(rad);
    const float y = center.y + radius * std::sin(rad);
    return {x, y};
}

std::pair<float, float> SpatialRadarView::screenToPolar(juce::Point<float> screenPos, juce::Rectangle<float> radarArea) const noexcept
{
    const auto center = radarArea.getCentre();
    const float maxRadius = std::min(radarArea.getWidth(), radarArea.getHeight()) * 0.46f;
    const float dx = screenPos.x - center.x;
    const float dy = screenPos.y - center.y;

    float rad = std::atan2(dy, dx);
    float azDeg = rad * 180.0f / static_cast<float>(std::numbers::pi) + 90.0f;
    while (azDeg > 180.0f) azDeg -= 360.0f;
    while (azDeg < -180.0f) azDeg += 360.0f;

    const float distPx = std::hypot(dx, dy);
    const float normDist = std::clamp(distPx / maxRadius, 0.05f, 1.0f);
    const float distMeters = std::clamp(normDist * normDist * 10.0f, 0.2f, 10.0f);

    return {azDeg, distMeters};
}

int SpatialRadarView::findPuckAt(juce::Point<int> pos, juce::Rectangle<float> radarArea) const noexcept
{
    for (int i = static_cast<int>(m_objects.size()) - 1; i >= 0; --i)
    {
        const auto pt = polarToScreen(m_objects[static_cast<std::size_t>(i)].azimuthDeg,
                                      m_objects[static_cast<std::size_t>(i)].distanceMeters,
                                      radarArea);
        if (std::hypot(static_cast<float>(pos.x) - pt.x, static_cast<float>(pos.y) - pt.y) <= 16.0f)
        {
            return i;
        }
    }
    return -1;
}

void SpatialRadarView::timerCallback()
{
    // Pulse level rings
    repaint();
}

void SpatialRadarView::paint(juce::Graphics& g)
{
    // Background
    g.fillAll(juce::Colour::fromRGB(16, 18, 24));

    auto bounds = getLocalBounds().toFloat();
    auto rightPanel = bounds.removeFromRight(90);
    auto radarArea = bounds.reduced(10);

    const auto center = radarArea.getCentre();
    const float maxR = std::min(radarArea.getWidth(), radarArea.getHeight()) * 0.46f;

    // Draw Radar Body
    g.setColour(juce::Colour::fromRGB(22, 26, 35));
    g.fillEllipse(center.x - maxR, center.y - maxR, maxR * 2.0f, maxR * 2.0f);
    g.setColour(juce::Colour::fromRGB(45, 52, 70));
    g.drawEllipse(center.x - maxR, center.y - maxR, maxR * 2.0f, maxR * 2.0f, 2.0f);

    // Concentric Range Rings: 1m, 2.5m, 5m, 10m
    const std::array<std::pair<float, const char*>, 4> rings {{
        {1.0f, "1m"}, {2.5f, "2.5m"}, {5.0f, "5m"}, {10.0f, "10m"}
    }};

    g.setFont(juce::FontOptions(9.5f));
    for (const auto& [distMeters, label] : rings)
    {
        const float r = std::sqrt(distMeters / 10.0f) * maxR;
        g.setColour(juce::Colour::fromRGBA(60, 75, 105, 120));
        g.drawEllipse(center.x - r, center.y - r, r * 2.0f, r * 2.0f, 1.0f);

        g.setColour(juce::Colour::fromRGB(100, 120, 155));
        g.drawText(label, center.x + 4, center.y - r - 6, 30, 12, juce::Justification::left);
    }

    // Crosshairs
    g.setColour(juce::Colour::fromRGBA(60, 75, 105, 90));
    g.drawLine(center.x - maxR, center.y, center.x + maxR, center.y, 1.0f);
    g.drawLine(center.x, center.y - maxR, center.x, center.y + maxR, 1.0f);

    // 45 deg diagonals
    const float diag = maxR * 0.7071f;
    g.drawLine(center.x - diag, center.y - diag, center.x + diag, center.y + diag, 1.0f);
    g.drawLine(center.x - diag, center.y + diag, center.x + diag, center.y - diag, 1.0f);

    // Orientation Labels
    g.setColour(juce::Colour::fromRGB(140, 165, 200));
    g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    g.drawText("FRONT (0°)", center.x - 40, center.y - maxR - 16, 80, 14, juce::Justification::centred);
    g.drawText("REAR (180°)", center.x - 40, center.y + maxR + 2, 80, 14, juce::Justification::centred);
    g.drawText("LEFT (-90°)", center.x - maxR - 70, center.y - 7, 65, 14, juce::Justification::centredRight);
    g.drawText("RIGHT (+90°)", center.x + maxR + 5, center.y - 7, 75, 14, juce::Justification::centredLeft);

    // Center Listener Icon (Ears + Nose)
    g.setColour(juce::Colour::fromRGB(220, 230, 245));
    g.fillEllipse(center.x - 9, center.y - 9, 18, 18);
    // Ears
    g.fillEllipse(center.x - 14, center.y - 4, 5, 8);
    g.fillEllipse(center.x + 9, center.y - 4, 5, 8);
    // Nose
    juce::Path nose;
    nose.addTriangle(center.x - 4, center.y - 8, center.x + 4, center.y - 8, center.x, center.y - 15);
    g.fillPath(nose);

    // Draw Track Pucks
    for (std::size_t i = 0; i < m_objects.size(); ++i)
    {
        const auto& obj = m_objects[i];
        const auto pt = polarToScreen(obj.azimuthDeg, obj.distanceMeters, radarArea);
        const bool isSelected = (static_cast<int>(i) == m_selectedIdx);

        // Sound Aura Ring (pulsing)
        g.setColour(obj.color.withAlpha(0.25f));
        g.fillEllipse(pt.x - 18, pt.y - 18, 36, 36);

        // Main Puck Disc
        g.setColour(obj.color);
        g.fillEllipse(pt.x - 12, pt.y - 12, 24, 24);

        // Border selection
        g.setColour(isSelected ? juce::Colours::white : obj.color.darker(0.3f));
        g.drawEllipse(pt.x - 12, pt.y - 12, 24, 24, isSelected ? 2.5f : 1.2f);

        // Track Name Badge
        g.setColour(juce::Colour::fromRGBA(10, 12, 16, 180));
        juce::Rectangle<float> badgeRect(pt.x - 30, pt.y + 13, 60, 13);
        g.fillRoundedRectangle(badgeRect, 2.0f);
        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        g.drawText(obj.name, badgeRect, juce::Justification::centred, true);
    }
}

void SpatialRadarView::resized()
{
    auto bounds = getLocalBounds();
    auto rightPanel = bounds.removeFromRight(90).reduced(8);

    m_azimuthLabel.setBounds(rightPanel.removeFromTop(20));
    m_distanceLabel.setBounds(rightPanel.removeFromTop(18));
    rightPanel.removeFromTop(10);
    m_elevationLabel.setBounds(rightPanel.removeFromTop(18));
    m_elevationSlider.setBounds(rightPanel.reduced(10, 4));

    if (m_selectedIdx >= 0 && m_selectedIdx < static_cast<int>(m_objects.size()))
    {
        const auto& obj = m_objects[static_cast<std::size_t>(m_selectedIdx)];
        m_azimuthLabel.setText(juce::String(static_cast<int>(obj.azimuthDeg)) + "° Az", juce::dontSendNotification);
        m_distanceLabel.setText(juce::String(obj.distanceMeters, 2) + "m Dist", juce::dontSendNotification);
    }
}

void SpatialRadarView::mouseDown(const juce::MouseEvent& e)
{
    auto bounds = getLocalBounds().toFloat();
    bounds.removeFromRight(90);
    auto radarArea = bounds.reduced(10);

    const int hit = findPuckAt(e.getPosition(), radarArea);
    if (hit >= 0)
    {
        m_selectedIdx = hit;
        m_draggingPuck = true;
        const auto& obj = m_objects[static_cast<std::size_t>(hit)];
        m_elevationSlider.setValue(obj.elevationDeg, juce::dontSendNotification);
        m_azimuthLabel.setText(juce::String(static_cast<int>(obj.azimuthDeg)) + "° Az", juce::dontSendNotification);
        m_distanceLabel.setText(juce::String(obj.distanceMeters, 2) + "m Dist", juce::dontSendNotification);
        repaint();
    }
}

void SpatialRadarView::mouseDrag(const juce::MouseEvent& e)
{
    if (m_draggingPuck && m_selectedIdx >= 0 && m_selectedIdx < static_cast<int>(m_objects.size()))
    {
        auto bounds = getLocalBounds().toFloat();
        bounds.removeFromRight(90);
        auto radarArea = bounds.reduced(10);

        const auto [az, dist] = screenToPolar(e.position, radarArea);
        auto& obj = m_objects[static_cast<std::size_t>(m_selectedIdx)];
        obj.azimuthDeg = az;
        obj.distanceMeters = dist;

        m_azimuthLabel.setText(juce::String(static_cast<int>(az)) + "° Az", juce::dontSendNotification);
        m_distanceLabel.setText(juce::String(dist, 2) + "m Dist", juce::dontSendNotification);

        if (m_onPositionChanged)
        {
            m_onPositionChanged(obj.trackId, obj.azimuthDeg, obj.elevationDeg, obj.distanceMeters);
        }
        repaint();
    }
}

void SpatialRadarView::mouseUp(const juce::MouseEvent&)
{
    m_draggingPuck = false;
}

} // namespace dawg::ui
