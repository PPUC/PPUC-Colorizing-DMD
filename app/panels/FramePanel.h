#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>

#include <QCoreApplication>
#include <QWidget>

#include "document/ProjectDocument.h"
#include "document/Triggers.h"
#include "FrameImageUtils.h"

class QCheckBox;
class QComboBox;
class QCompleter;
class QLabel;
class QLineEdit;
class QSpinBox;
class QToolButton;

namespace colorizing::app {

constexpr int kMaxFrameDurationMs = 30000;

// What the frame page displays. The background layer toggle and the DMD pixel settings are the
// page's own state and are kept across refreshes.
struct FramePanelState {
    const document::ProjectDocument* document = nullptr;
    std::optional<std::size_t> frameIndex;
    bool hasSelectedFrames = false;
};

// Inspector page for the frame metadata and the DMD rendering settings. (Whether the frame or a
// background is edited is the Frame Background section's Edit BG.)
// It only displays and reports what the user asked for; the owner applies the edits and calls
// refresh().
class FramePanel final : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(FramePanel)

public:
    explicit FramePanel(QWidget* parent = nullptr);

    // Also labels the Create/Remove HD buttons for the frame again.
    //
    // The two buttons follow the project's layout, see PlaneLabels.h: "Create HD" and
    // "Remove HD" in an ordinary project; "Create 256x64 Frame" alone, enabled while the frame
    // or its background lacks what the runtime needs, with PlaneLayout::NativeExtra; neither
    // with PlaneLayout::NativeOriginal. A button that does not apply is hidden.
    void refresh(const FramePanelState& state);
    void setImportImageEnabled(bool enabled);

    // The frame's trigger ID as the panel shows it, document::kNoTriggerId for none.
    [[nodiscard]] uint32_t triggerId() const;
    [[nodiscard]] bool isBackgroundLayerChecked() const;
    [[nodiscard]] bool isSpritesLayerChecked() const;

    // -1 clears the assignment.
    std::function<void(int backgroundId)> onBackgroundIdEdited;
    // A frame has one trigger, set through the input of its kind: the PUP trigger, the scene,
    // or one of the two monochrome fallbacks. While the frame has one, the inputs of the other
    // kinds are disabled. triggerId is the new ID, or document::kNoTriggerId when the input of
    // that kind was cleared.
    std::function<void(document::TriggerKind kind, uint32_t triggerId)> onTriggerEdited;
    std::function<void(int durationMs)> onDurationEdited;
    // The Tags field changed (comma-separated): see edit::retaggedFrame().
    std::function<void(const QString& text)> onTagsEdited;
    std::function<void(bool checked)> onBackgroundLayerToggled;
    std::function<void(bool checked)> onSpritesLayerToggled;
    std::function<void()> onImportImageRequested;
    std::function<void()> onCreateHdRequested;
    std::function<void()> onRemoveHdRequested;

private:
    void showFrameHdActions(bool canCreate, bool canRemove);
    // Hides what the layout has no use for.
    void showHdActionsFor(render::PlaneLayout layout);

    void fillScenes(const document::ProjectDocument* document, uint32_t triggerId);

    QSpinBox* m_backgroundIdSpinBox = nullptr;
    QSpinBox* m_pupTriggerSpinBox = nullptr;
    QComboBox* m_sceneComboBox = nullptr;
    QCheckBox* m_monochromeCheckBox = nullptr;
    QCheckBox* m_paletteMonochromeCheckBox = nullptr;
    QLabel* m_otherTriggerLabel = nullptr;
    QSpinBox* m_durationSpinBox = nullptr;
    QLineEdit* m_tagsEdit = nullptr;
    QCompleter* m_tagsCompleter = nullptr;
    // The tags of the frame shown, as the field shows them.
    QString m_shownTags;
    uint32_t m_triggerId = document::kNoTriggerId;
    QToolButton* m_backgroundLayerButton = nullptr;
    QToolButton* m_spritesLayerButton = nullptr;
    QToolButton* m_importImageButton = nullptr;
    QToolButton* m_createHdButton = nullptr;
    QToolButton* m_removeHdButton = nullptr;
};

}  // namespace colorizing::app
