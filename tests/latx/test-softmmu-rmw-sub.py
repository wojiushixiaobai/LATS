#!/usr/bin/env python3
"""Guard SUB's use of the proven RMW path and the exception ordering."""
import pathlib
import re

root = pathlib.Path(__file__).resolve().parents[2]
byhand = (root / 'target/i386/latx/translator/sys-tr-byhand64.c').read_text()
softmmu = (root / 'target/i386/latx/translator/sys-softmmu.c').read_text()

def function(source, signature):
    match = re.search(signature + r'[^;]*?\)\s*\{', source)
    assert match, signature
    end, depth = match.end(), 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]

translate = function(byhand, r'static void __tr_byhand64_addsub_mem_gpr\(')
eligible = function(softmmu, r'bool latxs_softmmu_rmw_reuse_eligible\(')
assert re.search(r'bool reuse\s*=\s*opnd_size\s*==\s*64\s*&&\s*latxs_softmmu_rmw_reuse_eligible\(pir1\)', translate)
assert '!latxs_ir1_has_prefix_lock(pir1)' in eligible
assert 'is_add ? LISA_ADD_W : LISA_SUB_W' in translate
assert 'is_add ? LISA_X86ADD_B : LISA_X86SUB_B' in translate
load = translate.index('latxs_softmmu_rmw_load64(')
calculation = translate.index('latxs_append_ir2_opnd3(cal + 1', load)
store = translate.index('latxs_softmmu_rmw_store64(', calculation)
flags = translate.index('LATXS_FLAG_REDUCTION_CHECKIF(pir1)', store)
assert load < calculation < store < flags
assert 'la_ld_by_op_size(' in translate and 'la_st_by_op_size(' in translate
print('softmmu RMW SUB eligibility and exception ordering: PASS')
