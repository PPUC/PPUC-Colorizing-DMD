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
        AddScene,
        ChangeScene,
        RemoveScene,
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
// has, at least one frame and one group, and a project with a 128x32 ROM frame.
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

// The frames of the scene, ascending.
[[nodiscard]] std::vector<std::size_t> sceneFrames(const document::ProjectDocument& document,
                                                   uint16_t sceneId);

}  // namespace colorizing::edit
