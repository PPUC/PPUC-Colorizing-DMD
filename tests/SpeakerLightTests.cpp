// Speaker lights: the project's effects and the frames' actions, their edits, the .cRPX and
// cROM storage, libserum playing them, and the WS2812FX preview.

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include "RuntimeFixtures.h"
#include "edit/SpeakerLightEdits.h"
#include "legacy/LegacyProjectFileReader.h"
#include "legacy/LegacyProjectFileWriter.h"
#include "legacy/LegacyProjectSerializer.h"
#include "platform/Utf8Path.h"
#include "runtime/RuntimePreview.h"
#include "runtime/SpeakerLightPlayer.h"

namespace {

using namespace colorizing;
using document::LightAction;
using document::LightActionKind;
using document::LightActions;
using document::LightEffect;
using document::SpeakerLightSettings;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: speaker lights: " << message << '\n';
        std::exit(1);
    }
}

LightEffect makeEffect(uint16_t id, const std::string& name, uint8_t mode)
{
    LightEffect effect;
    effect.id = id;
    effect.name = name;
    effect.mode = mode;
    effect.speed = 800;
    effect.options = document::kLightReverse;
    effect.colors = {0x00ff0000u, 0x0000ff00u, 0xff000000u};
    effect.brightness = 200;
    return effect;
}

LightAction effectAction(uint16_t id)
{
    return LightAction{LightActionKind::Effect, id, 0};
}

LightAction colorAction(uint32_t color)
{
    return LightAction{LightActionKind::Color, 0, color};
}

// Three frames; effects 1 (rainbow) and 2 (larson scanner, 2 s); channel 1 defaults to 2.
// Frame 1 starts effect 1 on channel 1 and a static color on channel 2, frame 2 turns
// channel 1 off.
document::ProjectSnapshot makeProjectWithLights()
{
    document::ProjectSnapshot snapshot =
        runtime_fixtures::makeProject(render::PlaneLayout::Ordinary, 3);
    snapshot.speakerLights.effects = {makeEffect(1, "Rainbow", 12),
                                      makeEffect(2, "Gr\xc3\xbcn scanner", 13)};
    snapshot.speakerLights.effects[1].durationMs = 2000;
    snapshot.speakerLights.defaults[0] = effectAction(2);
    snapshot.frames[1].lights[0] = effectAction(1);
    snapshot.frames[1].lights[1] = colorAction(0x00123456u);
    snapshot.frames[2].lights[0] = LightAction{LightActionKind::Off, 0, 0};
    return snapshot;
}

void testModelAndEdits()
{
    document::ProjectDocument document =
        document::ProjectDocument::fromSnapshot(makeProjectWithLights());
    expect(document.speakerLights().effects.size() == 2 &&
               document.frameLights(1)[0] == effectAction(1) &&
               document.framesUsingLightEffect(1) == std::vector<std::size_t>{1},
           "the snapshot's lights are the document's");

    SpeakerLightSettings settings = document.speakerLights();
    settings.effects[0].speed = 400;
    const auto change = edit::setSpeakerLights(document, settings);
    expect(change && document.speakerLights().effects[0].speed == 400,
           "an effect is changed in one step");
    change->revert(document);
    expect(document.speakerLights().effects[0].speed == 800, "and back");
    change->apply(document);

    SpeakerLightSettings twice = document.speakerLights();
    twice.effects[1].id = 1;
    bool refused = false;
    try {
        (void)edit::setSpeakerLights(document, twice);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    expect(refused && document.speakerLights().effects[1].id == 2,
           "two effects with one ID are refused");
    SpeakerLightSettings without = document.speakerLights();
    without.effects.erase(without.effects.begin());
    refused = false;
    try {
        document.setSpeakerLights(without);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    expect(refused, "an effect a frame starts cannot simply go");
    refused = false;
    try {
        document.setFrameLights(0, LightActions{effectAction(9)});
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    expect(refused, "a frame cannot start an effect the project does not have");

    const auto removal = edit::removeLightEffect(document, 2);
    expect(removal && document.speakerLights().effects.size() == 1 &&
               document.speakerLights().defaults[0].kind == LightActionKind::Unchanged,
           "removing effect 2 clears the default that names it");
    removal->revert(document);
    expect(document.speakerLights().effects.size() == 2 &&
               document.speakerLights().defaults[0] == effectAction(2),
           "undo brings it back");
    const auto removeUsed = edit::removeLightEffect(document, 1);
    expect(document.frameLights(1)[0].kind == LightActionKind::Unchanged &&
               document.frameLights(1)[1] == colorAction(0x00123456u) &&
               removeUsed->target().frameIndices == std::vector<std::size_t>{1},
           "removing effect 1 leaves frame 1's channel 1 unchanged, its other channels as "
           "they are");
    removeUsed->revert(document);
    expect(document.frameLights(1)[0] == effectAction(1), "undo starts it there again");

    edit::FrameLightsCommand frame(0, document.frameLights(0),
                                   LightActions{colorAction(0x00ff0000u)});
    frame.apply(document);
    expect(document.frameLights(0)[0] == colorAction(0x00ff0000u) &&
               frame.target().kind == edit::EditKind::FrameAttributes,
           "a frame's lights are a frame setting");
    frame.revert(document);
    expect(document::changesNothing(document.frameLights(0)), "and back");

    expect(runtime::lightsShownAt(document, 0)[0] == effectAction(2) &&
               runtime::lightsShownAt(document, 1)[0] == effectAction(1) &&
               runtime::lightsShownAt(document, 2)[0].kind == LightActionKind::Off &&
               runtime::lightsShownAt(document, 2)[1] == colorAction(0x00123456u) &&
               runtime::lightsShownAt(document, 0)[1].kind == LightActionKind::Unchanged,
           "what the channels play at a frame: the last change up to it, else the default");
}

void testAmbientLighting()
{
    document::Rgb565Frame frame = document::makeRgb565Frame(render::Size{128, 32}, 0);
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 128; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * 128 + x;
            if (x < 8) {
                frame.pixels[index] = 0xf800;
            } else if (x >= 120 && y < 16) {
                frame.pixels[index] = 0x001f;
            }
        }
    }
    const LightActions suggested = edit::suggestedAmbientLighting(frame);
    expect(suggested[0] == colorAction(0x00ff0000u) && suggested[1] == colorAction(0x000000ffu) &&
               suggested[2].kind == LightActionKind::Unchanged,
           "left red, right blue: the lit pixels of the outer quarters");
    const LightActions dark =
        edit::suggestedAmbientLighting(document::makeRgb565Frame(render::Size{128, 32}, 0));
    expect(dark[0].kind == LightActionKind::Off && dark[1].kind == LightActionKind::Off,
           "a black frame turns them off");
}

// WS2812FX scales by brightness / 256, as on the hardware: full brightness shows 0xfe of 0xff.
bool near(uint32_t shown, uint32_t color)
{
    for (int shift : {0, 8, 16}) {
        const int a = static_cast<int>((shown >> shift) & 0xff);
        const int b = static_cast<int>((color >> shift) & 0xff);
        if (std::abs(a - b) > 2) {
            return false;
        }
    }
    return true;
}

void testPlayer()
{
    expect(runtime::lightModeCount() == 80 && runtime::lightModeName(0) == "Static" &&
               runtime::lightModeName(12) == "Rainbow Cycle" && runtime::lightModeName(200).empty(),
           "the WS2812FX modes and their names");

    runtime::SpeakerLightPlayer player(8);
    LightEffect color;
    color.mode = document::kLightModeStatic;
    color.colors = {0x00ff8000u, 0, 0};
    player.play(color, 1000);
    std::vector<uint32_t> leds = player.colors();
    expect(leds.size() == 8 && near(leds[0], 0x00ff8000u) && near(leds[7], 0x00ff8000u),
           "a static color on every LED");
    color.colors[0] = 0xff000000u;
    player.play(color, 1000);
    expect(near(player.colors()[3], 0x00ffffffu), "the white of an RGBW strip shows as white");
    color.colors[0] = 0x00ff0000u;
    color.brightness = 128;
    player.play(color, 1000);
    const uint32_t dimmed = (player.colors()[0] >> 16) & 0xff;
    expect(dimmed > 100 && dimmed < 140, "the brightness scales it");

    LightEffect blue;
    blue.mode = document::kLightModeStatic;
    blue.colors = {0x000000ffu, 0, 0};
    player.setDefault(blue);
    LightEffect red;
    red.mode = document::kLightModeStatic;
    red.colors = {0x00ff0000u, 0, 0};
    red.durationMs = 500;
    player.play(red, 2000);
    player.update(2400);
    expect(near(player.colors()[0], 0x00ff0000u), "an effect plays for its duration");
    player.update(2500);
    expect(near(player.colors()[0], 0x000000ffu) && player.playing() == blue,
           "and then the default");

    LightEffect rainbow;
    rainbow.mode = document::kLightModeRainbowCycle;
    rainbow.speed = 1000;
    player.play(rainbow, 5000);
    player.update(5050);
    const std::vector<uint32_t> before = player.colors();
    player.update(5500);
    expect(before != player.colors() && before[0] != before[4],
           "a rainbow cycle moves along the strip and over time");
    player.setLedCount(24);
    expect(player.ledCount() == 24 && player.colors().size() == 24 &&
               player.playing() == rainbow,
           "the LED count changes, the effect stays");
}

// Migrating a project: ambient lighting for every frame without lights, one step.
void testAmbientLightingForAll()
{
    document::ProjectDocument document =
        document::ProjectDocument::fromSnapshot(makeProjectWithLights());
    expect(edit::framesWithoutLights(document) == std::vector<std::size_t>{0},
           "only frame 0 does nothing to the lights");
    std::vector<std::size_t> asked;
    const auto colorsOf = [&](std::size_t frameIndex) {
        asked.push_back(frameIndex);
        document::Rgb565Frame frame = document::makeRgb565Frame(render::Size{128, 32}, 0);
        for (int y = 0; y < 32; ++y) {
            frame.pixels[static_cast<std::size_t>(y) * 128] = 0x07e0;
        }
        return frame;
    };
    const LightActions frame1 = document.frameLights(1);
    const auto command = edit::addAmbientLightingToFramesWithoutLights(document, colorsOf);
    expect(command && asked == std::vector<std::size_t>{0} &&
               document.frameLights(0)[0] == colorAction(0x0000ff00u) &&
               document.frameLights(0)[1].kind == LightActionKind::Off &&
               document.frameLights(0)[2].kind == LightActionKind::Unchanged &&
               document.frameLights(1) == frame1,
           "frame 0 gets green on the left and off on the right, frame 1 keeps its lights");
    expect(!edit::addAmbientLightingToFramesWithoutLights(document, colorsOf),
           "a second run finds no frame without lights");
    command->revert(document);
    expect(document::changesNothing(document.frameLights(0)), "undo takes it back in one step");
}

std::filesystem::path tempPath(const std::string& suffix)
{
    static int counter = 0;
    static const std::string run =
        std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    return std::filesystem::temp_directory_path() /
           ("colorizing_qt_v2_lights_" + run + "_" + std::to_string(counter++) + suffix);
}

std::vector<uint8_t> readFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
}

void testFiles()
{
    const document::ProjectSnapshot snapshot = makeProjectWithLights();
    const std::filesystem::path cromPath = tempPath(".cROM");
    const std::filesystem::path crpxPath = tempPath(".cRPX");
    const legacy::LegacyProjectPaths paths{platform::utf8FromPath(cromPath),
                                           platform::utf8FromPath(crpxPath)};
    expect(legacy::LegacyProjectFileWriter().save(paths, snapshot).ok,
           "a project with speaker lights is saved");
    const legacy::LegacyLoadResult loaded = legacy::LegacyProjectFileReader().load(paths);
    expect(loaded.ok, "and loads: " + loaded.error.message);
    expect(loaded.snapshot.speakerLights == snapshot.speakerLights &&
               loaded.snapshot.frames[1].lights == snapshot.frames[1].lights &&
               loaded.snapshot.frames[2].lights == snapshot.frames[2].lights &&
               document::changesNothing(loaded.snapshot.frames[0].lights),
           "with its effects, names, defaults and frames (chunk LGHT)");

    // The cROM's settings block: the lights alone, without colorization settings.
    const std::vector<uint8_t> crom = readFile(cromPath);
    const std::vector<uint8_t> block = legacy::cromSettingsBlock(snapshot);
    expect(!block.empty() && block.size() < crom.size() &&
               std::equal(block.begin(), block.end(),
                          crom.end() - static_cast<std::ptrdiff_t>(block.size())) &&
               block[8] == legacy::kCromSettingsLights,
           "the cROM ends with a settings block of the lights");
    document::ProjectSnapshot plain = runtime_fixtures::makeProject(render::PlaneLayout::Ordinary, 3);
    expect(legacy::cromSettingsBlock(plain).empty(),
           "a project without lights or settings writes no block, as before");
    std::filesystem::remove(cromPath);
    std::filesystem::remove(crpxPath);
}

// libserum takes the lights from the cROM: the defaults, and the effects a frame starts.
void testLibserumPlaysThem()
{
    const document::ProjectDocument document =
        document::ProjectDocument::fromSnapshot(makeProjectWithLights());
    runtime::RuntimePreview& preview = runtime::RuntimePreview::instance();
    std::string error;
    expect(preview.openProject(document, runtime::PlaneRequest::Rows32, &error),
           "the project loads: " + error);
    const auto defaults = preview.defaultLights();
    expect(defaults[0] && defaults[0]->mode == 13 && defaults[0]->durationMs == 2000 &&
               defaults[1] && defaults[1]->mode == document::kLightModeStatic &&
               defaults[1]->colors[0] == 0,
           "the defaults: effect 2 on channel 1, the others off");
    runtime::RuntimeFrame frame = preview.colorize(document.originalFrame(0));
    expect(!frame.lights[0] && !frame.lights[1], "frame 0 changes nothing");
    frame = preview.colorize(document.originalFrame(1));
    expect(frame.identified && frame.lights[0] && frame.lights[0]->mode == 12 &&
               frame.lights[0]->speed == 800 &&
               frame.lights[0]->options == document::kLightReverse &&
               frame.lights[0]->brightness == 200 && frame.lights[0]->colors[2] == 0xff000000u &&
               frame.lights[1] && frame.lights[1]->colors[0] == 0x00123456u && !frame.lights[2],
           "frame 1 starts the rainbow on channel 1 and the color on channel 2");
    frame = preview.colorize(document.originalFrame(2));
    expect(frame.lights[0] && frame.lights[0]->colors[0] == 0 && !frame.lights[1],
           "frame 2 turns channel 1 off");
    preview.close();
}

}  // namespace

void runSpeakerLightTests()
{
    testModelAndEdits();
    testAmbientLighting();
    testAmbientLightingForAll();
    testPlayer();
    testFiles();
    testLibserumPlaysThem();
}
