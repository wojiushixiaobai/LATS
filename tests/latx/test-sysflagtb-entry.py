#!/usr/bin/env python3
"""Exercise the production TB-entry flag eligibility with synthetic IR1s."""
import pathlib
import re
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
src = (root / 'target/i386/latx/optimization/sys-flag-rdtn.c').read_text()
m = re.search(r'void __latxs_flag_reduction_tb_mode_parse_tb_use\([^;]*?\)\s*\{', src)
assert m
end, depth = m.end(), 1
while depth:
    depth += (src[end] == '{') - (src[end] == '}')
    end += 1
function = src[m.start():end]
config = (root / 'target/i386/latx/latx-config.c').read_text()
m = re.search(r'bool latx_tb_eflag_link_allowed\([^;]*?\)\s*\{', config)
assert m
end, depth = m.end(), 1
while depth:
    depth += (config[end] == '{') - (config[end] == '}')
    end += 1
guard = config[m.start():end]
support = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define LATXS_INSTPTN_ENABLE 1
#define LATXS_IR1_FLAGS_GENNOP 7
#define X86_INS_CMP 1
#define X86_INS_MOV 2
#define X86_INS_ADD 3
#define INSTPTN_OPC_NONE 0
#define PTN_EFLAGTB_VALID_BIT 0x8000
#define PTN_EFLAGTB_USE_MASK 0x3f
#define PTN_EFLAG_TYPE_INVALID 0
#define __ALL_EFLAGS 0x3f
#define unlikely(x) (x)
#define TB_EFLAGTB_SKIP(tb) ((tb)->ptn.os)
#define TB_EFLAGTB_USE(tb) ((tb)->ptn.pcoffset & PTN_EFLAGTB_USE_MASK)
#define TB_EFLAGTB_USE_VALID(tb) ((tb)->ptn.pcoffset & PTN_EFLAGTB_VALID_BIT)
#define LATX_TB_EFLAG_STAT_VETO_UNPUB 0
#define LATX_TB_EFLAG_STAT_VETO_USE 1
#define qatomic_fetch_add(p,n) __atomic_fetch_add(p,n,__ATOMIC_SEQ_CST)
static unsigned long latx_tb_eflag_stats[2];
static int option_enabled = 1;
static int hamt;
static int hamt_enabled(void) { return hamt; }
static int flag_reduction_tb_defer(void) { return option_enabled; }
typedef struct { int type; } IR1_OPND;
typedef struct { int opcode, count, def, illegal, flags, lock; IR1_OPND op[2]; struct { int opc; } instptn; } IR1_INST;
typedef struct { struct { int pcoffset, eflag_type, os; } ptn; int count;
    struct { void *ptr; } tc; unsigned jmp_insn_offset[2]; } TranslationBlock;
typedef struct { IR1_INST *ir1_inst_array; } TRANSLATION_DATA;
static TRANSLATION_DATA td;
static struct { TRANSLATION_DATA *tr_data; } env = { &td }, *lsenv = &env;
#define tb_ir1_num(tb) ((tb)->count)
#define latxs_ir1_has_prefix_lock(i) ((i)->lock)
#define latxs_ir1_is_illegal(i) ((i)->illegal)
#define ir1_opcode(i) ((i)->opcode)
#define ir1_opnd_num(i) ((i)->count)
#define ir1_get_opnd(i,n) (&(i)->op[n])
#define ir1_opnd_is_gpr(o) ((o)->type == 1)
#define ir1_opnd_is_imm(o) ((o)->type == 2)
#define ir1_get_eflag_def(i) ((i)->def)
'''
checks = r'''
static void check(IR1_INST *inst, int count, int expected) {
    TranslationBlock tb = { .count = count };
    td.ir1_inst_array = inst;
    __latxs_flag_reduction_tb_mode_parse_tb_use(&tb);
    assert(tb.ptn.pcoffset == (PTN_EFLAGTB_VALID_BIT | expected));
}
int main(void) {
    IR1_INST i[2] = {{.opcode=X86_INS_CMP, .count=2, .def=__ALL_EFLAGS,
                      .op={{1},{1}}}};
    check(i, 1, 0);                      /* cmp register, register */
    i[0].op[1].type=2; check(i, 1, 0);  /* cmp register, immediate */
    i[0].op[1].type=3; check(i, 1, 0x3f); /* faulting memory operand */
    i[0].op[0].type=3; i[0].op[1].type=1; check(i, 1, 0x3f);
    i[0].op[0].type=1; i[0].def=0x1f; check(i, 1, 0x3f); /* removed flag */
    i[0].def=__ALL_EFLAGS; i[0].instptn.opc=1; check(i, 1, 0x3f); /* fused */
    i[0].instptn.opc=0; i[0].illegal=1; check(i, 1, 0x3f); /* illegal */
    i[0].illegal=0; i[0].lock=1; check(i, 1, 0x3f); /* invalid lock */
    i[0].lock=0; i[0].flags=LATXS_IR1_FLAGS_GENNOP; check(i, 1, 0x3f);
    i[0].flags=0; i[0].opcode=X86_INS_MOV;
    i[1]=(IR1_INST){.opcode=X86_INS_CMP,.count=2,.def=__ALL_EFLAGS,.op={{1},{1}}};
    check(i, 2, 0x3f);                  /* fault/observer before definition */
    i[0].opcode=X86_INS_ADD; check(i, 1, 0x3f); /* not audited */
    check(i, 0, 0x3f);                  /* no instruction */
    TranslationBlock source = { .ptn.os=1 }, target = {0};
    assert(!latx_tb_eflag_link_allowed(&source, 0, &target)); /* unpublished */
    target.ptn.pcoffset = PTN_EFLAGTB_VALID_BIT | PTN_EFLAGTB_USE_MASK;
    assert(!latx_tb_eflag_link_allowed(&source, 0, &target)); /* unsafe */
    target.ptn.pcoffset = PTN_EFLAGTB_VALID_BIT;
    assert(latx_tb_eflag_link_allowed(&source, 0, &target));  /* safe */
    assert(latx_tb_eflag_link_allowed(&source, 1, &target));  /* eager edge */
    hamt=1; source.tc.ptr=(void *)(uintptr_t)0x10000000;
    target.tc.ptr=(void *)(uintptr_t)0x17fffffc;
    assert(latx_tb_eflag_link_allowed(&source, 0, &target)); /* near */
    target.tc.ptr=(void *)(uintptr_t)0x18000000;
    assert(!latx_tb_eflag_link_allowed(&source, 0, &target)); /* far */
    target.tc.ptr=(void *)(uintptr_t)0x10000002;
    assert(!latx_tb_eflag_link_allowed(&source, 0, &target)); /* unaligned */
    hamt=0;
    option_enabled = 0; target.ptn.pcoffset = 0;
    assert(latx_tb_eflag_link_allowed(&source, 0, &target));  /* feature off */
    assert(latx_tb_eflag_stats[0] == 1 && latx_tb_eflag_stats[1] == 1);
    puts("sysflagtb entry eligibility and link veto: PASS");
}
'''
with tempfile.TemporaryDirectory() as d:
    c = pathlib.Path(d) / 'test.c'
    exe = pathlib.Path(d) / 'test'
    c.write_text(support + function + guard + checks)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
