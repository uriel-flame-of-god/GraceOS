# ChibiVM

ChibiVM is a small userland VM toolchain integrated into GraceOS shell workflows.

## Scope

Current implementation is Step 1 and focuses on deterministic, minimal language behavior:

- Integer variables (`int x = 1;`)
- Assignment (`x = 2;`)
- Integer input (`dinput("prompt", x);`)
- Console output (`cprintf("x=${x}\\n");`)

## Shell Commands

- `chivm`
: Opens the interactive ChibiVM REPL.

- `chivm build <source> <output.cmdt>`
: Compiles a source file from BranchFS into CMDT format.

- `run <file.cmdt>`
: Executes CMDT through the process exec path.

## File Formats

ChibiVM uses two file forms:

- Source text files (user authored)
- `.cmdt` compiled output

CMDT is encoded as Intel-HEX-like records. The runtime decodes CMDT and executes the resulting source-compatible instruction stream.

## Example Workflow

```text
mkfile hello.chi 128
quill hello.chi
chivm build hello.chi hello.cmdt
run hello.cmdt
```

## REPL Behavior

Inside `chivm`:

- `help` shows REPL commands
- `reset` clears VM state
- `run <file>` executes a source file from BranchFS
- `exit` returns to GraceOS shell

## Notes and Limits

- Variable table is fixed-size (`CHIVM_MAX_VARS = 64`).
- Source and output buffers are bounded for predictable memory use.
- Runtime currently targets integer-centric scripts; richer control flow is planned for later phases.
- ChibiVM runs in userland workflows and does not bypass SASY/process-manager architecture.
