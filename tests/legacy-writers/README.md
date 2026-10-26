# Legacy writers (deprecated)

The writers of the legacy project files: `.cRP`, `.cRPX`, `.cRZ` and both together
(`LegacyProjectFileWriter`). The editor no longer writes any of them: projects are made for
libserum 2.9 and saved as project files (`.cdmd`); a machine gets the `.cROMc`
(`File -> Export .cROMc...`). The editor only reads legacy files (`core/legacy`), and its
`writeLegacyCrom()` writes the cROM libserum is handed by the runtime and the `.cROMc` export.

This code stays only because tests still make legacy projects with it: the readers' tests write
rich projects in the Win32 layout and read them back, and `colorizing_qt_v2_roundtrip` checks
that the real 3.17.3 projects read and write back byte-identical.

It is deprecated and is to be removed, together with those uses: the readers' tests then work
from fixed bytes and the real projects. Do not use it in new code or new tests.
