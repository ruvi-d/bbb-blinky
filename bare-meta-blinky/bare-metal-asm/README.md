# bare-metal-asm

Hand-written ARM assembly implementation of the blinky described in
[../README.md](../README.md) — start there for what it does, how it's
built, and how the boot image is assembled.

## Link map ([../blinky.ld](../blinky.ld))

Shared with [../bare-metal-c](../bare-metal-c). There is no `.bss` or stack
region here: `blinky.S` never touches `sp` or holds any mutable state. The
linker script does export a `__stack_top` symbol, but that's only for
`bare-metal-c`'s entry code — this project never references it.
