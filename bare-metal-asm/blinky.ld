/* Minimal link map for AM335x peripheral / memory boot.
 *
 * The ROM bootloader loads a single contiguous segment and jumps to its
 * start.  For peripheral boot (UART/Ethernet/USB) that address is the
 * hardcoded 0x402f0400 in the Cortex-A8 local RAM; the same address is used
 * for the memory-boot (MLO) load address (see the Makefile). */

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

	/* this app has no stack/bss: it never touches sp and holds no state */

	/DISCARD/ : { *( .comment .ARM.attributes .note.* ) }
}
