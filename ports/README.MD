# Ports (BSD-Style)

This directory contains third-party software ports adapted to GraceOS, plus optional GraceOS-native extensions layered on top of the base ports.

For a full overview, see [docs/PORTS.MD](docs/PORTS.MD).

## Layout

Each port should live under its own subdirectory, for example:

- [ports/sqlite](ports/sqlite)
- [ports/7zip](ports/7zip)

## Available Ports

- libsodium ([ports/libsodium](ports/libsodium))
	- Upstream: https://github.com/jedisct1/libsodium.git
	- Output: static library named libsodium.a (build tree)
	- GraceOS patches: shim headers/helpers under [ports/libsodium/grace](ports/libsodium/grace), randombytes in [ports/libsodium/grace/randombytes_grace.c](ports/libsodium/grace/randombytes_grace.c)
	- Known limitations: time-seeded PRNG for randombytes; test suite not wired into build
- 7zip ([ports/7zip](ports/7zip))
	- Upstream: https://github.com/ip7z/7zip.git
	- Output: static library named 7zdec.a (build tree)
	- GraceOS patches: build list in [ports/7zip/grace/BUILD.MD](ports/7zip/grace/BUILD.MD), SHA-256 override in [ports/7zip/grace/sha256_sodium.c](ports/7zip/grace/sha256_sodium.c), SPM gate in [ports/7zip/grace/crypto_gate.c](ports/7zip/grace/crypto_gate.c)
	- Known limitations: decoder-only; no standalone shell command yet; requires SPM permission `crypto/7zip` with `PERM_EXEC`

## Planned Ports

- SQLite ([ports/sqlite](ports/sqlite))
	- Upstream: https://github.com/sqlite/sqlite.git
	- GraceOS patches: not started yet
	- Known limitations: not available yet

## Build Notes

- Build all ports: `make PORTS=1`
- Build a single port: `make port-libsodium` or `make port-7zip`
- The 7zip port depends on libsodium and will build it first

## Guidance

- Keep upstream source and GraceOS-specific patches together.
- Use libtranslate for POSIX-heavy dependencies where possible.
- Document build steps and any required flags in the port directory.
- If you add GraceOS-native extensions (for example, vector acceleration for the SQLite port), document the extension separately from the base port.
