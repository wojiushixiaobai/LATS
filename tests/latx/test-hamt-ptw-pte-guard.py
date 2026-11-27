#!/usr/bin/env python3
"""Compile and exercise the production HAMT C-walker PTE safety predicate."""
import pathlib
import re
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
src = (root / 'target/i386/hamt/hamt-ptw.c').read_text()
m = re.search(r'static bool hamt_ptw_pte_direct_ok\([^;]*?\)\s*\{', src)
assert m
pos, depth = m.end(), 1
while depth:
    depth += (src[pos] == '{') - (src[pos] == '}')
    pos += 1
function = src[m.start():pos]
# Each raw page-table dereference must have its own production guard.
walker = src[src.index('static bool hamt_do_ptw('):src.index('bool hamt_try_do_ptw(')]
assert walker.count('if (!hamt_ptw_pte_direct_ok(env, pte_addr))') == 4
assert walker.count('pte = *((uint64_t *)haddr);') == 4
support = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define CONFIG_LATX_HACK_MEM 1
#define TARGET_PAGE_BITS 12
#define qatomic_read(p) __atomic_load_n((p), __ATOMIC_SEQ_CST)
typedef struct { void *ptw4k; } CPUX86State;
static uint8_t map[2048];
static uint8_t *ptwdata = map;
static uint64_t ptwdata_size = sizeof(map);
static uint64_t latx_hack_ram_below_4g_end = 0x200000;
static uint64_t latx_hack_ram_above_4g_start = 0x400000;
static uint64_t latx_hack_ram_above_4g_end = 0x800000;
'''
checks = r'''
int main(void) {
    CPUX86State env = { .ptw4k = map };
    assert(hamt_ptw_pte_direct_ok(&env, 0x100000));
    assert(hamt_ptw_pte_direct_ok(&env, 0x1ffff8));
    assert(!hamt_ptw_pte_direct_ok(&env, 0xfffff));
    assert(!hamt_ptw_pte_direct_ok(&env, 0x1ffff9));
    assert(!hamt_ptw_pte_direct_ok(&env, 0x200000));
    assert(hamt_ptw_pte_direct_ok(&env, 0x400000));
    assert(hamt_ptw_pte_direct_ok(&env, 0x7ffff8));
    assert(!hamt_ptw_pte_direct_ok(&env, 0x800000));
    assert(!hamt_ptw_pte_direct_ok(&env, 0x840000000ULL));
    assert(!hamt_ptw_pte_direct_ok(&env, UINT64_MAX - 6));
    for (int state=1; state<=4; state++) {
        map[0x400000 >> 12] = state;
        assert(!hamt_ptw_pte_direct_ok(&env, 0x400000));
    }
    map[0x400000 >> 12] = 0;
    map[0x401000 >> 12] = 1;
    assert(!hamt_ptw_pte_direct_ok(&env, 0x400ffc));
    map[0x401000 >> 12] = 0;
    env.ptw4k = NULL;
    assert(!hamt_ptw_pte_direct_ok(&env, 0x400000));
    puts("HAMT C PTE range and protection guard: PASS");
}
'''
with tempfile.TemporaryDirectory() as d:
    c = pathlib.Path(d) / 'test.c'
    exe = pathlib.Path(d) / 'test'
    c.write_text(support + function + checks)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
