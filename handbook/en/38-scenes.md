# Scenes and Scaling

A scene is a short sequence libserum plays by itself when a PUP trigger fires: an animation in
front of the game's frames, or behind them as a background. Each frame of a scene is a frame
of the project. Its original is not a ROM frame but a marker libserum generates for it
("PUP SCENE ID", "FRAME GROUP", "FRAME NUMBER" with the numbers). The project tells libserum
which frame is which frame of which scene, and libserum plays that frame for each step of the
scene; it finds a scene frame by its marker only in a colorization of the Win32 editor, which
does not tell it. The frame is colorized like any other.

The project keeps its scenes, together with libserum's scaling settings, and hands them to
libserum with the colorization, in the `.cROMc` (in a settings block at the end of its cROM).
So a colorization needs no `<rom>.pup.csv` and no `scaling.txt` any more: libserum takes the
scenes and scaling settings from the colorization and ignores those files beside it.
The project file keeps the scenes and which frame is which frame of which scene.

## The Scenes section

The `Scenes` section of the left sidebar, below the sprites, lists the project's scenes: a thumbnail
of the scene's first frame in the project (with the blue `HD` badge when that frame has HD data),
and below it the scene ID, the groups and frames, the frame duration, how many of its frames show
for a time of their own, a variant's base and speed, `background` for a background scene and how
many of its frames are missing in the project. The scenes are listed by their IDs.

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
  let it through). Their PUP triggers and monochrome fallbacks stay. One undo step, as with the
  field.
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
- a new frame duration is given to its frames that showed for the old one; a frame with a
  duration of its own keeps it (see "Frame durations" below);
- a frame of the scene deleted from the strip before is added again. Applying the settings
  after changing a field and changing it back therefore brings back missing frames.

Every change is one step of `Undo` and `Redo`, in order with adding, deleting and moving frames.

Scenes need a project with 128x32 ROM frames: libserum plays them in no other. In other projects
the section says so and offers no `New`.

## Deleting frames of a scene

libserum plays a scene's frames by their group and frame number: the numbers of a scene must have no gap. Deleting frames of a scene
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

What one line of a `<rom>.pup.csv` says, and the scene's name and frames:

| Field | Meaning |
| --- | --- |
| Scene ID (PUP trigger) | The trigger that starts the scene. Each scene has its own. A new scene, or a scene given another ID, takes one of 50000 to 61999, the trigger range kept for scenes; `New` suggests the lowest free one. A scene of an imported `pup.csv` keeps the ID it has. |
| Name | A name for the scene, shown with its ID in the lists (`50087 Jackpot`); the project keeps it, a `pup.csv` has none. |
| Plays the frames of | `Its own frames`, or another scene of the project: then the scene is a variant (see below). Chosen for a new scene; a variant can choose another base, a scene with frames of its own keeps them. |
| Frames | Frames of each group. |
| Groups | Sets of frames the scene can show; each time it plays it shows one group. |
| Pick the group at random | Otherwise the groups play in turn. |
| Frame duration | How long each frame shows, unless the frame has a duration of its own (see "Frame durations" below); 33 ms for a new scene (about 30 frames a second). A variant shows the duration its speed gives and cannot change it. |
| Speed | A variant only: how much faster than its base it plays the frames (see "Variants" below). |
| Plays | Once, in a loop, or several times. |
| Back and forth | Each time, the frames play to the last and back: 1 to 100, 99 to 2, then 1 to 100 again, so no frame shows twice in a row. The last time ends on the first frame; a scene with several groups picks its group each time it starts at the first frame. The canvas plays a background scene this way too. Needs libserum 2.99.7. |
| Can be interrupted | A frame of the game identified while the scene plays ends it. |
| Start immediately | The first frame shows when the trigger fires, without waiting for the next frame of the game. |
| Then plays | `No other scene`, or another scene of the project that starts when this one has played (see "One scene after another" below). |
| When it has played | Keep its last frame, black, or the frame shown before the scene. Off while another scene follows. |
| Hold the last frame for / Start by itself after | For a scene that cannot be interrupted and keeps its last frame: how long that frame stays. Otherwise: the scene starts by itself after this many seconds without a new frame. `Off` for neither. |
| Play as a background | The scene shows behind the frames of the game, where their background mask lets it through. The frame that starts it needs no background of its own; the canvas shows the scene's first frame through its mask (see "Backgrounds"). |
| Keep only dynamic content in front | Of the game's frames only dynamic zones, sprites and shadows show over the scene. |
| Black dynamic pixels show the scene | With a background scene: black pixels of dynamic zones let the scene through and cast no shadow. |
| Resume when triggered again | A trigger soon after the scene stopped continues it instead of starting it over. |

The window says how many frames the scene has in the project and why it cannot be taken (an ID
another scene has), and `OK` waits until it can.

## Variants

The settings of a scene say how it plays: in front or as a background, once or in a loop, and
so on. To use the same frames in another way, a scene can play the frames of another scene, its
base, with settings of its own: for example scene 50087 plays once in front, and scene 50088
plays the frames of 50087 as a looping background. A variant can also run slower or faster than
its base: its `Speed` says how much faster, `1x` as fast as the base, `2x` twice as fast (each
frame shows for half its time), `0.5x` at half the speed, from `0.1x` to `10x`. The frames keep
their times to each other: a frame that shows three times as long as the others in the base
does so in the variant too. Its `Frame duration` shows the base's at that speed.

A variant made before speeds existed played at a frame duration of its own; opening the project
gives it the speed that keeps that duration (a base of 40 ms and a variant of 20 ms: `2x`).

- A variant has no frames of its own. Its frames and groups are its base's, and change with
  them; the scene window does not let them be edited. For other frames, add a new scene.
- A scene that is a variant stays one (it can choose another base), and a scene with frames
  of its own stays one: frames are neither added to a variant nor taken from a scene.
- The base is a scene with frames of its own, not a variant itself.
- A double click on a variant in the `Scenes` section lists its base's frames; while they are
  listed, the Inspector's `Scene` section shows and changes the variant.
- A base given another ID keeps its variants; deleting a base deletes its variants too, after
  the question names them. Frames that start a variant play its base's frames, on the canvas
  too.

Variants need libserum 2.99.10 or later; a `pup.csv` cannot describe them, the project and the
`.cROMc` keep them.

## Frame durations

Each frame of a scene can show for a time of its own: a scene that replaces an animation of the
ROM can play it with the animation's times, a pause on one frame, a fast run on others. A new
frame of a scene shows for the scene's `Frame duration`; the `Duration` of the Inspector's
`Frame` section changes it for the selected frames (its tooltip names the scene's duration).
Type the scene's duration there again to make a frame follow the scene's.

- A new `Frame duration` of the scene goes to the frames that showed for the old one; the
  others keep theirs.
- Played back and forth, a frame shows for its time in both directions.
- The last frame before a scene that follows shows for its own time.
- A variant plays every frame at its speed: at `2x`, a frame of 100 ms shows for 50 ms.
- The canvas plays a background scene with the frames' times, as libserum does.

To take the times of an animation from a frame dump, load the dump into the `Frame Tester`
(see "Frame Tester"), go to the first frame of the animation and press `Timings To Scene…`.
Choose the scene and its group; `To dump frame` follows the scene's number of frames. The first
frame of the scene gets the time of the first dump frame, the second the time of the next one,
and so on, in one undo step. When the range and the scene differ in length, only the frames
both have are set; the status bar says how many. A time below 1 ms counts as 1 ms, one above
65535 ms as 65535 ms.

Frame durations of their own need libserum 2.99.20 or later; an earlier libserum plays every
frame at the scene's duration. A project saved by an editor without them opens with every frame
of a scene at its scene's duration, whatever its `Duration` field said, as that editor played it.

## One scene after another

For an animation that plays slowly and then ends fast, for
example a picture at 90 ms per frame that then dissolves to black at 16 ms per frame, make two
scenes and set `Then plays` of the first to the second. When the first scene has played (all
its repeats), its last frame shows for its own duration (or for `Hold the last frame for`,
when set), then the second scene starts and shows its first frame at once, with its own
settings. No frame of the game has to start the second scene, so a frame that shows several
times in an animation does not start it too early.

- The scene that follows replaces `When it has played`; what shows after the second scene is
  the second scene's choice, and it can be followed by a third.
- A scene cannot follow itself (use `Plays`: in a loop or several times). Scenes may follow each
  other in a circle; such scenes play until a frame of the game interrupts one that can be
  interrupted.
- A scene that a frame of the game interrupts starts no scene after it.
- A variant can follow a scene, and be followed.
- In the `Scenes` section a scene with a follow-up says `then 50088`. The follow-up given
  another ID still follows; deleting it leaves the scene before it without one.

The scene that follows needs libserum 2.99.18 or later; an earlier libserum ends the first scene
as `When it has played` says. A `pup.csv` cannot describe it; the project and the `.cROMc` keep
it.

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
128x32 plane). These are what a `scaling.txt` used to set. `Sign the exported .cROMc` (on by
default) signs the project's export, see "Signing". `OK` applies them as one undo step, `Cancel`
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
