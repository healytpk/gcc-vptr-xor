/* Does your kernel support the x32 ABI?
     gcc -mx32 -nostdlib -static x32probe.S -o x32probe && ./x32probe; echo $?
   42  = yes, x32 works -> use the ELF32/x32 path (crt0.S + demo.c)
   126 = "Exec format error" -> no x32 support; use the ELF64 path
         (crt064.S + demo64.c), or boot with syscall.x32=y  */
	.text
	.globl _start
_start:
	movl	$42, %edi
	movl	$0x4000003c, %eax
	syscall
	hlt
	.section	.note.GNU-stack,"",@progbits
