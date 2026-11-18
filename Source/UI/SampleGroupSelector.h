#pragma once

#include <functional>
#include <memory>
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>
#include "SampleLibrary/PercussionSampleLibrary.h"

// Message-thread sample grid. Pitch slots and direction pairs share a numbered cell.
class SampleGroupSelector final : public juce::Component
{
public:
    SampleGroupSelector();
    ~SampleGroupSelector() override;
    void setSampleGroups(const std::vector<PercussionSampleLibrary::SampleGroupInfo>& groups);
    void setGroupPlaying(int groupIndex, bool playing);
    void setShakeGroup(int groupIndex);
    std::function<void(int)> onEdit;
    std::function<void(int)> onShake;
    void resized() override;

private:
    class Cell;
    class PageButton;
    void selectPage(int page);
    void refreshActivity();
    std::vector<std::unique_ptr<Cell>> cells;
    std::vector<std::unique_ptr<PageButton>> pages;
    std::vector<bool> groupPlaying;
    int selectedPage = 0;
};
