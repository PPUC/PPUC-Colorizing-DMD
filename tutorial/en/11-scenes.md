# Scenes

## What a scene is

A scene is a short animation libserum plays by itself: when a frame of the game starts it, the
scene runs, frame after frame, in front of the game's frames or behind them as a background. An
explosion when a jackpot is lit, a title sequence in attract mode, an animated sky behind the
score.

The frames of a scene are frames of your project, colored like any other. Their originals are not
ROM frames but markers that name the scene and the frame's place in it. The game never shows
them; libserum plays them in order.

Scenes need a project with 128x32 ROM frames.

## Making a scene

1. In the left sidebar's `Scenes` section, press `New`. The scene window asks for the scene's
   ID, its groups and frames, and the frame duration. A background scene plays behind the game's
   frames.
2. The editor adds a frame for each frame of the scene, uncolored, behind the project's last
   frame.
3. A double click on the scene lists its frames in the strip. Color them as any frame; HD,
   backgrounds and sprites work there too. `Leave Scene` below the frame filter goes back.

The Inspector's `Scene` section holds the scene's settings while one of its frames is shown.
Changes wait for `Apply`.

## Starting it

A frame of the game starts the scene through its trigger: choose the scene in `Scene` of the
`Frame` section, or drag the scene from the list onto the canvas, for every selected frame. The
frame filter's `With triggers` lists the frames that start something.

A background scene plays through the background mask of the frames that follow, where they let a
background show.

## Watching it

`Play` with the Runtime view on (see the next chapter) plays the game's frames as libserum would,
scenes included. The frame tester names the scene and its frame while one plays.

Handbook: "Scenes and Scaling", "Frame List".
