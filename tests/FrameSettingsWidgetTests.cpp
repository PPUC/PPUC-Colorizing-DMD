// The Copy Frame Settings dialog: its choices, the frames each changes, kept choices.

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include <QCheckBox>
#include <QCoreApplication>
#include <QLabel>
#include <QSettings>
#include <QTemporaryDir>

#include "RuntimeFixtures.h"
#include "dialogs/FrameSettingsDialog.h"
#include "document/ProjectDocument.h"

namespace {

using namespace colorizing;
using edit::FrameSettingsPart;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: frame settings dialog: " << message << '\n';
        std::exit(1);
    }
}

void testDialog()
{
    QTemporaryDir directory;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
    QCoreApplication::setOrganizationName(QStringLiteral("FrameSettingsDialogTests"));

    document::ProjectSnapshot snapshot =
        runtime_fixtures::makeProject(render::PlaneLayout::Ordinary, 3);
    snapshot.frames[0].durationMs = 55;
    snapshot.frames[2].scene = document::SceneFrameRef{51400, 1, 0};
    const document::ProjectDocument document =
        document::ProjectDocument::fromSnapshot(snapshot);
    {
        app::FrameSettingsDialog dialog(document, 0, {0, 1, 2});
        expect(dialog.checkBox(FrameSettingsPart::DynamicZones)->isChecked() &&
                   dialog.checkBox(FrameSettingsPart::Sprites)->isChecked() &&
                   !dialog.checkBox(FrameSettingsPart::Identification)->isChecked() &&
                   !dialog.checkBox(FrameSettingsPart::Duration)->isChecked(),
               "at first the settings of the picture are chosen, not how the frame plays");
        expect(dialog.checkBox(FrameSettingsPart::Duration)
                   ->text()
                   .contains(QStringLiteral("changes 2 frame")),
               "each choice says how many frames it changes: " +
                   dialog.checkBox(FrameSettingsPart::Duration)->text().toStdString());
        QLabel* note = nullptr;
        for (QLabel* label : dialog.findChildren<QLabel*>()) {
            if (label->text().contains(QStringLiteral("frames of scenes"))) {
                note = label;
            }
        }
        expect(note != nullptr, "the dialog says that a target is a frame of a scene");
        dialog.checkBox(FrameSettingsPart::Duration)->setChecked(true);
        dialog.checkBox(FrameSettingsPart::Sprites)->setChecked(false);
        dialog.accept();
        const std::vector<FrameSettingsPart> parts = dialog.parts();
        expect(std::find(parts.begin(), parts.end(), FrameSettingsPart::Duration) !=
                       parts.end() &&
                   std::find(parts.begin(), parts.end(), FrameSettingsPart::Sprites) ==
                       parts.end(),
               "the parts are the chosen ones");
    }
    {
        app::FrameSettingsDialog dialog(document, 0, {1});
        expect(dialog.checkBox(FrameSettingsPart::Duration)->isChecked() &&
                   !dialog.checkBox(FrameSettingsPart::Sprites)->isChecked(),
               "the choices are kept for the next time");
    }
    {
        // Issue #48: the sprite section's Copy to Selected Frames chooses the sprites only and
        // leaves the kept choices alone.
        app::FrameSettingsDialog dialog(document, 0, {1}, nullptr,
                                        std::vector<FrameSettingsPart>{FrameSettingsPart::Sprites});
        expect(dialog.parts() == std::vector<FrameSettingsPart>{FrameSettingsPart::Sprites},
               "a dialog for the sprites chooses only them");
        dialog.accept();
    }
    {
        app::FrameSettingsDialog dialog(document, 0, {1});
        expect(dialog.checkBox(FrameSettingsPart::Duration)->isChecked() &&
                   !dialog.checkBox(FrameSettingsPart::Sprites)->isChecked(),
               "the choices kept are those of the last full dialog");
    }
    QCoreApplication::setOrganizationName(QString());
    QSettings::setDefaultFormat(QSettings::NativeFormat);
}

}  // namespace

void runFrameSettingsWidgetTests()
{
    testDialog();
}
