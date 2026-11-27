/*
 * SPDX-FileCopyrightText: 2024-2026 LATS Project Authors
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "common.h"
#include "cpu.h"
#include "lsenv.h"
#include "reg-alloc.h"
#include "latx-options.h"
#include "latx-tu.h"
#include "latx-ras.h"
#include "translate.h"
#include "latx-static-codes.h"
#include "latx-intb-sys.h"
#include "latx-helper.h"
#include "latx-misc.h"
#include "latx-cp-counter.h"
#include "latx-shadow-htable.h"
#include "latx-sys-hamt-dmw.h"
#include "latx-sys-smc-check.h"
#include "latx-config.h"

/*
 * option sysflagtb verification (option-gated: the checks below are only
 * reached when the option is enabled, which is off by default).  This uses a
 * plain always-compiled check instead of lsassertm() because lsassertm() is
 * compiled out unless CONFIG_LATX_DEBUG is defined, and the verification must
 * also fire in the release build used by the single consolidated VM round.
 * A violation is a correctness bug (stale M-EFLAGS or a missing slow path),
 * so fail fast instead of corrupting guest state silently.
 */
#define SYSFLAGTB_CHECK(cond, ...)                                       \
    do {                                                                 \
        if (unlikely(!(cond))) {                                         \
            fprintf(stderr, "[LATX-SYS] sysflagtb invariant failed at "  \
                    "%s:%d: ", __func__, __LINE__);                      \
            fprintf(stderr, __VA_ARGS__);                                \
            abort();                                                     \
        }                                                                \
    } while (0)
#include "latx-shbr.h"
#include "sys-aot.h"
#include "tcg/tcg-split-tb.h"
#include "ir2.h"
#include "qemu/cacheinfo.h"

#define pc_in_same_page(a, b, pm) ((((a)^(b))&(pm))==0)
#include "qemu/cacheflush.h"
#include "mem.h"
#include "exec/exec-all.h"

int latxs_ir1_is_cross_page(IR1_INST *ir1)
{
    ADDRX pc_base = ir1_addr(ir1);
    ADDRX pc_next = ir1_addr_next(ir1) - 1;
    return !pc_in_same_page(pc_base, pc_next, TARGET_PAGE_MASK);
}

int latxs_ir1_is_cross_page_eip(IR1_INST *ir1, uint64_t csbase)
{
    ADDRX pc_base = ir1_addr(ir1) + csbase;
    ADDRX pc_next = ir1_addr_next(ir1) - 1 + csbase;
    return !pc_in_same_page(pc_base, pc_next, TARGET_PAGE_MASK);
}

static int latxs_is_system_eob(TRANSLATION_DATA *td, IR1_INST *ir1)
{
    ADDRX pc_first = td->sys.pc;
    ADDRX pc_next  = ir1_addr_next(ir1) + td->sys.cs_base;

    if (unlikely(td->sys.tf)) {
        td->can_link = 0;
        td->can_indir_link = 0;
        return 1;
    } else if (unlikely(inhibit_irq_hflags() &&
                        (td->sys.flags & HF_INHIBIT_IRQ_MASK))) {
        td->sys_eob_can_link = 1;
        return 1;
    } else if (unlikely(td->sys.cflags & CF_USE_ICOUNT)) {
        /* string instructions are eob in icount mode */
        if (latxs_ir1_is_string_op(ir1)) {
            /* string in/out are included */
            if (latxs_ir1_has_prefix_rep(ir1) ||
                latxs_ir1_has_prefix_repe(ir1) ||
                latxs_ir1_has_prefix_repne(ir1))
            {
                return 1;
            }
        }
        /* io instructions is eob in icount mode */
        switch (ir1_opcode(ir1)) {
        case X86_INS_RDTSC:
        case X86_INS_RDTSCP:

        case X86_INS_IN:
        case X86_INS_INSB:
        case X86_INS_INSD:
        case X86_INS_INSW:
        case X86_INS_OUT:
        case X86_INS_OUTSB:
        case X86_INS_OUTSD:
        case X86_INS_OUTSW:

            td->sys_eob_can_link = 1;
            return 1;
        default:
            break;
        }

        if (latxs_ir1_is_cross_page_eip(ir1, td->sys.cs_base)) {
            td->sys_eob_can_link = 1;
            td->sys_eob_is_indir = 1;
            return 1;
        }

        if (!pc_in_same_page(pc_first, pc_next, TARGET_PAGE_MASK)) {
            td->sys_eob_can_link = 1;
            td->sys_eob_is_indir = 1;
            return 1;
        }

        return 0;

    } else if (unlikely(!pc_in_same_page(pc_first, pc_next, TARGET_PAGE_MASK))) {
#ifdef CONFIG_SOFTMMU
        td->sys_eob_use_fsht = 1;
        return 1;
#else
        return 0;
#endif
    } else if (unlikely(option_split_strins) && latxs_ir1_is_string_op(ir1)) {
        /* string in/out are included */
        if (latxs_ir1_has_prefix_rep(ir1) ||
            latxs_ir1_has_prefix_repe(ir1) ||
            latxs_ir1_has_prefix_repne(ir1))
        {
            return 1;
        }
    }

    return 0;
}

static int latxs_ir1_is_hit_breakpoint(TRANSLATION_DATA *td, IR1_INST *ir1)
{
    CPUState *cpu = current_cpu;

    if (unlikely(cpu->singlestep_enabled)) {
        return 1;
    }

    if (unlikely(!QTAILQ_EMPTY(&cpu->breakpoints))) {
        target_ulong pc = (target_ulong)(ir1_addr(ir1) + td->sys.cs_base);
        CPUBreakpoint *bp;
        QTAILQ_FOREACH(bp, &cpu->breakpoints, entry) {
            if (bp->pc == pc) {
                td->sys.bp_hit = 1;
                return 1;
            }
        }
    }

    if (unlikely(cpu->watchpoint_hit)) {
        td->sys.bp_hit = 1;
        return 1;
    }

    return 0;
}

/* mov to ss */
bool latxs_ir1_is_mov_to_seg_ss(IR1_INST *ir1)
{
    return latxs_ir1_is_mov_to_seg(ir1) &&
        ir1_opnd_base_reg_num(ir1_get_opnd(ir1, 0)) == ss_index;
}

/* pop to ss */
bool latxs_ir1_is_pop_to_seg_ss(IR1_INST *ir1)
{
    return latxs_ir1_is_pop_to_seg(ir1) &&
        ir1_opnd_base_reg_num(ir1_get_opnd(ir1, 0)) == ss_index;
}

/* These instructions can change the floating-point TB lookup flags. */
static bool changes_fp_lookup_mode(IR1_INST *ir1)
{
    switch (ir1_opcode(ir1)) {
    case X86_INS_LDMXCSR:
    case X86_INS_FXRSTOR:
    case X86_INS_FXRSTOR64:
    case X86_INS_XRSTOR:
    case X86_INS_XRSTOR64:
        return true;
    default:
        return false;
    }
}

int latxs_ir1_is_eob_in_sys(IR1_INST *ir1)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    bool is_pause_eob = false;

    switch (ir1_opcode(ir1)) {
    case X86_INS_MOV: {
        IR1_OPND *op0 = ir1_get_opnd(ir1, 0);
        if (unlikely(td->sys.cflags & CF_USE_ICOUNT)) {
            IR1_OPND *op1 = ir1_get_opnd(ir1, 1);
            if (op1->type == X86_OP_REG &&
                (unsigned)(op1->reg - X86_REG_CR0) <=
                (X86_REG_CR15 - X86_REG_CR0)) {
                td->sys_eob_can_link = 1;
                return 1;
            }
        } else if (op0->type == X86_OP_REG &&
                   unlikely(op0->reg == X86_REG_CR8)) {
            td->sys_eob_can_link = 1;
            return 1;
        }
        if (op0->type == X86_OP_REG) {
            x86_reg reg = op0->reg;
            if (unlikely((unsigned)(reg - X86_REG_CR0) <=
                         (X86_REG_DR15 - X86_REG_CR0))) {
                return 1;
            }
            if (unlikely(reg == X86_REG_SS)) {
                if (inhibit_irq_hflags()) {
                    return 1;
                }
            } else if (unlikely(td->sys.code32) &&
                       (reg == X86_REG_ES || reg == X86_REG_CS ||
                        reg == X86_REG_DS) &&
                       td->sys.pe && !td->sys.vm86) {
                return 1;
            }
        }
        break;
    }
    case X86_INS_POP: {
        IR1_OPND *op0 = ir1_get_opnd(ir1, 0);
        if (op0->type == X86_OP_REG) {
            x86_reg reg = op0->reg;
            if (unlikely(reg == X86_REG_SS)) {
                if (inhibit_irq_hflags()) {
                    return 1;
                }
            } else if (unlikely(td->sys.code32) &&
                       (reg == X86_REG_ES || reg == X86_REG_DS) &&
                       td->sys.pe && !td->sys.vm86) {
                return 1;
            }
        }
        break;
    }
    case X86_INS_STI:
        if (inhibit_irq_hflags()) {
            return 1;
        }
        break;
    case X86_INS_NOP:
        if (!option_pause && ir1->info->bytes[0] == X86_PREFIX_REPE) {
            is_pause_eob = true;
        }
        break;
    case X86_INS_PAUSE:
        if (!option_pause) {
            is_pause_eob = true;
        }
        break;
    case X86_INS_LDS:
    case X86_INS_LES:
        if (td->sys.pe && !td->sys.vm86 && td->sys.code32) {
            return 1;
        }
        break;
    case X86_INS_POPF:
    case X86_INS_POPFD:
    case X86_INS_POPFQ:
    case X86_INS_SYSENTER:
    case X86_INS_SYSEXIT:
    case X86_INS_SYSCALL:
    case X86_INS_SYSRET:
    case X86_INS_RSM:
    case X86_INS_IRET:
    case X86_INS_IRETD:
    case X86_INS_IRETQ:
    case X86_INS_LMSW:
    case X86_INS_INVPCID:
    /* These instructions can change the floating-point TB lookup flags. */
    case X86_INS_LDMXCSR:
    case X86_INS_FXRSTOR:
    case X86_INS_FXRSTOR64:
    case X86_INS_XRSTOR:
    case X86_INS_XRSTOR64:
    case X86_INS_WAIT:
    case X86_INS_RETF:
    case X86_INS_RETFQ:
    case X86_INS_LCALL:
    case X86_INS_LJMP:
    case X86_INS_CLTS:
    case X86_INS_INVLPG:
    case X86_INS_INVLPGA:
    case X86_INS_LSS:
    case X86_INS_FNSAVE:
    case X86_INS_XSETBV:
    case X86_INS_MWAIT:
    case X86_INS_VMRUN:
    case X86_INS_STGI:
    case X86_INS_WRMSR:
    case X86_INS_CLAC:
    case X86_INS_STAC:
        return 1;
    default:
        break;
    }

    return latxs_is_system_eob(td, ir1) ||
           is_pause_eob ||
           latxs_ir1_is_hit_breakpoint(td, ir1) ||
           latxs_ir1_is_port_io(ir1);
}

void latxs_tr_gen_eob(void)
{
    TRANSLATION_DATA *td = lsenv->tr_data;

    if (!td->need_eob) {
        return;
    }

    if (inhibit_irq_hflags()) {
        if (td->inhibit_irq_in_hflags &&
            !(td->sys.flags & HF_INHIBIT_IRQ_MASK)) {
            latxs_tr_gen_hflags_set(HF_INHIBIT_IRQ_MASK);
        } else {
            latxs_tr_gen_hflags_clr(HF_INHIBIT_IRQ_MASK);
        }
    }
}

static void latxs_tr_gen_exit_tb_update_eip(
        int ignore_eip, ADDRX eip, int opnd_size)
{
    if (!ignore_eip) {
        assert(option_tb_pcrel == 0);
        assert(option_page_pcrel == 0);
        IR2_OPND eip_opnd = latxs_ra_alloc_itemp();
        if (!option_sys_aot) {
            switch (opnd_size) {
                case 32:
                    latxs_load_imm32_to_ir2(&eip_opnd, eip, EXMode_Z);
                    break;
                case 16:
                    latxs_load_imm32_to_ir2(&eip_opnd, eip & 0xffff, EXMode_Z);
                    break;
                case 64:
                    latxs_load_imm64_to_ir2(&eip_opnd, eip);
                    break;
                default:
                    lsassert(0);
                    break;
            }
        } else {
            aot_load_guest_addr_to_ir2(&eip_opnd, eip, LOAD_GUEST_ADDR);
        }

        latxs_tr_gen_store_to_curr_eip(&eip_opnd);

        latxs_ra_free_temp(&eip_opnd);
    }

    /* should correctly set env->eip */
    if (lsenv->tr_data->sys.tf) {
        latxs_tr_gen_call_to_helper1_cfg((ADDR)helper_single_step,
                default_helper_cfg, HELPER_SINGLE_STEP);
    }
}

void latxs_tr_gen_popf_no_link(IR1_INST *pir1)
{
    latxs_tr_shbr_restore_zero64(pir1->shbr_restore_zero64_after);
#ifdef CONFIG_LATX_PROFILER
    TRANSLATION_DATA *td = lsenv->tr_data;

    if (td->is_tu_mode) {
        add_tu_profiler(td->curr_tb);
    } else {
        la_out1(LISA_BL, &td->tbprofile_rdtime_label);
    }
#endif
    latxs_tr_gen_eob();
    latxs_tr_gen_exit_tb_update_eip(0, ir1_addr_next(pir1), 64);
    latxs_tr_gen_exit_tb_j_context_switch(NULL, 0, 0);
}

static int cpu_hit_breakpoint(void)
{
    CPUState *cpu = env_cpu(lsenv->cpu_state);
    TRANSLATION_DATA *td = lsenv->tr_data;
    if (td->sys.bp_hit || cpu->singlestep_enabled) {
        lsassert(cpu == current_cpu);
        current_cpu->exception_index = EXCP_DEBUG;
        return 1;
    }
    return 0;
}

typedef struct SysEOBContext {
    TRANSLATION_DATA *td;
    TranslationBlock *tb;
    int irqcheck_mode;
    bool can_link;
    bool link_indir;
} SysEOBContext;

/* 0 : do not generate irq check */
#define SYSEOB_IRQCHECK_NONE    0
/* 1 : do irq check on cpu.neg.icount_decr.u32 */
#define SYSEOB_IRQCHECK_NORMAL  1
/* 2 : also do irq check on cpu.interrupt_request */
#define SYSEOB_IRQCHECK_REQ     2

static int latxs_sys_eob_check_common(IR1_INST *pir1)
{
    if (!pir1) { return 1; }
    /* If pir1 is already a normal TB-end instruction,
     * no need another exit codes. */
    if (ir1_is_tb_ending(pir1)) { return 1; }
    return 0;
}

static int latxs_sys_eob_check(SysEOBContext *ctx, IR1_INST *pir1)
{
    if (latxs_sys_eob_check_common(pir1)) return 1;
    if (option_split_strins && latxs_ir1_is_string_op(pir1)) {
        if (latxs_ir1_has_prefix_rep(pir1) ||
            latxs_ir1_has_prefix_repe(pir1) ||
            latxs_ir1_has_prefix_repne(pir1))
        {
            return 1;
        }
    }
    lsassertm(!lsenv->tr_data->tb_too_large_pir1,
            "system EOB should not exist with TB too large.\n");
    return 0;
}

static void latxs_sys_eob_prepare(SysEOBContext *ctx, IR1_INST *pir1)
{
    if (latxs_ir1_is_port_io(pir1)) {
        /* A port access is a device boundary.  Reuse the existing EOB
         * interrupt check so an async exit request (e.g. SeaBIOS writes
         * 0xCF9 and then falls through to int3) or a pending interrupt is
         * taken before the next guest instruction; a clean access still
         * links to the next TB. */
        ctx->irqcheck_mode = SYSEOB_IRQCHECK_REQ;
        return;
    }
    if (latxs_check_interrupt_at_block_end() &&
        (latxs_ir1_is_mov_to_cr(pir1) || latxs_ir1_is_mov_to_dr(pir1))) {
        ctx->irqcheck_mode = SYSEOB_IRQCHECK_NORMAL;
    }
    if ((inhibit_irq_hflags() && ctx->td->inhibit_irq_in_hflags)) {
        /* sti, write ss */
        if (inhibit_irq_hflags_link()) {
            ctx->can_link = true;
            ctx->link_indir = inhibit_irq_hflags_link_indir();
        } else {
            ctx->irqcheck_mode = SYSEOB_IRQCHECK_NONE;
            ctx->can_link = false;
        }
    }
    if ((inhibit_irq_hflags() && ctx->td->sys.flags & HF_INHIBIT_IRQ_MASK)) {
        /* the next inst after sti or write ss */
        if (inhibit_irq_hflags_link()) {
            ctx->irqcheck_mode = SYSEOB_IRQCHECK_REQ;
            ctx->can_link = true;
            ctx->link_indir = inhibit_irq_hflags_link_indir();
        } else {
            ctx->irqcheck_mode = SYSEOB_IRQCHECK_NONE;
            ctx->can_link = false;
        }
    }
    /* A pending IRQ may have outlived its notification. Only state changes
     * which can reopen delivery need an explicit request check before link. */
    if (ctx->can_link &&
        latxs_check_interrupt_at_block_end() &&
        (latxs_ir1_is_pop_eflags(pir1) || latxs_ir1_is_iret(pir1) ||
         latxs_ir1_is_sysret(pir1) || latxs_ir1_is_stgi(pir1) ||
         latxs_ir1_is_rsm(pir1))) {
        ctx->irqcheck_mode = SYSEOB_IRQCHECK_REQ;
    }
    if (changes_fp_lookup_mode(pir1)) {
        /* Return predictions only compare PCs. Discard them and any suspended
         * LLVM frames before looking up the next PC with the updated flags. */
        ctx->can_link = false;
        ctx->link_indir = false;
        latxs_ras_tp_emit_clear();
    }

}

static void latxs_sys_eob_update_eip(SysEOBContext *ctx, IR1_INST *pir1)
{
    ADDRX next_eip = ir1_addr_next(pir1);
    if (option_lazypc && !ctx->link_indir) {
        ctx->tb->next_pc[0] = next_eip;
    }
    /* SYSCALL applies FMASK before the single-step trap is tested. */
    if (ctx->td->sys.tf && ir1_opcode(pir1) == X86_INS_SYSCALL) {
        IR2_OPND flags = latxs_ra_alloc_itemp();
        IR2_OPND done = latxs_ir2_opnd_new_label();

        la_out2i(LISA_LD_WU, &flags, &la_env, offsetof(CPUX86State, eflags));
        la_out2i(LISA_ANDI, &flags, &flags, TF_MASK);
        la_out2(LISA_BEQZ, &flags, &done);
        latxs_ra_free_temp(&flags);
        latxs_tr_gen_call_to_helper1_cfg((ADDR)helper_single_step,
                default_helper_cfg, HELPER_SINGLE_STEP);
        latxs_append_ir2_opnd1(LISA_LABEL, &done);
        return;
    }
    latxs_tr_gen_exit_tb_update_eip(ctx->td->ignore_eip_update, next_eip, 64);
}

static void latxs_sys_eob_emit_intcheck(SysEOBContext *ctx)
{
    if (ctx->irqcheck_mode) {
        latxs_load_intcheck_to_reg(&la_a3);
        la_out3(LISA_BLT, &la_a3, &la_zero,
                latxs_ir2_interrupt_get_label(ctx->tb, 0));
        if (ctx->irqcheck_mode == SYSEOB_IRQCHECK_REQ) {
            la_out2i(LISA_LD_D, &la_a3, &la_env, offsetof(CPUX86State, interrupt_request_ptr));
            if (hamt_enabled()) latxs_tr_dmw_in_fix_mem(&la_a3);
            la_out2i(LISA_LD_WU, &la_a3, &la_a3, 0);
            la_out2 (LISA_BNEZ, &la_a3, latxs_ir2_interrupt_get_label(ctx->tb, 0));
        }
    }
}

static void latxs_sys_eob_emit_intcheck_exit(SysEOBContext *ctx,
        IR2_OPND *tbptr, int link)
{
    if (ctx->irqcheck_mode) {
        latxs_ir2_interrupt_set_label(ctx->tb, 0);
    }
    if (link) {
        latxs_tr_gen_exit_tb_load_tb_addr(tbptr, (ADDR)ctx->tb);
    }
    latxs_tr_gen_exit_tb_j_context_switch(tbptr, link, 0);
}

static void latxs_tr_emit_inline_njc_lookup(TranslationBlock *tb, bool call_target)
{
    bool compact;

    if (!intb_njc_inline_enabled()) {
        return;
    }
    compact = latx_jmp_cache_compact_enabled() &&
              (tb->flags & HF_CS64_MASK);
    gen_latxs_intb_lookup_njc_inline_target(compact, option_njc_layout,
                                            call_target);
}

static void latxs_sys_eob_emit_exit(SysEOBContext *ctx)
{
    IR2_OPND tbptr = latxs_ra_alloc_dbt_arg1(); /* a6($10) */
    bool emitted_exit_code = false;

    if (check_misc_flags(ctx->tb, MF_END_SYSCALL)) {
        /* syscall link handled in latxs_tb_set_jmp_target() */
        latxs_sys_eob_emit_intcheck_exit(ctx, &tbptr, 1);
        return;
    }

    if (ctx->can_link) {
        if (ctx->link_indir) {
            latxs_tr_gen_exit_tb_load_tb_addr(&tbptr, (ADDR)ctx->tb);
            latxs_tr_gen_load_from_curr_eip(&latxs_arg1_ir2_opnd);
#if defined(CONFIG_LATX_TU) && defined(CONFIG_SOFTMMU)
            if (aot_tu_enabled() && hamt_mode_dmw() && option_soft_ras &&
                option_rastoptp && option_int_opt == IRQ_CHECK_ASYNC_NOTIFY) {
                /* IRET and other state-changing exits can reach the NJC
                 * without visiting the C dispatcher. A suspended LLVM CALL
                 * owns a nonempty host frame; only the outer SP permits a
                 * fresh native entry. Compare normal aliases without changing
                 * the live SP or the target guest PC in a1. */
                IR2_OPND ordinary = la_new_label();
                size_t sp_offset = offsetof(CPUX86State, aot_native_outer_sp);
                g_assert(!(sp_offset & 3) && sp_offset <= 32764);
                /* The target state can differ from this TB after IRET.
                 * Kernel and compatibility targets retain their fast lookup. */
                la_out2i(LISA_LD_WU, &la_a2, &la_env, offsetof(CPUX86State, hflags));
                la_out2i(LISA_ANDI, &la_a3, &la_a2, HF_CPL_MASK);
                la_out2i(LISA_XORI, &la_a3, &la_a3, HF_CPL_MASK);
                la_out2(LISA_BNEZ, &la_a3, &ordinary);
                la_out2ii(LISA_BSTRPICK_D, &la_a3, &la_a2,
                        HF_CS64_SHIFT, HF_CS64_SHIFT);
                la_out2(LISA_BEQZ, &la_a3, &ordinary);
                la_out2ii(LISA_BSTRPICK_D, &la_a2, &la_a2,
                        HF_FORCE_TB_MODE_SHIFT, HF_FORCE_TB_MODE_SHIFT);
                la_out2(LISA_BNEZ, &la_a2, &ordinary);
                la_out2i(LISA_LDPTR_D, &la_a2, &la_env, sp_offset >> 2);
                la_out2i(LISA_LU52I_D, &la_a2, &la_a2, 0);
                la_out2i(LISA_LU52I_D, &la_a3, &la_sp, 0);
                la_out3(LISA_BNE, &la_a2, &la_a3, &ordinary);
                if (intb_njc_enabled() && latx_jmp_cache_compact_enabled()) {
                    /* Ordinary page crossings can reuse the validated NJC
                     * target. Native targets and misses still need resume's
                     * full state checks before creating a new LLVM frame. */
                    gen_latxs_intb_lookup_njc_inline(true, false);
                }
                tb_sc_table_offset_jmp(ctx->tb, ctx->td->mtcc_id,
                        intb_resume_lookup, ctx->td->tcg_region_offset,
                        ctx->td->ir2_asm_nr << 2, 0, INTB_RESUME_LOOKUP);
                la_label(ordinary);
            }
#endif
            latxs_tr_emit_inline_njc_lookup(ctx->tb, false);
            int offset = ctx->td->ir2_asm_nr << 2;
            int fid = fsht_flags_to_id(ctx->tb->flags);
            if (option_flaged_sht && fid >= 0 && ctx->td->sys_eob_use_fsht) {
                if (tucp_fast_fsht_check() &&
                    (ctx->tb->flags & HF_CPL_MASK) == HF_CPL_MASK) {
                    tb_sc_table_offset_jmp(ctx->tb, ctx->td->mtcc_id,
                            intb_fsht_lookup_tucpfast[fid],
                            ctx->td->tcg_region_offset, offset, 0,
                            INTB_FSHT_LOOKUP_TUCPFAST);
                } else {
                    tb_sc_table_offset_jmp(ctx->tb,
                            ctx->td->mtcc_id, intb_fsht_lookup[fid],
                            ctx->td->tcg_region_offset,
                            offset, 0, INTB_FSHT_LOOKUP);
                }
            } else {
                if (!intb_njc_inline_enabled() &&
                    latx_jmp_cache_compact_enabled() &&
                    (ctx->tb->flags & HF_CS64_MASK)) {
                    tb_sc_table_offset_jmp(ctx->tb, ctx->td->mtcc_id,
                            intb_lookup_compact, ctx->td->tcg_region_offset,
                            offset, 0, INTB_LOOKUP_COMPACT);
                } else {
                    tb_sc_table_offset_jmp(ctx->tb, ctx->td->mtcc_id,
                            intb_lookup, ctx->td->tcg_region_offset,
                            offset, 0, INTB_LOOKUP);
                }
            }
        } else {
            emitted_exit_code = true;
            latxs_sys_eob_emit_intcheck_exit(ctx, &tbptr, 1);
        }
    }
    if (!emitted_exit_code) {
        latxs_sys_eob_emit_intcheck_exit(ctx, &tbptr, 0);
    }
}

void latxs_tr_gen_sys_eob(IR1_INST *pir1)
{
    TRANSLATION_DATA *td = lsenv->tr_data;

    SysEOBContext ctx = {
        .td = td,
        .tb = td->curr_tb,
        .irqcheck_mode = SYSEOB_IRQCHECK_NONE,
        .can_link = td->can_link && td->sys_eob_can_link,
        .link_indir = td->sys_eob_is_indir,
    };

    if (latxs_sys_eob_check(&ctx, pir1)) return;

#ifdef CONFIG_LATX_PROFILER
    if (!ctx.td->is_tu_mode) { la_out1(LISA_BL, &(ctx.td->tbprofile_rdtime_label)); }
#endif

    latxs_tr_gen_eob(); /* inhibit irq hflags */

    latxs_sys_eob_prepare(&ctx, pir1);

    latxs_sys_eob_update_eip(&ctx, pir1);

    latxs_sys_eob_emit_intcheck(&ctx);

    if (ctx.can_link && !ctx.link_indir) {
        latxs_tr_gen_exit_tb_j_tb_link(ctx.tb, 0);
    }

    latxs_sys_eob_emit_exit(&ctx);
}

/* Need fix for aot. */
void latxs_tr_gen_exit_tb_load_tb_addr(IR2_OPND *tbptr, ADDR tb_addr)
{
    if (lsenv && lsenv->tr_data && lsenv->tr_data->curr_tb) {
        TranslationBlock *tb = lsenv->tr_data->curr_tb;
        ADDR code_ptr = (ADDR)tb->tc.ptr;
        ADDR code_off = (ADDR)(lsenv->tr_data->ir2_asm_nr << 2);
        if (option_sys_aot) {
            if (split_tb_base_ok()) {
                tb_addr &= (1ULL << 32) - 1;
            }
            latxs_load_addr_to_ir2(tbptr, tb_addr, LOAD_TB_ADDR);
            return;
        }

        ADDR ins_pc = code_ptr + code_off;
        /* tbptr = ins_pc + offset => offset = tbptr - ins_pc */
        int64_t offset = ((int64_t)tb_addr - (int64_t)ins_pc) >> 2;
        if (int32_in_int20(offset)) {
            latxs_append_ir2_opnd1i(LISA_PCADDI, tbptr, offset);
        } else {
            if (split_tb_base_ok()) {
                latxs_load_imm64_to_ir2(tbptr, tb_addr & ((1ULL << 32) - 1));
            } else {
                latxs_load_imm64_to_ir2(tbptr, tb_addr);
            }
        }
    } else {
        if (split_tb_base_ok()) {
            latxs_load_imm64_to_ir2(tbptr, tb_addr & ((1ULL << 32) - 1));
        } else {
            latxs_load_imm64_to_ir2(tbptr, tb_addr);
        }
    }
}

static void latxs_tb_link_align_inside_cacheline(TranslationBlock *tb)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    uint64_t tcptr = (uint64_t)tb->tc.ptr;
    tcptr += td->ir2_asm_nr << 2;
    assert(qemu_icache_linesize);
    if ((tcptr & (qemu_icache_linesize - 1)) ==
        (qemu_icache_linesize - 4))
    {
        la_out_nop();
    }
}

void latxs_tr_gen_exit_tb_j_tb_link(TranslationBlock *tb, int succ_id)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    bool recovery_marker = td->eflag_tb_defer.valid && hamt_enabled() &&
                           flag_reduction_tb_hamt_recover();
    IR2_OPND recovery;

    /* Keep the marker immediately before the slot.  Account for the extra
     * instruction when aligning the two patchable words. */
    if (recovery_marker) {
        uintptr_t slot = (uintptr_t)tb->tc.ptr +
                         ((td->ir2_asm_nr + 1) << 2);
        if ((slot & (qemu_icache_linesize - 1)) ==
            qemu_icache_linesize - 4) {
            la_out_nop();
        }
        recovery = latxs_ir2_opnd_new_label();
        latxs_append_ir2_opnd2(LISA_PCADDI, &la_ra, &recovery);
    }
    /* 0. make sure two inst not cross cacheline */
    latxs_tb_link_align_inside_cacheline(tb);

    /* 1. set a label for native code linkage */
    IR2_OPND goto_label_opnd = latxs_ir2_opnd_new_label();

    /* remember where the j instruction position */
    /* reuse jmp_reset_offset[], they will be rewritten in label_dispose */
    /* at that time we can calculate exact offset for this position */
    latxs_append_ir2_opnd1(LISA_LABEL, &goto_label_opnd);
    tb->jmp_reset_offset[succ_id] = goto_label_opnd.val;

    /* point to current j insn addr plus 8 by default, will resolve in */
    /* label_dispose, two instructions for pcaddu18i and jirl patch */
    /* patch may be pcaddu18i and jirl, this b should b to next insn of nop other than nop  */
    /* if nop was interrupt, and may just exec jirl without pcaddu18i  */
    latxs_append_ir2_opnda(LISA_B, 2);
    latxs_append_ir2_opnd0_(lisa_nop);
    if (recovery_marker) {
        latxs_append_ir2_opnd1(LISA_LABEL, &recovery);
    }
}

void latxs_tr_gen_exit_tb_j_context_switch(IR2_OPND *tbptr,
        int can_link, int succ_id)
{
    /* jump to context_switch_native_to_bt */
    TRANSLATION_DATA *td = lsenv->tr_data;
    TranslationBlock *tb = td->curr_tb;

    int offset = td->ir2_asm_nr << 2;

    uint32_t mtcc_id = td->mtcc_id;

    if (can_link) {
        if (succ_id) {
            tb_sc_table_offset_jmp(tb, mtcc_id, cs_native_to_bt_ret_n1,
                    td->tcg_region_offset, offset, 0, CS_NATIVE_TO_BT_RET_N1);
        } else {
            tb_sc_table_offset_jmp(tb, mtcc_id, cs_native_to_bt_ret_n0,
                    td->tcg_region_offset, offset, 0, CS_NATIVE_TO_BT_RET_N0);
        }
    } else {
        tb_sc_table_offset_jmp(tb, mtcc_id, cs_native_to_bt_ret_0,
                td->tcg_region_offset, offset, 0, CS_NATIVE_TO_BT_RET_0);
    }
}

void latxs_tr_gen_eob_if_tb_too_large(IR1_INST *pir1)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    TranslationBlock *tb = td->curr_tb;
    int can_link = option_tb_link && !(td->sys.cflags & CF_NO_GOTO_TB);
    IR2_OPND tbptr = latxs_ra_alloc_dbt_arg1();
    int succ_id = 1;

    if (latxs_sys_eob_check_common(pir1)) return ;
    lsassertm(!td->sys_eob_pir1,
            "TB too large should not exist with system EOB.\n");

#ifdef CONFIG_LATX_PROFILER
    if (!td->is_tu_mode) { la_out1(LISA_BL, &(td->tbprofile_rdtime_label)); }
#endif

    latxs_tr_gen_eob();
    if (cpu_hit_breakpoint()) { can_link = 0; }

    latxs_tr_gen_exit_tb_load_tb_addr(&tbptr, (ADDR)tb);

    if (can_link) {
        latxs_tr_gen_exit_tb_j_tb_link(tb, succ_id);
    }

    ADDRX next_eip = ir1_addr_next(pir1);
    if (option_lazypc) {
        tb->next_pc[succ_id] = next_eip;
    }

    if (!option_lazypc || !can_link) {
        latxs_tr_gen_exit_tb_update_eip(td->ignore_eip_update, next_eip, 64);
    }

    latxs_tr_gen_exit_tb_j_context_switch(&tbptr, can_link, succ_id);
}

#ifdef CONFIG_SOFTMMU
static int branch_is_cross_page(IR1_INST *branch, int n)
{
    ADDRX curr_eip = ir1_addr(branch);
    ADDRX next_eip = 0;

    if (ir1_is_call(branch) && !ir1_is_indirect_call(branch)) {
        next_eip = ir1_target_addr(branch);
        return (next_eip >> TARGET_PAGE_BITS) != (curr_eip >> TARGET_PAGE_BITS);
    }

    if (ir1_is_jump(branch) && !ir1_is_indirect_jmp(branch)) {
        next_eip = ir1_target_addr(branch);
        return (next_eip >> TARGET_PAGE_BITS) != (curr_eip >> TARGET_PAGE_BITS);
    }

    if (ir1_is_branch(branch)) {
        if (n == 0) next_eip = ir1_addr_next(branch);
        if (n == 1) next_eip = ir1_target_addr(branch);
        return (next_eip >> TARGET_PAGE_BITS) != (curr_eip >> TARGET_PAGE_BITS);
    }

    return 0;
}

static int branch_target_maybe_cross_page_tb(IR1_INST *branch, int n)
{
    /*ADDRX curr_eip = ir1_addr(branch);*/
    ADDRX next_eip = 0;
    ADDRX next_eip2 = 0;

    if (ir1_is_call(branch) && !ir1_is_indirect_call(branch)) {
        next_eip = ir1_target_addr(branch);
        next_eip2 = next_eip + 16;
        return (next_eip >> TARGET_PAGE_BITS) != (next_eip2 >> TARGET_PAGE_BITS);
    }

    if (ir1_is_jump(branch) && !ir1_is_indirect_jmp(branch)) {
        next_eip = ir1_target_addr(branch);
        next_eip2 = next_eip + 16;
        return (next_eip >> TARGET_PAGE_BITS) != (next_eip2 >> TARGET_PAGE_BITS);
    }

    if (ir1_is_branch(branch)) {
        if (n == 0) next_eip = ir1_addr_next(branch);
        if (n == 1) next_eip = ir1_target_addr(branch);
        next_eip2 = next_eip + 16;
        return (next_eip >> TARGET_PAGE_BITS) != (next_eip2 >> TARGET_PAGE_BITS);
    }

    return 0;
}
#endif

static void tr_gen_branch_save_next_eip(IR1_INST *branch, int n)
{
    int next_eip_size = latxs_ir1_addr_size(branch) << 3;

    ADDRX next_eip = 0;

    TRANSLATION_DATA *td = lsenv->tr_data;
    TranslationBlock *tb = td->curr_tb;

    int doit = 0;
    int ignore_eip = 0;

    if (ir1_is_call(branch) && !ir1_is_indirect_call(branch)) {
        next_eip = ir1_target_addr(branch);
        if (latxs_ir1_addr_size(branch) == 2 ||
            ir1_opnd_size(ir1_get_opnd(branch, 0)) == 16) {
            next_eip_size = 16;
        }
        doit = 1;
    }

    if (ir1_is_jump(branch) && !ir1_is_indirect_jmp(branch)) {
        next_eip = ir1_target_addr(branch);
        next_eip_size = ir1_opnd_size(ir1_get_opnd(branch, 0));
        ignore_eip = td->ignore_eip_update;
        doit = 1;
    }

    if (ir1_is_branch(branch)) {
        if (n == 0) next_eip = ir1_addr_next(branch);
        if (n == 1) next_eip = ir1_target_addr(branch);
        next_eip_size = ir1_opnd_size(ir1_get_opnd(branch, 0));
        doit = 1;
    }

    if (lsenv->tr_data->sys.code64) {
        next_eip_size = 64;
    }

    /* if pcrel, modify pc at end of tb*/
    if ((tb_cflags(tb) & CF_PCREL)) {
        ignore_eip = td->ignore_eip_update;
    }
    if (doit) {
        latxs_tr_gen_exit_tb_update_eip(ignore_eip, next_eip, next_eip_size);
    }
}

static void latxs_tr_pcrel_eip(IR1_INST *branch, int succ_id)
{
    int st_eip = 0;
    ADDRX next_eip = 0;
    TRANSLATION_DATA *td = lsenv->tr_data;
    int next_eip_size = latxs_ir1_addr_size(branch) << 3;
    IR1_OPCODE opcode = ir1_opcode(branch);

    if (ir1_is_branch(branch)) {
        if (succ_id == 0) next_eip = ir1_addr_next(branch);
        if (succ_id == 1) next_eip = ir1_target_addr(branch);
        next_eip_size = ir1_opnd_size(ir1_get_opnd(branch, 0));
        st_eip = 1;
    }
    if (opcode == X86_INS_RET) {
        // process at ret;
        st_eip = 2;
    }
    if (opcode == X86_INS_LJMP) {
        if (td->sys.pe && !td->sys.vm86) {
            // eip update at helper
        } else if (!ir1_opnd_is_mem(ir1_get_opnd(branch, 0))) {
            /* uptate for ljmp imm, mem seems no using */
            next_eip = ir1_opnd_uimm(ir1_get_opnd(branch, 1));
            next_eip_size = ir1_opnd_size(ir1_get_opnd(branch, 1));
            st_eip = 1;
        }
    }
    if (ir1_is_call(branch)) {
        if (ir1_is_indirect_call(branch)) {
            // process at call;
            st_eip = 2;
        } else {
            next_eip = ir1_target_addr(branch);
            if (latxs_ir1_addr_size(branch) == 2 ||
                ir1_opnd_size(ir1_get_opnd(branch, 0)) == 16) {
                next_eip_size = 16;
            }
            st_eip = 1;
        }
    }
    if (ir1_is_jump(branch)) {
        if (ir1_is_indirect_jmp(branch)) {
            // process at jmpin;
            st_eip = 2;
        } else {
            next_eip = ir1_target_addr(branch);
            next_eip_size = ir1_opnd_size(ir1_get_opnd(branch, 0));
            st_eip = 1;
        }
    }

    /*
    * update neip for test
    * for sys.32, cs_base need to be added for pc_save contains cs_base
    */
    switch (next_eip_size) {
    case 64:
        break;
    case 32:
        next_eip &= 0xffffffff;
        break;
    case 16:
        next_eip &= 0xffff;
        break;
    default:
        break;
    }

    int eip_offset = 0;
    if (option_tb_pcrel)
        eip_offset = next_eip - (td->pc_save - td->sys.cs_base);
    else if (option_page_pcrel)
        /* here assume pc_save must not cross page */
        eip_offset = next_eip - (td->curr_page - td->sys.cs_base);
    else
        assert(0);
    /* check during trans, it must be correct
     * however, during running, it might not equal
     */

    if (st_eip) {
        if (st_eip == 1) {
            IR2_OPND value_opnd = latxs_ra_alloc_itemp();
            // cal target eip
            latxs_tr_gen_add_st_curr_eip(&value_opnd, eip_offset, 0);
            latxs_ra_free_temp(&value_opnd);
        } else if (st_eip == 2){
            // do null
        }
        td->ignore_eip_update = 1;
    }
}

#ifdef CONFIG_SOFTMMU

#define CP_CHECK_PREPARE_DATA(tb, td)                    \
    int cpl = tb->flags & 0x3;                           \
    int canptw = (tb->flags & HF_HAMT_EN_MASK) ? 1 : 0;  \
    if (!option_fast_itlb_ptw) canptw = 0;               \
    int offset = td->ir2_asm_nr << 2;                    \
    int mtcc_id = td->mtcc_id;

static inline void tb_gen_cpu_neg_itlb(TranslationBlock *tb, uint64_t addr)
{
    if (option_pcid_cache) {
        int root = (tb->flags & HF_CPL_MASK) ?
            offsetof(CPUX86State, pcid_itlb_user) :
            offsetof(CPUX86State, pcid_itlb_kernel);
        la_out2i(LISA_LD_D, &la_a0, &la_env, root);
        if (option_aot_save) {
            int tc_offset = lsenv->tr_data->ir2_asm_nr << 2;
            add_curr_rel_entry(tb, LOAD_BY_GUEST_ADDR_CPU_NEG_ITLB,
                    tc_offset, 1, -1, addr - tb->pc);
        }
        la_out2i(LISA_LDPTR_D, &la_a0, &la_a0,
                 FAST_ITLB_INDEX(addr) * sizeof(fast_itlb_entry_t) / 4);
        return;
    }

    int off = 0;
    if ((tb->flags & HF_CPL_MASK)) {
        off = (int)offsetof(ArchCPU, neg.itlb_user) -
              (int)offsetof(ArchCPU, env);
    } else {
        off = (int)offsetof(ArchCPU, neg.itlb_knosmap) -
              (int)offsetof(ArchCPU, env);
    }
    off += (FAST_ITLB_INDEX(addr)) * sizeof(fast_itlb_entry_t);
    off = off >> 2;

    IR2_OPND itlb = la_a0;
    if (int32_in_int14(off)) {
        if (option_aot_save) {
            int tc_offset = lsenv->tr_data->ir2_asm_nr << 2;
            add_curr_rel_entry(tb, LOAD_BY_GUEST_ADDR_CPU_NEG_ITLB,
                    tc_offset, 1, -1, addr - tb->pc);
        }
        la_out2i(LISA_LDPTR_D, &itlb, &la_env, off);
    } else {
#ifdef CONFIG_LATX_DEBUG
        printf("%s:%d TB %p addr %lx\n", __func__, __LINE__, tb, addr);
        printf("%s:%d offset %x > 14 bits\n", __func__, __LINE__, off);
#endif
        IR2_OPND tmp = la_a1;
        la_imm(&tmp, (int64_t)off);
        la_out3(LISA_LDX_D, &itlb, &la_env, &tmp);
    }
}

static inline uint64_t latxs_tr_exit_tb_succ_pc(IR1_INST *branch, int succ_id)
{
    if (succ_id || ir1_is_call(branch)) {
        return ir1_target_addr(branch);
    }
    return ir1_addr_next(branch);
}

/*
 * option sysflagtb -- TB-mode cross-TB dead-flag elimination.
 *
 * The fused compare+branch translators emit their M-EFLAGS producer
 * (x86sub/x86add/x86and/x86or/x86dec) right before the native branch, which
 * means every execution pays one serialised LBT flag write although neither
 * successor reads the incoming flags.  When the guest instruction streams at
 * BOTH successor PCs redefine all six OSZAPC flags before reading any flag
 * (tb_disasm_eflag_use_zero(), a property of the guest bytes and therefore
 * stable across TB retranslation), the producer is deferred to the exit slow
 * paths of the two edges:
 *
 *   - a taken direct link skips it entirely (no EFLAGS observer),
 *   - link miss / IRQ-check / context-switch exits execute it in
 *     latxs_tr_tb_eflag_emit_deferred() below, before any C code runs.
 *
 * A direct link is only installed when the successor TB itself redefines all
 * flags before reading any (tb_add_jump() veto), which is what makes this
 * safe against TB splits / CF_COUNT_MASK / icount: if the successor turned
 * out not to contain the redefinition, the fallback path materialises and the
 * edge stays unlinked.
 */
bool latxs_tr_tb_eflag_defer_begin(IR1_INST *branch, int opcode,
        IR2_OPND *opnd0, IR2_OPND *opnd1, int n_operands)
{
#ifdef CONFIG_LATX_SMC_INST_SELF_CHECK
    /* The self-check executes a potentially faulting code-page read before
     * the successor's first guest instruction. */
    return false;
#endif
    TRANSLATION_DATA *td = lsenv->tr_data;
    TranslationBlock *tb = td->curr_tb;

    if (!flag_reduction_tb_defer() || !tb || !ir1_is_branch(branch)) {
        return false;
    }
    if (td->is_tu_mode || in_aot_translate || option_sys_aot) {
        return false;
    }
    /*
     * The deferred producer is TB-final: both callers of
     * latxs_tr_generate_exit_tb() below must be this branch's two edges, so
     * the branch has to be the last instruction of the TB.  A fused
     * translator that passes its producer instruction instead of the branch
     * fails here (and would otherwise never defer).
     */
    SYSFLAGTB_CHECK(branch == td->ir1_inst_array + tb->icount - 1,
              "sysflagtb: defer branch %p is not the TB-final IR1\n",
              branch);
    if (!td->can_link || td->eflag_tb_defer.valid) {
        return false;
    }
    if ((tb->flags & HF_CPL_MASK) != HF_CPL_MASK) {
        return false;
    }
    if (td->sys.tf || (td->sys.cflags & (CF_USE_ICOUNT | CF_NO_GOTO_TB |
                                         CF_NO_GOTO_PTR | CF_SINGLE_STEP))) {
        return false;
    }
    /* Only block-end checking observes an IRQ before the link slot, whose
     * IRQ path materialises the deferred flags.  Block-start/no-opt may
     * return to C at the successor entry, before its first instruction;
     * async notification can also expose the stale flags. */
    if (option_int_opt != IRQ_CHECK_BLOCK_END) {
        return false;
    }
    if (tucp_fast_link_check()) {
        return false;
    }
#ifdef CONFIG_SOFTMMU
    /* The plain TBLINK_STUB_DIRECT path is required: cross-page and
     * maybe-cross-page edges emit page/check stubs that may reach C before
     * the direct-link slot, where a stale M-EFLAGS would be observable. */
    if (branch_is_cross_page(branch, 0) || branch_is_cross_page(branch, 1) ||
        branch_target_maybe_cross_page_tb(branch, 0) ||
        branch_target_maybe_cross_page_tb(branch, 1)) {
        return false;
    }
#else
    return false;
#endif
#ifdef CONFIG_LATX_TU
    {
        /*
         * lsenv->cpu_state holds the CPUX86State (see lsenv.h and
         * latx-config.c: lsenv->cpu_state = env), not a CPUState.  Passing it
         * straight to latxs_tb_disasm_eflag_use_zero() made the callee read
         * CPUState.env_ptr at that address (offset 664, inside CPUX86State
         * save_regs) and call get_page_addr_code_hostp_noexit() with a
         * garbage/garbage-NULL env.  Use the established idiom (see
         * cpu_hit_breakpoint() below) to obtain the real CPUState.
         */
        CPUState *cpu = env_cpu(lsenv->cpu_state);
        target_ulong cs_base = tb->cs_base;
        uint32_t flags = tb->flags;
        uint32_t cflags = tb_cflags(tb);

        if (!latxs_tb_disasm_eflag_use_zero(tb, cpu,
                latxs_tr_exit_tb_succ_pc(branch, 0), cs_base, flags, cflags) ||
            !latxs_tb_disasm_eflag_use_zero(tb, cpu,
                latxs_tr_exit_tb_succ_pc(branch, 1), cs_base, flags, cflags)) {
            return false;
        }
    }
#else
    return false;
#endif
    td->eflag_tb_defer.valid = 1;
    td->eflag_tb_defer.emitted = 0;
    td->eflag_tb_defer.n_operands = n_operands;
    td->eflag_tb_defer.opcode = opcode;
    td->eflag_tb_defer.opnd0 = *opnd0;
    td->eflag_tb_defer.opnd1 = n_operands > 1 ? *opnd1 : latxs_zero_ir2_opnd;
    return true;
}

void latxs_tr_tb_eflag_defer_end(void)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    TranslationBlock *tb = td->curr_tb;

    /* Both branch edges of a fused compare+branch must have emitted the
     * slow-path materialisation; otherwise the TB would be missing it.  The
     * skip bitmap is written by the same emit, so requiring 0x3 also proves
     * that no edge was recorded as skipped without its materialisation. */
    SYSFLAGTB_CHECK(!td->eflag_tb_defer.valid ||
              (td->eflag_tb_defer.emitted == 2 &&
               TB_EFLAGTB_SKIP(tb) == 0x3u),
              "sysflagtb: deferred eflags emitted on %d/2 exits (skip=%#x)\n",
              td->eflag_tb_defer.emitted, TB_EFLAGTB_SKIP(tb));
    td->eflag_tb_defer.valid = 0;
}

typedef struct ExitTBEmitState {
    bool emitted_interrupt_check;
    int tblink_stub_mode;
    bool loaded_tb_addr;
    bool saved_next_eip;
    /* The exit edge emitted its patchable direct-link slot (B 2; nop or the
     * linkopt label) before the deferred materialisation. */
    bool link_slot_emitted;
#ifdef CONFIG_SOFTMMU
    bool cp_inline_p0;
    IR2_OPND cp_inline_fallback;
    IR2_OPND cp_inline_link_entry;
#ifdef CONFIG_LATX_TU
    bool tu_generation_inline;
    bool hras_checked_call;
    IR2_OPND tu_generation_fallback;
    IR2_OPND tu_generation_link_entry;
    IR2_OPND hras_check_source;
    unsigned hras_check_bits;
#endif
#endif
    bool deferred_tu_cold_exit;
} ExitTBEmitState;

/*
 * Option sysflagtb verification: the recorded producer operands must still be
 * readable at the exit slow path.  Guest GPRs are mapped to fixed host
 * registers that the exit scaffolding does not write, and RA only recycles
 * itemps that were explicitly freed, so an itemp operand must still be
 * allocated here.  A failure means the exit code (or a future translator
 * change) clobbered the deferred operands and the materialisation would write
 * garbage M-EFLAGS.
 */
static bool latxs_tr_tb_eflag_opnd_live(const IR2_OPND *opnd)
{
    if (!latxs_ir2_opnd_is_reg_temp((IR2_OPND *)opnd)) {
        return true;
    }
    for (int i = 0; i < (int)latxs_itemp_status_num; ++i) {
        if (latxs_itemp_status_default[i].physical_id ==
            latxs_ir2_opnd_reg((IR2_OPND *)opnd)) {
            return !latxs_ra_temp_is_free(i);
        }
    }
    return true;
}

/*
 * Opt-in slow-path counter (LATS_SYSFLAGTB_STAT=1): proves at runtime that
 * the deferred materialisation really executes on C exits.  It is emitted
 * only on the link-miss / pending-IRQ slow path, right before the producer,
 * so a taken direct link never reaches it.  a0/a1 are ABI argument registers
 * that no code on this path keeps live (the producer reads only guest GPRs /
 * itemps, and the context-switch jump does not use them), and the same
 * AMADD sequence is already used by latxs_external_tu_stub().
 */
static void latxs_tr_tb_eflag_emit_materialise_count(void)
{
    uint64_t *counter = latx_tb_eflag_stat_materialise_counter();

    if (!counter) {
        return;
    }
    la_imm(&la_a0, (uintptr_t)counter);
    if (hamt_enabled()) {
        latxs_tr_dmw_in_fix_mem(&la_a0);
    }
    la_out2i(LISA_ADDI_D, &la_a1, &la_zero, 1);
    la_out3(LISA_AMADD_D, &la_zero, &la_a0, &la_a1);
}

/* Emit the deferred flag producer on this exit edge's slow path.  The caller
 * guarantees that the direct-link slot has already been emitted (so a taken
 * link branches over this code) and that the interrupt-check label, if any,
 * points here (so a pending IRQ executes this before leaving to C). */
static void latxs_tr_tb_eflag_emit_deferred(TranslationBlock *tb, int succ_id,
        const ExitTBEmitState *state)
{
    TRANSLATION_DATA *td = lsenv->tr_data;

    if (!td->eflag_tb_defer.valid) {
        return;
    }
    /*
     * Coverage invariants of the deferred slow path.  They fire if a future
     * change selects a stub that can reach C before this point (cross-page
     * check / TU check / deferred TU cold exit), or if the link-slot
     * bookkeeping is broken, or if the two edges are not emitted in order.
     */
    SYSFLAGTB_CHECK(TBLINK_STUB_MODE(state->tblink_stub_mode) == TBLINK_STUB_DIRECT ||
              TBLINK_STUB_MODE(state->tblink_stub_mode) == TBLINK_STUB_NONE,
              "sysflagtb: deferred edge %d uses link stub mode %#x\n",
              succ_id, state->tblink_stub_mode);
    SYSFLAGTB_CHECK(!state->deferred_tu_cold_exit,
              "sysflagtb: deferred edge %d uses a TU cold exit\n", succ_id);
    if (TBLINK_STUB_MODE(state->tblink_stub_mode) == TBLINK_STUB_DIRECT) {
        SYSFLAGTB_CHECK(state->link_slot_emitted,
                  "sysflagtb: deferred edge %d emitted before its link slot\n",
                  succ_id);
    }
    SYSFLAGTB_CHECK(succ_id == td->eflag_tb_defer.emitted,
              "sysflagtb: deferred edge %d emitted out of order (next %d)\n",
              succ_id, td->eflag_tb_defer.emitted);
    SYSFLAGTB_CHECK(latxs_tr_tb_eflag_opnd_live(&td->eflag_tb_defer.opnd0) &&
              (td->eflag_tb_defer.n_operands < 2 ||
               latxs_tr_tb_eflag_opnd_live(&td->eflag_tb_defer.opnd1)),
              "sysflagtb: deferred producer operand no longer live\n");
    latxs_tr_tb_eflag_emit_materialise_count();
    if (td->eflag_tb_defer.n_operands > 1) {
        la_out2((IR2_OPCODE)td->eflag_tb_defer.opcode,
                &td->eflag_tb_defer.opnd0, &td->eflag_tb_defer.opnd1);
    } else {
        la_out1((IR2_OPCODE)td->eflag_tb_defer.opcode,
                &td->eflag_tb_defer.opnd0);
    }
    if (hamt_enabled() && flag_reduction_tb_hamt_recover()) {
        /* The marker must not survive the materialisation fallback. */
        la_out2i(LISA_ADDI_D, &la_ra, &la_zero, 0);
    }
    TB_EFLAGTB_SKIP(tb) |= 1u << succ_id;
    latx_tb_eflag_stat_defer(td->eflag_tb_defer.emitted);
    td->eflag_tb_defer.emitted++;
}

static inline void __latxs_tr_gen_to_cp_check_page1_id0(IR1_INST *branch,
        TranslationBlock *tb, TRANSLATION_DATA *td, bool intcheck)
{
    tb_gen_cpu_neg_itlb(tb, latxs_tr_exit_tb_succ_pc(branch, 0));
    CP_CHECK_PREPARE_DATA(tb, td);
    if (cpl == 3) {
        if (canptw) {
            if (intcheck) {
                tb_sc_table_offset_jmp(tb, mtcc_id,
                        cp_check_ptw_n0_p0_mmu_user_intcheck,
                        td->tcg_region_offset, offset, 1,
                        CP_CHECK_PTW_N0_P0_MMU_USER_INTCHECK);
            } else {
                tb_sc_table_offset_jmp(tb, mtcc_id,
                        cp_check_ptw_n0_p0_mmu_user,
                        td->tcg_region_offset, offset, 1,
                        CP_CHECK_PTW_N0_P0_MMU_USER);
            }
        } else {
            if (intcheck) {
                tb_sc_table_offset_jmp(tb, mtcc_id,
                        cp_check_n0_p0_mmu_user_intcheck,
                        td->tcg_region_offset, offset, 1,
                        CP_CHECK_N0_P0_MMU_USER_INTCHECK);
            } else {
                tb_sc_table_offset_jmp(tb, mtcc_id,
                        cp_check_n0_p0_mmu_user,
                        td->tcg_region_offset, offset, 1,
                        CP_CHECK_N0_P0_MMU_USER);
            }
        }
        return;
    }
    if (cpl == 0) {
        if (canptw) {
            if (intcheck) {
                tb_sc_table_offset_jmp(tb, mtcc_id,
                        cp_check_ptw_n0_p0_mmu_knosmap_intcheck,
                        td->tcg_region_offset, offset, 1,
                        CP_CHECK_PTW_N0_P0_MMU_KNOSMAP_INTCHECK);
            } else {
                tb_sc_table_offset_jmp(tb, mtcc_id,
                        cp_check_ptw_n0_p0_mmu_knosmap,
                        td->tcg_region_offset, offset, 1,
                        CP_CHECK_PTW_N0_P0_MMU_KNOSMAP);
            }
        } else {
            if (intcheck) {
                tb_sc_table_offset_jmp(tb, mtcc_id,
                        cp_check_n0_p0_mmu_knosmap_intcheck,
                        td->tcg_region_offset, offset, 1,
                        CP_CHECK_N0_P0_MMU_KNOSMAP_INTCHECK);
            } else {
                tb_sc_table_offset_jmp(tb, mtcc_id,
                        cp_check_n0_p0_mmu_knosmap,
                        td->tcg_region_offset, offset, 1,
                        CP_CHECK_N0_P0_MMU_KNOSMAP);
            }
        }
        return;
    }
    assert(0);
}
static inline void __latxs_tr_gen_to_cp_check_page1_id1(IR1_INST *branch,
        TranslationBlock *tb, TRANSLATION_DATA *td, bool intcheck)
{
    tb_gen_cpu_neg_itlb(tb, latxs_tr_exit_tb_succ_pc(branch, 1));
    CP_CHECK_PREPARE_DATA(tb, td);
    if (cpl == 3) {
        if (canptw) {
            if (intcheck) {
                tb_sc_table_offset_jmp(tb, mtcc_id,
                        cp_check_ptw_n1_p0_mmu_user_intcheck,
                        td->tcg_region_offset, offset, 1,
                        CP_CHECK_PTW_N1_P0_MMU_USER_INTCHECK);
            } else {
                tb_sc_table_offset_jmp(tb, mtcc_id,
                        cp_check_ptw_n1_p0_mmu_user,
                        td->tcg_region_offset, offset, 1,
                        CP_CHECK_PTW_N1_P0_MMU_USER);
            }
        } else {
            if (intcheck) {
                tb_sc_table_offset_jmp(tb, mtcc_id,
                        cp_check_n1_p0_mmu_user_intcheck,
                        td->tcg_region_offset, offset, 1,
                        CP_CHECK_N1_P0_MMU_USER_INTCHECK);
            } else {
                tb_sc_table_offset_jmp(tb, mtcc_id,
                        cp_check_n1_p0_mmu_user,
                        td->tcg_region_offset, offset, 1,
                        CP_CHECK_N1_P0_MMU_USER);
            }
        }
        return;
    }
    if (cpl == 0) {
        if (canptw) {
            if (intcheck) {
                tb_sc_table_offset_jmp(tb, mtcc_id,
                        cp_check_ptw_n1_p0_mmu_knosmap_intcheck,
                        td->tcg_region_offset, offset, 1,
                        CP_CHECK_PTW_N1_P0_MMU_KNOSMAP_INTCHECK);
            } else {
                tb_sc_table_offset_jmp(tb, mtcc_id,
                        cp_check_ptw_n1_p0_mmu_knosmap,
                        td->tcg_region_offset, offset, 1,
                        CP_CHECK_PTW_N1_P0_MMU_KNOSMAP);
            }
        } else {
            if (intcheck) {
                tb_sc_table_offset_jmp(tb, mtcc_id,
                        cp_check_n1_p0_mmu_knosmap_intcheck,
                        td->tcg_region_offset, offset, 1,
                        CP_CHECK_N1_P0_MMU_KNOSMAP_INTCHECK);
            } else {
                tb_sc_table_offset_jmp(tb, mtcc_id,
                        cp_check_n1_p0_mmu_knosmap,
                        td->tcg_region_offset, offset, 1,
                        CP_CHECK_N1_P0_MMU_KNOSMAP);
            }
        }
        return;
    }
    assert(0);
}
static inline void __latxs_tr_gen_to_cp_check_page2_id0(IR1_INST *branch,
        TranslationBlock *tb, TRANSLATION_DATA *td, bool intcheck)
{
    tb_gen_cpu_neg_itlb(tb, latxs_tr_exit_tb_succ_pc(branch, 0));
    CP_CHECK_PREPARE_DATA(tb, td);
    if (cpl == 3) {
        if (canptw) {
            if (intcheck) {
                tb_sc_table_offset_jmp(tb, mtcc_id,
                        cp_check_ptw_n0_p01_mmu_user_intcheck,
                        td->tcg_region_offset, offset, 1,
                        CP_CHECK_PTW_N0_P01_MMU_USER_INTCHECK);
            } else {
                tb_sc_table_offset_jmp(tb, mtcc_id,
                        cp_check_ptw_n0_p01_mmu_user,
                        td->tcg_region_offset, offset, 1,
                        CP_CHECK_PTW_N0_P01_MMU_USER);
            }
        } else {
            if (intcheck) {
                tb_sc_table_offset_jmp(tb, mtcc_id,
                        cp_check_n0_p01_mmu_user_intcheck,
                        td->tcg_region_offset, offset, 1,
                        CP_CHECK_N0_P01_MMU_USER_INTCHECK);
            } else {
                tb_sc_table_offset_jmp(tb, mtcc_id,
                        cp_check_n0_p01_mmu_user,
                        td->tcg_region_offset, offset, 1,
                        CP_CHECK_N0_P01_MMU_USER);
            }
        }
        return;
    }
    if (cpl == 0) {
        if (canptw) {
            if (intcheck) {
                tb_sc_table_offset_jmp(tb, mtcc_id,
                        cp_check_ptw_n0_p01_mmu_knosmap_intcheck,
                        td->tcg_region_offset, offset, 1,
                        CP_CHECK_PTW_N0_P01_MMU_KNOSMAP_INTCHECK);
            } else {
                tb_sc_table_offset_jmp(tb, mtcc_id,
                        cp_check_ptw_n0_p01_mmu_knosmap,
                        td->tcg_region_offset, offset, 1,
                        CP_CHECK_PTW_N0_P01_MMU_KNOSMAP);
            }
        } else {
            if (intcheck) {
                tb_sc_table_offset_jmp(tb, mtcc_id,
                        cp_check_n0_p01_mmu_knosmap_intcheck,
                        td->tcg_region_offset, offset, 1,
                        CP_CHECK_N0_P01_MMU_KNOSMAP_INTCHECK);
            } else {
                tb_sc_table_offset_jmp(tb, mtcc_id,
                        cp_check_n0_p01_mmu_knosmap,
                        td->tcg_region_offset, offset, 1,
                        CP_CHECK_N0_P01_MMU_KNOSMAP);
            }
        }
        return;
    }
    assert(0);
}
static inline void __latxs_tr_gen_to_cp_check_page2_id1(IR1_INST *branch,
        TranslationBlock *tb, TRANSLATION_DATA *td, bool intcheck)
{
    tb_gen_cpu_neg_itlb(tb, latxs_tr_exit_tb_succ_pc(branch, 1));
    CP_CHECK_PREPARE_DATA(tb, td);
    if (cpl == 3) {
        if (canptw) {
            if (intcheck) {
                tb_sc_table_offset_jmp(tb, mtcc_id,
                        cp_check_ptw_n1_p01_mmu_user_intcheck,
                        td->tcg_region_offset, offset, 1,
                        CP_CHECK_PTW_N1_P01_MMU_USER_INTCHECK);
            } else {
                tb_sc_table_offset_jmp(tb, mtcc_id,
                        cp_check_ptw_n1_p01_mmu_user,
                        td->tcg_region_offset, offset, 1,
                        CP_CHECK_PTW_N1_P01_MMU_USER);
            }
        } else {
            if (intcheck) {
                tb_sc_table_offset_jmp(tb, mtcc_id,
                        cp_check_n1_p01_mmu_user_intcheck,
                        td->tcg_region_offset, offset, 1,
                        CP_CHECK_N1_P01_MMU_USER_INTCHECK);
            } else {
                tb_sc_table_offset_jmp(tb, mtcc_id,
                        cp_check_n1_p01_mmu_user,
                        td->tcg_region_offset, offset, 1,
                        CP_CHECK_N1_P01_MMU_USER);
            }
        }
        return;
    }
    if (cpl == 0) {
        if (canptw) {
            if (intcheck) {
                tb_sc_table_offset_jmp(tb, mtcc_id,
                        cp_check_ptw_n1_p01_mmu_knosmap_intcheck,
                        td->tcg_region_offset, offset, 1,
                        CP_CHECK_PTW_N1_P01_MMU_KNOSMAP_INTCHECK);
            } else {
                tb_sc_table_offset_jmp(tb, mtcc_id,
                        cp_check_ptw_n1_p01_mmu_knosmap,
                        td->tcg_region_offset, offset, 1,
                        CP_CHECK_PTW_N1_P01_MMU_KNOSMAP);
            }
        } else {
            if (intcheck) {
                tb_sc_table_offset_jmp(tb, mtcc_id,
                        cp_check_n1_p01_mmu_knosmap_intcheck,
                        td->tcg_region_offset, offset, 1,
                        CP_CHECK_N1_P01_MMU_KNOSMAP_INTCHECK);
            } else {
                tb_sc_table_offset_jmp(tb, mtcc_id,
                        cp_check_n1_p01_mmu_knosmap,
                        td->tcg_region_offset, offset, 1,
                        CP_CHECK_N1_P01_MMU_KNOSMAP);
            }
        }
        return;
    }
    assert(0);
}
static inline void __latxs_tr_gen_to_cp_check_page1(IR1_INST *branch,
        TranslationBlock *tb, TRANSLATION_DATA *td, int succ_id, bool intcheck)
{
    if (!succ_id)   __latxs_tr_gen_to_cp_check_page1_id0(branch, tb, td, intcheck);
    else            __latxs_tr_gen_to_cp_check_page1_id1(branch, tb, td, intcheck);
}
static inline void __latxs_tr_gen_to_cp_check_page2(IR1_INST *branch,
        TranslationBlock *tb, TRANSLATION_DATA *td, int succ_id, bool intcheck)
{
    if (!succ_id)   __latxs_tr_gen_to_cp_check_page2_id0(branch, tb, td, intcheck);
    else            __latxs_tr_gen_to_cp_check_page2_id1(branch, tb, td, intcheck);
}
static inline void latxs_tr_gen_to_cp_check(IR1_INST *branch,
        TranslationBlock *tb, TRANSLATION_DATA *td, int succ_id, int check2page, bool intcheck)
{
    if (check2page) __latxs_tr_gen_to_cp_check_page2(branch, tb, td, succ_id, intcheck);
    else            __latxs_tr_gen_to_cp_check_page1(branch, tb, td, succ_id, intcheck);
}
#endif

/*
 * Exit-TB design
 *
 * This code builds the exit sequence for one TB successor.
 *
 * The exit is classified in two steps:
 *
 *   1. direct vs indirect
 *      - direct   : target is known at translation time
 *      - indirect : target is known only at runtime
 *
 *   2. optional exit modifiers
 *      - cross-page handling
 *      - interrupt handling
 *      - TB-link / lookup availability
 *
 * Main split:
 *
 *   - direct path
 *     Used by direct jmp / direct call / jcc successors.
 *     The code may:
 *       * record next_pc or save next EIP
 *       * emit a TB-link stub
 *       * fall back to cross-page lookup
 *       * finally return through context switch
 *
 *   - indirect path
 *     Used by ret / indirect jmp / indirect call.
 *     The code must save next EIP first, then:
 *       * try runtime lookup if enabled
 *       * otherwise return through context switch
 *
 * Cross-page handling:
 *
 *   Cross-page logic only refines the direct path.
 *   It selects one link-stub shape:
 *       * normal TB-link stub
 *       * cross-page check stub
 *       * save-EIP stub
 *       * inline cross-page check stub
 *       * no stub, use lookup instead
 *
 * Interrupt handling:
 *
 *   - IRQ_CHECK_BLOCK_END
 *       Insert explicit interrupt check around the exit.
 *
 *   - IRQ_CHECK_ASYNC_NOTIFY
 *       Do not insert an explicit check here.
 *       Only mark the exit with tb->int_opt[succ_id].
 */
typedef struct ExitTBContext {
    IR1_OPCODE branch_opcode;
    bool target_is_indirect;
    TRANSLATION_DATA *tr_ctx;
    TranslationBlock *curr_tb;
    bool allow_tb_link;
    uint32_t curr_mtcc_id;
    bool target_crosses_page;
    bool target_maybe_cross_page_block;
    bool target_in_current_tu;
    bool cross_page_intcheck;
} ExitTBContext;


#ifdef CONFIG_LATX_TU
typedef struct HrasDirectCall {
    ExitTBContext ctx;
    ExitTBEmitState state;
    IR2_OPND cold;
} HrasDirectCall;

static bool tu_hras_return_adjacent(TranslationBlock *tb, IR1_INST *branch);

bool latxs_hras_direct_checks_fit(uint32_t count, void *const *direct_calls,
                                  const int *label_pos)
{
#ifdef CONFIG_SOFTMMU
    for (uint32_t i = 0; direct_calls && i < count; i++) {
        const HrasDirectCall *call = direct_calls[i];
        const ExitTBEmitState *state;
        int64_t distance, limit;

        if (!call || !call->state.hras_check_bits) {
            continue;
        }
        state = &call->state;
        g_assert(label_pos[state->hras_check_source.val] >= 0);
        g_assert(label_pos[state->tu_generation_fallback.val] >= 0);
        distance = (int64_t)label_pos[state->tu_generation_fallback.val] -
                   label_pos[state->hras_check_source.val];
        /* In long form the cold B follows the short success check. */
        distance -= state->hras_check_bits == 26;
        limit = INT64_C(1) << (state->hras_check_bits - 1);
        if (distance < -limit || distance >= limit) {
            return false;
        }
    }
#endif
    return true;
}
#endif

static bool latxs_tr_exit_tb_uses_tu_check(const ExitTBContext *ctx)
{
#if defined(CONFIG_SOFTMMU) && defined(CONFIG_LATX_TU)
    return tucp_fast_direct_check() &&
           (tb_is_tu(ctx->curr_tb) || !option_external_tu_stub) &&
           (ctx->curr_tb->flags & HF_CPL_MASK) == HF_CPL_MASK &&
           !ctx->target_is_indirect && !ctx->target_in_current_tu;
#else
    return false;
#endif
}

static void latxs_tr_exit_tb_prepare_direct_interrupt(ExitTBContext *ctx,
        ExitTBEmitState *state, IR1_INST *branch, int succ_id)
{
    if (ctx->target_is_indirect || !succ_id) {
        return;
    }

    if (latxs_check_interrupt_at_block_end() &&
        (ctx->target_crosses_page ||
        ctx->target_maybe_cross_page_block) &&
        !latxs_tr_exit_tb_uses_tu_check(ctx)) {
        /* do interrupt check in cross-page checking codes */
        ctx->cross_page_intcheck = true;
        return;
    }

    if (latxs_check_interrupt_at_block_end()) {
        latxs_load_intcheck_to_reg(&la_a3);
        latxs_append_ir2_opnd3(LISA_BLT, &la_a3, &la_zero,
                latxs_ir2_interrupt_get_label(ctx->curr_tb, succ_id));
        state->emitted_interrupt_check = 1;
    } else if (option_int_opt == IRQ_CHECK_ASYNC_NOTIFY) {
        ctx->curr_tb->int_opt[succ_id] = 1;
    }
}

static int latxs_tr_exit_tb_select_link_stub_mode(ExitTBContext *ctx)
{
    int link_stub_mode = TBLINK_STUB_DIRECT;

#ifdef CONFIG_SOFTMMU
    if (latxs_tr_exit_tb_uses_tu_check(ctx)) {
        /* Every direct entry from outside the current TU validates all pages
         * described by the target TB, including ordinary non-TU TBs. */
        if (!ctx->target_in_current_tu) {
            link_stub_mode = TBLINK_STUB_TUCHECK;
        } else {
            if (!ctx->allow_tb_link) {
                link_stub_mode = TBLINK_STUB_NONE;
            }
        }
        return link_stub_mode;
    }
    if (ctx->target_maybe_cross_page_block) {
        if (!cp_tb_enabled()) link_stub_mode = TBLINK_STUB_DIRECT;
        if (cp_tb_link())   link_stub_mode = TBLINK_STUB_DIRECT;
        if (cp_tb_check())  link_stub_mode = TBLINK_STUB_CPCHECK | TBLINK_STUB_CPLABEL;
    } else if (ctx->target_crosses_page) {
        if (!cp_enabled()) ctx->allow_tb_link = 0;
        if (cp_link())   link_stub_mode = TBLINK_STUB_DIRECT;
        if (cp_check())  link_stub_mode = TBLINK_STUB_CPCHECK;
    }
#endif

    if (!ctx->allow_tb_link) {
        link_stub_mode = TBLINK_STUB_NONE;
    }

    return link_stub_mode;
}

#ifdef CONFIG_SOFTMMU
static void latxs_tr_exit_tb_set_cp_label(TranslationBlock *tb, int succ_id,
        const char *name)
{
    IR2_OPND cp_label_opnd = la_new_label();

    la_label_name(cp_label_opnd, name);
    tb->jmp_cp_data[succ_id] = cp_label_opnd.val;
}

static void latxs_tr_exit_tb_emit_cp_inline_p0_check(const ExitTBContext *ctx,
        ExitTBEmitState *state, IR2_OPND *tbptr, IR1_INST *branch, int succ_id)
{
    IR2_OPND expected_tag = la_a2;
    IR2_OPND int_pending = la_a3;
    bool merge_check = ctx->cross_page_intcheck && cp_inline_p0_merged_check();
    int page0_offset = TB_NEXT_DATA_OFF(page_addr_cache[0], succ_id);

    state->cp_inline_p0 = true;
    state->cp_inline_fallback = la_new_label();
    state->cp_inline_link_entry = la_new_label();

    if (ctx->cross_page_intcheck) {
        if (merge_check) {
            latxs_load_intcheck_to_reg(&int_pending);
        } else {
            latxs_load_intcheck_to_reg(&expected_tag);
            la_out3(LISA_BLT, &expected_tag, &la_zero,
                    &state->cp_inline_fallback);
        }
    }

    tb_gen_cpu_neg_itlb(ctx->curr_tb,
            latxs_tr_exit_tb_succ_pc(branch, succ_id));
    if (split_tb_base_ok()) {
        la_out1i(LISA_LU32I_D, tbptr, SPLIT_TB_BASE >> 32);
    }
    if (hamt_enabled()) {
        latxs_tr_dmw_in_fix_mem(tbptr);
    }
    la_out2i(LISA_LD_D, &expected_tag, tbptr, page0_offset);
    if (merge_check) {
        /*
         * int_pending is either 0 or -1.  In the supported <= 8 GiB guest
         * RAM layout, the 28-bit mixed physical page number cannot be all
         * ones, so the complete page-address mix cannot be UINT64_MAX.
         * Thus -1 is a safe mismatch sentinel while 0 leaves the cached tag
         * unchanged.
         */
        la_out3(LISA_OR, &expected_tag, &expected_tag, &int_pending);
        la_out3(LISA_BNE, &expected_tag, &la_a0,
                &state->cp_inline_fallback);
    } else {
        la_out3(LISA_BNE, &expected_tag, &la_a0, &state->cp_inline_fallback);
    }
    la_label_name(state->cp_inline_link_entry, "cross-page p0 inline hit");
}

static void latxs_tr_exit_tb_emit_cp_inline_p0_fallback(const ExitTBContext *ctx,
        const ExitTBEmitState *state, IR1_INST *branch, int succ_id)
{
    if (!state->cp_inline_p0) {
        return;
    }

    IR2_OPND fallback = state->cp_inline_fallback;
    IR2_OPND link_entry = state->cp_inline_link_entry;

    la_label_name(fallback, "cross-page p0 inline fallback");
    latxs_tr_gen_to_cp_check(branch, ctx->curr_tb, ctx->tr_ctx, succ_id, 0,
            ctx->cross_page_intcheck);
    la_out1(LISA_B, &link_entry);
}

static void latxs_tr_exit_tb_emit_cp_check_stub(const ExitTBContext *ctx,
        ExitTBEmitState *state, IR2_OPND *tbptr, ADDR tb_addr,
        IR1_INST *branch, int succ_id)
{
    int check2page = ctx->target_maybe_cross_page_block;
    if (!check2page) { assert(ctx->target_crosses_page); }

    latxs_tr_gen_exit_tb_load_tb_addr(tbptr, tb_addr);
    if (!check2page && option_link_opt && cp_inline_p0()) {
        latxs_tr_exit_tb_emit_cp_inline_p0_check(ctx, state, tbptr, branch, succ_id);
    } else {
        latxs_tr_gen_to_cp_check(branch, ctx->curr_tb, ctx->tr_ctx, succ_id, check2page,
                ctx->cross_page_intcheck);
    }

    if (option_link_opt) {
        /*
         * (1) bl cross-page check codes
         * (2) nop // to make (3) (4) not span in two cacheline
         * (3) nop === link opt ===
         * (4) j context switch
         */
        latxs_tb_link_align_inside_cacheline(ctx->curr_tb); // (2)
        IR2_OPND label = la_new_label();
        la_label_name(label, "tb link stub opt for cross page");
        ctx->curr_tb->jmp_reset_offset[succ_id] = label.val;
        ctx->curr_tb->link_opt[succ_id] = 1;
        la_out_nop(); // (3)
    } else {
        /*
         * (1) bl cross-page check codes
         * (2) nop // to make (3) (4) not span in two cacheline
         * (3) b 2 // tb link stub
         * (4) nop
         */
        latxs_tr_gen_exit_tb_j_tb_link(ctx->curr_tb, succ_id);
    }
}

static void latxs_tr_gen_to_tu_cp_check(TranslationBlock *tb,
        TRANSLATION_DATA *td, int succ_id, bool intcheck)
{
    int mtcc_id = td->mtcc_id;
    int offset = td->ir2_asm_nr << 2;

    lsassert((tb->flags & HF_CPL_MASK) == HF_CPL_MASK);
    if (succ_id) {
        if (intcheck) {
            tb_sc_table_offset_jmp(tb, mtcc_id,
                    tu_cp_check_n1_mmu_user_intcheck,
                    td->tcg_region_offset, offset, 1,
                    TU_CP_CHECK_N1_MMU_USER_INTCHECK);
        } else {
            tb_sc_table_offset_jmp(tb, mtcc_id,
                    tu_cp_check_n1_mmu_user,
                    td->tcg_region_offset, offset, 1,
                    TU_CP_CHECK_N1_MMU_USER);
        }
    } else {
        if (intcheck) {
            tb_sc_table_offset_jmp(tb, mtcc_id,
                    tu_cp_check_n0_mmu_user_intcheck,
                    td->tcg_region_offset, offset, 1,
                    TU_CP_CHECK_N0_MMU_USER_INTCHECK);
        } else {
            tb_sc_table_offset_jmp(tb, mtcc_id,
                    tu_cp_check_n0_mmu_user,
                    td->tcg_region_offset, offset, 1,
                    TU_CP_CHECK_N0_MMU_USER);
        }
    }
}

#ifdef CONFIG_LATX_TU
static bool tu_hras_emit_call_slot(const ExitTBContext *ctx,
        ExitTBEmitState *state, int succ_id)
{
#ifdef CONFIG_SOFTMMU
    TranslationBlock *tb = ctx->curr_tb;
    IR1_INST *branch = (IR1_INST *)tb->s_data->ir1 + tb->icount - 1;
    HrasDirectCall *call;
    IR2_OPND hot;
    unsigned bytes = 8;

    if (succ_id || !ctx->allow_tb_link || ctx->target_is_indirect ||
        (tb->bool_flags & IS_HRAS_CALL) ||
        !tu_hras_return_adjacent(tb, branch)) {
        return false;
    }
    /* Reserve the maximum slot width so the link encoder can
     * switch between BL+NOP and PCADDU18I+JIRL in place. */
    latxs_tb_link_align_inside_cacheline(tb);
    if (state->hras_checked_call && state->tu_generation_inline &&
        ctx->tr_ctx->hras_long_checks) {
        /* The success branch also skips any two-word slot alignment. */
        la_label_name(state->tu_generation_link_entry,
                      "TU generation inline hit");
    }
    if (latxs_hras_aot_translate()) {
        TranslationBlock *next = tb->s_data->next_tb[TU_TB_INDEX_NEXT];
        g_assert(next && next->icount && next->flags == tb->flags &&
                 next->cs_base == tb->cs_base &&
                 tb_cflags(next) == tb_cflags(tb));
    }
    call = g_new0(HrasDirectCall, 1);
    call->cold = la_new_label();
    call->ctx = *ctx;
    ctx->tr_ctx->hras_direct_call = call;
    hot = la_new_label();
    la_label(hot);
    tb->bool_flags |= IS_HRAS_CALL;
    tb->jmp_reset_offset[0] = hot.val;
    /* Until label disposal this field carries the selected slot width. */
    tb->jmp_insn_offset[0] = bytes;
    tb->link_opt[0] = 1;
    state->deferred_tu_cold_exit = true;
    la_out1(LISA_B, &call->cold);
    if (bytes == 8) {
        la_out_nop();
    }
    return true;
#else
    return false;
#endif
}
#endif

static void latxs_tr_exit_tb_emit_checked_link_slot(const ExitTBContext *ctx,
        ExitTBEmitState *state, int succ_id, const char *name)
{
#ifdef CONFIG_LATX_TU
    if (state->hras_checked_call &&
        tu_hras_emit_call_slot(ctx, state, succ_id)) {
        return;
    }
    if (state->hras_checked_call && state->tu_generation_inline &&
        ctx->tr_ctx->hras_long_checks) {
        la_label_name(state->tu_generation_link_entry,
                      "TU generation inline hit");
    }
#endif
    if (option_link_opt && !state->emitted_interrupt_check) {
        IR2_OPND label = la_new_label();

        latxs_tb_link_align_inside_cacheline(ctx->curr_tb);
        la_label_name(label, name);
        ctx->curr_tb->jmp_reset_offset[succ_id] = label.val;
        ctx->curr_tb->link_opt[succ_id] = 1;
        la_out_nop();
    } else {
        latxs_tr_gen_exit_tb_j_tb_link(ctx->curr_tb, succ_id);
    }
}

#ifdef CONFIG_LATX_TU
static void latxs_tr_exit_tb_emit_tu_memset_cache_check(
        ExitTBEmitState *state, IR2_OPND *offset,
        IR2_OPND *array, IR2_OPND *valid)
{
    la_out2i(LISA_LD_D, array, &la_env,
             offsetof(CPUX86State, tu_generation_array));
    la_out3(LISA_LDX_BU, valid, array, offset);
    if (state->hras_checked_call) {
        state->hras_check_source = la_new_label();
        la_label(state->hras_check_source);
        state->hras_check_bits = lsenv->tr_data->hras_long_checks ? 26 : 21;
    }
    if (state->hras_checked_call && lsenv->tr_data->hras_long_checks) {
        la_out2(LISA_BNEZ, valid, &state->tu_generation_link_entry);
        la_out1(LISA_B, &state->tu_generation_fallback);
    } else {
        la_out2(LISA_BEQZ, valid, &state->tu_generation_fallback);
    }
}

static void latxs_tr_exit_tb_emit_tu_counter_cache_check(
        ExitTBEmitState *state, IR2_OPND *offset,
        IR2_OPND *array, IR2_OPND *generation)
{
    la_out2i(LISA_LD_D, array, &la_env,
             offsetof(CPUX86State, tu_generation_array));
    la_out3(LISA_LDX_HU, generation, array, offset);
    la_out2i(LISA_LD_HU, array, &la_env,
             offsetof(CPUX86State, tu_generation));
    if (state->hras_checked_call) {
        state->hras_check_source = la_new_label();
        la_label(state->hras_check_source);
        state->hras_check_bits = lsenv->tr_data->hras_long_checks ? 26 : 16;
    }
    if (state->hras_checked_call && lsenv->tr_data->hras_long_checks) {
        la_out3(LISA_BEQ, array, generation,
                &state->tu_generation_link_entry);
        la_out1(LISA_B, &state->tu_generation_fallback);
    } else {
        la_out3(LISA_BNE, array, generation,
                &state->tu_generation_fallback);
    }
}

static void latxs_tr_exit_tb_emit_tu_offset_cache_check(
        const ExitTBContext *ctx, ExitTBEmitState *state,
        IR2_OPND *offset, IR2_OPND *array, IR2_OPND *valid, int succ_id)
{
    IR2_OPND patch_slot = la_new_label();

    la_label_name(patch_slot, "TU generation offset patch slot");
    ctx->curr_tb->s_data->tu_generation_offset_patch[succ_id] = patch_slot.val;
    la_out1i(LISA_LU12I_W, offset, 0);
    la_out2i(LISA_ORI, offset, offset, 0);
    latxs_tr_exit_tb_emit_tu_memset_cache_check(
            state, offset, array, valid);
}

static void latxs_tr_exit_tb_emit_tu_generation_check(
        const ExitTBContext *ctx, ExitTBEmitState *state, IR2_OPND *tbptr,
        int succ_id)
{
    IR2_OPND offset = la_a0;
    IR2_OPND array = la_a1;
    IR2_OPND valid = la_a2;
    int offset_offset = TB_TU_LINK_DATA_OFFSET(
                            next_tu_generation_offset[0]) +
                        sizeof(uint64_t) * succ_id;

    state->tu_generation_inline = true;
    state->tu_generation_fallback = la_new_label();
    state->tu_generation_link_entry = la_new_label();

    if (ctx->cross_page_intcheck) {
        latxs_load_intcheck_to_reg(&offset);
        la_out3(LISA_BLT, &offset, &la_zero,
                &state->tu_generation_fallback);
    }

    if (tucp_generation_offset()) {
        latxs_tr_exit_tb_emit_tu_offset_cache_check(
                ctx, state, &offset, &array, &valid, succ_id);
    } else {
        if (split_tb_base_ok()) {
            la_out1i(LISA_LU32I_D, tbptr, SPLIT_TB_BASE >> 32);
        }
        if (hamt_enabled()) {
            latxs_tr_dmw_in_fix_mem(tbptr);
        }
        la_out2i(LISA_LD_D, &offset, tbptr, offset_offset);
        if (tucp_generation_counter()) {
            latxs_tr_exit_tb_emit_tu_counter_cache_check(
                    state, &offset, &array, &valid);
        } else {
            latxs_tr_exit_tb_emit_tu_memset_cache_check(
                    state, &offset, &array, &valid);
        }
    }

    if (!state->hras_checked_call || !ctx->tr_ctx->hras_long_checks) {
        la_label_name(state->tu_generation_link_entry,
                      "TU generation inline hit");
    }
}

static void latxs_tr_exit_tb_emit_tu_generation_fallback(
        const ExitTBContext *ctx, const ExitTBEmitState *state,
        IR2_OPND *tbptr, ADDR tb_addr, int succ_id)
{
    if (!state->tu_generation_inline) {
        return;
    }

    IR2_OPND fallback = state->tu_generation_fallback;
    IR2_OPND link_entry = state->tu_generation_link_entry;

    la_label_name(fallback, "TU generation inline fallback");
    if (tucp_generation_offset()) {
        latxs_tr_gen_exit_tb_load_tb_addr(tbptr, tb_addr);
    }
    latxs_tr_gen_to_tu_cp_check(ctx->curr_tb, ctx->tr_ctx,
            succ_id, ctx->cross_page_intcheck);
    la_out1(LISA_B, &link_entry);
}
#endif

#ifdef CONFIG_LATX_TU
static bool tu_hras_checked_call_eligible(const ExitTBContext *ctx,
                                          IR1_INST *branch, int succ_id);
#endif

static void latxs_tr_exit_tb_emit_tu_check_stub(const ExitTBContext *ctx,
        ExitTBEmitState *state, IR2_OPND *tbptr, ADDR tb_addr,
        int succ_id)
{
    /* The CP label is after the normal interrupt check, so the TU static
     * checker always uses its no-intcheck entry. */
    assert(!ctx->cross_page_intcheck);

#ifdef CONFIG_LATX_TU
    IR1_INST *branch = (IR1_INST *)ctx->curr_tb->s_data->ir1 +
                      ctx->curr_tb->icount - 1;
    state->hras_checked_call =
        tu_hras_checked_call_eligible(ctx, branch, succ_id);
#endif

    /* Offset mode has no successor side-data dependency on the hit path. */
    if (!tucp_generation_offset()) {
        latxs_tr_gen_exit_tb_load_tb_addr(tbptr, tb_addr);
        state->loaded_tb_addr = 1;
    }

#ifdef CONFIG_LATX_TU
    if (tucp_generation_enabled()) {
        latxs_tr_exit_tb_emit_tu_generation_check(ctx, state, tbptr,
                                                  succ_id);
    } else
#endif
    {
        latxs_tr_gen_to_tu_cp_check(ctx->curr_tb, ctx->tr_ctx,
                succ_id, ctx->cross_page_intcheck);
    }

    latxs_tr_exit_tb_emit_checked_link_slot(ctx, state, succ_id,
            "tb link stub for TU check");
}

#ifdef CONFIG_LATX_TU
/* Stubs are independent region allocations, retired with their MTCC.
 * Linking runs under the existing TB jump lock. */
typedef struct ExternalTUStub {
    uint32_t code[192];
    uint64_t target;
    uint64_t executions;
    uint64_t successes;
    bool trace;
    struct ExternalTUStub *next;
} ExternalTUStub;

static ExternalTUStub *external_tu_stubs[MTCC_NR];
static pthread_mutex_t external_tu_lock = PTHREAD_MUTEX_INITIALIZER;

void latxs_external_tu_stub_summary(void)
{
    if (!getenv("LATS_EXTERNAL_TU_TRACE")) {
        return;
    }
    uint64_t executed = 0, checked = 0, stubs = 0;
    for (unsigned mtcc = 0; mtcc < MTCC_NR; mtcc++) {
        for (ExternalTUStub *stub = external_tu_stubs[mtcc]; stub;
             stub = stub->next) {
            executed += stub->executions;
            checked += stub->successes;
            stubs++;
        }
    }
    fprintf(stderr, "[External TU] summary stubs=%" PRIu64
            " executed=%" PRIu64 " checked=%" PRIu64 "\n",
            stubs, executed, checked);
}

static ExternalTUStub *external_tu_alloc_locked(unsigned mtcc, const void *source)
{
    if (getenv("LATS_EXTERNAL_TU_FAIL_ALLOC")) {
        return NULL;
    }
    TCGContext *s = tcg_ctx_mtcc[mtcc];
    ExternalTUStub *stub = tcg_region_alloc_data(s, sizeof(*stub));
    if (!stub) {
        return NULL;
    }
    int64_t distance = (intptr_t)stub - (intptr_t)source;
    if ((distance >> 2) != sextract64(distance >> 2, 0, 36)) {
        return NULL;
    }
    memset(stub, 0, sizeof(*stub));
    stub->next = external_tu_stubs[mtcc];
    external_tu_stubs[mtcc] = stub;
    return stub;
}

void latxs_external_tu_stub_reset(unsigned mtcc)
{
    uint64_t executions = 0, successes = 0;
    bool trace = false;
    pthread_mutex_lock(&external_tu_lock);
    for (ExternalTUStub *stub = external_tu_stubs[mtcc]; stub;
         stub = stub->next) {
        executions += stub->executions;
        successes += stub->successes;
        trace |= stub->trace;
    }
    external_tu_stubs[mtcc] = NULL;
    pthread_mutex_unlock(&external_tu_lock);
    if (trace) {
        fprintf(stderr, "[External TU] mtcc=%u executed=%" PRIu64
                " checked=%" PRIu64 " retired\n", mtcc, executions, successes);
    }
}

void latxs_external_tu_stub_unlink(TranslationBlock *tb, int n)
{
    ExternalTUStub *stub = tb->jit_tucheck_external[n];
    if (stub) {
        if (stub->trace && stub->executions) {
            fprintf(stderr, "[External TU] unlink source=%" PRIx64
                    " exit=%d executed=%" PRIu64 " checked=%" PRIu64 "\n",
                    (uint64_t)tb->pc, n, stub->executions, stub->successes);
        }
        /* Old readers may still finish: never reuse this stub before reset. */
        tb->jit_tucheck_external[n] = NULL;
    }
}

static void external_tu_count(uint64_t *counter)
{
    la_imm(&la_a0, (uintptr_t)counter);
    if (hamt_enabled()) {
        latxs_tr_dmw_in_fix_mem(&la_a0);
    }
    la_out2i(LISA_ADDI_D, &la_a1, &la_zero, 1);
    la_out3(LISA_AMADD_D, &la_zero, &la_a0, &la_a1);
}

void *latxs_external_tu_stub(TranslationBlock *tb, int n, void *target)
{
    TRANSLATION_DATA *saved = lsenv->tr_data;
    TRANSLATION_DATA scratch = { 0 };
    unsigned mtcc = curr_mtcc(lsenv->cpu_state);
    pthread_mutex_lock(&external_tu_lock);
    ExternalTUStub *stub = external_tu_alloc_locked(mtcc, tb->tc.ptr);
    pthread_mutex_unlock(&external_tu_lock);
    IR2_OPND done, tbptr;
    uint64_t check;
    int nr;

    if (!stub) {
        return NULL;
    }
    stub->target = (uintptr_t)target;
    stub->trace = getenv("LATS_EXTERNAL_TU_TRACE") != NULL;
    scratch.mtcc_id = mtcc;
    latxs_tr_sync_region_offset(mtcc);
    memcpy(scratch.tcg_region_offset, saved->tcg_region_offset,
           sizeof(scratch.tcg_region_offset));
    lsenv->tr_data = &scratch;
    latxs_tr_init(NULL);
    tbptr = latxs_ra_alloc_dbt_arg1();
    done = la_new_label();
    if (stub->trace) {
        external_tu_count(&stub->executions);
    }
    /* Load the full source pointer: an external PC cannot use TB-relative
     * address materialization or AOT relocation records. */
    la_imm(&tbptr, (uintptr_t)tb);
    if (hamt_enabled()) {
        latxs_tr_dmw_in_fix_mem(&tbptr);
    }
    if (tucp_generation_enabled()) {
        ExitTBEmitState state = { 0 };
        IR2_OPND slow = la_new_label();
        state.tu_generation_fallback = slow;
        la_out2i(LISA_LD_D, &la_a0, &tbptr,
                 TB_TU_LINK_DATA_OFFSET(next_tu_generation_offset[0]) + 8 * n);
        if (tucp_generation_counter()) {
            latxs_tr_exit_tb_emit_tu_counter_cache_check(
                    &state, &la_a0, &la_a1, &la_a2);
        } else {
            latxs_tr_exit_tb_emit_tu_memset_cache_check(
                    &state, &la_a0, &la_a1, &la_a2);
        }
        la_out1(LISA_B, &done);
        la_label_name(slow, "external TU generation miss");
    }
    check = n ? GET_SC_TABLE_OFFSET(mtcc, tu_cp_check_n1_mmu_user,
                                    scratch.tcg_region_offset) :
                GET_SC_TABLE_OFFSET(mtcc, tu_cp_check_n0_mmu_user,
                                    scratch.tcg_region_offset);
    la_imm(&la_a0, check);
    if (hamt_enabled()) {
        latxs_tr_dmw_in_fix_mem(&la_a0);
    }
    la_out2i(LISA_JIRL, &la_ra, &la_a0, 0);
    la_label_name(done, "external TU validated");
    if (stub->trace) {
        external_tu_count(&stub->successes);
    }
    la_imm(&la_a0, (uintptr_t)&stub->target);
    if (hamt_enabled()) {
        latxs_tr_dmw_in_fix_mem(&la_a0);
    }
    la_out2i(LISA_LD_D, &la_a0, &la_a0, 0);
    if (hamt_enabled()) {
        latxs_tr_dmw_in_fix_mem(&la_a0);
    }
    la_out2i(LISA_JIRL, &la_zero, &la_a0, 0);
    lsassert(scratch.ir2_asm_nr <= ARRAY_SIZE(stub->code));
    nr = latxs_tr_ir2_assemble(stub->code);
    latxs_tr_fini();
    mm_free(scratch.ir2_array);
    lsenv->tr_data = saved;
    if (nr < 0) {
        return NULL;
    }
    flush_idcache_range((uintptr_t)stub->code, (uintptr_t)stub->code, nr * 4);
    tb->jit_tucheck_external[n] = stub;
    if (stub->trace) {
        fprintf(stderr, "[External TU] linked source=%" PRIx64
                " exit=%d target=%p stub=%p instructions=%d\n",
                (uint64_t)tb->pc, n, target, stub->code, nr);
    }
    return stub->code;
}
#endif
#endif

#ifdef CONFIG_LATX_TU
static bool tu_hras_return_adjacent(TranslationBlock *tb, IR1_INST *branch)
{
#ifdef CONFIG_SOFTMMU
    TranslationBlock *next = tb->s_data->next_tb[TU_TB_INDEX_NEXT];

    return option_tu_hras && option_rastoptp && option_soft_ras &&
        lsenv->tr_data->ras_tp_has_call && tb_is_tu(tb) &&
        (latxs_hras_jit_translate(tb) ||
         latxs_hras_aot_translate()) &&
        (tb->flags & HF_CS64_MASK) &&
        (tb->flags & HF_CPL_MASK) == HF_CPL_MASK && !tb->cs_base &&
        ir1_is_call(branch) && !ir1_is_indirect_inst(branch) &&
        latxs_ir1_data_size(branch) != 16 &&
        next && next->icount && next->pc == ir1_addr_next(branch) &&
        tb_is_adjacent(tb, next);
#else
    return false;
#endif
}

static bool tu_hras_checked_call_eligible(const ExitTBContext *ctx,
                                          IR1_INST *branch, int succ_id)
{
    return !succ_id && ctx->allow_tb_link &&
        !(ctx->curr_tb->bool_flags & IS_HRAS_CALL) &&
        tu_hras_return_adjacent(ctx->curr_tb, branch);
}

static bool tu_hras_call(TranslationBlock *tb, IR1_INST *branch)
{
#ifdef CONFIG_SOFTMMU
    TranslationBlock *target;

    if (!tu_hras_return_adjacent(tb, branch)) return false;
    /* Compact and not-yet-created targets retain TUCHECK and use the fixed
     * slot behind its gate. Reusing the unchecked fast path for those sites
     * replaces formerly one-word calls with cold two-word layouts. */
    if (!tucp_fast_tu_direct() && tu_tree_lookup(ir1_target_addr(branch))) {
        return false;
    }
    target = tb_htable_lookup_noexit(env_cpu(lsenv->cpu_state),
            ir1_target_addr(branch), tb->cs_base, tb->flags,
            tb_cflags(tb) & ~CF_INVALID);
    if (!target) {
        return false;
    }
    return latxs_hras_call_target_needs_long(tb, target,
                                             get_current_inst_addr(tb),
                                             false);
#else
    return false;
#endif
}

static bool latxs_tr_exit_tb_tu_cold_exit_eligible(
        const ExitTBContext *ctx,
        const ExitTBEmitState *state, int succ_id)
{
    TranslationBlock *tb = ctx->curr_tb;

    return tb_is_tu(tb) &&
        tb->s_data->tu_cold_exit_offset[succ_id] == TU_COLD_EXIT_DIRECT_PENDING &&
        TBLINK_STUB_MODE(state->tblink_stub_mode) == TBLINK_STUB_DIRECT &&
        ctx->allow_tb_link && !ctx->target_is_indirect &&
        !ctx->target_crosses_page &&
        !ctx->target_maybe_cross_page_block;
}

#ifdef CONFIG_SOFTMMU
static bool tu_hras_emit_local_irq_branch(const ExitTBContext *ctx,
        const ExitTBEmitState *state, IR1_INST *branch, int succ_id)
{
    TranslationBlock *tb = ctx->curr_tb;

    /* The caller has already selected an owner-local TU cold exit. */
    if (!option_tu_hras || !option_rastoptp || !option_soft_ras ||
        in_aot_translate || option_aot_save ||
        succ_id != TU_TB_INDEX_TARGET || !ctx->target_in_current_tu ||
        !state->emitted_interrupt_check || !ir1_is_branch(branch) ||
        !(tb->flags & HF_CS64_MASK) || tb->cs_base ||
        (tb->flags & HF_CPL_MASK) != HF_CPL_MASK) {
        return false;
    }
    /* Keep the IRQ check, but reuse the TU's fixed local edge rather than
     * an 8-byte relink slot whose alignment NOP can enter a hot loop. */
    tb->s_data->tu_jmp[TU_TB_INDEX_TARGET] =
        get_current_inst_addr(tb) - (uintptr_t)tb->tc.ptr;
    latxs_append_ir2_opnda(LISA_B, 0);
    return true;
}
#endif

static void latxs_tr_gen_exit_tb_j_tb_link_cold(TranslationBlock *tb,
        int succ_id, IR2_OPND *cold_exit_label)
{
    latxs_tb_link_align_inside_cacheline(tb);

    IR2_OPND goto_label_opnd = latxs_ir2_opnd_new_label();
    latxs_append_ir2_opnd1(LISA_LABEL, &goto_label_opnd);
    tb->jmp_reset_offset[succ_id] = goto_label_opnd.val;
    tb->link_opt[succ_id] = 1;

    latxs_append_ir2_opnd1(LISA_B, cold_exit_label);
    latxs_append_ir2_opnd0_(lisa_nop);
}
#endif

static void latxs_tr_exit_tb_emit_link_stub(const ExitTBContext *ctx,
        ExitTBEmitState *state, IR2_OPND *tbptr, ADDR tb_addr,
        IR1_INST *branch, int succ_id)
{
    if (!ctx->allow_tb_link || ctx->target_is_indirect) {
        return;
    }

    switch (TBLINK_STUB_MODE(state->tblink_stub_mode)) {
    case TBLINK_STUB_DIRECT:
#ifdef CONFIG_LATX_TU
#ifdef CONFIG_SOFTMMU
        if (!succ_id && ctx->target_in_current_tu) {
            latxs_tr_exit_tb_set_cp_label(ctx->curr_tb, succ_id,
                    "same TU CALL link entry");
            if (tu_hras_emit_call_slot(ctx, state, succ_id)) {
                break;
            }
            ctx->curr_tb->jmp_cp_data[succ_id] = 0;
        }
#endif
        if (latxs_tr_exit_tb_tu_cold_exit_eligible(
                ctx, state, succ_id)) {
            IR2_OPND cold_exit_label = latxs_ir2_opnd_new_label();
            ctx->curr_tb->s_data->tu_cold_exit_offset[succ_id] = cold_exit_label.val;
            state->deferred_tu_cold_exit = true;
            la_label_name(cold_exit_label, "TU direct cold exit");
#ifdef CONFIG_SOFTMMU
            if (!tu_hras_emit_local_irq_branch(ctx, state, branch, succ_id))
#endif
            {
                latxs_tr_gen_exit_tb_j_tb_link_cold(
                        ctx->curr_tb, succ_id, &cold_exit_label);
            }
            if (state->emitted_interrupt_check) {
                latxs_ir2_interrupt_set_label(ctx->curr_tb, succ_id);
                latxs_append_ir2_opnd1(LISA_B, &cold_exit_label);
            }
            break;
        }
#endif
        if (option_link_opt && !state->emitted_interrupt_check &&
            !(ctx->tr_ctx->eflag_tb_defer.valid && hamt_enabled() &&
              flag_reduction_tb_hamt_recover())) {
            IR2_OPND label = la_new_label();

            latxs_tb_link_align_inside_cacheline(ctx->curr_tb);
            la_label_name(label, "tb link stub opt");
            ctx->curr_tb->jmp_reset_offset[succ_id] = label.val;
            ctx->curr_tb->link_opt[succ_id] = 1;
        } else {
            latxs_tr_gen_exit_tb_j_tb_link(ctx->curr_tb, succ_id);
        }
        state->link_slot_emitted = true;
        break;
#ifdef CONFIG_SOFTMMU
    case TBLINK_STUB_CPCHECK:
        if (TBLINK_STUB_NEED_CPLABEL(state->tblink_stub_mode)) {
            latxs_tr_exit_tb_set_cp_label(ctx->curr_tb, succ_id,
                    "label for cross-page tb (mode cpcheck)");
        }
        state->loaded_tb_addr = 1;
        latxs_tr_exit_tb_emit_cp_check_stub(ctx, state, tbptr, tb_addr,
                                            branch, succ_id);
        break;
    case TBLINK_STUB_TUCHECK:
#ifdef CONFIG_LATX_TU
        if (succ_id == 0 &&
            tu_hras_call(ctx->curr_tb, branch)) {
            IR2_OPND cold = la_new_label();
            IR2_OPND hot = la_new_label();

            latxs_tb_link_align_inside_cacheline(ctx->curr_tb);
            la_label(hot);
            ctx->curr_tb->bool_flags |= IS_HRAS_CALL;
            ctx->curr_tb->jmp_reset_offset[succ_id] = hot.val;
            ctx->curr_tb->jmp_insn_offset[succ_id] = 8;
            ctx->curr_tb->link_opt[succ_id] = 1;
            ctx->curr_tb->jmp_cp_data[succ_id] = 0;
            ctx->curr_tb->s_data->tu_cold_exit_offset[succ_id] = cold.val;
            state->deferred_tu_cold_exit = true;
            /* The final link instruction is immediately before return TB. */
            la_out1(LISA_B, &cold);
            la_out_nop();
            break;
        }
#endif
        latxs_tr_exit_tb_set_cp_label(ctx->curr_tb, succ_id,
                "label for TU check bypass");
#ifdef CONFIG_LATX_TU
        if (tu_hras_checked_call_eligible(ctx, branch, succ_id)) {
            ctx->curr_tb->s_data->has_hras_check_gate = true;
            la_out_nop();
        }
#endif
        latxs_tr_exit_tb_emit_tu_check_stub(ctx, state, tbptr, tb_addr,
                                            succ_id);
        break;
#endif
    default:
        break;
    }
}

static void latxs_tr_exit_tb_save_direct_next_pc(const ExitTBContext *ctx,
        const ExitTBEmitState *state, IR1_INST *branch, int succ_id)
{
    if (option_lazypc) {
        if (succ_id || ir1_is_call(branch)) {
            ctx->curr_tb->next_pc[succ_id] = ir1_target_addr(branch);
        } else {
            ctx->curr_tb->next_pc[succ_id] = ir1_addr_next(branch);
        }
        int neips = ir1_opnd_size(ir1_get_opnd(branch, 0));
        if (!ctx->tr_ctx->sys.code64 && neips != 64) {
            ctx->curr_tb->next_pc[succ_id] &= ((1ULL << neips) - 1ULL);
        }
        return;
    }

    if (!state->saved_next_eip || state->emitted_interrupt_check) {
        tr_gen_branch_save_next_eip(branch, succ_id);
    }
}

static void latxs_tr_exit_tb_emit_direct_action(const ExitTBContext *ctx,
        const ExitTBEmitState *state, IR2_OPND *tbptr, ADDR tb_addr,
        IR1_INST *branch, int succ_id)
{
    if (state->deferred_tu_cold_exit) {
#ifdef CONFIG_LATX_TU
        HrasDirectCall *call = ctx->tr_ctx->hras_direct_call;
        if (call) {
            call->state = *state;
            call->state.deferred_tu_cold_exit = false;
        }
#endif
        if (option_lazypc) {
            latxs_tr_exit_tb_save_direct_next_pc(ctx, state, branch, succ_id);
        }
        return;
    }

    latxs_tr_shbr_restore_zero64(
        branch->shbr_restore_zero64_exit[succ_id]);
    latxs_tr_exit_tb_save_direct_next_pc(ctx, state, branch, succ_id);

    if ((ctx->allow_tb_link && !state->loaded_tb_addr) || state->emitted_interrupt_check) {
        latxs_tr_gen_exit_tb_load_tb_addr(tbptr, tb_addr);
    }
    if (option_lazypc && !ctx->allow_tb_link) {
        tr_gen_branch_save_next_eip(branch, succ_id);
    }
    latxs_tr_gen_exit_tb_j_context_switch(tbptr, ctx->allow_tb_link, succ_id);
#ifdef CONFIG_SOFTMMU
    latxs_tr_exit_tb_emit_cp_inline_p0_fallback(ctx, state, branch, succ_id);
#ifdef CONFIG_LATX_TU
    latxs_tr_exit_tb_emit_tu_generation_fallback(ctx, state, tbptr, tb_addr,
                                                 succ_id);
#endif
#endif
}

typedef enum ExitTBContextSwitchAction {
    EXIT_TB_CS_ACTION_UNDEF = 0,
    EXIT_TB_CS_ACTION_DEFAULT,
    EXIT_TB_CS_ACTION_INDIR,
} ExitTBContextSwitchAction;

static ExitTBContextSwitchAction latxs_tr_exit_tb_get_cs_action(
        const ExitTBContext *ctx, IR1_INST *branch)
{
    if (ir1_is_branch(branch)) {
        return EXIT_TB_CS_ACTION_DEFAULT;
    }
    if (ctx->branch_opcode == X86_INS_RET) {
        return EXIT_TB_CS_ACTION_INDIR;
    }
    if (ir1_is_call(branch)) {
        return ir1_is_indirect_call(branch) ?
                EXIT_TB_CS_ACTION_INDIR :
                EXIT_TB_CS_ACTION_DEFAULT;
    }
    if (ir1_is_jump(branch)) {
        return ir1_is_indirect_jmp(branch) ?
                EXIT_TB_CS_ACTION_INDIR :
                EXIT_TB_CS_ACTION_DEFAULT;
    }
    return EXIT_TB_CS_ACTION_UNDEF;
}

static void latxs_tr_emit_aot_ijmp_observe(const ExitTBContext *ctx,
                                           IR1_INST *branch)
{
#ifdef CONFIG_SOFTMMU
    if (option_aot_save && !in_aot_translate && aot_tu_enabled() &&
        ctx->branch_opcode == X86_INS_JMP && ir1_is_indirect_jmp(branch) &&
        latxs_ir1_data_size(branch) != 16 &&
        (ctx->curr_tb->flags & HF_CS64_MASK) && !ctx->curr_tb->cs_base &&
        (ctx->curr_tb->flags & HF_CPL_MASK) == HF_CPL_MASK &&
        tb_page_addr0(ctx->curr_tb) != -1 &&
        tb_page_addr1(ctx->curr_tb) == -1) {
        IR2_OPND skip_obs = la_new_label();
        IR2_OPND key_reg = latxs_ra_alloc_itemp();
        IR2_OPND idx_reg = latxs_ra_alloc_itemp();
        IR2_OPND tbl_reg = latxs_ra_alloc_itemp();
        uint64_t jmp_pc = ir1_addr(branch);
        uint64_t site_key = ((uint64_t)(tb_page_addr0(ctx->curr_tb) &
                                        TARGET_PAGE_MASK) *
                             UINT64_C(0x9e3779b185ebca87)) ^
                            (jmp_pc * UINT64_C(0xc2b2ae3d27d4eb4f));
        if (!site_key) {
            site_key = 1;
        }
#ifdef CONFIG_LATX_OPT_NOFP
        helper_cfg_t cfg = nofp_helper_cfg;
#else
        helper_cfg_t cfg = default_helper_cfg;
#endif
        la_imm(&key_reg, (ADDR)site_key);
        la_out3(LISA_XOR, &key_reg, &key_reg, &latxs_arg1_ir2_opnd);
        la_out2i(LISA_SRLI_D, &idx_reg, &key_reg, LATX_AOT_IJMP_SEEN_BITS);
        la_out3(LISA_XOR, &idx_reg, &idx_reg, &key_reg);
        la_out2ii(LISA_BSTRPICK_D, &idx_reg, &idx_reg,
                  LATX_AOT_IJMP_SEEN_BITS - 1, 0);
        la_out2i(LISA_SLLI_D, &idx_reg, &idx_reg, 3);
        la_imm(&tbl_reg, (ADDR)latx_aot_ijmp_seen);
        if (hamt_enabled()) {
            latxs_tr_dmw_in_fix_mem(&tbl_reg);
        }
        la_out3(LISA_ADD_D, &tbl_reg, &idx_reg, &tbl_reg);
        la_out2i(LISA_LD_D, &idx_reg, &tbl_reg, 0);
        la_out3(LISA_BEQ, &idx_reg, &key_reg, &skip_obs);
        latxs_ra_free_temp(&tbl_reg);
        latxs_ra_free_temp(&idx_reg);
        latxs_ra_free_temp(&key_reg);

        latxs_tr_gen_intb_sync_gpc();
        latxs_tr_gen_call_to_helper_prologue_cfg(cfg);
        la_out2_(lisa_mov, &la_a0, &la_env);
        la_imm(&la_a1, (ADDR)ctx->curr_tb);
        la_imm(&la_a2, (ADDR)jmp_pc);
        if (hamt_enabled()) {
            latxs_tr_dmw_out_fix_mem(&la_a0);
            latxs_tr_dmw_out_fix_mem(&la_a1);
        }
        la_imm(&la_ra, (ADDR)latx_helper_observe_indirect_jmp);
        la_out1_(lisa_call, &la_ra);
        latxs_tr_gen_call_to_helper_epilogue_cfg(cfg);
        latxs_tr_gen_load_from_curr_eip(&latxs_arg1_ir2_opnd);
        la_label(skip_obs);
    }
#else
    (void)ctx;
    (void)branch;
#endif
}

#ifdef CONFIG_LATX_TU
static bool latxs_tr_exit_tb_use_tu_indirect_inline_exit(
        const ExitTBContext *ctx, IR1_INST *branch, int succ_id)
{
    TranslationBlock *tb = ctx->curr_tb;
    bool supported = ir1_is_indirect_call(branch) ||
        ir1_is_indirect_jmp(branch);

    if (!(tb_is_tu(tb) &&
          tb->s_data->tu_cold_exit_offset[succ_id] ==
              TU_INDIRECT_EXIT_INLINE_PENDING &&
          latxs_check_interrupt_at_block_end() &&
          ctx->tr_ctx->opt_can_link && ctx->tr_ctx->opt_can_indir_link &&
          supported)) {
        return false;
    }

    tb->s_data->tu_cold_exit_offset[succ_id] = TU_COLD_EXIT_INVALID;
    return true;
}
#endif

static void latxs_tr_exit_tb_emit_indirect_context_switch(
        const ExitTBContext *ctx, int succ_id)
{
    (void)ctx;
    latxs_tr_gen_intb_sync_gpc();
    latxs_tr_gen_exit_tb_j_context_switch(NULL, 0, succ_id);
}

static bool latxs_tr_exit_tb_outline_irq(const ExitTBContext *ctx)
{
    TranslationBlock *tb = ctx->curr_tb;
    unsigned mask = ctx->branch_opcode == X86_INS_JMP ? 1 : 2;

    return (option_irq_outline & mask) && option_tu_hras == 2 &&
           !in_aot_translate && !option_aot_save &&
           (tb->flags & HF_CS64_MASK) && !tb->cs_base &&
           (tb->flags & HF_CPL_MASK) == HF_CPL_MASK;
}

static void latxs_tr_exit_tb_emit_indirect_interrupt(const ExitTBContext *ctx,
        int succ_id, bool inline_tu_exit, IR2_OPND *njc_miss, int fsht_id)
{
    if (ctx->branch_opcode == X86_INS_RET) {
        return;
    }

    if (latxs_check_interrupt_at_block_end()) {
        IR2_OPND intop = latxs_ra_alloc_itemp();

        latxs_load_intcheck_to_reg(&intop);
        if (inline_tu_exit) {
            if (latxs_tr_exit_tb_outline_irq(ctx)) {
                HrasIndirectCallLabels *labels = &ctx->tr_ctx->hras_indirect_call;
                if (fsht_id >= 0) {
                    labels->fsht_deferred = true;
                    labels->fsht_fast = tucp_fast_fsht_check();
                    labels->fsht_id = fsht_id;
                    labels->fsht = la_new_label();
                    *njc_miss = labels->fsht;
                } else if (njc_miss) {
                    *njc_miss = labels->miss;
                }
                labels->irq_deferred = true;
                labels->irq_succ_id = succ_id;
                labels->irq = la_new_label();
                /* The regular path falls through. Cold labels are resolved
                 * after all hot TU members, with signed branch range checks. */
                la_out3(LISA_BLT, &intop, &la_zero, &labels->irq);
                latxs_ra_free_temp(&intop);
                return;
            }
            IR2_OPND lookup_label = latxs_ir2_opnd_new_label();

            latxs_append_ir2_opnd3(LISA_BGE, &intop, &la_zero,
                    &lookup_label);
            latxs_ra_free_temp(&intop);
            latxs_tr_exit_tb_emit_indirect_context_switch(ctx, succ_id);
            if (fsht_id >= 0) {
                HrasIndirectCallLabels *labels = &ctx->tr_ctx->hras_indirect_call;

                /* Reuse the existing IRQ branch to skip FSHT on NJC hits. */
                gen_latxs_intb_fsht_call_miss(fsht_id,
                    tucp_fast_fsht_check() &&
                    (ctx->curr_tb->flags & HF_CPL_MASK) == HF_CPL_MASK,
                    njc_miss, &labels->miss, &labels->call);
            } else if (njc_miss) {
                /* The existing BGE skips this near miss island. NJC's
                 * conditional branches stay local even in a large TU. */
                la_label(*njc_miss);
                la_out1(LISA_B, &ctx->tr_ctx->hras_indirect_call.miss);
            }
            latxs_append_ir2_opnd1(LISA_LABEL, &lookup_label);
        } else {
            latxs_append_ir2_opnd3(LISA_BLT, &intop, &la_zero,
                    latxs_ir2_interrupt_get_label(ctx->curr_tb, succ_id));
            latxs_ra_free_temp(&intop);
        }
    } else if (option_int_opt == IRQ_CHECK_ASYNC_NOTIFY) {
        ctx->curr_tb->int_opt[succ_id] = 1;
    }
}

typedef enum NJCJumpPath {
    NJC_JUMP_SHARED,
    NJC_JUMP_SOURCE_LEAF,
    NJC_JUMP_SOURCE_INLINE,
} NJCJumpPath;

static NJCJumpPath latxs_tr_exit_tb_njc_path(const ExitTBContext *ctx, int fid)
{
    TranslationBlock *tb = ctx->curr_tb;

    if (!option_njc_source_jump || ctx->branch_opcode != X86_INS_JMP ||
        !ctx->target_is_indirect || !(tb->flags & HF_CS64_MASK) ||
        tb->cs_base || (tb->flags & HF_CPL_MASK) != HF_CPL_MASK || fid < 0) {
        return NJC_JUMP_SHARED;
    }
    if (in_aot_translate) {
        if (!option_aot_save || !aot_tu_enabled()) {
            return NJC_JUMP_SHARED;
        }
        return option_table_addr ? NJC_JUMP_SOURCE_INLINE : NJC_JUMP_SOURCE_LEAF;
    }
    /* AOT-enabled processes also translate uncached ordinary TBs. */
    if (option_aot_save || option_aot_load) {
        return NJC_JUMP_SHARED;
    }
    if (!jit_tu_requested() && option_table_addr) {
        return NJC_JUMP_SOURCE_INLINE;
    }
    return option_tu_hras == 2 ? NJC_JUMP_SOURCE_LEAF : NJC_JUMP_SHARED;
}

static void latxs_tr_exit_tb_emit_indirect_lookup(const ExitTBContext *ctx,
                                                 bool hras_call)
{
    /* The successful softRAS return has already jumped away. Match the
     * complete ras_tp_pop admission: only this RET miss has restored the
     * outer SP, so it may start an independent native ABI entry. */
    if (ctx->branch_opcode == X86_INS_RET && option_soft_ras &&
        option_rastoptp && aot_tu_enabled() && hamt_enabled() &&
        hamt_mode_dmw() && option_int_opt == IRQ_CHECK_ASYNC_NOTIFY &&
        !(ctx->tr_ctx->sys.flags & HF_INHIBIT_IRQ_MASK) &&
        (ctx->curr_tb->flags & HF_CS64_MASK) && !ctx->curr_tb->cs_base &&
        (ctx->curr_tb->flags & HF_CPL_MASK) == HF_CPL_MASK &&
        latxs_ir1_data_size(ctx->tr_ctx->curr_ir1_inst) != 16) {
        tb_sc_table_offset_jmp(ctx->curr_tb, ctx->curr_mtcc_id,
                intb_resume_lookup, ctx->tr_ctx->tcg_region_offset,
                ctx->tr_ctx->ir2_asm_nr << 2, 0, INTB_RESUME_LOOKUP);
        return;
    }
    int offset;
    int fid = fsht_flags_to_id(ctx->curr_tb->flags);
    int link = hras_call && option_tu_hras == 2;
    /* A guest tail jump preserves the caller's softRAS entry and host SP.
     * The native callee restores its own frame and returns through that
     * entry, just as it does after a CALL. Select its ABI entry without
     * adding another return address or unwinding the caller's frame. */
    bool native_entry = ((ctx->branch_opcode == X86_INS_CALL &&
                          ctx->tr_ctx->ras_tp_has_call) ||
                         (ctx->branch_opcode == X86_INS_JMP &&
                          ir1_is_indirect_jmp(ctx->tr_ctx->curr_ir1_inst))) &&
        option_soft_ras && option_rastoptp &&
        aot_tu_enabled() && hamt_enabled() &&
        option_int_opt == IRQ_CHECK_ASYNC_NOTIFY &&
        latx_jmp_cache_compact_enabled() &&
        (ctx->curr_tb->flags & HF_CS64_MASK) && !ctx->curr_tb->cs_base;

    /* Inline CALLs use the dedicated miss path below: their NJC slot lives
     * in ra until lookup finishes. Without inline NJC, link this existing B
     * exactly once and leave the shared lookup's final JIRL as a tail jump. */
    latxs_tr_emit_inline_njc_lookup(ctx->curr_tb, native_entry);
    offset = ctx->tr_ctx->ir2_asm_nr << 2;

    NJCJumpPath path = latxs_tr_exit_tb_njc_path(ctx, fid);
    switch (path) {
    case NJC_JUMP_SOURCE_INLINE:
        gen_latxs_intb_lookup_njc_inline(true, option_table_addr);
        break;
    case NJC_JUMP_SOURCE_LEAF: {
        IR2_OPND miss = la_new_label();
        g_assert(!link);
        g_assert(GET_SC_TABLE(ctx->curr_mtcc_id, intb_njc_return));
        tb_sc_table_offset_jmp(ctx->curr_tb, ctx->curr_mtcc_id,
                intb_njc_return, ctx->tr_ctx->tcg_region_offset,
                offset, 1, INTB_NJC_RETURN);
        /* The miss return skips exactly this one instruction. */
        la_out2i(LISA_JIRL, &la_zero, &la_a0, 0);
        la_label(miss);
        break;
    }
    case NJC_JUMP_SHARED:
        break;
    }
    offset = ctx->tr_ctx->ir2_asm_nr << 2;

    if (path == NJC_JUMP_SOURCE_INLINE) {
        tb_sc_table_offset_jmp(ctx->curr_tb, ctx->curr_mtcc_id,
                intb_fsht_after_njc[fid], ctx->tr_ctx->tcg_region_offset,
                offset, 0, INTB_FSHT_AFTER_NJC);
    } else if (option_flaged_sht && fid >= 0) {
        if (tucp_fast_fsht_check() &&
            (ctx->curr_tb->flags & HF_CPL_MASK) == HF_CPL_MASK) {
            if (native_entry) {
                tb_sc_table_offset_jmp(ctx->curr_tb, ctx->curr_mtcc_id,
                    intb_fsht_call_lookup_tucpfast[fid],
                    ctx->tr_ctx->tcg_region_offset, offset, link,
                    INTB_FSHT_CALL_LOOKUP_TUCPFAST);
            } else tb_sc_table_offset_jmp(ctx->curr_tb, ctx->curr_mtcc_id,
                    intb_fsht_lookup_tucpfast[fid],
                    ctx->tr_ctx->tcg_region_offset, offset, link,
                    INTB_FSHT_LOOKUP_TUCPFAST);
        } else {
            tb_sc_table_offset_jmp(ctx->curr_tb, ctx->curr_mtcc_id,
                    intb_fsht_lookup[fid], ctx->tr_ctx->tcg_region_offset,
                    offset, link, INTB_FSHT_LOOKUP);
        }
    } else {
        if (!intb_njc_inline_enabled() &&
            latx_jmp_cache_compact_enabled() &&
            (ctx->curr_tb->flags & HF_CS64_MASK)) {
            tb_sc_table_offset_jmp(ctx->curr_tb, ctx->curr_mtcc_id,
                    intb_lookup_compact, ctx->tr_ctx->tcg_region_offset,
                    offset, link, INTB_LOOKUP_COMPACT);
        } else {
            tb_sc_table_offset_jmp(ctx->curr_tb, ctx->curr_mtcc_id,
                    intb_lookup, ctx->tr_ctx->tcg_region_offset,
                    offset, link, INTB_LOOKUP);
        }
    }
}

static void latxs_tr_exit_tb_emit_indirect_slowpath(const ExitTBContext *ctx,
        int succ_id)
{
    if (!latxs_check_interrupt_at_block_end()) {
        return;
    }

    if (ctx->branch_opcode != X86_INS_RET) {
        latxs_ir2_interrupt_set_label(ctx->curr_tb, succ_id);
        latxs_tr_exit_tb_emit_indirect_context_switch(ctx, succ_id);
    }
#ifndef CONFIG_RAS_DISABLE_CPL0
    {
        TranslationBlock *tb = ctx->tr_ctx->curr_tb;

        if (!(tb->flags & HF_CPL_MASK) && ctx->branch_opcode == X86_INS_RET) {
            latxs_ir2_interrupt_set_label(tb, succ_id);
            latxs_tr_exit_tb_emit_indirect_context_switch(ctx, succ_id);
        }
    }
#endif
}

static void latxs_tr_exit_tb_emit_indirect_action(const ExitTBContext *ctx,
        const ExitTBEmitState *state, IR1_INST *branch, int succ_id)
{
    if (!state->saved_next_eip) {
        tr_gen_branch_save_next_eip(branch, succ_id);
    }

    if (!ctx->tr_ctx->opt_can_link || !ctx->tr_ctx->opt_can_indir_link) {
        latxs_tr_exit_tb_emit_indirect_context_switch(ctx, succ_id);
        return;
    }

#ifdef CONFIG_LATX_TU
    bool inline_tu_exit =
        latxs_tr_exit_tb_use_tu_indirect_inline_exit(
            ctx, branch, succ_id);
#else
    bool inline_tu_exit = false;
#endif

    IR2_OPND njc_miss;
    bool hras_call = false;
    bool inline_hras_call = false;
    int hras_fsht_id = -1;
#if defined(CONFIG_LATX_TU) && defined(CONFIG_SOFTMMU)
    hras_call = option_tu_hras && option_rastoptp && option_soft_ras &&
        (latxs_hras_jit_translate(ctx->curr_tb) ||
         latxs_hras_aot_translate()) &&
        ir1_is_indirect_call(branch) && latxs_ir1_data_size(branch) != 16 &&
        (ctx->curr_tb->flags & HF_CS64_MASK) && !ctx->curr_tb->cs_base &&
        (ctx->curr_tb->flags & HF_CPL_MASK) == HF_CPL_MASK &&
        ctx->tr_ctx->ras_tp_has_call;
    if (latxs_hras_aot_translate() && !inline_tu_exit) {
        /* No adjacent continuation was prepared for this cold source. */
        hras_call = false;
    }
    /* Keep the link at the CALL site so the following return block is
     * the hardware RAS prediction. Other indirect exits keep their mode. */
    inline_hras_call = hras_call && option_intb_njc &&
        (!latxs_hras_aot_translate() || inline_tu_exit);
    if (inline_hras_call) {
        ctx->tr_ctx->hras_indirect_call = (HrasIndirectCallLabels) {
            .valid = true,
            .miss = la_new_label(),
            .call = la_new_label(),
        };
        if (option_flaged_sht) {
            hras_fsht_id = fsht_flags_to_id(ctx->curr_tb->flags);
        }
        njc_miss = inline_tu_exit ? la_new_label() :
                   ctx->tr_ctx->hras_indirect_call.miss;
    }
#endif
    latxs_tr_exit_tb_emit_indirect_interrupt(
            ctx, succ_id, inline_tu_exit,
            inline_hras_call ? &njc_miss : NULL,
            inline_tu_exit ? hras_fsht_id : -1);
    if (inline_hras_call) {
        HrasIndirectCallLabels *labels = &ctx->tr_ctx->hras_indirect_call;
        if (hras_fsht_id >= 0 && inline_tu_exit) {
            if (labels->fsht_deferred) {
                gen_latxs_intb_fsht_call_njc_save(hras_fsht_id,
                    &njc_miss, &labels->call, &labels->target,
                    labels->fsht_scratch);
            } else {
                gen_latxs_intb_fsht_call_njc(hras_fsht_id,
                    &njc_miss, &labels->call, &labels->target);
            }
        } else if (hras_fsht_id >= 0) {
            gen_latxs_intb_fsht_call(hras_fsht_id,
                tucp_fast_fsht_check() &&
                (ctx->curr_tb->flags & HF_CPL_MASK) == HF_CPL_MASK,
                &labels->miss, &labels->call, &labels->target);
        } else {
            gen_latxs_intb_lookup_njc_call(latx_jmp_cache_compact_enabled(),
                    &njc_miss, &labels->call, &labels->target);
        }
        if (!inline_tu_exit) {
            latxs_tr_exit_tb_emit_indirect_slowpath(ctx, succ_id);
        }
        return;
    }
    latxs_tr_exit_tb_emit_indirect_lookup(ctx, hras_call);
    if (!inline_tu_exit) {
        latxs_tr_exit_tb_emit_indirect_slowpath(ctx, succ_id);
    }
}

static void latxs_tr_exit_tb_prepare(ExitTBContext *ctx,
        ExitTBEmitState *state, IR2_OPND *tbptr, ADDR tb_addr,
        IR1_INST *branch, int succ_id)
{
    /* debug support : when hit break point, do NOT TB-link */
    if (cpu_hit_breakpoint()) {
        ctx->allow_tb_link = 0;
    }

#ifdef CONFIG_LATX_PROFILER
    if (!ctx->tr_ctx->is_tu_mode)
    {
        la_out1(LISA_BL, &(ctx->tr_ctx->tbprofile_rdtime_label));
    }
#endif

    /*
     * <A> EOB end-of-tb worker
     *
     * for branch instructions, EOB will be called before branch
     * since it contains two exit ports.
     */
    if (!ir1_is_branch(branch)) {
        latxs_tr_gen_eob();
    }

    /* situation for generate update pcrel
     *        | branch| jmp  | call | ret | ljmp|
     * ---------------------------------------------
     *   dir  |   1   |  1   |   1  | 0   |  1  |
     *  indir |   0   |  1   |   1  | 1   |  0  |
     *   next |              |   1  |
     * for direct, get target here and st to neip
     * for indirect, mov target to sp in tr_in
     */
    if ((tb_cflags(ctx->curr_tb) & CF_PCREL)) {
        latxs_tr_pcrel_eip(branch, succ_id);
    }

    ctx->target_is_indirect = ir1_is_indirect_inst(branch);

#ifdef CONFIG_SOFTMMU
    ctx->target_crosses_page = branch_is_cross_page(branch, succ_id);
    ctx->target_maybe_cross_page_block = branch_target_maybe_cross_page_tb(branch, succ_id);
    if (ctx->target_in_current_tu) {
        ctx->target_crosses_page = false;
        ctx->target_maybe_cross_page_block = false;
    }
#endif

    if (ctx->target_crosses_page) {
        add_misc_flags(ctx->curr_tb, succ_id ? MF_NTB_CROSSPAGE_1 : MF_NTB_CROSSPAGE_0);
    }

    latxs_tr_exit_tb_prepare_direct_interrupt(ctx, state, branch, succ_id);

    state->tblink_stub_mode = latxs_tr_exit_tb_select_link_stub_mode(ctx);
    latxs_tr_exit_tb_emit_link_stub(ctx, state, tbptr, tb_addr, branch, succ_id);

    if (state->emitted_interrupt_check &&
        !state->deferred_tu_cold_exit) {
        latxs_ir2_interrupt_set_label(ctx->curr_tb, succ_id);
    }

    /*
     * sysflagtb: the direct-link slot was emitted above, and the interrupt
     * label (if any) now points at the current position, so both the
     * link-miss fall-through and the pending-IRQ branch reach this
     * materialisation before the context switch / any C observer.  A taken
     * direct link branches over it.
     */
    latxs_tr_tb_eflag_emit_deferred(ctx->curr_tb, succ_id, state);
}

static void latxs_tr_exit_tb_finish(const ExitTBContext *ctx,
        ExitTBEmitState *state, IR2_OPND *tbptr, ADDR tb_addr,
        IR1_INST *branch, int succ_id)
{
    switch (latxs_tr_exit_tb_get_cs_action(ctx, branch)) {
    case EXIT_TB_CS_ACTION_DEFAULT: /* 直接跳转 */
        latxs_tr_exit_tb_emit_direct_action(ctx, state, tbptr, tb_addr,
                                            branch, succ_id);
        break;
    case EXIT_TB_CS_ACTION_INDIR:
        latxs_tr_emit_aot_ijmp_observe(ctx, branch);
        latxs_tr_exit_tb_emit_indirect_action(ctx, state, branch, succ_id);
        break;
    default:
        lsassertm(0, "not implement.\n");
        break;
    }
}

#ifdef CONFIG_LATX_TU
static void tu_call_cold_reloc_begin(TranslationBlock *tb,
                                    int *main_start, int *main_end)
{
    *main_start = TB_REL_START(tb);
    *main_end = TB_REL_END(tb);
    if (in_aot_translate) {
        curr_rel_entry_num = 0;
    }
}

static void tu_call_cold_reloc_end(TranslationBlock *tb,
                                  int main_start, int main_end)
{
    if (in_aot_translate) {
        append_tu_ras_rel_entries(tb, main_start, main_end);
    }
}

void latxs_tr_gen_indirect_call_cold(
        HrasIndirectCallLabels labels)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    TranslationBlock *tb = td->curr_tb;
    IR2_OPND failed = la_new_label();
    int main_start = 0, main_end = 0;
    bool aot_tu_cold = in_aot_translate && tb_is_tu(tb);
#ifdef CONFIG_LATX_OPT_NOFP
    helper_cfg_t cfg = nofp_helper_cfg;
#else
    helper_cfg_t cfg = default_helper_cfg;
#endif

    if (aot_tu_cold) {
        tu_call_cold_reloc_begin(tb, &main_start, &main_end);
    }
    la_label(labels.miss);
    latxs_tr_gen_intb_sync_gpc();
    latxs_tr_gen_call_to_helper_prologue_cfg(cfg);
    la_out2_(lisa_mov, &la_a0, &la_env);
    latxs_load_addr_to_ir2(&la_a1, (ADDR)tb, LOAD_TB_FULL_ADDR);
    if (hamt_enabled()) {
        latxs_tr_dmw_out_fix_mem(&la_a0);
        latxs_tr_dmw_out_fix_mem(&la_a1);
    }
    /* The helper rechecks this source after lookup/flush before returning
     * to its unique final JIRL. AOT owns both address relocations. */
    if (option_aot_save) {
        latxs_load_addr_to_ir2(&la_ra, (ADDR)latx_helper_lookup_tb_call,
                               LOAD_HRAS_CALL_HELPER);
    } else {
        la_imm(&la_ra, (ADDR)latx_helper_lookup_tb_call);
    }
    la_out1_(lisa_call, &la_ra);
    latxs_tr_gen_call_to_helper_epilogue_cfg(cfg);

    if (option_aot_save) {
        latxs_load_addr_to_ir2(&la_a1,
                GET_SC_TABLE(td->mtcc_id, cs_native_to_bt_ret_0),
                LOAD_HRAS_CALL_RET0_ADDR);
    } else {
        la_imm(&la_a1, GET_SC_TABLE(td->mtcc_id, cs_native_to_bt_ret_0));
    }
    if (hamt_enabled()) {
        latxs_tr_dmw_in_fix_mem(&la_a0);
        latxs_tr_dmw_in_fix_mem(&la_a1);
    }
    la_out3(LISA_BEQ, &la_a0, &la_a1, &failed);
    if (labels.target.val != la_a0.val) {
        la_out2_(lisa_mov, &labels.target, &la_a0);
    }
    la_out1(LISA_B, &labels.call);
    la_label(failed);
    la_out2i(LISA_JIRL, &la_zero, &la_a0, 0);
    if (aot_tu_cold) {
        tu_call_cold_reloc_end(tb, main_start, main_end);
    }
}

void latxs_tr_gen_tu_cold_exits(uint32_t tb_num_in_tu,
        TranslationBlock **tb_list,
        const HrasIndirectCallLabels *indirect_calls,
        void *const *direct_calls)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    TranslationBlock *saved_tb = td->curr_tb;
    IR1_INST *saved_ir1 = td->ir1_inst_array;
    int saved_ignore_eip = td->ignore_eip_update;
    int saved_code64 = td->sys.code64;
    TranslationBlock **cold_order;
    uint32_t i;

    cold_order = g_new(TranslationBlock *, tb_num_in_tu);
    memcpy(cold_order, tb_list, sizeof(*cold_order) * tb_num_in_tu);
    for (i = 1; i < tb_num_in_tu; i++) {
        TranslationBlock *key = cold_order[i];
        uint32_t j = i;

        while (j > 0 && cold_order[j - 1]->pc > key->pc) {
            cold_order[j] = cold_order[j - 1];
            j--;
        }
        cold_order[j] = key;
    }

    for (i = 0; i < tb_num_in_tu; i++) {
        TranslationBlock *tb = cold_order[i];
        IR1_INST *branch = (IR1_INST *)tb->s_data->ir1 + tb->icount - 1;
        int succ_id;

        td->curr_tb = tb;
        td->ir1_inst_array = tb->s_data->ir1;
        td->ignore_eip_update = tb->s_data->ignore_eip_update;
        td->sys.code64 = (tb->flags >> HF_CS64_SHIFT) & 1;

        for (succ_id = 0; succ_id < 2; succ_id++) {
            uint32_t cold_exit = tb->s_data->tu_cold_exit_offset[succ_id];
            IR2_OPND cold_exit_label;
            IR2_OPND tbptr;
            int main_start, main_end;

            if (cold_exit == TU_COLD_EXIT_INVALID ||
                cold_exit == TU_COLD_EXIT_DIRECT_PENDING ||
                cold_exit == TU_INDIRECT_EXIT_INLINE_PENDING) {
                continue;
            }

            tu_call_cold_reloc_begin(tb, &main_start, &main_end);
            if (cold_exit == TU_COLD_EXIT_JMP_INTERRUPT_DEFERRED) {
                latxs_ir2_interrupt_set_label(tb, succ_id);
                latxs_tr_shbr_restore_zero64(
                    branch->shbr_restore_zero64_exit[succ_id]);
                tr_gen_branch_save_next_eip(branch, succ_id);
                latxs_tr_gen_exit_tb_j_context_switch(NULL, 0, succ_id);
                tb->s_data->tu_cold_exit_offset[succ_id] = TU_COLD_EXIT_INVALID;
                tu_call_cold_reloc_end(tb, main_start, main_end);
                continue;
            }

            cold_exit_label = latxs_ir2_opnd_new(
                    IR2_OPND_LABEL, cold_exit);
#ifdef CONFIG_SOFTMMU
            if (succ_id == 0 && (tb->bool_flags & IS_HRAS_CALL)) {
                uint16_t hot_label = tb->jmp_reset_offset[succ_id];
                ExitTBContext ctx = {
                    .branch_opcode = ir1_opcode(branch),
                    .tr_ctx = td,
                    .curr_tb = tb,
                    .allow_tb_link = true,
                    .curr_mtcc_id = td->mtcc_id,
                };
                ExitTBEmitState state = {
                    .tblink_stub_mode = TBLINK_STUB_TUCHECK,
                };

                la_label(cold_exit_label);
                tbptr = latxs_ra_alloc_dbt_arg1();
                latxs_tr_exit_tb_emit_tu_check_stub(
                        &ctx, &state, &tbptr, (ADDR)tb, succ_id);
                latxs_tr_exit_tb_finish(
                        &ctx, &state, &tbptr, (ADDR)tb, branch, succ_id);
                /* The checker's cold slot is never dynamically linked. */
                tb->jmp_reset_offset[succ_id] = hot_label;
                tb->link_opt[succ_id] = 1;
                tb->jmp_cp_data[succ_id] = 0;
                tu_call_cold_reloc_end(tb, main_start, main_end);
                continue;
            }
#endif
            latxs_append_ir2_opnd1(LISA_LABEL, &cold_exit_label);
            latxs_tr_shbr_restore_zero64(
                branch->shbr_restore_zero64_exit[succ_id]);
            if (!option_lazypc) {
                tr_gen_branch_save_next_eip(branch, succ_id);
            }
            tbptr = latxs_ra_alloc_dbt_arg1();
            latxs_tr_gen_exit_tb_load_tb_addr(&tbptr, (ADDR)tb);
            latxs_tr_gen_exit_tb_j_context_switch(&tbptr, 1, succ_id);
            tu_call_cold_reloc_end(tb, main_start, main_end);
        }
    }

    for (i = 0; direct_calls && i < tb_num_in_tu; i++) {
        HrasDirectCall *call = direct_calls[i];
        TranslationBlock *tb = tb_list[i];
        IR2_OPND tbptr;
        IR1_INST *branch;

        if (!call) continue;
        int main_start, main_end;
        tu_call_cold_reloc_begin(tb, &main_start, &main_end);
        td->curr_tb = tb;
        td->ir1_inst_array = tb->s_data->ir1;
        td->ignore_eip_update = tb->s_data->ignore_eip_update;
        td->sys.code64 = (tb->flags >> HF_CS64_SHIFT) & 1;
        branch = (IR1_INST *)tb->s_data->ir1 + tb->icount - 1;
        la_label(call->cold);
        if (call->state.emitted_interrupt_check) {
            latxs_ir2_interrupt_set_label(tb, 0);
        }
        tbptr = latxs_ra_alloc_dbt_arg1();
        latxs_tr_exit_tb_emit_direct_action(&call->ctx, &call->state,
                &tbptr, (ADDR)tb, branch, 0);
        tu_call_cold_reloc_end(tb, main_start, main_end);
    }

    for (i = 0; indirect_calls && i < tb_num_in_tu; i++) {
        TranslationBlock *tb = tb_list[i];

        if (!indirect_calls[i].valid && !indirect_calls[i].irq_deferred) {
            continue;
        }
        td->curr_tb = tb;
        td->ir1_inst_array = tb->s_data->ir1;
        td->ignore_eip_update = tb->s_data->ignore_eip_update;
        td->sys.code64 = (tb->flags >> HF_CS64_SHIFT) & 1;
        if (indirect_calls[i].irq_deferred) {
            HrasIndirectCallLabels labels = indirect_calls[i];
            la_label(labels.irq);
            latxs_tr_gen_intb_sync_gpc();
            latxs_tr_gen_exit_tb_j_context_switch(NULL, 0, labels.irq_succ_id);
        }
        if (indirect_calls[i].fsht_deferred) {
            HrasIndirectCallLabels labels = indirect_calls[i];
            gen_latxs_intb_fsht_call_miss_saved(labels.fsht_id,
                labels.fsht_fast, &labels.fsht,
                &labels.miss, &labels.call, labels.fsht_scratch);
        }
        if (indirect_calls[i].valid) {
            latxs_tr_gen_indirect_call_cold(indirect_calls[i]);
        }
    }

    td->curr_tb = saved_tb;
    td->ir1_inst_array = saved_ir1;
    td->ignore_eip_update = saved_ignore_eip;
    td->sys.code64 = saved_code64;
    g_free(cold_order);
}
#endif

void latxs_tr_generate_exit_tb(IR1_INST *branch, int succ_id)
{
    ExitTBContext ctx = {
        .branch_opcode = ir1_opcode(branch),
        .target_is_indirect = false,
        .tr_ctx = lsenv->tr_data,
        .curr_tb = lsenv->tr_data->curr_tb,
        .allow_tb_link = lsenv->tr_data->can_link,
        .curr_mtcc_id = lsenv->tr_data->mtcc_id,
        .target_crosses_page = false,
        .target_maybe_cross_page_block = false,
        .target_in_current_tu = false,
        .cross_page_intcheck = false,
    };
#if defined(CONFIG_LATX_TU) && defined(CONFIG_SOFTMMU)
    if (tucp_fast_tu_direct() && ctx.tr_ctx->is_tu_mode &&
        !ir1_is_indirect_inst(branch))
    {
        TranslationBlock *target_tb;

        if (ir1_is_call(branch)) {
            target_tb = tu_tree_lookup(ir1_target_addr(branch));
            ctx.target_in_current_tu = target_tb != NULL;
        } else {
            target_tb = ctx.curr_tb->s_data->next_tb[succ_id];
            ctx.target_in_current_tu = target_tb &&
                                       tb_is_in_tb_list(target_tb);
        }
    }
#endif
    ExitTBEmitState state = { 0 };
    IR2_OPND tbptr = latxs_ra_alloc_dbt_arg1(); /* t8($24) */
    ADDR tb_addr = (ADDR)ctx.curr_tb;

    latxs_tr_exit_tb_prepare(&ctx, &state, &tbptr, tb_addr, branch, succ_id);
    latxs_tr_exit_tb_finish(&ctx, &state, &tbptr, tb_addr, branch, succ_id);
#ifdef CONFIG_LATX_TU
    if (ctx.curr_tb->s_data->tu_cold_exit_offset[succ_id] ==
            TU_COLD_EXIT_DIRECT_PENDING ||
        ctx.curr_tb->s_data->tu_cold_exit_offset[succ_id] ==
            TU_INDIRECT_EXIT_INLINE_PENDING) {
        ctx.curr_tb->s_data->tu_cold_exit_offset[succ_id] = TU_COLD_EXIT_INVALID;
    }
#endif
}
