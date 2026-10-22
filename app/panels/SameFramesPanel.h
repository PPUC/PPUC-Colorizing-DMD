#pragma once

#include <cstddef>
#include <functional>
#include <vector>

#include <QCoreApplication>
#include <QWidget>

#include "edit/SameFrames.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QToolButton;

namespace colorizing::app {

struct SameFramesPanelState {
    bool hasFrame = false;
    // The frames the runtime cannot tell from the current one under its comparison mask and
    // shape mode. The panel lists at most edit::kMaxListedSameFrames of them.
    std::vector<std::size_t> sameFrames;
    std::size_t selectedFrameCount = 0;
    std::size_t frameCount = 0;
    // The current frame has a comparison mask, which Common To Mask writes.
    bool hasAssignedMask = false;
};

// Which pixels Spot Common marks: those in which the selected frames differ, or those they
// have in common, as the Win32 editor marks them.
enum class SpotMarks {
    Differing,
    Common,
};

// The comparison side of the Mask page: which frames are the same as the current one, how
// many would be under each mask, deleting same frames, the pixels that differ among the
// selected frames, and the frames that use a mask. It only displays and reports what the
// user asked for; the owner works everything out and calls the refresh functions.
class SameFramesPanel final : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(SameFramesPanel)

public:
    explicit SameFramesPanel(QWidget* parent = nullptr);

    void refresh(const SameFramesPanelState& state);
    // The counts of "Update Same List"; nullptr while there are none for the current frame.
    void setMaskCounts(const std::vector<edit::SameFrameCounts>* counts);
    // The masks offered under "Frames using mask": how many frames have no mask and how
    // many use each mask. Keeps the choice if that mask is still there.
    void setMaskChoices(std::size_t framesWithoutMask, const std::vector<std::size_t>& useCounts);
    void setFramesUsingMask(const std::vector<std::size_t>& frames);
    // Does not notify.
    void setSpotCommonChecked(bool checked);
    // Does not notify.
    void setSpotMarks(SpotMarks marks);

    // -1 for "no mask".
    [[nodiscard]] int usingMaskId() const;
    [[nodiscard]] bool keepsSelectedFrames() const;
    [[nodiscard]] bool isSpotCommonChecked() const;
    [[nodiscard]] SpotMarks spotMarks() const;

    std::function<void(std::size_t frameIndex)> onFrameRequested;
    std::function<void()> onUpdateCountsRequested;
    std::function<void()> onDeleteSameAsCurrentRequested;
    std::function<void()> onDeleteSameAsSelectedRequested;
    std::function<void()> onDeleteAllDuplicatesRequested;
    std::function<void(bool checked)> onSpotCommonToggled;
    std::function<void(SpotMarks marks)> onSpotMarksChanged;
    std::function<void()> onCommonToMaskRequested;
    std::function<void()> onUsingMaskChanged;
    std::function<void()> onSelectUsingMaskRequested;

private:
    QLabel* m_sameCountLabel = nullptr;
    QComboBox* m_sameFramesComboBox = nullptr;
    QToolButton* m_updateCountsButton = nullptr;
    QListWidget* m_maskCountsList = nullptr;
    QToolButton* m_deleteSameAsCurrentButton = nullptr;
    QToolButton* m_deleteSameAsSelectedButton = nullptr;
    QToolButton* m_deleteAllDuplicatesButton = nullptr;
    QCheckBox* m_keepSelectedCheckBox = nullptr;
    QToolButton* m_spotCommonButton = nullptr;
    QComboBox* m_spotMarksComboBox = nullptr;
    QToolButton* m_commonToMaskButton = nullptr;
    QComboBox* m_usingMaskComboBox = nullptr;
    QListWidget* m_usingMaskList = nullptr;
    QToolButton* m_selectUsingMaskButton = nullptr;
};

}  // namespace colorizing::app
