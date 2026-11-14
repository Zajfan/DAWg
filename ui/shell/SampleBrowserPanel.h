#pragma once

#include <dawg/engine/WsolaTimeStretcher.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace dawg::ui {

struct SampleFileInfo
{
    std::filesystem::path path;
    std::string filename;
    std::uintmax_t sizeBytes {0};
    std::string extension;
};

class SampleBrowserPanel final : public juce::Component, public juce::ListBoxModel
{
public:
    using SampleSelectCallback = std::function<void(const std::filesystem::path& path)>;
    using SampleImportCallback = std::function<void(const std::filesystem::path& path)>;
    using SampleAuditionCallback = std::function<void(const std::filesystem::path& path, bool syncTempo)>;

    SampleBrowserPanel(SampleImportCallback importCallback = nullptr,
                       SampleAuditionCallback auditionCallback = nullptr);
    ~SampleBrowserPanel() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // ListBoxModel
    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    void selectedRowsChanged(int lastRowSelected) override;

    void setDirectory(const std::filesystem::path& dir);
    [[nodiscard]] std::filesystem::path currentDirectory() const { return m_currentDir; }
    void refreshFiles();

private:
    void filterFiles(const juce::String& query);

    SampleImportCallback m_importCallback;
    SampleAuditionCallback m_auditionCallback;

    std::filesystem::path m_currentDir;
    std::vector<SampleFileInfo> m_allFiles;
    std::vector<SampleFileInfo> m_filteredFiles;
    int m_selectedRow {-1};

    juce::Label m_headerLabel;
    juce::Label m_pathLabel;
    juce::TextEditor m_searchBox;
    juce::ListBox m_fileListBox;

    juce::TextButton m_parentDirButton {".. [Up]"};
    juce::TextButton m_homeDirButton {"Home"};
    juce::TextButton m_tmpDirButton {"Temp"};

    juce::ToggleButton m_syncTempoToggle {"Sync to BPM"};
    juce::TextButton m_playButton {"Play [Audition]"};
    juce::TextButton m_stopButton {"Stop"};
    juce::TextButton m_importButton {"+ Add to Track"};
};

} // namespace dawg::ui
