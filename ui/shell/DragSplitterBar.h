#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace dawg::ui {

class DragSplitterBar final : public juce::Component
{
public:
    enum class Orientation : std::uint8_t
    {
        horizontal, // Drags up and down (resizes height)
        vertical    // Drags left and right (resizes width)
    };

    DragSplitterBar(Orientation orientation, std::function<void(int delta)> onDrag)
        : m_orientation(orientation)
        , m_onDrag(std::move(onDrag))
    {
        setMouseCursor(m_orientation == Orientation::horizontal
                       ? juce::MouseCursor::UpDownResizeCursor
                       : juce::MouseCursor::LeftRightResizeCursor);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        m_dragStartPos = (m_orientation == Orientation::horizontal) ? e.getScreenY() : e.getScreenX();
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        const int currentPos = (m_orientation == Orientation::horizontal) ? e.getScreenY() : e.getScreenX();
        const int delta = currentPos - m_dragStartPos;
        m_dragStartPos = currentPos;
        if (m_onDrag && delta != 0)
            m_onDrag(delta);
    }

    void mouseDoubleClick(const juce::MouseEvent&) override
    {
        if (m_onDoubleClick)
            m_onDoubleClick();
    }

    void setOnDoubleClick(std::function<void()> cb)
    {
        m_onDoubleClick = std::move(cb);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour::fromRGB(28, 30, 36));
        g.setColour(juce::Colour::fromRGB(55, 60, 72));

        if (m_orientation == Orientation::horizontal)
        {
            const float midY = static_cast<float>(getHeight()) * 0.5f;
            g.drawHorizontalLine(static_cast<int>(midY),
                                 static_cast<float>(getWidth() / 2 - 24),
                                 static_cast<float>(getWidth() / 2 + 24));
        }
        else
        {
            const float midX = static_cast<float>(getWidth()) * 0.5f;
            g.drawVerticalLine(static_cast<int>(midX),
                               static_cast<float>(getHeight() / 2 - 24),
                               static_cast<float>(getHeight() / 2 + 24));
        }
    }

private:
    Orientation m_orientation;
    std::function<void(int delta)> m_onDrag;
    std::function<void()> m_onDoubleClick;
    int m_dragStartPos {0};
};

} // namespace dawg::ui
