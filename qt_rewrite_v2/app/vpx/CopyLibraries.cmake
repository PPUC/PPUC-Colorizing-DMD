# Copies libraries into a plugin's folder, symbolic links as links (cmake -E copy would copy
# each link as a whole file: libavcodec.dylib, .63.dylib and .63.1.102.dylib three times).
# cmake -DPATTERNS=<glob|glob|...> -DDESTINATION=<folder> -P CopyLibraries.cmake
# The libraries are found here, from glob patterns: a list of every file can be longer than
# the 8191 characters a Windows command line takes.
string(REPLACE "|" ";" patterns "${PATTERNS}")
file(GLOB files LIST_DIRECTORIES false ${patterns})
if(files)
    file(COPY ${files} DESTINATION "${DESTINATION}")
endif()
