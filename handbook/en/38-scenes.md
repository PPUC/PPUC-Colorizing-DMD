# Scenes and Scaling

A scene is a short sequence libserum plays by itself when a PUP trigger fires: an animation in
front of the game's frames, or behind them as a background. Each frame of a scene is a frame
of the project. Its original is not a ROM frame but a marker libserum generates for it
("PUP SCENE ID", "FRAME GROUP", "FRAME NUMBER" with the numbers), and libserum identifies the
frame by that marker while the scene plays. The frame is colorized like any other.

The project keeps its scenes, together with libserum's scaling settings, and hands them to
libserum with the colorization, in the `.cROMc` (in a settings block at the end of its cROM).
So a colorization needs no `<rom>.pup.csv` and no `scaling.txt` any more: libserum takes the
scenes and scaling settings from the colorization and ignores those files beside it.
The project file keeps the scenes and which frame is which frame of which scene.

## The Scenes section

The `Scenes` section of the left sidebar, below the sprites, lists the project's scenes: a thumbnail
of the scene's first frame in the project (with the blue `HD` badge when that frame has HD data),
and below it the scene ID, the groups and frames, the frame duration, `background` for a background
scene and how many of its frames are missing in the project. The scenes are listed by their IDs.

- `New` opens the scene window for a new scene and adds a frame for each of its frames behind
  the last frame of the project, group by group: uncolorized, with the marker as the original,
  lasting the scene's frame duration.
- A double click on a scene sets the frame filter to `Frames of scene ...`, an entry for that
  scene, so the strip lists its frames, and shows its first frame on the frame canvas; the Inspector's `Scene` section then has its settings. The
  toolbar's `<` goes back to the frame and the timeline before (see "Canvas"), `>` into the
  scene again. `Leave Scene`, below the frame filter while a scene's frames are listed, goes
  back to the frames before in one step, however many of the scene's frames were visited.
- A scene dragged from the list onto the canvas becomes the trigger of the selected frames, as
  the `Scene` field of the Inspector's `Frame` section sets it: each of them starts the scene
  (a background scene then plays behind the frames that follow, where their background masks
  let it through). Frames with a trigger of another kind (a PUP trigger, a monochrome
  fallback) are left as they are. One undo step, as with the field.
- `Delete` removes the selected scene. The question offers to delete its frames too or to keep
  them as ordinary frames.
- `Used by the current frame` above the left sidebar's lists shows only the scene the current
  frame uses: the scene it is a frame of, or the scene it starts by its trigger (the `Scene` of
  the `Frame` section); none for a frame of neither.
- `Not used by any frame` there lists the scenes no frame of the game starts.

The `Scene` section of the Inspector (the right sidebar) shows the settings of the scene the
frame on the canvas belongs to, the same fields as the scene window. It is there only while a
frame of a scene is on the frame canvas (not while a background or a sprite is edited), opens
when it comes (the Inspector hides the comparison mask, sprite and dynamic sections meanwhile),
and
follows the frame once it has stayed a quarter of a second. Changes wait for `Apply`; `Revert`
shows the scene's settings again. Unapplied changes stay while frames of the same scene are
shown and go when a frame of another scene comes. With `Apply` the scene's frames follow:

- frames the scene gains (more frames or groups) are added behind its last frame;
- frames it no longer has are deleted, after a question, with their colorization;
- with another scene ID its frames get the new markers as originals and keep their
  colorization;
- a new frame duration is given to all its frames;
- a frame of the scene deleted from the strip before is added again. Applying the settings
  after changing a field and changing it back therefore brings back missing frames.

Every change is one step of `Undo` and `Redo`, in order with adding, deleting and moving frames.

Scenes need a project with 128x32 ROM frames: libserum plays them in no other. In other projects
the section says so and offers no `New`.

## Deleting frames of a scene

libserum identifies a scene frame by the group and frame number of its marker, and plays a
scene's frames by number: the numbers of a scene must have no gap. Deleting frames of a scene
from the strip (`Delete Frames`, `Delete` in the strip, or any other deletion of frames)
therefore makes the scene smaller: it has that many frames less, and each frame behind a
deleted one takes the number before it, with the new marker as its original; its colorization
stays. The question before the deletion names the scenes that get fewer frames.

All groups of a scene have the same number of frames. In a scene with several groups the same
frame number goes in every group (select it in each), or whole groups go, and the groups behind
them are renumbered. Anything else is refused with a message, and nothing is deleted. A scene
keeps at least one frame: deleting all its frames is refused; delete the scene in the `Scenes`
section instead. The deletion is one step of `Undo`, which brings back the frames, the numbers
and the markers.

## The scene window

What one line of a `<rom>.pup.csv` says:

| Field | Meaning |
| --- | --- |
| Scene ID (PUP trigger) | The trigger that starts the scene. Each scene has its own. A new scene, or a scene given another ID, takes one of 50000 to 61999, the trigger range kept for scenes; `New` suggests the lowest free one. A scene of an imported `pup.csv` keeps the ID it has. |
| Frames | Frames of each group. |
| Groups | Sets of frames the scene can show; each time it plays it shows one group. |
| Pick the group at random | Otherwise the groups play in turn. |
| Frame duration | How long each frame shows; 33 ms for a new scene (about 30 frames a second). |
| Plays | Once, in a loop, or several times. |
| Can be interrupted | A frame of the game identified while the scene plays ends it. |
| Start immediately | The first frame shows when the trigger fires, without waiting for the next frame of the game. |
| When it has played | Keep its last frame, black, or the frame shown before the scene. |
| Hold the last frame for / Start by itself after | For a scene that cannot be interrupted and keeps its last frame: how long that frame stays. Otherwise: the scene starts by itself after this many seconds without a new frame. `Off` for neither. |
| Play as a background | The scene shows behind the frames of the game, where their background mask lets it through. The frame that starts it needs no background of its own; the canvas shows the scene's first frame through its mask (see "Backgrounds"). |
| Keep only dynamic content in front | Of the game's frames only dynamic zones, sprites and shadows show over the scene. |
| Black dynamic pixels show the scene | With a background scene: black pixels of dynamic zones let the scene through and cast no shadow. |
| Resume when triggered again | A trigger soon after the scene stopped continues it instead of starting it over. |

The window says how many frames the scene has in the project and why it cannot be taken (an ID
another scene has), and `OK` waits until it can.

## What a frame of a scene can have

A frame of a scene is the scene's picture. libserum renders it without looking for sprites and
starts nothing by its trigger, and a comparison mask, a shape mode or dynamic zones would only
act on its original, the scene marker. So a frame of a scene has none of these:

- Assigning sprites leaves frames of scenes out (the status bar says how many), `Capture` refuses
  one, and the canvas draws no sprite on it.
- Dynamic zones cannot be painted on it; the status bar says why.
- A comparison mask, the shape mode and a trigger are not set on it; a change on several frames
  leaves it out.
- Opening a project takes all of them off its frames of scenes, with a note in the status bar;
  the project then counts as changed.

Its background, color rotations and speaker lights stay: libserum renders them on a scene's
frames (one background image for all frames of a scene, colors that rotate, lights that follow
the scene).

## The frame strip

A frame of a scene says so below its thumbnail: `Scene 1001 (3)` for its third frame, with
groups `Scene 1001 (2.3)` for the third frame of group 2. The frame filter has, for a project
with scenes, `Scene frames` and `Without scene frames` (the frames of the game alone). The frames
of one scene are listed by a double click on it (`Frames of scene ...`); `Using the selected
asset` with a scene selected in the `Scenes` section lists the frames of the game that start it,
by their trigger, also as a background scene.

## Scaling

`File -> Project Settings...` opens the settings of the whole colorization: the scaling
settings and the speaker lights' defaults (see "Speaker Lights"; a change of them is an undo step
of its own). `Upscaling` is how libserum makes the 256x64 picture of a frame that has none: `Scale2x`
(libserum's default) or `Line doubling`. `Upscaled shadow offset` is how far a dynamic shadow falls on
the 256x64 plane: 1 pixel (native, the default) or 2 pixels (proportional, as thick as on the
128x32 plane). These are what a `scaling.txt` used to set. `OK` applies them as one undo step, `Cancel`
leaves the project as it was.

## Projects of the Win32 editor

The Win32 editor knows no scenes: their settings were a `<rom>.pup.csv` beside the project, the
frames a dump of libdmdutil's scene tool imported as frames. When a project of the Win32 editor
(or one saved by this editor before it kept scenes) is opened, the editor takes the
`<rom>.pup.csv` and `scaling.txt` beside its `.cROM` into the project, reads them as libserum
does, and links every frame whose original is a scene marker to its scene. The status bar says
what was taken. The files are left as they are; once the project is saved they are no longer
needed (and libserum ignores them). A project without these files gets no scenes and libserum's
default scaling.
