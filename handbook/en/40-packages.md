# Packages

A package carries frames from one colorization into another: attract mode frames that a vendor
or an era shares, a font as sprites, test frames. It is a file of its own, `<name>.cdmdpkg`, made
like a project file (see "Project Files").

## Exporting frames

Select the frames in the frame list, then `File -> Export Selected Frames As Package...`. The
package holds the frames in their order, and everything they use:

- the comparison masks (with their names), backgrounds and sprites (with their names) assigned
  to them;
- the light effects they start (see "Speaker Lights"), not the project's defaults;
- the project's license (see "License"), and for every frame who changed it last and when.

It does not hold scenes, sections, palettes or the editor's other data. A frame of a scene
comes as an ordinary frame. The project is not changed by the export.

## Importing a package

`File -> Import Package...` adds the frames of a package to the open project. The package must
have frames of the project's size (128x32, 192x64 or 256x64) with as many shades; otherwise the
import is refused with the reason.

Before anything changes, the editor says how many frames the package has, how many of them the
project has already, the package's license and who made its frames. A frame the project has
already is one with the same ID (it came from this project, or was imported before), or a frame
libserum identifies by the same ROM frame: the same original under a comparison mask of the
same pixels (or none), in the same shape mode. For those frames:

- `Update Them`: the package's frame replaces the project's frame, which keeps its place in the
  frame list and its ID. Its pictures, masks, settings and assignments are the package's.
- `Skip Them`: only the frames the project does not have are added.
- `Add Them As New`: every frame is added, the known ones as copies.

A package with no known frames is imported with `Import`. `Cancel` changes nothing.

New frames go behind the last frame. A comparison mask, background or sprite with exactly the
same content as one of the project is not added again: the frames use the project's own. The
others are added behind the project's; a sprite whose name the project has already gets a
number (`Ball 2`). A light effect is added unless the project has one with the same settings.
The frames keep their tags and their IDs (a frame whose ID the project has already gets a new
one), and who changed them last.

The import is one undo step. It also ends the undo histories of the frames it updated: their
earlier edits can no longer be undone one by one. A package may hold at most as many sprites per
frame as the project allows (see "libserum target"); a project of libserum 2.8 refuses a frame
with more than 32.
