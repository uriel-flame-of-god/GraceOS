# GraceOS 7zDec Build Notes

This port uses the C-only 7zDec entrypoint with BranchFS-backed I/O.

## Defines
- `GRACEOS_BFS`
- `Z7_EXTRACT_ONLY`
- `Z7_PPMD_SUPPORT`
- `Z7_ST`

## Minimal C source list (extract/list)
- `C/Util/7z/7zMain.c`
- `C/7zAlloc.c`
- `C/7zArcIn.c`
- `C/7zBuf.c`
- `C/7zBuf2.c`
- `C/7zCrc.c`
- `C/7zDec.c`
- `C/7zFile.c`
- `C/7zStream.c`
- `C/Bcj2.c`
- `C/Bra.c`
- `C/Bra86.c`
- `C/BraIA64.c`
- `C/CpuArch.c`
- `C/Delta.c`
- `C/Lzma2Dec.c`
- `C/LzmaDec.c`
- `C/Ppmd7.c`
- `C/Ppmd7Dec.c`

## Notes
- File I/O is fully buffered in memory; output is committed to BranchFS on close.
- Directory creation and file time updates are no-ops for now.
- Start with `7z l` and `7z e` (no directory paths).

## Allocator
- Keep `C/7zAlloc.c` in the build; it is required by the decoder.

## Encryption (encrypted 7z)
- GraceOS crypto is libsodium-backed; 7zip crypto should call the GraceOS crypto layer.
- Link against the GraceOS libsodium build when enabling encryption.
- AES method wiring is not enabled in the C-only decoder yet; this is the next step for encrypted archives.
