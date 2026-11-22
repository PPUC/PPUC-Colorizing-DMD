# Plugins

The editor hosts plugins as VPX does: each one a folder with its `plugin.cfg` and library in the
editor's plugin folder (see "DMD Output" for where it is). The packages ship VPX's DMDUtil and
PUP plugins, the same ones VPX and PPUC use; PUP brings ffmpeg, which the editor's movie clip
import uses too. Besides those, which the editor loads itself, a plugin can work with the
editor: read the open project, change it, show frames, and add entries to a `Plugins` menu.

## Installing a plugin

A plugin you install goes into a folder of its own for your plugins, not into the program's:

- Windows: `%APPDATA%\ppuc.org\PPUC Colorizing DMD\plugins`
- macOS: `~/Library/Application Support/ppuc.org/PPUC Colorizing DMD/plugins`
- Linux: `~/.local/share/ppuc.org/PPUC Colorizing DMD/plugins`

`Settings -> Plugins` names it, with `Open Folder`, which makes it if it is not there yet. Unpack
the plugin there so that its folder (the one with `plugin.cfg` in it) lies directly in
`plugins`, then restart the editor and check the plugin in `Settings -> Plugins`. To update a
plugin, replace its folder; to remove it, delete its folder. The folder is kept when the editor
is updated, and the program's folder, the app bundle on macOS and the AppImage on Linux stay as
they are. The editor looks into the program's plugin folder first: a plugin there is not replaced
by one of the same id in your folder.

## Using plugins

- `Settings -> Plugins` lists the plugins found in both folders, with their version; the
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
  them for the plugin. A plugin can also ask for what it needs before it starts, in a form with
  its own button (`Start`, for example): a file is chosen with `Choose...` or typed in, and a
  box that confirms something (that you may use a file, for example) is unchecked every time
  the form opens; the button stays off until it is checked.
- A plugin can also show a form that stays open beside the editor and changes as you fill it
  in: choosing one entry can enable another field, hide it or give it other choices, and the
  form can show pictures, such as a preview of what the plugin will make from the current
  frame. A short list of entries can be shown as radio buttons instead of a drop-down list, so
  that one click chooses an entry. You can work in the editor while it is open, for example select other frames. Besides
  its own button and `Cancel` such a form can have further buttons, such as `Back` to go to a
  plugin's previous step; like the plugin's own button they keep what you entered for the next
  time, `Cancel` does not.
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
Version 4 adds `ShowForm`, the settings form with its own accept button, and fields for a file,
a paragraph of text and a confirmation that has to be checked.
Version 5 feeds ROM frames to libserum with the whole project loaded (`RunOriginals`: what
libserum identifies for each frame and what the display shows after it), and in edit batches
sets a frame's comparison mask and shape mode, adds and changes comparison masks, and sets the
colors of a frame's dynamic sets.
Version 6 adds a frame in an edit batch (`AddFrame`): a ROM frame the project lacks, such as a
score with more digits, with the colorized pictures and the settings of a frame of the same
kind, as `Edit -> Copy Frame Settings...` copies them, without its trigger and DOF events. It
also moves frames in an edit batch (`MoveFrames`, as the frame list moves them), and `EndEdit`
tells the plugin when the editor did not record its batch: in a team project, an edit of what a
partner works on is taken back, and the status bar says why.
Version 7 copies frame settings in an edit batch (`CopyFrameSettings`), the parts
`Edit -> Copy Frame Settings...` offers, such as dynamic zones, their colors and shadows.
Version 8 creates HD for frames and sprites with the filters of Create HD (`CreateFrameHd`,
`CreateSpriteHd`), deletes frames (`DeleteFrames`), and names, adds and deletes sections
(`SetSection`, `DeleteSection`), all in edit batches.
Version 9 adds live forms (`ShowLiveForm`): a form that does not block the editor, with up to
four extra buttons, that tells the plugin which field the author changed (`GetFormValue` reads
the values shown) and that the plugin changes while it is open (`SetFormField`: enable, hide,
another value, label, choices or help); a new field kind shows a row of pictures (RGB565, drawn
2 to 8 times their size without smoothing, with captions). `CloseForm` closes it. In edit
batches, `CreateScene` adds a scene whose frames are made like frames of the project, with the
settings of the scene window, and `SetFrameScene` sets the scene a frame starts. The current
frame (`GetCurrentFrame`) is the frame clicked last, normally one of the selected frames; a
frame unselected with Ctrl+click (Cmd+click on macOS) stays current, so a plugin that takes two
selected frames as "from" and "to" checks that the current one is among them.
Version 10 adds a field kind for every form: a choice shown as radio buttons, all its entries
at once, up to four on a line (`CDMDPI_FIELD_RADIO`).
Version 11 tells a plugin the editor's language and theme, so that it can show its own texts in
the language of the menus and draw for a light or a dark window: `GetLanguage` gives the
language in effect as `en`, `de` or `fr` (a change in the settings applies from the next start),
and `IsDarkTheme` whether the dark theme is shown, with `Follow the system` resolved.
Version 12 lets a plugin run some of the editor's own functions instead of rebuilding them:
`RunEditorAction` runs an entry of the `Edit` menu as if you chose it, on the frame shown, the
selected frames and the selection, with your draw color and dynamic set, as one undo step of
the editor's; `IsEditorActionEnabled` says whether it can run now. The entries are named by
the ids the keyboard shortcut settings keep them under, listed in the plugin header. Supported
are `Copy`, the selection entries (select all,
none, invert, undo and redo the selection, from the background mask, mirror), `Black Original
To Black`, `Remove Dynamic Zones`, `Assign Dynamic Set`, `Dynamic To Static`, and every
direction of `Shadow Next To Selection` and `Grow Selection`; none of them opens a window. A
plugin can so offer buttons for them, for example the eight shadow directions.
Version 13 adds `SetFrameDuration`, a change of an edit batch: how long a frame shows, as the
`Frame` section's `Duration` sets it. A frame of a scene then shows for that time when the scene
plays (see "Scenes and Scaling", "Frame durations"), so a plugin can give a scene the times of an
animation; `CDMDPI_NONE` gives such a frame the scene's duration again.
Version 14 adds `CDMDPI_SCENE_START_WITH_NEXT_FRAME` for `CreateScene`: the scene starts with the
game's next frame (see "Scenes and Scaling", the setting `Starts`).
Its functions run on the editor's main thread; a plugin thread goes through `RunOnMainThread`.
