#pragma once

#include <functional>
#include <memory>
#include <optional>

#include <QCoreApplication>
#include <QImage>
#include <QString>

#include "CanvasEditController.h"
#include "FramePreviewWidget.h"
#include "edit/CopyPaste.h"

namespace colorizing::app {

struct PasteModeOutputs {
    // Applies and records the command; once per frame pasted on.
    std::function<void(std::unique_ptr<edit::EditCommand> command)> commit;
    std::function<void()> commitsFinished;
    std::function<void(const QString& text, int timeoutMs)> showStatus;
    // nullopt removes the floating picture.
    std::function<void(std::optional<CanvasPasteOverlay> overlay)> showOverlay;
    // Paste mode started or ended: the canvas follows the mouse only meanwhile.
    std::function<void(bool active)> activeChanged;
    // The copied colors as an image for the system clipboard; unset in tests.
    std::function<void(const QImage& image)> exportImage;
};

// Copy and paste of frame content (the Win32 IDC_COPY / IDC_PASTE): the internal clipboard and
// the paste mode, in which the copied pixels float over the frame canvas, follow the mouse or
// the arrow keys, can be mirrored, and are stamped by a click or Enter onto the current frame
// or every selected frame, one undoable step per frame.
//
// Keys in paste mode: arrows (and the keypad's 2, 4, 6, 8 as in Win32) move by a pixel, with
// Shift by 10; K mirrors horizontally, L vertically; Enter/Return stamps; Escape cancels.
// A stamp with Shift pastes only where the originals match, with Alt the dynamic mask alone;
// otherwise the mode the paste was started with applies.
class PasteModeController {
    Q_DECLARE_TR_FUNCTIONS(PasteModeController)

public:
    explicit PasteModeController(PasteModeOutputs outputs = {});

    // Copies the selection of the current frame. Returns whether the clipboard was filled.
    bool copy(const CanvasEditContext& context, const edit::PlaneSelection* selection);
    [[nodiscard]] const edit::CopyBuffer* clipboard() const;
    // What a copy from elsewhere (the sprite canvas) put into the clipboard.
    void setClipboard(edit::CopyBuffer buffer);
    void clearClipboard();

    // Starts paste mode with the box where it was copied from. Refused (status line) without
    // clipboard, on another plane or layer, or on the background target.
    bool begin(const CanvasEditContext& context, edit::PasteMode mode);
    [[nodiscard]] bool isActive() const;
    [[nodiscard]] edit::PasteMode mode() const;
    [[nodiscard]] const edit::PastePlacement& placement() const;
    // Ends paste mode without pasting.
    void cancel();

    // The box centred on the canvas pixel (Win32: the cursor at the middle of the box).
    void follow(const render::CanvasHit& hit);
    void nudge(int dx, int dy);
    void toggleMirror(bool horizontally, bool vertically);
    // Pastes on context.selectedFrames and ends paste mode; true if a frame changed.
    bool stamp(const CanvasEditContext& context, Qt::KeyboardModifiers modifiers);
    // A key while the canvas has the focus in paste mode; true if it was taken.
    bool handleKey(const CanvasEditContext& context, int key, Qt::KeyboardModifiers modifiers);

    // The mode a stamp with these modifiers uses.
    [[nodiscard]] edit::PasteMode stampMode(Qt::KeyboardModifiers modifiers) const;

private:
    void updateOverlay() const;
    void setActive(bool active);
    void showStatus(const QString& text, int timeoutMs) const;
    // Why paste cannot start or stamp in the context, or an empty string.
    [[nodiscard]] QString refusal(const CanvasEditContext& context, edit::PasteMode mode) const;

    PasteModeOutputs m_outputs;
    std::optional<edit::CopyBuffer> m_clipboard;
    bool m_active = false;
    edit::PasteMode m_mode = edit::PasteMode::All;
    edit::PastePlacement m_placement;
};

// The colors of the copied pixels as an image of the box, transparent elsewhere.
[[nodiscard]] QImage copyBufferImage(const edit::CopyBuffer& buffer);

}  // namespace colorizing::app
