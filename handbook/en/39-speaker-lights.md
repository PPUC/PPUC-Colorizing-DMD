# Speaker Lights

A PPUC DMD can drive LED strips around the speakers of the backbox (WS2812 "speaker lights",
up to four channels; usually channel 1 is the left speaker and channel 2 the right one). A
colorization can play effects on them: a frame starts an effect, a static color or turns a
channel off when it shows. libserum 2.9 and later read them from the colorization and hand them
to the DMD; an older libserum ignores them. The number of LEDs and whether a strip is RGB or
RGBW are set up on the DMD itself (its menu), as is the brightness.

## Light effects

The `Light Effects` section of the left sidebar lists the project's effects, each with its
colors, its kind and how many frames start it. `New` adds one, `Delete` removes the selected
one after asking; the frames that start it leave that channel unchanged then.

The selected effect is edited in the `Light Effect` section of the right sidebar. The settings
are those of the WS2812FX library the DMD's firmware plays them with:

- `Name`: how frames and the project settings offer the effect.
- `Effect`: one of the library's 80 effects (`Static`, `Blink`, `Rainbow Cycle`, `Larson
  Scanner`, `Fire Flicker`, ...). `Static` shows `Color 1` on every LED.
- `Color 1` to `Color 3`: the colors the effect uses, each with a white value (`W`) for RGBW
  strips; an RGB strip ignores it.
- `Speed`: the length of one cycle of the effect in ms; lower is faster.
- `Options`: `Reverse` runs it the other way round, `Gamma` gamma-corrects the colors; `Fade`
  sets how fast the effects that fade do, `Size` the size of the moving parts of the effects that
  have them.
- `Brightness`: a share of the brightness set on the DMD (100 %: as set there).
- `Duration`: how long the effect plays before the channel returns to its default (see below),
  up to 65535 ms; `Until the next effect` keeps it until a frame starts another one.

The preview below the fields plays the effect as the fields say, on 16 LEDs. `Apply` changes
the effect (one undo step), `Revert` shows its settings again.

## What a frame does

The `Speaker Lights` section of the right sidebar shows, for the frame on the canvas, what it
does to each channel when it shows: `Unchanged` (the channel keeps what it plays), `Off`, a
`Static color` (the button next to it picks the color) or one of the effects. A change applies
to every selected frame, that channel only; each frame's change is a step in its own undo
history. The channels are independent: a frame can start a different effect on each.

`Suggest from Frame` sets channels 1 and 2 of every selected frame to a static color taken from
the frame itself, as ambient lighting: channel 1 the average of the lit pixels of the frame's
leftmost content, channel 2 of its rightmost content, each a quarter of the frame's width from
the first lit column seen from that side. A black margin, such as the empty score block of a
single player game on many Stern ROMs, is skipped, so the color comes from what is shown; a
black frame turns both off. The colors can be changed afterwards like any other.

Colors set this way are marked `automatic` beside the channel in the `Speaker Lights` section.
When the frame's colors have changed since (the frame was painted after), the mark reads
`automatic, out of date`; the frame list's filter `Ambient lighting out of date` lists those
frames. `Edit -> Update Ambient Lighting` brings every automatic channel of
the project up to date with its frame's colors, in one undo step; it is the one to run after
reworking frames. A channel you set yourself, including a color you pick for an automatic
one, is yours: it loses the mark and no update changes it.

`Edit -> Ambient Lighting For Frames Without Speaker Lights...` does the same for every frame of the project that does nothing to any channel yet, in one undo step. It
is meant for a project made before speaker lights: frames already set up are left as they are,
so it can be run again after adding frames.

A frame that starts the effect a channel already plays (the frames of an animation that all
start it, a frame shown again) leaves it running, unless its duration has run out.

## The default

`File -> Project Settings...` has a `Speaker light` field per channel: what the channel shows
while no frame has started anything and after an effect with a duration. Without one the
channel is off. A project without any speaker light setting leaves the lights to the DMD's own
menu setting.

## The preview

`View -> Speaker Light Preview` shows two speakers beside the canvas and in the frame tester (off
until switched on). `Settings -> Appearance -> Speaker Light Preview` sets them up. Like the size of the canvas pixels it is a setting of the editor, not of the
project, since machines differ: the `Shape` (a ring of LEDs, `Circle`, or LEDs around a
standing `Rectangle`), the number of `LEDs` per speaker, and which channel the `Left speaker`
and the `Right speaker` show.

- Beside the canvas the speakers sit level with the middle of the colorized frame; dragging the
  border between a speaker and the canvas makes both speakers larger or smaller (kept for the
  next start). They play what the channels play while the frame on the canvas shows: per channel the last frame up to it in the frame list that changes the channel, the
  default before that.
- In the frame tester, beside the 32-row and the 64-row plane, they play what libserum starts while the frames are fed, from the defaults
  of the loaded project on, as the DMD would get it.

The preview runs the effect code of the DMD's firmware itself, so the effects look the same as on
the machine, up to the LEDs' own colors.

## In the files

The effects, the defaults and the frames' settings are kept in the `.cRPX` (with the effects'
names) and written into the settings block at the end of the `.cROM`, where libserum reads them.
A project without speaker lights writes the `.cROM` as before.
