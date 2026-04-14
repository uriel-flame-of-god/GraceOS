# GraceOS Security Policy Manager (SPM)

## Overview

The Security Policy Manager (SPM) is a kernel subsystem responsible for defining and enforcing security policies that govern process behaviour in GraceOS.

SPM is initialised **before** the Process Manager during boot. This ordering guarantees that the policy framework is in place before any process objects are created or scheduled.

Source files:
- `kernel/spm/spm.c` — implementation
- `kernel/spm/spm.h` — public API and policy structures
- `kernel/sys/sys_spm.c` — syscall interface

## Role in the Kernel

SPM acts as the security foundation for the executive layer:

- Establishes the security policy table at boot
- Provides a validation interface that the Process Manager consults when creating or modifying processes
- Exposes security operations to userland via a dedicated syscall group

SPM complements the per-process capability flags (`CAP_*`) and `doas` enforcement implemented in `kernel/proc/security.c`. Where `security.c` handles per-process runtime checks, SPM manages the global policy rules that those checks consult.

SPM does **not** replace the Process Manager — it is a peer subsystem that the Process Manager depends on.

## Initialisation

SPM is initialised in `kmain()` with a single call:

```c
spm_init();
```

The boot log records this step:

```
[INIT] Initializing spm security subsystem
```

### Position in the Boot Sequence

```
sasy_init          ← Segment allocator ready
spm_init           ← Security policies loaded   (HERE)
proc_init          ← Process table created
exec_init          ← ELF loader ready
sched_init         ← Scheduler ready
pipe_init          ← IPC pipes ready
minit_init         ← Orchestration layer ready
```

SPM must come before `proc_init` so that any process creation path can immediately query policy without checking whether SPM is ready.

## API

### spm_init

```c
void spm_init(void);
```

Initialises the security policy manager. Sets up internal policy tables and prepares the subsystem to answer policy queries. Called once during `kmain`.

### Syscall Interface

SPM exposes security policy operations to userland through `kernel/sys/sys_spm.c`. These syscalls allow privileged processes to query or update policy entries without reaching directly into kernel memory.

The syscall handler is registered during `syscall_init()` and dispatched through the standard INT 0x80 ABI.

Specific syscall numbers for SPM operations are defined in `kernel/spm/spm.h` and the syscall dispatcher in `kernel/sys/syscall.c`.

## Userland Interface (`spm`)

GraceOS ships a userland CLI wrapper for SPM in `userland/spm/spm.c`. It talks to the SPM syscall group and exposes basic user + capability management from the shell.

Supported commands:

```
spm user add <username> --uid <uid>
spm user list
spm user passwd <username>
spm perm grant <user> <perm> <target>
spm perm list <user>
spm can [--as <user>] <perm> <target>
spm whoami
```

Permission names accepted by `spm perm grant` and `spm can`:

```
read | write | exec | spawn | kill | pause | grant | admin | all
```

## Design Notes

- SPM follows the GraceOS principle of **explicit subsystem boundaries**: it owns policy data and no other subsystem writes policy tables directly.
- Policy enforcement is synchronous — queries return immediately with an allow/deny result.
- SPM does not perform scheduling or memory allocation after `spm_init()` returns; it is a read-mostly subsystem at runtime.
- Future work may add policy persistence (loading rules from BranchFS at boot) and audit logging.
