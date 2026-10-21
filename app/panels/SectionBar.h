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


// Above the frame list: the sections (jump to one, name, delete, order), as the Win32 editor's
// section box. Its trigger list is gone: one entry per frame with a trigger was no help with
// hundreds of them; the frame filter's With triggers lists those frames.
class SectionBar final : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(SectionBar)

public:
    explicit SectionBar(QWidget* parent = nullptr);

    // The list; then setCurrent() shows the current frame's section in it.
    void setContent(bool hasProject, const std::vector<SectionBarEntry>& sections);
    // currentSection: the section of the current frame; startsHere: it starts at that frame.
    void setCurrent(std::optional<std::size_t> currentSection,
                    bool startsHere,
                    std::optional<std::size_t> currentFrame);

    std::function<void(std::size_t sectionIndex)> onSectionChosen;
    std::function<void()> onNameRequested;
    std::function<void()> onDeleteRequested;
    std::function<void()> onOrderRequested;

private:
    QComboBox* m_sectionComboBox = nullptr;
    QToolButton* m_nameButton = nullptr;
    QToolButton* m_deleteButton = nullptr;
    QToolButton* m_orderButton = nullptr;
    bool m_hasProject = false;
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
