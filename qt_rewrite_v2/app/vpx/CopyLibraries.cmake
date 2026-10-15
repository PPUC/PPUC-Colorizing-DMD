# Copies libraries into a plugin's folder, symbolic links as links (cmake -E copy would copy
# each link as a whole file: libavcodec.dylib, .63.dylib and .63.1.102.dylib three times).
# cmake -DFILES=<a|b|...> -DDESTINATION=<folder> -P CopyLibraries.cmake
string(REPLACE "|" ";" files "${FILES}")
file(COPY ${files} DESTINATION "${DESTINATION}")
