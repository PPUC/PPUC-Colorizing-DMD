# Copies libraries into a plugin's folder, symbolic links as links (cmake -E copy would copy
# each link as a whole file: libavcodec.dylib, .63.dylib and .63.1.102.dylib three times).
# cmake -DPATTERNS=<glob,glob,...> -DDESTINATION=<folder> -P CopyLibraries.cmake
# The libraries are found here, from glob patterns: a list of every file can be longer than a
# Windows command line takes. The patterns are separated by commas: on Windows the build runs
# the command through cmd.exe, where a "|" (or a ";", for CMake) would split it.
string(REPLACE "," ";" patterns "${PATTERNS}")
file(GLOB files LIST_DIRECTORIES false ${patterns})
if(files)
    file(COPY ${files} DESTINATION "${DESTINATION}")
endif()
