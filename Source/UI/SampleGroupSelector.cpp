#include "SampleGroupSelector.h"
#include <algorithm>

namespace
{
constexpr int pageSize = 9;
constexpr int cellGap = 12;
const juce::Colour idle(0xff8e8b8b), active(0xffca7a7a);

class CircleButton final : public juce::Button
{
public:
    explicit CircleButton(const juce::String& name) : Button(name) {}
    void paintButton(juce::Graphics& g, bool, bool) override
    {
        auto bounds = getLocalBounds().toFloat().reduced(1.0f);
        g.setColour(juce::Colour(0xffa5a4a4));
        g.fillEllipse(bounds);
        g.setColour(juce::Colours::black);
        g.drawEllipse(bounds, getToggleState() ? 2.0f : 1.0f);
        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        g.drawText(getName(), getLocalBounds(), juce::Justification::centred);
    }
};
}

class SampleGroupSelector::Cell final : public juce::Component
{
public:
    Cell(SampleGroupSelector& parent, int number) : owner(parent), sampleNumber(number)
    {
        for (auto* button : { &lp, &ed, &rp })
            addChildComponent(*button);
        addMouseListener(this, true);
        lp.setTooltip("Open cursor shake area");
        ed.setTooltip("Edit sample");
        rp.setTooltip("Replace sample (not yet available)");
        lp.onClick = [this]
        {
            if (owner.onShake && directionalGroup >= 0)
                owner.onShake(directionalGroup);
        };
        ed.onClick = [this]
        {
            if (owner.onEdit)
                owner.onEdit(editGroup);
        };
    }
    ~Cell() override { removeMouseListener(this); }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds();
        g.fillAll(juce::Colour(0xffd9d9d9));
        g.setColour(hovered ? juce::Colours::black : playing ? active : idle);
        g.drawRect(bounds, 1);
        g.setFont(juce::Font(juce::FontOptions(13.0f)));
        if (!hovered)
            g.drawFittedText(notes, bounds.reduced(10).withHeight(44),
                             juce::Justification::topLeft, 3);
        g.setFont(juce::Font(juce::FontOptions(18.0f)));
        g.drawText(juce::String(sampleNumber), bounds.withTrimmedTop(juce::jmax(0, getHeight() - 32)),
                   juce::Justification::centred);
    }
    void resized() override
    {
        constexpr int diameter = 30, gap = 8;
        const int left = (getWidth() - diameter * 3 - gap * 2) / 2;
        lp.setBounds(left, 8, diameter, diameter);
        ed.setBounds(left + diameter + gap, 8, diameter, diameter);
        rp.setBounds(left + (diameter + gap) * 2, 8, diameter, diameter);
    }
    void mouseEnter(const juce::MouseEvent&) override { updateHover(); }
    void mouseExit(const juce::MouseEvent&) override { updateHover(); }
    void updateHover()
    {
        hovered = isMouseOver(true);
        for (auto* button : { &lp, &ed, &rp })
            button->setVisible(hovered);
        repaint();
    }
    SampleGroupSelector& owner;
    int sampleNumber, editGroup = -1, directionalGroup = -1;
    std::vector<int> groups;
    juce::String notes;
    bool playing = false, hovered = false;
    CircleButton lp { "LP" }, ed { "ED" }, rp { "RP" };
};

class SampleGroupSelector::PageButton final : public juce::Button
{
public:
    explicit PageButton(int page) : Button("Sample page " + juce::String(page + 1)) {}
    void paintButton(juce::Graphics& g, bool, bool) override
    {
        g.setColour(playing ? active : getToggleState() ? juce::Colour(0xff696868)
                                                       : juce::Colour(0xffb2b2b2));
        g.fillRect(0, getHeight() / 2 - 2, getWidth(), 4);
    }
    bool playing = false;
};

SampleGroupSelector::SampleGroupSelector() = default;
SampleGroupSelector::~SampleGroupSelector() = default;

void SampleGroupSelector::setSampleGroups(
    const std::vector<PercussionSampleLibrary::SampleGroupInfo>& groups)
{
    cells.clear();
    pages.clear();
    groupPlaying.assign(groups.size(), false);
    for (int i = 0; i < static_cast<int>(groups.size()); ++i)
    {
        const auto& group = groups[static_cast<size_t>(i)];
        auto found = std::find_if(cells.begin(), cells.end(), [&] (const auto& cell)
        { return cell->sampleNumber == group.noteIndex; });
        if (found == cells.end())
        {
            cells.push_back(std::make_unique<Cell>(*this, group.noteIndex));
            found = cells.end() - 1;
            addAndMakeVisible(**found);
        }
        auto& cell = **found;
        cell.groups.push_back(i);
        if (cell.editGroup < 0)
            cell.editGroup = i;
        if (cell.notes.isNotEmpty())
            cell.notes += " / ";
        cell.notes += juce::MidiMessage::getMidiNoteName(group.midiNote, true, true, 3);
        if (group.hasDirectionalSamples)
        {
            cell.notes += " / " + juce::MidiMessage::getMidiNoteName(group.downMidiNote, true, true, 3);
            if (cell.directionalGroup < 0)
                cell.directionalGroup = i;
        }
        cell.lp.setEnabled(cell.directionalGroup >= 0);
    }
    const int count = (static_cast<int>(cells.size()) + pageSize - 1) / pageSize;
    for (int i = 0; i < count; ++i)
    {
        auto button = std::make_unique<PageButton>(i);
        button->onClick = [this, i] { selectPage(i); };
        addAndMakeVisible(*button);
        pages.push_back(std::move(button));
    }
    selectPage(0);
}

void SampleGroupSelector::selectPage(int page)
{
    selectedPage = page;
    for (int i = 0; i < static_cast<int>(cells.size()); ++i)
        cells[static_cast<size_t>(i)]->setVisible(i / pageSize == page);
    for (int i = 0; i < static_cast<int>(pages.size()); ++i)
        pages[static_cast<size_t>(i)]->setToggleState(i == page, juce::dontSendNotification);
    resized();
}

void SampleGroupSelector::setGroupPlaying(int groupIndex, bool playing)
{
    if (groupIndex < 0 || groupIndex >= static_cast<int>(groupPlaying.size())
        || groupPlaying[static_cast<size_t>(groupIndex)] == playing)
        return;
    groupPlaying[static_cast<size_t>(groupIndex)] = playing;
    for (auto& cell : cells)
        if (playing && std::find(cell->groups.begin(), cell->groups.end(), groupIndex) != cell->groups.end())
        {
            cell->editGroup = groupIndex;
        }
    refreshActivity();
}

void SampleGroupSelector::refreshActivity()
{
    for (auto& cell : cells)
    {
        const bool playing = std::any_of(cell->groups.begin(), cell->groups.end(),
            [this] (int group) { return groupPlaying[static_cast<size_t>(group)]; });
        if (cell->playing != playing)
        {
            cell->playing = playing;
            cell->repaint();
        }
    }
    for (int page = 0; page < static_cast<int>(pages.size()); ++page)
    {
        bool playing = false;
        for (int i = page * pageSize; i < juce::jmin((page + 1) * pageSize, static_cast<int>(cells.size())); ++i)
            playing = playing || cells[static_cast<size_t>(i)]->playing;
        auto& button = *pages[static_cast<size_t>(page)];
        if (button.playing != playing)
        {
            button.playing = playing;
            button.repaint();
        }
    }
}

void SampleGroupSelector::setShakeGroup(int groupIndex)
{
    for (auto& cell : cells)
        cell->lp.setToggleState(groupIndex >= 0
            && std::find(cell->groups.begin(), cell->groups.end(), groupIndex) != cell->groups.end(),
            juce::dontSendNotification);
}

void SampleGroupSelector::resized()
{
    const int gridHeight = juce::jmax(0, getHeight() - 28);
    for (int i = 0; i < static_cast<int>(cells.size()); ++i)
    {
        const int slot = i % pageSize, col = slot % 3, row = slot / 3;
        const int left = col * (getWidth() + cellGap) / 3;
        const int top = row * (gridHeight + cellGap) / 3;
        const int right = (col + 1) * (getWidth() + cellGap) / 3 - cellGap;
        const int bottom = (row + 1) * (gridHeight + cellGap) / 3 - cellGap;
        cells[static_cast<size_t>(i)]->setBounds(left, top, juce::jmax(0, right - left), juce::jmax(0, bottom - top));
    }
    constexpr int lineWidth = 32, pageGap = 15;
    const int total = static_cast<int>(pages.size()) * (lineWidth + pageGap) - pageGap;
    for (int i = 0; i < static_cast<int>(pages.size()); ++i)
        pages[static_cast<size_t>(i)]->setBounds((getWidth() - total) / 2 + i * (lineWidth + pageGap),
                                                getHeight() - 20, lineWidth, 20);
}
