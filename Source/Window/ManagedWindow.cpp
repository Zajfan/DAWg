#include "ManagedWindow.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace dawg::app::window {
namespace {

juce::Rectangle<int> toJuceBounds(const WindowBounds bounds)
{
    return { bounds.x, bounds.y, bounds.width, bounds.height };
}

int unlimitedSize()
{
    return std::numeric_limits<int>::max() / 2;
}

} // namespace

ManagedWindow::ManagedWindow(const juce::String& name,
                             const juce::Colour backgroundColour,
                             WindowOptions options,
                             std::unique_ptr<juce::Component> content,
                             CloseRequested closeRequested,
                             const bool closeOnEscape)
    : juce::ResizableWindow(name, backgroundColour, true),
      m_closeRequested(std::move(closeRequested)),
      m_closeOnEscape(closeOnEscape)
{
    setUsingNativeTitleBar(options.useNativeTitleBar);
    setResizable(options.resizable, false);
    setResizeLimits(std::max(1, options.minimumWidth), std::max(1, options.minimumHeight),
                    options.maximumWidth > 0 ? options.maximumWidth : unlimitedSize(),
                    options.maximumHeight > 0 ? options.maximumHeight : unlimitedSize());
    setAlwaysOnTop(options.alwaysOnTop);
    setOpaque(! options.transparent);
    setContentOwned(content.release(), true);
    setBounds(toJuceBounds(options.bounds));
    setAlpha(std::clamp(options.opacity, 0.0f, 1.0f));
    setVisible(options.visible);
}

ManagedWindow::~ManagedWindow() = default;

#if JUCE_LINUX || JUCE_BSD
int ManagedWindow::getDesktopWindowStyleFlags() const
{
    // JUCE translates this into MWM_FUNC_MAXIMIZE. Without it, GNOME rejects
    // EWMH maximize requests even when the window is resizable.
    const auto styleFlags = juce::ResizableWindow::getDesktopWindowStyleFlags();
    return isResizable() ? styleFlags | juce::ComponentPeer::windowHasMaximiseButton : styleFlags;
}
#endif

void ManagedWindow::userTriedToCloseWindow()
{
    if (m_closeRequested)
        m_closeRequested();
}

bool ManagedWindow::keyPressed(const juce::KeyPress& key)
{
    if (m_closeOnEscape && key == juce::KeyPress::escapeKey && m_closeRequested) {
        m_closeRequested();
        return true;
    }

    return juce::ResizableWindow::keyPressed(key);
}

} // namespace dawg::app::window
