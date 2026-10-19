#pragma once

#include <QCoreApplication>
#include <QDialog>
#include <QWidget>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

class QComboBox;
class QListWidget;
class QPushButton;
class QToolButton;

namespace colorizing::app {

struct SectionBarEntry {
    QString name;
    std::size_t firstFrame = 0;
};

struct TriggerBarEntry {
    std::size_t frameIndex = 0;
    uint32_t triggerId = 0;
};

// Above the frame list: the sections (jump to one, name, delete, order) and the trigger list
// (jump to a frame with a trigger), as the Win32 editor's section and trigger boxes.
class SectionBar final : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(SectionBar)

public:
    explicit SectionBar(QWidget* parent = nullptr);

    // The lists; then setCurrent() shows where the current frame is in them.
    void setContent(bool hasProject,
                    const std::vector<SectionBarEntry>& sections,
                    const std::vector<TriggerBarEntry>& triggers);
    // currentSection: the section of the current frame; startsHere: it starts at that frame.
    void setCurrent(std::optional<std::size_t> currentSection,
                    bool startsHere,
                    std::optional<std::size_t> currentFrame);

    std::function<void(std::size_t sectionIndex)> onSectionChosen;
    std::function<void()> onNameRequested;
    std::function<void()> onDeleteRequested;
    std::function<void()> onOrderRequested;
    std::function<void(std::size_t frameIndex)> onTriggerChosen;

private:
    QComboBox* m_sectionComboBox = nullptr;
    QToolButton* m_nameButton = nullptr;
    QToolButton* m_deleteButton = nullptr;
    QToolButton* m_orderButton = nullptr;
    QComboBox* m_triggerComboBox = nullptr;
    bool m_hasProject = false;
    std::vector<std::size_t> m_triggerFrames;
};

// The section organizer: the list in its order, Up, Down and A-Z, each applied at once.
class SectionOrderDialog final : public QDialog {
    Q_DECLARE_TR_FUNCTIONS(SectionOrderDialog)

public:
    explicit SectionOrderDialog(QWidget* parent = nullptr);

    void setNames(const QStringList& names, int selectedRow);

    // The owner changes the sections and gives the dialog the new names (setNames).
    std::function<void(int row, int offset)> onMoveRequested;
    std::function<void()> onSortRequested;

private:
    void updateButtons();

    QListWidget* m_list = nullptr;
    QPushButton* m_upButton = nullptr;
    QPushButton* m_downButton = nullptr;
    QPushButton* m_sortButton = nullptr;
};

}  // namespace colorizing::app
