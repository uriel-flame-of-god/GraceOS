# ChibiVM

ChibiVM is a small userland VM toolchain integrated into GraceOS shell workflows.

## Types

| Keyword     | Description                                  | Example                          |
|-------------|----------------------------------------------|----------------------------------|
| `int`       | 32-bit signed integer                        | `int x = 42;`                    |
| `bool`      | Boolean (`true` / `false`)                   | `bool flag = true;`              |
| `string`    | Up to 127-character text                     | `string s = "hello";`            |
| `char`      | Single character                             | `char c = 'A';`                  |
| `array`     | Heterogeneous array (any mix of types)       | `array a = [1, "two", true];`    |
| `int[]`     | Homogeneous int array                        | `int[] n = [10, 20, 30];`        |
| `bool[]`    | Homogeneous bool array                       | `bool[] b = [true, false];`      |
| `string[]`  | Homogeneous string array                     | `string[] ss = ["a", "b"];`      |
| `char[]`    | Homogeneous char array                       | `char[] cs = ['x', 'y'];`        |

## Control Flow

```text
if (condition) {
    // ...
} else if (condition) {
    // ...
} else {
    // ...
}
```

### Condition syntax

```text
value op value   — compare two values
value            — truthy check (non-zero int, non-empty string, true bool, etc.)
!value           — negated truthy check
```

Supported operators: `==`  `!=`  `<`  `>`  `<=`  `>=`

## I/O

```text
cprintf("text ${var}\n");     — print with variable interpolation (all types)
dinput("prompt: ", varname);  — read input into a variable (type from the variable)
```

## Shell Commands

- `chivm`
: Opens the interactive ChibiVM REPL.

- `chivm build <source> <output.cmdt>`
: Compiles a source file from BranchFS into CMDT format.

- `run <file.cmdt>`
: Executes a CMDT file through the process exec path.

## File Formats

ChibiVM uses two file forms:

- Source text files (user authored, extension `.chi` by convention)
- `.cmdt` compiled output (Intel HEX encoding of the source text)

Because CMDT encodes source text, all new syntax is automatically supported by the compiler.

## Example: grade checker

```text
int score = 0;
dinput("Enter score: ", score);

if (score >= 90) {
    cprintf("Grade: A\n");
} else if (score >= 75) {
    cprintf("Grade: B\n");
} else if (score >= 50) {
    cprintf("Grade: C\n");
} else {
    cprintf("Grade: F\n");
}
```

## Example: array usage

```text
int[] nums = [10, 20, 30];
cprintf("First: ${nums[0]}\n");
nums[1] = 99;
cprintf("After edit: ${nums}\n");
```

## REPL Behavior

Inside `chivm`:

- `help`         — show command and syntax reference
- `reset`        — clear all variables and array pool
- `run <file>`   — execute a source file from BranchFS
- `exit`         — return to GraceOS shell

Multi-line statements (e.g. `if` blocks) are automatically accumulated until
curly braces are balanced before execution.

## Notes and Limits

- Variable table: `CHIVM_MAX_VARS = 64`
- String length: up to 127 characters
- Array pool: 256 element slots shared across all array variables
- No arithmetic in condition expressions — pre-compute into a variable
- No nested arrays (arrays of arrays)
- ChibiVM runs in userland workflows and does not bypass the SASY/process-manager architecture
