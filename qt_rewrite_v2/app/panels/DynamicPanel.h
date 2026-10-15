#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include <QCoreApplication>
#include <QWidget>

#include "document/ProjectDocument.h"

class QLabel;
class QListWidget;
class QSpinBox;
class QToolButton;

namespace colorizing::app {

// Inspector page for the dynamic sets of a frame: the sets the frame uses (each with a picture of
// its zones), the set to paint, its colors and its shadow.
// The selected set is the page's own state. The page only displays and reports what the user
// asked for; the owner applies the edits and calls the refresh functions.
class DynamicPanel final : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(DynamicPanel)

public:
    explicit DynamicPanel(QWidget* parent = nullptr);

    // The list of the frame's sets; rebuilt only when the project, the frame or the plane
    // changed, otherwise only its selection follows the selected set.
    void refreshSets(const document::ProjectDocument* document,
                     std::optional<std::size_t> frameIndex,
                     render::Resolution resolution);
    // Colors of the selected set.
    void refreshColors(const document::ProjectDocument* document,
                       std::optional<std::size_t> frameIndex,
                       render::Resolution resolution);
    // Shadow directions and shadow color of the selected set.
    void refreshShadow(const document::ProjectDocument* document,
                       std::optional<std::size_t> frameIndex,
                       render::Resolution resolution);
    void setDynamicSetEnabled(bool enabled);
    // A line shown at the top of the page; hidden while empty.
    void setNote(const QString& text);
    // Clamped to the offered sets; does not notify.
    void setDynamicSet(int setIndex);

    [[nodiscard]] uint8_t dynamicSet() const;

    std::function<void(int setIndex)> onDynamicSetChanged;
    std::function<void(std::size_t originalColorIndex)> onColorClicked;
    // bit is the direction's bit in the shadow direction mask.
    std::function<void(uint8_t bit, bool enabled)> onShadowDirectionToggled;
    std::function<void()> onShadowColorClicked;

private:
    void selectListedSet();

    QLabel* m_noteLabel = nullptr;
    QListWidget* m_setList = nullptr;
    // What the list was built for.
    const document::ProjectDocument* m_listDocument = nullptr;
    uint64_t m_listRevision = 0;
    std::optional<std::size_t> m_listFrame;
    render::Resolution m_listResolution = render::Resolution::Sd;
    QSpinBox* m_dynamicSetSpinBox = nullptr;
    std::vector<QToolButton*> m_colorButtons;
    std::vector<QToolButton*> m_shadowDirectionButtons;
    QToolButton* m_shadowColorButton = nullptr;
};

}  // namespace colorizing::app
