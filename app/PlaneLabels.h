#pragma once

#include <QCoreApplication>
#include <QString>

#include "document/ProjectDocument.h"
#include "render/ProjectGeometry.h"

namespace colorizing::app {

// "HD" and "SD" are shown to the user only where HD is an extra resolution next to SD: in the
// ordinary layout (and while no project is loaded). A project with another layout has one
// plane, the native one, which is named by its size, and no HD or SD wording, badge, counter,
// filter or control. A project that renders its extra plane only (PlaneLayout::NativeExtra)
// still needs that data for every frame, background and sprite; what lacks it has "no
// picture", a frame the runtime shows nothing for is "not rendered", and the repair is called
// "Create <size> Frame" / "Create <size> Sprite".
[[nodiscard]] bool hasHdAsExtraResolution(const document::ProjectDocument* document);

// "256x64"
[[nodiscard]] QString planeSizeText(render::Size size);
// The size of the project's native plane.
[[nodiscard]] QString nativePlaneText(const document::ProjectDocument& document);
// "HD" or "SD" in an ordinary project, otherwise the plane's size.
[[nodiscard]] QString planeLabel(const document::ProjectDocument& document,
                                 render::Resolution plane);

// What a list entry appends to a background or sprite: " (HD)" in an ordinary project when it
// has HD data, " (no picture)" in a project that renders the extra plane only when it lacks
// that data, nothing otherwise.
[[nodiscard]] QString extraDataSuffix(const document::ProjectDocument& document,
                                      bool hasExtraData);

// A sprite's entry in the left sidebar's list: "12 - Ball1 (HD)", or the number alone for a
// sprite without a name.
[[nodiscard]] QString spriteListCaption(const document::ProjectDocument& document,
                                        std::size_t sprite);

// "no picture", translated.
[[nodiscard]] QString noPictureText();

}  // namespace colorizing::app
