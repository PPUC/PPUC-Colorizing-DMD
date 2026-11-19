# Your First Project

## Create it from a dump

Choose `File -> New Project From Dump...` and pick the dump. The window reads it at once and
shows what it found: the frame size, 4 or 16 shades, and how many frames there are. Nothing is
created before you press `Create Project`, so try the options and read the numbers.

- **ROM name.** Proposed from the file name. It is the name libserum looks the colorization up
  by, so it has to be the game's ROM name, for example `afm_113b`. The line below the field says
  when a name cannot be used.
- **Leave out transitional frames (WPC).** While a WPC display changes from one picture to the
  next, it shows a mix of both for a moment, and the dump records these ghost frames. In smooth
  animations they can be every second frame. Turning the option on leaves them out, often half
  of the dump. It works only here, while the dump is read in the order the machine showed it.
  If a real fade made of very short steps disappears too, lower the limit.
- **Leave out frames shown for less than N ms** and **with more than N shades.** Very short
  frames and frames with many shades are mostly fades between two pictures.
- Repeated frames are always left out: each picture comes into the project once.

After changing an option, press `Read Dump Again`; the numbers say how many frames the project
will get. Then press `Create Project`.

Fewer frames is less work, but a frame left out is a frame libserum will not find. When in
doubt, keep it: frames can be deleted later, and the editor finds duplicates for you (see
"Telling Frames Apart").

## What you get

Every frame is uncolorized. It shows its original in the plain original colors, an amber ramp
from black to the brightest shade, as on the real display. There are no masks, sprites,
backgrounds or sections yet.

The window title reads `<ROM name> (not saved yet)`. Save now: `File -> Save` asks where to
write the project file and proposes `<ROM name>.cdmd` beside the dump. From then on `Save` writes
to it without asking.

## Save often, and know the safety net

Every 10 minutes the editor writes an autosave of a project with unsaved changes, in the
background, without touching your project file. If the editor or the computer stops, opening
the project again offers its newer autosave (`Open Autosave`); a new project that was never
saved is offered when the editor starts. The project file itself changes only when you save.

## Your name

Open `Settings -> Projects` and enter `Your name`. Every save notes who changed which frame,
mask, background or sprite, and the published `.cROMc` names you as the author. It matters as
soon as anyone else works on the colorization with you.

## Opening other projects

`File -> Open Project...` opens a `.cdmd`, or the files of an earlier editor: a `.cROM` or a
`.cRZ`. Those are converted on opening and saved as a `.cdmd` from then on; the old files stay as
they are.

Handbook: "Creating A Project From A Dump", "Saving", "Opening Projects".
