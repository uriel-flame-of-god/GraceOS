# 7-Zip on GitHub
7-Zip website: [7-zip.org](https://7-zip.org)

GraceOS port notes:
- Build the C/ASM CLI (`C/Util/7z/7zMain.c`) and define `GRACEOS_BFS`.
- File I/O is BranchFS-backed and fully buffered in memory.
- Directory creation and file time updates are no-ops for now.
- Start with `7z l` and `7z e` (no directory paths).
- Keep `C/7zAlloc.c` in the build (required by the decoder).
- For encrypted 7z, route crypto through the GraceOS libsodium-backed crypto layer and wire AES in the C decoder.
