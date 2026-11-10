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
- `Merge Changes...`, `Share My Changes` and `History...`, as in the `Team` menu, and
  `Add File...` for shared changes that come as a file (see below).

Below the buttons, `Time spent` lists the time each of you spent on the project, as far as the
shared changes have brought it to your copy: your shared changes carry your time, a merge takes
the partner's. The project time in the status bar is the sum.

While shared changes wait, the section's header shows a bell and their number, also while the
section is closed; it goes once they are merged.

## Sharing and merging

- `Team -> Share My Changes` saves the project, asks for a short comment for the partners (what you
  changed, what is still open; it may stay empty), then sends everything you changed since the last
  merge (frames, comparison masks, backgrounds, sprites, palettes, the frame order, and the
  settings: license, scenes, speaker lights, helper lines, sections). Changes are counted per frame
  (and per mask, background, sprite, palette), not per pixel.
- `Team -> Merge Changes...` first lists the changes the partners shared, oldest first, with who
  shared them, when, their comment and what they change (as `Team -> History...` shows them,
  below). Read them, then `Merge` takes them in, in that order; `Cancel` leaves them waiting.
  What only a partner changed is taken; new frames come in their place, deleted ones go. The
  merge is one undo step per shared package; it ends the undo histories of the frames it
  replaced.
- When you and a partner changed the same frame, the merge asks which to keep: for each conflict
  your version, the partner's and the one before both changes side by side, and `Keep Mine` or
  `Take Theirs` (for a frame one of you deleted, `Delete It` or `Keep It Deleted`). What you keep
  of yours goes to the partner with your next shared changes. `All Mine` and `All Theirs` choose
  for every row.
- After both of you have merged each other's changes, both copies are the same again.

The project is saved before sharing and merging, and after merging: a save gives the changed
frames their new revisions, by which the copies tell what changed.

## The history of the shared changes

`Team -> History...` (or `History...` in the `Team` section) lists the changes shared in the team
project, newest first, so that you can read later what a merge took in. Each shows:

- who shared it and when; `waiting to be merged` for a partner's change you have not merged yet;
- the comment its author typed at `Share My Changes` (`No comment` when it stayed empty);
- what it changed, with the numbers as your project has them now: `Frames added: 2360`,
  `Frames changed: 12-15, 40`, `Comparison masks changed: 36`, the same for backgrounds, sprites
  and palettes, how many were deleted, and whether the frame order or the settings changed.
  A frame that is no longer in the project is counted, as `1 no longer in the project`; one a
  waiting change adds is counted as `new`.

Double-click a line of frames to select them in the frame list. The window stays open while you
work, and follows the merges and the partners' new changes.

Your copy keeps the history in the project file: your own shared changes and the ones you merged.
A waiting change shows what it changes once the editor has downloaded it (a change that comes as
a file once you added it); before, its comment. Changes merged with an older editor, which kept no
history, are taken from the editor's download folder, as far as this computer downloaded them.

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
`claimed`. In the frame list, a frame a partner works on (or reserved) shows the partner's
GitHub picture in its upper left corner, a lock until the picture is loaded. Moving or deleting
frames claims the frame order, changing the settings claims the settings. While a partner
changes the settings, `File -> Project Settings...` says so when you press `OK` and changes
nothing; merge the partner's changes first. The speaker light effects belong to the settings:
`New`, `Delete` and `Apply` of a light effect say so the same way.

The exported `.cROMc` of a team project names the one who started it (`Owner`) as the author,
whoever exports it, and the other members as contributors (see "License"), unless the license's
`Attribution` names the author.

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

## Working on two computers

You can share a project with yourself, for example the day's work on a desktop and the evening's
on a laptop. Make it a team project (`Team -> Start Team Project...`, partners may stay empty),
then:

1. On the first computer, share your changes (`Team -> Share My Changes`), save the project,
   and copy the project file to the second computer.
2. Open it there, signed in to GitHub with the same account. The editor sees that the copy was
   last used on another computer and asks:
   - `Another Computer...`: this is a further computer you work on. Give it a short name (for
     example `laptop`). The copy becomes a member of the team of its own, shown as `kraken
     (laptop)`, under your account.
   - `Moved Here`: you moved the project to this computer and no longer use the old copy; it
     stays the same member.
   - `Later`: nothing is shared, merged or claimed from this copy until you open it again and
     choose.
3. From then on each computer shares what it changed, and the other merges it, as with a
   partner: merge at the start of a session, share at its end. Your frames that the other
   computer changed and has not shared yet are read-only, as a partner's are, and say `kraken
   (laptop) works on it`.

Partners see your second computer as one more member, and a change counts as merged by everyone
only once all your computers merged it too. Copy the project file only once per computer; later
the changes go through GitHub. Copy the signing key over as well, see "Signing".

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
