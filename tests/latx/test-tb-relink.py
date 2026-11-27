#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2024-2026 LATS Project Authors
#
# SPDX-License-Identifier: GPL-2.0-only

"""Compile production jump linking/unlinking with a small patching fixture.

Usage: python3 tests/latx/test-tb-relink.py [source-root]
The fixture replaces code patching and HRAS lookup; eligibility, destination
claiming and reverse-list maintenance use the actual production functions.
"""
import os
import pathlib
import re
import shlex
import subprocess
import sys
import tempfile

root = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(__file__).resolve().parents[2]
source = (root / 'accel/tcg/cpu-exec.c').read_text()

def function(name, text=source):
    match = re.search(r'(?:static )?(?:bool|void) ' + name + r'\([^;]*?\)\s*\{', text)
    if not match:
        raise RuntimeError('missing production function: ' + name)
    start = match.start()
    end = match.end()
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

support = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <pthread.h>
#define CONFIG_LATX_TU 1
#define CONFIG_SOFTMMU 1
#define CF_INVALID 1
#define CF_USE_ICOUNT 2
#define MF_END_SYSCALL 0x4000
#define MF_LINK_STATUS 1
#define MF_LINK0_MASK 0x00f00000
#define MF_LINK1_MASK 0x000f0000
#define MF_LINK0_SHIFT 20
#define MF_LINK1_SHIFT 16
#define MF_MTCC_ID_MASK 0x0f000000
#define MF_MTCC_ID_SHIFT 24
#define IS_AOT_TB 1
#define IS_HRAS_CALL 2
#define TB_JMP_OFFSET_INVALID -1
#define CPU_LOG_EXEC 1
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define unlikely(a) (a)
#define qatomic_read(p) __atomic_load_n(p, __ATOMIC_SEQ_CST)
#define qatomic_set(p,v) __atomic_store_n(p,v,__ATOMIC_SEQ_CST)
#define qatomic_cmpxchg(p,o,v) ({ __typeof__(*(p)) exp_=(o); \
 __atomic_compare_exchange_n(p,&exp_,v,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST); exp_; })
#define qatomic_or(p,v) __atomic_fetch_or(p,v,__ATOMIC_SEQ_CST)
#define g_assert_not_reached() assert(0)
#define TB_FOR_EACH_JMP(d,t,n) \
    for (uintptr_t p_=(d)->jmp_list_head; p_ && \
         (((t)=(void *)(p_ & ~(uintptr_t)1)), ((n)=p_ & 1), 1); \
         p_=(t)->jmp_list_next[n])
typedef struct TranslationBlock {
    uint32_t flags, cflags, misc_flags, bool_flags;
    uintptr_t jmp_dest[2], jmp_list_next[2], jmp_list_head;
    int jmp_insn_offset[2];
    pthread_mutex_t jmp_lock;
    struct { void *ptr; } tc;
} TranslationBlock;
static struct { struct { void *syscall_tb; } *env_ptr; } cpu;
#define current_cpu (&cpu)
static int option_opt_syscall, option_tu_hras, mode = 2;
static bool mtcc, prepare_ok = true;
static TranslationBlock *native_target;
static bool qemu_tcg_mode_1_tcg(void) { return mode == 1; }
static bool qemu_tcg_mode_2_lat(void) { return mode == 2; }
static bool qemu_tcg_mtcc_enabled(void) { return mtcc; }
static bool check_misc_flags(TranslationBlock *tb, uint32_t f) { return tb->misc_flags & f; }
static uint32_t get_mask_from_miscflags(const TranslationBlock *tb,uint32_t m,int s) { return (tb->misc_flags&m)>>s; }
static bool latx_aot_native_jump_target(TranslationBlock *tb) { return tb == native_target; }
static bool latxs_aot_direct_call_entry(TranslationBlock *a,int n,TranslationBlock *b) { (void)a;(void)n;(void)b;return false; }
static TranslationBlock *latxs_hras_local_target(TranslationBlock *a,TranslationBlock *b) { (void)a;return b; }
static bool latxs_hras_prepare_link(void *c,TranslationBlock *a,int n,TranslationBlock *b) { (void)c;(void)a;(void)n;(void)b;return prepare_ok; }
static void qemu_thread_jit_write(void) {}
static void qemu_spin_lock(pthread_mutex_t *p) { assert(!pthread_mutex_lock(p)); }
static void qemu_spin_unlock(pthread_mutex_t *p) { assert(!pthread_mutex_unlock(p)); }
static void tb_set_jmp_target(TranslationBlock *a,int n,uintptr_t p) { (void)a;(void)n;(void)p; }
static void latx_tb_set_jmp_target(TranslationBlock *a,int n,TranslationBlock *b) { (void)a;(void)n;(void)b; }
static void latx_tb_reset_jmp_target(TranslationBlock *tb,int n) { (void)tb;(void)n; }
/* P2 (option sysflagtb) hooks used by tb_add_jump(); the fixture does not
 * model deferred M-EFLAGS so the veto is always permissive and the post-link
 * invariant is a no-op. */
static bool latx_tb_eflag_link_allowed(TranslationBlock *a,int n,TranslationBlock *b) { (void)a;(void)n;(void)b;return true; }
static void latx_tb_eflag_stat_link(TranslationBlock *a,int n,TranslationBlock *b) { (void)a;(void)n;(void)b; }
#define qemu_log_mask(...) ((void)0)
'''

checks = r'''
static TranslationBlock src, dst[2];
static void init_tb(TranslationBlock *tb) {
    *tb = (TranslationBlock){0};
    assert(!pthread_mutex_init(&tb->jmp_lock, NULL));
}
static void *repeat(void *arg) {
    int n = (uintptr_t)arg;
    for (unsigned round = 0; round < 10000; round++) {
        tb_add_jump(&src,n,&dst[n]);
        assert(qatomic_read(&src.jmp_dest[n]) == (uintptr_t)&dst[n]);
        /* An occupied slot must retain the winner and its reverse link. */
        tb_add_jump(&src,n,&dst[!n]);
        assert(qatomic_read(&src.jmp_dest[n]) == (uintptr_t)&dst[n]);
        unlink_old_dest(&src,&dst[n],n);
        assert(!qatomic_read(&src.jmp_dest[n]));
    }
    return NULL;
}
int main(void) {
    init_tb(&src); init_tb(&dst[0]); init_tb(&dst[1]);
    src.misc_flags = 0xa0ff0800; /* old saturated counts and RAS bits */
    pthread_t threads[2];
    for (uintptr_t n=0;n<2;n++) assert(!pthread_create(&threads[n],NULL,repeat,(void *)n));
    for (unsigned n=0;n<2;n++) assert(!pthread_join(threads[n],NULL));
    assert(src.misc_flags == 0xa0ff0801);
    src.jmp_dest[0]=0;
    assert(!dst[0].jmp_list_head && !dst[1].jmp_list_head);
    /* Cross-page check failures can alternate between two still-valid TBs. */
    for (unsigned round=0;round<10000;round++) {
        TranslationBlock *target=&dst[round&1];
        tb_add_jump(&src,0,target);
        assert(src.jmp_dest[0] == (uintptr_t)target);
        unlink_old_dest(&src,target,0);
    }
    src.jmp_dest[0]=1;
    tb_add_jump(&src,0,&dst[0]); assert(src.jmp_dest[0]==1);
    src.jmp_dest[0]=0;
    uint32_t saved=src.misc_flags;
    dst[0].cflags=CF_INVALID;
    tb_add_jump(&src,0,&dst[0]); assert(src.misc_flags==saved && !src.jmp_dest[0]);
    dst[0].cflags=0; dst[0].flags=1;
    tb_add_jump(&src,0,&dst[0]); assert(src.misc_flags==saved && !src.jmp_dest[0]);
    dst[0].flags=0; mtcc=true; dst[0].misc_flags=0x01000000;
    tb_add_jump(&src,0,&dst[0]); assert(src.misc_flags==saved && !src.jmp_dest[0]);
    mtcc=false; native_target=&dst[0];
    tb_add_jump(&src,0,&dst[0]); assert(src.misc_flags==saved && !src.jmp_dest[0]);
    native_target=NULL; option_tu_hras=1; prepare_ok=false;
    tb_add_jump(&src,0,&dst[0]); assert(src.misc_flags==saved && !src.jmp_dest[0]);
    option_tu_hras=0; prepare_ok=true;
    src.cflags=CF_USE_ICOUNT;
    tb_add_jump(&src,0,&dst[0]); assert(src.jmp_dest[0] == (uintptr_t)&dst[0]);
    unlink_old_dest(&src,&dst[0],0); src.cflags=0; mode=1;
    tb_add_jump(&src,0,&dst[0]); assert(src.jmp_dest[0] == (uintptr_t)&dst[0]);
    puts("PASS: immediate relink after 20,000 unlinks and 10,000 target switches; occupied/tagged slots and eligibility");
}
'''

with tempfile.TemporaryDirectory(prefix='tb-relink-test-') as temp:
    path = pathlib.Path(temp)
    c = path / 'test.c'
    retry = function('tb_link_retry_ready') if 'static bool tb_link_retry_ready(' in source else ''
    unlink = function('unlink_old_dest', (root / 'accel/tcg/tb-maint.c').read_text())
    c.write_text(support + retry + '\n' + function('tb_add_jump') + unlink + checks)
    command = shlex.split(os.environ.get('CC', 'cc'))
    subprocess.run(command + ['-std=gnu11','-O2','-Wall','-Wextra','-Werror','-Wno-sign-compare','-pthread',str(c),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
