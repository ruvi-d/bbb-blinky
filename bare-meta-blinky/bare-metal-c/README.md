# bare-metal-c

C port of [../bare-metal-asm](../bare-metal-asm) — see
[../README.md](../README.md) for what it does, how it's built, and how the
boot image is assembled. This file only covers what's specific to the C
port.

## Why this needs more than the asm version

`blinky.S` never sets up a stack — it holds no state and its `bl`/`ret` pair
doesn't need one. C code can't make that assumption: even though nothing
here uses stack memory for locals in any meaningful way, GCC's calling
convention still expects a valid `sp` before the first `bl`. So `main.c`
adds one thing the asm version didn't need: a tiny naked `_start` that loads
`sp` from a linker-provided `__stack_top` symbol and branches into `blink()`,
which then does the exact same four steps as the asm version.

`blink()` is marked `__attribute__((used))` because it's only referenced
from inline asm inside `_start` — invisible to the optimizer, which would
otherwise treat it as dead code and drop it (confirmed: without this, the
linker fails with `undefined reference to 'blink'`).

## Link map ([../blinky.ld](../blinky.ld))

Shared with [../bare-metal-asm](../bare-metal-asm) — same `a8ram` region,
same `0x402f0400` load address — plus one addition this project relies on
and `bare-metal-asm` doesn't:

```
__stack_top = ORIGIN( a8ram ) + LENGTH( a8ram );
```

Still no `.bss`: there are no mutable globals, only the stack.

## Verifying against the asm version

```bash
arm-linux-gnueabi-objdump -d bin/blinky.elf
arm-linux-gnueabi-objdump -d ../bare-metal-asm/bin/blinky.elf
```

Both hit the same four MMIO addresses with the same immediate values in the
same order, and `_start` is first in both images. The C binary comes out
larger than the asm one (GCC inlines `delay()` at both call sites, and the
`volatile` loop counter forces a stack spill/reload every iteration instead
of staying in a register) — that overhead is expected and doesn't change
behavior.
