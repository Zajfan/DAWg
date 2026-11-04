#include "WindowRegistry.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace dawg::app::window {

std::optional<WindowHandle> WindowRegistry::registerWindow(WindowRegistration registration)
{
    const auto parent = registration.snapshot.parent;
    if (parent != 0 && !contains(parent))
        return std::nullopt;

    registration.limits.minimumWidth = std::max(1, registration.limits.minimumWidth);
    registration.limits.minimumHeight = std::max(1, registration.limits.minimumHeight);
    if ((registration.limits.maximumWidth != 0
         && registration.limits.maximumWidth < registration.limits.minimumWidth)
        || (registration.limits.maximumHeight != 0
            && registration.limits.maximumHeight < registration.limits.minimumHeight))
        return std::nullopt;

    const auto handle = m_nextHandle++;
    registration.snapshot.handle = handle;

    m_windows.emplace(handle,
                      Record { std::move(registration.snapshot), registration.limits,
                               std::move(registration.completion) });
    return handle;
}

bool WindowRegistry::contains(WindowHandle handle) const noexcept
{
    return m_windows.find(handle) != m_windows.end();
}

std::optional<WindowSnapshot> WindowRegistry::snapshot(WindowHandle handle) const
{
    const auto it = m_windows.find(handle);
    if (it == m_windows.end())
        return std::nullopt;

    return it->second.snapshot;
}

bool WindowRegistry::resize(WindowHandle handle, WindowBounds bounds)
{
    const auto it = m_windows.find(handle);
    if (it == m_windows.end())
        return false;

    bounds.width = std::max(bounds.width, it->second.limits.minimumWidth);
    bounds.height = std::max(bounds.height, it->second.limits.minimumHeight);
    if (it->second.limits.maximumWidth > 0)
        bounds.width = std::min(bounds.width, it->second.limits.maximumWidth);
    if (it->second.limits.maximumHeight > 0)
        bounds.height = std::min(bounds.height, it->second.limits.maximumHeight);

    it->second.snapshot.bounds = bounds;
    return true;
}

bool WindowRegistry::setState(WindowHandle handle, WindowState state)
{
    const auto it = m_windows.find(handle);
    if (it == m_windows.end())
        return false;

    it->second.snapshot.state = state;
    return true;
}

bool WindowRegistry::setOpacity(WindowHandle handle, float opacity)
{
    const auto it = m_windows.find(handle);
    if (it == m_windows.end())
        return false;

    it->second.snapshot.opacity = std::clamp(opacity, 0.0f, 1.0f);
    return true;
}

std::vector<ClosedWindow> WindowRegistry::removeTree(WindowHandle handle)
{
    if (!contains(handle))
        return {};

    std::vector<WindowHandle> removalOrder;
    const auto collect = [this, &removalOrder](auto&& self, WindowHandle current) -> void {
        std::vector<WindowHandle> children;
        for (const auto& [candidate, record] : m_windows) {
            if (record.snapshot.parent == current)
                children.push_back(candidate);
        }

        std::sort(children.begin(), children.end());
        for (const auto child : children)
            self(self, child);

        removalOrder.push_back(current);
    };
    collect(collect, handle);

    std::vector<ClosedWindow> closedWindows;
    closedWindows.reserve(removalOrder.size());
    for (const auto current : removalOrder) {
        const auto it = m_windows.find(current);
        if (it == m_windows.end())
            continue;

        std::function<void(int)> completion;
        if (it->second.snapshot.modal)
            completion = std::move(it->second.completion);
        closedWindows.emplace_back(current, std::move(completion));
        m_windows.erase(it);
    }

    return closedWindows;
}

bool WindowRegistry::moveToDisplay(WindowHandle handle, const DisplayInfo& display)
{
    const auto it = m_windows.find(handle);
    if (it == m_windows.end())
        return false;

    auto bounds = it->second.snapshot.bounds;
    const auto& userArea = display.logicalUserBounds;
    const auto maximumX = userArea.x + std::max(0, userArea.width - bounds.width);
    const auto maximumY = userArea.y + std::max(0, userArea.height - bounds.height);
    bounds.x = std::clamp(bounds.x, userArea.x, maximumX);
    bounds.y = std::clamp(bounds.y, userArea.y, maximumY);

    it->second.snapshot.bounds = bounds;
    it->second.snapshot.display = display.id;
    return true;
}

WindowBounds WindowRegistry::logicalToPhysical(const DisplayInfo& display, WindowBounds logical)
{
    const auto scale = display.scale > 0.0 ? display.scale : 1.0;
    return {
        display.physicalBounds.x
            + static_cast<int>(std::lround((logical.x - display.logicalUserBounds.x) * scale)),
        display.physicalBounds.y
            + static_cast<int>(std::lround((logical.y - display.logicalUserBounds.y) * scale)),
        static_cast<int>(std::lround(logical.width * scale)),
        static_cast<int>(std::lround(logical.height * scale)),
    };
}

WindowBounds WindowRegistry::physicalToLogical(const DisplayInfo& display, WindowBounds physical)
{
    const auto scale = display.scale > 0.0 ? display.scale : 1.0;
    return {
        display.logicalUserBounds.x
            + static_cast<int>(std::lround((physical.x - display.physicalBounds.x) / scale)),
        display.logicalUserBounds.y
            + static_cast<int>(std::lround((physical.y - display.physicalBounds.y) / scale)),
        static_cast<int>(std::lround(physical.width / scale)),
        static_cast<int>(std::lround(physical.height / scale)),
    };
}

} // namespace dawg::app::window
