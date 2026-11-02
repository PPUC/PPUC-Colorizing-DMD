# Legacy file fixtures

`legacy/` holds fixed `.cROM`, `.cRZ`, `.cRP` and `.cRPX` files. The editor only reads these
formats, so the tests of the readers work from these files. They are not regenerated: they were
written once by the writers the editor had until they were removed, and stand for the projects
that exist. Only add a file when a reader needs a case they do not cover.

| Files | Project |
|---|---|
| `crp-plain`, `crpx-plain` | The synthetic project of `LegacyProjectFileReaderTests.cpp`, as `.cRP` and `.cRPX` |
| `crpx-chunks` | The same with helper lines, saved zones, tags, a PuP pack and scenes |
| `crp-pup` | The same in a `.cRP`: the PuP pack name in the Win32 field |
| `crpx-noscenes` | The same with colorization settings without scenes |
| `crpx-libserum29` | For libserum 2.9: 300 sprites, 128 slots; its big cROM as a `.cRZ` |
| `crz.cRZ`, `zip64.cRZ` | Archives: a cROM under an unrelated entry name, and zip64 of 3000 bytes of 7 |
| `ordinary`, `original256x64` | `makeSnapshot()` and its 256x64 form of `LegacyProjectSerializerTests.cpp` |
| `native192x64` | `makeSnapshot()` of `NativeOriginalProjectTests.cpp`, with an extra plane |
| `full` | `makeFullProject(PlaneLayout::NativeExtra)` of `RuntimeFixtures.h` |
| `license`, `lights` | The `.cRPX` chunks `LICN` and `LGHT`, see `LicenseTests.cpp`, `SpeakerLightTests.cpp` |
