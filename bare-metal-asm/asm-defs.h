// vim: ft=asm
#pragma once

.altmacro
.syntax	unified


// return and conditional return
.macro ret c=	; bx\c&	lr		; .endm


//-------- macros for defining functions (from bbb-asm-demo) ------------------//

// end the current function
.macro .done
.endm

// redefine .done to do nothing
.macro .donedone
	.purgem .done
	.macro .done
	.endm
.endm

// start a function
.macro .fun label
	.done

	.balign	4
	.global	\label&
	.type	\label&, "function"
\label&:

	.purgem .done
	.macro .done
		.size \label&, . - \label&

		.donedone
	.endm
.endm
