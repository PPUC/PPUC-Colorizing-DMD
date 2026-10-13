#include "render/CanvasLayout.h"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

using colorizing::render::CanvasHit;
using colorizing::render::CanvasLayout;
using colorizing::render::CanvasLayoutOptions;
using colorizing::render::CanvasRegion;
using colorizing::render::Point;
using colorizing::render::Resolution;

namespace {

void fail(const std::string& message)
{
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        fail(message);
    }
}

CanvasHit requireHit(const CanvasLayout& layout, Point point)
{
    std::optional<CanvasHit> hit = layout.hitTest(point);
    if (!hit) {
        fail("expected hit at " + std::to_string(point.x) + "," + std::to_string(point.y));
    }
    return *hit;
}

void testSdAndOriginalHaveSameDisplaySize()
{
    CanvasLayoutOptions options;
    options.activeResolution = Resolution::Sd;
    options.showOriginal = true;
    options.showHdReference = false;
    options.gap = 8;

    const CanvasLayout layout = CanvasLayout::buildFrameLayout(options);

    expect(layout.displaySize().width == 256, "SD layout width should use HD display units");
    expect(layout.displaySize().height == 64 + 8 + 64, "SD + original layout height");
    expect(layout.regions().size() == 2, "SD layout should contain colorized and original regions");

    const CanvasHit colorHit = requireHit(layout, Point{255, 63});
    expect(colorHit.region == CanvasRegion::ColorizedFrame, "bottom-right SD colorized region");
    expect(colorHit.resolution == Resolution::Sd, "colorized SD resolution");
    expect(colorHit.x == 127 && colorHit.y == 31, "SD display coordinates map to SD pixels");

    const CanvasHit originalHit = requireHit(layout, Point{0, 72});
    expect(originalHit.region == CanvasRegion::OriginalFrame, "original region starts after gap");
    expect(originalHit.resolution == Resolution::Sd, "original region resolution");
    expect(originalHit.x == 0 && originalHit.y == 0, "original hit maps to first SD pixel");

    expect(!layout.hitTest(Point{10, 66}), "gap between regions should not hit");
}

void testHdActiveFrameUsesHdCoordinates()
{
    CanvasLayoutOptions options;
    options.activeResolution = Resolution::Hd;
    options.showOriginal = true;
    options.showHdReference = false;
    options.gap = 8;

    const CanvasLayout layout = CanvasLayout::buildFrameLayout(options);

    expect(layout.displaySize().width == 256, "HD active layout width");
    expect(layout.displaySize().height == 64 + 8 + 64, "HD + original layout height");

    const CanvasHit colorHit = requireHit(layout, Point{255, 63});
    expect(colorHit.region == CanvasRegion::ColorizedFrame, "HD active colorized region");
    expect(colorHit.resolution == Resolution::Hd, "HD active resolution");
    expect(colorHit.x == 255 && colorHit.y == 63, "HD display coordinates map to HD pixels");

    const CanvasHit originalHit = requireHit(layout, Point{255, 127 + 8});
    expect(originalHit.region == CanvasRegion::OriginalFrame, "original remains SD under HD active frame");
    expect(originalHit.resolution == Resolution::Sd, "original SD resolution under HD active frame");
    expect(originalHit.x == 127 && originalHit.y == 31, "original still maps to SD pixels");
}

void testOptionalHdReferenceRegion()
{
    CanvasLayoutOptions options;
    options.activeResolution = Resolution::Sd;
    options.showOriginal = true;
    options.showHdReference = true;
    options.hasHdFrame = true;
    options.gap = 6;

    const CanvasLayout layout = CanvasLayout::buildFrameLayout(options);

    expect(layout.regions().size() == 3, "HD reference should add third region when available");
    expect(layout.displaySize().height == 64 + 6 + 64 + 6 + 64, "three stacked regions height");

    const CanvasHit hdHit = requireHit(layout, Point{200, 64 + 6 + 64 + 6 + 10});
    expect(hdHit.region == CanvasRegion::HdFrame, "HD reference region hit");
    expect(hdHit.resolution == Resolution::Hd, "HD reference resolution");
    expect(hdHit.x == 200 && hdHit.y == 10, "HD reference maps one-to-one");
}

void testMissingHdReferenceDoesNotCreateRegion()
{
    CanvasLayoutOptions options;
    options.activeResolution = Resolution::Sd;
    options.showOriginal = false;
    options.showHdReference = true;
    options.hasHdFrame = false;

    const CanvasLayout layout = CanvasLayout::buildFrameLayout(options);

    expect(layout.regions().size() == 1, "missing HD frame should not create an HD region");
    expect(!layout.region(CanvasRegion::HdFrame), "missing HD frame region lookup");
}

}  // namespace

void runProjectDocumentTests();
void runDrawingToolsTests();
void runEditCommandTests();
void runLegacyCromFramesTests();
void runLegacyCromMetadataTests();
void runLegacyCrpOriginalsTests();
void runLegacyProjectFileReaderTests();
void runLegacyProjectIoTests();
void runLegacyProjectSerializerTests();
void runNativeOriginalProjectTests();
void runProjectGeometryTests();
void runRuntimePreviewTests();

int main()
{
    testSdAndOriginalHaveSameDisplaySize();
    testHdActiveFrameUsesHdCoordinates();
    testOptionalHdReferenceRegion();
    testMissingHdReferenceDoesNotCreateRegion();
    runProjectDocumentTests();
    runDrawingToolsTests();
    runEditCommandTests();
    runLegacyCromFramesTests();
    runLegacyCromMetadataTests();
    runLegacyCrpOriginalsTests();
    runLegacyProjectFileReaderTests();
    runLegacyProjectIoTests();
    runLegacyProjectSerializerTests();
    runNativeOriginalProjectTests();
    runProjectGeometryTests();
    runRuntimePreviewTests();

    std::cout << "colorizing_qt_v2_tests passed\n";
    return 0;
}
