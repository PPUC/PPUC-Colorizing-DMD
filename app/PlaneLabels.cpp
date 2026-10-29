#include "PlaneLabels.h"

namespace colorizing::app {

bool hasHdAsExtraResolution(const document::ProjectDocument* document)
{
    return !document || document->geometry().layout() == render::PlaneLayout::Ordinary;
}

QString planeSizeText(render::Size size)
{
    return QStringLiteral("%1x%2").arg(size.width).arg(size.height);
}

QString nativePlaneText(const document::ProjectDocument& document)
{
    return planeSizeText(document.originalFrameSize());
}

QString planeLabel(const document::ProjectDocument& document, render::Resolution plane)
{
    if (!hasHdAsExtraResolution(&document)) {
        return planeSizeText(document.geometry().planeSize(plane));
    }
    return plane == render::Resolution::Hd ? QCoreApplication::translate("PlaneLabels", "HD")
                                           : QCoreApplication::translate("PlaneLabels", "SD");
}

QString extraDataSuffix(const document::ProjectDocument& document, bool hasExtraData)
{
    if (hasHdAsExtraResolution(&document)) {
        return hasExtraData ? QCoreApplication::translate("PlaneLabels", " (HD)") : QString();
    }
    return document.rendersHdPlaneOnly() && !hasExtraData
               ? QStringLiteral(" (%1)").arg(noPictureText())
               : QString();
}

QString spriteListCaption(const document::ProjectDocument& document, std::size_t sprite)
{
    const document::SpriteRecord& record = document.sprite(sprite);
    const QString number = QString::number(static_cast<int>(sprite));
    const QString label = record.name.empty()
                              ? number
                              : QStringLiteral("%1 - %2").arg(
                                    number, QString::fromStdString(record.name));
    return label + extraDataSuffix(document, record.hasHd);
}

QString noPictureText()
{
    return QCoreApplication::translate("PlaneLabels", "no picture");
}

}  // namespace colorizing::app
