#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <string>
#include <vector>

namespace dawg::ui {

// Spotlight / Command Palette Overlay (Ctrl+K / Cmd+K)
// Keyboard-navigable quick-action palette for instant navigation to any panel,
// workspace preset, transport command, or editing tool.
class CommandPaletteOverlay final : public juce::Component
{
public:
    struct PaletteItem
    {
        std::string id;
        std::string title;
        std::string category;
        std::string shortcut;
    };

    using ActionCallback = std::function<void(const std::string& actionId)>;

    explicit CommandPaletteOverlay(ActionCallback actionCb);
    ~CommandPaletteOverlay() override = default;

    void open();
    void close();
    [[nodiscard]] bool isOpen() const noexcept { return isVisible(); }

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    void rebuildCatalog();
    void filterItems();
    void executeSelected();

    ActionCallback m_actionCb;
    juce::TextEditor m_searchEditor;

    std::vector<PaletteItem> m_allCatalog;
    std::vector<PaletteItem> m_filteredCatalog;
    int m_selectedIndex {0};

    juce::Rectangle<int> m_dialogBounds;
};

} // namespace dawg::ui
