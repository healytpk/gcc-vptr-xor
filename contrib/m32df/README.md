# `-m32df`: dual function pointers on an i386 base

`-m32df` is an x86 ABI in which **data pointers are 4 bytes but function
pointers are 8**. It exists as an implementation proof for the proposal to add
`intfptr_t` / `uintfptr_t` to `<cstdint>`: the standard defines `[u]intptr_t`
for *object* pointers only, so on a target like this one no standard integer
type is guaranteed to round-trip a function pointer.

| type | size |
|------|------|
| `void *`, `int *` | **4** |
| `long`, `size_t` | **4** |
| function pointer | **8** |
| `uintptr_t` | 4 — too narrow to hold a function pointer |
| `uintfptr_t` / `intfptr_t` | **8** |

The base is i386, so a `-m32df` program is an ordinary `ELF 32-bit, Intel
80386` executable. It runs on any x86-64 Linux, because ia32 emulation is
essentially always enabled — unlike the x32 ABI, whose process support is
compiled out or disabled by default on current kernels.

## The dual pointer

A function pointer is 64 bits, and the upper half is not padding:

```
 63                      32 31                    1   0
+--------------------------+----------------------+---+
|   stack_top (upper bits) | stack_top (lower)    |abi|
+--------------------------+----------------------+---+
|                     entry (32)                       |
+------------------------------------------------------+
```

* **entry** — the 32-bit code address of the callee.
* **stack_top** — a 16-byte-aligned address of the top of a heap-allocated
  stack the call should run on, or 0 to use the caller's stack. (16-aligned
  means the low four bits are free, so bit 0 can carry the tag.)
* **abi** — 0 = System V, 1 = Microsoft. The *caller* places arguments per the
  chosen convention; the runtime helper performs only the call mechanism.

A plain function address such as `&f` has a zero upper half — "here, now,
default convention" — so ordinary function pointers behave exactly as usual.

## How a dual call is lowered

Under `-m32df` every indirect call goes through the `__dualcall` helper in
libgcc. The compiler emits, at the call site:

```
	movl	<entry>, %eax
	movl	<stack|abi>, %edx	# the dual pointer, as a DImode pair
	movl	$<argbytes>, %ecx
	call	__dualcall
```

`EDX:EAX` is simply how i386 holds a 64-bit value, so the dual pointer arrives
split for free. Direct calls are untouched, and a dual pointer is never
tail-called — the helper has to regain control to restore the stack.

The i386 difference worth knowing: **arguments are passed on the stack**, so
unlike a register-argument target the helper cannot just move `%esp` — that
would strand them. The compiler therefore also passes the outgoing argument
block size in `%ecx`, and `__dualcall` relocates that block onto the stack it
swaps in. Argument lists of any size work.

## Which C library

`m32`, `m64` and `mx32` use glibc exactly as before. `m32df` cannot: widening
function pointers changes the size of every structure holding one, and each
indirect call across the library boundary would disagree about the pointer's
width. No newlib port for this ABI exists either, so `m32df` links against the
small C library in `libgcc/config/i386/m32df-libc.c` — `printf`, `malloc`, the
`mem*`/`str*` family, the syscall layer, and the C++ runtime support routines
(`new`/`delete`, guard variables, `__cxa_atexit`, static construction).

It lives in libgcc rather than a separate target library for a structural
reason: libgcc is built for every multilib, and the file guards itself with
`#ifdef __M32DF__`, so it compiles to **nothing** in the other multilibs — 42
global symbols under `-m32df`, zero elsewhere. No library on the search path
can shadow glibc. `LIB_SPEC` for `m32df` is correspondingly empty; the driver
links libgcc regardless.

This also sidesteps a limitation of GCC's build system: a target library is
always built for the *default* multilib as well as each configured one, so a
real newlib could not have been confined to `m32df` alone — its `libc.a` would
have installed where the driver finds it ahead of glibc and hijacked `-m64`.

## Building programs

```sh
gcc -m32df demo.c -o demo
./demo
```

```
-m32df
  sizeof(void*)      = 4
  sizeof(fn pointer) = 8
  sizeof(uintptr_t)  = 4  (cannot hold a function pointer)
  sizeof(uintfptr_t) = 8  (can)
  libc: "malloc works" (12 chars) at 0x...
  plain call: ran on the current stack  OK
  dual  call: ran on the carried stack  OK
PASS
```

The driver forces `-static` (there is no dynamic C library for this ABI),
`-nolibc` (which suppresses the `-latomic_asneeded` the generic GNU/Linux link
sequence would add; libatomic is not built for `m32df`, as it needs a hosted
libc and pthreads) and `-no-pie`, since `__dualcall` plants an absolute address
for its return trampoline.

### C++

libstdc++ is not built for `m32df` either, and the `g++` driver appends
`-lstdc++` as a command-line argument rather than through a spec, so no spec
can remove it. Drive the compiler through `gcc -x c++`:

```sh
gcc -m32df -x c++ -fno-exceptions -fno-rtti tester.cpp -o prog
```

`-fno-exceptions -fno-rtti` are required: throwing needs the unwinder and
type-info machinery that live in libstdc++/libsupc++. Supported: `new` and
`delete`, virtual functions, function-local statics, objects of static storage
duration with constructors and destructors, `atexit`. Templates, classes and
`constexpr` are front-end features and always worked. Not available: the
standard *library* — use the C entry points declared `extern "C"`.

Static construction exposes the ABI split nicely. GCC emits `.init_array`
entries as `.long` — *address*-sized, because an address is 32 bits here —
while a function pointer is 64 bits. The runtime reads those arrays as 32-bit
words and widens each before calling; treating them as an array of function
pointers would stride by 8 and read garbage. With no hosted libc to run them,
`crt0-m32df.S` calls `__m32df_run_init_array` before `main`, and `exit` runs
the destructors registered by `__cxa_atexit` followed by `.fini_array`.

## Files

| path | purpose |
|---|---|
| `libgcc/config/i386/dualcall.S` | the `__dualcall` call-lowering helper |
| `libgcc/config/i386/crt0-m32df.S` | freestanding startup code (`crt0.o`) |
| `libgcc/config/i386/m32df-libc.c` | the minimal C library |
| `libgcc/config/i386/t-m32df` | builds all three for the `m32df` multilib |
| `contrib/m32df/demo.c` | the demo above |
| `gcc/testsuite/gcc.target/i386/m32df-{abi,dualcall}.c` | compile tests |

## Limitations

* A callee reached through a dual pointer may not use `-mregparm`: `%ecx` is
  scratch in the call sequence.
* The ABI tag selects the caller's argument placement only; Microsoft-x64
  shadow space is not reserved, so use the carried-stack path with System V
  callees.
* The callee must respect the size of the stack the pointer carries.
