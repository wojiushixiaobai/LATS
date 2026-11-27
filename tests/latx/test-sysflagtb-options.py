#!/usr/bin/env python3
"""Check production sysflagtb option safety gates with synthetic modes."""
import pathlib
import re
import subprocess
import tempfile

src = (pathlib.Path(__file__).resolve().parents[2] /
       'target/i386/latx/latx-options.c').read_text()
m = re.search(r'static void latxs_options_check_sysflagtb\([^;]*?\)\s*\{', src)
assert m
end, depth = m.end(), 1
while depth:
    depth += (src[end] == '{') - (src[end] == '}')
    end += 1
function = src[m.start():end]
support = r'''
#include <assert.h>
#include <stdio.h>
#define CONFIG_SOFTMMU 1
#define FLAGRDTN_TB_DEFER 1
#define FLAGRDTN_TB_HAMT_RECOVER 2
#define IRQ_CHECK_BLOCK_END 2
static int option_sys_flag_reduction_tb, hamt, tu, option_tu_hras;
static int option_sys_aot, option_aot_save, option_aot_load, option_aot_fork;
static int option_rastoptp = 1;
static int option_int_opt = IRQ_CHECK_BLOCK_END;
static int warnings;
#define warn_report(...) (++warnings)
static int flag_reduction_tb_defer(void) { return option_sys_flag_reduction_tb & FLAGRDTN_TB_DEFER; }
static int flag_reduction_tb_hamt_recover(void) { return option_sys_flag_reduction_tb & FLAGRDTN_TB_HAMT_RECOVER; }
static int hamt_enabled(void) { return hamt; }
static int tu_runtime_enabled(void) { return tu; }
'''
checks = r'''
static void reset(int mode, int h, int t) {
    option_sys_flag_reduction_tb = mode; hamt = h; tu = t; warnings = 0;
    option_tu_hras = 0; option_rastoptp = 1; option_int_opt = IRQ_CHECK_BLOCK_END;
}
int main(void) {
    /* Production default: defer under HAMT without the recovery bit is still
     * refused, and the recovery bit is cleared as well. */
    reset(FLAGRDTN_TB_DEFER, 1, 0);
    latxs_options_check_sysflagtb();
    assert(option_sys_flag_reduction_tb == 0 && warnings == 1);

    /* Validation mode: defer + recovery under HAMT is kept, but forces
     * rastoptp off and warns that the configuration is not shippable. */
    reset(FLAGRDTN_TB_DEFER | FLAGRDTN_TB_HAMT_RECOVER, 1, 0);
    latxs_options_check_sysflagtb();
    assert(option_sys_flag_reduction_tb ==
           (FLAGRDTN_TB_DEFER | FLAGRDTN_TB_HAMT_RECOVER));
    assert(option_rastoptp == 0 && warnings == 2);

    /* tuhras != 0 disables the deferral under HAMT. */
    reset(FLAGRDTN_TB_DEFER | FLAGRDTN_TB_HAMT_RECOVER, 1, 0);
    option_tu_hras = 1;
    latxs_options_check_sysflagtb();
    assert(!(option_sys_flag_reduction_tb & FLAGRDTN_TB_DEFER));
    assert(option_sys_flag_reduction_tb == 0);

    /* Any IRQ-check mode other than block-end disables the deferral. */
    reset(FLAGRDTN_TB_DEFER | FLAGRDTN_TB_HAMT_RECOVER, 1, 0);
    option_int_opt = 0;
    latxs_options_check_sysflagtb();
    assert(option_sys_flag_reduction_tb == 0);

    /* TU runtime still rejects the whole option. */
    reset(FLAGRDTN_TB_DEFER | FLAGRDTN_TB_HAMT_RECOVER, 0, 1);
    latxs_options_check_sysflagtb();
    assert(option_sys_flag_reduction_tb == 0);

    /* Deferral without HAMT keeps working; the recovery bit is inert and is
     * not reported as active. */
    reset(FLAGRDTN_TB_DEFER | FLAGRDTN_TB_HAMT_RECOVER, 0, 0);
    latxs_options_check_sysflagtb();
    assert(option_sys_flag_reduction_tb == FLAGRDTN_TB_DEFER && warnings == 0);

    /* Plain deferral without HAMT and without the recovery bit is unchanged. */
    reset(FLAGRDTN_TB_DEFER, 0, 0);
    latxs_options_check_sysflagtb();
    assert(option_sys_flag_reduction_tb == FLAGRDTN_TB_DEFER && warnings == 0);
    puts("sysflagtb option safety gates: PASS");
}
'''
with tempfile.TemporaryDirectory() as d:
    c, exe = pathlib.Path(d)/'test.c', pathlib.Path(d)/'test'
    c.write_text(support + function + checks)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
