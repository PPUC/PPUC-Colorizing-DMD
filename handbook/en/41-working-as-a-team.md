# Working As A Team

Several authors can work on one colorization at the same time, each in a copy of the project.
What one changes goes to the others as shared changes, which they merge into their copies. A
GitHub repository of the team carries the shared changes and the claims (who works on what);
the project itself goes to the partners once, for example through Google Drive. Authors who
work alone need none of this.

Everything is in the `Team` menu, and the most used steps are in the `Team` section of the
right side bar too.

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

## The Team section

A team project has a `Team` section in the right side bar (it is not there for other projects):

- the team's repository and who you are signed in as, or `Sign In To GitHub...`;
- the partners' shared changes waiting to be merged, oldest first, each with who shared it
  (and their GitHub picture), when, and their comment; a double click merges them, as `Merge
  Changes (2)...` below does;
- when the editor last looked for them (`Checked at 14:05:12`);
- `Merge Changes...` and `Share My Changes`, as in the `Team` menu, and `Add File...` for
  shared changes that come as a file (see below).

While shared changes wait, the section's header shows a bell and their number, also while the
section is closed; it goes once they are merged.

## Sharing and merging

- `Team -> Share My Changes` saves the project, asks for a short comment for the partners (what you
  changed, what is still open; it may stay empty), then sends everything you changed since the last
  merge (frames, comparison masks, backgrounds, sprites, palettes, the frame order, and the
  settings: license, scenes, speaker lights, helper lines, sections). Changes are counted per frame
  (and per mask, background, sprite, palette), not per pixel.
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

## Large changes as a file

GitHub is no place for large files: it takes none over 100 MB, and the repository keeps every
file it ever had. When your shared changes come to more than 20 MB (new frames from a long
video, for example), `Share My Changes` asks how to send them:

- `Save As File...` saves them as a file (`.cdmdchanges`) for you to send another way, for
  example through Google Drive. GitHub gets only a notice: the partners see the change in their
  `Team` section at once, with its size and your comment, and `comes as a file`.
- `Through GitHub` sends them as usual (not offered above 70 MB).

A partner who received the file adds it with `Add File...` in the `Team` section, or drops it
there. The editor checks that it belongs to this team project and has not been merged yet;
then the change merges like any other. Shared changes merge in the order they were shared: a
merge stops at a change whose file is not added yet, and says so.

## Claims

Your first edit of a frame (or mask, background, sprite, palette) claims it, saved or not: the
partners see it read-only a few seconds later. An edit of a frame a partner claimed is undone at
once; the status bar says who works on it since when, and the frame's line below the canvas says
`claimed`. Moving or deleting frames claims the frame order, changing the settings claims the
settings.

- The editor keeps the claims in step in the background, every 15 seconds and a moment after
  you edit something new, without making you wait; `Team -> Refresh Claims` does it now and says
  when they are up to date.
- In the same way it looks for the partners' shared changes and downloads them: the status bar
  says when there are new ones, the bell on the `Team` section's header and `Team -> Merge
  Changes (2)...` how many wait. Merging them then needs no waiting for the network.
- `Team -> Reserve Selected Frames` claims frames before you change them ("I'll do the attract
  mode tonight"); `Release My Reservations` gives them back.
- Once you have merged a partner's change, its frame (or the frame order, the settings) is free
  for you, even while the partner's claim is still listed: you have their version. It is
  locked again when the partner changes it once more, saved or not.
- A claim ends when your change has reached every partner: when they merged it and you merged
  their next shared changes. An edit you undo and save without a change ends its claim too.
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
