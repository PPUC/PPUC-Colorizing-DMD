# PPUC Colorizing DMD

PPUC Colorizing DMD is the editor for Serum colorizations of pinball DMD frames: it turns frame
dumps into colorized frames, with backgrounds, sprites, dynamic zones, color rotations and PUP
scenes, and writes the `.cROM`/`.cRZ` and `.cROMc` files that
[libserum](https://github.com/PPUC/libserum) plays on a machine, in VPX or in PPUC.

It runs on Windows, macOS (Apple silicon) and Linux. It opens the projects of the Win32
ColorizingDMD editor (CDMD 3.17.3); a project saved here is kept in a `.cRPX` beside its `.cROM`
and cannot be opened by the Win32 editor again.

## Downloads

The packages of every build are on the
[Actions](https://github.com/PPUC/ColorizingDMD/actions) page; releases are under
[Releases](https://github.com/PPUC/ColorizingDMD/releases). Version 4.0.0 will be the first
release; until then the versions are 3.99.x (`Help -> About` shows yours).

## Handbook

The handbook is built into the editor (`Help -> Handbook`, `F1`). Its chapters are in
[handbook/en](handbook/en/README.md).

## Building

CMake 3.20, a C++17 compiler, Qt 6 (Widgets) and a checkout of
[libserum](https://github.com/PPUC/libserum) beside this repository (`../libserum`, or set
`COLORIZINGDMD_LIBSERUM_DIR`):

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

The release packages are made by `packaging/package-<windows|linux|macos>.sh`; see
`.github/workflows/qt-editor.yml` for the whole setup, including the DMD output and video
plugins.

## The Win32 editor

The Win32 editor this one replaces ([ColorizingDMD](https://github.com/zesinger/ColorizingDMD) by zesinger, CDMD, last version 3.17.3) is kept at
the tag [`v3.17.3`](https://github.com/PPUC/ColorizingDMD/tree/v3.17.3):

```sh
git checkout v3.17.3            # its sources, to build with Visual Studio
git show v3.17.3:ColorizingDMD.cpp
```

Its tutorials still explain the concepts: a
[comprehensive tutorial](https://www.pincabpassion.net/t15414-comprehensive-tuto-about-colorizingdmd)
and the [videos by Dtatane](https://www.youtube.com/watch?v=PJWOm6L_Lhc&list=PL_BgMMKhwOKJ9EieULq_tjClotEdiR6MO).

## License

PPUC Colorizing DMD is free software: you can redistribute it and/or modify it under the terms
of the GNU General Public License as published by the Free Software Foundation, either version
3 of the License, or (at your option) any later version. See [LICENSE](LICENSE).
