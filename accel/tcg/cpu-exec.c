/*
 *  emulator main execution loop
 *
 *  Copyright (c) 2003-2005 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "sys-aot.h"
#include "latx-sigrt-sys.h"
#include "hamt.h"
#include "qemu/osdep.h"
#include "qemu/qemu-print.h"
#include "qapi/error.h"
#include "qapi/type-helpers.h"
#include "hw/core/tcg-cpu-ops.h"
#include "trace.h"
#include "disas/disas.h"
#include "exec/exec-all.h"
#include "tcg/tcg.h"
#include "qemu/atomic.h"
#include "qemu/rcu.h"
#include "exec/log.h"
#include "qemu/main-loop.h"
#if defined(TARGET_I386) && !defined(CONFIG_USER_ONLY)
#include "hw/i386/apic.h"
#endif
#include "sysemu/cpus.h"
#include "exec/cpu-all.h"
#include "sysemu/cpu-timers.h"
#include "exec/replay-core.h"
#include "sysemu/tcg.h"
#include "exec/helper-proto-common.h"
#include "tb-jmp-cache.h"
#include "tb-hash.h"
#include "tb-context.h"
#include "internal.h"
#include "tcg/tcg-split-tb.h"
#include "qemu/cacheflush.h"
#include "qemu/memalign.h"
#include "hamt-dmw.h"
#include "latx-intb-sys.h"
#include "latx-hack.h"

#include "latx/include/latx-ras.h"
#include "latx/include/latx-misc.h"
#include "latx/include/latx-options.h"
#include "latx/include/latx-shadow-htable.h"
#include "latx/include/latx-config.h"
#include "latx/include/lsenv.h"
#include "latx/include/latx-sys-mmio.h"
#include "latx/include/latx-native-asm.h"

#ifdef CONFIG_LATX_TU
#include "latx-tu.h"
#endif
/* -icount align implementation. */

typedef struct SyncClocks {
    int64_t diff_clk;
    int64_t last_cpu_icount;
    int64_t realtime_clock;
} SyncClocks;

#if !defined(CONFIG_USER_ONLY)
/* Allow the guest to have a max 3ms advance.
 * The difference between the 2 clocks could therefore
 * oscillate around 0.
 */
#define VM_CLOCK_ADVANCE 3000000
#define THRESHOLD_REDUCE 1.5
#define MAX_DELAY_PRINT_RATE 2000000000LL
#define MAX_NB_PRINTS 100


int64_t max_delay;
int64_t max_advance;

static void align_clocks(SyncClocks *sc, CPUState *cpu)
{
    int64_t cpu_icount;

    if (!icount_align_option) {
        return;
    }

    cpu_icount = cpu->icount_extra + cpu_neg(cpu)->icount_decr.u16.low;
    sc->diff_clk += icount_to_ns(sc->last_cpu_icount - cpu_icount);
    sc->last_cpu_icount = cpu_icount;

    if (sc->diff_clk > VM_CLOCK_ADVANCE) {
#ifndef _WIN32
        struct timespec sleep_delay, rem_delay;
        sleep_delay.tv_sec = sc->diff_clk / 1000000000LL;
        sleep_delay.tv_nsec = sc->diff_clk % 1000000000LL;
        if (nanosleep(&sleep_delay, &rem_delay) < 0) {
            sc->diff_clk = rem_delay.tv_sec * 1000000000LL + rem_delay.tv_nsec;
        } else {
            sc->diff_clk = 0;
        }
#else
        Sleep(sc->diff_clk / SCALE_MS);
        sc->diff_clk = 0;
#endif
    }
}

static void print_delay(const SyncClocks *sc)
{
    static float threshold_delay;
    static int64_t last_realtime_clock;
    static int nb_prints;

    if (icount_align_option &&
        sc->realtime_clock - last_realtime_clock >= MAX_DELAY_PRINT_RATE &&
        nb_prints < MAX_NB_PRINTS) {
        if ((-sc->diff_clk / (float)1000000000LL > threshold_delay) ||
            (-sc->diff_clk / (float)1000000000LL <
             (threshold_delay - THRESHOLD_REDUCE))) {
            threshold_delay = (-sc->diff_clk / 1000000000LL) + 1;
            qemu_printf("Warning: The guest is now late by %.1f to %.1f seconds\n",
                        threshold_delay - 1,
                        threshold_delay);
            nb_prints++;
            last_realtime_clock = sc->realtime_clock;
        }
    }
}

static void init_delay_params(SyncClocks *sc, CPUState *cpu)
{
    if (!icount_align_option) {
        return;
    }
    sc->realtime_clock = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL_RT);
    sc->diff_clk = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) - sc->realtime_clock;
    sc->last_cpu_icount
        = cpu->icount_extra + cpu_neg(cpu)->icount_decr.u16.low;
    if (sc->diff_clk < max_delay) {
        max_delay = sc->diff_clk;
    }
    if (sc->diff_clk > max_advance) {
        max_advance = sc->diff_clk;
    }

    /* Print every 2s max if the guest is late. We limit the number
       of printed messages to NB_PRINT_MAX(currently 100) */
    print_delay(sc);
}
#else
static void align_clocks(SyncClocks *sc, const CPUState *cpu)
{
}

static void init_delay_params(SyncClocks *sc, const CPUState *cpu)
{
}
#endif /* CONFIG USER ONLY */

uint32_t curr_cflags(CPUState *cpu)
{
    uint32_t cflags = cpu->tcg_cflags;

    /*
     * Record gdb single-step.  We should be exiting the TB by raising
     * EXCP_DEBUG, but to simplify other tests, disable chaining too.
     *
     * For singlestep and -d nochain, suppress goto_tb so that
     * we can log -d cpu,exec after every TB.
     */
    if (unlikely(cpu->singlestep_enabled)) {
        cflags |= CF_NO_GOTO_TB | CF_NO_GOTO_PTR | CF_SINGLE_STEP | 1;
    } else if (qatomic_read(&one_insn_per_tb)) {
        cflags |= CF_NO_GOTO_TB | 1;
    } else if (qemu_loglevel_mask(CPU_LOG_TB_NOCHAIN)) {
        cflags |= CF_NO_GOTO_TB;
        cflags |= CF_NO_GOTO_PTR;
    }

    return cflags;
}

struct tb_desc {
    vaddr pc;
    uint64_t cs_base;
    CPUArchState *env;
    tb_page_addr_t page_addr0;
    uint32_t flags;
    uint32_t cflags;
};

static bool tb_lookup_cmp(const void *p, const void *d)
{
    const TranslationBlock *tb = p;
    const struct tb_desc *desc = d;

    if ((tb_cflags(tb) & CF_PCREL || tb->pc == desc->pc) &&
        tb_page_addr0(tb) == desc->page_addr0 &&
        tb->cs_base == desc->cs_base &&
        tb->flags == desc->flags &&
        tb_cflags(tb) == desc->cflags) {
        /* check next page if needed */
        tb_page_addr_t tb_phys_page1 = tb_page_addr1(tb);
        if (tb_phys_page1 == -1) {
            return true;
        } else {
            tb_page_addr_t phys_page1;
            vaddr virt_page1;

            /*
             * We know that the first page matched, and an otherwise valid TB
             * encountered an incomplete instruction at the end of that page,
             * therefore we know that generating a new TB from the current PC
             * must also require reading from the next page -- even if the
             * second pages do not match, and therefore the resulting insn
             * is different for the new TB.  Therefore any exception raised
             * here by the faulting lookup is not premature.
             */
            virt_page1 = TARGET_PAGE_ALIGN(desc->pc);
            phys_page1 = get_page_addr_code(desc->env, virt_page1);
            if (tb_phys_page1 == phys_page1) {
                return true;
            }
        }
    }
    return false;
}

static bool tb_lookup_cmp_noexit(const void *p, const void *d)
{
    const TranslationBlock *tb = p;
    const struct tb_desc *desc = d;

    if ((tb_cflags(tb) & CF_PCREL || tb->pc == desc->pc) &&
        tb_page_addr0(tb) == desc->page_addr0 &&
        tb->cs_base == desc->cs_base &&
        tb->flags == desc->flags &&
        tb_cflags(tb) == desc->cflags) {
        /* check next page if needed */
        tb_page_addr_t tb_phys_page1 = tb_page_addr1(tb);
        if (tb_phys_page1 == -1) {
            return true;
        } else {
            tb_page_addr_t phys_page1;
            vaddr virt_page1;
            virt_page1 = TARGET_PAGE_ALIGN(desc->pc);
            phys_page1 = get_page_addr_code_noexit(desc->env, virt_page1);
            if (tb_phys_page1 == phys_page1) {
                return true;
            }
        }
    }
    return false;
}

static bool aot_tb_lookup_cmp(const void *p, const void *d)
{
    const TranslationBlock *tb = p;
    const struct tb_desc *desc = d;
    if ((tb_cflags(tb) & CF_INVALID) &&
        !(tb_cflags(tb) & CF_NO_ACTIVE)) {
        return false;
    }

    if ((tb_cflags(tb) & CF_PCREL || tb->pc == desc->pc) &&
        tb_page_addr0(tb) == desc->page_addr0 &&
        tb->cs_base == desc->cs_base &&
        tb->flags == desc->flags &&
       (tb_cflags(tb) == desc->cflags
        || tb_cflags(tb) == (desc->cflags & ~(CF_INVALID | CF_NO_ACTIVE)))
        ) {
        /* check next page if needed */
        tb_page_addr_t tb_phys_page1 = tb_page_addr1(tb);
        if (tb_phys_page1 == -1) {
            return true;
        } else {
            tb_page_addr_t phys_page1;
            vaddr virt_page1;

            /*
             * We know that the first page matched, and an otherwise valid TB
             * encountered an incomplete instruction at the end of that page,
             * therefore we know that generating a new TB from the current PC
             * must also require reading from the next page -- even if the
             * second pages do not match, and therefore the resulting insn
             * is different for the new TB.  Therefore any exception raised
             * here by the faulting lookup is not premature.
             */
            virt_page1 = TARGET_PAGE_ALIGN(desc->pc);
            phys_page1 = get_page_addr_code(desc->env, virt_page1);
            if (tb_phys_page1 == phys_page1) {
                return true;
            }
        }
    }
    return false;
}

static inline void tb_htable_reinsert_fsht(TranslationBlock *tb, vaddr pc,
                                          tb_page_addr_t phys_pc,
                                          uint32_t flags)
{
    int fid;

    if (!option_flaged_sht || tb_page_addr1(tb) != (tb_page_addr_t)-1 ||
        (tb_cflags(tb) & (CF_NO_GOTO_TB | CF_NO_GOTO_PTR))) {
        return;
    }
    fid = fsht_flags_to_id(flags);
    if (fid < 0) {
        return;
    }

    /* Reuse this lookup's PC/physical address and the constant-time flag
     * index.  No scan, out-of-line insertion call or per-TB cache is needed. */
#ifdef CONFIG_USER_ONLY
    phys_pc = pc;
#endif
    uint64_t *slot = (uint64_t *)(uintptr_t)fsht[fid].base +
                     (((pc >> 32) ^ phys_pc) & FSHT_MASK);
#ifdef CONFIG_LATX_TU
    if (tb_is_hras_private(tb)) {
        return;
    }
#endif
    qatomic_set(slot, (uintptr_t)tb);
#ifdef CONFIG_LATX_TU
    smp_mb();
    if (tb_is_hras_private(tb)) {
        qatomic_cmpxchg(slot, (uintptr_t)tb, 0);
    }
#endif
}

#if defined(CONFIG_SOFTMMU)
#define PHYSICAL_TB_CACHE_SIZE 4096

/* This is a host-thread cache of QHT results, not a guest address
 * translation cache. The caller still resolves and checks the executable
 * mapping before using it, including after a guest context switch. */
static __thread struct {
    unsigned full_epoch;
    unsigned supervisor_epoch;
    TranslationBlock *entry[PHYSICAL_TB_CACHE_SIZE];
} physical_tb_cache;

static TranslationBlock *supervisor_tb_qht_lookup(const struct tb_desc *desc,
                                                   uint32_t hash)
{
    unsigned full = qatomic_read(&tb_ctx.tb_flush_count);
    unsigned supervisor = qatomic_read(&tb_ctx.tb_flush_count_mtcc[0]);
    if (unlikely(full != physical_tb_cache.full_epoch ||
                 supervisor != physical_tb_cache.supervisor_epoch)) {
        memset(physical_tb_cache.entry, 0, sizeof(physical_tb_cache.entry));
        physical_tb_cache.full_epoch = full;
        physical_tb_cache.supervisor_epoch = supervisor;
    }

    TranslationBlock **slot = &physical_tb_cache.entry[
        hash & (PHYSICAL_TB_CACHE_SIZE - 1)];
    TranslationBlock *tb = *slot;
    /* Whole-cache recycling runs at a vCPU safe point. Individual
     * invalidations leave the TB allocated and change cflags. */
    if (tb && !(tb_cflags(tb) & CF_INVALID) &&
        tb->pc == desc->pc && tb_page_addr0(tb) == desc->page_addr0 &&
        tb->flags == desc->flags && tb_cflags(tb) == desc->cflags &&
        tb->cs_base == desc->cs_base) {
        return tb;
    }

    tb = tb_qht_lookup(&tb_ctx.htable[0], desc, hash, tb_lookup_cmp);
    if (tb && tb_page_addr1(tb) == (tb_page_addr_t)-1 &&
        !(tb->bool_flags & (IS_AOT_TB | IS_TU_TB))) {
        *slot = tb;
    }
    return tb;
}
#endif

static TranslationBlock *tb_htable_lookup_with_hint(CPUState *cpu, vaddr pc,
                                          uint64_t cs_base, uint32_t flags,
                                          uint32_t cflags,
                                          TranslationBlock *hint)
{
    tb_page_addr_t phys_pc;
    struct tb_desc desc;
    uint32_t h;

    desc.env = cpu->env_ptr;
    desc.cs_base = cs_base;
    desc.flags = flags;
    desc.cflags = cflags;
    desc.pc = pc;
    phys_pc = get_page_addr_code(desc.env, pc);
    if (phys_pc == -1) {
        return NULL;
    }
    desc.page_addr0 = phys_pc;

    TranslationBlock *tb = NULL;

    if (qemu_tcg_mtcc_enabled()) {
        uint32_t id = curr_mtcc(current_cpu->env_ptr);
        assert(id < MTCC_NR);
        if (hint &&
#ifdef CONFIG_LATX_TU
            !tb_is_hras_private(hint) &&
#endif
            get_mask_from_miscflags(hint, MF_MTCC_ID_MASK, MF_MTCC_ID_SHIFT) == id &&
            tb_lookup_cmp(hint, &desc)) {
            tb = hint;
        } else {
            h = tb_hash_func(phys_pc, (cflags & CF_PCREL ? 0 : pc),
                             flags, cs_base, cflags);
#ifdef CONFIG_SOFTMMU
            if (id == 0 && option_pcid_cache && !option_sys_aot &&
                !jit_tu_enabled() &&
                !(cflags & (CF_PCREL | CF_SINGLE_STEP | CF_USE_ICOUNT |
                            CF_NO_GOTO_TB | CF_NO_GOTO_PTR))) {
                tb = supervisor_tb_qht_lookup(&desc, h);
            } else
#endif
            {
                tb = tb_qht_lookup(&tb_ctx.htable[id], &desc, h, tb_lookup_cmp);
            }
            assert(tb == NULL || get_mask_from_miscflags(tb,
                        MF_MTCC_ID_MASK, MF_MTCC_ID_SHIFT) == id);
        }
        if (tb) {
            tb_htable_reinsert_fsht(tb, pc, phys_pc, flags);
        }
        return tb;
    } else {
        if (hint &&
#ifdef CONFIG_LATX_TU
            !tb_is_hras_private(hint) &&
#endif
            tb_lookup_cmp(hint, &desc)) {
            tb = hint;
        } else {
            h = tb_hash_func(phys_pc, (cflags & CF_PCREL ? 0 : pc),
                             flags, cs_base, cflags);
            tb = tb_qht_lookup(&tb_ctx.htable[0], &desc, h, tb_lookup_cmp);
        }
        if (tb) {
            tb_htable_reinsert_fsht(tb, pc, phys_pc, flags);
        }
        return tb;
    }
}

#ifdef CONFIG_LATX_TU
TranslationBlock *tb_htable_lookup(CPUState *cpu, vaddr pc,
                                          uint64_t cs_base, uint32_t flags,
                                          uint32_t cflags)

#else
static TranslationBlock *tb_htable_lookup(CPUState *cpu, vaddr pc,
                                          uint64_t cs_base, uint32_t flags,
                                          uint32_t cflags)
#endif
{
    return tb_htable_lookup_with_hint(cpu, pc, cs_base, flags, cflags, NULL);
}

TranslationBlock *tb_htable_lookup_noexit(CPUState *cpu, vaddr pc,
        uint64_t cs_base, uint32_t flags, uint32_t cflags)
{
    tb_page_addr_t phys_pc;
    struct tb_desc desc;
    uint32_t h;

    desc.env = cpu->env_ptr;
    desc.cs_base = cs_base;
    desc.flags = flags;
    desc.cflags = cflags;
    desc.pc = pc;
    phys_pc = get_page_addr_code_noexit(desc.env, pc);
    if (phys_pc == -1) {
        return NULL;
    }
    desc.page_addr0 = phys_pc;
    h = tb_hash_func(phys_pc, (cflags & CF_PCREL ? 0 : pc),
                     flags, cs_base, cflags);

    TranslationBlock *tb = NULL;

    if (qemu_tcg_mtcc_enabled()) {
        uint32_t id = curr_mtcc(current_cpu->env_ptr);
        assert(id < MTCC_NR);
        tb = tb_qht_lookup(&tb_ctx.htable[id], &desc, h, tb_lookup_cmp_noexit);
        assert(tb == NULL || get_mask_from_miscflags(tb,
                    MF_MTCC_ID_MASK, MF_MTCC_ID_SHIFT) == id);
        if (tb) {
            tb_htable_reinsert_fsht(tb, pc, phys_pc, flags);
        }
        return tb;
    } else {
        TranslationBlock *tb = NULL;
        tb = tb_qht_lookup(&tb_ctx.htable[0], &desc, h, tb_lookup_cmp_noexit);
        if (tb) {
            tb_htable_reinsert_fsht(tb, pc, phys_pc, flags);
        }
        return tb;
    }
}

static TranslationBlock *aot_tb_htable_lookup(CPUState *cpu, vaddr pc,
                                          uint64_t cs_base, uint32_t flags,
                                          uint32_t cflags)
{
    tb_page_addr_t phys_pc;
    struct tb_desc desc;
    uint32_t h;

    desc.env = cpu->env_ptr;
    desc.cs_base = cs_base;
    desc.flags = flags;
    desc.cflags = cflags;
    desc.pc = pc;
    phys_pc = get_page_addr_code(desc.env, pc);
    if (phys_pc == -1) {
        return NULL;
    }
    desc.page_addr0 = phys_pc;
    h = tb_hash_func(phys_pc, (cflags & CF_PCREL ? 0 : pc),
                     flags, cs_base, cflags);

    TranslationBlock *tb = NULL;

    if (qemu_tcg_mtcc_enabled()) {
        uint32_t id = curr_mtcc(current_cpu->env_ptr);
        assert(id < MTCC_NR);
        tb = tb_qht_lookup(&tb_ctx.htable[id], &desc, h, aot_tb_lookup_cmp);
        assert(tb == NULL || get_mask_from_miscflags(tb,
                    MF_MTCC_ID_MASK, MF_MTCC_ID_SHIFT) == id);
        return tb;
    } else {
        TranslationBlock *tb = NULL;
        tb = tb_qht_lookup(&tb_ctx.htable[0], &desc, h, aot_tb_lookup_cmp);
        return tb;
    }
}

#ifdef CONFIG_LATX_PRINT
int tb_lookup_cnt[8];
int tb_jc_cnt[8];
int tb_htable_cnt[8];
#endif

/*
 * Validate every page owned by the target TU.  On failure this function has
 * the side effect of setting HF_FORCE_TB_MODE in env->hflags, so the
 * dispatcher will look up or generate a single-TB translation instead.
 */
static bool cpu_tupage_check(CPUState *cpu, const TranslationBlock *tb,
                             uint32_t flags)
{
    if (tu_pages_valid(cpu, tb)) {
        return true;
    }

    if (!(flags & HF_FORCE_TB_MODE)) {
        cpu->env_ptr->hflags |= HF_FORCE_TB_MODE;
    }
    return false;
}

/*
 * Look up a TB using the mode selected by HF_FORCE_TB_MODE.  A forced-mode
 * miss temporarily clears the bit in env->hflags and *flags to retry the
 * normal TU.  A failed TU page check may set the bit again; in that case this
 * function mirrors it into *flags and retries the forced-mode TB identity.
 */
static TranslationBlock *cpu_tb_lookup(CPUState *cpu, vaddr pc,
        uint64_t cs_base, uint32_t *flags, uint32_t cflags)
{
    CPUX86State *env = cpu->env_ptr;
    bool force_tb_mode = *flags & HF_FORCE_TB_MODE;
    TranslationBlock *tb = tb_lookup(cpu, pc, cs_base, *flags, cflags);

    if (tb) {
        return tb;
    }

    if (force_tb_mode) {
        env->hflags &= ~HF_FORCE_TB_MODE;
        *flags &= ~HF_FORCE_TB_MODE;
        tb = tb_lookup(cpu, pc, cs_base, *flags, cflags);
        if (tb) {
            return tb;
        }
        if (!(env->hflags & HF_FORCE_TB_MODE)) {
            return NULL;
        }
    }

    if (env->hflags & HF_FORCE_TB_MODE) {
        *flags |= HF_FORCE_TB_MODE;
        tb = tb_lookup(cpu, pc, cs_base, *flags, cflags);
    }
    return tb;
}

/* Might cause an exception, so have a longjmp destination ready */
TranslationBlock *tb_lookup(CPUState *cpu, vaddr pc,
                                          uint64_t cs_base, uint32_t flags,
                                          uint32_t cflags)
{
    TranslationBlock *tb;
    CPUJumpCacheEntry *entry;

    /* we should never be trying to look up an INVALID tb */
    tcg_debug_assert(!(cflags & CF_INVALID));

#ifdef CONFIG_LATX_PRINT
    if (flags & HF_CPL_MASK) {
        tb_lookup_cnt[cpu->cpu_index + 4]++;
    } else {
        tb_lookup_cnt[cpu->cpu_index]++;
    }
#endif
    entry = tb_jmp_cache_entry(cpu, pc);

#ifdef LATX_PCREL
    if (cflags & CF_PCREL) {
        /* Use acquire to ensure current load of pc from jc. */
        tb = tb_jmp_cache_load(entry, true);

        if (likely(tb &&
                   entry->pc == pc &&
                   tb->cs_base == cs_base &&
                   tb->flags == flags &&
                   tb_cflags(tb) == cflags)) {
            return tb;
        }
        tb = tb_htable_lookup(cpu, pc, cs_base, flags, cflags);
        if (tucp_fast_lookup_check() && tb && TB_TU_PAGES(tb) &&
            !cpu_tupage_check(cpu, tb, flags)) {
            tb = NULL;
        }
        if (tb == NULL) {
            return NULL;
        }
#if defined(CONFIG_TB_FLAGS_NO_EFLAGS)
        if (check_misc_flags(tb, MF_FLAGS_NOEFLAGS) &&
            tu_jmp_cache_can_insert(cpu, tb)) {
            tb_jmp_cache_insert(cpu, pc, true, tb);
        }
#else
        if (tu_jmp_cache_can_insert(cpu, tb)) {
            tb_jmp_cache_insert(cpu, pc, true, tb);
        }
#endif
    } else {
#else
    {
#endif
        /* Use rcu_read to ensure current load of pc from *tb. */
        tb = tb_jmp_cache_load(entry, false);

        if (likely(tb &&
                   tb->pc == pc &&
                   tb->cs_base == cs_base &&
                   tb->flags == flags &&
                   tb_cflags(tb) == cflags)) {
#ifdef CONFIG_LATX_PRINT
            if (flags & HF_CPL_MASK) {
                tb_jc_cnt[cpu->cpu_index + 4]++;
            } else {
                tb_jc_cnt[cpu->cpu_index]++;
            }
#endif
            return tb;
        }
        tb = tb_htable_lookup(cpu, pc, cs_base, flags, cflags);
        if (tucp_fast_lookup_check() && tb && TB_TU_PAGES(tb) &&
            !cpu_tupage_check(cpu, tb, flags)) {
            tb = NULL;
        }
        if (tb == NULL) {
            return NULL;
        }
#ifdef CONFIG_LATX_PRINT
        if (flags & HF_CPL_MASK) {
            tb_htable_cnt[cpu->cpu_index + 4]++;
        } else {
            tb_htable_cnt[cpu->cpu_index]++;
        }
#endif
        /* Use the pc value already stored in tb->pc. */
#if defined(CONFIG_TB_FLAGS_NO_EFLAGS)
        if (check_misc_flags(tb, MF_FLAGS_NOEFLAGS) &&
            tu_jmp_cache_can_insert(cpu, tb)) {
            tb_jmp_cache_insert(cpu, pc, false, tb);
        }
#else
        if (tu_jmp_cache_can_insert(cpu, tb)) {
            tb_jmp_cache_insert(cpu, pc, false, tb);
        }
#endif
    }

    return tb;
}

TranslationBlock *aot_tb_lookup(CPUState *cpu, vaddr pc,
                                          uint64_t cs_base, uint32_t flags,
                                          uint32_t cflags)
{
    TranslationBlock *tb;

    if (cflags & CF_PCREL) {
        /* Use acquire to ensure current load of pc from jc. */
        tb = aot_tb_htable_lookup(cpu, pc, cs_base, flags, cflags);
        if (tb == NULL) {
            return NULL;
        }
    } else {
        tb = aot_tb_htable_lookup(cpu, pc, cs_base, flags, cflags);
        if (tb == NULL) {
            return NULL;
        }
    }

    return tb;
}

TranslationBlock *export_tb_lookup(CPUState *cpu, vaddr pc,
                                          uint64_t cs_base, uint32_t flags,
                                          uint32_t cflags)
{
    return tb_lookup(cpu, pc, cs_base, flags, cflags);
}

static void log_cpu_exec(vaddr pc, CPUState *cpu,
                         const TranslationBlock *tb)
{
    if (qemu_log_in_addr_range(pc)) {
        qemu_log_mask(CPU_LOG_EXEC,
                      "Trace %d: %p [%08" PRIx64
                      "/%016" VADDR_PRIx "/%08x/%08x] %s\n",
                      cpu->cpu_index, tb->tc.ptr, (uint64_t)tb->cs_base, pc,
                      tb->flags, tb->cflags, lookup_symbol(pc));

        if (qemu_loglevel_mask(CPU_LOG_TB_CPU)) {
            FILE *logfile = qemu_log_trylock();
            if (logfile) {
                int flags = 0;

                if (qemu_loglevel_mask(CPU_LOG_TB_FPU)) {
                    flags |= CPU_DUMP_FPU;
                }
#if defined(TARGET_I386)
                flags |= CPU_DUMP_CCOP;
#endif
                if (qemu_loglevel_mask(CPU_LOG_TB_VPU)) {
                    flags |= CPU_DUMP_VPU;
                }
                cpu_dump_state(cpu, logfile, flags);
                qemu_log_unlock(logfile);
            }
        }
    }
}

static bool check_for_breakpoints_slow(CPUState *cpu, vaddr pc,
                                       uint32_t *cflags)
{
    CPUBreakpoint *bp;
    bool match_page = false;

    /*
     * Singlestep overrides breakpoints.
     * This requirement is visible in the record-replay tests, where
     * we would fail to make forward progress in reverse-continue.
     *
     * TODO: gdb singlestep should only override gdb breakpoints,
     * so that one could (gdb) singlestep into the guest kernel's
     * architectural breakpoint handler.
     */
    if (cpu->singlestep_enabled) {
        return false;
    }

    QTAILQ_FOREACH(bp, &cpu->breakpoints, entry) {
        /*
         * If we have an exact pc match, trigger the breakpoint.
         * Otherwise, note matches within the page.
         */
        if (pc == bp->pc) {
            bool match_bp = false;

            if (bp->flags & BP_GDB) {
                match_bp = true;
            } else if (bp->flags & BP_CPU) {
#ifdef CONFIG_USER_ONLY
                g_assert_not_reached();
#else
                CPUClass *cc = CPU_GET_CLASS(cpu);
                assert(cc->tcg_ops->debug_check_breakpoint);
                match_bp = cc->tcg_ops->debug_check_breakpoint(cpu);
#endif
            }

            if (match_bp) {
                cpu->exception_index = EXCP_DEBUG;
                return true;
            }
        } else if (((pc ^ bp->pc) & TARGET_PAGE_MASK) == 0) {
            match_page = true;
        }
    }

    /*
     * Within the same page as a breakpoint, single-step,
     * returning to helper_lookup_tb_ptr after each insn looking
     * for the actual breakpoint.
     *
     * TODO: Perhaps better to record all of the TBs associated
     * with a given virtual page that contains a breakpoint, and
     * then invalidate them when a new overlapping breakpoint is
     * set on the page.  Non-overlapping TBs would not be
     * invalidated, nor would any TB need to be invalidated as
     * breakpoints are removed.
     */
    if (match_page) {
        *cflags = (*cflags & ~CF_COUNT_MASK) | CF_NO_GOTO_TB | 1;
    }
    return false;
}

static inline bool check_for_breakpoints(CPUState *cpu, vaddr pc,
                                         uint32_t *cflags)
{
    return unlikely(!QTAILQ_EMPTY(&cpu->breakpoints)) &&
        check_for_breakpoints_slow(cpu, pc, cflags);
}

/**
 * helper_lookup_tb_ptr: quick check for next tb
 * @env: current cpu state
 *
 * Look for an existing TB matching the current cpu state.
 * If found, return the code pointer.  If not found, return
 * the tcg epilogue so that we return into cpu_tb_exec.
 */
const void *HELPER(lookup_tb_ptr)(CPUArchState *env)
{
    CPUState *cpu = env_cpu(env);
    TranslationBlock *tb;
    vaddr pc;
    uint64_t cs_base;
    uint32_t flags, cflags;

    cpu_get_tb_cpu_state(env, &pc, &cs_base, &flags);

    cflags = curr_cflags(cpu);
    if (check_for_breakpoints(cpu, pc, &cflags)) {
        cpu_loop_exit(cpu);
    }

    tb = tb_lookup(cpu, pc, cs_base, flags, cflags);
    if (tb == NULL) {
        return tcg_code_gen_epilogue[curr_mtcc(env)];
    }

    if (qemu_loglevel_mask(CPU_LOG_TB_CPU | CPU_LOG_EXEC)) {
        log_cpu_exec(pc, cpu, tb);
    }

    return tb->tc.ptr;
}

#include "target/i386/latx/include/latx-helper.h"
#include "target/i386/latx/include/latx-static-codes.h"

#if defined(CONFIG_LATX_TU) && defined(CONFIG_SOFTMMU)
static void latx_aot_check_flush(CPUState *cpu, unsigned mtcc,
                                uint32_t full, uint32_t partial)
{
    if (full != qatomic_read(&tb_ctx.tb_flush_count) ||
        partial != qatomic_read(&tb_ctx.tb_flush_count_mtcc[mtcc])) {
        /* A generated resolver or an LLVM return address may have expired. */
        latxs_flush_soft_ras(cpu);
        cpu->exception_index = EXCP_INTERRUPT;
        cpu_loop_exit(cpu);
    }
}

static bool latx_aot_native_jump_target(const TranslationBlock *tb)
{
    return tb && aot_tu_enabled() && hamt_enabled() && hamt_mode_dmw() &&
        option_soft_ras && option_rastoptp &&
        option_int_opt == IRQ_CHECK_ASYNC_NOTIFY &&
        (tb->bool_flags & (IS_AOT_TB | IS_TU_TB)) == (IS_AOT_TB | IS_TU_TB) &&
        (qatomic_load_acquire(&tb->s_data->aot_native_call_entry) ||
         qatomic_load_acquire(&tb->s_data->aot_direct_native_entry));
}
#endif

#if defined(CONFIG_SOFTMMU) && defined(CONFIG_LATX_HACK_MEM) && defined(NDEBUG)
TranslationBlock *latx_supervisor_dispatch[1 << LATX_SUPERVISOR_DISPATCH_BITS];

bool latx_supervisor_dispatch_enabled(void)
{
    return qemu_tcg_mtcc_enabled() && hamt_enabled() && hamt_mode_dmw() &&
           option_pcid_cache && !option_page_pcrel &&
           !option_sys_aot && !jit_tu_enabled();
}

void latx_supervisor_dispatch_flush(void)
{
    /* Called at the same vCPU safe point as translation cache recycling. */
    memset(latx_supervisor_dispatch, 0, sizeof(latx_supervisor_dispatch));
}

static void latx_supervisor_dispatch_insert(TranslationBlock *tb, vaddr pc,
                                            uint32_t flags, uint32_t cflags)
{
    if ((flags & (HF_CPL_MASK | HF_CS64_MASK | HF_FORCE_TB_MODE)) != HF_CS64_MASK ||
        (cflags & ~(CF_PARALLEL | CF_CLUSTER_MASK)) ||
        !latx_supervisor_dispatch_enabled() ||
        tb_page_addr1(tb) != (tb_page_addr_t)-1 ||
        (tb->bool_flags & (IS_AOT_TB | IS_TU_TB)) ||
        (tb_cflags(tb) != cflags) ||
        (tb_page_addr0(tb) & HACK_RAMBASE_MASK)) {
        return;
    }
    unsigned index = (pc ^ (pc >> 12)) & (ARRAY_SIZE(latx_supervisor_dispatch) - 1);

    if (qatomic_read(&latx_supervisor_dispatch[index]) != tb) {
        qatomic_store_release(&latx_supervisor_dispatch[index], tb);
    }
}
#endif

static const void *latx_helper_lookup_tb_hashtable_impl(void *_env,
                                                        bool call_target,
                                                        TranslationBlock **resolved)
{
    CPUArchState *env = _env;
    CPUState *cpu = env_cpu(env);
    TranslationBlock *tb;
    vaddr pc;
    uint64_t cs_base;
    uint32_t flags, cflags;
    int mtcc_id = curr_mtcc(env);
    const void *res = (void *)GET_SC_TABLE(mtcc_id, cs_native_to_bt_ret_0);
#ifdef CONFIG_LATX_TU
    bool pages_valid = false;
#else
    (void)call_target;
#endif
#if defined(CONFIG_LATX_TU) && defined(CONFIG_SOFTMMU)
    uint32_t full = qatomic_read(&tb_ctx.tb_flush_count);
    uint32_t partial = qatomic_read(&tb_ctx.tb_flush_count_mtcc[mtcc_id]);
    int ras_top = env->ras_tp.top;
#endif

    cpu_get_tb_cpu_state(env, &pc, &cs_base, &flags);
    cflags = curr_cflags(cpu);

#if defined(CONFIG_LATX_TU) && defined(CONFIG_SOFTMMU)
    /*
     * A cached ordinary target may already contain linked LLVM calls.
     * Suspended contexts must enter through the dispatcher's single TBs.
     */
    if (env->aot_native_fault_active &&
        (flags & HF_CPL_MASK) == HF_CPL_MASK) {
        if (resolved) {
            *resolved = NULL;
        }
        return res;
    }
#endif
    TranslationBlock *hint = NULL;
#if defined(CONFIG_LATX_TU) && defined(CONFIG_SOFTMMU)
    if (QTAILQ_EMPTY(&cpu->breakpoints) &&
        !(env->hflags & HF_FORCE_TB_MODE) &&
        !(cflags & (CF_INVALID | CF_NO_ACTIVE | CF_SINGLE_STEP | CF_USE_ICOUNT))) {
        CPUJumpCacheEntry *entry = tb_jmp_cache_entry(cpu, pc);
        TranslationBlock *cached = tb_jmp_cache_load(entry, cflags & CF_PCREL);
#ifdef LATX_PCREL
        bool pc_match = cached && ((cflags & CF_PCREL) ? entry->pc == pc : cached->pc == pc);
#else
        bool pc_match = cached && cached->pc == pc;
#endif
        if (pc_match &&
            cached->cs_base == cs_base && cached->flags == flags &&
            tb_cflags(cached) == cflags && !tb_is_hras_private(cached) &&
            (!aot_tu_enabled() || (flags & HF_CPL_MASK) != HF_CPL_MASK ||
             (cached->bool_flags & IS_AOT_TB))) {
            hint = cached;
        }
    }
#endif
    /*tb = tb_lookup(cpu, pc, cs_base, flags, cflags);*/
    tb = tb_htable_lookup_with_hint(cpu, pc, cs_base, flags, cflags, hint);
    if (tucp_fast_lookup_check() && tb && TB_TU_PAGES(tb) &&
        !cpu_tupage_check(cpu, tb, flags)) {
        tb = NULL;
    }
#if defined(CONFIG_LATX_TU) && defined(CONFIG_SOFTMMU)
    /* Both CALL and ordinary JMP misses can load an existing AOT target. */
    if ((!tb || ((flags & HF_CPL_MASK) == HF_CPL_MASK &&
                  aot_tu_enabled() && !(tb->bool_flags & IS_AOT_TB))) &&
        QTAILQ_EMPTY(&cpu->breakpoints) &&
        !(env->hflags & HF_FORCE_TB_MODE) &&
        !(cflags & (CF_INVALID | CF_NO_ACTIVE | CF_SINGLE_STEP | CF_USE_ICOUNT))) {
        TranslationBlock *loaded = load_aot(cpu, pc, cs_base, flags,
                                            cflags, lsenv);
        latx_aot_check_flush(cpu, mtcc_id, full, partial);
        if (loaded && loaded->pc == pc && loaded->cs_base == cs_base &&
            loaded->flags == flags && tb_cflags(loaded) == cflags &&
            (!TB_TU_PAGES(loaded) ||
             cpu_tupage_check(cpu, loaded, flags))) {
            tb = loaded;
        }
    }
    latx_aot_check_flush(cpu, mtcc_id, full, partial);
#endif
    /* EFLAGS can forbid fast-cache admission without invalidating a TU. */
    bool target_pages_valid = tb && tu_jmp_cache_can_insert(cpu, tb);
#if defined(CONFIG_TB_FLAGS_NO_EFLAGS)
    if (target_pages_valid && check_misc_flags(tb, MF_FLAGS_NOEFLAGS)) {
#else
    if (target_pages_valid) {
#endif
        tb_jmp_cache_insert(cpu, pc, cflags & CF_PCREL, tb);
#ifdef CONFIG_LATX_TU
        pages_valid = true;
#endif
    }
#ifdef CONFIG_LATX_TU
    /* Only expose a target after the complete TU page check used by the
     * normal CALL entry selection. A failed cache admission can indicate
     * that a non-entry dependency page no longer matches. */
    if (resolved) {
        *resolved = pages_valid ? tb : NULL;
    }
#else
    if (resolved) {
        *resolved = NULL;
    }
#endif
#if defined(CONFIG_LATX_TU) && defined(CONFIG_SOFTMMU)
    latx_aot_check_flush(cpu, mtcc_id, full, partial);
    if (!call_target && (!target_pages_valid ||
        (option_rastoptp && (env->ras_tp.top != ras_top ||
                             qatomic_read(&env->ras_tp.reset_pending))))) {
        return res;
    }
    if (!call_target && latx_aot_native_jump_target(tb)) {
        /* The guest jump is complete. Unwind any older LLVM frame through
         * the existing C exit before selecting the native continuation. */
        return res;
    }
#endif
#ifdef CONFIG_LATX_TU
    if (call_target && option_int_opt == IRQ_CHECK_ASYNC_NOTIFY &&
        !env->aot_native_fault_active && pages_valid &&
        (tb->bool_flags & (IS_AOT_TB | IS_TU_TB)) ==
                                 (IS_AOT_TB | IS_TU_TB) &&
        !(tb_cflags(tb) & CF_INVALID)) {
        const void *entry = qatomic_load_acquire(&tb->s_data->aot_native_call_entry);
        if (entry) {
            return entry;
        }
    }
#endif
#if defined(CONFIG_SOFTMMU) && defined(CONFIG_LATX_HACK_MEM) && defined(NDEBUG)
    if (!call_target && tb) {
        latx_supervisor_dispatch_insert(tb, pc, flags, cflags);
    }
#endif
    return tb ? tb->tc.ptr : res;
}

const void *latx_helper_lookup_tb_hashtable(void *env)
{
    return latx_helper_lookup_tb_hashtable_impl(env, false, NULL);
}

const void *latx_helper_lookup_tb_hashtable_call(void *env)
{
    return latx_helper_lookup_tb_hashtable_impl(env, true, NULL);
}

#if defined(CONFIG_LATX_TU) && defined(CONFIG_SOFTMMU)
static void tb_add_jump(TranslationBlock *tb, int n, TranslationBlock *tb_next);

static bool latx_aot_frame_pending(CPUArchState *env, int top)
{
    CPUState *cpu = env_cpu(env);
    return env->aot_native_fault_active ||
        top < 0 || top > LATX_RAS_TP_SIZE || env->ras_tp.top != top ||
        qatomic_read(&env->ras_tp.reset_pending) ||
        (int32_t)qatomic_read(&env_neg(env)->icount_decr.u32) < 0 ||
        qatomic_read(&cpu->exit_request) ||
        qatomic_read(&cpu->interrupt_request) ||
        (env->hflags & HF_FORCE_TB_MODE) || !QTAILQ_EMPTY(&cpu->breakpoints);
}

/* Called only after the native CALL resolver has validated its source.
 * The static helper prologue has already synchronized the guest state. */
static TranslationBlock *latx_aot_translate_call_target(CPUState *cpu, vaddr pc,
                                                       uint64_t cs_base,
                                                       uint32_t flags,
                                                       uint32_t cflags,
                                                       unsigned mtcc,
                                                       uint32_t full,
                                                       uint32_t partial,
                                                       int top)
{
    CPUArchState *env = cpu->env_ptr;
    uint32_t lookup_flags = flags;
    if (cpu->cflags_next_tb != -1) {
        return NULL;
    }
    TranslationBlock *tb = cpu_tb_lookup(cpu, pc, cs_base, &lookup_flags, cflags);
    latx_aot_check_flush(cpu, mtcc, full, partial);
    if (lookup_flags != flags || latx_aot_frame_pending(env, top)) {
        return NULL;
    }
    if (!tb) {
        int do_tu = jit_tu_enabled() && (flags & HF_CPL_MASK) &&
                    !(flags & HF_FORCE_TB_MODE);
        if (do_tu && (tlb_rw_check(env, 1, pc + cs_base) || cpu_is_inhibit_irq(cpu))) {
            do_tu = 0;
        }
        mmap_lock();
        tb = do_tu ? tu_gen_code(cpu, pc, cs_base, flags, cflags) :
                     tb_gen_code(cpu, pc, cs_base, flags, cflags);
        mmap_unlock();
        latx_aot_check_flush(cpu, mtcc, full, partial);
    }
    if (!tb || tb->pc != pc || tb->cs_base != cs_base || tb->flags != flags ||
        tb_cflags(tb) != cflags || latx_aot_frame_pending(env, top)) {
        return NULL;
    }
    bool pages_valid = tu_jmp_cache_can_insert(cpu, tb);
    latx_aot_check_flush(cpu, mtcc, full, partial);
    if (!pages_valid || cpu->cflags_next_tb != -1 || latx_aot_frame_pending(env, top)) {
        return NULL;
    }
    tb_jmp_cache_insert(cpu, pc, cflags & CF_PCREL, tb);
    return tb;
}

/* The guest CALL is complete. A guard miss precedes the softRAS push;
 * other resolver entries arrive after that push and the native BL. */
static const void *latx_helper_aot_direct_call_impl(void *_env, void *_source,
                                                   uint64_t pc, bool ordinary,
                                                   uint32_t *guard_slot,
                                                   uint32_t *generation_out)
{
    CPUArchState *env = _env;
    TranslationBlock *source = _source, *target = NULL;
    unsigned mtcc = curr_mtcc(env);
    uint32_t full = qatomic_read(&tb_ctx.tb_flush_count);
    uint32_t partial = qatomic_read(&tb_ctx.tb_flush_count_mtcc[mtcc]);
    int ras_top = env->ras_tp.top;
    env->eip = pc;
    if (guard_slot) {
        uint64_t cs_base;
        uint32_t flags, cflags = curr_cflags(env_cpu(env));
        vaddr current_pc;
        cpu_get_tb_cpu_state(env, &current_pc, &cs_base, &flags);
        if ((tb_cflags(source) & CF_INVALID) ||
            latx_aot_frame_pending(env, ras_top) ||
            source->flags != flags || source->cs_base != cs_base ||
            tb_cflags(source) != cflags ||
            (cflags & (CF_INVALID | CF_NO_ACTIVE | CF_NO_GOTO_TB |
                        CF_NO_GOTO_PTR | CF_SINGLE_STEP | CF_USE_ICOUNT))) {
            return NULL;
        }
        bool source_pages_valid = tu_jmp_cache_can_insert(env_cpu(env), source);
        latx_aot_check_flush(env_cpu(env), mtcc, full, partial);
        if (!source_pages_valid || latx_aot_frame_pending(env, ras_top) ||
            (tb_cflags(source) & CF_INVALID)) {
            return NULL;
        }
    }
    if (ordinary && ((int32_t)qatomic_read(&env_neg(env)->icount_decr.u32) < 0 ||
                     ras_top <= 0 || qatomic_read(&env->ras_tp.reset_pending))) {
        return NULL;
    }
    latx_helper_lookup_tb_hashtable_impl(env, true, &target);
    latx_aot_check_flush(env_cpu(env), mtcc, full, partial);
    if (!target && !ordinary) {
        CPUState *cpu = env_cpu(env);
        uint64_t cs_base;
        uint32_t flags, cflags = curr_cflags(cpu);
        vaddr current_pc;
        cpu_get_tb_cpu_state(env, &current_pc, &cs_base, &flags);
        bool source_valid = tu_jmp_cache_can_insert(cpu, source);
        latx_aot_check_flush(cpu, mtcc, full, partial);
        if (current_pc == pc && source_valid && !(tb_cflags(source) & CF_INVALID) &&
            source->flags == flags && source->cs_base == cs_base &&
            tb_cflags(source) == cflags &&
            !(cflags & (CF_INVALID | CF_NO_ACTIVE | CF_NO_GOTO_TB |
                        CF_NO_GOTO_PTR | CF_SINGLE_STEP | CF_USE_ICOUNT)) &&
            !latx_aot_frame_pending(env, ras_top)) {
            target = latx_aot_translate_call_target(cpu, pc, cs_base, flags, cflags,
                                                    mtcc, full, partial, ras_top);
            latx_aot_check_flush(cpu, mtcc, full, partial);
            if (target) {
                latx_helper_lookup_tb_hashtable_impl(env, true, &target);
                latx_aot_check_flush(cpu, mtcc, full, partial);
            }
            source_valid = tu_jmp_cache_can_insert(cpu, source);
            latx_aot_check_flush(cpu, mtcc, full, partial);
            if (!source_valid || latx_aot_frame_pending(env, ras_top)) {
                return NULL;
            }
        }
    }
    if (guard_slot) {
        bool source_pages_valid = tu_jmp_cache_can_insert(env_cpu(env), source);
        latx_aot_check_flush(env_cpu(env), mtcc, full, partial);
        if (!source_pages_valid || latx_aot_frame_pending(env, ras_top)) {
            return NULL;
        }
    }
    if ((tb_cflags(source) & CF_INVALID) || !target ||
        (ordinary && (!tu_jmp_cache_can_insert(env_cpu(env), source) ||
                      env->ras_tp.top != ras_top ||
                      qatomic_read(&env->ras_tp.reset_pending)))) {
        return NULL;
    }
    tb_add_jump(source, 0, target);
    latx_aot_check_flush(env_cpu(env), mtcc, full, partial);
    if (guard_slot && latx_aot_frame_pending(env, ras_top)) return NULL;
    return latxs_aot_direct_call_replay(source, target, ordinary, guard_slot,
                                        generation_out);
}

const void *latx_helper_aot_direct_call(void *env, void *source, uint64_t pc)
{
    return latx_helper_aot_direct_call_impl(env, source, pc, false, NULL, NULL);
}

const void *latx_helper_aot_ordinary_call(void *env, void *source, uint64_t pc)
{
    return latx_helper_aot_direct_call_impl(env, source, pc, true, NULL, NULL);
}

uint32_t latx_helper_aot_direct_guard(void *env, void *source, uint64_t pc,
                                     uint32_t *slot)
{
    uint32_t generation = 0;
    return latx_helper_aot_direct_call_impl(env, source, pc, false, slot,
                                            &generation) ? generation : 0;
}

const void *latx_helper_aot_linked_call(void *_env, void *_source, uint64_t pc)
{
    CPUArchState *env = _env;
    CPUState *cpu = env_cpu(env);
    TranslationBlock *source = _source, *target = NULL;
    unsigned mtcc = curr_mtcc(env);
    uint32_t full = qatomic_read(&tb_ctx.tb_flush_count);
    uint32_t partial = qatomic_read(&tb_ctx.tb_flush_count_mtcc[mtcc]);
    uint32_t flags, cflags = curr_cflags(cpu);
    uint64_t cs_base;
    vaddr current_pc;
    int ras_top = env->ras_tp.top;

    env->eip = pc;
    cpu_get_tb_cpu_state(env, &current_pc, &cs_base, &flags);
    if (current_pc != pc || (tb_cflags(source) & CF_INVALID) ||
        latx_aot_frame_pending(env, ras_top) ||
        source->flags != flags || source->cs_base != cs_base ||
        tb_cflags(source) != cflags ||
        (cflags & (CF_INVALID | CF_NO_ACTIVE | CF_NO_GOTO_TB |
                    CF_NO_GOTO_PTR | CF_SINGLE_STEP | CF_USE_ICOUNT))) {
        return NULL;
    }
    bool source_valid = tu_jmp_cache_can_insert(cpu, source);
    latx_aot_check_flush(cpu, mtcc, full, partial);
    if (!source_valid || latx_aot_frame_pending(env, ras_top) ||
        (tb_cflags(source) & CF_INVALID)) {
        return NULL;
    }
    latx_helper_lookup_tb_hashtable_impl(env, true, &target);
    latx_aot_check_flush(cpu, mtcc, full, partial);
    if (!target && !latx_aot_frame_pending(env, ras_top)) {
        target = latx_aot_translate_call_target(cpu, pc, cs_base, flags, cflags,
                                                mtcc, full, partial, ras_top);
        latx_aot_check_flush(cpu, mtcc, full, partial);
        if (target) {
            latx_helper_lookup_tb_hashtable_impl(env, true, &target);
            latx_aot_check_flush(cpu, mtcc, full, partial);
        }
    }
    source_valid = tu_jmp_cache_can_insert(cpu, source);
    bool target_valid = target && tu_jmp_cache_can_insert(cpu, target);
    latx_aot_check_flush(cpu, mtcc, full, partial);
    if (!source_valid || !target_valid || !target ||
        (tb_cflags(source) & CF_INVALID) || (tb_cflags(target) & CF_INVALID) ||
        latx_aot_frame_pending(env, ras_top)) {
        return NULL;
    }
    tb_add_jump(source, 0, target);
    latx_aot_check_flush(cpu, mtcc, full, partial);
    if (latx_aot_frame_pending(env, ras_top)) {
        return NULL;
    }
    return latxs_aot_linked_call_replay(source, target);
}

/* An NJC miss precedes the LLVM CALL's softRAS push and native JIRL.
 * Resolve this target without turning the indirect edge into a direct link. */
const void *latx_helper_aot_indirect_call(void *_env, void *_source, uint64_t pc,
                                        uint64_t *target_info)
{
    CPUArchState *env = _env;
    CPUState *cpu = env_cpu(env);
    TranslationBlock *source = _source, *target = NULL;
    unsigned mtcc = curr_mtcc(env);
    uint32_t full = qatomic_read(&tb_ctx.tb_flush_count);
    uint32_t partial = qatomic_read(&tb_ctx.tb_flush_count_mtcc[mtcc]);
    uint32_t flags, cflags = curr_cflags(cpu);
    uint64_t cs_base;
    vaddr current_pc;
    int top = env->ras_tp.top;
    env->eip = pc;
    target_info[0] = target_info[1] = 0;
    cpu_get_tb_cpu_state(env, &current_pc, &cs_base, &flags);
    if ((tb_cflags(source) & CF_INVALID) || latx_aot_frame_pending(env, top) ||
        source->flags != flags || source->cs_base != cs_base ||
        tb_cflags(source) != cflags ||
        (cflags & (CF_INVALID | CF_NO_ACTIVE | CF_NO_GOTO_TB |
                    CF_NO_GOTO_PTR | CF_SINGLE_STEP | CF_USE_ICOUNT))) {
        return NULL;
    }
    bool source_valid = tu_jmp_cache_can_insert(cpu, source);
    latx_aot_check_flush(cpu, mtcc, full, partial);
    if (!source_valid || latx_aot_frame_pending(env, top)) return NULL;

    const void *entry = latx_helper_lookup_tb_hashtable_impl(env, true, &target);
    latx_aot_check_flush(cpu, mtcc, full, partial);
    if (!target && !latx_aot_frame_pending(env, top)) {
        target = latx_aot_translate_call_target(cpu, current_pc, cs_base, flags, cflags,
                                                mtcc, full, partial, top);
        latx_aot_check_flush(cpu, mtcc, full, partial);
        if (target) {
            entry = latx_helper_lookup_tb_hashtable_impl(env, true, &target);
            latx_aot_check_flush(cpu, mtcc, full, partial);
        }
    }
    source_valid = tu_jmp_cache_can_insert(cpu, source);
    latx_aot_check_flush(cpu, mtcc, full, partial);
    if (!entry || !target || !source_valid ||
        (tb_cflags(source) & CF_INVALID) || (tb_cflags(target) & CF_INVALID) ||
        latx_aot_frame_pending(env, top) || target->pc != current_pc ||
        target->flags != flags || target->cs_base != cs_base ||
        tb_cflags(target) != cflags) {
        return NULL;
    }
    if (TB_TU_PAGES(target)) {
        uint64_t id = TB_TU_PAGES(target)->id;
        if (!env->tu_generation_array || !id ||
            id >= option_tucp_generation_array_size) return NULL;
        target_info[0] = id;
    }
    target_info[1] = DMW_IN(target);
    return (const void *)(uintptr_t)DMW_IN(entry);
}

/* Diagnostics stay in the existing C cold path. The caller supplies source
 * only after the original exit decoding and cache-flush checks permit it. */


/* A normal n=1 edge has already executed its guest branch. Resolve ordinary
 * destinations without unwinding an enclosing LLVM frame; native targets
 * use the full C exit instead. */
const void *latx_helper_aot_ordinary_jump(void *_env, uintptr_t exit)
{
    CPUArchState *env = _env;
    CPUState *cpu = env_cpu(env);
    TranslationBlock *source, *target = NULL;
    unsigned mtcc = curr_mtcc(env);
    uint32_t full = qatomic_read(&tb_ctx.tb_flush_count);
    uint32_t partial = qatomic_read(&tb_ctx.tb_flush_count_mtcc[mtcc]);
    uint32_t flags, cflags = curr_cflags(cpu);
    uint64_t cs_base;
    vaddr pc;
    int top = env->ras_tp.top;

    if ((exit & TB_EXIT_MASK) != TB_EXIT_IDX1 ||
        !aot_tu_enabled() || !hamt_enabled() || !hamt_mode_dmw() ||
        !option_soft_ras || !option_rastoptp ||
        option_int_opt != IRQ_CHECK_ASYNC_NOTIFY ||
        top <= 0 || latx_aot_frame_pending(env, top) ||
        (cflags & (CF_INVALID | CF_NO_ACTIVE | CF_NO_GOTO_TB |
                    CF_NO_GOTO_PTR | CF_SINGLE_STEP | CF_USE_ICOUNT))) {
            return NULL;
    }
    exit = DMW_OUT(exit & ~TB_EXIT_MASK);
    if (option_split_tb) {
        source = (void *)(exit && split_tb_base_ok() ? exit | SPLIT_TB_BASE : exit);
    } else {
        source = tcg_splitwx_to_rw((void *)exit);
    }
    cpu_get_tb_cpu_state(env, &pc, &cs_base, &flags);
    if (!source || source->flags != flags || source->cs_base != cs_base ||
        !(flags & HF_CS64_MASK) || cs_base ||
        (flags & HF_CPL_MASK) != HF_CPL_MASK ||
        tb_cflags(source) != cflags ||
        source->jmp_insn_offset[1] == TB_JMP_OFFSET_INVALID ||
        source->signal_unlink[1] || check_misc_flags(source, MF_END_SYSCALL) ||
        get_mask_from_miscflags(source, MF_MTCC_ID_MASK, MF_MTCC_ID_SHIFT) != mtcc ||
        qatomic_read(&source->jmp_dest[1])) {
            return NULL;
    }
    if (option_lazypc) {
        env->eip = source->next_pc[1];
    }
    bool source_pages_valid = tu_jmp_cache_can_insert(cpu, source);
    latx_aot_check_flush(cpu, mtcc, full, partial);
    if (!source_pages_valid || latx_aot_frame_pending(env, top) ||
        (tb_cflags(source) & CF_INVALID)) {
            return NULL;
    }
    latx_helper_lookup_tb_hashtable_impl(env, false, &target);
    latx_aot_check_flush(cpu, mtcc, full, partial);
    if (!target ||
        (latx_aot_native_jump_target(target) &&
         !latxs_aot_direct_call_entry(source, 1, target)) ||
        (tb_cflags(source) & CF_INVALID) ||
        latx_aot_frame_pending(env, top) ||
        target->flags != flags || target->cs_base != cs_base ||
        tb_cflags(target) != cflags ||
        (tb_page_addr1(target) != -1 && !cp_tb_enabled())) {
            return NULL;
    }
    source_pages_valid = tu_jmp_cache_can_insert(cpu, source);
    latx_aot_check_flush(cpu, mtcc, full, partial);
    if (!source_pages_valid || (tb_cflags(source) & CF_INVALID) ||
        source->signal_unlink[1] || latx_aot_frame_pending(env, top)) {
            return NULL;
    }
    tb_add_jump(source, 1, target);
    latx_aot_check_flush(cpu, mtcc, full, partial);
    if (latx_aot_frame_pending(env, top)) {
            return NULL;
    }
    const void *replay = latxs_aot_ordinary_jump_replay(source, target);
    return replay;
}
#endif

/* The shared native fallback calls this helper after clearing the RAS
 * and restoring the translator's outer SP. Never select a fresh frame
 * from an ordinary JMP while an older LLVM frame may still be live. */
const void *latx_helper_aot_resume(void *_env)
{
    CPUArchState *env = _env;
    const void *fallback = (void *)GET_SC_TABLE(curr_mtcc(env), cs_native_to_bt_ret_0);
#if defined(CONFIG_LATX_TU) && defined(CONFIG_SOFTMMU)
    CPUState *cpu = env_cpu(env);
    TranslationBlock *target = NULL;
    uint64_t cs_base;
    uint32_t flags, cflags = curr_cflags(cpu);
    int top = env->ras_tp.top;
    vaddr pc;
    cpu_get_tb_cpu_state(env, &pc, &cs_base, &flags);
    if (!aot_tu_enabled() || !hamt_enabled() || !hamt_mode_dmw() ||
        !option_soft_ras || !option_rastoptp ||
        option_int_opt != IRQ_CHECK_ASYNC_NOTIFY ||
        !(flags & HF_CS64_MASK) || cs_base ||
        (flags & HF_CPL_MASK) != HF_CPL_MASK ||
        latx_aot_frame_pending(env, top) ||
        (cflags & (CF_INVALID | CF_NO_ACTIVE | CF_NO_GOTO_TB |
                    CF_NO_GOTO_PTR | CF_SINGLE_STEP | CF_USE_ICOUNT))) {
        return fallback;
    }
    /* call_target selects the ABI entry for the current guest state only.
     * Do not execute another guest CALL/RET or adjust the guest RSP. */
    const void *entry = latx_helper_lookup_tb_hashtable_impl(env, true, &target);
    if (!target || latx_aot_frame_pending(env, top) ||
        target->pc != pc || target->cs_base != cs_base ||
        target->flags != flags || tb_cflags(target) != cflags) {
        return fallback;
    }
    if (!top && entry == target->tc.ptr &&
        (target->bool_flags & (IS_AOT_TB | IS_TU_TB)) == (IS_AOT_TB | IS_TU_TB) &&
        !(tb_cflags(target) & CF_INVALID)) {
        const void *osr = qatomic_load_acquire(&target->s_data->aot_direct_native_entry);
        if (osr) {
            return osr;
        }
    }
    return entry;
#else
    return fallback;
#endif
}

#if defined(CONFIG_LATX_TU) && defined(CONFIG_SOFTMMU)
static void latx_record_call_entry(CPUState *cpu, TranslationBlock *tb,
                                   const LatxAOTCallObservation *call)
{
    if (!call->valid || !tb || tb->pc != call->pc ||
        tb->cs_base != call->cs_base || tb->flags != call->flags ||
        tb_cflags(tb) != call->cflags || (tb_cflags(tb) & CF_INVALID)) {
        return;
    }
    qatomic_or(&tb->misc_flags, MF_OBSERVED_CALL_ENTRY);
    tb_jmp_cache_insert(cpu, tb->pc, tb_cflags(tb) & CF_PCREL, tb);
}
#endif

/* JIT-only CALL miss: the source must still exist before re-entering its
 * final JIRL. The normal lookup can invalidate it while checking TU pages. */
const void *latx_helper_lookup_tb_call(void *_env, void *_source)
{
    CPUArchState *env = _env;
    TranslationBlock *source = _source;
    unsigned mtcc = curr_mtcc(env);
    uint32_t full_count = qatomic_read(&tb_ctx.tb_flush_count);
    uint32_t mtcc_count = qatomic_read(&tb_ctx.tb_flush_count_mtcc[mtcc]);
    TranslationBlock *resolved = NULL;
#if defined(CONFIG_LATX_TU) && defined(CONFIG_SOFTMMU)
    CPUState *cpu = env_cpu(env);
    LatxAOTCallObservation call = { 0 };
    env->aot_observed_call_pending.valid = false;
    if (option_aot_save && !(source->bool_flags & IS_AOT_TB)) {
        cpu_get_tb_cpu_state(env, &call.pc, &call.cs_base, &call.flags);
        call.cflags = curr_cflags(cpu);
        call.valid = true;
    }
#endif
    const void *target = latx_helper_lookup_tb_hashtable_impl(env, true, &resolved);

    /* Do not dereference a recycled TB slot after a code-cache flush. */
    if (full_count != qatomic_read(&tb_ctx.tb_flush_count) ||
        mtcc_count != qatomic_read(&tb_ctx.tb_flush_count_mtcc[mtcc]) ||
        (tb_cflags(source) & CF_INVALID)) {
        return (void *)GET_SC_TABLE(curr_mtcc(env), cs_native_to_bt_ret_0);
    }
#if defined(CONFIG_LATX_TU) && defined(CONFIG_SOFTMMU)
    if (resolved) {
        latx_record_call_entry(cpu, resolved, &call);
    } else if (call.valid && target ==
               (void *)GET_SC_TABLE(curr_mtcc(env), cs_native_to_bt_ret_0)) {
        /* The CALL has already changed guest RSP and eip. Carry only its
         * target identity across this C exit; never retain the source TB. */
        env->aot_observed_call_pending = call;
    }
#endif
    return target;
}

#include "tcg/tcg-rrtrace.h"

#if !defined(CONFIG_USER_ONLY)
#include "target/i386/latx/include/latx-tb-trace-sys.h"
static void rrtrace_tb_after_exec(CPUState *cpu, TranslationBlock *tb)
{
    latxs_tb_trace_rr(cpu->env_ptr, tb, 0);
}
void rrtrace_tb_after_excep(CPUState *cpu)
{
    if (cpu->curr_exec_tb) {
        latxs_tb_trace_rr(cpu->env_ptr, cpu->curr_exec_tb, 1);
    }
}
#else
void rrtrace_tb_after_excep(CPUState *cpu) {}
#endif

static void signal_relink_jmp_target(TranslationBlock *tb, int n)
{
    void *jmp_inst_ptr;
    uint64_t inst = tb->signal_unlink[n];
    tb->signal_unlink[n] = 0;

    if (tb->jmp_cp_data[n] & TB_JMP_CP_DATA_LINKED) {
        int jmp_cp_off = tb->jmp_cp_data[n] & TB_JMP_CP_DATA_OFF;
        jmp_inst_ptr = (void *)((uint64_t)tb->tc.ptr +
                (tb->jmp_insn_offset[n] - (jmp_cp_off << 2)));
    } else {
        jmp_inst_ptr = (void *)((uint64_t)tb->tc.ptr +
            tb->jmp_insn_offset[n]);
    }

    if (n == 0 && (tb->bool_flags & IS_HRAS_CALL)) {
        unsigned bytes = tb->jmp_reset_offset[n] - tb->jmp_insn_offset[n];
        if (bytes == 4) {
            qatomic_set((uint32_t *)jmp_inst_ptr, (uint32_t)inst);
        } else {
            g_assert(bytes == 8);
            qatomic_set((uint64_t *)jmp_inst_ptr, inst);
        }
        flush_idcache_range((uintptr_t)jmp_inst_ptr, (uintptr_t)jmp_inst_ptr, bytes);
    } else {
        qatomic_set((uint64_t*)jmp_inst_ptr, inst);
        flush_idcache_range((uintptr_t)jmp_inst_ptr, (uintptr_t)jmp_inst_ptr, 8);
    }
}

/* Execute a TB, and fix up the CPU state afterwards if necessary */
/*
 * Disable CFI checks.
 * TCG creates binary blobs at runtime, with the transformed code.
 * A TB is a blob of binary code, created at runtime and called with an
 * indirect function call. Since such function did not exist at compile time,
 * the CFI runtime has no way to verify its signature and would fail.
 * TCG is not considered a security-sensitive part of QEMU so this does not
 * affect the impact of CFI in environment with high security requirements
 */
static TranslationBlock * QEMU_DISABLE_CFI
cpu_tb_exec(CPUState *cpu, TranslationBlock *itb, int *tb_exit,
            const void *entry)
{
    CPUArchState *env = cpu->env_ptr;
    uintptr_t ret = 0;
    TranslationBlock *last_tb;
    const void *tb_ptr = entry ? entry : itb->tc.ptr;

    if (qemu_loglevel_mask(CPU_LOG_TB_CPU | CPU_LOG_EXEC)) {
        log_cpu_exec(log_pc(cpu, itb), cpu, itb);
    }

    cpu->curr_exec_tb = itb;

    uint32_t mtcc_id = curr_mtcc(env);
    latxs_ras_tp_prepare(cpu, mtcc_id);

#if defined(CONFIG_LATX_TU) && defined(CONFIG_SOFTMMU)
    /* The C dispatcher has abandoned any earlier LLVM frames. A published
     * continuation receives the current guest state without another guest
     * CALL, RET or stack adjustment. Jumps to ordinary destinations may
     * still retain a live LLVM caller until the normal full exit. */
    if (!entry && aot_tu_enabled() && hamt_enabled() && hamt_mode_dmw() &&
        option_soft_ras && option_rastoptp &&
        option_int_opt == IRQ_CHECK_ASYNC_NOTIFY &&
        (itb->bool_flags & IS_AOT_TB) && tb_is_tu(itb) &&
        TB_TU_PAGES(itb) && TB_TU_PAGES(itb)->native &&
        (itb->flags & HF_CS64_MASK) && !itb->cs_base &&
        (itb->flags & HF_CPL_MASK) == HF_CPL_MASK &&
        !(env->hflags & HF_FORCE_TB_MODE) &&
        !(tb_cflags(itb) & (CF_INVALID | CF_NO_ACTIVE | CF_NO_GOTO_TB |
                           CF_NO_GOTO_PTR | CF_SINGLE_STEP | CF_USE_ICOUNT))) {
        uint32_t full = qatomic_read(&tb_ctx.tb_flush_count);
        uint32_t partial = qatomic_read(&tb_ctx.tb_flush_count_mtcc[mtcc_id]);
        bool valid = tu_jmp_cache_can_insert(cpu, itb);
        latx_aot_check_flush(cpu, mtcc_id, full, partial);
        if (valid && !(tb_cflags(itb) & CF_INVALID)) {
            const void *native =
                qatomic_load_acquire(&itb->s_data->aot_native_call_entry);
            const void *osr = (!native && !env->ras_tp.top) ?
                qatomic_load_acquire(&itb->s_data->aot_direct_native_entry) : NULL;
            const void *selected = native ? native : osr;
            bool pending = latx_aot_frame_pending(env, env->ras_tp.top);
            if (selected && pending) {
                int requests = qatomic_read(&cpu->interrupt_request);
                if (qatomic_read(&cpu->exit_request) ||
                    (requests & (CPU_INTERRUPT_DEBUG | CPU_INTERRUPT_HALT |
                                 CPU_INTERRUPT_INIT | CPU_INTERRUPT_EXITTB)) ||
                    x86_cpu_pending_interrupt(cpu, requests)) {
                    /* Loading a native owner can race with a new request.
                     * Let the dispatcher handle it before guest execution.
                     * Masked interrupts still allow ordinary code to advance. */
                    lats_request_tb_exit(cpu);
                    cpu->curr_exec_tb = NULL;
                    env->eip = itb->pc - itb->cs_base;
                    *tb_exit = TB_EXIT_REQUESTED;
                    return itb;
                }
            } else if (selected) {
                tb_ptr = selected;
            }
        }
    }
#endif

    qemu_thread_jit_execute();
    if (option_soft_ras && !option_rastoptp) {
        uint64_t top_addr;
        Soft_RAS *ras;

        ras = (Soft_RAS *)env->s_ras_ptr;
        top_addr = (uint64_t)&ras->stack[0];
#ifndef CONFIG_RAS_SLOW
        int top = ras->top;
        uint64_t new_addr = top_addr + (top << 4);
        __asm__ __volatile__ (
                MOVGR_2_SCR0(%0)
                ::"r"(new_addr)
                :);
#else
        __asm__ __volatile__ (
                MOVGR_2_SCR0(%0)
                ::"r"(top_addr)
                :);
#endif
    }
    if (qemu_tcg_mode_1_tcg()) {
        ret = tcg_qemu_tb_exec[mtcc_id](env, tb_ptr);
    } else if (qemu_tcg_mode_2_lat()) {
        latxs_before_exec_tb(cpu, itb);
        ret = tcg_qemu_tb_exec2[mtcc_id](tb_ptr, env);
        latxs_after_exec_tb(cpu, itb);
    } else {
        assert(0);
    }

#ifndef CONFIG_RAS_SLOW
    if (option_soft_ras && !option_rastoptp) {
        uint64_t top_addr;
        int new_top;
        Soft_RAS *ras = (Soft_RAS *)env->s_ras_ptr;

        __asm__ __volatile__ (
                "movscr2gr %0, $scr0\n"
                :"=r"(top_addr)
                ::);
        new_top = (top_addr - (uint64_t)&ras->stack[0]) >> 4;
        ras->top = new_top;
    }
#endif

    cpu->curr_exec_tb = NULL;

    cpu->can_do_io = 1;
#if !defined(CONFIG_USER_ONLY)
    if (icount_enabled() && qemu_rrtrace_enabled()) {
        if (!(ret & 0x2)) {
            /* trace only when tb_exit == 0 or 1 */
            rrtrace_tb_after_exec(cpu, itb);
        } else {
            latxs_tb_trace_rr(cpu->env_ptr, itb, 2);
        }
    }
#endif
    qemu_plugin_disable_mem_helpers(cpu);
    /*
     * TODO: Delay swapping back to the read-write region of the TB
     * until we actually need to modify the TB.  The read-only copy,
     * coming from the rx region, shares the same host TLB entry as
     * the code that executed the exit_tb opcode that arrived here.
     * If we insist on touching both the RX and the RW pages, we
     * double the host TLB pressure.
     */
    if (option_split_tb) {
        last_tb = (void *)(ret & ~TB_EXIT_MASK);
        if (last_tb && split_tb_base_ok())  {
            last_tb = (void *)((uint64_t)last_tb | SPLIT_TB_BASE);
        }
    } else {
        last_tb = tcg_splitwx_to_rw((void *)(ret & ~TB_EXIT_MASK));
    }
    *tb_exit = ret & TB_EXIT_MASK;

    if (option_lazypc && last_tb && !check_misc_flags(last_tb, MF_END_SYSCALL)) {
        int id = ret & TB_EXIT_MASK;
        if (id == 0 || id == 1) env->eip = last_tb->next_pc[id];
        if (id == 3) env->eip = last_tb->pc - last_tb->cs_base;
    }

    trace_exec_tb_exit(last_tb, *tb_exit);
    if (last_tb) {
        if (last_tb->signal_unlink[0]) {
            signal_relink_jmp_target(last_tb, 0);
        }
        if (last_tb->signal_unlink[1]) {
            signal_relink_jmp_target(last_tb, 1);
        }
    }

    if (*tb_exit > TB_EXIT_IDX1) {
#ifdef CONFIG_LATX_OPT
        if (last_tb && !(tb_cflags(last_tb) & CF_PCREL)) {
            env->eip = last_tb->pc - last_tb->cs_base;
        }
#else
        /* We didn't start executing this TB (eg because the instruction
         * counter hit zero); we must restore the guest PC to the address
         * of the start of the TB.
         */
        CPUClass *cc = CPU_GET_CLASS(cpu);

        if (cc->tcg_ops->synchronize_from_tb) {
            cc->tcg_ops->synchronize_from_tb(cpu, last_tb);
        } else {
            tcg_debug_assert(!(tb_cflags(last_tb) & CF_PCREL));
            assert(cc->set_pc);
            cc->set_pc(cpu, last_tb->pc);
        }
        if (qemu_loglevel_mask(CPU_LOG_EXEC)) {
            vaddr pc = log_pc(cpu, last_tb);
            if (qemu_log_in_addr_range(pc)) {
                qemu_log("Stopped execution of TB chain before %p [%016"
                         VADDR_PRIx "] %s\n",
                         last_tb->tc.ptr, pc, lookup_symbol(pc));
            }
        }
#endif
    }

    /*
     * If gdb single-step, and we haven't raised another exception,
     * raise a debug exception.  Single-step with another exception
     * is handled in cpu_handle_exception.
     */
    if (unlikely(cpu->singlestep_enabled) && cpu->exception_index == -1) {
        cpu->exception_index = EXCP_DEBUG;
        cpu_loop_exit(cpu);
    }

    return last_tb;
}


static inline void cpu_exec_enter(CPUState *cpu)
{
#ifdef CONFIG_LATX_OPT
    CPUArchState *env = (CPUArchState *)cpu->env_ptr;

    CC_SRC = env->eflags & (CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C);
#ifdef CONFIG_LATX_OPT_EFLAGS
    __lbt_mtflag_all(CC_SRC);
#endif
    env->df = 1 - (2 * ((env->eflags >> 10) & 1));
    CC_OP = CC_OP_EFLAGS;
    env->eflags &= ~(DF_MASK | CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C);
#else
    CPUClass *cc = CPU_GET_CLASS(cpu);

    if (cc->tcg_ops->cpu_exec_enter) {
        cc->tcg_ops->cpu_exec_enter(cpu);
    }
#endif
}

static inline void cpu_exec_exit(CPUState *cpu)
{
#ifdef CONFIG_LATX_OPT
    CPUArchState *env = (CPUArchState *)cpu->env_ptr;

    env->eflags = cpu_compute_eflags(env);
#else
    CPUClass *cc = CPU_GET_CLASS(cpu);

    if (cc->tcg_ops->cpu_exec_exit) {
        cc->tcg_ops->cpu_exec_exit(cpu);
    }
#endif
}

static void cpu_exec_longjmp_cleanup(CPUState *cpu)
{
    /* A nonlocal exit can discard an LLVM host frame while its ordinary
     * callee runs. Its cached return must not reenter the discarded frame. */
    /* Non-buggy compilers preserve this; assert the correct value. */
    g_assert(cpu == current_cpu);

#if defined(TARGET_I386) && defined(CONFIG_SOFTMMU)
    if (cpu->exception_index != EXCP_ATOMIC) {
        cpu->env_ptr->aot_native_atomic.descriptor = 0;
    }
#endif
#ifdef CONFIG_USER_ONLY
    clear_helper_retaddr();
    if (have_mmap_lock()) {
        mmap_unlock();
    }
#else
#if defined(CONFIG_LATX_TU) && defined(CONFIG_SOFTMMU)
    if (aot_tu_enabled() && hamt_enabled() && option_rastoptp) {
        latxs_flush_soft_ras(cpu);
    }
#endif
    if (option_soft_ras) {
        Soft_RAS *ras = cpu->s_ras;
        TranslationBlock *patching_tb = ras->patching_tb;
        if (patching_tb) {
#ifdef CONFIG_RAS_DEBUG
            uint64_t pretrans_pc = lsenv->tr_data->pc_next;
            fprintf(stderr, "[RAS-DBG] [cpu%d] "
                "%s %lx Failed to pre-translate %lx.\n", cpu->cpu_index,
                __func__, patching_tb->pc, pretrans_pc);
#endif
            latxs_ras_unlink(patching_tb);
            ras->patching_tb = NULL;
        }
    }
    /*
     * For softmmu, a tlb_fill fault during translation will land here,
     * and we need to release any page locks held.  In system mode we
     * have one tcg_ctx per thread, so we know it was this cpu doing
     * the translation.
     *
     * Alternative 1: Install a cleanup to be called via an exception
     * handling safe longjmp.  It seems plausible that all our hosts
     * support such a thing.  We'd have to properly register unwind info
     * for the JIT for EH, rather that just for GDB.
     *
     * Alternative 2: Set and restore cpu->jmp_env in tb_gen_code to
     * capture the cpu_loop_exit longjmp, perform the cleanup, and
     * jump again to arrive here.
     */
#if defined(CONFIG_LATX_TCG_MODE)
    if (tcg_ctx->gen_tb) {
        tb_unlock_pages(tcg_ctx->gen_tb);
        tcg_ctx->gen_tb = NULL;
    }
#else
    tb_unlock_ctx_pages(1);
#endif
#endif
    if (qemu_mutex_iothread_locked()) {
        qemu_mutex_unlock_iothread();
    }
    assert_no_pages_locked();

#ifdef CONFIG_LATX_OPT_NOFP
    if (qemu_tcg_mode_2_lat()) {
        __nofp_sync_xmm_from_env(cpu);
    }
#endif
}

void cpu_exec_step_atomic(CPUState *cpu)
{
    CPUArchState *env = cpu->env_ptr;
    TranslationBlock *tb;
    vaddr pc;
    uint64_t cs_base;
    uint32_t flags, cflags;
    int tb_exit;
    volatile bool exec_entered = false;
    volatile bool native_atomic = false;

    if (sigsetjmp(cpu->jmp_env, 0) == 0) {
        start_exclusive();
        g_assert(cpu == current_cpu);
        g_assert(!cpu->running);
        cpu->running = true;

#if defined(TARGET_I386) && defined(CONFIG_SOFTMMU)
        if (env->aot_native_atomic.descriptor) {
            native_atomic = true;
            latx_aot_atomic_complete(env);
            goto atomic_done;
        }
#endif

        cpu_get_tb_cpu_state(env, &pc, &cs_base, &flags);

        cflags = curr_cflags(cpu);
        /* Execute in a serial context. */
        cflags &= ~CF_PARALLEL;
        /* After 1 insn, return and release the exclusive lock. */
        cflags |= CF_NO_GOTO_TB | CF_NO_GOTO_PTR | 1;
        /*
         * No need to check_for_breakpoints here.
         * We only arrive in cpu_exec_step_atomic after beginning execution
         * of an insn that includes an atomic operation we can't handle.
         * Any breakpoint for this insn will have been recognized earlier.
         */

        tb = tb_lookup(cpu, pc, cs_base, flags, cflags);
        if (tb == NULL) {
            mmap_lock();
            tb = tb_gen_code(cpu, pc, cs_base, flags, cflags);
            mmap_unlock();
        }

        cpu_exec_enter(cpu);
        exec_entered = true;
        /* execute the generated code */
        trace_exec_tb(tb, pc);
        cpu_tb_exec(cpu, tb, &tb_exit, NULL);
        cpu_exec_exit(cpu);
        exec_entered = false;
    } else {
        cpu_exec_longjmp_cleanup(cpu);
        /* A guest fault during the serial retry still needs state packed. */
        if (exec_entered) {
            cpu_exec_exit(cpu);
            exec_entered = false;
        }
    }

    /*
     * As we start the exclusive region before codegen we must still
     * be in the region if we longjump out of either the codegen or
     * the execution.
     */
atomic_done:
    g_assert(cpu_in_exclusive_context(cpu));
    cpu->running = false;
    end_exclusive();
#ifdef CONFIG_LATX_OPT_NOFP
    if (native_atomic) {
        __nofp_sync_xmm_from_env(cpu);
    }
#endif
}

void tb_set_jmp_target(TranslationBlock *tb, int n, uintptr_t addr)
{
    if (qemu_tcg_mode_2_lat()) g_assert(0);
    /*
     * Get the rx view of the structure, from which we find the
     * executable code address, and tb_target_set_jmp_target can
     * produce a pc-relative displacement to jmp_target_addr[n].
     */
    uintptr_t offset = tb->jmp_insn_offset[n];
    uintptr_t jmp_rx = (uintptr_t)tb->tc.ptr + offset;
    uintptr_t jmp_rw = jmp_rx - tcg_splitwx_diff;

    tb->jmp_target_addr[n] = addr;
    tb_target_set_jmp_target(tb, n, jmp_rx, jmp_rw);
}

static void tb_add_jump(TranslationBlock *tb, int n,
                               TranslationBlock *tb_next)
{
    uintptr_t old;

    if (check_misc_flags(tb, MF_END_SYSCALL)) {
        if (option_opt_syscall == 1) {
            current_cpu->env_ptr->syscall_tb = (void *)tb_next->tc.ptr;
        } else if (option_opt_syscall == 2) {
            current_cpu->env_ptr->syscall_tb = (void *)tb_next;
        }
        return;
    }

    if (tb->flags != tb_next->flags) {
        return;
    }

    /*
     * MTCC regions have independent lifetimes: either region can be reset and
     * its TB slots immediately reused while the other remains live.  A direct
     * edge across regions would therefore outlive its destination.  Keep
     * chaining within one region so split-cache flushes need not patch code
     * that another vCPU may currently be executing.
     */
    if (qemu_tcg_mtcc_enabled() &&
        get_mask_from_miscflags(tb, MF_MTCC_ID_MASK, MF_MTCC_ID_SHIFT) !=
        get_mask_from_miscflags(tb_next, MF_MTCC_ID_MASK,
                                MF_MTCC_ID_SHIFT)) {
        return;
    }

#if defined(CONFIG_LATX_TU) && defined(CONFIG_SOFTMMU)
    if (latx_aot_native_jump_target(tb_next) &&
        !latxs_aot_direct_call_entry(tb, n, tb_next)) {
        return;
    }
    if ((unlikely(option_tu_hras) ||
         (tb->bool_flags & (IS_AOT_TB | IS_HRAS_CALL)) ==
             (IS_AOT_TB | IS_HRAS_CALL)) && qemu_tcg_mode_2_lat()) {
        if (tb->jmp_insn_offset[n] == TB_JMP_OFFSET_INVALID) {
            return;
        }
        /* The destination CAS below cannot claim an occupied or tagged
         * slot. Its HRAS layout was prepared before the winning claim;
         * do not repeat the owner-member search on these exits. */
        if (qatomic_read(&tb->jmp_dest[n])) {
            return;
        }
        tb_next = latxs_hras_local_target(tb, tb_next);
        if (!latxs_hras_prepare_link(current_cpu, tb, n, tb_next)) {
            return;
        }
    }
#endif
    qemu_thread_jit_write();
    assert(n < ARRAY_SIZE(tb->jmp_list_next));
    qemu_spin_lock(&tb_next->jmp_lock);

    /* make sure the destination TB is valid */
    if (tb_next->cflags & CF_INVALID) {
        goto out_unlock_next;
    }
    /* Atomically claim the jump destination slot only if it was NULL */
    old = qatomic_cmpxchg(&tb->jmp_dest[n], (uintptr_t)NULL,
                          (uintptr_t)tb_next);
    if (old) {
        goto out_unlock_next;
    }

    /*
     * We must set the link_status to 1 before the actual linking
     * occurs to avoid encountering a race condition when invoking
     * the unlink_old_dest() function.
     */
    qatomic_or(&tb->misc_flags, MF_LINK_STATUS);

    /* patch the native jump address */
    if (qemu_tcg_mode_1_tcg()) {
        tb_set_jmp_target(tb, n, (uintptr_t)tb_next->tc.ptr);
    } else if (qemu_tcg_mode_2_lat()) {
        latx_tb_set_jmp_target(tb, n, tb_next);
    } else {
        assert(0);
    }

    /* add in TB jmp list */
    tb->jmp_list_next[n] = tb_next->jmp_list_head;
    tb_next->jmp_list_head = (uintptr_t)tb | n;

    qemu_spin_unlock(&tb_next->jmp_lock);

    qemu_log_mask(CPU_LOG_EXEC, "Linking TBs %p index %d -> %p\n",
                  tb->tc.ptr, n, tb_next->tc.ptr);
    return;

 out_unlock_next:
    qemu_spin_unlock(&tb_next->jmp_lock);
    return;
}

static bool cpu_handle_halt(CPUState *cpu)
{
#ifndef CONFIG_USER_ONLY
    if (cpu->halted) {
#if defined(TARGET_I386)
        if (cpu->interrupt_request & CPU_INTERRUPT_POLL) {
            X86CPU *x86_cpu = X86_CPU(cpu);
            qemu_mutex_lock_iothread();
            apic_poll_irq(x86_cpu->apic_state);
            cpu_reset_interrupt(cpu, CPU_INTERRUPT_POLL);
            qemu_mutex_unlock_iothread();
        }
#endif /* TARGET_I386 */
#ifdef CONFIG_LATX_OPT
        if (x86_cpu_pending_interrupt(cpu, cpu->interrupt_request) == 0) {
            return true;
	    }
#else
        if (!cpu_has_work(cpu)) {
            return true;
        }
#endif

        cpu->halted = 0;
    }
#endif /* !CONFIG_USER_ONLY */

    return false;
}

static void cpu_handle_debug_exception(CPUState *cpu)
{
    CPUClass *cc = CPU_GET_CLASS(cpu);
    CPUWatchpoint *wp;

    if (!cpu->watchpoint_hit) {
        QTAILQ_FOREACH(wp, &cpu->watchpoints, entry) {
            wp->flags &= ~BP_WATCHPOINT_HIT;
        }
    }

    if (cc->tcg_ops->debug_excp_handler) {
        cc->tcg_ops->debug_excp_handler(cpu);
    }
}

#ifdef CONFIG_LATX_OPT
extern void x86_cpu_do_interrupt(CPUState *cpu);
extern bool x86_cpu_exec_interrupt(CPUState *cpu, int int_req);
#endif

static bool cpu_handle_exception(CPUState *cpu, int *ret)
{
    if (cpu->exception_index < 0) {
#ifndef CONFIG_USER_ONLY
        if (replay_has_exception()
            && cpu_neg(cpu)->icount_decr.u16.low + cpu->icount_extra == 0) {
            /* Execute just one insn to trigger exception pending in the log */
            cpu->cflags_next_tb = (curr_cflags(cpu) & ~CF_USE_ICOUNT)
                | CF_LAST_IO | CF_NOIRQ | 1;
        }
#endif
        return false;
    }
#if defined(CONFIG_LATX_TU) && defined(CONFIG_SOFTMMU)
    cpu->env_ptr->aot_observed_call_pending.valid = false;
#endif
    if (cpu->exception_index >= EXCP_INTERRUPT) {
        /* exit request from the cpu execution loop */
        *ret = cpu->exception_index;
        if (*ret == EXCP_DEBUG) {
            cpu_handle_debug_exception(cpu);
        }
        cpu->exception_index = -1;
        return true;
    } else {
#if defined(CONFIG_USER_ONLY)
        /* if user mode only, we simulate a fake exception
           which will be handled outside the cpu execution
           loop */
#if defined(TARGET_I386)
        CPUClass *cc = CPU_GET_CLASS(cpu);
        cc->tcg_ops->fake_user_interrupt(cpu);
#endif /* TARGET_I386 */
        *ret = cpu->exception_index;
        cpu->exception_index = -1;
        return true;
#else
        if (replay_exception()) {
#ifdef CONFIG_LATX_OPT
            /*qemu_mutex_lock_iothread();*/
            x86_cpu_do_interrupt(cpu);
            /*qemu_mutex_unlock_iothread();*/
#else
            CPUClass *cc = CPU_GET_CLASS(cpu);
            qemu_mutex_lock_iothread();
            cc->tcg_ops->do_interrupt(cpu);
            qemu_mutex_unlock_iothread();
#endif
            cpu->exception_index = -1;

            if (unlikely(cpu->singlestep_enabled)) {
                /*
                 * After processing the exception, ensure an EXCP_DEBUG is
                 * raised when single-stepping so that GDB doesn't miss the
                 * next instruction.
                 */
                *ret = EXCP_DEBUG;
                cpu_handle_debug_exception(cpu);
                return true;
            }
        } else if (!replay_has_interrupt()) {
            /* give a chance to iothread in replay mode */
            *ret = EXCP_INTERRUPT;
            return true;
        }
#endif
    }

    return false;
}

#ifndef CONFIG_USER_ONLY
/*
 * CPU_INTERRUPT_POLL is a virtual event which gets converted into a
 * "real" interrupt event later. It does not need to be recorded for
 * replay purposes.
 */
static bool need_replay_interrupt(int interrupt_request)
{
#if defined(TARGET_I386)
    return !(interrupt_request & CPU_INTERRUPT_POLL);
#else
    return true;
#endif
}
#endif /* !CONFIG_USER_ONLY */

#if defined(CONFIG_LATX_OPT) && defined(TARGET_I386) && \
    !defined(CONFIG_USER_ONLY)
static bool latx_maskable_interrupt_gate_closed(CPUState *cpu,
                                                 int interrupt_request)
{
    const int maskable = CPU_INTERRUPT_HARD | CPU_INTERRUPT_VIRQ;

    /* Other requests must retain the serialized handling below. */
    return interrupt_request &&
           !(interrupt_request & ~maskable) &&
           x86_cpu_pending_interrupt(cpu, interrupt_request) == 0;
}
#endif

static bool cpu_exec_exit_pending(CPUState *cpu)
{
    return unlikely(qatomic_read(&cpu->exit_request))
        || (icount_enabled()
        && (cpu->cflags_next_tb == -1 || cpu->cflags_next_tb & CF_USE_ICOUNT)
        && cpu_neg(cpu)->icount_decr.u16.low + cpu->icount_extra == 0);
}

static bool cpu_handle_interrupt(CPUState *cpu,
                                        TranslationBlock **last_tb)
{
#if defined(CONFIG_LATX_OPT) && defined(TARGET_I386) && \
    !defined(CONFIG_USER_ONLY)
    int interrupt_request;
#endif

    /*
     * If we have requested custom cflags with CF_NOIRQ we should
     * skip checking here. Any pending interrupts will get picked up
     * by the next TB we execute under normal cflags.
     */
    if (cpu->cflags_next_tb != -1 && cpu->cflags_next_tb & CF_NOIRQ) {
        return false;
    }

    /*
     * Clear the interrupt flag now since we're processing
     * cpu->interrupt_request and cpu->exit_request.
     * Ensure zeroing happens before reading cpu->exit_request or
     * cpu->interrupt_request (see also smp_wmb in cpu_exit())
     */
    qatomic_set_mb(&cpu_neg(cpu)->icount_decr.u16.high, 0);

#if defined(CONFIG_LATX_OPT) && defined(TARGET_I386) && \
    !defined(CONFIG_USER_ONLY)
    interrupt_request = qatomic_read(&cpu->interrupt_request);
    if (option_irq_gate_nobql &&
        unlikely(latx_maskable_interrupt_gate_closed(cpu, interrupt_request)) &&
        likely(!cpu_exec_exit_pending(cpu))) {
        uint32_t cflags = cpu->cflags_next_tb;

        if (cflags == -1) {
            cflags = curr_cflags(cpu);
        }
        /* Keep an IF-masked request pending. Reopening delivery checks it
         * explicitly; special execution modes keep their existing path. */
        if (qemu_tcg_mode_2_lat() &&
            (option_int_opt == IRQ_CHECK_BLOCK_END ||
             option_int_opt == IRQ_CHECK_ASYNC_NOTIFY) &&
            inhibit_irq_hflags() && !(cpu->env_ptr->eflags & IF_MASK) &&
            cpu->cflags_next_tb == -1 && replay_mode == REPLAY_MODE_NONE &&
            !(cflags & (CF_USE_ICOUNT | CF_NO_GOTO_TB | CF_NO_GOTO_PTR))) {
            return false;
        }
        /* IF=1 interrupt shadows and special modes still use an unchained TB. */
        if (cpu->env_ptr->cr[0] & CR0_PG_MASK) {
            cpu->cflags_next_tb = cflags | CF_NO_GOTO_TB | CF_NO_GOTO_PTR;
            cpu->env_ptr->aot_observed_call_pending.valid = false;
            *last_tb = NULL;
            return false;
        }
    }
#endif

    if (unlikely(qatomic_read(&cpu->interrupt_request))) {
        int interrupt_request;
        qemu_mutex_lock_iothread();
        interrupt_request = cpu->interrupt_request;
        if (unlikely(cpu->singlestep_enabled & SSTEP_NOIRQ)) {
            /* Mask out external interrupts for this step. */
            interrupt_request &= ~CPU_INTERRUPT_SSTEP_MASK;
        }
        if (interrupt_request & CPU_INTERRUPT_DEBUG) {
            cpu->interrupt_request &= ~CPU_INTERRUPT_DEBUG;
            cpu->exception_index = EXCP_DEBUG;
            qemu_mutex_unlock_iothread();
            return true;
        }
#if !defined(CONFIG_USER_ONLY)
        if (replay_mode == REPLAY_MODE_PLAY && !replay_has_interrupt()) {
            /* Do nothing */
        } else if (interrupt_request & CPU_INTERRUPT_HALT) {
            replay_interrupt();
            cpu->interrupt_request &= ~CPU_INTERRUPT_HALT;
            cpu->halted = 1;
            cpu->exception_index = EXCP_HLT;
            qemu_mutex_unlock_iothread();
            return true;
        }
#if defined(TARGET_I386)
        else if (interrupt_request & CPU_INTERRUPT_INIT) {
            X86CPU *x86_cpu = X86_CPU(cpu);
            CPUArchState *env = &x86_cpu->env;
            replay_interrupt();
            cpu_svm_check_intercept_param(env, SVM_EXIT_INIT, 0, 0);
            do_cpu_init(x86_cpu);
            cpu->exception_index = EXCP_HALTED;
            qemu_mutex_unlock_iothread();
            return true;
        }
#else
        else if (interrupt_request & CPU_INTERRUPT_RESET) {
            replay_interrupt();
            cpu_reset(cpu);
            qemu_mutex_unlock_iothread();
            return true;
        }
#endif /* !TARGET_I386 */
        /* The target hook has 3 exit conditions:
           False when the interrupt isn't processed,
           True when it is, and we should restart on a new TB,
           and via longjmp via cpu_loop_exit.  */
        else {
#ifndef CONFIG_LATX_OPT
            CPUClass *cc = CPU_GET_CLASS(cpu);

            if (cc->tcg_ops->cpu_exec_interrupt &&
                cc->tcg_ops->cpu_exec_interrupt(cpu, interrupt_request)) {
#else
            if (x86_cpu_exec_interrupt(cpu, interrupt_request)) {
#endif
#if defined(CONFIG_LATX_TU) && defined(CONFIG_SOFTMMU)
                cpu->env_ptr->aot_observed_call_pending.valid = false;
#endif
                if (need_replay_interrupt(interrupt_request)) {
                    replay_interrupt();
                }
                /*
                 * After processing the interrupt, ensure an EXCP_DEBUG is
                 * raised when single-stepping so that GDB doesn't miss the
                 * next instruction.
                 */
                if (unlikely(cpu->singlestep_enabled)) {
                    cpu->exception_index = EXCP_DEBUG;
                    qemu_mutex_unlock_iothread();
                    return true;
                }
                cpu->exception_index = -1;
                *last_tb = NULL;
            }
            /* The target hook may have updated the 'cpu->interrupt_request';
             * reload the 'interrupt_request' value */
            interrupt_request = cpu->interrupt_request;
#ifdef CONFIG_LATX_OPT
            /*
             * Delivery was refused (interrupt gates closed: IF=0 or the
             * sti shadow).  Nothing re-arms the translated-code exit flag
             * afterwards, and the LAPIC timer is one-shot, so without a
             * re-arm the request can starve forever.  Re-arm so the vCPU
             * exits generated code again at the next TB start and
             * re-evaluates once the gates have opened.
             */
            if (interrupt_request & (CPU_INTERRUPT_HARD | CPU_INTERRUPT_NMI |
                                     CPU_INTERRUPT_SMI | CPU_INTERRUPT_POLL)) {
                lats_request_tb_exit(cpu);
            }
#endif
        }
#endif /* !CONFIG_USER_ONLY */
        if (interrupt_request & CPU_INTERRUPT_EXITTB) {
            cpu->interrupt_request &= ~CPU_INTERRUPT_EXITTB;
#if defined(CONFIG_LATX_TU) && defined(CONFIG_SOFTMMU)
            cpu->env_ptr->aot_observed_call_pending.valid = false;
#endif
            /* ensure that no TB jump will be modified as
               the program flow was changed */
            *last_tb = NULL;
        }

        /* If we exit via cpu_loop_exit/longjmp it is reset in cpu_exec */
        qemu_mutex_unlock_iothread();
    }

    /* Finally, check if we need to exit to the main loop.  */
    if (cpu_exec_exit_pending(cpu)) {
        qatomic_set(&cpu->exit_request, 0);
        if (cpu->exception_index == -1) {
            cpu->exception_index = EXCP_INTERRUPT;
        }
        return true;
    }

    return false;
}

static void cpu_loop_exec_tb(CPUState *cpu, TranslationBlock *tb,
                                    vaddr pc, TranslationBlock **last_tb,
                                    int *tb_exit, const void *entry)
{
    int32_t insns_left;

    trace_exec_tb(tb, pc);
    tb = cpu_tb_exec(cpu, tb, tb_exit, entry);
    if (*tb_exit != TB_EXIT_REQUESTED) {
        *last_tb = tb;
        return;
    }

    *last_tb = NULL;
    insns_left = qatomic_read(&cpu_neg(cpu)->icount_decr.u32);
    if (insns_left < 0) {
        /* Something asked us to stop executing chained TBs; just
         * continue round the main loop. Whatever requested the exit
         * will also have set something else (eg exit_request or
         * interrupt_request) which will be handled by
         * cpu_handle_interrupt.  cpu_handle_interrupt will also
         * clear cpu->icount_decr.u16.high.
         */
        return;
    }

    /* Instruction counter expired.  */
    assert(icount_enabled());
#ifndef CONFIG_USER_ONLY
    /* Ensure global icount has gone forward */
    icount_update(cpu);
    /* Refill decrementer and continue execution.  */
    insns_left = MIN(0xffff, cpu->icount_budget);
    cpu_neg(cpu)->icount_decr.u16.low = insns_left;
    cpu->icount_extra = cpu->icount_budget - insns_left;

    /*
     * If the next tb has more instructions than we have left to
     * execute we need to ensure we find/generate a TB with exactly
     * insns_left instructions in it.
     */
    if (insns_left > 0 && insns_left < tb->icount)  {
        assert(insns_left <= CF_COUNT_MASK);
        assert(cpu->icount_extra == 0);
        cpu->cflags_next_tb = (tb->cflags & ~CF_COUNT_MASK) | insns_left;
    }
#endif
}

static inline ADDRX get_page(ADDRX pc)
{
    return pc & TARGET_PAGE_MASK;
}

/* main execution loop */
static int __attribute__((noinline))
cpu_exec_loop(CPUState *cpu, SyncClocks *sc)
{
    int ret;

    /* if an exception is pending, we execute it here */
    while (!cpu_handle_exception(cpu, &ret)) {
        TranslationBlock *last_tb = NULL;
        int tb_exit = 0;
#if defined(CONFIG_LATX_TU) && defined(CONFIG_SOFTMMU)
        unsigned last_mtcc = 0;
        uint32_t last_full_count = 0, last_mtcc_count = 0;
#endif

        while (!cpu_handle_interrupt(cpu, &last_tb)) {
            TranslationBlock *tb;
            vaddr pc;
            uint64_t cs_base;
            uint32_t flags, cflags;
#if defined(CONFIG_LATX_TU) && defined(CONFIG_SOFTMMU)
            const void *call_entry = NULL;
            unsigned mtcc = curr_mtcc(cpu->env_ptr);
#endif

            CPUX86State *env = cpu->env_ptr;
#if defined(CONFIG_LATX_TU) && defined(CONFIG_SOFTMMU)
            LatxAOTCallObservation observed_call = env->aot_observed_call_pending;
            /* Consume before lookup, translation, breakpoints or native resume.
             * A failure must not tag a later exception or interrupt handler. */
            env->aot_observed_call_pending.valid = false;
#endif

#ifdef CONFIG_LATX_HACK_MEM
            tb = hamt_native_resume_tb(env);
            if (tb) {
                cflags = cpu->cflags_next_tb == -1 ? curr_cflags(cpu) :
                                                    cpu->cflags_next_tb;
                if ((cflags & (CF_SINGLE_STEP | CF_USE_ICOUNT |
                               CF_NO_GOTO_TB | CF_NO_GOTO_PTR)) ||
                    !QTAILQ_EMPTY(&cpu->breakpoints) ||
                    !QTAILQ_EMPTY(&cpu->watchpoints)) {
                    error_report("LLVM resume: debug or constrained execution "
                                 "requires precise recovery");
                    abort();
                }
                pc = tb->pc;
                call_entry = hamt_native_resume_entry;
                last_tb = NULL;
                goto execute_tb;
            }
#endif
            if (env->aot_native_fault_active &&
                (env->hflags & HF_CPL_MASK) == HF_CPL_MASK) {
                env->hflags |= HF_FORCE_TB_MODE;
            }

            if ((env->hflags & HF_FORCE_TB_MODE) &&
                (env->hflags & HF_CPL_MASK) != HF_CPL_MASK) {
                env->hflags &= ~HF_FORCE_TB_MODE;
            }
            cpu_get_tb_cpu_state(env, &pc, &cs_base, &flags);

            /*
             * When requested, use an exact setting for cflags for the next
             * execution.  This is used for icount, precise smc, and stop-
             * after-access watchpoints.  Since this request should never
             * have CF_INVALID set, -1 is a convenient invalid value that
             * does not require tcg headers for cpu_common_reset.
             */
            cflags = cpu->cflags_next_tb;
            if (cflags == -1) {
                cflags = curr_cflags(cpu);
            } else {
                cpu->cflags_next_tb = -1;
            }

            if (env->aot_native_fault_active &&
                (flags & HF_CPL_MASK) == HF_CPL_MASK) {
                cflags = (cflags & ~CF_COUNT_MASK) |
                    CF_NO_GOTO_TB | CF_NO_GOTO_PTR | 1;
            }

            if (check_for_breakpoints(cpu, pc, &cflags)) {
                break;
            }

            tb = cpu_tb_lookup(cpu, pc, cs_base, &flags, cflags);

            bool retry_aot = tb == NULL;
#if defined(CONFIG_LATX_TU) && defined(CONFIG_SOFTMMU)
            retry_aot |= tb && (flags & HF_CPL_MASK) == HF_CPL_MASK &&
                aot_tu_enabled() && !(tb->bool_flags & IS_AOT_TB);
#endif
            if (retry_aot && !(flags & HF_FORCE_TB_MODE)) {
                TranslationBlock *loaded = load_aot(cpu, pc, cs_base,
                                                    flags, cflags, lsenv);
                if (loaded && loaded->pc == pc &&
                    loaded->cs_base == cs_base && loaded->flags == flags &&
                    tb_cflags(loaded) == cflags) {
                    tb = loaded;
                }
            }

            if (tb == NULL) {
#ifdef CONFIG_LATX_OPT_NOFP
                if (qemu_tcg_mode_2_lat()) {
                    __nofp_sync_fpr_to_env(cpu, 1);
                }
#endif
                mmap_lock();
#ifdef CONFIG_LATX_TU
                int do_tu = 0;
                if (jit_tu_enabled() && (flags & HF_CPL_MASK) &&
                    !(flags & HF_FORCE_TB_MODE)) {
                    do_tu = 1;
#ifdef CONFIG_SOFTMMU
                    if (tlb_rw_check(cpu->env_ptr, 1, pc + cs_base)) do_tu = 0;
#endif
                    if (cpu_is_inhibit_irq(cpu)) do_tu = 0;
                }

                tb = do_tu ? tu_gen_code(cpu, pc, cs_base, flags, cflags) :
                             tb_gen_code(cpu, pc, cs_base, flags, cflags);
#else
                tb = tb_gen_code(cpu, pc, cs_base, flags, cflags);
#endif
                mmap_unlock();

                /*
                 * We add the TB in the virtual pc hash table
                 * for the fast lookup
                 */
#ifdef LATX_PCREL
                if (cflags & CF_PCREL) {
                    if (tu_jmp_cache_can_insert(cpu, tb)) {
                        tb_jmp_cache_insert(cpu, pc, true, tb);
                    }
                } else {
#else
                {
#endif
                    /* Use the pc value already stored in tb->pc. */
                    if (tu_jmp_cache_can_insert(cpu, tb)) {
                        tb_jmp_cache_insert(cpu, pc, false, tb);
                    }
                }
            }

#if defined(CONFIG_LATX_TU) && defined(CONFIG_SOFTMMU)
            if (observed_call.valid && tu_jmp_cache_can_insert(cpu, tb)) {
                latx_record_call_entry(cpu, tb, &observed_call);
            }
#endif
#ifndef CONFIG_USER_ONLY
            /*
             * We don't take care of direct jumps when address mapping
             * changes in system emulation.  So it's not safe to make a
             * direct jump to a TB spanning two pages because the mapping
             * for the second page can change.
             */
            if (tb_page_addr1(tb) != -1 && !cp_tb_enabled()) {
                last_tb = NULL;
            }
#if defined(CONFIG_LATX_TU) && defined(CONFIG_SOFTMMU)
            if (last_tb && (last_mtcc != mtcc ||
                last_full_count != qatomic_read(&tb_ctx.tb_flush_count) ||
                last_mtcc_count != qatomic_read(&tb_ctx.tb_flush_count_mtcc[mtcc]))) {
                last_tb = NULL;
            }
            if (last_tb && tb_exit == 0 &&
                !(tb_cflags(last_tb) & CF_INVALID) &&
                (last_tb->inst_ptr_offset || (last_tb->bool_flags & IS_HRAS_CALL)) &&
                TB_TU_PAGES(tb) &&
                tu_pages_valid(cpu, tb)) {
                call_entry = latxs_aot_direct_call_entry(last_tb, 0, tb);
            }
#endif
            /* See if we can patch the calling TB. */
            if (last_tb) {
                if (qemu_tcg_mode_2_lat() &&
                    !(last_tb->cflags & CF_USE_ICOUNT))
                {
                    TranslationBlock *old_next = NULL;
                    uintptr_t ptr = qatomic_read(&last_tb->jmp_dest[tb_exit]);
                    if (!(ptr & 0x1)) {
                        old_next = (TranslationBlock *)(ptr);
                    }
                    if (old_next &&
                        (old_next->pc == tb->pc) &&
                        (old_next->page_addr[0] != tb->page_addr[0]) &&
                        !check_misc_flags(last_tb, MF_LINK_STATUS)) {
                        unlink_old_dest(last_tb, old_next, tb_exit);
                    }
                    tb_add_jump(last_tb, tb_exit, tb);
                } else {
                    tb_add_jump(last_tb, tb_exit, tb);
                }
            }
#else
            if (last_tb) {
                tb_add_jump(last_tb, tb_exit, tb);
            }
#endif

#ifdef CONFIG_RAS_PROFILER
            ras_profiler(cpu);
#endif

#if defined(CONFIG_LATX_TU) && defined(CONFIG_SOFTMMU)
#ifdef CONFIG_LATX_HACK_MEM
        execute_tb:
#endif
            last_mtcc = mtcc;
            last_full_count = qatomic_read(&tb_ctx.tb_flush_count);
            last_mtcc_count = qatomic_read(&tb_ctx.tb_flush_count_mtcc[mtcc]);
#endif
            cpu_loop_exec_tb(cpu, tb, pc, &last_tb, &tb_exit,
#if defined(CONFIG_LATX_TU) && defined(CONFIG_SOFTMMU)
                             call_entry
#else
                             NULL
#endif
                             );

            /* Try to align the host and virtual clocks
               if the guest is in advance */
            align_clocks(sc, cpu);
        }
#ifdef CONFIG_LATX_OPT_NOFP
        if (qemu_tcg_mode_2_lat()) {
            __nofp_sync_fpr_to_env(cpu, 2);
        }
#endif
    }
    return ret;
}

static int cpu_exec_setjmp(CPUState *cpu, SyncClocks *sc)
{
    /* Prepare setjmp context for exception handling. */
    if (unlikely(sigsetjmp(cpu->jmp_env, 0) != 0)) {
        cpu_exec_longjmp_cleanup(cpu);
    }

#if defined(CONFIG_LATX_TU) && defined(CONFIG_SOFTMMU)
    cpu->env_ptr->aot_observed_call_pending.valid = false;
#endif
    return cpu_exec_loop(cpu, sc);
}

int cpu_exec(CPUState *cpu)
{
    int ret;
    SyncClocks sc = { 0 };

    /* replay_interrupt may need current_cpu */
    current_cpu = cpu;

    if (cpu_handle_halt(cpu)) {
        return EXCP_HALTED;
    }

    rcu_read_lock();
    cpu_exec_enter(cpu);

    /*
     * Calculate difference between guest clock and host clock.
     * This delay includes the delay of the last cycle, so
     * what we have to do is sleep until it is 0. As for the
     * advance/delay we gain here, we try to fix it next time.
     */
    init_delay_params(&sc, cpu);

    ret = cpu_exec_setjmp(cpu, &sc);

    cpu_exec_exit(cpu);
    rcu_read_unlock();

    return ret;
}

static LatxNJC *latx_njc_aligned_alloc(size_t entries)
{
    size_t bytes = entries * sizeof(LatxNJCEntry);
    size_t header = offsetof(LatxNJC, array);
    void *allocation;
    LatxNJC *cache;

    G_STATIC_ASSERT(sizeof(LatxNJCEntry) == 32);
    g_assert(bytes && !(bytes & (bytes - 1)) && bytes <= SIZE_MAX / 2);
    g_assert(bytes >= header + sizeof(allocation));
    /* Keep the existing flexible-array ABI and RCU header. Place the array
     * at the aligned midpoint; only its header and contents are initialized.
     * The preceding word retains the real allocation for deferred freeing. */
    allocation = qemu_memalign(bytes, 2 * bytes);
    cache = (LatxNJC *)((char *)allocation + bytes - header);
    memset(cache, 0, header + bytes);
    memcpy((char *)cache - sizeof(allocation), &allocation, sizeof(allocation));
    g_assert(!((uintptr_t)cache->array & (bytes - 1)));
    return cache;
}

static void latx_njc_aligned_free(LatxNJC *cache)
{
    void *allocation;

    memcpy(&allocation, (char *)cache - sizeof(allocation), sizeof(allocation));
    qemu_vfree(allocation);
}

void tcg_alloc_jump_caches(CPUJumpCache **jc, LatxNJC **njc)
{
    size_t n = latx_jmp_cache_size();

    *jc = g_malloc0(sizeof(**jc) + n * sizeof((*jc)->array[0]));
    *njc = NULL;
    if (latx_jmp_cache_compact_enabled()) {
        *njc = option_table_addr ? latx_njc_aligned_alloc(n) :
            g_malloc0(sizeof(**njc) + n * sizeof((*njc)->array[0]));
    }
}

void tcg_free_jump_caches(CPUJumpCache *jc, LatxNJC *njc)
{
    if (njc) {
        if (option_table_addr) {
            call_rcu(njc, latx_njc_aligned_free, rcu);
        } else {
            g_free_rcu(njc, rcu);
        }
    }
    g_free_rcu(jc, rcu);
}

void tcg_exec_realizefn(CPUState *cpu, Error **errp)
{
    static bool tcg_target_initialized;
    CPUClass *cc = CPU_GET_CLASS(cpu);

    if (!tcg_target_initialized) {
        cc->tcg_ops->initialize();
        tcg_target_initialized = true;
    }

    tcg_alloc_jump_caches(&cpu->tb_jmp_cache, &cpu->latx_njc_cache);
    tlb_init(cpu);
#ifdef CONFIG_SOFTMMU
    latx_pcid_cache_init(cpu);
#endif
    if (option_soft_ras) {
        latxs_ras_init(cpu);
    }
#ifndef CONFIG_USER_ONLY
    tcg_iommu_init_notifier_list(cpu);
#endif /* !CONFIG_USER_ONLY */
    /* qemu_plugin_vcpu_init_hook delayed until cpu_index assigned. */
}

/* undo the initializations in reverse order */
void tcg_exec_unrealizefn(CPUState *cpu)
{
#ifndef CONFIG_USER_ONLY
    tcg_iommu_free_notifier_list(cpu);
#endif /* !CONFIG_USER_ONLY */

#ifdef CONFIG_SOFTMMU
    latx_pcid_cache_destroy(cpu);
#endif
    tlb_destroy(cpu);
    tcg_free_jump_caches(cpu->tb_jmp_cache, cpu->latx_njc_cache);
}
