# Speaker Lights

A PPUC DMD can drive LED strips around the speakers of the backbox (WS2812 "speaker lights",
up to four channels; usually channel 1 is the left speaker and channel 2 the right one). A
colorization can play effects on them: a frame starts an effect, a static color or turns a
channel off when it shows. libserum 3.0 and later read them from the colorization and hand them
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
- `Duration`: how long the effect plays before the channel returns to its default (see below);
  `Until the next effect` keeps it until a frame starts another one.

The preview below the fields plays the effect as the fields say, on 16 LEDs. `Apply` changes
the effect (one undo step), `Revert` shows its settings again.

## What a frame does

The `Speaker Lights` section of the right sidebar shows, for the frame on the canvas, what it
does to each channel when it shows: `Unchanged` (the channel keeps what it plays), `Off`, a
`Static color` (the button next to it picks the color) or one of the effects. A change applies
to every selected frame, that channel only; each frame's change is a step in its own undo
history. The channels are independent: a frame can start a different effect on each.

`Suggest from Frame` sets channels 1 and 2 of every selected frame to a static color taken from
the frame itself, as ambient lighting: channel 1 the average of the lit pixels of the left quarter of
the frame, channel 2 of the right quarter (off for a black part). The colors can be changed
afterwards like any other.

A frame that starts the effect a channel already plays (the frames of an animation that all
start it, a frame shown again) leaves it running, unless its duration has run out.

## The default

`File -> Project Settings...` has a `Speaker light` field per channel: what the channel shows
while no frame has started anything and after an effect with a duration. Without one the
channel is off. A project without any speaker light setting leaves the lights to the DMD's own
menu setting.

## The preview

`Settings -> Appearance -> Speaker Light Preview` shows two speakers beside the canvas and in the
frame tester. Like the size of the canvas pixels it is a setting of the editor, not of the
project, since machines differ: the `Shape` (a ring of LEDs, `Circle`, or LEDs around a
standing `Rectangle`), the number of `LEDs` per speaker, and which channel the `Left speaker`
and the `Right speaker` show.

- Beside the canvas the speakers play what the channels play while the frame on the canvas
  shows: per channel the last frame up to it in the frame list that changes the channel, the
  default before that.
- In the frame tester they play what libserum starts while the frames are fed, from the defaults
  of the loaded project on, as the DMD would get it.

The preview runs the effect code of the DMD's firmware itself, so the effects look the same as on
the machine, up to the LEDs' own colors.

## In the files

The effects, the defaults and the frames' settings are kept in the `.cRPX` (with the effects'
names) and written into the settings block at the end of the `.cROM`, where libserum reads them.
A project without speaker lights writes the `.cROM` as before.
