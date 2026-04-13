# GraceOS Ports

This document describes the BSD-style ports tree for third-party software that is adapted to GraceOS.

## Overview

Ports live under the top-level ports/ directory. Each port contains upstream source code plus GraceOS-specific patches and shims.

## Build

Build all ports:

```cmd
make PORTS=1
```

Build a single port:

```cmd
make port-libsodium
make port-7zip
```

Notes:
- port-7zip depends on port-libsodium and will build it first.
- Ports are optional and built separately from the kernel.

## Current Ports

### libsodium

- Upstream: https://github.com/jedisct1/libsodium.git
- Location: [ports/libsodium](ports/libsodium)
- Output: static library named libsodium.a (build tree)
- Purpose: crypto library for userland ports
- GraceOS patches:
	- GraceOS shim headers and helpers under [ports/libsodium/grace](ports/libsodium/grace)
	- Custom randombytes provider in [ports/libsodium/grace/randombytes_grace.c](ports/libsodium/grace/randombytes_grace.c)
- Known limitations:
	- randombytes is a time-seeded PRNG until a kernel entropy source exists
	- Upstream test suite is not wired into the GraceOS build yet

### 7zip

- Upstream: https://github.com/ip7z/7zip.git
- Location: [ports/7zip](ports/7zip)
- Output: static library named 7zdec.a (build tree)
- Scope: C-only decoder build
- I/O: BranchFS-backed file access
- Crypto: uses libsodium for SHA-256 while keeping 7zip AES for encryption
- Security: gated by SPM permission `crypto/7zip` with `PERM_EXEC`
- Userland tool: `7c` in [userland/7z](userland/7z)
  - ZIP store-only encode/decode (no compression)
  - Default decode outdir: `result`
  - `--overwrite` requires `PERM_WRITE` on the existing output path
- GraceOS patches:
	- Port build list in [ports/7zip/grace/BUILD.MD](ports/7zip/grace/BUILD.MD)
	- SHA-256 override in [ports/7zip/grace/sha256_sodium.c](ports/7zip/grace/sha256_sodium.c)
	- SPM crypto gate in [ports/7zip/grace/crypto_gate.c](ports/7zip/grace/crypto_gate.c)
	- UTIME_OMIT fallback in [ports/7zip/C/Util/7z/7zMain.c](ports/7zip/C/Util/7z/7zMain.c)
- Known limitations:
	- 7z is decoder-only (no encoder)
	- `7c` ZIP support is store-only (no compression)
	- ZIP data descriptors and encryption are not supported yet
	- Requires SPM permission `crypto/7zip` with `PERM_EXEC` for hashing

## Planned Ports

### SQLite

- Upstream: https://github.com/sqlite/sqlite.git
- Location: [ports/sqlite](ports/sqlite)
- GraceOS patches: not started yet
- Known limitations: not available yet

## Porting Guidance

- Keep upstream source and GraceOS-specific code together.
- Use libtranslate for POSIX-heavy dependencies where possible.
- Document any GraceOS-specific behavior in the port directory.
