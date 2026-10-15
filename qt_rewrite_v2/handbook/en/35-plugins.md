# Plugins

The editor hosts plugins as VPX does: each one a folder with its `plugin.cfg` and library in the
editor's plugin folder (see "DMD Output" for where it is). Besides DMDUtil, which the DMD output
loads itself, a plugin can work with the editor: read the open project, show frames, and add
entries to a `Plugins` menu.

- `Settings -> Plugins` lists the plugins found in the plugin folder, with their version; the
  tooltip has their description. Built-in plugins, which the editor loads itself when it needs
  them (DMDUtil, for the DMD output), are listed checked and marked `built in`; they cannot be
  unchecked. Any other checked plugin loads at once and at every start of the editor;
  unchecking unloads it. A plugin that does not load is named with the reason.
- The `Plugins` menu appears while a plugin has entries in it.
- What a plugin logs is in `Help -> Plugin Log`.
- Plugins that are not part of the editor's packages come from their authors; load only those
  you trust, as they run inside the editor.

For plugin authors: the editor API is the C header `plugin-api/ColorizingDMDPlugin.h` in the
editor's sources, on top of the VPX plugin SDK's `MsgPlugin.h`. Version 1 reads the project
(summary, frames, comparison masks, dynamic zones, sections), the current and the selected
frames, shows frames, adds menu entries, shows messages and status lines, and keeps settings.
Its functions run on the editor's main thread; a plugin thread goes through `RunOnMainThread`.
