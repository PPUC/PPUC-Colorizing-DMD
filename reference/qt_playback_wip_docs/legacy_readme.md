# PPUC-Serum-Colorizer

PPUC-Serum-Colorizer is a software to colorize VPinMAME DMD using the Serum colorization format.

# Downloads

Click on the "releases" link on the right hand side of this page.

# Build

## Full rebuild
1. `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`
2. `cmake --build build --config Release --target PPUC-Serum-Colorizer`

## macOS (Homebrew)
1. `brew install qt opencv`
2. `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(brew --prefix qt)" -DOpenCV_DIR="$(brew --prefix opencv)/lib/cmake/opencv4"`
3. `cmake --build build`

## Linux (Ubuntu/Debian)
1. `sudo apt-get install -y cmake pkg-config qt6-base-dev qt6-base-dev-tools libqt6opengl6-dev libopencv-dev`
2. `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`
3. `cmake --build build`

## Windows (vcpkg + Visual Studio)
1. Install Visual Studio 2022 with C++ workload.
2. `git clone https://github.com/microsoft/vcpkg.git`
3. `vcpkg\bootstrap-vcpkg.bat`
4. `vcpkg\vcpkg.exe install qtbase opencv --triplet x64-windows`
5. `cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=%CD%\\vcpkg\\scripts\\buildsystems\\vcpkg.cmake`
6. `cmake --build build --config Release`

# PPUC-Serum-Colorizer Serum Editor

To install the editor, just download the zip from the "PPUC-Serum-Colorizer editor" directory and save it anywhere on your disk (DO NOT unzip PPUC-Serum-Colorizer and SortingCDump in the same directory!).

If you are using Windows 10 or more recent, right click on the file and choose "Properties". If in the Properties window, there is a box "Unblock" in the bottom right, check it and click "OK".
Then uncompress the ZIP file.

There is a comprehensive tutorial here https://www.pincabpassion.net/t15414-comprehensive-tuto-about-colorizingdmd

You should also check the videos made by Dtatane here https://www.youtube.com/watch?v=PJWOm6L_Lhc&list=PL_BgMMKhwOKJ9EieULq_tjClotEdiR6MO

# DLL/EXE to use Serum ingame

Depending on whether you are using the 64bit or the 32bit version of VPinMame (if you don't know, chances are that you are using the 32bit version), download respectively the 2 files (DmdDevice.dll and DmdExt.exe) in "DMD-extensions x64" or "DMD-extensions x86" from the [DMD Extensions](https://github.com/freezy/dmd-extensions) download. Then copy them to your "VPinMame" directory.
If you are using Windows 10 or more recent, right click on each file and choose "Properties". If in the Properties window, there is a box "Unblock" in the bottom right, check it and click "OK".
