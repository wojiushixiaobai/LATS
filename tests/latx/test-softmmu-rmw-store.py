#!/usr/bin/env python3
"""Compile the production RMW store emitter and execute its emitted IR model.

The fixture checks fast RAM, invalidated entry (without touching a stale
pointer), read-only/slow write tag, and nonzero offset.  The fallback stub
models only a store; it fails if the emitter replays the guest read.
"""
import pathlib
import re
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
src = (root / 'target/i386/latx/translator/sys-softmmu.c').read_text()
m = re.search(r'void latxs_softmmu_rmw_store64\([^;]*?\)\s*\{', src)
assert m
end, depth = m.end(), 1
while depth:
    depth += (src[end] == '{') - (src[end] == '}')
    end += 1
function = src[m.start():end]
support = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define TARGET_PAGE_BITS 12
#define LISA_BEQ 1
#define LISA_ADDI_D 2
#define LISA_BSTRINS_D 3
#define LISA_LD_D 4
#define LISA_BNE 5
#define LISA_STX_D 6
#define LISA_B 7
#define LISA_LABEL 8
#define LISA_ST_D 9
#define SLOW 10
#define LISA_AMADD_D 11
#define LOAD_IMM 12
typedef struct { int id; } IR2_OPND;
typedef struct { uint64_t addr_write, addend; } CPUTLBEntry;
typedef struct { int op,a,b,c; int64_t imm,imm2; } insn;
static insn code[64]; static int nr, next_label=40, next_reg=20;
static IR2_OPND latxs_zero_ir2_opnd={0}, latxs_arg0_ir2_opnd={4};
static IR2_OPND latxs_arg1_ir2_opnd={5}, latxs_arg2_ir2_opnd={6};
static IR2_OPND latxs_arg3_ir2_opnd={7};
static IR2_OPND latxs_ir2_opnd_new_label(void) { return (IR2_OPND){next_label++}; }
static IR2_OPND latxs_ra_alloc_itemp(void) { return (IR2_OPND){next_reg++}; }
static void latxs_ra_free_temp(IR2_OPND *p) { (void)p; }
static uint64_t hit_count;
static bool counting;
static uint64_t *latxs_softmmu_rmw_hit_counter(void) { return counting ? &hit_count : NULL; }
static bool hamt_enabled(void) { return false; }
static void latxs_load_imm64_to_ir2(IR2_OPND *p, uint64_t v) {
    code[nr++]=(insn){.op=LOAD_IMM,.a=p->id,.imm=(int64_t)v};
}
static void latxs_tr_dmw_in_fix_mem(IR2_OPND *p) { (void)p; assert(0); }
static void latxs_append_ir2_opnd1(int op, IR2_OPND *a) {
    code[nr++]=(insn){.op=op,.a=a->id};
}
static void latxs_append_ir2_opnd2i(int op, IR2_OPND *a, IR2_OPND *b, int64_t imm) {
    code[nr++]=(insn){.op=op,.a=a->id,.b=b->id,.imm=imm};
}
static void latxs_append_ir2_opnd3(int op, IR2_OPND *a, IR2_OPND *b, IR2_OPND *c) {
    code[nr++]=(insn){.op=op,.a=a->id,.b=b->id,.c=c->id};
}
static void __gen_ldst_softmmu_helper_native(int op,IR2_OPND *v,IR2_OPND *b,int off,int save) {
    assert(op==LISA_ST_D && save==1);
    code[nr++]=(insn){.op=SLOW,.a=v->id,.b=b->id,.imm=off};
}
'''
checks = r'''
static int find_label(int id) {
    for (int i=0;i<nr;i++) if(code[i].op==LISA_LABEL && code[i].a==id) return i;
    assert(0); return 0;
}
static void execute(bool valid, bool writable, int offset, bool diagnostic) {
    uint64_t regs[64]={0}, guest=0x1100, data=7, backing=3;
    CPUTLBEntry entry={.addr_write=writable ? 0x1000 : 0x1001,
                       .addend=(uint64_t)(uintptr_t)&backing - guest};
    IR2_OPND value={12}, base={13}, v={8};
    nr=0; next_label=40; next_reg=20; counting=diagnostic; hit_count=0;
    latxs_softmmu_rmw_store64(&value,&base,offset,&v);
    assert(code[0].op==LISA_BEQ && code[0].a==8);
    int tag_index=offset ? 2 : 1;
    assert(code[tag_index].op==LISA_LD_D && code[tag_index].a==7 &&
           code[tag_index].b==6 && code[tag_index].imm==offsetof(CPUTLBEntry,addr_write));
    assert(code[tag_index+1].op==LISA_BNE && code[tag_index+1].a==4 &&
           code[tag_index+1].b==7);
    assert(code[tag_index+2].op==LISA_STX_D && code[tag_index+2].c==5);
    regs[12]=data; regs[13]=guest-offset; regs[8]=valid;
    regs[4]=valid ? 0x1000 : 1; /* read-hit page key */
    regs[5]=valid ? entry.addend : 1; /* read-hit, possibly tagged addend */
    regs[6]=valid ? (uintptr_t)&entry : 1; /* stale pointer must not be read */
    int slow=0;
    for (int pc=0;pc<nr;pc++) {
        insn x=code[pc]; uint64_t ptr;
        switch(x.op) {
        case LISA_BEQ: if(regs[x.a]==regs[x.b]) pc=find_label(x.c); break;
        case LISA_BNE: if(regs[x.a]!=regs[x.b]) pc=find_label(x.c); break;
        case LISA_B: pc=find_label(x.a); break;
        case LISA_LABEL: break;
        case LISA_ADDI_D: regs[x.a]=regs[x.b]+x.imm; break;
        case LISA_BSTRINS_D: assert(x.imm==11 && x.imm2==0); regs[x.a]&=~UINT64_C(0xfff); break;
        case LISA_LD_D: ptr=regs[x.b]+x.imm; memcpy(&regs[x.a],(void *)(uintptr_t)ptr,8); break;
        case LISA_STX_D: ptr=regs[x.b]+regs[x.c]; memcpy((void *)(uintptr_t)ptr,&regs[x.a],8); break;
        case LOAD_IMM: regs[x.a]=x.imm; break;
        case LISA_AMADD_D: assert(x.a==0); *(uint64_t *)(uintptr_t)regs[x.b]+=regs[x.c]; break;
        case SLOW: assert(regs[x.b]+x.imm==guest); backing=regs[x.a]; slow++; break;
        default: assert(0);
        }
    }
    assert(backing==data);
    assert(slow==(!valid || !writable));
    assert(hit_count==(diagnostic && valid && writable));
}
int main(void) {
    for (int diagnostic=0; diagnostic<2; diagnostic++) {
        execute(true,true,0,diagnostic); execute(true,true,24,diagnostic);
        execute(false,true,0,diagnostic); execute(false,true,24,diagnostic);
        execute(true,false,0,diagnostic); execute(true,false,24,diagnostic);
    }
    puts("softmmu RMW store control flow: PASS");
}
'''
with tempfile.TemporaryDirectory() as d:
    c = pathlib.Path(d) / 'test.c'
    exe = pathlib.Path(d) / 'test'
    c.write_text(support + function + checks)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
