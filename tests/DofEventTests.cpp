// DOF events: the text the author types, the project file, and what libserum emits.

#include "document/DofEvents.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "RuntimeFixtures.h"
#include "document/ProjectDocument.h"
#include "edit/EditCommand.h"
#include "platform/Utf8Path.h"
#include "project/ProjectFile.h"
#include "runtime/RuntimePreview.h"

namespace {

using namespace colorizing;
using document::DofEvent;
using document::DofEvents;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: DOF events: " << message << '\n';
        std::exit(1);
    }
}

void testText()
{
    std::string wrong;
    const std::optional<DofEvents> events =
        document::parseDofEvents(" S5=255, l12=1;0  D33=-2;7 ", &wrong);
    expect(events && events->size() == 3, "three events, separated by spaces and commas");
    expect((*events)[0] == DofEvent{'S', 5, 255, std::nullopt} &&
               (*events)[1] == DofEvent{'L', 12, 1, 0} &&
               (*events)[2] == DofEvent{'D', 33, -2, 7},
           "type, number, value and turn-off value; a lowercase type taken as capital");
    expect(document::formatDofEvents(*events) == "S5=255 L12=1;0 D33=-2;7",
           "written back as the author types them");
    expect(document::parseDofEvents("")->empty(), "an empty field: no events");
    for (const char* text : {"S5", "S=1", "5=1", "S-5=1", "S5=x", "S5=1;", "S5=1;x", "S5=1=2",
                             "S5=99999999999"}) {
        wrong.clear();
        expect(!document::parseDofEvents(std::string("L1=1 ") + text, &wrong) && wrong == text,
               std::string("not an event, and named: ") + text);
    }
}

void testDocumentAndFile()
{
    document::ProjectDocument document = document::ProjectDocument::fromSnapshot(
        runtime_fixtures::makeProject(render::PlaneLayout::Ordinary, 2));
    const DofEvents events{{'S', 5, 255, std::nullopt}, {'L', 12, 1, 0}};
    edit::FrameDofEventsCommand command(1, {}, events);
    command.apply(document);
    expect(document.frameDofEvents(1) == events && document.frameDofEvents(0).empty(),
           "the frame has its events");
    command.revert(document);
    expect(document.frameDofEvents(1).empty(), "undone");
    command.apply(document);
    bool refused = false;
    try {
        document.setFrameDofEvents(0, {{'?', 1, 1, std::nullopt}});
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    expect(refused && document.frameDofEvents(0).empty(), "a type that is no letter is refused");

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("colorizing_dof_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".cdmd");
    expect(project::saveProjectFile(platform::utf8FromPath(path), document.toSnapshot(),
                                    {"test"})
               .ok,
           "the project is saved");
    const legacy::LegacyLoadResult loaded =
        project::loadProjectFile(platform::utf8FromPath(path));
    std::filesystem::remove(path);
    expect(loaded.ok && loaded.snapshot.frames[1].dofEvents == events &&
               loaded.snapshot.frames[0].dofEvents.empty(),
           "the project file keeps them, the turn-off value included");
}

// libserum emits a frame's events when it shows it, not again while it stays, and a turn-off
// value with the next frame unless that frame sets the element itself.
void testLibserumEmitsThem()
{
    document::ProjectDocument document = document::ProjectDocument::fromSnapshot(
        runtime_fixtures::makeProject(render::PlaneLayout::Ordinary, 4));
    document.setFrameDofEvents(0, {{'L', 12, 1, 0}, {'S', 5, 255, std::nullopt}});
    document.setFrameDofEvents(1, {{'D', 33, 7, 2}});
    document.setFrameDofEvents(2, {{'L', 12, 3, std::nullopt}});
    runtime::RuntimePreview& preview = runtime::RuntimePreview::instance();
    std::string error;
    expect(preview.openProject(document, runtime::PlaneRequest::Both, &error),
           "the project loads: " + error);
    const auto emitted = [&](std::size_t frame) {
        return document::formatDofEvents(preview.colorize(document.originalFrame(frame)).dofEvents);
    };
    expect(emitted(0) == "L12=1 S5=255", "frame 0 emits its events");
    expect(emitted(0).empty(), "not again while it stays");
    expect(emitted(1) == "L12=0 D33=7", "frame 1 turns L12 off, then its own");
    expect(emitted(2) == "D33=2 L12=3", "frame 2 turns D33 off; it sets L12 itself");
    expect(emitted(3).empty(), "frame 3 has none, and nothing to turn off");
    preview.close();
}

}  // namespace

void runDofEventTests()
{
    testText();
    testDocumentAndFile();
    testLibserumEmitsThem();
}
