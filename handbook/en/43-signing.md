# Signing

The editor signs the `.cROMc` it exports with your signing key. libserum checks the signature
when it loads the file, without going online:

- **Signed**: the file is the one you released. libserum's log names you.
- **Not signed**: nothing is shown; libserum's log says that the file is not signed.
- **Modified**: someone changed the file after you signed it. libserum still loads and plays it,
  but shows the **signing notice** on the DMD for the first 10 seconds and then for 5 seconds
  every 5 to 10 minutes.

Signing protects nothing: anyone can still change a `.cROMc`. It tells players whether a file
is still yours.

## Your signing key

`Settings -> Signing` holds your key. It stays on this computer, in the editor's settings.

1. `Make a Signing Key` makes the key. The page shows its `Public key`; `Copy` copies it.
2. Send the public key and your GitHub account to the PPUC signing team. They send you a
   certificate that says the key is yours.
3. Paste the certificate into `Certificate`. The page says `Exports are signed as ...` with
   your name once libserum accepts it, or why it cannot be used.

Until the certificate is there the editor exports unsigned files, and the status bar says so
after an export. `Make a New Key...` replaces the key; you need a new certificate for it. Files
you signed with the old key stay signed.

## Signing a project's export

Every project is signed by default. `File -> Project Settings...` -> `Sign the exported .cROMc`
turns it off for one project; the project file keeps the choice, and a team project shares it.
After an export the status bar says whether the file was signed.

## The signing notice

The signing notice is a frame of the project that libserum shows instead of the game's frames
while the notice is up. Write whatever message you like on it: who made the colorization, where
the original is.

- `Edit -> Edit Signing Notice` goes to it, and makes it first when the project has none. The
  first signed export makes it as well. A new notice is black with the words
  `COLORIZATION MODIFIED` in white, on HD too.
- It is the last frame of the frame list, captioned `Signing notice`. Frames you add or move
  behind the last frame go before it, and it does not move.
- It is a frame like any other: HD, color rotations, dynamic content, sprites, a background or
  a background scene (the scene its trigger starts) all show. libserum never identifies it from
  a ROM frame, and its PUP and monochrome triggers are not exported.
- When the notice ends, the game's frames show again as if the notice had not been there.
- Deleting it is allowed. libserum then writes `COLORIZATION MODIFIED` over the game's frames
  instead, which it also does when someone removes the notice from the file.

## Testing what players see

`File -> Export Modified .cROMc for Testing...` exports the project as usual, signed with your
key if you have one, and then changes a byte of the signature. libserum takes the file for a
modified one and shows the notice. Without a key the file gets a signature block that no key
made, with the same result. Do not release such a file.
