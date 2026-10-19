#include "PasteModeController.h"

#include <exception>
#include <utility>

#include "FrameImageUtils.h"
#include "edit/FrameContentCommand.h"

namespace colorizing::app {

namespace {

constexpr int kStatusMs = 2500;
constexpr int kLongStatusMs = 4000;

edit::EditKind historyKind(const CanvasEditContext& context)
{
    return context.layer == CanvasEditLayer::DynamicMask ? edit::EditKind::DynamicMask
                                                         : edit::EditKind::FramePixels;
}

QString modeText(edit::PasteMode mode)
{
    switch (mode) {
        case edit::PasteMode::All:
            return QCoreApplication::translate("PasteModeController", "Paste");
        case edit::PasteMode::WhereOriginalsMatch:
            return QCoreApplication::translate("PasteModeController",
                                               "Paste where the originals match");
        case edit::PasteMode::DynamicMaskOnly:
            return QCoreApplication::translate("PasteModeController", "Paste the dynamic mask");
    }
    return QString();
}

}  // namespace

PasteModeController::PasteModeController(PasteModeOutputs outputs)
    : m_outputs(std::move(outputs))
{
}

bool PasteModeController::copy(const CanvasEditContext& context,
                               const edit::PlaneSelection* selection)
{
    if (!context.document || !context.currentFrame) {
        return false;
    }
    if (context.layer != CanvasEditLayer::FramePixels &&
        context.layer != CanvasEditLayer::DynamicMask) {
        showStatus(tr("Copy works on the frame's pixels"), kStatusMs);
        return false;
    }
    if (!selection) {
        showStatus(tr("Nothing is selected to copy"), kStatusMs);
        return false;
    }
    if (selection->plane() != context.activePlane) {
        // Win32 Copy_Content: the selection was made on the other plane.
        showStatus(tr("The selection is on the other plane"), kStatusMs);
        return false;
    }
    try {
        m_clipboard = edit::copySelection(*context.document, *context.currentFrame, *selection);
    } catch (const std::exception& ex) {
        showStatus(QString::fromUtf8(ex.what()), kLongStatusMs);
        return false;
    }
    if (m_outputs.exportImage) {
        m_outputs.exportImage(copyBufferImage(*m_clipboard));
    }
    showStatus((m_clipboard->hasDynamicData()
                    ? tr("Copied %1 pixels of frame %2 with the dynamic mask")
                    : tr("Copied %1 pixels of frame %2"))
                   .arg(static_cast<qulonglong>(m_clipboard->selectedCount()))
                   .arg(static_cast<qulonglong>(*context.currentFrame)),
               kStatusMs);
    return true;
}

const edit::CopyBuffer* PasteModeController::clipboard() const
{
    return m_clipboard ? &*m_clipboard : nullptr;
}

void PasteModeController::setClipboard(edit::CopyBuffer buffer)
{
    m_clipboard = std::move(buffer);
}

void PasteModeController::clearClipboard()
{
    cancel();
    m_clipboard.reset();
}

QString PasteModeController::refusal(const CanvasEditContext& context,
                                     edit::PasteMode mode) const
{
    if (!context.document || !context.currentFrame) {
        return tr("No frame to paste on");
    }
    if (!m_clipboard) {
        return tr("Nothing has been copied");
    }
    if (context.layer != CanvasEditLayer::FramePixels &&
        context.layer != CanvasEditLayer::DynamicMask) {
        return tr("Paste works on the frame's pixels");
    }
    if (const std::optional<std::string> reason =
            edit::pasteRefusal(*context.document, *m_clipboard, context.activePlane, mode)) {
        QString text = QString::fromStdString(*reason);
        if (!text.isEmpty()) {
            text[0] = text[0].toUpper();
        }
        return text;
    }
    return QString();
}

bool PasteModeController::begin(const CanvasEditContext& context, edit::PasteMode mode)
{
    const QString reason = refusal(context, mode);
    if (!reason.isEmpty()) {
        showStatus(reason, kLongStatusMs);
        return false;
    }
    m_mode = mode;
    m_placement = edit::PastePlacement{m_clipboard->box.x, m_clipboard->box.y, false, false};
    setActive(true);
    updateOverlay();
    showStatus(tr("%1: click or Enter stamps, arrows move, K/L mirror, Esc cancels")
                   .arg(modeText(mode)),
               kLongStatusMs);
    return true;
}

bool PasteModeController::isActive() const
{
    return m_active;
}

edit::PasteMode PasteModeController::mode() const
{
    return m_mode;
}

const edit::PastePlacement& PasteModeController::placement() const
{
    return m_placement;
}

void PasteModeController::cancel()
{
    if (!m_active) {
        return;
    }
    setActive(false);
    if (m_outputs.showOverlay) {
        m_outputs.showOverlay(std::nullopt);
    }
}

void PasteModeController::follow(const render::CanvasHit& hit)
{
    if (!m_active || !m_clipboard || hit.resolution != m_clipboard->plane) {
        return;
    }
    const int x = hit.x - m_clipboard->box.width / 2;
    const int y = hit.y - m_clipboard->box.height / 2;
    if (x == m_placement.x && y == m_placement.y) {
        return;
    }
    m_placement.x = x;
    m_placement.y = y;
    updateOverlay();
}

void PasteModeController::nudge(int dx, int dy)
{
    if (!m_active) {
        return;
    }
    m_placement.x += dx;
    m_placement.y += dy;
    updateOverlay();
}

void PasteModeController::toggleMirror(bool horizontally, bool vertically)
{
    if (!m_active) {
        return;
    }
    if (horizontally) {
        m_placement.mirrorHorizontally = !m_placement.mirrorHorizontally;
    }
    if (vertically) {
        m_placement.mirrorVertically = !m_placement.mirrorVertically;
    }
    updateOverlay();
}

edit::PasteMode PasteModeController::stampMode(Qt::KeyboardModifiers modifiers) const
{
    if (modifiers.testFlag(Qt::AltModifier)) {
        return edit::PasteMode::DynamicMaskOnly;
    }
    if (modifiers.testFlag(Qt::ShiftModifier)) {
        return edit::PasteMode::WhereOriginalsMatch;
    }
    return m_mode;
}

bool PasteModeController::stamp(const CanvasEditContext& context,
                                Qt::KeyboardModifiers modifiers)
{
    if (!m_active) {
        return false;
    }
    const edit::PasteMode mode = stampMode(modifiers);
    const QString reason = refusal(context, mode);
    if (!reason.isEmpty()) {
        showStatus(reason, kLongStatusMs);
        return false;
    }
    const document::ProjectDocument& document = *context.document;
    std::size_t changedFrames = 0;
    try {
        for (std::size_t frameIndex : context.selectedFrames) {
            edit::FrameContentChanges changes = edit::makePasteChanges(
                document, *m_clipboard, frameIndex, context.activePlane, m_placement, mode);
            if (changes.isEmpty()) {
                continue;
            }
            if (m_outputs.commit) {
                m_outputs.commit(std::make_unique<edit::FrameContentCommand>(
                    frameIndex, document.dynamicPlane(), std::move(changes), historyKind(context)));
            }
            ++changedFrames;
        }
    } catch (const std::exception& ex) {
        if (changedFrames > 0 && m_outputs.commitsFinished) {
            m_outputs.commitsFinished();
        }
        showStatus(QString::fromUtf8(ex.what()), kLongStatusMs);
        return changedFrames > 0;
    }
    // Win32 leaves paste mode with the stamp.
    cancel();
    if (changedFrames > 0 && m_outputs.commitsFinished) {
        m_outputs.commitsFinished();
    }
    showStatus(changedFrames > 0
                   ? tr("%1 on %2 frame(s)")
                         .arg(modeText(mode))
                         .arg(static_cast<qulonglong>(changedFrames))
                   : tr("%1: nothing changed").arg(modeText(mode)),
               kStatusMs);
    return changedFrames > 0;
}

bool PasteModeController::handleKey(const CanvasEditContext& context,
                                    int key,
                                    Qt::KeyboardModifiers modifiers)
{
    if (!m_active) {
        return false;
    }
    const bool keypad = modifiers.testFlag(Qt::KeypadModifier);
    const int step = modifiers.testFlag(Qt::ShiftModifier) ? 10 : 1;
    switch (key) {
        case Qt::Key_Left:
            nudge(-step, 0);
            return true;
        case Qt::Key_Right:
            nudge(step, 0);
            return true;
        case Qt::Key_Up:
            nudge(0, -step);
            return true;
        case Qt::Key_Down:
            nudge(0, step);
            return true;
        case Qt::Key_K:
            toggleMirror(true, false);
            return true;
        case Qt::Key_L:
            toggleMirror(false, true);
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            stamp(context, modifiers & ~Qt::KeypadModifier);
            return true;
        case Qt::Key_Escape:
            cancel();
            showStatus(tr("Paste cancelled"), kStatusMs);
            return true;
        default:
            break;
    }
    if (keypad) {
        switch (key) {
            case Qt::Key_4:
                nudge(-1, 0);
                return true;
            case Qt::Key_6:
                nudge(1, 0);
                return true;
            case Qt::Key_8:
                nudge(0, -1);
                return true;
            case Qt::Key_2:
                nudge(0, 1);
                return true;
            default:
                break;
        }
    }
    return false;
}

void PasteModeController::updateOverlay() const
{
    if (!m_active || !m_clipboard || !m_outputs.showOverlay) {
        return;
    }
    CanvasPasteOverlay overlay;
    overlay.resolution = m_clipboard->plane;
    overlay.box = render::Rect{m_placement.x,
                               m_placement.y,
                               m_clipboard->box.width,
                               m_clipboard->box.height};
    for (int y = 0; y < m_clipboard->box.height; ++y) {
        for (int x = 0; x < m_clipboard->box.width; ++x) {
            const std::size_t source = edit::pasteSourceIndex(*m_clipboard, m_placement, x, y);
            if (!m_clipboard->mask[source]) {
                continue;
            }
            overlay.pixels.emplace_back(render::Point{m_placement.x + x, m_placement.y + y},
                                        rgb565ToColor(m_clipboard->colors[source]));
        }
    }
    m_outputs.showOverlay(std::move(overlay));
}

void PasteModeController::setActive(bool active)
{
    if (m_active == active) {
        return;
    }
    m_active = active;
    if (m_outputs.activeChanged) {
        m_outputs.activeChanged(active);
    }
}

void PasteModeController::showStatus(const QString& text, int timeoutMs) const
{
    if (m_outputs.showStatus) {
        m_outputs.showStatus(text, timeoutMs);
    }
}

QImage copyBufferImage(const edit::CopyBuffer& buffer)
{
    QImage image(buffer.box.width, buffer.box.height, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    for (int y = 0; y < buffer.box.height; ++y) {
        for (int x = 0; x < buffer.box.width; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) *
                                          static_cast<std::size_t>(buffer.box.width) +
                                      static_cast<std::size_t>(x);
            if (buffer.mask[index]) {
                image.setPixelColor(x, y, rgb565ToColor(buffer.colors[index]));
            }
        }
    }
    return image;
}

}  // namespace colorizing::app
