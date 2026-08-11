/* Freestanding startup code for the -mx32df ABI.
   Copyright (C) 2026 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 3, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

Under Section 7 of GPL version 3, you are granted additional
permissions described in the GCC Runtime Library Exception, version
3.1, as published by the Free Software Foundation.

You should have received a copy of the GNU General Public License and
a copy of the GCC Runtime Library Exception along with this program;
see the files COPYING3 and COPYING.RUNTIME respectively.  If not, see
<http://www.gnu.org/licenses/>.  */

/* Entry point for a static -mx32df program linked without a C library.
   Defining X32DF_ELF64 selects the ordinary 64-bit syscall numbers, for
   running -mx32df code inside an ELF64 container on a kernel without x32
   support; the default uses the x32 syscall convention.  */

#ifdef __X32DF__

/* The ordinary 64-bit syscall numbers are used throughout: an x32 process may
   invoke the 64-bit syscall table directly, and exit takes only a scalar, so
   the same object works in a true x32 process and in an ELF64 container.  */
#define SYS_EXIT	60			/* __NR_exit */

	.section .bss
	.align 16
	.globl __x32df_lowstack_lo
__x32df_lowstack_lo:
	.space 262144
	.globl __x32df_lowstack_hi
__x32df_lowstack_hi:

	.text
	.globl _start
	.type _start,@function
_start:
	xorl	%ebp, %ebp		/* outermost frame */

	/* -mx32df is x32 codegen: the compiler adjusts the stack pointer with
	   32-bit instructions (subl $N,%esp), so the stack must live below
	   4GiB.  Under a real x32 kernel it already does and we keep the
	   process stack (so argc/argv remain reachable).  Inside an ELF64
	   container the kernel puts the stack near 0x7fff_xxxx_xxxx, which a
	   32-bit RSP update would truncate, so switch to a low stack in .bss.  */
	movq	%rsp, %rax
	shrq	$32, %rax
	jnz	.Lswitch_stack

	/* Low stack already: hand main the real argc/argv.  The initial process
	   stack of an ELF32 (x32) image uses 4-byte slots.  */
	movl	(%rsp), %edi		/* argc */
	leal	4(%rsp), %esi		/* argv */
	andq	$-16, %rsp
	jmp	.Lcall_main

.Lswitch_stack:
	movl	$__x32df_lowstack_hi, %esp	/* zero-extends: a <4GiB stack */
	andq	$-16, %rsp
	xorl	%edi, %edi		/* argc = 0    */
	xorl	%esi, %esi		/* argv = NULL: the real vector lives
					   above 4GiB and is unreachable from
					   32-bit pointers.  */

.Lcall_main:
	/* The CALL below pushes 8 bytes, leaving RSP%16==8 at main's entry,
	   which is what the SysV ABI requires.  */
	call	main
	movl	%eax, %edi		/* exit status = main's return value */
	movl	$SYS_EXIT, %eax
	syscall
	hlt
	.size _start, .-_start

#endif /* __X32DF__ */

	.section	.note.GNU-stack,"",@progbits
