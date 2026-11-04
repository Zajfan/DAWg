#include "WindowManager.h"

#include "ManagedWindow.h"
#include "NativeWindowState.h"

#include <algorithm>
#include <limits>
#include <iostream>
#include <utility>

namespace dawg::app::window {
namespace {

juce::Colour windowBackground()
{
    return juce::Colours::darkgrey;
}

WindowBounds clampedBounds(WindowBounds bounds, const WindowSizeLimits limits)
{
    const auto minimumWidth = std::max(1, limits.minimumWidth);
    const auto minimumHeight = std::max(1, limits.minimumHeight);
    bounds.width = std::max(bounds.width, minimumWidth);
    bounds.height = std::max(bounds.height, minimumHeight);
    if (limits.maximumWidth > 0)
        bounds.width = std::min(bounds.width, std::max(minimumWidth, limits.maximumWidth));
    if (limits.maximumHeight > 0)
        bounds.height = std::min(bounds.height, std::max(minimumHeight, limits.maximumHeight));
    return bounds;
}

WindowSizeLimits limitsFor(const WindowOptions& options)
{
    return {
        std::max(1, options.minimumWidth),
        std::max(1, options.minimumHeight),
        options.maximumWidth,
        options.maximumHeight,
    };
}

WindowBounds fromJuceBounds(const juce::Rectangle<int> bounds)
{
    return { bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight() };
}

using WindowMap = std::unordered_map<WindowHandle, std::unique_ptr<ManagedWindow>>;

} // namespace

class WindowManager::ModalCallback final : public juce::ModalComponentManager::Callback {
public:
    ModalCallback(WindowManager& manager, const WindowHandle handle,
                  const std::weak_ptr<int> lifetime)
        : m_manager(&manager), m_handle(handle), m_lifetime(lifetime)
    {
    }

    void modalStateFinished(const int result) override
    {
        std::cerr << "modal callback " << m_handle << " expired=" << m_lifetime.expired() << '\\n';
        if (! m_lifetime.expired())
            m_manager->finishModal(m_handle, result);
    }

private:
    WindowManager* m_manager;
    WindowHandle m_handle;
    std::weak_ptr<int> m_lifetime;
};

WindowManager::WindowManager()
{
    m_capabilities.supportsTransparency = juce::Desktop::canUseSemiTransparentWindows();
    m_capabilities.supportsPerWindowOpacity = m_capabilities.supportsTransparency;
    m_capabilities.supportsFramelessWindows = true;
    m_capabilities.supportsFullscreen = true;
    static_cast<void>(refreshDisplays());
}

WindowManager::~WindowManager()
{
    if (! isMessageThread()) {
        jassertfalse;
        m_lifetime.reset();
        auto* deferredWindows = new WindowMap(std::move(m_windows));
        const auto queued = juce::MessageManager::callAsync([deferredWindows] {
            delete deferredWindows;
        });
        if (! queued)
            jassertfalse;
        return;
    }

    m_lifetime.reset();
    while (! m_windows.empty()) {
        const auto handle = m_windows.begin()->first;
        destroyTree(handle, 0);
    }
}

bool WindowManager::isMessageThread() const noexcept
{
    const auto* messageManager = juce::MessageManager::getInstanceWithoutCreating();
    return messageManager != nullptr && messageManager->isThisTheMessageThread();
}

std::optional<WindowHandle> WindowManager::createWindow(
    std::unique_ptr<juce::Component> content, WindowOptions options)
{
    return createWindowInternal(std::move(content), std::move(options), false, false, {});
}

std::optional<WindowHandle> WindowManager::showModalDialog(
    std::unique_ptr<juce::Component> content, DialogOptions options)
{
    return createWindowInternal(std::move(content), std::move(options.window), true,
                                options.closeOnEscape,
                                std::move(options.completion));
}

std::optional<WindowHandle> WindowManager::createWindowInternal(
    std::unique_ptr<juce::Component> content,
    WindowOptions options,
    const bool modal,
    const bool closeOnEscape,
    std::function<void(int)> completion)
{
    if (! isMessageThread() || content == nullptr)
        return std::nullopt;
    if ((options.transparent && ! m_capabilities.supportsTransparency)
        || (! options.useNativeTitleBar && ! m_capabilities.supportsFramelessWindows)
        || (options.initialState == WindowState::fullscreen && ! m_capabilities.supportsFullscreen))
        return std::nullopt;
    if (options.parent != 0 && ! m_registry.contains(options.parent))
        return std::nullopt;

    const auto limits = limitsFor(options);
    options.bounds = clampedBounds(options.bounds, limits);
    WindowRegistration registration;
    registration.snapshot.title = options.title;
    registration.snapshot.bounds = options.bounds;
    registration.snapshot.state = WindowState::normal;
    registration.snapshot.visible = options.visible;
    registration.snapshot.modal = modal;
    registration.snapshot.opacity = std::clamp(options.opacity, 0.0f, 1.0f);
    registration.snapshot.parent = options.parent;
    if (const auto* display = displayForBounds(options.bounds); display != nullptr)
        registration.snapshot.display = display->id;
    registration.limits = limits;
    registration.completion = std::move(completion);

    const auto handle = m_registry.registerWindow(std::move(registration));
    if (! handle)
        return std::nullopt;

    auto closeCallback = [this, windowHandle = *handle,
                          closeRequested = std::move(options.closeRequested)]() mutable {
        if (closeRequested)
            closeRequested(windowHandle);
        else
            static_cast<void>(closeWindow(windowHandle));
    };

    std::unique_ptr<ManagedWindow> window;
    try {
        window = std::make_unique<ManagedWindow>(juce::String(options.title), windowBackground(),
                                                 std::move(options), std::move(content),
                                                 std::move(closeCallback), closeOnEscape);
    } catch (...) {
        static_cast<void>(m_registry.removeTree(*handle));
        return std::nullopt;
    }

    if (window->getPeer() == nullptr) {
        static_cast<void>(m_registry.removeTree(*handle));
        return std::nullopt;
    }

    if (! m_capabilities.supportsMaximize)
        m_capabilities.supportsMaximize = native::queryCapabilities(window->getPeer()).supportsMaximize;

    const auto parent = options.parent;
    const auto initialState = options.initialState;
    m_windows.emplace(*handle, std::move(window));

    if (modal) {
        auto* modalWindow = m_windows.at(*handle).get();
        auto callback = std::make_unique<ModalCallback>(*this, *handle, m_lifetime);
        modalWindow->enterModalState(true, callback.get(), false);
        callback.release();
        if (parent != 0) {
            if (const auto parentWindow = m_windows.find(parent); parentWindow != m_windows.end())
                parentWindow->second->toFront(true);
        }
    }

    if (initialState != WindowState::normal && ! setWindowState(*handle, initialState)) {
        static_cast<void>(closeWindow(*handle));
        return std::nullopt;
    }
    return handle;
}

void WindowManager::destroyTree(const WindowHandle handle, const int result,
                                const bool exitModalDescendants)
{
    std::unordered_set<WindowHandle> modalHandles;
    for (const auto& [candidate, window] : m_windows) {
        const auto snapshot = m_registry.snapshot(candidate);
        if (snapshot && snapshot->modal)
            modalHandles.insert(candidate);
    }

    auto closedWindows = m_registry.removeTree(handle);
    if (exitModalDescendants) {
        for (const auto& closedWindow : closedWindows) {
            if (modalHandles.contains(closedWindow.handle))
                m_closingModals.insert(closedWindow.handle);
        }
        for (const auto& closedWindow : closedWindows) {
            if (! modalHandles.contains(closedWindow.handle))
                continue;
            if (const auto it = m_windows.find(closedWindow.handle); it != m_windows.end())
                it->second->exitModalState(result);
        }
    }

    for (const auto& closedWindow : closedWindows) {
        m_closingModals.erase(closedWindow.handle);
        m_windows.erase(closedWindow.handle);
    }
    for (auto& closedWindow : closedWindows) {
        if (closedWindow.completion) {
            auto completion = std::move(closedWindow.completion);
            completion(result);
        }
    }
}

bool WindowManager::closeWindow(const WindowHandle handle)
{
    if (! isMessageThread())
        return false;

    const auto snapshot = m_registry.snapshot(handle);
    if (! snapshot || ! m_windows.contains(handle))
        return false;

    if (snapshot->modal) {
        if (! m_closingModals.insert(handle).second)
            return true;
        m_windows.at(handle)->exitModalState(0);
        return true;
    }

    destroyTree(handle, 0);
    return true;
}

void WindowManager::finishModal(const WindowHandle handle, const int result)
{
    std::cerr << "finish modal " << handle << " registry=" << m_registry.contains(handle) << '\\n';
    if (! isMessageThread())
        return;
    if (m_registry.contains(handle))
        destroyTree(handle, result, false);
    else {
        m_closingModals.erase(handle);
        m_windows.erase(handle);
    }
}

bool WindowManager::resizeWindow(const WindowHandle handle, const WindowBounds bounds)
{
    if (! isMessageThread())
        return false;
    const auto it = m_windows.find(handle);
    if (it == m_windows.end() || ! m_registry.resize(handle, bounds))
        return false;

    const auto snapshot = m_registry.snapshot(handle);
    if (! snapshot)
        return false;
    it->second->setBounds({ snapshot->bounds.x, snapshot->bounds.y,
                            snapshot->bounds.width, snapshot->bounds.height });
    return true;
}

bool WindowManager::setWindowState(const WindowHandle handle, const WindowState state)
{
    if (! isMessageThread())
        return false;
    const auto it = m_windows.find(handle);
    const auto snapshot = m_registry.snapshot(handle);
    if (it == m_windows.end() || ! snapshot)
        return false;

    auto* window = it->second.get();
    bool success = true;
    switch (state) {
    case WindowState::normal:
        if (snapshot->state == WindowState::maximized) {
            auto* peer = window->getPeer();
            success = peer != nullptr && native::setMaximized(*peer, false);
        }
        if (success && window->isMinimised())
            window->setMinimised(false);
        if (success && window->isFullScreen())
            window->setFullScreen(false);
        break;
    case WindowState::minimized:
        if (window->isFullScreen())
            window->setFullScreen(false);
        if (snapshot->state == WindowState::maximized) {
            auto* peer = window->getPeer();
            success = peer != nullptr && native::setMaximized(*peer, false);
        }
        if (success)
            window->setMinimised(true);
        break;
    case WindowState::fullscreen:
        if (! m_capabilities.supportsFullscreen)
            return false;
        if (window->isMinimised())
            window->setMinimised(false);
        if (snapshot->state == WindowState::maximized) {
            auto* peer = window->getPeer();
            success = peer != nullptr && native::setMaximized(*peer, false);
        }
        if (success)
            window->setFullScreen(true);
        break;
    case WindowState::maximized: {
        if (! m_capabilities.supportsMaximize)
            return false;
        if (window->isMinimised())
            window->setMinimised(false);
        if (window->isFullScreen())
            window->setFullScreen(false);
        auto* peer = window->getPeer();
        success = peer != nullptr && native::setMaximized(*peer, true);
        break;
    }
    }

    return success && m_registry.setState(handle, state);
}

bool WindowManager::setWindowOpacity(const WindowHandle handle, const float opacity)
{
    if (! isMessageThread() || ! m_capabilities.supportsPerWindowOpacity)
        return false;
    const auto it = m_windows.find(handle);
    if (it == m_windows.end() || ! m_registry.setOpacity(handle, opacity))
        return false;
    const auto snapshot = m_registry.snapshot(handle);
    if (! snapshot)
        return false;
    it->second->setAlpha(snapshot->opacity);
    return true;
}

bool WindowManager::moveToDisplay(const WindowHandle handle, const DisplayId displayId)
{
    if (! isMessageThread())
        return false;
    const auto it = m_windows.find(handle);
    const auto* display = findDisplay(displayId);
    if (it == m_windows.end() || display == nullptr
        || ! m_registry.moveToDisplay(handle, *display))
        return false;

    const auto snapshot = m_registry.snapshot(handle);
    if (! snapshot)
        return false;
    it->second->setBounds({ snapshot->bounds.x, snapshot->bounds.y,
                            snapshot->bounds.width, snapshot->bounds.height });
    return true;
}

std::optional<WindowSnapshot> WindowManager::getWindow(const WindowHandle handle) const
{
    if (! isMessageThread())
        return std::nullopt;
    return m_registry.snapshot(handle);
}

WindowCapabilities WindowManager::getCapabilities() const noexcept
{
    if (! isMessageThread())
        return {};
    return m_capabilities;
}

std::vector<DisplayInfo> WindowManager::getDisplays() const
{
    if (! isMessageThread())
        return {};
    return m_displays;
}

bool WindowManager::refreshDisplays()
{
    if (! isMessageThread())
        return false;

    const auto& displays = juce::Desktop::getInstance().getDisplays().displays;
    std::vector<DisplayInfo> refreshed;
    refreshed.reserve(static_cast<std::size_t>(displays.size()));
    DisplayId nextId = 1;
    for (const auto& display : displays) {
        DisplayInfo info;
        info.id = nextId++;
        info.primary = display.isMain;
        info.logicalUserBounds = fromJuceBounds(display.userBounds.toNearestInt());
        info.logicalBounds = fromJuceBounds(display.logicalBounds.toNearestInt());
        info.physicalBounds = fromJuceBounds(display.physicalBounds);
        info.scale = display.scale;
        info.dpi = display.dpi;
        refreshed.push_back(info);
    }
    m_displays = std::move(refreshed);
    return true;
}

const DisplayInfo* WindowManager::findDisplay(const DisplayId displayId) const noexcept
{
    const auto it = std::find_if(m_displays.begin(), m_displays.end(),
                                 [displayId](const DisplayInfo& display) {
                                     return display.id == displayId;
                                 });
    return it == m_displays.end() ? nullptr : &*it;
}

const DisplayInfo* WindowManager::displayForBounds(const WindowBounds bounds) const noexcept
{
    const auto centerX = bounds.x + bounds.width / 2;
    const auto centerY = bounds.y + bounds.height / 2;
    for (const auto& display : m_displays) {
        const auto& area = display.logicalUserBounds;
        if (centerX >= area.x && centerX < area.x + area.width
            && centerY >= area.y && centerY < area.y + area.height)
            return &display;
    }

    const auto primary = std::find_if(m_displays.begin(), m_displays.end(),
                                      [](const DisplayInfo& display) {
                                          return display.primary;
                                      });
    return primary == m_displays.end()
               ? (m_displays.empty() ? nullptr : &m_displays.front())
               : &*primary;
}

std::optional<WindowBounds> WindowManager::logicalToPhysical(
    const DisplayId displayId, const WindowBounds bounds) const
{
    if (! isMessageThread())
        return std::nullopt;
    const auto* display = findDisplay(displayId);
    return display == nullptr ? std::nullopt
                              : std::optional<WindowBounds>(WindowRegistry::logicalToPhysical(
                                    *display, bounds));
}

std::optional<WindowBounds> WindowManager::physicalToLogical(
    const DisplayId displayId, const WindowBounds bounds) const
{
    if (! isMessageThread())
        return std::nullopt;
    const auto* display = findDisplay(displayId);
    return display == nullptr ? std::nullopt
                              : std::optional<WindowBounds>(WindowRegistry::physicalToLogical(
                                    *display, bounds));
}

} // namespace dawg::app::window
