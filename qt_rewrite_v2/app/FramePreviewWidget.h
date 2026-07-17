#pragma once

#include <cstddef>

#include <QWidget>

#include "document/ProjectDocument.h"
#include "render/CanvasLayout.h"

namespace colorizing::app {

class FramePreviewWidget final : public QWidget {
public:
    explicit FramePreviewWidget(QWidget* parent = nullptr);

    void setDocument(const document::ProjectDocument* document);
    void setFrameIndex(std::size_t frameIndex);
    void setActiveResolution(render::Resolution resolution);

    [[nodiscard]] QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void paintEmpty(QPainter& painter);
    void paintFrame(QPainter& painter);
    void paintRegion(QPainter& painter, const render::CanvasRegionLayout& region);

    const document::ProjectDocument* m_document = nullptr;
    std::size_t m_frameIndex = 0;
    bool m_hasFrame = false;
    render::Resolution m_activeResolution = render::Resolution::Sd;
};

}  // namespace colorizing::app

