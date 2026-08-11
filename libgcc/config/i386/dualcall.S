/* __dualcall -- call-lowering support routine for the -mx32df ABI.
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

/* Only the -mx32df multilib has 64-bit "dual" function pointers and therefore
   emits calls to this routine; in every other multilib this file assembles to
   nothing.  */

#ifdef __X32DF__

	.text
	.globl	__dualcall
	.type	__dualcall, @function
	.p2align 4
__dualcall:
	.cfi_startproc
	/* %r11 = dual pointer.  Unpack entry and stack_top. */
	movl	%r11d, %eax		/* %eax = entry (low 32), zero-extended */
	shrq	$32, %r11		/* %r11 = high word: (stack_top | abi) */
	andl	$0xFFFFFFFE, %r11d	/* %r11 = stack_top (strip the abi tag bit) */
	jz	.Lcurrent		/* stack_top == 0 => run on current stack */

	/* --- swap onto the carried heap stack for this call --- */
	movq	%rsp, %r10		/* %r10 = caller RSP */
	leaq	-24(%r11), %rsp		/* RSP = stack_top - 24.  stack_top is
					 * 16-aligned, so RSP % 16 == 8, matching
					 * the SysV entry invariant (RSP+8 aligned). */
	movq	%r10, 8(%rsp)		/* [stack_top-16] = caller RSP (above ret slot) */
	leaq	.Lreturn(%rip), %r10
	movq	%r10, (%rsp)		/* [stack_top-24] = return trampoline; this is
					 * the return address the callee's RET will pop. */
	jmp	*%rax			/* enter the callee on the heap stack */

.Lcurrent:
	/* No custom stack: tail-jump to the callee on the current stack.
	 * __dualcall was entered via CALL, so the callee's RET returns straight
	 * to the real caller. */
	jmp	*%rax

.Lreturn:
	/* The callee has returned here on the heap stack with RSP == stack_top-16
	 * and its return value in RAX/RDX/XMM0-1.  Restore the caller's RSP and
	 * return, without disturbing any value register. */
	movq	(%rsp), %rsp		/* RSP = caller RSP */
	ret				/* return to the real caller */
	.cfi_endproc
	.size	__dualcall, .-__dualcall

	/* Not an executable stack. */

#endif /* __X32DF__ */

	/* Not an executable stack.  */
	.section	.note.GNU-stack,"",@progbits
