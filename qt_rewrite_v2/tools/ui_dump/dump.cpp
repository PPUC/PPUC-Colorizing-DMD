// UI regression dump: builds the real MainWindow offscreen, drives it through scripted states
// and prints every widget plus document and canvas state after each one. See README.md.
#include <algorithm>
#include <array>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <QtWidgets>

#include "document/ProjectDocument.h"
#include "document/ProjectSnapshot.h"
#include "edit/DrawingTools.h"
#include "edit/UndoHistories.h"
#include "FrameImageUtils.h"
#include "legacy/LegacyProjectSerializer.h"
#include "render/CanvasLayout.h"
// Not in every tree the scripts are built against, see "When MainWindow changes" in README.md.
#if __has_include("LazyListIcons.h")
#include "LazyListIcons.h"
#define UI_DUMP_HAS_LAZY_THUMBNAILS 1
#endif

#if __has_include("edit/StructureCommand.h")
#define UI_DUMP_HAS_STRUCTURE_COMMANDS 1
#endif

#if __has_include("dialogs/DumpImportDialog.h")
#include "dialogs/DumpImportDialog.h"
#include "import/FrameDumpImport.h"
#define UI_DUMP_HAS_DUMP_IMPORT 1
#endif

#if __has_include("UnsavedChanges.h")
#include "UnsavedChanges.h"
#include "edit/EditCommand.h"
#define UI_DUMP_HAS_FIXES_2026_10_14 1
#endif

#if __has_include("panels/ColorSetPanel.h")
#include "edit/ColorSetEdits.h"
#include "panels/ColorSetPanel.h"
#include "panels/EditColorBar.h"
#define UI_DUMP_HAS_COLOR_SETS 1
#endif

#if __has_include("PasteModeController.h")
#include "EditMenuActions.h"
#include "edit/CopyPaste.h"
#include "edit/PlaneSelection.h"
#define UI_DUMP_HAS_SELECTION_TOOLS 1
#endif

#if __has_include("dialogs/ProjectTaskDialog.h")
#include <atomic>
#include <chrono>
#include <thread>
#include "dialogs/ProjectTaskDialog.h"
#define UI_DUMP_HAS_PROJECT_TASKS 1
#endif

#if __has_include("Autosave.h")
#include <QElapsedTimer>
#define UI_DUMP_HAS_AUTOSAVE 1
#endif

#if __has_include("panels/SectionBar.h")
#include "edit/Sections.h"
#define UI_DUMP_HAS_SECTIONS 1
#endif

#if __has_include("RuntimeView.h")
#include "edit/EditCommand.h"
#include "runtime/RuntimePreview.h"
#define UI_DUMP_HAS_RUNTIME_VIEW 1
#endif

// The scripts drive MainWindow and the canvas widgets through their private members. Redefining
// the keyword is not valid C++; it works with GCC and Clang, where access does not affect
// layout or symbol names. See README.md.
#define private public
#include "FramePreviewWidget.h"
#include "SpriteEditorWidget.h"
#include "MainWindow.h"
#ifdef UI_DUMP_HAS_SECTIONS
#include "panels/SectionBar.h"
#endif
#undef private

using namespace colorizing;

static QString describe(QWidget* w, QWidget* window)
{
    QStringList parts;
    parts << QString::fromLatin1(w->metaObject()->className());
    const QPoint pos = w->mapTo(window, QPoint(0, 0));
    parts << QStringLiteral("@%1,%2 %3x%4").arg(pos.x()).arg(pos.y()).arg(w->width()).arg(w->height());
    parts << QStringLiteral("vis=%1 en=%2").arg(w->isVisibleTo(window)).arg(w->isEnabled());
    parts << QStringLiteral("min=%1x%2 max=%3x%4 sp=%5/%6")
                 .arg(w->minimumWidth()).arg(w->minimumHeight())
                 .arg(w->maximumWidth()).arg(w->maximumHeight())
                 .arg(w->sizePolicy().horizontalPolicy()).arg(w->sizePolicy().verticalPolicy());
    parts << QStringLiteral("focus=%1").arg(w->focusPolicy());
    if (!w->toolTip().isEmpty()) parts << QStringLiteral("tip='%1'").arg(w->toolTip());
    if (!w->styleSheet().isEmpty()) parts << QStringLiteral("css='%1'").arg(w->styleSheet());
    if (auto* l = qobject_cast<QLabel*>(w)) {
        parts << QStringLiteral("label='%1' wrap=%2 pm=%3").arg(l->text()).arg(l->wordWrap()).arg(!l->pixmap().isNull());
    }
    if (auto* l = qobject_cast<QLabel*>(w)) {
        if (!l->pixmap().isNull()) {
            const QImage image = l->pixmap().toImage().convertToFormat(QImage::Format_ARGB32);
            parts << QStringLiteral("pmhash=%1x%2:%3").arg(image.width()).arg(image.height())
                         .arg(qHashBits(image.constBits(), static_cast<size_t>(image.sizeInBytes())));
        }
    }
    if (auto* lw = qobject_cast<QListWidget*>(w)) {
        QStringList items;
        for (int i = 0; i < lw->count(); ++i) {
            items << QStringLiteral("%1%2%3").arg(lw->item(i)->text().replace('\n', '/'))
                         .arg(lw->item(i)->isSelected() ? "#" : "")
                         .arg(lw->item(i)->icon().isNull() ? "" : "*");
        }
        parts << QStringLiteral("list cur=%1 items=[%2]").arg(lw->currentRow()).arg(items.join(';'));
    }
    if (auto* b = qobject_cast<QAbstractButton*>(w)) {
        parts << QStringLiteral("btn='%1' checkable=%2 checked=%3").arg(b->text()).arg(b->isCheckable()).arg(b->isChecked());
    }
    if (auto* e = qobject_cast<QLineEdit*>(w)) {
        parts << QStringLiteral("edit='%1' maxlen=%2").arg(e->text()).arg(e->maxLength());
    }
    if (auto* s = qobject_cast<QSpinBox*>(w)) {
        parts << QStringLiteral("spin=%1 [%2..%3] step=%4 suffix='%5' special='%6'")
                     .arg(s->value()).arg(s->minimum()).arg(s->maximum()).arg(s->singleStep())
                     .arg(s->suffix()).arg(s->specialValueText());
    }
    if (auto* c = qobject_cast<QComboBox*>(w)) {
        QStringList items;
        for (int i = 0; i < c->count(); ++i) {
            items << QStringLiteral("%1=%2%3").arg(c->itemText(i)).arg(c->itemData(i).toString())
                         .arg(c->itemIcon(i).isNull() ? "" : "*");
        }
        parts << QStringLiteral("combo cur=%1 icon=%2x%3 items=[%4]").arg(c->currentIndex())
                     .arg(c->iconSize().width()).arg(c->iconSize().height()).arg(items.join(';'));
    }
    if (auto* t = qobject_cast<QTabWidget*>(w)) {
        QStringList tabs;
        for (int i = 0; i < t->count(); ++i) tabs << t->tabText(i);
        parts << QStringLiteral("tabs cur=%1 [%2]").arg(t->currentIndex()).arg(tabs.join(';'));
    }
    return parts.join(QStringLiteral(" | "));
}

// The hidden sprite selection combo (and Qt's popup internals below it) is a selection model,
// not UI. Its lines are tagged so a comparison can tell them from every other widget.
static QString ownerTag(QWidget* w)
{
    for (QWidget* cur = w; cur; cur = cur->parentWidget()) {
        if (cur->toolTip() == QLatin1String("Loaded sprite preview")) {
            return QStringLiteral("[hidden sprite selection combo] ");
        }
    }
    return QString();
}

// One hash over the thumbnails of all frame list rows. The application builds a thumbnail
// when its row comes into view; the dump asks for all of them, so that it shows the same for
// a tree that builds them up front.
static size_t frameThumbnailsHash(app::MainWindow& w)
{
#ifdef UI_DUMP_HAS_LAZY_THUMBNAILS
    w.m_frameThumbnails->ensureBuilt(0, w.m_frameList->count() - 1);
#endif
    size_t hash = 0;
    for (int row = 0; row < w.m_frameList->count(); ++row) {
        const QImage image = w.m_frameList->item(row)->icon().pixmap(w.m_frameList->iconSize())
                                 .toImage().convertToFormat(QImage::Format_ARGB32);
        // Seed 0, as everywhere in this file: a seeded qHashBits is not stable across runs.
        hash = hash * 31 + qHashBits(image.constBits(), static_cast<size_t>(image.sizeInBytes()));
    }
    return hash;
}

static void dump(const char* title, app::MainWindow& w)
{
    QApplication::processEvents();
    const size_t thumbnailsHash = frameThumbnailsHash(w);
    std::cout << "===== " << title << " =====\n";
    QStringList lines;
    for (QWidget* child : w.findChildren<QWidget*>()) {
        lines << ownerTag(child) + describe(child, &w);
    }
    lines.sort();
    for (const QString& line : lines) std::cout << line.toStdString() << '\n';
    std::cout << "--- status: " << w.statusBar()->currentMessage().toStdString() << '\n';
    std::cout << "--- frame thumbnails: " << w.m_frameList->count() << " hash=" << thumbnailsHash << '\n';
    std::cout << "--- undo=" << w.m_undoAction->isEnabled() << " redo=" << w.m_redoAction->isEnabled()
              << " activeColor=" << w.m_activeDrawColor
              << " dirMode=" << w.m_canvasEdit.isSelectionDirectionMode()
              << " sel=" << w.m_canvasEdit.hasSelection()
              << " pending=" << w.m_canvasEdit.isFreeGradientPending() << '\n';
    if (w.m_document) {
        const auto& d = *w.m_document;
        std::cout << "--- doc dirty=" << d.isDirty();
        for (std::size_t p = 0; p < d.paletteCount(); ++p) {
            std::cout << " pal" << p << "='" << d.palette(p).name << "':";
            for (std::size_t c = 0; c < 8; ++c) std::cout << std::hex << d.paletteColor(p, c) << std::dec << ',';
        }
        for (std::size_t f = 0; f < d.frameCount(); ++f) {
            for (auto r : {render::Resolution::Sd, render::Resolution::Hd}) {
                // A project that is edited on its original plane only has no other to read.
                if (r == render::Resolution::Hd && d.editsOriginalPlaneOnly()) continue;
                for (std::size_t s = 0; s < document::kMaxColorRotations; ++s) {
                    std::cout << " rot" << f << (r == render::Resolution::Sd ? "sd" : "hd") << s << "="
                              << d.colorRotationLength(f, r, s) << '/' << d.colorRotationDelayMs(f, r, s) << '/'
                              << std::hex << d.colorRotationColor(f, r, s, 0) << std::dec;
                }
            }
            unsigned long sum = 0;
            for (int y = 0; y < 32; ++y) for (int x = 0; x < 128; ++x)
                sum = sum * 31 + d.colorizedPixel(f, render::Resolution::Sd, x, y);
            std::cout << " px" << f << '=' << sum;
        }
        std::cout << '\n';
        std::cout << "--- sprites:";
        for (std::size_t i = 0; i < d.spriteCount(); ++i) {
            const document::SpriteRecord& sp = d.sprite(i);
            std::cout << " s" << i << "='" << sp.name << "' hd=" << sp.hasHd << " areas=";
            for (const auto& a : sp.detectionAreas) std::cout << a.x << ',' << a.y << ',' << a.width << ',' << a.height << ';';
            std::cout << " o=" << qHashBits(sp.originalSd.pixels.data(), sp.originalSd.pixels.size())
                      << " c=" << qHashBits(sp.colorizedSd.pixels.data(), sp.colorizedSd.pixels.size() * 2)
                      << " m=" << qHashBits(sp.maskHd.pixels.data(), sp.maskHd.pixels.size())
                      << " h=" << qHashBits(sp.colorizedHd.pixels.data(), sp.colorizedHd.pixels.size() * 2);
        }
        std::cout << '\n' << "--- frame sprite refs:";
        for (std::size_t f = 0; f < d.frameCount(); ++f) {
            const auto& refs = d.frameSpriteReferences(f);
            std::cout << " f" << f << "(" << refs.size() << ")";
            for (std::size_t slot = 0; slot < refs.size(); ++slot) {
                if (refs[slot].spriteId == 255 && refs[slot].minX == 0 && refs[slot].minY == 0 &&
                    refs[slot].maxX == 0 && refs[slot].maxY == 0) continue;
                std::cout << ' ' << slot << ':' << int(refs[slot].spriteId) << '[' << refs[slot].minX << ','
                          << refs[slot].minY << '-' << refs[slot].maxX << ',' << refs[slot].maxY << ']';
            }
            std::cout << " hd=" << d.hasHdFrame(f);
        }
        std::cout << '\n';
    }
    {
        const std::optional<std::size_t> spriteSel = w.currentSpriteIndex();
        const std::optional<std::size_t> captureFrame = w.m_canvasEdit.captureSelectionFrame();
        const std::size_t captureCount = w.m_canvasEdit.captureSelectionPixels(captureFrame).size();
        std::cout << "--- sprite state: sel=" << (spriteSel ? int(*spriteSel) : -1)
                  << " canvasActive=" << w.isSpriteCanvasActive()
                  << " captureMode=" << w.isSpriteCaptureSelectionModeActive()
                  << " dirty=" << w.m_spriteDependentViewsDirty
                  << " captureFrame=" << (captureFrame ? int(*captureFrame) : -1)
                  << " captureMask=" << captureCount
                  << " capturePixels=" << w.m_canvasEdit.captureSelectionPixels(w.currentFrameIndex()).size()
                  << " res=" << (w.m_activeResolution == render::Resolution::Hd ? "hd" : "sd")
                  << " stack=" << (w.m_canvasStack->currentWidget() == w.m_spriteEditor ? "editor" : "preview")
                  << " keys=";
        for (const edit::HistoryKey& key : w.currentHistoryKeys()) {
            std::cout << int(key.kind) << '/' << key.index << '/' << (key.resolution == render::Resolution::Hd ? "hd" : "sd")
                      << '/' << w.m_undoHistories.canUndo(key) << w.m_undoHistories.canRedo(key) << ' ';
        }
        std::cout << '\n';
        const auto& pv = *w.m_preview;
        std::cout << "--- preview: frame=" << pv.m_frameIndex << " has=" << pv.m_hasFrame
                  << " boundsOverlay=" << pv.m_showSelectedSpriteBoundsOverlay << " ref=";
        if (pv.m_selectedSpriteReference) {
            const auto& r = *pv.m_selectedSpriteReference;
            std::cout << int(r.spriteId) << '[' << r.minX << ',' << r.minY << '-' << r.maxX << ',' << r.maxY << ']';
        } else {
            std::cout << "none";
        }
        std::cout << " selOverlay=" << pv.m_selectionOverlayPixels.size()
                  << " selRegion=" << int(pv.m_selectionOverlayRegion)
                  << " previewPixels=" << pv.m_previewPixels.size()
                  << " res=" << (pv.m_activeResolution == render::Resolution::Hd ? "hd" : "sd") << '\n';
        const auto& ed = *w.m_spriteEditor;
        std::cout << "--- editor: doc=" << (ed.m_document != nullptr)
                  << " sprite=" << (ed.m_spriteIndex ? int(*ed.m_spriteIndex) : -1)
                  << " areasVisible=" << ed.m_showDetectionAreas
                  << " area=" << (ed.m_selectedAreaIndex ? int(*ed.m_selectedAreaIndex) : -1)
                  << " res=" << (ed.m_activeResolution == render::Resolution::Hd ? "hd" : "sd")
                  << " tool=" << int(ed.m_drawingTool) << " color=" << ed.m_activeDrawColor << '\n';
        std::cout << "--- preview flags: maskOverlay=" << pv.m_showComparisonMaskOverlay
                  << " dynOverlay=" << pv.m_showDynamicMaskOverlay
                  << " bgMaskOverlay=" << pv.m_showBackgroundMaskOverlay
                  << " bgLayer=" << pv.m_showBackgroundLayer
                  << " bgEdit=" << pv.m_showBackgroundImageEditMode
                  << " selBg=" << (pv.m_selectedBackgroundIndex ? int(*pv.m_selectedBackgroundIndex) : -1) << '\n';
        const std::optional<std::size_t> selBg = w.selectedBackgroundIndex();
        const std::optional<std::size_t> curBg = w.currentBackgroundIndex();
        const std::optional<std::size_t> curMask = w.currentComparisonMaskIndex();
        const std::optional<std::size_t> curFrame = w.currentFrameIndex();
        std::cout << "--- window state: frame=" << (curFrame ? int(*curFrame) : -1)
                  << " selected=";
        for (std::size_t f : w.selectedFrameIndices()) std::cout << f << ',';
        std::cout << " dynSet=" << int(w.activeDynamicSet())
                  << " target=" << int(w.activePaintTarget())
                  << " selBg=" << (selBg ? int(*selBg) : -1)
                  << " curBg=" << (curBg ? int(*curBg) : -1)
                  << " curMask=" << (curMask ? int(*curMask) : -1)
                  << " maskMode=" << w.isComparisonMaskModeActive()
                  << " dynMode=" << w.isDynamicMaskModeActive()
                  << " bgMaskMode=" << w.isBackgroundMaskModeActive()
                  << " bgTarget=" << w.isBackgroundImageTargetActive()
                  << " tool=" << int(w.m_drawingTool)
                  << " playing=" << w.m_playbackTimer->isActive()
                  << " interval=" << w.m_playbackTimer->interval()
                  << " rotTimer=" << w.m_colorRotationPreviewTimer->isActive()
                  << " dmd=" << int(w.currentDmdRenderOptions().pixelShape) << '/'
                  << w.currentDmdRenderOptions().pixelCoverage << '\n';
        if (w.m_document) {
            const auto& d = *w.m_document;
            const auto hashIdx = [](const document::IndexedFrame& f) {
                return qHashBits(f.pixels.data(), f.pixels.size());
            };
            const auto hashRgb = [](const document::Rgb565Frame& f) {
                return qHashBits(f.pixels.data(), f.pixels.size() * 2);
            };
            std::cout << "--- frame meta:";
            for (std::size_t f = 0; f < d.frameCount(); ++f) {
                std::cout << " f" << f << "[mask=" << int(d.comparisonMaskId(f))
                          << " shape=" << int(d.shapeComparisonMode(f))
                          << " trig=" << d.triggerId(f) << " dur=" << d.frameDurationMs(f)
                          << " hd=";
                if (d.hasHdFrame(f)) std::cout << hashRgb(d.colorizedFrame(f, render::Resolution::Hd));
                else std::cout << "none";
                const auto& bg = d.backgroundData(f);
                if (bg) {
                    std::cout << " bg=" << bg->backgroundId << '/' << hashIdx(bg->maskSd) << '/' << hashIdx(bg->maskHd);
                } else {
                    std::cout << " bg=none";
                }
                const auto& dyn = d.dynamicData(f);
                if (dyn) {
                    std::cout << " dyn=" << hashIdx(dyn->maskSd) << '/' << hashIdx(dyn->maskHd)
                              << '/' << qHashBits(dyn->colorsSd.data(), dyn->colorsSd.size() * 2)
                              << '/' << qHashBits(dyn->colorsHd.data(), dyn->colorsHd.size() * 2)
                              << '/' << qHashBits(dyn->shadowDirectionsSd.data(), dyn->shadowDirectionsSd.size())
                              << '/' << qHashBits(dyn->shadowColorsSd.data(), dyn->shadowColorsSd.size() * 2)
                              << '/' << qHashBits(dyn->shadowDirectionsHd.data(), dyn->shadowDirectionsHd.size())
                              << '/' << qHashBits(dyn->shadowColorsHd.data(), dyn->shadowColorsHd.size() * 2);
                } else {
                    std::cout << " dyn=none";
                }
                std::cout << ']';
            }
            std::cout << '\n' << "--- masks:";
            for (std::size_t m = 0; m < d.comparisonMaskCount(); ++m) std::cout << ' ' << hashIdx(d.comparisonMask(m));
            std::cout << '\n' << "--- backgrounds:";
            for (std::size_t b = 0; b < d.backgroundCount(); ++b) {
                std::cout << ' ' << hashRgb(d.background(b).colorizedSd) << '/';
                if (d.background(b).colorizedHd) std::cout << hashRgb(*d.background(b).colorizedHd);
                else std::cout << "none";
            }
            std::cout << '\n';
        }
        QWidget* canvas = w.m_canvasStack->currentWidget();
        const QImage image = canvas->grab().toImage().convertToFormat(QImage::Format_ARGB32);
        std::cout << "--- canvas grab: " << image.width() << 'x' << image.height() << ':'
                  << qHashBits(image.constBits(), static_cast<size_t>(image.sizeInBytes())) << '\n';
    }
}

static void dumpFocusChain(app::MainWindow& w)
{
    std::cout << "===== focus chain =====\n";
    QWidget* cur = &w;
    int guard = 0;
    do {
        QString text;
        if (auto* b = qobject_cast<QAbstractButton*>(cur)) text = b->text();
        std::cout << ownerTag(cur).toStdString() << cur->metaObject()->className() << " | " << cur->toolTip().toStdString() << " | "
                  << text.toStdString() << " | fp=" << cur->focusPolicy() << '\n';
        cur = cur->nextInFocusChain();
    } while (cur != &w && ++guard < 5000);
}

template <typename T>
static T* byTip(app::MainWindow& w, const char* tip)
{
    T* found = nullptr;
    for (T* child : w.findChildren<T*>()) {
        if (child->toolTip() == QLatin1String(tip)) {
            if (found) { std::cerr << "ambiguous tip " << tip << '\n'; std::exit(2); }
            found = child;
        }
    }
    if (!found) { std::cerr << "missing tip " << tip << '\n'; std::exit(2); }
    return found;
}

// For a widget whose tool tip a change rewrote: the scripts run against both trees.
template <typename T>
static T* byAnyTip(app::MainWindow& w, std::initializer_list<const char*> tips)
{
    for (const char* tip : tips) {
        for (T* child : w.findChildren<T*>()) {
            if (child->toolTip() == QLatin1String(tip)) {
                return byTip<T>(w, tip);
            }
        }
    }
    std::cerr << "missing tip " << *tips.begin() << '\n';
    std::exit(2);
}

static QToolButton* gridButton(app::MainWindow& w, const char* gridTip, int index)
{
    QWidget* grid = byTip<QWidget>(w, gridTip);
    auto buttons = grid->findChildren<QToolButton*>(QString(), Qt::FindDirectChildrenOnly);
    return buttons.at(index);
}

// For buttons whose tool tip changes with the state.
static QToolButton* toolButtonByTextPrefix(app::MainWindow& w, const char* textPrefix)
{
    QToolButton* found = nullptr;
    for (QToolButton* button : w.findChildren<QToolButton*>()) {
        if (button->text().startsWith(QLatin1String(textPrefix))) {
            if (found) { std::cerr << "ambiguous text " << textPrefix << '\n'; std::exit(2); }
            found = button;
        }
    }
    if (!found) { std::cerr << "missing text " << textPrefix << '\n'; std::exit(2); }
    return found;
}

int main(int argc, char* argv[])
{
    QApplication appInstance(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("ColorizingDMDLayoutDump"));
    QApplication::setApplicationName(QStringLiteral("LayoutDump"));
    app::MainWindow w;
    w.show();
    dump("empty", w);
    dumpFocusChain(w);

    document::ProjectSnapshot snapshot;
    snapshot.colorCount = 16;
    snapshot.romName = "dump";
    for (int i = 0; i < 3; ++i) {
        snapshot.frames.push_back(document::ProjectFrameSnapshot{
            document::makeIndexedFrame(render::kSdFrameSize, 0),
            document::makeRgb565Frame(render::kSdFrameSize, 0),
            document::makeRgb565Frame(render::kHdFrameSize, 0),
            0,
            255,
        });
    }
    document::PaletteRecord p0;
    p0.colors.assign(document::kPaletteColorCount, 0);
    for (std::size_t i = 0; i < p0.colors.size(); ++i) p0.colors[i] = static_cast<uint16_t>(i * 1000);
    document::PaletteRecord p1 = p0;
    p1.name = "second";
    p1.colors[0] = 0xf800;
    p1.colors[7] = 0x001f;
    document::PaletteRecord p2;
    p2.colors.assign(document::kPaletteColorCount, 0x1234);
    p2.name = "tiny";
    snapshot.palettes = {p0, p1, p2};
    w.m_document = document::ProjectDocument::fromSnapshot(snapshot);
    w.m_document->setColorRotationLength(0, render::Resolution::Sd, 1, 3);
    w.m_document->setColorRotationDelayMs(0, render::Resolution::Sd, 1, 250);
    w.m_document->setColorRotationColor(0, render::Resolution::Sd, 1, 0, 0xf800);
    w.m_document->setColorRotationColor(0, render::Resolution::Sd, 1, 2, 0x07e0);
    w.m_document->setColorRotationLength(0, render::Resolution::Hd, 0, 2);
    w.m_document->setColorRotationLength(1, render::Resolution::Sd, 1, 1);
    w.m_document->markClean();
    w.refreshProjectSummary();
    dump("loaded", w);

    auto* rotSlot = byTip<QComboBox>(w, "Color rotation slot for the active SD/HD mode");
    auto* rotLength = byTip<QSpinBox>(w, "Number of colors in this rotation");
    auto* rotDelay = byTip<QSpinBox>(w, "Delay between rotation steps");
    auto* rotClear = byTip<QToolButton>(w, "Clear the selected color rotation slot");
    rotSlot->setCurrentIndex(1);
    dump("rot slot 1", w);
    rotLength->setValue(5);
    dump("rot length 5", w);
    rotDelay->setValue(120);
    dump("rot delay 120", w);
    gridButton(w, "Colors in the selected rotation", 40)->click();
    dump("rot inactive color click", w);
    w.m_hdButton->click();
    dump("hd", w);
    rotSlot->setCurrentIndex(0);
    rotLength->setValue(4);
    dump("hd slot0 len4", w);
    w.m_sdButton->click();
    rotSlot->setCurrentIndex(1);
    w.m_frameList->selectAll();
    dump("all frames selected", w);
    rotDelay->setValue(77);
    dump("multi delay 77", w);
    rotClear->click();
    dump("rot clear", w);
    w.m_frameList->setCurrentRow(1);
    dump("frame 1", w);
    w.m_frameList->setCurrentRow(0);

    auto* palCombo = byTip<QComboBox>(w, "Selected palette");
    auto* palName = byTip<QLineEdit>(w, "Selected palette name");
    auto* palFrom = byTip<QSpinBox>(w, "First palette color index for gradient fill");
    auto* palTo = byTip<QSpinBox>(w, "Last palette color index for gradient fill");
    auto* palShape = byTip<QComboBox>(w, "Shape used by the Grad drawing tool");
    auto* palPattern = byTip<QComboBox>(w, "Color distribution used by the Grad drawing tool");
    auto* palDirection = byTip<QComboBox>(w, "Direction used by the Grad drawing tool");
    auto* palApply = byTip<QToolButton>(w, "Fill the selected palette range between the endpoint colors");
    auto* palFill = byTip<QToolButton>(w, "Fill the current canvas selection with the palette gradient");
    auto* palSetDir = byTip<QToolButton>(w, "Draw the free gradient direction line for the current selection");
    auto* palClearSel = byTip<QToolButton>(w, "Clear the current canvas selection");

    palCombo->setCurrentIndex(1);
    dump("palette 1", w);
    palName->setText(QStringLiteral("renamed"));
    Q_EMIT palName->editingFinished();
    dump("palette renamed", w);
    gridButton(w, "Colors in the selected palette", 7)->click();
    dump("palette pick 8", w);
    palFrom->setValue(1);
    palTo->setValue(8);
    dump("palette range", w);
    palApply->click();
    dump("palette apply gradient", w);
    w.m_undoAction->trigger();
    dump("undo gradient", w);
    w.m_redoAction->trigger();
    dump("redo gradient", w);
    palTo->setValue(1);
    palApply->click();
    dump("apply same endpoints", w);
    palFrom->setValue(8);
    palTo->setValue(2);

    w.setDrawingTool(app::MainWindow::DrawingTool::Selection);
    for (int shape = 0; shape < 3; ++shape) {
        palShape->setCurrentIndex(shape);
        w.m_canvasEdit.setSelectionFromStroke(w.canvasEditContext(), render::CanvasHit{render::CanvasRegion::ColorizedFrame, render::Resolution::Sd, 2, 2},
                                       render::CanvasHit{render::CanvasRegion::ColorizedFrame, render::Resolution::Sd, 20, 12});
        dump("selection made", w);
        for (int pattern = 0; pattern < 3; ++pattern) {
            palPattern->setCurrentIndex(pattern);
            for (int direction = 0; direction < palDirection->count(); ++direction) {
                palDirection->setCurrentIndex(direction);
                palFill->click();
                std::cout << "fill shape=" << shape << " pattern=" << pattern << " dir=" << direction << '\n';
                dump("fill", w);
            }
        }
    }
    palDirection->setCurrentIndex(0);
    palSetDir->click();
    dump("set dir on", w);
    palSetDir->click();
    dump("set dir off", w);
    palSetDir->click();
    w.m_canvasEdit.setSelectionDirection(render::CanvasHit{render::CanvasRegion::ColorizedFrame, render::Resolution::Sd, 2, 2},
                                  render::CanvasHit{render::CanvasRegion::ColorizedFrame, render::Resolution::Sd, 20, 2});
    dump("dir line set", w);
    palFill->click();
    dump("free fill", w);
    palSetDir->click();
    palCombo->setCurrentIndex(2);
    dump("palette 2 tiny while dir mode", w);
    palClearSel->click();
    dump("clear sel", w);
    palSetDir->click();
    dump("set dir without selection", w);

    w.setDrawingTool(app::MainWindow::DrawingTool::PaletteGradient);
    palCombo->setCurrentIndex(1);
    palDirection->setCurrentIndex(3);
    palShape->setCurrentIndex(1);
    palPattern->setCurrentIndex(2);
    w.m_preview->m_strokeFinishedHandler(render::CanvasHit{render::CanvasRegion::ColorizedFrame, render::Resolution::Sd, 30, 3},
                       render::CanvasHit{render::CanvasRegion::ColorizedFrame, render::Resolution::Sd, 60, 25}, false);
    dump("grad tool stroke", w);
    palDirection->setCurrentIndex(0);
    w.m_preview->m_strokeFinishedHandler(render::CanvasHit{render::CanvasRegion::ColorizedFrame, render::Resolution::Sd, 70, 3},
                       render::CanvasHit{render::CanvasRegion::ColorizedFrame, render::Resolution::Sd, 90, 25}, false);
    dump("grad tool free pending", w);
    palPattern->setCurrentIndex(0);
    dump("pattern change cancels pending", w);

    w.m_document.reset();
    w.refreshProjectSummary();
    dump("closed", w);

#include "sprite_script.inc"

#include "inspector_script.inc"

    w.m_document.reset();
    w.refreshProjectSummary();
    dump("closed at the end", w);

#include "big_original_script.inc"

#include "native_original_script.inc"

// States of a tree that has the Runtime view; a tree without it ends here.
#ifdef UI_DUMP_HAS_RUNTIME_VIEW
#include "runtime_view_script.inc"
#endif

// States of a tree that can add and delete records; a tree without that ends here.
#ifdef UI_DUMP_HAS_STRUCTURE_COMMANDS
#include "structure_script.inc"
#endif

// States of a tree that imports frame dumps; a tree without that ends here.
#ifdef UI_DUMP_HAS_DUMP_IMPORT
#include "dump_import_script.inc"
#endif

// States of a tree with the bug fixes of 2026-10-14; a tree without them ends here.
#ifdef UI_DUMP_HAS_FIXES_2026_10_14
#include "fixes_script.inc"
#endif

// States of a tree with edit colors and color sets; a tree without them ends here.
#ifdef UI_DUMP_HAS_COLOR_SETS
#include "colors_script.inc"
#endif

// States of a tree with copy and paste and the selection helpers; a tree without them ends here.
#ifdef UI_DUMP_HAS_SELECTION_TOOLS
#include "selection_script.inc"
#endif

// States of a tree that opens and saves on a worker thread; a tree without that ends here.
#ifdef UI_DUMP_HAS_PROJECT_TASKS
#include "progress_script.inc"
#endif

// States of a tree with autosave and recovery; a tree without them ends here.
#ifdef UI_DUMP_HAS_AUTOSAVE
#include "autosave_script.inc"
#endif

// States of a tree with the section bar; a tree without it ends here.
#ifdef UI_DUMP_HAS_SECTIONS
#include "sections_script.inc"
#endif

    return 0;
}
