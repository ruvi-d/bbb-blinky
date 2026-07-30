SUMMARY = "GPIO1_28 LED blinker out-of-tree kernel module, direct MMIO"
DESCRIPTION = "A Linux kernel module that blinks the LED on AM335x GPIO1_28 at 1 Hz by \
               writing the GPIO and pin-mux registers directly through ioremap(), using \
               the same register sequence as the bare-metal blinky it is derived from. \
               Blinking starts on insmod and stops on rmmod."
HOMEPAGE = "https://github.com/ruvi-d/bbb-blinky"
LICENSE = "GPL-2.0-only"
# nooelint: oelint.var.licenseremotefile
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/GPL-2.0-only;md5=801f80980d171dd6425610833a22dbe6"

inherit module

SRC_URI = "file://Makefile \
           file://kblinky-mmio.c \
           "

# Sources are built where they are unpacked. On Yocto 5.1 (styhead) and newer
# this needs to be "${UNPACKDIR}" instead.
S = "${WORKDIR}"

# Lets an image pull the module in as kernel-module-kblinky-mmio, matching the
# naming that in-tree modules get.
RPROVIDES:${PN} += "kernel-module-kblinky-mmio"

# Hardcoded AM335x register addresses, and it relies on gpio-omap having
# already ungated the GPIO1 clock -- so it is only meaningful on an AM335x
# machine whose kernel has CONFIG_GPIO_OMAP. BitBake skips the recipe for any
# other MACHINE rather than building a module that would fault on load.
# Adjust to match your MACHINE.
COMPATIBLE_MACHINE = "^(dogbonedark)$"
