/* Definitions for AMD x86-64 running Linux-based GNU systems with ELF format.
   Copyright (C) 2001-2026 Free Software Foundation, Inc.
   Contributed by Jan Hubicka <jh@suse.cz>, based on linux.h.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

Under Section 7 of GPL version 3, you are granted additional
permissions described in the GCC Runtime Library Exception, version
3.1, as published by the Free Software Foundation.

You should have received a copy of the GNU General Public License and
a copy of the GCC Runtime Library Exception along with this program;
see the files COPYING3 and COPYING.RUNTIME respectively.  If not, see
<http://www.gnu.org/licenses/>.  */

#define GNU_USER_LINK_EMULATION32 "elf_i386"
#define GNU_USER_LINK_EMULATION64 "elf_x86_64"
#define GNU_USER_LINK_EMULATIONX32 "elf32_x86_64"

#define GLIBC_DYNAMIC_LINKER32 "/lib/ld-linux.so.2"
#define GLIBC_DYNAMIC_LINKER64 "/lib64/ld-linux-x86-64.so.2"
#define GLIBC_DYNAMIC_LINKERX32 "/libx32/ld-linux-x32.so.2"

#undef MUSL_DYNAMIC_LINKER32
#define MUSL_DYNAMIC_LINKER32 "/lib/ld-musl-i386.so.1"
#undef MUSL_DYNAMIC_LINKER64
#define MUSL_DYNAMIC_LINKER64 "/lib/ld-musl-x86_64.so.1"
#undef MUSL_DYNAMIC_LINKERX32
#define MUSL_DYNAMIC_LINKERX32 "/lib/ld-musl-x32.so.1"

/* --- -m32df links against newlib, not glibc. ----------------------------
   The m32df ABI widens function pointers to 64 bits, so it is ABI-
   incompatible with any stock C library: every call through a function
   pointer across the library boundary would disagree about the pointer's
   width.  It therefore gets its own C library (newlib, built with -m32df),
   kept in its own os-directory (../libm32df, see t-linux64) so that "-lc"
   under -m32df resolves to newlib there while -m64/-m32/-mx32 keep finding
   glibc in lib64/lib/libx32 -- the two C libraries never collide on the
   search path.

   All that remains is to switch the *conventions* that differ between a
   glibc-hosted binary and a static newlib one:

     * force a static link for -m32df.  Because the dynamic linker in
       LINK_SPEC is emitted only inside %{!static:...}, this also drops the
       PT_INTERP / -dynamic-linker automatically, and makes
       LINK_GCC_C_SEQUENCE_SPEC wrap libc/libgcc in --start-group (newlib's
       libc and its OS-glue are mutually recursive);
     * use newlib's crt0 in place of glibc's crt1/crti/crtn (crtbegin/crtend
       still come from this ABI's libgcc multilib, which is also where the
       __dualcall support routine lives);
     * pull in newlib (-lc) plus the m32df OS-glue library (-lm32dfsys, the
       syscall nucleus) instead of glibc's -lc and -lpthread.

   Everything outside the %{m32df:...} arm is the unmodified GNU userspace
   behaviour, so -m64/-m32/-mx32 are completely unaffected.  */

/* -nolibc suppresses the two things the generic GNU/Linux link sequence would
   otherwise add for this ABI: LINK_LIBATOMIC_SPEC (-latomic_asneeded, and
   libatomic is not built for m32df -- it needs a hosted C library and
   pthreads) and %L, which is empty here anyway.  %G (-lgcc) is unaffected, so
   the C library, crt0 and __dualcall still come from this ABI's libgcc.
   -no-pie is forced too: the __dualcall helper plants an absolute address for
   its return trampoline, and a static non-PIE layout keeps that simple.  */
#undef  DRIVER_SELF_SPECS
#define DRIVER_SELF_SPECS \
  "%{m32df:%{!static:-static} %{!nolibc:-nolibc} %{!pie:-no-pie} -fno-pie} " \
  SUBTARGET_DRIVER_SELF_SPECS

#undef  STARTFILE_SPEC
#define STARTFILE_SPEC \
  "%{m32df:crt0%O%s crtbegin%O%s}" \
  "%{!m32df:" GNU_USER_TARGET_STARTFILE_SPEC "}"

#undef  ENDFILE_SPEC
#define ENDFILE_SPEC \
  "%{m32df:crtend%O%s}" \
  "%{!m32df:" GNU_USER_TARGET_ENDFILE_SPEC "}"

/* -m32df has no separate C library to name here: its minimal libc is built
   into this ABI's libgcc multilib (see libgcc/config/i386/m32df-libc.c), which
   the driver links anyway via LINK_GCC_C_SEQUENCE_SPEC.  Keeping the arm empty
   means nothing on the search path can shadow glibc for the other ABIs.  */
#undef  LIB_SPEC
#define LIB_SPEC \
  "%{!m32df:" GNU_USER_TARGET_LIB_SPEC "}"
