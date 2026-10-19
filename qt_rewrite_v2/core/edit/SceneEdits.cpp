#include "edit/SceneEdits.h"

#include <algorithm>
#include <stdexcept>

#include "edit/FrameMove.h"
#include "edit/StateCommands.h"
#include "edit/StructureCommand.h"
#include "runtime/Scenes.h"

namespace colorizing::edit {

namespace {

const EditTarget kStructureTarget{EditKind::ProjectStructure, 0, render::Resolution::Sd, {}};

// The colorization settings, before and after.
class SettingsCommand final : public EditCommand {
public:
    SettingsCommand(std::optional<document::ColorizationSettings> before,
                    std::optional<document::ColorizationSettings> after)
        : m_before(std::move(before)),
          m_after(std::move(after))
    {
    }

    void apply(document::ProjectDocument& document) const override
    {
        document.setColorizationSettings(m_after);
    }
    void revert(document::ProjectDocument& document) const override
    {
        document.setColorizationSettings(m_before);
    }
    [[nodiscard]] EditTarget target() const override { return kStructureTarget; }
    [[nodiscard]] std::vector<RecordReference> recordReferences() const override { return {}; }

private:
    std::optional<document::ColorizationSettings> m_before;
    std::optional<document::ColorizationSettings> m_after;
};

// What a scene frame is: its original, its link and its duration.
struct SceneFrameState {
    document::IndexedFrame original;
    std::optional<document::SceneFrameRef> scene;
    uint32_t durationMs = 0;
};

SceneFrameState sceneFrameState(const document::ProjectDocument& document, std::size_t frame)
{
    return {document.originalFrame(frame), document.sceneFrame(frame),
            document.frameDurationMs(frame)};
}

void setSceneFrameState(document::ProjectDocument& document,
                        std::size_t frame,
                        const SceneFrameState& state)
{
    document.setOriginalFrame(frame, state.original);
    document.setSceneFrame(frame, state.scene);
    document.setFrameDurationMs(frame, state.durationMs);
}

// Frames of a scene that stay frames of it, with another original, link or duration. Run in
// the structure history, against the numbering it was made for.
class SceneFramesCommand final : public EditCommand {
public:
    struct Change {
        std::size_t frame = 0;
        SceneFrameState before;
        SceneFrameState after;
    };

    explicit SceneFramesCommand(std::vector<Change> changes) : m_changes(std::move(changes)) {}

    void apply(document::ProjectDocument& document) const override
    {
        for (const Change& change : m_changes) {
            setSceneFrameState(document, change.frame, change.after);
        }
    }
    void revert(document::ProjectDocument& document) const override
    {
        for (auto change = m_changes.rbegin(); change != m_changes.rend(); ++change) {
            setSceneFrameState(document, change->frame, change->before);
        }
    }
    [[nodiscard]] EditTarget target() const override { return kStructureTarget; }
    [[nodiscard]] std::vector<RecordReference> recordReferences() const override { return {}; }

private:
    std::vector<Change> m_changes;
};

document::ColorizationSettings settingsOf(const document::ProjectDocument& document)
{
    return document.colorizationSettings().value_or(document::ColorizationSettings{});
}

const document::SceneDefinition& requireScene(const document::ProjectDocument& document,
                                              uint16_t sceneId)
{
    const auto& settings = document.colorizationSettings();
    const document::SceneDefinition* scene = settings ? settings->scene(sceneId) : nullptr;
    if (!scene) {
        throw std::out_of_range("the project has no scene " + std::to_string(sceneId));
    }
    return *scene;
}

uint8_t groupsOf(const document::SceneDefinition& scene)
{
    return scene.frameGroups == 0 ? 1 : scene.frameGroups;
}

bool hasFrame(const document::SceneDefinition& scene, const document::SceneFrameRef& frame)
{
    return frame.group >= 1 && frame.group <= groupsOf(scene) &&
           frame.frameIndex < scene.frameCount;
}

// Collects the commands of a change as they are made, each already applied; a failure takes
// back those made before it.
class Steps {
public:
    explicit Steps(document::ProjectDocument& document) : m_document(document) {}
    Steps(const Steps&) = delete;
    Steps& operator=(const Steps&) = delete;
    ~Steps()
    {
        while (!m_steps.empty()) {
            m_steps.back()->revert(m_document);
            m_steps.pop_back();
        }
    }

    void add(std::unique_ptr<EditCommand> command)
    {
        if (command) {
            m_steps.push_back(std::move(command));
        }
    }

    std::unique_ptr<SceneEditCommand> finish(std::string description)
    {
        if (m_steps.empty()) {
            return nullptr;
        }
        return std::make_unique<SceneEditCommand>(std::move(m_steps), std::move(description));
    }

private:
    document::ProjectDocument& m_document;
    std::vector<std::unique_ptr<EditCommand>> m_steps;
};

std::unique_ptr<EditCommand> settingsStep(document::ProjectDocument& document,
                                          document::ColorizationSettings settings)
{
    if (document.colorizationSettings() == settings) {
        return nullptr;
    }
    auto command =
        std::make_unique<SettingsCommand>(document.colorizationSettings(), std::move(settings));
    command->apply(document);
    return command;
}

// New frames for the scene's frames in `frames`, as addScene() makes them.
std::vector<document::FrameRecord> newSceneFrames(
    const document::ProjectDocument& document,
    const document::SceneDefinition& scene,
    const std::vector<document::SceneFrameRef>& frames)
{
    std::vector<document::FrameRecord> records;
    records.reserve(frames.size());
    for (const document::SceneFrameRef& frame : frames) {
        document::FrameRecord record = document.makeUncolorizedFrame(
            runtime::sceneMarkerFrame(scene, frame, document.colorCount()), scene.durationMs);
        record.scene = frame;
        records.push_back(std::move(record));
    }
    return records;
}

}  // namespace

SceneEditCommand::SceneEditCommand(std::vector<std::unique_ptr<EditCommand>> steps,
                                   std::string description)
    : m_steps(std::move(steps)),
      m_description(std::move(description))
{
    if (m_steps.empty()) {
        throw std::invalid_argument("a scene edit needs a step");
    }
}

void SceneEditCommand::apply(document::ProjectDocument& document) const
{
    std::size_t applied = 0;
    try {
        for (; applied < m_steps.size(); ++applied) {
            m_steps[applied]->apply(document);
        }
    } catch (...) {
        while (applied > 0) {
            m_steps[--applied]->revert(document);
        }
        throw;
    }
}

void SceneEditCommand::revert(document::ProjectDocument& document) const
{
    std::size_t remaining = m_steps.size();
    try {
        for (; remaining > 0; --remaining) {
            m_steps[remaining - 1]->revert(document);
        }
    } catch (...) {
        for (; remaining < m_steps.size(); ++remaining) {
            m_steps[remaining]->apply(document);
        }
        throw;
    }
}

EditTarget SceneEditCommand::target() const
{
    return kStructureTarget;
}

std::vector<ReplacedData> SceneEditCommand::replacedData() const
{
    std::vector<ReplacedData> replaced;
    for (const auto& step : m_steps) {
        const std::vector<ReplacedData> more = step->replacedData();
        replaced.insert(replaced.end(), more.begin(), more.end());
    }
    return replaced;
}

std::vector<RecordReference> SceneEditCommand::recordReferences() const
{
    return {};
}

std::vector<RenumberedRecords> SceneEditCommand::renumberedRecords() const
{
    std::vector<RenumberedRecords> renumbered;
    for (const auto& step : m_steps) {
        const std::vector<RenumberedRecords> more = step->renumberedRecords();
        renumbered.insert(renumbered.end(), more.begin(), more.end());
    }
    return renumbered;
}

std::size_t SceneEditCommand::heldBytes() const
{
    std::size_t bytes = 0;
    for (const auto& step : m_steps) {
        bytes += step->heldBytes();
    }
    return bytes;
}

const std::string& SceneEditCommand::description() const
{
    return m_description;
}

bool SceneEditCommand::changesFrames() const
{
    return std::any_of(m_steps.begin(), m_steps.end(), [](const auto& step) {
        return dynamic_cast<const StructureCommand*>(step.get()) ||
               dynamic_cast<const FrameMoveCommand*>(step.get()) ||
               dynamic_cast<const SceneFramesCommand*>(step.get());
    });
}

std::string sceneProblem(const document::ProjectDocument& document,
                         const document::SceneDefinition& scene,
                         std::optional<uint16_t> replacing)
{
    if (!runtime::supportsScenes(document.geometry())) {
        return "libserum plays scenes only in projects with 128x32 ROM frames";
    }
    if (scene.frameCount == 0) {
        return "a scene needs at least one frame";
    }
    if (scene.frameGroups == 0) {
        return "a scene needs at least one group";
    }
    const auto& settings = document.colorizationSettings();
    if (settings && (!replacing || *replacing != scene.sceneId) &&
        settings->scene(scene.sceneId)) {
        return "the project has a scene " + std::to_string(scene.sceneId) + " already";
    }
    return {};
}

std::vector<std::size_t> sceneFrames(const document::ProjectDocument& document, uint16_t sceneId)
{
    std::vector<std::size_t> frames;
    for (std::size_t frame = 0; frame < document.frameCount(); ++frame) {
        const auto& scene = document.sceneFrame(frame);
        if (scene && scene->sceneId == sceneId) {
            frames.push_back(frame);
        }
    }
    return frames;
}

std::unique_ptr<SceneEditCommand> setColorizationSettings(
    document::ProjectDocument& document,
    document::ColorizationSettings settings)
{
    Steps steps(document);
    steps.add(settingsStep(document, std::move(settings)));
    return steps.finish("change the scaling settings");
}

std::unique_ptr<SceneEditCommand> addScene(document::ProjectDocument& document,
                                           const document::SceneDefinition& scene)
{
    if (const std::string problem = sceneProblem(document, scene); !problem.empty()) {
        throw std::invalid_argument(problem);
    }
    std::vector<document::SceneFrameRef> frames;
    for (uint8_t group = 1; group <= groupsOf(scene); ++group) {
        for (uint16_t index = 0; index < scene.frameCount; ++index) {
            frames.push_back({scene.sceneId, group, index});
        }
    }
    std::vector<document::FrameRecord> records = newSceneFrames(document, scene, frames);
    Steps steps(document);
    steps.add(addFrames(document, std::move(records)));
    document::ColorizationSettings settings = settingsOf(document);
    settings.scenes.push_back(scene);
    steps.add(settingsStep(document, std::move(settings)));
    return steps.finish("add scene " + std::to_string(scene.sceneId));
}

std::unique_ptr<SceneEditCommand> changeScene(document::ProjectDocument& document,
                                              uint16_t sceneId,
                                              const document::SceneDefinition& scene)
{
    (void)requireScene(document, sceneId);
    if (const std::string problem = sceneProblem(document, scene, sceneId); !problem.empty()) {
        throw std::invalid_argument(problem);
    }
    Steps steps(document);

    // Frames that stay: a new original for another ID, the new duration.
    std::vector<SceneFramesCommand::Change> changes;
    std::vector<std::size_t> gone;
    std::vector<document::SceneFrameRef> present;
    for (std::size_t frame : sceneFrames(document, sceneId)) {
        document::SceneFrameRef ref = *document.sceneFrame(frame);
        ref.sceneId = scene.sceneId;
        if (!hasFrame(scene, ref)) {
            gone.push_back(frame);
            continue;
        }
        present.push_back(ref);
        SceneFrameState before = sceneFrameState(document, frame);
        SceneFrameState after = before;
        after.scene = ref;
        after.durationMs = scene.durationMs;
        if (scene.sceneId != sceneId) {
            after.original = runtime::sceneMarkerFrame(scene, ref, document.colorCount());
        }
        if (after.scene != before.scene || after.durationMs != before.durationMs ||
            scene.sceneId != sceneId) {
            changes.push_back({frame, std::move(before), std::move(after)});
        }
    }
    if (!changes.empty()) {
        auto command = std::make_unique<SceneFramesCommand>(std::move(changes));
        command->apply(document);
        steps.add(std::move(command));
    }
    steps.add(deleteFrames(document, gone));

    // Frames it has no frame for, behind its last frame.
    std::vector<document::SceneFrameRef> missing;
    for (uint8_t group = 1; group <= groupsOf(scene); ++group) {
        for (uint16_t index = 0; index < scene.frameCount; ++index) {
            const document::SceneFrameRef ref{scene.sceneId, group, index};
            if (std::find(present.begin(), present.end(), ref) == present.end()) {
                missing.push_back(ref);
            }
        }
    }
    if (!missing.empty()) {
        const std::vector<std::size_t> remaining = sceneFrames(document, scene.sceneId);
        const std::size_t firstNew = document.frameCount();
        steps.add(addFrames(document, newSceneFrames(document, scene, missing)));
        if (!remaining.empty()) {
            std::vector<std::size_t> added(missing.size());
            for (std::size_t index = 0; index < added.size(); ++index) {
                added[index] = firstNew + index;
            }
            steps.add(moveFrames(document, added, remaining.back() + 1));
        }
    }

    document::ColorizationSettings settings = settingsOf(document);
    for (document::SceneDefinition& definition : settings.scenes) {
        if (definition.sceneId == sceneId) {
            definition = scene;
            break;
        }
    }
    steps.add(settingsStep(document, std::move(settings)));
    return steps.finish("change scene " + std::to_string(scene.sceneId));
}

std::unique_ptr<SceneEditCommand> removeScene(document::ProjectDocument& document,
                                              uint16_t sceneId,
                                              bool deleteSceneFrames)
{
    (void)requireScene(document, sceneId);
    Steps steps(document);
    const std::vector<std::size_t> frames = sceneFrames(document, sceneId);
    if (deleteSceneFrames) {
        steps.add(deleteFrames(document, frames));
    } else if (!frames.empty()) {
        std::vector<SceneFramesCommand::Change> changes;
        for (std::size_t frame : frames) {
            SceneFrameState before = sceneFrameState(document, frame);
            SceneFrameState after = before;
            after.scene.reset();
            changes.push_back({frame, std::move(before), std::move(after)});
        }
        auto command = std::make_unique<SceneFramesCommand>(std::move(changes));
        command->apply(document);
        steps.add(std::move(command));
    }
    document::ColorizationSettings settings = settingsOf(document);
    settings.scenes.erase(std::remove_if(settings.scenes.begin(), settings.scenes.end(),
                                         [sceneId](const document::SceneDefinition& scene) {
                                             return scene.sceneId == sceneId;
                                         }),
                          settings.scenes.end());
    steps.add(settingsStep(document, std::move(settings)));
    return steps.finish("remove scene " + std::to_string(sceneId));
}

}  // namespace colorizing::edit
