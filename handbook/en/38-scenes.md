# Scenes and Scaling

A scene is a short sequence libserum plays by itself when a PUP trigger fires: an animation in
front of the game's frames, or behind them as a background. Each frame of a scene is a frame
of the project. Its original is not a ROM frame but a marker libserum generates for it
("PUP SCENE ID", "FRAME GROUP", "FRAME NUMBER" with the numbers), and libserum identifies the
frame by that marker while the scene plays. The frame is colorized like any other.

The project keeps its scenes, together with libserum's scaling settings, and writes them into
the `.cROM` it saves (in a settings block at its end, which older libserum versions do not
read). So a colorization needs no `<rom>.pup.csv` and no `scaling.txt` any more: libserum
2.8.2 and later take the scenes and scaling settings from the colorization and ignore those
files beside it. The `.cRPX` keeps the scenes and which frame is which frame of which scene.

## The Scenes section

The `Scenes` section of the left sidebar, below the sprites, lists the project's scenes: a thumbnail of the scene's first frame
in the project, and below it the scene ID, the groups and frames, the frame duration,
`background` for a background scene and how many of its frames are missing in the project.

- `Add` opens the scene window for a new scene and adds a frame for each of its frames behind
  the last frame of the project, group by group: uncolorized, with the marker as the original,
  lasting the scene's frame duration.
- `Edit` (or a double click) changes the selected scene. Its frames follow:
  - frames the scene gains (more frames or groups) are added behind its last frame;
  - frames it no longer has are deleted, after a question, with their colorization;
  - with another scene ID its frames get the new markers as originals and keep their
    colorization;
  - a new frame duration is given to all its frames;
  - a frame of the scene deleted from the strip before is added again. Editing a scene and
    pressing `OK` without changes therefore brings back missing frames.
- `Delete` removes the selected scene. The question offers to delete its frames too or to keep
  them as ordinary frames.
- `Show Frames` sets the frame filter to `Using the selected asset` with the selected scene:
  the strip lists its frames, and selecting another scene lists that one's.
- `Used by the current frame` above the left sidebar's lists shows only the scene of the
  current frame (none for a frame of no scene).

The `Scene` section of the left sidebar, below `Scenes`, shows the settings of the scene the
frame on the canvas belongs to, the same fields as the scene window. It is there only while a
frame of a scene is on the frame canvas (not while a background or a sprite is edited), and
follows the frame once it has stayed a quarter of a second. Changes wait for `Apply`, which
changes the scene as `Edit` does, frames following and with the same question when frames go;
`Revert` shows the scene's settings again. Unapplied changes stay while frames of the same scene
are shown and go when a frame of another scene comes.

Every change is one step of `Undo` and `Redo`, in order with adding, deleting and moving frames.

Scenes need a project with 128x32 ROM frames: libserum plays them in no other. In other projects
the section says so and offers no `Add`.

## The scene window

What one line of a `<rom>.pup.csv` says:

| Field | Meaning |
| --- | --- |
| Scene ID (PUP trigger) | The trigger that starts the scene. Each scene has its own. A new scene, or a scene given another ID, takes one of 50000 to 61999, the trigger range kept for scenes; `Add` suggests the lowest free one. A scene of an imported `pup.csv` keeps the ID it has. |
| Frames | Frames of each group. |
| Groups | Sets of frames the scene can show; each time it plays it shows one group. |
| Pick the group at random | Otherwise the groups play in turn. |
| Frame duration | How long each frame shows; 33 ms for a new scene (about 30 frames a second). |
| Plays | Once, in a loop, or several times. |
| Can be interrupted | A frame of the game identified while the scene plays ends it. |
| Start immediately | The first frame shows when the trigger fires, without waiting for the next frame of the game. |
| When it has played | Keep its last frame, black, or the frame shown before the scene. |
| Hold the last frame for / Start by itself after | For a scene that cannot be interrupted and keeps its last frame: how long that frame stays. Otherwise: the scene starts by itself after this many seconds without a new frame. `Off` for neither. |
| Play as a background | The scene shows behind the frames of the game, where their background mask lets it through. |
| Keep only dynamic content in front | Of the game's frames only dynamic zones, sprites and shadows show over the scene. |
| Black dynamic pixels show the scene | With a background scene: black pixels of dynamic zones let the scene through and cast no shadow. |
| Resume when triggered again | A trigger soon after the scene stopped continues it instead of starting it over. |

The window says how many frames the scene has in the project and why it cannot be taken (an ID
another scene has), and `OK` waits until it can.

## The frame strip

A frame of a scene says so below its thumbnail: `Scene 1001 (3)` for its third frame, with
groups `Scene 1001 (2.3)` for the third frame of group 2. The frame filter has, for a project
with scenes, `Scene frames` and `Without scene frames` (the frames of the game alone). The frames
of one scene are `Using the selected asset` with the scene selected in the `Scenes` section.

## Scaling

`File -> Project Settings...` opens the settings of the whole colorization. `Upscaling` is how libserum makes the 256x64 picture of a frame that has none: `Scale2x`
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
