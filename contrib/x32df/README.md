# Running `-mx32df` programs

Everything needed is in the tree; no files have to be copied in.

* `libgcc/config/i386/dualcall.S` — the `__dualcall` call-lowering helper.
* `libgcc/config/i386/crt0-x32df.S` — freestanding startup code (`crt0.o`).
* `libgcc/config/i386/x32df-libc.c` — the minimal C library for this ABI.
* `libgcc/config/i386/t-x32df` — builds all three for the `mx32df` multilib.
* `contrib/x32df/demo.c` — the demo below.
* `contrib/x32df/x32probe.S` — kernel x32-support probe.
* `gcc/testsuite/gcc.target/i386/x32df-{abi,dualcall}.c` — the compile tests.

## Which C library

`m32`, `m64` and `mx32` use glibc, exactly as they always did. `mx32df` cannot:
widening function pointers to 64 bits changes the size of any structure holding
one, and every indirect call across the library boundary would disagree about
the pointer's width. No newlib port for this ABI exists either, so `mx32df`
links against the small C library in `libgcc/config/i386/x32df-libc.c`, which
provides `printf`, `malloc`, the `mem*`/`str*` family and the syscall layer.

It lives in libgcc rather than in a separate target library for a structural
reason. libgcc is built for every multilib, and the file guards itself with
`#ifdef __X32DF__`, so it compiles to **nothing** in the m32, m64 and mx32
multilibs — verified: 27 global symbols under `-mx32df`, zero under the others.
There is therefore no library anywhere on the search path that could shadow
glibc. `LIB_SPEC` for `mx32df` is correspondingly empty; the driver links
libgcc anyway.

This also sidesteps a real limitation of GCC's build system: a target library
is always built for the *default* multilib as well as each configured one, so a
genuine newlib could not have been restricted to `mx32df` alone — its `libc.a`
would have installed where the driver finds it ahead of glibc and hijacked
`-m64`.

Below, `$B` is your build directory (`$B/gcc/xgcc -B$B/gcc`) and `$S` the source
tree. Once libgcc has been built for the `mx32df` multilib, `crt0.o` and
`__dualcall` come from libgcc and drop out of these command lines.

## Path B — ELF64 container, runs on any x86-64 kernel

Start here: it needs no special kernel configuration.

```sh
$B/gcc/xgcc -B$B/gcc -mx32df -O2 -ffreestanding -fno-builtin -nostdlib -no-pie \
    -DX32DF_ELF64 -Wa,--64 -Wl,-m,elf_x86_64 \
    $S/libgcc/config/i386/crt0-x32df.S $S/contrib/x32df/demo.c \
    $S/libgcc/config/i386/x32df-libc.c $S/libgcc/config/i386/dualcall.S -o demo
./demo
```

```
-mx32df
  sizeof(void*)      = 4
  sizeof(fn pointer) = 8
  sizeof(uintptr_t)  = 4  (cannot hold a function pointer)
  sizeof(uintfptr_t) = 8  (can)
  libc: "malloc works" (12 chars) at 0x...
  plain call: ran on the current stack  OK
  dual  call: ran on the carried stack  OK
PASS
```

Once libgcc has been built for the `mx32df` multilib, `crt0.o`, `__dualcall`
and the C library all come from libgcc and the command collapses to
`xgcc -mx32df demo.c -o demo`.

The ABI is unchanged — 4-byte data pointers, 8-byte function pointers, calls
lowered through `__dualcall`. Only the container and the syscall convention
differ. Two constraints, both handled for you:

* everything must sit below 4 GiB, hence `-no-pie`;
* the *stack* must too. `-mx32df` is x32 codegen, so the compiler adjusts the
  stack pointer with 32-bit instructions (`subl $8,%esp`), which truncates a
  high address — and an ELF64 process stack sits near `0x7fff_xxxx_xxxx`.
  `crt0-x32df.S` detects this at runtime and switches to a low `.bss` stack.
  (`__dualcall` itself saves and restores RSP with a full 64-bit `movq`, so it
  is correct either way.)

## Path A — true x32, ELF32

```sh
$B/gcc/xgcc -B$B/gcc -mx32df -O2 -ffreestanding -fno-builtin -nostdlib \
    $S/libgcc/config/i386/crt0-x32df.S $S/contrib/x32df/demo.c \
    $S/libgcc/config/i386/x32df-libc.c $S/libgcc/config/i386/dualcall.S -o demo
./demo
```

This is the principled target for the ABI: a genuine x32 binary
(`ELF 32-bit LSB executable, x86-64`) using x32 syscall numbers. The kernel
places the whole address space below 4 GiB, so `crt0-x32df.S` keeps the real
process stack and passes `argc`/`argv` through.

It needs a kernel built with `CONFIG_X86_X32_ABI`; many distributions ship with
it off, and Linux 5.18+ may also want `syscall.x32=y` on the kernel command
line. Check first:

```sh
gcc -mx32 -nostdlib -static contrib/x32df/x32probe.S -o x32probe && ./x32probe; echo $?
```

`42` means x32 works. `126` with *Exec format error* means it does not — use
Path B, or enable x32 and retry.

Path A was **not executed** where these files were developed: that kernel had no
x32 support. It is verified only as far as producing a correctly-formed x32
executable. Path B was run, and passes.

## What the demo proves

* `sizeof (void *) == 4` while `sizeof (void (*) (void)) == 8`, checked by
  `_Static_assert` so the program would not compile otherwise;
* `uintptr_t` is 4 bytes and cannot round-trip a function pointer, while
  `uintfptr_t` is 8 and can;
* a call through a dual pointer whose high 32 bits carry a stack address really
  does execute on that stack and return correctly, while a plain function
  pointer (high half zero) runs on the current stack.

That last point is the end-to-end join: the compiler emits
`movq <ptr>, %r11 ; call __dualcall`, and the libgcc helper performs the swap.
