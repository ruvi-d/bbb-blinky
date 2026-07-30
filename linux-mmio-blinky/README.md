# linux-mmio-blinky

The same **GPIO1_28** blinky as [../bare-metal-blinky](../bare-metal-blinky),
but running under Linux on the BeagleBone Black (AM335x) — same pin, same
registers, same write sequence, reached through an MMU mapping of the physical
addresses instead of the raw addresses themselves.

GPIO1_28 is brought out on ball **V18/U18**, pin-mux name **GPMC_BEn1**.

| Project | Runs as | Registers reached with | Built by |
|---------|---------|------------------------|----------|
| [linux-mmio-kernel](linux-mmio-kernel) | A loadable kernel module | `ioremap()` | A BitBake recipe, copied into a Yocto layer |
| [linux-mmio-c](linux-mmio-c) | A userspace process, as root | `mmap()` on `/dev/mem` | `make`, with an `arm-linux-gnueabi-` cross toolchain |
| [linux-mmio-bash](linux-mmio-bash) | A shell script, as root | `devmem2` on `/dev/mem`, one access per invocation | Nothing — copy it over and run it |

These deliberately bypass the kernel subsystems that own the GPIO and pin-mux
registers, which is why they work at all and why they are not patterns to
copy. Each project's README explains what it gives up and what the supported
equivalent would be.
