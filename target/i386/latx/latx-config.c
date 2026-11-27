/*
 * SPDX-FileCopyrightText: 2024-2026 LATS Project Authors
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "common.h"
#include "ir1.h"
#include "ir2.h"
#include "lsenv.h"
#include "reg-alloc.h"
#include "latx-options.h"
#include "flag-reduction.h"
#include "translate.h"
#include "hamt-ptw.h"
#include "latx-config.h"
#include "latx-sys-flag-rdtn.h"
#include "latx-static-codes.h"
#include "latx-tb-trace-sys.h"
#include "latx-sys-inst-ptn.h"
#include "latx-sysflagtb-recovery.h"
#include "latx-native-asm.h"
#include "latx-cp-counter.h"
#include "latx-ras.h"
#ifdef CONFIG_LATX_TU
#include "./include/latx-tu.h"
#endif

#ifdef CONFIG_LATX_PRINT
uint64_t cp_num[2];
uint64_t cp_tb_hit[2];
uint64_t cp_itlb_hit[2];
uint64_t cp_ptw_hit[2];
uint64_t cp_check_hit[2];
#endif

#include "latx-hamt.h"
#include "latx-hamt-counter.h"
#include "hamt-cpu.h"
#include "hamt-dmw.h"

#include "sysemu/rr.h"
#include "sysemu/sysemu.h"

#include "latxs-imm-cache.h"

static void xtm_capstone_init(void)
{
    if (cs_open(CS_ARCH_X86, CS_MODE_32, &handle) != CS_ERR_OK) {
        fprintf(stderr, "%s %s %d error : cs_open \n", __FILE__, __func__,
                __LINE__);
        exit(-1);
    }
    cs_option(handle, CS_OPT_DETAIL, CS_OPT_ON);

#ifdef CONFIG_SOFTMMU
    if (cs_open(CS_ARCH_X86, CS_MODE_16, &handle16) != CS_ERR_OK) {
        fprintf(stderr, "%s %s %d error : cs_open\n",
                __FILE__, __func__, __LINE__);
        exit(-1);
    }
    cs_option(handle16, CS_OPT_DETAIL, CS_OPT_ON);
#endif

    if (cs_open(CS_ARCH_X86, CS_MODE_64, &handle64) != CS_ERR_OK) {
        fprintf(stderr, "%s %s %d error : cs_open\n",
                __FILE__, __func__, __LINE__);
        exit(-1);
    }
    cs_option(handle64, CS_OPT_DETAIL, CS_OPT_ON);
}

static void __attribute__((__constructor__)) latx_init(void)
{
    latxs_options_init();
}

static __thread ENV lsenv_real;
__thread TRANSLATION_DATA tr_data_real;

/* global lsenv defined here */
__thread ENV *lsenv;

#include "accel/tcg/tb-jmp-cache.h"
#include "qemu/log-for-trace.h"

#ifdef CONFIG_SOFTMMU
extern void *hpet_global_ptr;
extern void *timers_state_ptr;

extern QemuMutex qemu_global_mutex;
#endif

void latx_lsenv_init(CPUArchState *env)
{
    lsenv = &lsenv_real;
    lsenv->cpu_state = env;
    lsenv->tr_data = &tr_data_real;
#ifdef CONFIG_LATX_TU
    tu_control_init();
#endif

    if (!lsenv->tr_data->slow_path_rcd) {
        lsenv->tr_data->slow_path_rcd_max = 4;
        lsenv->tr_data->slow_path_rcd_nr  = 0;
        lsenv->tr_data->slow_path_rcd =
            mm_calloc(4, sizeof(softmmu_sp_rcd_t));
    }

#ifdef LATXS_IMM_CACHE_ENABLE
    lsenv->tr_data->dynamic_option_imm = option_imm_cache;
    imm_trace("lsenv init dynamic option_imm_cache:%d\n", option_imm_cache);
#endif

#ifndef CONFIG_LATX_OPT_NOFP
    if (option_soft_fpu_fast) {
        env->mode_fpu = 1;
    }
#endif

    CPUState *cpu = env_cpu(env);
    X86CPU *xcpu = X86_CPU(cpu);
    env->tb_jmp_cache_ptr = (void *)&cpu->tb_jmp_cache->array[0];
    env->latx_njc_cache_ptr = cpu->latx_njc_cache ?
        (void *)&cpu->latx_njc_cache->array[0] : NULL;
    if (hamt_enabled() && hamt_mode_dmw()) {
        if (option_table_addr) {
            env->tb_jmp_cache_ptr =
                (void *)(uintptr_t)DMW_IN(env->tb_jmp_cache_ptr);
        }
        if (env->latx_njc_cache_ptr) {
            env->latx_njc_cache_ptr =
                (void *)(uintptr_t)DMW_IN(env->latx_njc_cache_ptr);
        }
    }
    if (tucp_generation_enabled() && !env->tu_generation_array) {
        void *array = g_malloc0(option_tucp_generation_array_size);

        env->tu_generation_array = hamt_enabled() && hamt_mode_dmw() ?
            (void *)(uintptr_t)DMW_IN(array) : array;
        env->tu_generation = tucp_generation_counter() ? 1 : 0;
    }
    env->in_hamt_exception = false;
    env->cflags = cpu->tcg_cflags;
    env->s_ras_ptr = cpu->s_ras;
#ifdef CONFIG_RAS_PROFILER
    env->ras_profile_ptr = malloc(sizeof(struct RAS_Profile));
    memset(env->ras_profile_ptr, 0, sizeof(struct RAS_Profile));
#endif

#if defined(CONFIG_SOFTMMU) && defined(CONFIG_LATX_PROFILER)
    env->hamt_data_ptr = (void *)hamt_cpu_base[cpu->cpu_index].data;
#endif

    if (qemu_loglevel_mask(CPU_LOG_TB_NOCHAIN)) {
        latx_disable_all_tblink();
    }

    env->rr_counter_ptr = rr_counter_get_ptr();
#ifdef CONFIG_SOFTMMU
    env->qemu_global_mutex_ptr = (void *)&(qemu_global_mutex.spin);
#endif
    env->syscall_tb = NULL;

#ifdef CONFIG_SOFTMMU
    if (hamt_use_ptw()) {
        env->ptw4k = hamt_get_ptwdata();
        env->ptw1m = hamt_get_ptwdata1m();
    }
#endif
#ifdef CONFIG_LATX_PRINT
    printf("%s:%d env %p maps of ptw4k %p\n", __func__, __LINE__,
            env, env->ptw4k);
    printf("%s:%d env %p maps of ptw1m %p\n", __func__, __LINE__,
            env, env->ptw1m);
#endif
    xtm_capstone_init();

#ifdef CONFIG_LATX_TB_EXEC_TRACE
    if (!env->tb_exec_trace) {
        env->tb_exec_trace = latx_new_tb_exec_trace();
        printf("%s:%d tb exec record %p\n", __func__, __LINE__,
                env->tb_exec_trace);
    }
    env->tb_exec_trace_nr = 0;
#endif

    env->apic_state = xcpu->apic_state;
    env->interrupt_request_ptr = &(cpu->interrupt_request);
#ifdef CONFIG_LATX_PRINT
    printf("%s:%d env %p apic_state %p\n", __func__, __LINE__,
            env, env->apic_state);
    printf("%s:%d env %p interrupt_request_ptr %p\n", __func__, __LINE__,
            env, env->interrupt_request_ptr);
#endif

#ifdef CONFIG_SOFTMMU
    env->hpet_state = hpet_global_ptr;
#ifdef CONFIG_LATX_PRINT
    printf("%s:%d env %p hpet_state %p\n", __func__, __LINE__,
            env, env->hpet_state);
#endif

    env->timers_state = timers_state_ptr;
#ifdef CONFIG_LATX_PRINT
    printf("%s:%d env %p timers_state %p\n", __func__, __LINE__,
            env, env->timers_state);
#endif
#endif
}

void latxs_lsenv_switch(CPUArchState *env)
{
    lsenv->cpu_state = env;
}

#include "sysemu/cpu-timers.h"

target_ulong get_eip_cpu_state(TranslationBlock *tb)
{
    if ((tb_cflags(tb) & CF_PCREL)) {
        CPUX86State *env = lsenv->cpu_state;
        /* if option ptb, may have to add an offset
           for TB in the sequence */
        int offset = 0;
        target_ulong eip = env->eip + env->segs[R_CS].base;
        // printf("get_eip: eip = %lx, offset = %x\n", eip, offset);
        return eip + offset;
    } else {
        return tb->pc;
    }
}
static void latx_tb_eflag_stat_init(void);

int target_latxs_host(CPUState *cpu, TranslationBlock *tb,
        int max_insns, void *code_highwater, int *search_size,
        void *host_pc)
{
#ifdef CONFIG_SOFTMMU
    if (icount_enabled() && qemu_rrtrace_enabled()) {
        /* rrtrace dump tb */
        if (rrtrace_mode == 3) {
            int do_dump = 0;
            /* dump via pc */
            if (rrtrace_func == 3 && qemu_rrtrace_dump_if_tb_pc(tb->pc)) {
                latx_enable_dump(); do_dump = 1;
            }
            if (!do_dump) latx_disable_dump();
        }
    }
#endif

    option_config_dump(tb->flags & 0x3);

    TRANSLATION_DATA *td = lsenv->tr_data;
    if (flag_reduction_tb_defer()) {
        latx_tb_eflag_stat_init();
    }
    latxs_tr_sys_init(tb, max_insns, code_highwater, host_pc);
    td->is_tu_mode = 0;

    latxs_tr_disasm(tb);

    if (td->deadlock_avoidance) {
        return td->deadlock_avoidance;
    }

    /* perform instruction pattern */
    latxs_inst_pattern(tb);
    /* perform flag reduction OR just set flag */
    latxs_flag_reduction(tb);

    int ret = latxs_tr_translate_tb(tb, search_size);

    /*
     * option sysflagtb: publish this TB's incoming-flag usedef answer.  A
     * predecessor that skipped its TB-final M-EFLAGS materialisation may only
     * be linked to a successor with eflag_use == 0 (see
     * latx_tb_eflag_link_allowed()), which is what makes the skip independent
     * of TB splits, CF_COUNT_MASK/icount and successor retranslation.
     */
    if (ret > 0 && !td->is_tu_mode && flag_reduction_tb_defer()) {
        __latxs_flag_reduction_tb_mode_parse_tb_use(tb);
    }

    /*
     * option sysflagtb + HAMT recovery: verify the generated recovery layout
     * before the TB is published.  The runtime handler trusts the marker
     * word and the TB metadata structurally; a layout mismatch would make an
     * in-flight deferred jump resume with stale flags, which is exactly the
     * failure mode this option must not introduce.  Fail fast instead
     * (verification-only configuration; a stock run never reaches this).
     */
    if (ret > 0 && !td->is_tu_mode && flag_reduction_tb_defer() &&
        hamt_enabled() && flag_reduction_tb_hamt_recover()) {
        int v = latx_sysflagtb_verify_tb(tb, ret);
        if (v) {
            fprintf(stderr, "[LATX-SYS] sysflagtb recovery layout invariant "
                    "failed for tb %p (pc=%#llx): %d\n",
                    tb, (unsigned long long)tb->pc, v);
            abort();
        }
    }

    return ret;
}

/* tb_add_jump() guard for option sysflagtb.  Edge @n of @tb skipped its
 * M-EFLAGS materialisation on the direct-link fast path, so the link may only
 * be installed when @next_tb redefines all six flags before reading any:
 * then the successor restores the architectural flag state internally, and a
 * later IRQ/exception exit from it observes correct flags.  When this returns
 * false the edge keeps its "B 2" fallback, which materialises the flags and
 * then leaves to C; a stale flag can never reach a direct link.  With the
 * option off no TB ever sets eflag_tb_skip, so the check is a single
 * always-false test and linking is byte-identical to before. */
bool latx_tb_eflag_link_allowed(TranslationBlock *tb, int n,
                                TranslationBlock *next_tb)
{
    /* Option off (the default): no TB ever has a skip bit, and this early
     * return also makes the check independent of ptn.pcoffset/payload
     * initialisation in allocation paths that predate the option. */
    if (!flag_reduction_tb_defer()) {
        return true;
    }
    if (!(TB_EFLAGTB_SKIP(tb) & (1u << n))) {
        return true;
    }
    if (hamt_enabled()) {
        /* Deferred HAMT edges require a one-word B: the generic long link
         * is installed with two independent writes and cannot carry this
         * protocol safely across concurrent relinking. */
        intptr_t delta = (intptr_t)((uintptr_t)next_tb->tc.ptr -
                         ((uintptr_t)tb->tc.ptr + tb->jmp_insn_offset[n]));
        if ((delta & 3) || delta < -(INT64_C(1) << 27) ||
            delta >= (INT64_C(1) << 27)) {
            return false;
        }
    }
    if (!TB_EFLAGTB_USE_VALID(next_tb)) {
        qatomic_fetch_add(&latx_tb_eflag_stats[LATX_TB_EFLAG_STAT_VETO_UNPUB], 1);
        return false;
    }
    if (TB_EFLAGTB_USE(next_tb) != 0) {
        qatomic_fetch_add(&latx_tb_eflag_stats[LATX_TB_EFLAG_STAT_VETO_USE], 1);
        return false;
    }
    return true;
}

/*
 * option sysflagtb runtime invariants.  Verification-only state: every entry
 * point returns immediately when the option is off (the default), so the
 * stock TB-link path keeps its cost and behaviour.
 */
uint64_t latx_tb_eflag_stats[LATX_TB_EFLAG_STAT_NR];
static bool latx_tb_eflag_stat_materialise_on;
static gsize latx_tb_eflag_stat_inited;

uint64_t *latx_tb_eflag_stat_materialise_counter(void)
{
    return latx_tb_eflag_stat_materialise_on ?
        &latx_tb_eflag_stats[LATX_TB_EFLAG_STAT_MATERIALISE] : NULL;
}

void latx_tb_eflag_stat_defer(int edge_index)
{
    qatomic_fetch_add(&latx_tb_eflag_stats[LATX_TB_EFLAG_STAT_DEFER_EDGE], 1);
    if (edge_index == 0) {
        qatomic_fetch_add(&latx_tb_eflag_stats[LATX_TB_EFLAG_STAT_DEFER_TB], 1);
    }
}

void latx_tb_eflag_stat_link(TranslationBlock *tb, int n,
                             TranslationBlock *next_tb)
{
    if (!flag_reduction_tb_defer() ||
        !(TB_EFLAGTB_SKIP(tb) & (1u << n))) {
        return;
    }
    /* Invariant (b): a skip edge may only be linked to a successor whose
     * published answer says "redefines all six flags before reading any".
     * latx_tb_eflag_link_allowed() enforces this before the jmp_dest CAS;
     * this check re-verifies the post-link state, so any bookkeeping mistake
     * in the link path (e.g. patching a slot after a refused claim) is fatal
     * instead of silently corrupting guest flags.  Deliberately not
     * lsassertm(): that is compiled out unless CONFIG_LATX_DEBUG, and the
     * verification must work in the release candidate build.  Only reached
     * when the option is enabled (skip bit set), default off. */
    if (unlikely(!TB_EFLAGTB_USE_VALID(next_tb) ||
                 TB_EFLAGTB_USE(next_tb) != 0)) {
        fprintf(stderr, "[LATX-SYS] sysflagtb invariant failed: skip edge %d "
                "of tb %p linked to tb %p with eflag_use=%#x valid=%d\n",
                n, tb, next_tb, TB_EFLAGTB_USE(next_tb),
                TB_EFLAGTB_USE_VALID(next_tb));
        abort();
    }
    qatomic_fetch_add(&latx_tb_eflag_stats[LATX_TB_EFLAG_STAT_LINK], 1);
}

static void latx_tb_eflag_stat_report(Notifier *notifier, void *opaque)
{
    fprintf(stderr,
            "[LATX-SYS] sysflagtb stats: defer_tb=%" PRIu64
            " defer_edge=%" PRIu64 " link=%" PRIu64
            " veto_use=%" PRIu64 " veto_unpub=%" PRIu64
            " materialise=%" PRIu64 " recovered=%" PRIu64
            " recover_reject=%" PRIu64 " (%s)\n",
            qatomic_read(&latx_tb_eflag_stats[LATX_TB_EFLAG_STAT_DEFER_TB]),
            qatomic_read(&latx_tb_eflag_stats[LATX_TB_EFLAG_STAT_DEFER_EDGE]),
            qatomic_read(&latx_tb_eflag_stats[LATX_TB_EFLAG_STAT_LINK]),
            qatomic_read(&latx_tb_eflag_stats[LATX_TB_EFLAG_STAT_VETO_USE]),
            qatomic_read(&latx_tb_eflag_stats[LATX_TB_EFLAG_STAT_VETO_UNPUB]),
            qatomic_read(&latx_tb_eflag_stats[LATX_TB_EFLAG_STAT_MATERIALISE]),
            qatomic_read(&latx_tb_eflag_stats[LATX_TB_EFLAG_STAT_RECOVERED]),
            qatomic_read(&latx_tb_eflag_stats[
                LATX_TB_EFLAG_STAT_RECOVER_REJECT]),
            latx_tb_eflag_stat_materialise_on ?
                "materialisation counter on" : "materialisation counter off");
}

static Notifier latx_tb_eflag_stat_notifier = {
    .notify = latx_tb_eflag_stat_report,
};

/* Called on the first ordinary translation when the option is enabled. */
static void latx_tb_eflag_stat_init(void)
{
    if (g_once_init_enter(&latx_tb_eflag_stat_inited)) {
        /* Publish the counter mode and register the notifier once, even when
         * multiple vCPUs translate their first TB concurrently. */
        latx_tb_eflag_stat_materialise_on =
            getenv("LATS_SYSFLAGTB_STAT") != NULL;
        qemu_add_exit_notifier(&latx_tb_eflag_stat_notifier);
        g_once_init_leave(&latx_tb_eflag_stat_inited, 1);
    }
}

void latxs_fix_after_excp_or_int(CPUState *cpu)
{
    CPUX86State *env = cpu->env_ptr;

    latxs_tb_trace(env, cpu->curr_exec_tb, 1);

    lsassertm(env->is_fcsr_simd == 0, "fcsr simd %s\n", __func__);

#ifdef CONFIG_LATX_OPT_NOFP
    if (!env->native_recovered) {
        __nofp_sync_xmm_to_env(cpu);
        __nofp_sync_fpr_to_env(cpu, 0);
    }
#endif
    env->native_recovered = false;
}

/* This is used in target/i386/fpu_helper.c */
int latxs_get_top_bias_from_env(CPUX86State *env)
{
    return (int)env->vregs[3];
}

void *latx_dump_hamt_context(void)
{
    CPUX86State *env = lsenv->cpu_state;

    printf("-------- LATX-SYS ----------------------\n");
    printf("----------------------------------------\n");
    printf("         ENV %p\n", env);
    printf("         CPL %d\n", env->hflags & 0x3);
    printf("----------------------------------------\n");
    if (qemu_tcg_mtcc_enabled()) {
        for (int i = 0; i < MTCC_NR; ++i) {
            printf(" Code Cache %d Base %p\n",
                    i, tcg_ctx_mtcc[i]->code_gen_buffer);
        }
    } else {
        printf(" Code Cache Base %p\n",
                tcg_ctx->code_gen_buffer);
    }
    printf("----------------------------------------\n");

    return env;
}
