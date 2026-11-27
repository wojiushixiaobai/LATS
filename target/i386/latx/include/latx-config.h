/*
 * SPDX-FileCopyrightText: 2024-2026 LATS Project Authors
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef _LATX_CONFIG_H_
#define _LATX_CONFIG_H_

#include "latx-types.h"
#include "qemu/osdep.h"
#include "exec/exec-all.h"
#include "latx-tb-trace-sys.h"
#include "./error.h"
#include "latx/include/latx-hamt-counter.h"
#include "latx/include/latx-tb-counter.h"
#include "latx/include/latx-cp-counter.h"
#include "latx/include/latx-intb-counter.h"

/* for pcrel, during translate, only get tb->pc from env*/
target_ulong get_eip_cpu_state(TranslationBlock *tb);

void latx_tb_set_jmp_target(struct TranslationBlock *, int, struct TranslationBlock *);
/* option sysflagtb: tb_add_jump() veto for a deferred-eflags exit edge. */
bool latx_tb_eflag_link_allowed(struct TranslationBlock *, int,
                                struct TranslationBlock *);

/*
 * option sysflagtb runtime invariants (verification only; all updates are
 * skipped when the option is off, which is the default, so a stock run is
 * unaffected).  The counters are host-side except for MATERIALISE, which is
 * incremented by an opt-in instruction sequence (LATS_SYSFLAGTB_STAT=1 at
 * translation time) placed on the exit slow path immediately before the
 * deferred flag producer.  They are printed once on QEMU exit.
 */
enum {
    /* TBs translated with at least one edge that skips materialisation. */
    LATX_TB_EFLAG_STAT_DEFER_TB,
    /* skip bits recorded over those TBs (0..2 per TB). */
    LATX_TB_EFLAG_STAT_DEFER_EDGE,
    /* direct links actually installed on a skip edge. */
    LATX_TB_EFLAG_STAT_LINK,
    /* skip links refused: successor TB reads incoming flags. */
    LATX_TB_EFLAG_STAT_VETO_USE,
    /* skip links refused: successor TB usedef answer not published yet. */
    LATX_TB_EFLAG_STAT_VETO_UNPUB,
    /* exit slow paths that executed the deferred materialisation. */
    LATX_TB_EFLAG_STAT_MATERIALISE,
    LATX_TB_EFLAG_STAT_RECOVERED,
    /* in-flight recovery looked at a nonzero $ra that was not a marker. */
    LATX_TB_EFLAG_STAT_RECOVER_REJECT,
    LATX_TB_EFLAG_STAT_NR,
};
extern uint64_t latx_tb_eflag_stats[LATX_TB_EFLAG_STAT_NR];
void latx_tb_eflag_stat_defer(int edge_index);
void latx_tb_eflag_stat_link(TranslationBlock *tb, int n,
                             TranslationBlock *next_tb);
/* Non-NULL only when the opt-in slow-path materialisation counter is on. */
uint64_t *latx_tb_eflag_stat_materialise_counter(void);
#if defined(CONFIG_LATX_TU) && defined(CONFIG_SOFTMMU)
void *latxs_aot_direct_call_entry(struct TranslationBlock *, int,
                                  struct TranslationBlock *);
const void *latxs_aot_direct_call_replay(struct TranslationBlock *,
                                         struct TranslationBlock *, bool,
                                         uint32_t *, uint32_t *);
const void *latxs_aot_linked_call_replay(struct TranslationBlock *,
                                         struct TranslationBlock *);
const void *latxs_aot_ordinary_jump_replay(struct TranslationBlock *,
                                           struct TranslationBlock *);
#endif
void latx_tb_reset_jmp_target(struct TranslationBlock *, int);

#include "latx-sys-env-init-func.h"
void latxs_lsenv_switch(CPUArchState *env);
/* For Functions defined in latx and used outside */
//int target_latxs_static_codes(void *code_ptr);
#include "latx-sys-static-codes-func.h"
int target_latxs_host(CPUState *cpu, TranslationBlock *tb,
        int max_insns, void *code_highwater, int *search_size,
        void *host_pc);
//#include "latx-sys-tb-exec-func.h"
static inline void latxs_before_exec_tb(CPUState *cpu, TranslationBlock *tb)
{
    __attribute__((__unused__)) CPUX86State *env = cpu->env_ptr;

    lsassertm(env->is_fcsr_simd == 0, "fcsr simd %s\n", __func__);

#ifdef CONFIG_LATX_PRINT
    tb_counter_print_before_tb();
    cp_counter_print_before_tb();
    intb_counter_print_before_tb();
#if defined(CONFIG_SOFTMMU)
    hamt_ipi_counter_print_before_tb(cpu->cpu_index);
    hamt_tlbrefill_counter_print_before_tb(cpu->cpu_index);
#endif
#endif

    latxs_tb_trace(env, tb, 0);
}

static inline void latxs_after_exec_tb(CPUState *cpu, TranslationBlock *tb)
{
}
void latxs_fix_after_excp_or_int(CPUState *cpu);

void latxs_update_fcsr(void *env);

/* This is used in target/i386/fpu_helper.c */
int latxs_get_top_bias_from_env(CPUX86State *env);

#endif /* _LATX_CONFIG_H_ */
