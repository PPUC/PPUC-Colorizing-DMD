# What A Colorization Is

A pinball machine with a dot matrix display (DMD) sends its pictures as frames of shades: 4 or
16 levels of one color, 128x32 pixels on most machines. A colorization gives these frames
colors. On the machine, or in a virtual pinball setup, libserum receives every frame the game
sends, looks it up in the colorization and shows the colored picture instead.

## How libserum finds a frame

libserum does not search for a similar picture. It computes a hash of the frame the game sent
and looks for a frame of the colorization with the same hash. A frame of your project is
therefore an exact copy of a frame the game showed, its "original", plus the colors you give
it.

That is why a project starts from a frame dump: a recording of the frames the game showed while
it was played. It is also why much of the work is about frames that are almost, but not quite,
the same, as the next sections show.

## The parts of a colorization

- **Colorized frames.** Each frame of the project has its original and its colored picture. A
  frame the game shows exactly as recorded gets exactly that picture.
- **Comparison masks.** A frame that shows a score is never shown twice the same way: the score
  changes. A comparison mask hides the pixels that change, so that libserum compares only the
  rest. One colorized frame then stands for every score.
- **Dynamic zones.** The hidden pixels still need colors. A dynamic zone colors them by their
  shade with a set of colors, whatever they show: the score is colored when it is drawn.
- **Sprites.** Small pictures that move over a frame (a ball, a character, a digit) are found
  wherever they are and colored as they were captured.
- **Backgrounds.** A picture behind the frame's dark pixels, shared by many frames.
- **Color rotations.** Colors that cycle while a frame is shown, for shine and movement.
- **Scenes.** Short animations libserum plays by itself when a frame triggers them.
- **HD.** A second picture of each frame at 256x64, upscaled from the 128x32 one and refined, for
  displays with more pixels.

None of these is needed to start. A colorization of plain colored frames already plays; the
rest makes it cover more of the game with less work.

## The files

- `<name>.cdmd` is your project: everything you work on, in one file. The editor saves it.
- `<rom>.cROMc` is what you publish. `File -> Export .cROMc...` makes it from the project.
  Players put it in the colorization folder of the game; libserum loads nothing else.
- A frame dump (`.txt`, also as `.gz` or `.zip`) is what a project starts from.
- `.cROM`, `.cRZ`, `.cRP` and `.cRPX` are the files of earlier editors. The editor opens and
  converts them and never writes them.

## What you need

- The editor.
- A frame dump of the game. Dumps are recorded by the software that shows the DMD on a computer
  or a real machine; the Serum community can help you find or record one.
- Patience for the first project. The work is detailed, and most of it repeats for many frames,
  which is where the editor's multi-frame tools come in.

Handbook: "Project Files", "Opening Projects".
