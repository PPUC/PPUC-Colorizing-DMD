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
Its functions run on the editor's main thread; a plugin thread goes through `RunOnMainThread`.
