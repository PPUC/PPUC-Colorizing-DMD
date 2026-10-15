# DMD Output

`View -> Output To DMD`, or the `DMD` button in the toolbar beside `Runtime`, shows the canvas
picture on a real DMD as a live preview (colors on a DMD differ from those on a monitor). The
editor hosts VPX plugins, as VPX and PPUC do, and the picture goes through VPX's DMDUtil plugin to
libdmdutil. Switching it on loads the plugin, which makes libdmdutil look for attached displays;
switching it off unloads it and lets them go.

- What is shown: the frame as the editor composes it on the plane the canvas shows (with the
  background when the canvas's background layer is on; a stroke appears when it is finished,
  overlays such as the selection or the mask are not sent), libserum's picture while the Runtime
  view is on (playback and color rotations included), and while the Frame Tester is open, the
  frame it last fed. The picture follows every edit, frame change and playback step. libdmdutil
  scales it to the display, a 256x64 picture down to a 128x32 display as well.
- Which plane: `Picture` on the `DMD Output` page of the Settings, for projects with a native
  128x32 plane and an extra 256x64 one:
  - `The plane the canvas shows` (the default): the plane being edited, in the Runtime view the
    rows it shows, in the Frame Tester the plane of the canvas;
  - `Native plane (128x32)`;
  - `Extra plane (256x64)`: a frame without extra data sends its native plane; in the Runtime
    view and the Frame Tester libserum's 64-row picture, which libserum upscales for such a
    frame, as on a machine with a 64-row display.
  The canvas keeps showing its own plane; in the Runtime view libserum renders both planes in
  one go, so playback and color rotations stay the same on screen and display. A project with
  one plane always sends that one.
- The `DMD Output` page of the Settings (see "Settings"; `View -> DMD Output Settings...`
  opens the dialog at that page) says where it goes:
  - displays connected to this computer: ZeDMD (any serial port, or the one given; or over
    Wi-Fi at an address), with its brightness (0 to 15, or the display's own) and `ZeDMD debug
    output` (more messages from the ZeDMD connection in the plugin log), Pixelcade (any port or
    the one given) and PIN2DMD;
  - or `Use DMDServer`: a `dmdserver` (libdmdutil's server process) at an address and port, which drives the
    displays itself.
  The settings are kept and become the DMDUtil plugin's settings; changing them restarts a
  running output (the plugin reads them when it loads).
- After switching it on, libdmdutil looks for displays in the background; after a few seconds
  the status bar shows the last thing the plugin or libdmdutil reported, such as a display it
  found or why one failed. When the plugin does not load, the output switches off again and the
  status bar says why.
- `Help -> Plugin Log` shows everything the plugins and the plugin host logged since the editor
  started (the last 5000 lines): which displays libdmdutil looked for, found or failed to open,
  with time and level. `Show` limits it to information, warnings or errors; `Copy All` copies the
  lines shown, for a report; `Clear` starts over. When a display stays dark, switch on `ZeDMD
  debug output` (for a ZeDMD), switch the output off and on, and look there.
- The plugins lie in a folder of their own, one folder per plugin as VPX ships them: `plugins`
  beside the program (Windows, Linux) or `ColorizingDMD.app/Contents/PlugIns/vpx` (macOS). The
  environment variable `COLORIZINGDMD_VPX_PLUGIN_DIR` names another folder. Without the
  DMDUtil plugin there the two menu entries are disabled. The release packages ship it.
