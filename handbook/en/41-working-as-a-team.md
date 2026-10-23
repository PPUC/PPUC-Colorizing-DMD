# Working As A Team

Several authors can work on one colorization at the same time, each in a copy of the project.
What one changes goes to the others as shared changes, which they merge into their copies. A
GitHub repository of the team carries the shared changes and the claims (who works on what);
the project itself goes to the partners once, for example through Google Drive. Authors who
work alone need none of this.

Everything is in the `Team` menu.

## Signing in

`Team -> Sign In To GitHub...` shows a code and opens GitHub's page in the browser (the code is
on the clipboard too). Enter it there and allow PPUC Colorizing DMD access. The editor never
sees the password; it keeps the access GitHub gives it until `Sign Out`. The menu then says
`Signed In As <login>`.

Name yourself in `Settings -> Projects -> Your name`: a save writes it as the author of what you
changed. In a team project without a name, your GitHub login is written.

## Starting a team project

Open the project and choose `Team -> Start Team Project...`:

- `Organization`: the GitHub organization the repository goes to (for example the Serum team's),
  or empty for your own account;
- `Repository`: its name, `<ROM name>-colorization` by default. It holds the claims and the
  shared changes, never the project;
- `Partners`: their GitHub logins, separated by commas. They are invited to the repository.

The editor creates a private repository, invites the partners and saves the project as a team
project. Then give the partners the saved project file (`.cdmd`) once: it is the snapshot
everyone starts from. They open it, sign in, and work from the `Team` menu like you. You are
the project's owner.

## Sharing and merging

- `Team -> Share My Changes` saves the project, then sends everything you changed since the
  last merge (frames, comparison masks, backgrounds, sprites, palettes, the frame order, and the
  settings: license, scenes, speaker lights, helper lines, sections). Changes are counted per
  frame (and per mask, background, sprite, palette), not per pixel.
- `Team -> Merge Changes...` takes in the changes the partners shared, oldest first. What only a
  partner changed is taken; new frames come in their place, deleted ones go. The merge is one
  undo step per shared package; it ends the undo histories of the frames it replaced.
- When you and a partner changed the same frame, the merge asks which to keep: for each conflict
  your version, the partner's and the one before both changes side by side, and `Keep Mine` or
  `Take Theirs` (for a frame one of you deleted, `Delete It` or `Keep It Deleted`). What you keep
  of yours goes to the partner with your next shared changes. `All Mine` and `All Theirs` choose
  for every row.
- After both of you have merged each other's changes, both copies are the same again.

The project is saved before sharing and merging, and after merging: a save gives the changed
frames their new revisions, by which the copies tell what changed.

## Claims

A saved change of a frame (or mask, background, sprite, palette) claims it: the claim goes to the
repository with the next share or refresh of the claims (at the latest a minute later), and the
partners see it read-only. Save early when others might work on the same frames. An edit of a
frame a partner claimed is undone at once; the status bar says who works on it since when, and
the frame's line below the canvas says `claimed`. Moving or deleting frames claims the frame
order, changing the settings claims the settings.

- Claims are read when the project opens and every minute while it is open; `Team -> Refresh
  Claims` reads them now.
- `Team -> Reserve Selected Frames` claims frames before you change them ("I'll do the attract
  mode tonight"); `Release My Reservations` gives them back.
- A claim ends when your change has reached every partner: when they merged it and you merged
  their next shared changes.
- Assigning a mask, background or sprite a partner claimed is allowed: it changes the frame,
  not the mask. The mask may still change until the next merge.

## Breaking the lock

When a partner cannot be reached, anyone can go on alone with `Team -> Break The Lock...`:

- The owner goes on in the project's repository with a new snapshot. The partners' claims and
  the shared changes not merged yet stop counting; give the partners the saved project file
  again, and they start from it.
- Anyone else goes on in a fork of the repository in their own account, as the owner of the
  fork. The team's repository stays as it was.

A partner whose lock was broken is told so; sharing and merging stop in that copy. What they
changed meanwhile can still go to the others as a package (see "Packages").

## Needs

The team features need a GitHub account and a build of the editor with a GitHub app to sign in
through (the PPUC builds have one). Merging and sharing need the network; everything else works
without it.
