/* Interceptor functions.
   Copyright (C) 1992-2026 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 3, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

/* Contents of this file:

Definition of types:
    - struct pending_interceptor_thunk

Definitions of global variables: 
    - pending_interceptor_thunks
    - tree current_interceptor_thunk_decl
    - char *current_interceptor__core_asm_name

Definitions of functions:
    extern - handle_interceptor_attribute
    static - edit_interceptor_core_type_to_be_noexcept_and_return_function_pointer
    extern - start_function_interceptor
    static - emit_pure_assembly_as_function
    static - get_assembler_for_interceptor_thunk
    extern - queue_emission_of_interceptor_thunk_if_decl_is_interceptor
    extern - flush_pending_interceptor_thunks 
*/

#include "interceptor.h"
#define INCLUDE_STRING
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "target.h"
#include "c-family/c-target.h"
#include "cp-tree.h"
#include "timevar.h"
#include "stringpool.h"
#include "cgraph.h"
#include "stor-layout.h"
#include "varasm.h"
#include "attribs.h"
#include "flags.h"
#include "tree-iterator.h"
#include "decl.h"
#include "interceptor.h"
#include "intl.h"
#include "toplev.h"
#include "c-family/c-objc.h"
#include "c-family/c-pragma.h"
#include "c-family/c-ubsan.h"
#include "cp/cp-name-hint.h"
#include "debug.h"
#include "plugin.h"
#include "builtins.h"
#include "gimplify.h"
#include "asan.h"
#include "gcc-rich-location.h"
#include "langhooks.h"
#include "context.h"  /* For 'g'.  */
#include "omp-general.h"
#include "omp-offload.h"  /* For offload_vars.  */
#include "opts.h"
#include "langhooks-def.h"  /* For lhd_simulate_record_decl  */
#include "coroutines.h"
#include "contracts.h"
#include "gcc-urlifier.h"
#include "diagnostic-highlight-colors.h"
#include "pretty-print-markup.h"
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "target.h"
#include "function.h"
#include "tree.h"
#include "memmodel.h"
#include "../c-family/c-common.h"
#include "gimple-expr.h"
#include "tm_p.h"
#include "stringpool.h"
#include "cgraph.h"
#include "diagnostic.h"
#include "intl.h"
#include "stor-layout.h"
#include "calls.h"
#include "attribs.h"
#include "varasm.h"
#include "trans-mem.h"
#include "../c-family/c-objc.h"
#include "common/common-target.h"
#include "langhooks.h"
#include "tree-inline.h"
#include "ipa-strub.h"
#include "toplev.h"
#include "tree-iterator.h"
#include "opts.h"
#include "gimplify.h"
#include "tree-pretty-print.h"
#include "gcc-rich-location.h"
#include "gcc-urlifier.h"
#include "attr-callback.h"

/* Information required for each thunk in the queue to be emitted. */
struct GTY(()) pending_interceptor_thunk
{
  tree thunk_decl;
  char * GTY((skip)) core_asm_name;
};

/* Global variables needed for [[interceptor]] functions. */
static GTY(()) tree current_interceptor_thunk_decl = NULL_TREE;
static char *current_interceptor_core_asm_name = NULL;
static GTY(()) vec<pending_interceptor_thunk *, va_gc> *pending_interceptor_thunks = NULL;

/* Handle an "interceptor" attribute; arguments as in
   struct attribute_spec.handler.  */

tree
handle_interceptor_attribute (tree *node, tree name, tree ARG_UNUSED (args),
                              int ARG_UNUSED (flags), bool *no_add_attrs)
{
  if (TREE_CODE (*node) != FUNCTION_DECL)
    {
      error ("%qE attribute only applies to functions", name);
      *no_add_attrs = true;
      return NULL_TREE;
    }

  tree const fntype = TREE_TYPE (*node);

  if (TREE_CODE (fntype) != FUNCTION_TYPE
      || !VOID_TYPE_P (TREE_TYPE (fntype))
      || TYPE_ARG_TYPES (fntype) != void_list_node)
    {
      error_at (DECL_SOURCE_LOCATION (*node),
                "interceptor function %qD must have type %<void(void)%>",
                *node);
      *no_add_attrs = true;
      return NULL_TREE;
    }

  if (!TYPE_NOTHROW_P (fntype))
    {
      error_at (DECL_SOURCE_LOCATION (*node),
                "interceptor function %qD must be declared %<noexcept%>",
                *node);
      *no_add_attrs = true;
      return NULL_TREE;
    }

  return NULL_TREE;
}

static void
edit_interceptor_core_type_to_be_noexcept_and_return_function_pointer (tree core_decl)
{
  gcc_assert (TREE_CODE (core_decl) == FUNCTION_DECL);
  gcc_assert (TREE_CODE (TREE_TYPE (core_decl)) == FUNCTION_TYPE);

  /* Build: void(void) */
  tree target_fn_type = build_function_type_list (void_type_node, NULL_TREE);

  /* Build: void(*)(void) */
  tree target_fn_ptr_type = build_pointer_type (target_fn_type);

  /* Build: auto(void) -> void (*)(void) */
  tree new_type = build_function_type_list (target_fn_ptr_type, NULL_TREE);

  /* noexcept(true) */
  new_type = build_exception_variant (new_type, noexcept_true_spec);

  TREE_TYPE (core_decl) = new_type;

  if (!DECL_RESULT (core_decl)) return;

  TREE_TYPE    (DECL_RESULT (core_decl)) = target_fn_ptr_type;
  DECL_CONTEXT (DECL_RESULT (core_decl)) = core_decl         ;
}

void
start_function_interceptor (tree *pdecl1)
{
  tree core_decl;
  const char *thunk_name;
  char *core_name;

  gcc_assert (*pdecl1 != NULL_TREE);

  gcc_assert (current_interceptor_thunk_decl    == NULL_TREE);
  gcc_assert (current_interceptor_core_asm_name == NULL     );

  current_interceptor_thunk_decl = *pdecl1;

  /* Keep the public decl visible as the declaration.  */
  mangle_decl (current_interceptor_thunk_decl);
  pushdecl (current_interceptor_thunk_decl);

  /* Make separate core decl.  */
  core_decl = copy_node (*pdecl1);
  DECL_LANG_SPECIFIC (core_decl) = DECL_LANG_SPECIFIC (*pdecl1);
  cxx_dup_lang_specific_decl (core_decl);
  DECL_ATTRIBUTES (core_decl) = remove_attribute ("interceptor", DECL_ATTRIBUTES (core_decl));
  DECL_INTERCEPTOR_P (core_decl) = 0;
  edit_interceptor_core_type_to_be_noexcept_and_return_function_pointer (core_decl);

  /* Save original/public symbol name.  */
  mangle_decl (*pdecl1);
  thunk_name = IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME (*pdecl1));

  /* Compute core mangled name.  */
  core_name = XNEWVEC (char, 7 + strlen (thunk_name) + 1);
  strcpy (core_name, "__core_" );
  strcat (core_name, thunk_name);

  /* Give only the copied core decl the new assembler name.  */
#if 0
  overwrite_mangling (core_decl, get_identifier (core_name));
  record_mangling (core_decl, true);
#else
  SET_DECL_ASSEMBLER_NAME (core_decl, get_identifier (core_name));
  DECL_NAME (core_decl) = get_identifier (core_name);
#endif

  current_interceptor_core_asm_name = core_name;

  /* Compile body into core decl.  */
  *pdecl1 = core_decl;
}

static tree
emit_pure_assembly_as_function (const char *mangled_name,
                                tree model_decl,
                                const char *assembly)
{
  location_t loc;
  tree asm_string;
  char *buf;
  char linkage_directive[64];

  gcc_assert (mangled_name != NULL);
  gcc_assert (model_decl != NULL_TREE);
  gcc_assert (TREE_CODE (model_decl) == FUNCTION_DECL);
  gcc_assert (assembly != NULL);

  loc = DECL_SOURCE_LOCATION (model_decl);

  /* For now, keep linkage handling very simple.  */
  if (TREE_PUBLIC (model_decl))
    strcpy (linkage_directive, ".globl ");
  else
    linkage_directive[0] = '\0';

  /* Emit a complete top-level asm function.
     The body text in 'assembly' should NOT contain the function label.  */
  buf = XNEWVEC (char,
                 strlen (linkage_directive)
                 + strlen (mangled_name)
                 + strlen ("\n.type , @function\n:\n\n.size , .-\n")
                 + strlen (mangled_name) * 4
                 + strlen (assembly)
                 + 1);

  sprintf (buf,
           "%s%s\n"
           ".type %s, @function\n"
           "%s:\n"
           "%s\n"
           ".size %s, .-%s\n",
           linkage_directive, mangled_name,
           mangled_name,
           mangled_name,
           assembly,
           mangled_name, mangled_name);

  asm_string = build_string (strlen (buf) + 1, buf);

  finish_asm_stmt (loc,
                   /*volatile_p=*/1,
                   asm_string,
                   /*output_operands=*/NULL_TREE,
                   /*input_operands=*/NULL_TREE,
                   /*clobbers=*/NULL_TREE,
                   /*labels=*/NULL_TREE,
                   /*inline_p=*/false,
                   /*toplev_p=*/true);

  XDELETEVEC (buf);

  /* We are no longer creating a FUNCTION_DECL here.  */
  return NULL_TREE;
}

enum BaseArchitecture { BaseArchitecture_Unspecified, BaseArchitecture_x86, BaseArchitecture_ARM };

template<BaseArchitecture A = BaseArchitecture_Unspecified, unsigned ptr_size = 0u>
static char *
get_assembler_for_interceptor_thunk (const char *core_name);

template<>
char *
get_assembler_for_interceptor_thunk<BaseArchitecture_x86, 32u> (const char *core_name)
{
  constexpr char front[] =
    "pushl %%eax\n"
    "pushl %%edx\n"
    "pushl %%ecx\n"
    "pushl %%ebx\n"

    "movl $1, %%eax\n"
    "cpuid\n"

    "bt $25, %%edx\n"
    "jnc 3f\n"

    "bt $27, %%ecx\n"
    "jnc 1f\n"
    "bt $28, %%ecx\n"
    "jnc 1f\n"

    "xorl %%ecx, %%ecx\n"
    "xgetbv\n"
    "andl $0x6, %%eax\n"
    "cmpl $0x6, %%eax\n"
    "je 2f\n"

    "1:\n"
    "popl %%ebx\n"
    "subl $48, %%esp\n"
    "movdqu %%xmm0, 0(%%esp)\n"
    "movdqu %%xmm1, 16(%%esp)\n"
    "movdqu %%xmm2, 32(%%esp)\n"
    "call ";

  constexpr char middle[] =
    "\n"
    "movl %%eax, %%esi\n"
    "movdqu 0(%%esp), %%xmm0\n"
    "movdqu 16(%%esp), %%xmm1\n"
    "movdqu 32(%%esp), %%xmm2\n"
    "addl $48, %%esp\n"
    "popl %%ecx\n"
    "popl %%edx\n"
    "popl %%eax\n"
    "jmp *%%esi\n"

    "2:\n"
    "popl %%ebx\n"
    "subl $96, %%esp\n"
    "vmovdqu %%ymm0, 0(%%esp)\n"
    "vmovdqu %%ymm1, 32(%%esp)\n"
    "vmovdqu %%ymm2, 64(%%esp)\n"
    "call ";

  constexpr char back[] =
    "\n"
    "movl %%eax, %%esi\n"
    "vmovdqu 0(%%esp), %%ymm0\n"
    "vmovdqu 32(%%esp), %%ymm1\n"
    "vmovdqu 64(%%esp), %%ymm2\n"
    "addl $96, %%esp\n"
    "popl %%ecx\n"
    "popl %%edx\n"
    "popl %%eax\n"
    "jmp *%%esi\n"

    "3:\n"
    "popl %%ebx\n"
    "call ";

  constexpr char tail[] =
    "\n"
    "movl %%eax, %%esi\n"
    "popl %%ecx\n"
    "popl %%edx\n"
    "popl %%eax\n"
    "jmp *%%esi\n";

  char *p = XNEWVEC (char,
                     sizeof (front) - 1u
                     + strlen (core_name)
                     + sizeof (middle) - 1u
                     + strlen (core_name)
                     + sizeof (back) - 1u
                     + strlen (core_name)
                     + sizeof (tail) - 1u
                     + 1u);

  strcpy (p, front);
  strcat (p, core_name);
  strcat (p, middle);
  strcat (p, core_name);
  strcat (p, back);
  strcat (p, core_name);
  strcat (p, tail);

  return p;
}

template<>
char *
get_assembler_for_interceptor_thunk<BaseArchitecture_x86, 64u> (const char *core_name)
{
  constexpr char front[] =
    "subq $56, %%rsp\n"
    "movq %%rax, 0(%%rsp)\n"
    "movq %%rdi, 8(%%rsp)\n"
    "movq %%rsi, 16(%%rsp)\n"
    "movq %%rdx, 24(%%rsp)\n"
    "movq %%rcx, 32(%%rsp)\n"
    "movq %%r8, 40(%%rsp)\n"
    "movq %%r9, 48(%%rsp)\n"

    "pushq %%rbx\n"  /* Preserve RBX before executing CPUID */

    /* Detect SSE-128 / AVX-256 / AVX-512 each time */
    "movl $1, %%eax\n"
    "cpuid\n"
    "bt $27, %%ecx\n"
    "jnc 1f\n" /* jump to 1 = SSE-128 */
    "bt $28, %%ecx\n"
    "jnc 1f\n" /* jump to 1 = SSE-128 */

    "xorl %%ecx, %%ecx\n"
    "xgetbv\n"
    "movl %%eax, %%r10d\n"
    "andl $0x6, %%r10d\n"
    "cmpl $0x6, %%r10d\n"
    "jne 1f\n" /* jump to 1 = SSE-128 */

    "movl $7, %%eax\n"
    "xorl %%ecx, %%ecx\n"
    "cpuid\n"
    "bt $16, %%ebx\n"
    "jnc 2f\n" /* jump to 2 = AVX-256 */

    "xorl %%ecx, %%ecx\n"
    "xgetbv\n"
    "andl $0xE6, %%eax\n"
    "cmpl $0xE6, %%eax\n"
    "je 3f\n" /* jump to 3 = AVX-512 */

    "2:\n" /* AVX-256 path */
    "popq %%rbx\n"
    "subq $272, %%rsp\n"
    "movq $2, 0(%%rsp)\n"
    "vmovdqu %%ymm0, 8(%%rsp)\n"
    "vmovdqu %%ymm1, 40(%%rsp)\n"
    "vmovdqu %%ymm2, 72(%%rsp)\n"
    "vmovdqu %%ymm3, 104(%%rsp)\n"
    "vmovdqu %%ymm4, 136(%%rsp)\n"
    "vmovdqu %%ymm5, 168(%%rsp)\n"
    "vmovdqu %%ymm6, 200(%%rsp)\n"
    "vmovdqu %%ymm7, 232(%%rsp)\n"
    "jmp 4f\n"

    "3:\n" /* AVX-512 path */
    "popq %%rbx\n"
    "subq $592, %%rsp\n"
    "movq $3, 0(%%rsp)\n"
    "vmovdqu64 %%zmm0, 8(%%rsp)\n"
    "vmovdqu64 %%zmm1, 72(%%rsp)\n"
    "vmovdqu64 %%zmm2, 136(%%rsp)\n"
    "vmovdqu64 %%zmm3, 200(%%rsp)\n"
    "vmovdqu64 %%zmm4, 264(%%rsp)\n"
    "vmovdqu64 %%zmm5, 328(%%rsp)\n"
    "vmovdqu64 %%zmm6, 392(%%rsp)\n"
    "vmovdqu64 %%zmm7, 456(%%rsp)\n"
    "kmovq %%k0, 520(%%rsp)\n"
    "kmovq %%k1, 528(%%rsp)\n"
    "kmovq %%k2, 536(%%rsp)\n"
    "kmovq %%k3, 544(%%rsp)\n"
    "kmovq %%k4, 552(%%rsp)\n"
    "kmovq %%k5, 560(%%rsp)\n"
    "kmovq %%k6, 568(%%rsp)\n"
    "kmovq %%k7, 576(%%rsp)\n"
    "jmp 4f\n"

    "1:\n" /* SSE-128 path */
    "popq %%rbx\n"
    "subq $144, %%rsp\n"
    "movq $1, 0(%%rsp)\n"
    "movdqu %%xmm0, 8(%%rsp)\n"
    "movdqu %%xmm1, 24(%%rsp)\n"
    "movdqu %%xmm2, 40(%%rsp)\n"
    "movdqu %%xmm3, 56(%%rsp)\n"
    "movdqu %%xmm4, 72(%%rsp)\n"
    "movdqu %%xmm5, 88(%%rsp)\n"
    "movdqu %%xmm6, 104(%%rsp)\n"
    "movdqu %%xmm7, 120(%%rsp)\n"

    "4:\n" /* Call core */
    "subq $32, %%rsp\n"   /* 32 bytes stack shadow space for Microsoft */
    "call ";

constexpr char back[] =
    "\n"
    "addq $32, %%rsp\n"   /* 32 bytes stack shadow space for Microsoft */
    "movq %%rax, %%r11\n"  /* Preserve the return value (i.e. the target function address) */

    /* Restore according to mode  */
    "cmpq $1, 0(%%rsp)\n"
    "je 5f\n" /* jump to 5 = SSE-128 */
    "cmpq $2, 0(%%rsp)\n"
    "je 6f\n" /* jump to 6 = AVX-256 */
    "cmpq $3, 0(%%rsp)\n"
    "je 7f\n" /* jump to 7 = AVX-512 */
    "ud2\n"

    "5:\n" /* Restore SSE-128 state */
    "movdqu 8(%%rsp), %%xmm0\n"
    "movdqu 24(%%rsp), %%xmm1\n"
    "movdqu 40(%%rsp), %%xmm2\n"
    "movdqu 56(%%rsp), %%xmm3\n"
    "movdqu 72(%%rsp), %%xmm4\n"
    "movdqu 88(%%rsp), %%xmm5\n"
    "movdqu 104(%%rsp), %%xmm6\n"
    "movdqu 120(%%rsp), %%xmm7\n"
    "movq 144(%%rsp), %%rax\n"
    "movq 152(%%rsp), %%rdi\n"
    "movq 160(%%rsp), %%rsi\n"
    "movq 168(%%rsp), %%rdx\n"
    "movq 176(%%rsp), %%rcx\n"
    "movq 184(%%rsp), %%r8\n"
    "movq 192(%%rsp), %%r9\n"
    "addq $200, %%rsp\n"
    "jmp *%%r11\n" /* jump to target function */

    "6:\n" /* Restore AVX-256 state */
    "vmovdqu 8(%%rsp), %%ymm0\n"
    "vmovdqu 40(%%rsp), %%ymm1\n"
    "vmovdqu 72(%%rsp), %%ymm2\n"
    "vmovdqu 104(%%rsp), %%ymm3\n"
    "vmovdqu 136(%%rsp), %%ymm4\n"
    "vmovdqu 168(%%rsp), %%ymm5\n"
    "vmovdqu 200(%%rsp), %%ymm6\n"
    "vmovdqu 232(%%rsp), %%ymm7\n"
    "movq 272(%%rsp), %%rax\n"
    "movq 280(%%rsp), %%rdi\n"
    "movq 288(%%rsp), %%rsi\n"
    "movq 296(%%rsp), %%rdx\n"
    "movq 304(%%rsp), %%rcx\n"
    "movq 312(%%rsp), %%r8\n"
    "movq 320(%%rsp), %%r9\n"
    "addq $328, %%rsp\n"
    "jmp *%%r11\n" /* jump to target function */

    "7:\n" /* Restore AVX-512 state */
    "vmovdqu64 8(%%rsp), %%zmm0\n"
    "vmovdqu64 72(%%rsp), %%zmm1\n"
    "vmovdqu64 136(%%rsp), %%zmm2\n"
    "vmovdqu64 200(%%rsp), %%zmm3\n"
    "vmovdqu64 264(%%rsp), %%zmm4\n"
    "vmovdqu64 328(%%rsp), %%zmm5\n"
    "vmovdqu64 392(%%rsp), %%zmm6\n"
    "vmovdqu64 456(%%rsp), %%zmm7\n"
    "kmovq 520(%%rsp), %%k0\n"
    "kmovq 528(%%rsp), %%k1\n"
    "kmovq 536(%%rsp), %%k2\n"
    "kmovq 544(%%rsp), %%k3\n"
    "kmovq 552(%%rsp), %%k4\n"
    "kmovq 560(%%rsp), %%k5\n"
    "kmovq 568(%%rsp), %%k6\n"
    "kmovq 576(%%rsp), %%k7\n"
    "movq 592(%%rsp), %%rax\n"
    "movq 600(%%rsp), %%rdi\n"
    "movq 608(%%rsp), %%rsi\n"
    "movq 616(%%rsp), %%rdx\n"
    "movq 624(%%rsp), %%rcx\n"
    "movq 632(%%rsp), %%r8\n"
    "movq 640(%%rsp), %%r9\n"
    "addq $648, %%rsp\n"
    "jmp *%%r11\n";  /* jump to target function */

  char *const p = XNEWVEC (char, sizeof(front)-1u + strlen (core_name) + sizeof(back)-1u + 1u);
  strcpy (p, front    );
  strcat (p, core_name);
  strcat (p, back     );
  return p;
}

template<>
char *
get_assembler_for_interceptor_thunk<BaseArchitecture_Unspecified,0u> (const char *core_name)
{
  if ( TARGET_64BIT ) return get_assembler_for_interceptor_thunk< BaseArchitecture_x86, 64u >(core_name);
  /************/ else return get_assembler_for_interceptor_thunk< BaseArchitecture_x86, 32u >(core_name);
}

void
queue_emission_of_interceptor_thunk_if_decl_is_interceptor (tree fndecl)
{
  if (current_interceptor_thunk_decl       == NULL_TREE) return;
  if (current_interceptor_core_asm_name    == NULL     ) return;
  if (current_interceptor_core_asm_name[0] == '\0'     ) return;

  if (!DECL_ASSEMBLER_NAME_SET_P (current_interceptor_thunk_decl)) return;

  if (0 != strcmp (IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME (fndecl)), current_interceptor_core_asm_name)) return;

  pending_interceptor_thunk *q = ggc_cleared_alloc<pending_interceptor_thunk> ();

  q->thunk_decl    = current_interceptor_thunk_decl   ;
  q->core_asm_name = current_interceptor_core_asm_name;

  vec_safe_push (pending_interceptor_thunks, q);

  current_interceptor_thunk_decl    = NULL_TREE;
  current_interceptor_core_asm_name = NULL     ;
}

void
flush_pending_interceptor_thunks (void)
{
  if (!pending_interceptor_thunks)
    return;

  for (unsigned i = 0; i < pending_interceptor_thunks->length (); ++i)
    {
      pending_interceptor_thunk *q = (*pending_interceptor_thunks)[i];
      char *asm_text = get_assembler_for_interceptor_thunk (q->core_asm_name);

      emit_pure_assembly_as_function (q->core_asm_name + strlen ("__core_"), q->thunk_decl, asm_text);

      XDELETEVEC (asm_text        );
      XDELETEVEC (q->core_asm_name);
      q->core_asm_name = NULL;
    }

  pending_interceptor_thunks = NULL;
}

tree
build_interceptor_goto_target (location_t loc, tree expr)
{
  tree target_fn_type  = build_function_type_list (void_type_node, NULL_TREE);
  tree target_ptr_type = build_pointer_type (target_fn_type);

  /* First try a C-style cast.  */
  tree converted = build_c_cast (loc, target_ptr_type, expr);
  if (converted != error_mark_node)
    return converted;

  /* Maybe also try a bit_cast here if the size and alignment match */

  error_at (loc, "operand of %<goto ->%> is not castable to %<void (*)(void)%>");

  return error_mark_node;
}

#include "gt-cp-interceptor.h"
