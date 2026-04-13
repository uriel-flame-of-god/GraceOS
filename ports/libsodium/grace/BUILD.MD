# GraceOS libsodium Build Notes

GraceOS builds libsodium as the system crypto library used by other ports (for example, 7zip).

## Scope
- Build the core library only (no tests/tools).
- Prefer a static library for the GraceOS toolchain.

## Build
- Use the port Makefile: `make -C ports/libsodium`

## Defines
- `GRACEOS`
- `CONFIGURED=1`
- `DEV_MODE=1`
- `SODIUM_STATIC`
- `NATIVE_LITTLE_ENDIAN` (x86_64)

## Include Paths
- `ports/libsodium/src/libsodium/include`
- `ports/libsodium/src/libsodium/include/sodium`
- `ports/libsodium/grace`
- `lib/libgrace`

## Sources
- All `ports/libsodium/src/libsodium/**/*.c` sources
- Exclude:
	- `ports/libsodium/src/libsodium/randombytes/sysrandom/randombytes_sysrandom.c`
	- `ports/libsodium/src/libsodium/randombytes/internal/randombytes_internal_random.c`
- Add:
	- `ports/libsodium/grace/randombytes_grace.c`
	- `ports/libsodium/grace/errno.c`

## Notes
- `randombytes_grace.c` uses `time_ms()` as a placeholder seed and is **not** cryptographically secure.
- Link `libgrace` to provide `time_ms()` for the RNG seed.
- Replace the placeholder with a kernel RNG or hardware entropy source when available.
- Keep the build freestanding: no host libc, no pthreads, no dynamic loader.
- Consumers should link against the GraceOS libsodium build rather than bundling their own crypto.
- GraceOS shims provide `assert.h`, `limits.h`, `stddef.h`, `stdint.h`, and `stdlib.h` for freestanding builds.
