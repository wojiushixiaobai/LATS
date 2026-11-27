#!/usr/bin/env python3
"""Exercise the production opt-in long-mode predicate and CPL gate."""
import pathlib
import re
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
cpu = (root / 'target/i386/cpu.h').read_text()
hamt = (root / 'include/hamt-options.h').read_text()
options = (root / 'target/i386/latx/latx-options.c').read_text()
segment = (root / 'target/i386/tcg/seg_helper.c').read_text()
helper = (root / 'target/i386/helper.c').read_text()

def extract(src, name):
    m = re.search(r'static inline bool ' + name + r'\([^;]*?\)\s*\{', src)
    if not m:
        raise RuntimeError('missing ' + name)
    pos, depth = m.end(), 1
    while depth:
        depth += (src[pos] == '{') - (src[pos] == '}')
        pos += 1
    return src[m.start():pos]

def extract_update(src):
    m = re.search(r'int update_hamt_en\([^;]*?\)\s*\{', src)
    assert m
    pos, depth = m.end(), 1
    while depth:
        depth += (src[pos] == '{') - (src[pos] == '}')
        pos += 1
    return src[m.start():pos]

assert 'update_hamt_en(env);' in helper[helper.index('void x86_cpu_set_a20('):helper.index('void cpu_x86_update_cr0(')]

# Initialization must turn off both unvalidated refill walkers and reject TU
# or AOT; this check is structural, not an execution of the option parser.
init = options[options.index('static void latx_sys_check_options(void)'):]
init = init[:init.index('if (option_pcid &&')]
for required in ('hamt_allow_long_no_pse()', 'tu_runtime_enabled()',
                 'option_sys_aot', 'option_aot_save', 'option_aot_load',
                 'option_aot_fork', 'option_hamt_config &= ~0xc0',
                 'option_fast_itlb_ptw = 0',
                 'option_hamt_config &= ~HAMT_CONFIG_LONG_NO_PSE'):
    assert required in init, required

support = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#define HAMT_CONFIG_LONG_NO_PSE (1U << 18)
#define PG_MODE_PAE (1 << 0)
#define PG_MODE_LMA (1 << 1)
#define PG_MODE_NXE (1 << 2)
#define PG_MODE_PSE (1 << 3)
#define PG_MODE_LA57 (1 << 4)
#define PG_MODE_WP (1 << 16)
#define PG_MODE_PKE (1 << 17)
#define PG_MODE_PKS (1 << 18)
#define PG_MODE_SMEP (1 << 19)
static int option_hamt_config;
#define HF_HAMT_EN_MASK (1U << 30)
typedef struct { bool watchpoints; } CPUState;
typedef struct {
    unsigned hflags, a20_mask;
    int pg_mode;
    CPUState cpu;
} CPUX86State;
#define QTAILQ_EMPTY(head) (!*(head))
static CPUState *env_cpu(CPUX86State *env) { return &env->cpu; }
static bool hamt_enabled(void) { return true; }
static int get_pg_mode(CPUX86State *env) { return env->pg_mode; }
'''
checks = r'''
int main(void) {
    const int req = PG_MODE_PAE | PG_MODE_LMA | PG_MODE_NXE | PG_MODE_WP;
    option_hamt_config = 0x3329;
    assert(!x86_hamt_data_mode_supported(req));
    assert(x86_hamt_data_mode_supported(req | PG_MODE_PSE));
    assert(x86_hamt_data_mode_supported(req | PG_MODE_PSE | PG_MODE_SMEP));
    assert(hamt_do_cpl(0) && hamt_do_cpl(3));
    option_hamt_config |= HAMT_CONFIG_LONG_NO_PSE;
    assert(x86_hamt_data_mode_supported(req));
    assert(x86_hamt_data_mode_supported(req | PG_MODE_SMEP));
    assert(x86_hamt_data_mode_supported(req | PG_MODE_PSE));
    assert(!hamt_do_cpl(0) && !hamt_do_cpl(1) && !hamt_do_cpl(2));
    assert(hamt_do_cpl(3));
    const int bad[] = { PG_MODE_LA57, PG_MODE_PKE, PG_MODE_PKS };
    for (unsigned i = 0; i < sizeof(bad)/sizeof(bad[0]); i++) {
        assert(!x86_hamt_data_mode_supported(req | bad[i]));
    }
    for (int bit = 0; bit < 4; bit++) {
        const int required[] = { PG_MODE_PAE, PG_MODE_LMA, PG_MODE_NXE, PG_MODE_WP };
        assert(!x86_hamt_data_mode_supported(req & ~required[bit]));
    }
    CPUX86State env = { .a20_mask = 1U << 20, .pg_mode = req };
    assert(update_hamt_en(&env) && (env.hflags & HF_HAMT_EN_MASK));
    env.a20_mask = 0;
    assert(update_hamt_en(&env) && !(env.hflags & HF_HAMT_EN_MASK));
    env.a20_mask = 1U << 20;
    assert(update_hamt_en(&env) && (env.hflags & HF_HAMT_EN_MASK));
    env.cpu.watchpoints = true;
    assert(update_hamt_en(&env) && !(env.hflags & HF_HAMT_EN_MASK));
    env.cpu.watchpoints = false;
    assert(update_hamt_en(&env) && (env.hflags & HF_HAMT_EN_MASK));
    env.pg_mode |= PG_MODE_LA57;
    assert(update_hamt_en(&env) && !(env.hflags & HF_HAMT_EN_MASK));
    puts("HAMT long mode no-PSE opt-in, A20 and CPL gate: PASS");
}
'''
with tempfile.TemporaryDirectory() as d:
    source = pathlib.Path(d) / 'test.c'
    exe = pathlib.Path(d) / 'test'
    source.write_text(support + extract(hamt, 'hamt_allow_long_no_pse') +
                      extract(hamt, 'hamt_do_cpl') +
                      extract(cpu, 'x86_hamt_data_mode_supported') +
                      extract_update(segment) + checks)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', str(source), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
