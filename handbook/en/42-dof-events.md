# DOF Events

A colorization can drive hardware of the cabinet through DOF, the DirectOutput Framework: a
shaker motor, a fan, a strobe, contactors or LEDs. A frame lists DOF events; when the frame
shows, libserum hands them to the program that runs the colorization (`Serum_GetDofEvents()`,
libserum 2.99.8 and later), which passes each to libdof. What an event does is set up in DOF's
own configuration, as for any table; the project only says which events a frame sends.

## The DOF events field

The `DOF events` field of the Inspector's `Frame` section lists the events of the current frame,
separated by spaces (or commas). An event is written as DOF names table elements:

- a letter for the kind of element: `S` solenoid, `L` lamp, `W` switch, `G` GI string, `M` mech,
  `N` get mech, `E` EM table, `D` LED, `C` score, `B` score digit (another capital letter is
  taken as it is, for kinds libdof may add);
- the element's number;
- `=` and the value, for example `S5=255`;
- optionally `;` and a turn-off value, for example `L12=1;0`.

A lowercase letter is taken as capital. A part that is not an event is marked below the field
and nothing changes until it is corrected. A change replaces the events of all selected frames;
each frame's change can be undone (see "Undo And Redo").

## When the events are sent

- A frame sends its events when it shows, in the order they are written, also a frame of a scene
  at its step.
- A frame that stays on the display sends nothing again, also when libserum identifies it once
  more because its comparison mask hides what changed.
- A turn-off value goes out with the next frame that shows: `L12=1;0` on one frame sends `L12=0`
  when another frame follows, unless that frame sets `L12` itself. The turn-off values go first,
  then the frame's own events.
- libserum gives the values no meaning: a pulse of a solenoid is `S5=255;0`, or `S5=255` on one
  frame and `S5=0` on a later one.

## Testing them

The frame tester (see "Frame Tester") shows the events libserum sent last in the line under the
pictures, as `DOF S5=255 L12=0`. `Edit -> Copy Frame Settings` copies a frame's events to other
frames with `DOF events` (see "Copy And Paste").
