// The speaker light widgets: the Light Effects list, the Light Effect section with its
// preview, the frame's Speaker Lights section, the speakers beside the canvas, the defaults
// in Project Settings and the speakers of the frame tester.

#include <QApplication>
#include <QComboBox>
#include <QLineEdit>
#include <QListWidget>
#include <QSettings>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QToolButton>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "Appearance.h"
#include "RuntimeFixtures.h"
#include "SpeakerLightView.h"
#include "dialogs/FrameTesterDialog.h"
#include "dialogs/ProjectSettingsDialog.h"
#include "document/ProjectDocument.h"
#include "panels/FrameLightsPanel.h"
#include "panels/LightEffectPanel.h"
#include "panels/LightEffectsPanel.h"
#include "panels/ProjectSettingsPanel.h"

namespace {

using namespace colorizing;
using document::LightAction;
using document::LightActionKind;
using document::LightEffect;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: speaker light widgets: " << message << '\n';
        std::exit(1);
    }
}

LightEffect makeEffect(uint16_t id, const std::string& name, uint8_t mode)
{
    LightEffect effect;
    effect.id = id;
    effect.name = name;
    effect.mode = mode;
    effect.speed = 600;
    return effect;
}

// Effect 1 a rainbow, effect 2 a static green; frame 1 starts the rainbow on channel 1 and
// a static blue on channel 2; channel 1 defaults to effect 2.
document::ProjectDocument makeDocument()
{
    document::ProjectSnapshot snapshot =
        runtime_fixtures::makeProject(render::PlaneLayout::Ordinary, 3);
    snapshot.speakerLights.effects = {makeEffect(1, "Rainbow", 12),
                                      makeEffect(2, "Green", document::kLightModeStatic)};
    snapshot.speakerLights.effects[1].colors[0] = 0x0000ff00u;
    snapshot.speakerLights.defaults[0] = LightAction{LightActionKind::Effect, 2, 0};
    snapshot.frames[1].lights[0] = LightAction{LightActionKind::Effect, 1, 0};
    snapshot.frames[1].lights[1] = LightAction{LightActionKind::Color, 0, 0x000000ffu};
    return document::ProjectDocument::fromSnapshot(std::move(snapshot));
}

bool allLeds(const std::vector<uint32_t>& colors, uint32_t mask)
{
    if (colors.empty()) {
        return false;
    }
    for (uint32_t color : colors) {
        // Lit in the channel of mask only.
        if ((color & mask) == 0 || (color & ~mask & 0x00ffffffu) != 0) {
            return false;
        }
    }
    return true;
}

void testEffectList()
{
    const document::ProjectDocument document = makeDocument();
    app::LightEffectsPanel panel;
    int selections = 0;
    panel.onSelectionChanged = [&]() { ++selections; };
    panel.setProject(&document);
    expect(panel.list()->count() == 2 &&
               panel.list()->item(0)->text().startsWith(QStringLiteral("Rainbow\nRainbow Cycle")) &&
               panel.list()->item(0)->text().contains(QStringLiteral("1 frame")),
           "the effects are listed with their mode and the frames that start them");
    panel.selectEffect(2);
    expect(panel.selectedEffect() == 2 && selections == 1, "an effect is selected");
    panel.setProject(&document);
    expect(panel.selectedEffect() == 2, "and stays selected when the list is filled again");
}

void testEffectSection()
{
    app::LightEffectPanel panel;
    expect(!panel.effectId(), "without an effect nothing is edited");
    std::vector<LightEffect> applied;
    panel.onApplyRequested = [&](const LightEffect& effect) { applied.push_back(effect); };
    LightEffect green = makeEffect(2, "Green", document::kLightModeStatic);
    green.colors[0] = 0x0000ff00u;
    green.durationMs = 1500;
    panel.setEffect(green);
    expect(panel.effectId() == 2 && panel.effect() == green && !panel.hasChanges(),
           "the effect is shown as the project has it");
    panel.advancePreview(50);
    expect(allLeds(panel.preview()->colors(), 0x0000ff00u),
           "the preview plays it: green on every LED");
    panel.setColor(0, 0x00ff0000u);
    expect(panel.hasChanges() && panel.effect().colors[0] == 0x00ff0000u,
           "a new color is a change");
    panel.advancePreview(50);
    expect(allLeds(panel.preview()->colors(), 0x00ff0000u), "the preview plays the fields");
    // The preview repeats an effect with a duration.
    panel.advancePreview(2000);
    expect(allLeds(panel.preview()->colors(), 0x00ff0000u), "also past its duration");
    QToolButton* apply = nullptr;
    for (QToolButton* button : panel.findChildren<QToolButton*>()) {
        if (button->text() == QStringLiteral("Apply")) {
            apply = button;
        }
    }
    expect(apply && apply->isEnabled(), "Apply is offered");
    apply->click();
    expect(applied.size() == 1 && applied[0].colors[0] == 0x00ff0000u && applied[0].id == 2 &&
               applied[0].durationMs == 1500,
           "Apply reports the effect as the fields say");
    panel.findChild<QLineEdit*>()->setText(QString());
    panel.setColor(1, 0x00ffffffu);
    expect(!apply->isEnabled(), "an effect needs a name");
    panel.setEffect(green);
    expect(!panel.hasChanges() && panel.effect() == green, "Revert shows it again");
}

void testFrameSection()
{
    const document::ProjectDocument document = makeDocument();
    app::FrameLightsPanel panel;
    std::vector<std::pair<std::size_t, LightAction>> changes;
    panel.onChannelChanged = [&](std::size_t channel, const LightAction& action) {
        changes.emplace_back(channel, action);
    };
    panel.setFrame(&document, 1);
    expect(panel.channel(0)->action() == LightAction{LightActionKind::Effect, 1, 0} &&
               panel.channel(1)->action() == LightAction{LightActionKind::Color, 0, 0x000000ffu} &&
               panel.channel(2)->action().kind == LightActionKind::Unchanged && changes.empty(),
           "frame 1's channels are shown, without a report");
    auto* box = panel.channel(2)->findChild<QComboBox*>();
    box->setCurrentIndex(box->findText(QStringLiteral("Green")));
    expect(changes.size() == 1 && changes[0].first == 2 &&
               changes[0].second == LightAction{LightActionKind::Effect, 2, 0},
           "choosing an effect for channel 3 is reported");
    panel.channel(3)->setColor(0x00ff00ffu);
    expect(changes.size() == 2 && changes[1].first == 3 &&
               changes[1].second == LightAction{LightActionKind::Color, 0, 0x00ff00ffu},
           "a color for channel 4 too");
    panel.setFrame(&document, std::nullopt);
    expect(!panel.channel(0)->isEnabled(), "without a frame there is nothing to edit");
}

void testSpeakersBesideTheCanvas()
{
    const document::ProjectDocument document = makeDocument();
    QWidget parent;
    app::SpeakerLightPair speakers(&parent);
    app::SpeakerLightPreviewOptions options;
    options.visible = true;
    options.ledCount = 12;
    speakers.setOptions(options);
    speakers.showFrame(&document, 0);
    speakers.advance(50);
    expect(speakers.left()->colors().size() == 12 &&
               allLeds(speakers.left()->colors(), 0x0000ff00u) &&
               speakers.right()->colors()[0] == 0,
           "frame 0: the left speaker plays channel 1's default (green), the right is off");
    speakers.showFrame(&document, 1);
    speakers.advance(50);
    const std::vector<uint32_t> rainbow = speakers.left()->colors();
    expect(allLeds(speakers.right()->colors(), 0x000000ffu) && rainbow[0] != rainbow[6],
           "frame 1: the rainbow on the left, blue on the right");
    speakers.showFrame(&document, 2);
    speakers.advance(400);
    expect(speakers.left()->colors() != rainbow,
           "frame 2 changes nothing: the rainbow goes on moving");
    options.rightChannel = 0;
    options.shape = app::SpeakerShape::Rectangle;
    speakers.setOptions(options);
    speakers.showFrame(&document, 2);
    speakers.advance(10);
    const std::vector<uint32_t> right = speakers.right()->colors();
    expect(!allLeds(right, 0x000000ffu) && right[0] != right[6],
           "both speakers can show channel 1: the right one plays the rainbow too");
    // Centred on the frame it accompanies: in a tall view, near the top when asked for.
    app::SpeakerLightView view;
    view.resize(100, 400);
    view.setColors(std::vector<uint32_t>(12, 0x00ff0000u));
    const auto litRows = [&view]() {
        const QImage image = view.grab().toImage();
        int first = image.height();
        int last = -1;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                if (qRed(image.pixel(x, y)) > 200) {
                    first = std::min(first, y);
                    last = std::max(last, y);
                }
            }
        }
        return std::make_pair(first, last);
    };
    const auto middle = litRows();
    expect(middle.first > 120 && middle.second < 280, "without a centre: the view's middle");
    view.setCenterY(80.0);
    const auto top = litRows();
    expect(top.first < 40 && top.second < 160, "centred on y 80 near the top");
    view.setCenterY(10.0);
    expect(litRows().first >= 0 && litRows().second < 120,
           "kept inside the view when the centre is too close to the edge");
    options.visible = false;
    speakers.setOptions(options);
    expect(speakers.left()->isHidden(), "the preview can be off");
}

void testProjectDefaults()
{
    const document::ProjectDocument document = makeDocument();
    app::ProjectSettingsDialog dialog(document);
    expect(dialog.lightDefaults()[0] == LightAction{LightActionKind::Effect, 2, 0} &&
               dialog.lightDefaults()[1].kind == LightActionKind::Unchanged,
           "the defaults are shown, a channel without one as off");
    dialog.panel()->lightDefault(1)->setColor(0x00ffff00u);
    expect(dialog.lightDefaults()[1] == LightAction{LightActionKind::Color, 0, 0x00ffff00u},
           "a default color for channel 2");
}

void testFrameTester()
{
    // The preview's settings in a folder of the test's own.
    QTemporaryDir directory;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
    QCoreApplication::setOrganizationName(QStringLiteral("SpeakerLightWidgetTests"));
    const document::ProjectDocument document = makeDocument();
    app::SpeakerLightPreviewOptions options;
    options.visible = true;
    options.ledCount = 8;
    app::saveSpeakerLightPreviewOptions(options);
    app::FrameTesterDialog dialog(document, {0, 1, 2});
    dialog.show();
    QApplication::processEvents();
    app::SpeakerLightPair* speakers = dialog.speakerLights();
    speakers->advance(50);
    expect(speakers->left()->colors().size() == 8 && allLeds(speakers->left()->colors(), 0x0000ff00u),
           "the tester starts with libserum's default for channel 1");
    dialog.showPosition(1);
    speakers->advance(50);
    expect(allLeds(speakers->right()->colors(), 0x000000ffu) &&
               speakers->left()->colors()[0] != speakers->left()->colors()[4],
           "frame 1: libserum starts the rainbow and the blue");
    dialog.close();

    // The tester draws the canvas' pixels: dots at 50 % leave black between them.
    const auto blackPixels = [&](const app::DmdRenderOptions& options) {
        app::saveDmdRenderOptions(options);
        app::FrameTesterDialog tester(document, {0});
        tester.resize(900, 600);
        tester.show();
        QApplication::processEvents();
        const QImage image = tester.grab().toImage();
        int black = 0;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                black += (image.pixel(x, y) & 0xffffff) == 0 ? 1 : 0;
            }
        }
        tester.close();
        return black;
    };
    app::DmdRenderOptions squares;
    squares.pixelShape = app::DmdPixelShape::Square;
    squares.pixelCoverage = 1.0;
    app::DmdRenderOptions dots;
    dots.pixelShape = app::DmdPixelShape::Dot;
    dots.pixelCoverage = 0.5;
    expect(blackPixels(dots) > blackPixels(squares) + 1000,
           "the frame tester draws the pixel shape and size of the canvas");
}

}  // namespace

void runSpeakerLightWidgetTests()
{
    testEffectList();
    testEffectSection();
    testFrameSection();
    testSpeakersBesideTheCanvas();
    testProjectDefaults();
    testFrameTester();
}
