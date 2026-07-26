/* Minimal link map for AM335x peripheral / memory boot.
 * Shared by bare-metal-asm/ and bare-metal-c/ (both sit alongside this file).
 *
 * The ROM bootloader loads a single contiguous segment and jumps to its
 * start.  For peripheral boot (UART/Ethernet/USB) that address is the
 * hardcoded 0x402f0400 in the Cortex-A8 local RAM; the same address is used
 * for the memory-boot (MLO) load address (see each project's Makefile). */

MEMORY
{
	/* Cortex-A8 local RAM -- 64 KB (first 1 KB reserved/inaccessible) */
	a8ram : org = 0x402F0000, len = 0x10000
}

PHDRS { image PT_LOAD; }

ENTRY( _start )

SECTIONS
{
	/* reserve the secure/ROM-owned first 1 KB */
	.reserved 0x402F0000 (NOLOAD) : { . += 0x400; } >a8ram :NONE

	. = 0x402F0400;
	.text . : {
		KEEP( *( .text._start ) )	/* entrypoint must come first */
		*( .text .text.* )
	} :image

	.rodata : { *( .rodata .rodata.* ) } :image
	.data   : { *( .data .data.* ) }     :image

	/* __stack_top: used by bare-metal-c's _start, which needs a valid sp
	 * before its first `bl` -- points at the top of RAM, growing down.
	 * bare-metal-asm never references it: it never touches sp and holds no
	 * state. no .bss either way: no mutable globals anywhere. */
	__stack_top = ORIGIN( a8ram ) + LENGTH( a8ram );

	/DISCARD/ : { *( .comment .ARM.attributes .note.* ) }
}
