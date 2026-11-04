#pragma once

#include <QCoreApplication>
#include <QDialog>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "render/CanvasTypes.h"
#include "vpx/EditorApi.h"

class QCheckBox;
class QLabel;
class QListWidget;
class QPushButton;

namespace colorizing::document {
class ProjectDocument;
}

namespace colorizing::app {

class FramePictureView;

// The pictures a plugin proposes (the editor API's ReviewPictures), each beside the picture it
// replaces, for the author to pick which to apply. The dialog only shows and picks; Apply runs
// the owner's action (setApplyAction), which applies the chosen ones and closes it.
class PictureReviewDialog final : public QDialog {
    Q_DECLARE_TR_FUNCTIONS(PictureReviewDialog)

public:
    // The pictures it replaces are taken from the document now.
    PictureReviewDialog(const document::ProjectDocument& document,
                        const QString& title,
                        std::vector<vpx::ProposedPicture> pictures,
                        QWidget* parent = nullptr);

    [[nodiscard]] const std::vector<vpx::ProposedPicture>& pictures() const;
    // The pictures checked in the list, ascending.
    [[nodiscard]] std::vector<std::size_t> chosen() const;
    void setChecked(std::size_t picture, bool checked);
    // The picture shown.
    [[nodiscard]] std::size_t current() const;
    void setCurrent(std::size_t picture);
    // Pixels of the shown picture that change.
    [[nodiscard]] std::size_t changedPixels() const;
    // Nothing can be applied any more: the project changed (the reason is shown).
    void setOutdated(const QString& reason);
    [[nodiscard]] bool isOutdated() const { return m_isOutdated; }
    [[nodiscard]] bool canApply() const;

    // "Frame 12 (HD)", as the list names a picture without the plugin's label.
    [[nodiscard]] static QString defaultLabel(const document::ProjectDocument& document,
                                              const edit::PictureEdit& picture);

    void setApplyAction(std::function<void()> action);

private:
    struct Entry {
        render::Size size;
        // The picture it replaces, and the proposed one as it would be (written pixels only).
        std::vector<uint16_t> before;
        std::vector<uint16_t> after;
        std::size_t changed = 0;
    };

    void showPicture();
    void updateApply();
    [[nodiscard]] QImage render(const Entry& entry, bool proposed, int pixelScale) const;

    std::vector<vpx::ProposedPicture> m_pictures;
    std::vector<Entry> m_entries;
    QListWidget* m_list = nullptr;
    FramePictureView* m_before = nullptr;
    FramePictureView* m_after = nullptr;
    QCheckBox* m_markSkipped = nullptr;
    QLabel* m_details = nullptr;
    QLabel* m_outdated = nullptr;
    QPushButton* m_apply = nullptr;
    bool m_isOutdated = false;
    std::function<void()> m_applyAction;
};

}  // namespace colorizing::app
