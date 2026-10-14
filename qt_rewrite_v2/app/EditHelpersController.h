#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <QString>

#include "CanvasEditController.h"
#include "edit/AutoCopy.h"
#include "edit/ScrollFill.h"

namespace colorizing::app {

struct EditHelpersOutputs {
    std::function<void(std::unique_ptr<edit::EditCommand> command)> commit;
    std::function<void()> commitsFinished;
    std::function<void(const QString& text, int timeoutMs)> showStatus;
};

// What auto copy would do, for the question before it runs.
struct AutoCopyPlan {
    edit::AutoCopyScan scan;
    // Matches whose pixels or dynamic mask would change.
    std::vector<std::size_t> changingFrames;
};

// What a scrolling fill would do.
struct ScrollFillPlan {
    std::size_t firstFrame = 0;
    std::size_t lastFrame = 0;
    std::vector<edit::ScrollFillFrame> frames;
    [[nodiscard]] std::size_t changingCount() const;
};

// The helpers of the Edit menu that work on frames rather than on the selection itself: auto
// copy, scrolling auto fill, the recolor wand, "black original to black" and the dynamic
// helpers. Each builds one FrameContentCommand per changed frame (in the frame's pixel
// history, or in Dynamic mode its dynamic mask history, so that it is undone where it was made)
// and hands them to its owner.
class EditHelpersController {
public:
    explicit EditHelpersController(EditHelpersOutputs outputs = {});

    // Why the helper cannot run in the context (plane, layer), or an empty string.
    [[nodiscard]] static QString autoCopyRefusal(const CanvasEditContext& context,
                                                 const edit::PlaneSelection* selection);
    // candidates: the frames to look at, all when empty.
    [[nodiscard]] AutoCopyPlan planAutoCopy(const CanvasEditContext& context,
                                            const edit::PlaneSelection& selection,
                                            const std::vector<std::size_t>& candidates) const;
    void applyAutoCopy(const CanvasEditContext& context,
                       const edit::PlaneSelection& selection,
                       const AutoCopyPlan& plan);

    [[nodiscard]] static QString scrollFillRefusal(const CanvasEditContext& context);
    [[nodiscard]] ScrollFillPlan planScrollFill(const CanvasEditContext& context) const;
    void applyScrollFill(const CanvasEditContext& context, ScrollFillPlan plan);

    // The wand: every pixel of the clicked pixel's color in each selected frame gets the draw
    // color, or the edit color of its shade in color set replace mode. Refused on a dynamic
    // pixel, as in Win32.
    void recolor(const CanvasEditContext& context, const render::CanvasHit& hit);
    // On the selection when there is one on the plane, else on the whole frame; every selected
    // frame.
    void blackOriginalToBlack(const CanvasEditContext& context,
                              const edit::PlaneSelection* selection);
    void removeDynamicContent(const CanvasEditContext& context,
                              const edit::PlaneSelection* selection);
    void assignDynamicSet(const CanvasEditContext& context, const edit::PlaneSelection* selection);

private:
    // Commits the frames' changes; returns how many frames changed.
    std::size_t commitAll(const CanvasEditContext& context,
                          std::vector<std::pair<std::size_t, edit::FrameContentChanges>> changes);
    [[nodiscard]] bool requireDynamicPlane(const CanvasEditContext& context) const;
    void showStatus(const QString& text, int timeoutMs) const;

    EditHelpersOutputs m_outputs;
};

}  // namespace colorizing::app
