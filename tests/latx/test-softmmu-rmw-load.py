#!/usr/bin/env python3
"""Check the production RMW load registration and cold invalidation contract.

A load miss can resize or refill the TLB.  The store must never dereference
its old entry: the miss stub has to clear the live validity temporary before
calling the shared slow path, while a hit must keep it set.
"""
import pathlib
import re
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
src = (root / 'target/i386/latx/translator/sys-softmmu.c').read_text()

def extract(signature):
    m = re.search(signature + r'[^;]*?\)\s*\{', src)
    assert m, signature
    end, depth = m.end(), 1
    while depth:
        depth += (src[end] == '{') - (src[end] == '}')
        end += 1
    return src[m.start():end]

load = extract(r'void latxs_softmmu_rmw_load64\(')
cold = extract(r'static void tr_gen_softmmu_slow_path_common\(')
# The validity clear belongs to each access's miss label and runs before
# entering the shared body.  That body saves/restores the live temporaries.
stub = re.search(r'latxs_append_ir2_opnd1\(LISA_LABEL, &sp->label_slow_path\);(.*?)latxs_append_ir2_opnd1\(LISA_B, &label_body\);', cold, re.S)
assert stub, 'missing per-access cold stub'
assert re.search(r'if\s*\(!latxs_ir2_opnd_is_inv\(&sp->rmw_valid\)\).*?latxs_append_ir2_opnd2i\(LISA_ADDI_D, &sp->rmw_valid,\s*&latxs_zero_ir2_opnd, 0\);', stub.group(1), re.S), 'RMW validity is not cleared on a miss'
assert stub.group(1).index('&sp->rmw_valid') < stub.group(1).index('LISA_PCADDI')

support = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#define LISA_ADDI_D 1
#define LISA_LD_D 2
typedef struct { int id; } IR2_OPND;
typedef struct { IR2_OPND rmw_valid; } softmmu_sp_rcd_t;
typedef struct { int slow_path_rcd_nr; void *slow_path_rcd; } TRANSLATION_DATA;
static TRANSLATION_DATA td;
static struct { TRANSLATION_DATA *tr_data; } state = { &td }, *lsenv = &state;
static IR2_OPND latxs_zero_ir2_opnd = {0};
static int step;
static void latxs_append_ir2_opnd2i(int op, IR2_OPND *dst, IR2_OPND *zero, int v) {
    assert(step++ == 0 && op == LISA_ADDI_D && dst->id == 8 && zero->id == 0 && v == 1);
}
static void __gen_ldst_softmmu_helper_native(int op, IR2_OPND *dst, IR2_OPND *base, int off, int save) {
    assert(step++ == 1 && op == LISA_LD_D && dst->id == 12 && base->id == 13 && off == 24 && save == 1);
    td.slow_path_rcd_nr++;
}
'''
checks = r'''
int main(void) {
    softmmu_sp_rcd_t records[3] = {{{90}}, {{91}}, {{92}}};
    IR2_OPND dst = {12}, base = {13}, valid = {8};
    td.slow_path_rcd_nr = 1; td.slow_path_rcd = records;
    latxs_softmmu_rmw_load64(&dst, &base, 24, &valid);
    assert(step == 2 && td.slow_path_rcd_nr == 2);
    assert(records[0].rmw_valid.id == 90 && records[1].rmw_valid.id == 8 && records[2].rmw_valid.id == 92);
    puts("softmmu RMW load miss invalidation: PASS");
}
'''
with tempfile.TemporaryDirectory() as d:
    c, exe = pathlib.Path(d) / 'test.c', pathlib.Path(d) / 'test'
    c.write_text(support + load + checks)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
