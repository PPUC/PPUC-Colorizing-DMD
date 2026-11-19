# Publishing

## The license

Before you share a colorization, choose what others may do with it: `File -> Project
Settings...`, `License`. None of the licenses offered lets others change your colorization;
whoever wants to improve it works on it together with you in the Serum community.

- `All rights reserved`: others may play it, but not share it without your permission.
- `Creative Commons, no changes`: shared unchanged, naming you; commercial use optional.
- `PPUC Colorization License`: used and shared unchanged, naming you, with choices about
  commercial use and real pinball machines.
- `Custom`: your own text.

`Attribution`, `Link` and `Contact` say who is named, where the colorization is found and how to
reach you. They go into the published file, readable at its start.

## Exporting

`File -> Export .cROMc...` writes `<rom>.cROMc`, the one file a player needs. It is made by the
same libserum the editor uses, from the project as it is in the editor, saved or not. Masks,
backgrounds and sprites no frame uses are left out.

Players put the `.cROMc` in the colorization folder of the game. Ask them to remove the old
files of a colorization (`.cROM`, `.cRZ`) from that folder: libserum loads only the `.cROMc`.

A `.cROMc` needs libserum 2.9 or later on the machine. `Help -> About PPUC Colorizing DMD` names
the editor's libserum.

## Signing

The editor signs the exported file with your signing key. libserum checks the signature when it
loads the file: a signed file names you in its log, and a file someone changed after you signed
it shows a notice on the display. Signing does not stop anyone from changing the file; it tells
players whether it is still yours. `Settings -> Signing` holds your key and its certificate.

## Working with others

- **Packages** (`File -> Export Selected Frames As Package...`, `File -> Import Package...`)
  carry frames with everything they use from one project to another: attract mode frames, a font
  of sprites, test frames.
- **Team projects** let several authors work on one colorization at the same time, each in a
  copy of the project; a GitHub repository of the team carries the shared changes. See the
  `Team` menu.
- **Plugins** add tools to the editor. See `Settings -> Plugins`.

## Where to go from here

The handbook (`Help -> Handbook`, `F1`) describes every control and every rule, searchable.
The Serum community answers what neither covers.

Handbook: "Project Files" ("License"), "Saving" ("Exporting the .cROMc"), "Signing",
"Packages", "Working As A Team", "Plugins".
