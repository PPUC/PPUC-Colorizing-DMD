#pragma once

#include <QImage>
#include <QWidget>

#include <functional>

#include "render/CanvasTypes.h"

namespace colorizing::app {

// A DMD picture that fills the room it has: rendered at the largest whole number of screen
// pixels per frame pixel that fits (at least 1), centred, on black. The frame tester's
// pictures, which grow with its window.
class FramePictureView final : public QWidget {
public:
    // Renders the frame with that many screen pixels per frame pixel.
    using Renderer = std::function<QImage(int pixelScale)>;

    explicit FramePictureView(QWidget* parent = nullptr);

    // frameSize: the frame's pixels.
    void setFrame(render::Size frameSize, Renderer renderer);
    void clear();
    [[nodiscard]] bool hasFrame() const { return static_cast<bool>(m_renderer); }
    // The scale the picture is drawn at in the current size; 0 without a frame.
    [[nodiscard]] int pixelScale() const;

    [[nodiscard]] QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    render::Size m_frameSize{0, 0};
    Renderer m_renderer;
    // The picture rendered last, and its scale.
    QImage m_image;
    int m_imageScale = 0;
};

}  // namespace colorizing::app
