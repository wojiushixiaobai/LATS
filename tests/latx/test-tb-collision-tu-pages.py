#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2024-2026 LATS Project Authors
# SPDX-License-Identifier: GPL-2.0-only
"""Run the ordinary TB generator on a deterministic registration collision.

The guest mappings and the competing publisher are fixtures. The TB key
comparison, TU page check and generator (including its retry) come from the
source tree. Pass a source root to exercise another revision with the same test.
"""
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[2]


def function(path, signature):
    source = (root / path).read_text()
    start = source.index(signature)
    return source[start:source.index('\n}', start) + 2] + '\n'


header = (root / 'include/exec/translation-block.h').read_text()
codec_start = header.index('#define TU_PAGE_DESC_PAGE_BITS')
page_codec = header[codec_start:header.index('\n#endif', codec_start)]
page_mix = header[header.index('#define tb_mix_page_addr_n'):
                  header.index('#define TB_OPT_MMIO_NONE')]
comparator = function('accel/tcg/tb-maint.c', 'static bool tb_cmp(')
validator = function('target/i386/latx/optimization/sys-tu.c', 'bool tu_pages_valid(')
generator = function('accel/tcg/translate-all.c', 'TranslationBlock *lat_tb_gen_code(')

fixture = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CONFIG_SOFTMMU 1
#define CONFIG_LATX_TU 1
#define TARGET_PAGE_MASK (~UINT64_C(0xfff))
#define MAX_PAGE_IN_TU 3
#define HF_FORCE_TB_MODE (UINT32_C(1) << 31)
#define CF_COUNT_MASK 127
#define CF_LAST_IO (1u << 15)
#define CF_INVALID (1u << 18)
#define CF_PCREL (1u << 21)
#define TCG_MAX_INSNS 128
typedef uint64_t tb_page_addr_t;
typedef uint64_t vaddr;

typedef struct {
    struct { uint64_t vp_phys_hi, mix; } pages[MAX_PAGE_IN_TU];
} TranslationUnitPages;
typedef struct {
    vaddr pc;
    uint64_t cs_base;
    uint32_t flags, cflags;
    tb_page_addr_t entry_page, second_page;
    bool is_tu;
    TranslationUnitPages *pages;
} TranslationBlock;
typedef struct {
    uint32_t hflags;
    tb_page_addr_t successor;
} CPUArchState, CPUX86State;
typedef struct { CPUArchState *env_ptr; int exception_index; } CPUState;

enum { ENTRY = 0x1000, SUCCESSOR = 0x2000, ENTRY_FRAME = 0x4000,
       ORIGINAL_FRAME = 0x5000, OTHER_FRAME = 0x6000, USER_FLAGS = 3 };
static TranslationBlock winner, forced;
static bool page_locked, forced_cached, retry_page_busy, lookup_held_lock;
static bool code_published, code_protected;
static unsigned generations, collisions, protections;
#define tb_cflags(tb) ((tb)->cflags)
#define tb_page_addr0(tb) ((tb)->entry_page)
#define tb_page_addr1(tb) ((tb)->second_page)
#define tb_is_tu(tb) ((tb)->is_tu)
#define TB_TU_PAGES(tb) ((tb)->pages)
#define tu_pages_cache_hit(env, desc, slot) ((void)(slot), false)
#define tu_pages_cache_update(env, slot) ((void)0)

static tb_page_addr_t get_page_addr_code_noexit(CPUArchState *env, vaddr pc)
{
    assert(!page_locked);
    assert(pc == ENTRY || pc == SUCCESSOR);
    return pc == ENTRY ? ENTRY_FRAME : env->successor;
}
''' + page_codec + '\n' + page_mix + '\n' + comparator + validator + r'''
enum { GENCODE_ERR_NONE, GENCODE_ERR_TB_EXIST, GENCODE_ERR_FLUSH1,
       GENCODE_ERR_FLUSH2, GENCODE_ERR_BUF_OVERFLOW, GENCODE_ERR_TB_ONESHOT };
static struct { tb_page_addr_t pre_locked_page0, pre_locked_page1; } context;
static typeof(context) *tcg_ctx, *tcg_ctx_mtcc[] = { &context };
static struct { int is_tu_mode; } data;
static struct { typeof(data) *tr_data; } ls = { &data }, *lsenv = &ls;
#define curr_mtcc(env) 0
#define lal_level(level) false
#define tb_page_locked(phys) page_locked
#define QEMU_BUILD_BUG_ON(e) _Static_assert(!(e), #e)
#define assert_memory_lock() ((void)0)
#define qemu_thread_jit_write() ((void)0)
#define tb_unlock_page0(phys) tb_unlock_ctx_pages(1)
#define assert_no_pages_locked() assert(!page_locked)
#define qatomic_set(ptr, value) abort()
#define tb_flush_mtcc(cpu, id) abort()
#define mmap_unlock() abort()
#define EXCP_INTERRUPT 1
#define cpu_loop_exit(cpu) abort()
#define g_assert_not_reached() abort()
#define option_rastoptp true
#define option_soft_ras false
#define add_rel_entry(tb) ((void)0)
#define lat_ras_pretranslate_locked(cpu, tb, phys, host) abort()

static tb_page_addr_t get_page_addr_code_hostp(CPUArchState *env, vaddr pc,
                                              void **host)
{
    static unsigned char code;
    *host = &code;
    return get_page_addr_code_noexit(env, pc);
}

static bool tb_trylock_page0(tb_page_addr_t phys)
{
    assert(phys == ENTRY_FRAME && !page_locked);
    if (retry_page_busy && collisions) {
        return true;
    }
    page_locked = true;
    return false;
}

static void tb_lock_page0(tb_page_addr_t phys)
{
    assert(phys == ENTRY_FRAME && !page_locked && retry_page_busy);
    page_locked = true;
}

static void tb_unlock_ctx_pages(int all)
{
    assert(all && page_locked);
    page_locked = false;
}

static bool tb_page_has_code_locked(tb_page_addr_t phys)
{
    assert(phys == ENTRY_FRAME && page_locked);
    return code_published;
}

static bool tlb_code_page_is_protected(tb_page_addr_t phys)
{
    assert(phys == ENTRY_FRAME && page_locked);
    return code_protected;
}

static void tlb_protect_code(tb_page_addr_t phys)
{
    assert(phys == ENTRY_FRAME && !page_locked);
    code_protected = true;
    protections++;
}

static TranslationBlock *tb_lookup(CPUState *cpu, vaddr pc, uint64_t cs,
                                   uint32_t flags, int cflags)
{
    assert(flags == cpu->env_ptr->hflags);
    assert(pc == ENTRY && cs == 0 && cflags == 0);
    lookup_held_lock |= page_locked;
    if (flags == winner.flags) {
        /* A jump-cache hit can return the winner without checking its TU. */
        return &winner;
    }
    assert(flags == forced.flags);
    return forced_cached ? &forced : NULL;
}

static TranslationBlock *lat_tb_gen_code_locked(CPUState *cpu, vaddr pc,
        uint64_t cs, uint32_t flags, int cflags, tb_page_addr_t phys,
        void *host, int *error)
{
    TranslationBlock key = { .pc = pc, .cs_base = cs, .flags = flags,
        .cflags = cflags, .entry_page = phys, .second_page = -1 };
    assert(host && page_locked && code_protected && flags == cpu->env_ptr->hflags);
    generations++;
    assert(generations <= 2);
    /* Publish the competing entry before the ordinary key is inserted. */
    if (tb_cmp(&key, &winner)) {
        code_published = true;
        collisions++;
        *error = GENCODE_ERR_TB_EXIST;
        return &winner;
    }
    assert(tb_cmp(&key, &forced));
    *error = forced_cached ? GENCODE_ERR_TB_EXIST : GENCODE_ERR_NONE;
    return &forced;
}
''' + generator + r'''
static bool check(const char *name, bool is_tu, tb_page_addr_t successor,
                  bool cached)
{
    /* The entry TB fits on one page. Only its TU depends on SUCCESSOR. */
    TranslationUnitPages desc = { .pages = {
        { tu_page_desc_pack_vp(ENTRY, ENTRY_FRAME),
          tb_cal_page_addr_mix(ENTRY, ENTRY_FRAME) },
        { tu_page_desc_pack_vp(SUCCESSOR, ORIGINAL_FRAME),
          tb_cal_page_addr_mix(SUCCESSOR, ORIGINAL_FRAME) },
        { UINT64_MAX, 0 }
    } };
    CPUArchState env = { .hflags = USER_FLAGS, .successor = successor };
    CPUState cpu = { .env_ptr = &env };
    bool retry = is_tu && successor != ORIGINAL_FRAME;
    winner = (TranslationBlock){ .pc = ENTRY, .flags = env.hflags,
        .entry_page = ENTRY_FRAME, .second_page = -1,
        .is_tu = is_tu, .pages = is_tu ? &desc : NULL };
    forced = winner;
    forced.flags |= HF_FORCE_TB_MODE;
    forced.is_tu = false;
    forced.pages = NULL;
    forced_cached = cached;
    lookup_held_lock = false;
    code_published = code_protected = false;
    generations = collisions = protections = 0;
    TranslationBlock *result = lat_tb_gen_code(&cpu, ENTRY, 0, env.hflags, 0);
    bool ok = result == (retry ? &forced : &winner) &&
              env.hflags == (retry ? forced.flags : winner.flags) &&
              collisions == 1 && generations == (retry && !cached ? 2u : 1u) &&
              protections == 1 && !lookup_held_lock && !page_locked;
    printf("%s %s (returned %s, generations %u, lookup held lock %d)\n",
           ok ? "PASS" : "FAIL", name, result == &winner ? "winner" : "forced TB",
           generations, lookup_held_lock);
    return ok;
}

int main(void)
{
    bool ok = check("plain TB", false, OTHER_FRAME, false);
    ok &= check("TU with matching pages", true, ORIGINAL_FRAME, false);
    ok &= check("TU with remapped successor", true, OTHER_FRAME, false);
    ok &= check("TU with unmapped successor", true, (tb_page_addr_t)-1, false);
    ok &= check("remapped successor with cached forced TB", true,
                OTHER_FRAME, true);
    retry_page_busy = true;
    ok &= check("busy retry page with cached forced TB", true,
                OTHER_FRAME, true);
    ok &= check("busy retry page without cached forced TB", true,
                OTHER_FRAME, false);
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
'''

with tempfile.TemporaryDirectory(prefix='lats-tb-tu-pages-') as temp:
    source = Path(temp) / 'collision.c'
    binary = Path(temp) / 'collision'
    source.write_text(fixture)
    subprocess.run([*shlex.split(os.environ.get('CC', 'cc')), '-std=gnu11',
                    '-Wall', '-Wextra', '-Werror', '-Wno-sign-compare',
                    str(source), '-o', str(binary)],
                   check=True)
    sys.exit(subprocess.run([str(binary)], timeout=10).returncode)
