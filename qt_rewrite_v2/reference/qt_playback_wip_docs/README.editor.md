# Editor + libserum Development Notes

This document captures legacy-format behavior and editor migration decisions.
It complements `readme.md` (user-facing info) and must not change file formats.

## Guiding constraints
- Keep cROM/cROMc formats 100% compatible with existing tools.
- The editor should be a drop-in replacement for the legacy app.
- Avoid new dependencies in libserum (no Qt/OpenCV).
- The editor targets Serum V2; libserum retains V1 for other consumers.

## Current direction
- Use libserum for canvas + preview rendering (WYSIWYG).
- Add a libserum editor API with non-owning data views to avoid RAM blowups.
- Preview row renders only the visible subset of frames.

## libserum editor API concept
- New header: `serum-editor.h` (separate from `serum-decode.h`).
- Core functions (names TBD):
  - Create/destroy editor context.
  - Attach a non-owning `SerumDataView` (raw pointers + sizes).
  - Render a frame by ID using project originals.
  - Match sprites for a frame and return positions.
  - Optional: render with precomputed matches to avoid repeat matching.
- Color rotations should be off by default, enabled only on demand.

## Legacy data relationships (v2)
- Dynamic masks: `dynamasks[frame][pixel]` -> dynacouche index.
- Dynamic colors: `dyna4cols_v2[frame][dynacouche * nocolors + originalPixel]`.
- Sprite dynamic colors:
  - `dynaspritemasks[sprite][pixel]` -> dynacouche index.
  - `dynasprite4cols[sprite][dynacouche * nocolors + originalPixel]`.
- Sprite detection:
  - Frame detection zones: `framespriteBB[frame][slot*4..]`.
  - Per-sprite detection areas: `spritedetareas[sprite][area*4..]`.

## Rendering order (runtime behavior)
1) Background (when original pixel is 0 and mask allows).
2) Dynamic mask replacement.
3) Color rotations (if enabled).
4) Sprites overlay.

## UI conventions implemented so far
- Masks and dynamic masks show semi-transparent overlays + outlines on grid.
- Background mask is independent from mask/dynamic mask toggles.
- HD frames render as same absolute size with higher internal resolution.

## Open tasks
- Add libserum editor API and wire canvas + preview rendering.
- Use libserum sprite matching to fix sprite placement in editor.
- Add on-demand rotation preview button.
