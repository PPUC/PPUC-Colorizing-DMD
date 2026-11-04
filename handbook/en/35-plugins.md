# Plugins

The editor hosts plugins as VPX does: each one a folder with its `plugin.cfg` and library in the
editor's plugin folder (see "DMD Output" for where it is). The packages ship VPX's DMDUtil and
PUP plugins, the same ones VPX and PPUC use; PUP brings ffmpeg, which the editor's movie clip
import uses too. Besides those, which the editor loads itself, a plugin can work with the
editor: read the open project, change it, show frames, and add entries to a `Plugins` menu.

- `Settings -> Plugins` lists the plugins found in the plugin folder, with their version; the
  tooltip has their description. Built-in plugins, which the editor loads itself when it needs
  them, are listed checked and marked `built in`; they cannot be unchecked: DMDUtil for the DMD
  output, and PUP (VPX's Pin Up Player) for the PuP tester. Any other checked plugin loads at
  once and at every start of the editor; unchecking unloads it. A plugin that does not load is
  named with the reason.
- The `Plugins` menu appears while a plugin has entries in it.
- What a plugin changes in the project is one step of the project's history, under the name the
  plugin gives it: one `Undo` takes it back whole (the status bar says `Undo: <name>`), however
  many frames it changed. It is undone in order with adding and deleting frames, sprites and
  backgrounds. Undo steps made before it on the frames it changed are no longer available.
- A plugin that proposes new pictures (an upscaler, for example) shows them in a review window:
  the list on the left names each frame, background or sprite with its plane, checked at first;
  beside it is the picture now and the proposed one. `Mark the pixels the proposal leaves as
  they are` tints the pixels the plugin does not write (where a frame shows its background, for
  example). `Apply` writes the checked pictures as one step of the history; a frame, background
  or sprite without HD gets it first, as `Create HD` makes it. `Discard` writes nothing. The
  window does not block the editor, so the frames can be looked at meanwhile; but once the
  project changes, the proposal is outdated and can no longer be applied: ask the plugin again.
- A plugin can show a form for its own settings (a service address, for example); `Save` keeps
  them for the plugin.
- What a plugin logs is in `Help -> Plugin Log`.
- Plugins that are not part of the editor's packages come from their authors; load only those
  you trust, as they run inside the editor.

For plugin authors: the editor API is the C header `plugin-api/ColorizingDMDPlugin.h` in the
editor's sources, on top of the VPX plugin SDK's `MsgPlugin.h`. Version 1 reads the project
(summary, frames, comparison masks, dynamic zones, sections), the current and the selected
frames, shows frames, adds menu entries, shows messages and status lines, and keeps settings.
Version 2 adds edit batches: between `BeginEdit` and `EndEdit` a plugin fills frames with a
color or the original's plain colors except where it keeps them, sets dynamic zones and backgrounds, deletes
backgrounds and clears color rotations; the changes apply at once and are recorded as one step.
Version 3 reads background masks, background and sprite pictures and sprite shapes, the sprites
a frame looks for, and the background and sprite the author works on; writes frame, background
and sprite pictures in edit batches; and opens the review window and the settings form.
Its functions run on the editor's main thread; a plugin thread goes through `RunOnMainThread`.
