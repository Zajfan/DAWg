#include "SampleBrowserPanel.h"

#include <algorithm>
#include <cstdlib>

namespace dawg::ui {

SampleBrowserPanel::SampleBrowserPanel(SampleImportCallback importCallback,
                                       SampleAuditionCallback auditionCallback)
    : m_importCallback(std::move(importCallback))
    , m_auditionCallback(std::move(auditionCallback))
{
    m_headerLabel.setText("Audio Sample Browser", juce::dontSendNotification);
    m_headerLabel.setFont(juce::FontOptions(14.0f).withStyle("Bold"));
    m_headerLabel.setColour(juce::Label::textColourId, juce::Colour(0xfff0f6fc));
    addAndMakeVisible(m_headerLabel);

    m_pathLabel.setFont(juce::FontOptions(11.0f));
    m_pathLabel.setColour(juce::Label::textColourId, juce::Colour(0xff8b949e));
    addAndMakeVisible(m_pathLabel);

    m_searchBox.setTextToShowWhenEmpty("Search audio samples...", juce::Colour(0xff8b949e));
    m_searchBox.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff161b22));
    m_searchBox.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff30363d));
    m_searchBox.setColour(juce::TextEditor::textColourId, juce::Colour(0xffc9d1d9));
    m_searchBox.onTextChange = [this] { filterFiles(m_searchBox.getText()); };
    addAndMakeVisible(m_searchBox);

    auto setupNavBtn = [this](juce::TextButton& btn) {
        btn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff21262d));
        btn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffc9d1d9));
        addAndMakeVisible(btn);
    };

    setupNavBtn(m_parentDirButton);
    m_parentDirButton.onClick = [this] {
        if (m_currentDir.has_parent_path())
            setDirectory(m_currentDir.parent_path());
    };

    setupNavBtn(m_homeDirButton);
    m_homeDirButton.onClick = [this] {
        const char* home = std::getenv("HOME");
        if (home != nullptr)
            setDirectory(std::filesystem::path(home));
    };

    setupNavBtn(m_tmpDirButton);
    m_tmpDirButton.onClick = [this] {
        setDirectory(std::filesystem::temp_directory_path());
    };

    m_fileListBox.setModel(this);
    m_fileListBox.setRowHeight(28);
    m_fileListBox.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff0d1117));
    m_fileListBox.setColour(juce::ListBox::outlineColourId, juce::Colour(0xff30363d));
    m_fileListBox.addMouseListener(this, true);
    addAndMakeVisible(m_fileListBox);

    m_syncTempoToggle.setColour(juce::ToggleButton::textColourId, juce::Colour(0xffc9d1d9));
    m_syncTempoToggle.setToggleState(false, juce::dontSendNotification);
    addAndMakeVisible(m_syncTempoToggle);

    setupNavBtn(m_playButton);
    m_playButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff238636));
    m_playButton.onClick = [this] {
        if (m_selectedRow >= 0 && static_cast<std::size_t>(m_selectedRow) < m_filteredFiles.size())
        {
            if (m_auditionCallback)
                m_auditionCallback(m_filteredFiles[static_cast<std::size_t>(m_selectedRow)].path, m_syncTempoToggle.getToggleState());
        }
    };

    setupNavBtn(m_stopButton);
    m_stopButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff30363d));
    m_stopButton.onClick = [this] {
        if (m_auditionCallback)
            m_auditionCallback({}, false);
    };

    setupNavBtn(m_importButton);
    m_importButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1f6feb));
    m_importButton.onClick = [this] {
        if (m_selectedRow >= 0 && static_cast<std::size_t>(m_selectedRow) < m_filteredFiles.size())
        {
            const auto& file = m_filteredFiles[static_cast<std::size_t>(m_selectedRow)];
            if (std::filesystem::is_directory(file.path))
            {
                setDirectory(file.path);
            }
            else if (m_importCallback)
            {
                m_importCallback(file.path);
            }
        }
    };

    const char* home = std::getenv("HOME");
    if (home != nullptr)
        setDirectory(std::filesystem::path(home));
    else
        setDirectory(std::filesystem::current_path());
}

void SampleBrowserPanel::setDirectory(const std::filesystem::path& dir)
{
    std::error_code ec;
    if (! std::filesystem::exists(dir, ec) || ! std::filesystem::is_directory(dir, ec))
        return;

    m_currentDir = std::filesystem::canonical(dir, ec);
    if (ec)
        m_currentDir = dir;

    m_pathLabel.setText(m_currentDir.string(), juce::dontSendNotification);
    refreshFiles();
}

void SampleBrowserPanel::refreshFiles()
{
    m_allFiles.clear();
    std::error_code ec;

    // Subdirectories first
    for (const auto& entry : std::filesystem::directory_iterator(m_currentDir, std::filesystem::directory_options::skip_permission_denied, ec))
    {
        if (entry.is_directory(ec))
        {
            const auto filename = entry.path().filename().string();
            if (! filename.empty() && filename.front() != '.')
            {
                m_allFiles.push_back({
                    .path = entry.path(),
                    .filename = "[DIR] " + filename,
                    .sizeBytes = 0,
                    .extension = "dir"
                });
            }
        }
    }

    // Sort directories alphabetically
    std::sort(m_allFiles.begin(), m_allFiles.end(), [](const SampleFileInfo& a, const SampleFileInfo& b) {
        return a.filename < b.filename;
    });

    const std::size_t dirCount = m_allFiles.size();

    // Audio files
    for (const auto& entry : std::filesystem::directory_iterator(m_currentDir, std::filesystem::directory_options::skip_permission_denied, ec))
    {
        if (entry.is_regular_file(ec))
        {
            auto ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".wav" || ext == ".flac" || ext == ".aiff" || ext == ".aif" || ext == ".ogg")
            {
                m_allFiles.push_back({
                    .path = entry.path(),
                    .filename = entry.path().filename().string(),
                    .sizeBytes = entry.file_size(ec),
                    .extension = ext
                });
            }
        }
    }

    // Sort files alphabetically
    std::sort(m_allFiles.begin() + static_cast<std::ptrdiff_t>(dirCount), m_allFiles.end(), [](const SampleFileInfo& a, const SampleFileInfo& b) {
        return a.filename < b.filename;
    });

    filterFiles(m_searchBox.getText());
}

void SampleBrowserPanel::filterFiles(const juce::String& query)
{
    m_filteredFiles.clear();
    const auto lower = query.toLowerCase();

    for (const auto& file : m_allFiles)
    {
        if (query.isEmpty() || juce::String(file.filename).toLowerCase().contains(lower))
            m_filteredFiles.push_back(file);
    }

    m_selectedRow = -1;
    m_fileListBox.updateContent();
    m_fileListBox.repaint();
}

int SampleBrowserPanel::getNumRows()
{
    return static_cast<int>(m_filteredFiles.size());
}

void SampleBrowserPanel::paintListBoxItem(const int rowNumber, juce::Graphics& g,
                                          const int width, const int height, const bool rowIsSelected)
{
    if (rowNumber < 0 || static_cast<std::size_t>(rowNumber) >= m_filteredFiles.size())
        return;

    const auto& file = m_filteredFiles[static_cast<std::size_t>(rowNumber)];
    const bool isDir = file.extension == "dir";

    if (rowIsSelected)
        g.fillAll(juce::Colour(0xff1f6feb));
    else if (rowNumber % 2 == 0)
        g.fillAll(juce::Colour(0xff161b22));
    else
        g.fillAll(juce::Colour(0xff0d1117));

    g.setColour(isDir ? juce::Colour(0xffe3b341) : (rowIsSelected ? juce::Colours::white : juce::Colour(0xffc9d1d9)));
    g.setFont(isDir ? juce::FontOptions(12.0f).withStyle("Bold") : juce::FontOptions(12.0f));

    g.drawText(file.filename, 8, 0, width - 80, height, juce::Justification::centredLeft, true);

    if (! isDir && file.sizeBytes > 0)
    {
        g.setColour(rowIsSelected ? juce::Colour(0xffd0d7de) : juce::Colour(0xff8b949e));
        g.setFont(juce::FontOptions(10.0f));
        const double sizeKb = static_cast<double>(file.sizeBytes) / 1024.0;
        juce::String sizeStr = (sizeKb > 1024.0)
            ? juce::String(sizeKb / 1024.0, 1) + " MB"
            : juce::String(sizeKb, 0) + " KB";
        g.drawText(sizeStr, width - 75, 0, 65, height, juce::Justification::centredRight);
    }
}

void SampleBrowserPanel::selectedRowsChanged(const int lastRowSelected)
{
    m_selectedRow = lastRowSelected;
}

void SampleBrowserPanel::listBoxItemDoubleClicked(const int row, const juce::MouseEvent& /*e*/)
{
    if (row < 0 || static_cast<std::size_t>(row) >= m_filteredFiles.size())
        return;

    const auto& file = m_filteredFiles[static_cast<std::size_t>(row)];
    if (file.extension == "dir" || std::filesystem::is_directory(file.path))
    {
        setDirectory(file.path);
    }
    else
    {
        if (m_auditionCallback)
            m_auditionCallback(file.path, m_syncTempoToggle.getToggleState());
    }
}

void SampleBrowserPanel::mouseDrag(const juce::MouseEvent& e)
{
    if (e.getDistanceFromDragStart() > 6 && m_selectedRow >= 0 && static_cast<std::size_t>(m_selectedRow) < m_filteredFiles.size())
    {
        const auto& file = m_filteredFiles[static_cast<std::size_t>(m_selectedRow)];
        if (file.extension != "dir" && ! std::filesystem::is_directory(file.path))
        {
            juce::DragAndDropContainer::performExternalDragDropOfFiles({ file.path.string() }, false, this);
        }
    }
}

void SampleBrowserPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff161b22));
    g.setColour(juce::Colour(0xff30363d));
    g.drawRect(getLocalBounds(), 1);
}

void SampleBrowserPanel::resized()
{
    const int pad = 8;
    const int w = getWidth() - 2 * pad;
    int y = pad;

    m_headerLabel.setBounds(pad, y, w, 22);
    y += 24;

    m_pathLabel.setBounds(pad, y, w, 18);
    y += 20;

    const int navBtnWidth = (w - 2 * 4) / 3;
    m_parentDirButton.setBounds(pad, y, navBtnWidth, 24);
    m_homeDirButton.setBounds(pad + navBtnWidth + 4, y, navBtnWidth, 24);
    m_tmpDirButton.setBounds(pad + 2 * (navBtnWidth + 4), y, navBtnWidth, 24);
    y += 28;

    m_searchBox.setBounds(pad, y, w, 26);
    y += 30;

    const int bottomBarHeight = 65;
    m_fileListBox.setBounds(pad, y, w, getHeight() - y - bottomBarHeight - pad);

    const int botY = getHeight() - bottomBarHeight;
    m_syncTempoToggle.setBounds(pad, botY, 110, 24);

    const int playBtnW = (w - 120 - 4) / 2;
    m_playButton.setBounds(pad + 115, botY, playBtnW, 24);
    m_stopButton.setBounds(pad + 115 + playBtnW + 4, botY, playBtnW, 24);

    m_importButton.setBounds(pad, botY + 28, w, 28);
}

} // namespace dawg::ui
