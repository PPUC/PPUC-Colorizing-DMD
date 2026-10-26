#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "document/ProjectDocument.h"
#include "document/Scene.h"
#include "edit/EditCommand.h"

namespace colorizing::edit {

// A change of the colorization settings (document/Scene.h) with the frames it needs: adding,
// changing or removing a scene, or the scaling settings. One step in the structure history
// ({ProjectStructure, 0, Sd}), undone in order with the additions, deletions and moves of
// frames, which it may contain.
class SceneEditCommand final : public EditCommand {
public:
    enum class Kind {
        Scaling,
        // The libserum target (document/LibserumTarget.h), maybe with the scaling settings.
        LibserumTarget,
        AddScene,
        ChangeScene,
        RemoveScene,
        // Frames deleted from the strip, their scenes made smaller (deleteFramesOfScenes()).
        DeleteSceneFrames,
    };

    // sceneId: the scene added, changed (its new ID) or removed.
    SceneEditCommand(std::vector<std::unique_ptr<EditCommand>> steps,
                     Kind kind,
                     uint16_t sceneId = 0);

    void apply(document::ProjectDocument& document) const override;
    void revert(document::ProjectDocument& document) const override;
    [[nodiscard]] EditTarget target() const override;
    [[nodiscard]] std::vector<ReplacedData> replacedData() const override;
    [[nodiscard]] std::vector<RecordReference> recordReferences() const override;
    [[nodiscard]] std::vector<RenumberedRecords> renumberedRecords() const override;
    [[nodiscard]] std::size_t heldBytes() const override;

    [[nodiscard]] Kind kind() const;
    [[nodiscard]] uint16_t sceneId() const;
    // Whether frames are added, deleted or moved.
    [[nodiscard]] bool changesFrames() const;

private:
    std::vector<std::unique_ptr<EditCommand>> m_steps;
    Kind m_kind;
    uint16_t m_sceneId;
};

// Why the scene cannot be the project's scene `replacing` (the ID of the scene it replaces,
// nothing for a new one); empty if it can. A scene needs an ID no other scene of the project
// has, at least one frame and one group, and a project with a 128x32 ROM frame. A new ID
// (a new scene, or another ID for a scene) is one of kFirstSceneId to kLastSceneId; a scene
// keeps the ID it has, also one outside them.
[[nodiscard]] std::string sceneProblem(const document::ProjectDocument& document,
                                       const document::SceneDefinition& scene,
                                       std::optional<uint16_t> replacing = std::nullopt);

// Each runs the change on the document and returns the command, already applied; nullptr
// when nothing changes. When it fails the document is left as it was and the exception is
// passed on: std::invalid_argument for a scene sceneProblem() objects to, std::out_of_range
// for a scene the project does not have.
//
// The settings as they are, e.g. the scaling settings. A project without settings gets them.
[[nodiscard]] std::unique_ptr<SceneEditCommand> setColorizationSettings(
    document::ProjectDocument& document,
    document::ColorizationSettings settings);
// The project settings: the colorization settings and the libserum target, in one step.
// Throws std::invalid_argument when the project does not fit the target
// (ProjectDocument::libserumTargetProblems()).
[[nodiscard]] std::unique_ptr<SceneEditCommand> setProjectSettings(
    document::ProjectDocument& document,
    document::ColorizationSettings settings,
    document::LibserumTarget target);
// Adds the scene with a frame for each of its frames in each group, behind the last frame:
// the scene marker as the original, uncolorized, lasting the scene's frame duration.
[[nodiscard]] std::unique_ptr<SceneEditCommand> addScene(document::ProjectDocument& document,
                                                         const document::SceneDefinition& scene);
// Replaces the scene sceneId with `scene`. Its frames follow: with another ID they get the new
// markers as originals, frames of groups or frame numbers it no longer has are deleted, and
// those it has no frame for (also ones deleted from the strip before) are made as addScene()
// makes them, behind the scene's last frame. A new frame duration is given to its frames.
[[nodiscard]] std::unique_ptr<SceneEditCommand> changeScene(document::ProjectDocument& document,
                                                            uint16_t sceneId,
                                                            const document::SceneDefinition& scene);
// Removes the scene; with deleteFrames its frames go too, otherwise they stay as ordinary
// frames.
[[nodiscard]] std::unique_ptr<SceneEditCommand> removeScene(document::ProjectDocument& document,
                                                            uint16_t sceneId,
                                                            bool deleteFrames);

// Deletes the frames (any set, in any order). A scene some of them belong to gets smaller, so
// that its frames stay numbered without a gap (libserum identifies a scene frame by the group
// and frame number of its marker): the frames behind a deleted one take the numbers before
// them, with their markers as originals, and the scene has that many frames (or groups) less.
// All groups of a scene have the same number of frames, so of a scene with several groups the
// same frame numbers go in each (a frame a group lacks counts as deleted there), or whole
// groups. Throws std::invalid_argument when the frames do not allow that, or when a scene would
// have no frame left (Remove the scene instead); nothing changes then. Frames of no scene are
// deleted as deleteFrames() does. nullptr for no frames.
[[nodiscard]] std::unique_ptr<SceneEditCommand> deleteFramesOfScenes(
    document::ProjectDocument& document,
    const std::vector<std::size_t>& frames);

// The lowest ID from kFirstSceneId to kLastSceneId no scene of the project has; nothing when
// all are taken.
[[nodiscard]] std::optional<uint16_t> nextFreeSceneId(const document::ProjectDocument& document);

// The frames of the scene, ascending.
// Whether the frame starts the scene: a frame of the game (not one of a scene) whose trigger
// is the scene's ID -- also for a background scene, which the same trigger keeps playing.
[[nodiscard]] bool startsScene(const document::ProjectDocument& document,
                               std::size_t frameIndex,
                               uint16_t sceneId);
[[nodiscard]] std::vector<std::size_t> sceneFrames(const document::ProjectDocument& document,
                                                   uint16_t sceneId);
// The scene the frame uses: the one it is a frame of, or else the one it starts (startsScene(),
// a scene the project has); nothing for neither. "Used by the current frame" lists it.
[[nodiscard]] std::optional<uint16_t> sceneUsedByFrame(const document::ProjectDocument& document,
                                                       std::size_t frameIndex);

}  // namespace colorizing::edit
