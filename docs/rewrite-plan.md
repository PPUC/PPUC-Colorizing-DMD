# ColorizingDMD Qt Rewrite v2 Plan

This document is the review plan for a clean Qt rewrite of ColorizingDMD.
Implementation should not start until this plan is reviewed and accepted.

## Goals

- Build a platform-independent Qt editor that replaces the legacy Windows app.
- Keep the legacy `.cROM` and `.cRP` project formats as the editing source of truth.
- Treat SD and HD editing/rendering as first-class behavior from the first milestone.
- Use libserum for runtime-style previews so editor preview behavior stays close to real usage.
- Keep libserum runtime behavior unchanged; only add editor-facing APIs or helpers when they reduce duplication or risk in the app.
- Preserve compatibility with existing projects and the legacy binary layouts.

## Non-Goals For The Initial Rewrite

- Do not use `.cROMc` as the editable project format.
- Do not build a second cROM-based runtime preview path inside the editor.
- Do not port the `qt_playback_wip` `MainWindow` architecture forward.
- Do not change cROM/cRP/cROMc binary layouts.
- Do not add Qt, OpenCV, or app dependencies to libserum.

## First Release Constraints

- The first release of the new Qt app must not change the `.cROM` format or binary layout.
- Features that require changing record counts or reallocating `.cROM` sections, such as creating
  new backgrounds, creating new masks, or changing sprite table sizes, must be postponed or
  implemented only when they can reuse existing capacity without changing the file layout.
- Existing projects must remain editable as `.cROM/.cRP`; `.cROMc` remains a runtime/conversion
  artifact handled through libserum, not the editor save format.
- Missing first-release UI is acceptable only if loaded data is preserved exactly enough for
  existing projects to remain safe to save.

## Prioritized Missing Features

### Priority 1

1. Sprites:
   - Load/save sprite data from existing `.cROM` records without changing the format.
   - Sprite previews/list.
   - Sprite colorized/original display.
   - Sprite detection areas.
   - Frame sprite zones.
   - Crop/copy new sprites from original frames by selecting a shape or outline.
   - Sprite assignment/removal/reorder inside existing project capacity.
   - Sprite dynamic masks and colors.
   - Correct composed preview behavior with sprites.

2. Palette system:
   - Full/reduced palette views.
   - Integrate dynamic color editing with the palette workflow.
   - Drag/copy colors between frame colors, dynamic colors, and palette slots.
   - Palette gradients and gradient-fill workflows.
   - Palette undo/redo.
   - Preserve palette/rotation data even before every editor operation is exposed.

3. Shadows and dynamic shadows:
   - Preserve and expose existing shadow data from loaded `.cROM` projects.
   - Render shadows in editor previews without changing first-release `.cROM` layout.
   - Edit shadow and dynamic-shadow data only where the legacy format already supports it.
   - Keep runtime-preview validation libserum-backed once preview is implemented.

4. Frame metadata and trigger IDs:
   - Preserve loaded per-frame trigger IDs.
   - Expose trigger ID editing in the frame inspector.
   - Support the legacy "Switch to Monochrome" trigger behavior without changing `.cROM` layout.
   - Keep trigger list/rename/delete workflows format-compatible with existing project capacity.

5. Image import pipeline:
   - Import image data into existing frames/backgrounds.
   - Reimport/replace image data.
   - Scaling options, including selectable scalers for HD generation.
   - Avoid any operation that requires changing first-release `.cROM` layout.

### Priority 2

6. Background management:
   - Better background list/preview instead of numeric `BG ID` only.
   - Background editing on the canvas for existing background records.
   - Import/export/reimport existing background images.
   - Create/delete background records only after a format-compatible strategy exists.

7. Comparison mask management:
   - Better mask list/preview.
   - Improved mask assignment for selected frames.
   - Create/delete masks only after a format-compatible strategy exists.

8. HD management expansion:
   - Batch create/remove HD for selected frames.
   - Selectable scaling algorithms.
   - Apply equivalent HD workflows to existing backgrounds and sprites where format-safe.

### After Priority 2

9. Frame playback / sequence preview:
   - Basic playback controls.
   - Play selected frames or current filtered range.
   - Timing support, initially fixed-duration if needed.
   - Later: libserum-backed playback/preview path with in-memory/minimal cROM conversion.

### Priority 3

10. UI restructure:
   - Reduce crowded toolbar.
   - Move tools/settings into panels/tabs following the better ideas from `qt_playback_wip`.
   - Add component browsers for frames, masks, backgrounds, sprites, and palettes.

11. Save/autosave/recovery polish:
   - `Save As` exists.
   - Autosave and recovery are still missing.
   - Playback-related autosave should wait until playback is prioritized.

12. Imports and exports:
   - Video import should be implemented after still-image import is stable.
   - PNG export can stay as a useful convenience.
   - GIF/video export should wait until playback and timing are meaningful.

## Format Strategy

The editor loads and saves:

- `.cROM` for colorization/runtime project data.
- `.cRP` for original frames and editor metadata.

The editor ignores `.cROMc` for editing. When runtime preview or playback is required, the app should use libserum's existing ability to convert/load cROM data as it does outside the editor.

If `.cROMc` support is added later, it should be treated as import/export/runtime artifact support, not as the primary in-memory or saved editor format.

## libserum Strategy

Use `../libserum` directly. Do not use the stale `libserum -> ../libserum_concentrate` symlink from the WIP branch.

Allowed libserum work:

- Add new editor-facing functions.
- Add plain C/C++ data-view APIs for rendering and sprite matching.
- Add helpers that expose runtime decisions without duplicating runtime logic in the app.
- Add tests around new APIs.

Not allowed:

- Change existing runtime behavior.
- Add Qt/OpenCV dependencies to libserum.
- Change file format layout.
- Fork or duplicate the runtime rendering algorithm inside the app.

Candidate libserum editor APIs:

```cpp
struct SerumEditorProjectView;
struct SerumEditorFrameView;
struct SerumEditorSpriteMatch;

bool SerumEditor_RenderFramePreview(
    const SerumEditorProjectView* project,
    const SerumEditorFrameView* frame,
    bool useHd,
    uint16_t* outRgb565);

uint8_t SerumEditor_MatchSprites(
    const SerumEditorProjectView* project,
    const SerumEditorFrameView* frame,
    SerumEditorSpriteMatch* outMatches,
    uint8_t maxMatches);
```

Exact signatures should be designed after inspecting the current `../libserum` internals and adding tests.

## Architecture

Create a new Qt application structure. The new code should be cleanly separated from legacy Win32 UI code.

Proposed layout:

```text
qt_rewrite_v2/
  CMakeLists.txt
  app/
    main.cpp
    MainWindow.*
    actions/
    dialogs/
    widgets/
  core/
    document/
      ProjectDocument.*
      ProjectPaths.*
    legacy/
      LegacyCromReader.*
      LegacyCromWriter.*
      LegacyCrpReader.*
      LegacyCrpWriter.*
    model/
      FrameModel.*
      PaletteModel.*
      MaskModel.*
      DynamicMaskModel.*
      SpriteModel.*
      BackgroundModel.*
    render/
      CanvasScene.*
      CanvasLayout.*
      EditorFrameComposer.*
      RuntimePreviewService.*
    edit/
      EditCommand.*
      DrawingTools.*
      UndoStack.*
    test/
```

`MainWindow` should be a coordinator only. It should not own format logic, rendering algorithms, undo data, libserum conversion details, or raw project buffers.

## Domain Model

Create `ProjectDocument` as the single editable project model.

Responsibilities:

- Own the editable project state.
- Expose typed operations for frames, masks, dynamic masks, sprites, backgrounds, palettes, rotations, triggers, and metadata.
- Track dirty state.
- Provide undoable commands.
- Save through one legacy save path.

The model should preserve the legacy data relationships:

- One comparison mask assignment per frame.
- One dynamic mask map per frame/resolution.
- Dynamic colors are per-frame and per-resolution.
- Sprite dynamic masks and dynamic colors are per-sprite.
- Background masks are independent from comparison/dynamic mask modes.
- Sprite detection zones and sprite detection areas must map to the existing legacy fields.

## SD/HD Rules From Day One

SD and HD must be implemented together from the first usable milestone.

Rules:

- SD frames are 128x32.
- HD frames are 256x64.
- SD and HD must share the same editing abstractions.
- UI mode switches must not swap to a different rendering architecture.
- Coordinate mapping must be explicit and tested.
- HD absence must be represented explicitly, not inferred from widget state.
- All canvas overlays must know whether they target SD or HD.

Every feature added to SD must either support HD immediately or explicitly fail with a visible "not available for HD" state and a tracked follow-up. Silent SD-only behavior is not acceptable.

## Canvas And Rendering Plan

The central canvas should render a testable scene description, not own editing logic.

Introduce:

```cpp
enum class CanvasRegion {
    ColorizedFrame,
    OriginalFrame,
    HdFrame,
    SpriteColorized,
    SpriteOriginal,
    Background
};

enum class Resolution {
    Sd,
    Hd
};

struct CanvasHit {
    CanvasRegion region;
    Resolution resolution;
    int x;
    int y;
};
```

Canvas layers:

- Base image layer.
- Original/reference layer.
- HD layer when enabled and available.
- Mask overlay layer.
- Dynamic mask overlay layer.
- Background mask overlay layer.
- Sprite/zones overlay layer.
- Hover/edit preview layer.
- Border/outline layer.

Important design rule:

The canvas widget receives a `CanvasScene` and emits `CanvasHit` events. It must not know how to apply masks, dynamic colors, sprite matching, or project edits.

This avoids the WIP problem where stacking, scaling, outlines, mouse mapping, and edit behavior were mixed in one widget.

## Runtime Preview Plan

Runtime previews should use libserum, including its cROM to cROMc/runtime conversion behavior.

The editor should have one runtime-preview service:

```text
ProjectDocument -> temporary runtime project materialization -> libserum -> preview frame
```

Rules:

- Runtime preview must not duplicate libserum rendering.
- If libserum preview fails, show/log the error.
- Avoid an editor-only fallback renderer for runtime preview.
- The editor composer may still render edit layers for interaction, but playback/output preview must be libserum-backed.

## Feature Parity Required For Existing Project Testing

To use existing projects as meaningful test fixtures, the rewrite must support at least these project features:

- Frame colorization.
- Original/reference frames from `.cRP`.
- Comparison masks.
- Dynamic masks and dynamic color sets.
- SD and HD frames.
- Backgrounds and background masks.
- Sprites.
- Sprite detection zones and sprite detection areas.
- Sprite dynamic masks and colors.
- Color palettes and reduced palettes.
- Color rotations.
- Frame durations.
- Sections.
- Trigger IDs.
- Shape comparison mode.
- Dynamic shadow fields if present in project data.
- PupPack metadata preservation if present in project data.

The first implementation does not need to expose every editor UI immediately, but loading and saving existing projects must preserve unsupported fields.

## Save Strategy

There must be one save path for `.cROM/.cRP`:

```text
Save
Save As
Autosave later, if added
```

These operations should differ only by target paths and UI behavior.

Save rules:

- Flush all pending edits before serialization.
- Preserve unsupported metadata.
- Refuse to save when required original/reference frame data is missing.
- Round-trip a loaded project without edits as closely as possible.
- Add binary fixture tests before broad feature work.

## Testing Strategy

Testing should start before UI feature expansion.

Required test groups:

- cROM load/save round-trip.
- cRP load/save round-trip.
- ProjectDocument invariants.
- SD canvas layout.
- HD canvas layout.
- SD/HD mode switching.
- Canvas hit testing.
- Mask outline generation.
- Dynamic mask mapping.
- Background mask mapping.
- Sprite zone mapping.
- Runtime preview smoke tests through libserum.

Use existing real projects as fixtures, plus small synthetic fixtures that isolate one feature at a time.

## Milestones

### Milestone 0: Planning And Fixtures

- Review this plan.
- Select fixture projects.
- Document required feature coverage for each fixture.
- Decide branch/build layout.
- Add initial CMake target for `qt_rewrite_v2`.

### Milestone 1: Core Legacy Project Round-Trip

- Implement `.cROM` reader/writer wrappers.
- Implement `.cRP` reader/writer wrappers.
- Create `ProjectDocument`.
- Add fixture round-trip tests.
- Preserve all known legacy fields, including unsupported UI fields.

### Milestone 2: SD/HD Canvas Foundation

- Implement `CanvasScene`.
- Implement `CanvasLayout`.
- Implement SD/HD hit testing.
- Implement frame canvas with colorized/original/HD regions.
- Add tests for all coordinate transforms.

### Milestone 3: Frame Editing

- Implement frame list and selected frame view.
- Implement palette selection.
- Implement point, line, rectangle, filled rectangle, circle, filled circle, ellipse, filled ellipse, color picker, and magic fill.
- Implement gradient fill after palette gradients are available.
- Implement undo/redo for frame edits.
- Support SD and HD frame edits from the start.

### Milestone 4: Masks And Dynamic Masks

- Implement comparison mask assignment and editing.
- Implement dynamic mask assignment and editing.
- Render overlays and colored outlines as canvas layers.
- Ensure mask editing occurs on the original/reference region.
- Support SD and HD dynamic mask data.

### Milestone 5: Backgrounds

- Implement background list and assignment.
- Implement background canvas.
- Implement background editing for existing background records.
- Implement background masks.
- Support SD and HD backgrounds.
- Preserve the "all-black HD background means missing" behavior where required.

### Milestone 6: Sprites

- Implement sprite list and sprite canvas.
- Implement sprite original/colorized display.
- Implement sprite detection areas.
- Implement frame sprite zones.
- Implement crop/copy sprite creation from an original frame selection.
- Implement sprite dynamic masks and dynamic colors.
- Use libserum editor APIs for sprite matching/placement where possible.
- Support SD and HD sprites.

### Milestone 6b: Shadows

- Load/save existing shadow and dynamic-shadow data without changing `.cROM` structure.
- Render shadow layers in editor previews.
- Add editing controls only for shadow data that already exists in the legacy format.

### Milestone 7: Runtime Preview And Playback

- Build runtime preview service through libserum.
- Materialize temporary runtime data from the current `.cROM/.cRP` project.
- Use libserum for frame preview/playback.
- Add color rotation preview.
- Add playback controls after static runtime preview is reliable.

### Milestone 8: Legacy Workflow Completion

- Sections.
- Trigger IDs in the frame inspector.
- Trigger list, rename/delete, and per-frame trigger assignment.
- Switch-to-monochrome trigger behavior.
- Frame duration editor.
- Image import.
- Video import.
- GIF export.
- Selection helpers.
- Copy/paste tools.
- Advanced palette/rotation tooling, including palette gradients.
- Debug check action.
- Autosave, if still wanted.

## Review Questions

1. Should `qt_rewrite_v2` live as a top-level folder beside the legacy Win32 files, or should it become the main CMake/app structure while legacy code remains build-disabled?
2. Which existing projects should become mandatory fixtures?
3. Which fields are acceptable to preserve without UI in the first usable build?
4. Should runtime preview materialization write temporary files, or should we first add a libserum in-memory loading API?
5. Should libserum editor APIs be added before frame editing, or only when sprite/background/runtime preview work needs them?
