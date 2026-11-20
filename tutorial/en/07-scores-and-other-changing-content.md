# Scores And Other Changing Content

## Dynamic zones

A dynamic zone colors its pixels by their shade, whatever they show. Each zone belongs to a
dynamic set: a color for every shade of the original. libserum draws the frame's colored
picture and then, in the zones, gives each pixel its set's color for the shade the game sent.

Put a zone where the comparison mask hides the score, and the score is colored whatever it shows:
the mask makes the frame match, the zone colors what changes.

Dynamic zones serve a second purpose: pixel art colored through a set is quick to recolor,
because changing the set's colors recolors every pixel of it.

## Painting zones

1. Press `Dynamic` in the toolbar. The zones are outlined on the colorized frame and shown on the
   original: the selected set in yellow, the others in cyan. Zoomed in, each area carries its set
   number.
2. Choose the set in `Dyn Set` of the `Dynamic Sets` section.
3. Paint the zone with the drawing tools, on either picture. `Shift` erases. `Flood` stops at
   other zones: clicked into a zone it refills just that zone, clicked outside it fills the
   pixels of that color that are in no zone yet.
4. Press `Dynamic` again to paint colors.

The right mouse button paints zones too, whatever mode is on, as in the Win32 editor. If you
came from the Win32 editor, `Settings -> Appearance -> Dynamic Zones` makes the left button paint
colors while `Dynamic` only shows the zones.

Dynamic zones live on the 128x32 picture. In HD mode `Dynamic` is off; switch to SD to see and
paint them.

## The set's colors

The `Dynamic Sets` section shows a swatch for each shade of the selected set. A click opens the
color picker; a palette color dragged onto a swatch takes its place. The change goes to every
selected frame with dynamic data. `Shift`+drag from the darkest to the lightest swatch makes a
gradient.

`Edit > Dyn` under the toolbar copies the edit colors into the set, `Dyn > Edit` the other way:
work out the colors with `Set Replace` on the frame, then hand them to the set.

## Shadows

A score over a busy picture reads better with a shadow. The direction buttons `TL` to `BR` in the
`Dynamic Sets` section cast a shadow from the set's pixels to their neighbors, in `Shadow Color`.

## Helpers

- `Edit -> Assign Dynamic Set` (`Alt+A`) puts the selected pixels into the selected set.
- `Edit -> Remove Dynamic Zones` (`Ctrl/Cmd+E`) takes them out again.
- Copy and paste carry zones and set colors along with the pixels.

## A score, step by step

1. Find a frame with a score, and select every frame that shows the same picture with other
   scores.
2. Make a comparison mask over the score's area and assign it (previous chapter). Delete the
   same frames you do not need.
3. Press `Dynamic`, choose set 0 and paint a zone over the same area.
4. Give the set its colors: a gradient from dark to light in the score's color.
5. Check in the Runtime view or the frame tester that other scores are found and colored (see
   "Checking Your Work").

Handbook: "Dynamic Masks", "Settings".
