/*
 * SPDX-FileCopyrightText: 2024-2026 LATS Project Authors
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ir1-decode.h"
#include "common.h"
#include "exec/exec-all.h"
#include "tcg/tcg.h"
#include "tcg/insn-start-words.h"
#include "translate.h"
#include "lsenv.h"
#include "accel/tcg/internal.h"
#include "latx-config.h"
#include "latx-tu.h"
#include "latx-tu-page-lock.h"
#include "latx-options.h"
#include "gpr-highbits-policy.h"
#include "latx-misc.h"
#include "latx-sys-flag-rdtn.h"
#include "latx-sys-inst-ptn.h"

#include "hamt-data.h"
#include "sys-aot.h"
#include "sys-aot-tu-merge.h"
#include "sys-aot-common.h"
#include "qemu/cacheflush.h"
#include "tcg/tcg-internal.h"
#include "latx-ras.h"
#include "exec/tb-flush.h"
#include "exec/cputlb.h"
#include "exec/ram_addr.h"
#include "hamt-ptw.h"
#include "hamt-dmw.h"
#include "latx-sys-push-pop.h"
#include "latx-sys-hamt-dmw.h"
#include "latxs-imm-cache.h"
#include "latx-shbr.h"
#include "latx-ghbr.h"

#ifdef CONFIG_LATX_TU

typedef struct RasTPCallLabels {
    bool valid;
    IR2_OPND gate, miss, done;
} RasTPCallLabels;

static inline ADDRX get_page(ADDRX pc)
{
    return pc & TARGET_PAGE_MASK;
}

static __thread TUControl tu_data_rel;
__thread TUControl *tu_data;
static uint64_t tu_pages_next_id = 1;
static unsigned int tu_pages_id_exhausted_warned;

#define TU_NOP 0x03400000
#ifdef CONFIG_SOFTMMU
#define assert_memory_lock()
#else
#define assert_memory_lock() tcg_debug_assert(have_mmap_lock())
#endif
//
static gint gpc_cmp(gconstpointer ap, gconstpointer bp)
{
    const TranslationBlock *a = (TranslationBlock *)ap;
    const TranslationBlock *b = (TranslationBlock *)bp;

    return (a->pc < b->pc) ? -1 : (a->pc > b->pc);
}

static inline void tu_trees_init(void)
{
    tu_data->tree = g_tree_new(gpc_cmp);
}

inline void tu_trees_reset(void)
{
    /* Increment the refcount first so that destroy acts as a reset */
    g_tree_ref(tu_data->tree);
    g_tree_destroy(tu_data->tree);
}

static inline void tu_trees_remove(TranslationBlock *tb)
{
    /* remove tb which has been changed */
    g_tree_remove(tu_data->tree, tb);
    if (option_soft_ras) {
        TranslationBlock *preorder_tb = NULL;
        TranslationBlock *preorder_ptr = NULL;
        if (tb->preorder_tb_offset) {
            preorder_tb = (TranslationBlock *)((int64_t)tb + tb->preorder_tb_offset);
            latxs_ras_unlink(preorder_tb);
            while (preorder_tb->preorder_next) {
                preorder_ptr = preorder_tb;
                preorder_tb = preorder_tb->preorder_next;
                latxs_ras_unlink(preorder_tb);
                preorder_ptr->preorder_next = NULL;
            }
        }
    }
}

TranslationBlock *tu_tree_lookup(target_ulong pc)
{
    TranslationBlock key = {.pc = pc};
    return (TranslationBlock *)g_tree_lookup(tu_data->tree, &key);
}

static void tu_tree_insert(TranslationBlock *tb)
{
    g_tree_insert(tu_data->tree, tb, tb);
}

void tu_control_init(void)
{
    tu_data = &tu_data_rel;
    tu_data->ir1_num_in_tu = 0;
    tu_data->tb_num = 0;
    tu_trees_init();

    return;
}

static void tu_reset_explore_state(void)
{
    tu_data->tb_num = 0;
    tu_data->ir1_num_in_tu = 0;
    memset(tu_data->tb_list, 0, sizeof(tu_data->tb_list));
    tu_page_lock_reset_explore();
    tu_trees_reset();
}

static void tu_reset_gencode_state(void)
{
    tu_page_lock_reset_gencode();
    tu_reset_explore_state();
}

static void tu_do_tb_flush(CPUState *cpu)
{
    CPUArchState *env = cpu->env_ptr;
    int mtcc_id = curr_mtcc(env);
    tcg_ctx = tcg_ctx_mtcc[mtcc_id];

    tb_flush_mtcc(cpu, mtcc_id);
    mmap_unlock();

    tu_trees_reset();
    tu_reset_tb_flush();

#ifdef CONFIG_LATX_PRINT
    printf("%s:%d CPU[%d]\n", __func__, __LINE__, cpu->cpu_index);
#endif

    cpu->exception_index = EXCP_INTERRUPT;
    cpu_loop_exit(cpu);
}

inline void tu_push_back(TranslationBlock *tb)
{
    if (!tb) {
        return;
    }
    /* A TU must never expose more TBs than the translation arrays support. */
    if (tu_data->tb_num >= MAX_TB_IN_TS) {
        return;
    }
    TranslationBlock** tb_list = tu_data->tb_list;
    uint32_t* tb_num_in_tu = &tu_data->tb_num;

    tb_list[(*tb_num_in_tu)++] = tb;
}

void tu_reset_tb(TranslationBlock *tb)
{
    /* Also covers the thread-local template copied into heap AOT TBs. */
    tb->search_ptr = NULL;
    tb->s_data->opt_ptr = NULL;
    qemu_spin_init(&tb->jmp_lock);
    tb->jmp_dest[0] = (uintptr_t)NULL;
    tb->jmp_dest[1] = (uintptr_t)NULL;
    tb->jmp_list_head = (uintptr_t)NULL;
    tb->jmp_list_next[0] = (uintptr_t)NULL;
    tb->jmp_list_next[1] = (uintptr_t)NULL;

    /* init top in and top out */
    tb->s_data->eflag_use = __ALL_EFLAGS;
    tb->s_data->eflag_use_real = 0;
    tb->s_data->tb_too_large_ir1_index = -1;
    tb->s_data->sys_eob_ir1_index = -1;
    tb->s_data->_top_out = -1;
    tb->s_data->_top_in = -1;
    tb->jmp_reset_offset[0] = TB_JMP_RESET_OFFSET_INVALID;
    tb->jmp_reset_offset[1] = TB_JMP_RESET_OFFSET_INVALID;
    tb->jmp_insn_offset[0] = TB_JMP_OFFSET_INVALID;
    tb->jmp_insn_offset[1] = TB_JMP_OFFSET_INVALID;
    tb->jmp_cp_data[0] = 0;
    tb->jmp_cp_data[1] = 0;
    tb->s_data->tu_generation_offset_patch[0] = TB_JMP_OFFSET_INVALID;
    tb->s_data->tu_generation_offset_patch[1] = TB_JMP_OFFSET_INVALID;
    tb->jit_tucheck_external[0] = NULL;
    tb->jit_tucheck_external[1] = NULL;
    tb->next_pc[0] = 0;
    tb->next_pc[1] = 0;
    tb->link_opt[0] = 0;
    tb->link_opt[1] = 0;
    tb->jmp_inst_hold[0] = 0;
    tb->jmp_inst_hold[1] = 0;
    tb->int_opt[0] = 0;
    tb->int_opt[1] = 0;
    tb->signal_unlink[0] = 0;
    tb->signal_unlink[1] = 0;
    tb->page_addr_mix = 0;
    tb->opt_mmio_type = TB_OPT_MMIO_NONE;
    memset(&tb->ptn, 0, sizeof(tb->ptn));
    tb->label_pos_nocheck = 0;
    TB_TU_PAGES(tb) = NULL;
//#ifdef CONFIG_LATX_INSTS_PATTERN
//    tb->eflags_target_arg[0] = TB_JMP_RESET_OFFSET_INVALID;
//    tb->eflags_target_arg[1] = TB_JMP_RESET_OFFSET_INVALID;
//    tb->eflags_target_arg[2] = TB_JMP_RESET_OFFSET_INVALID;
//#endif
    tb->bool_flags = OPT_BCC;
#ifdef CONFIG_SOFTMMU
    if (can_do_hamt(lsenv->cpu_state)) {
        tb->bool_flags |= CAN_DO_HAMT;
    }
#endif
    tb_set_page_addr0(tb, -1);
    tb_set_page_addr1(tb, -1);
    tb->ras_dest = (uintptr_t)NULL;
#ifdef CONFIG_LATX_TU
    /* tb->tu_start_mode = TU_TB_START_NONE; */
    tb->tc.offset_in_tu = 0;
    tb->s_data->x86_next_pc = 0;
    tb->s_data->x86_target_pc = 0;
    tb->s_data->is_first_tb = 0;
    tb->s_data->tu_size = 0;
    tb->s_data->last_ir1_type = 0;
    tb->s_data->tu_tb_mode = TU_TB_MODE_NONE;
    tb->s_data->tu_jmp[TU_TB_INDEX_TARGET] = TB_JMP_RESET_OFFSET_INVALID;
    tb->s_data->tu_jmp[TU_TB_INDEX_NEXT] = TB_JMP_RESET_OFFSET_INVALID;
    //tb->jmp_target_arg[TU_TB_INDEX_TARGET] = TB_JMP_RESET_OFFSET_INVALID;
    //tb->jmp_target_arg[TU_TB_INDEX_NEXT] = TB_JMP_RESET_OFFSET_INVALID;
    //tb->jmp_stub_reset_offset[0] = TB_JMP_RESET_OFFSET_INVALID;
    //tb->jmp_stub_reset_offset[1] = TB_JMP_RESET_OFFSET_INVALID;
    tb->s_data->tu_unlink_stub_offset = TU_UNLINK_STUB_INVALID;
    tb->s_data->tu_cold_exit_offset[0] = TU_COLD_EXIT_INVALID;
    tb->s_data->tu_cold_exit_offset[1] = TU_COLD_EXIT_INVALID;
    LATXS_RAS_TP_COLD_OFFSET(tb) = 0;
    LATXS_RAS_TP_COLD_INSNS(tb) = 0;
#endif

//#ifdef CONFIG_LATX_AOT
//    tb->s_data->rel_start = -1;
//    tb->s_data->rel_end = -1;
//#endif
    tb->s_data->next_tb[TU_TB_INDEX_NEXT] = NULL;
    tb->s_data->next_tb[TU_TB_INDEX_TARGET] = NULL;
    tb->s_data->is_invalidate = false;
}

/* Create a TB and initialize it. */
static __thread TranslationBlock tu_tb_tmp;
static __thread struct separated_data tmp_s_data;
static __thread int search_id;
static __thread int search_size;
static __thread uint8_t search_buff[MAX_TB_IN_CACHE * TCG_MAX_INSNS];
static __thread uint32_t search_buff_offset[MAX_TB_IN_CACHE];

static int16_t tu_ir1_index(TranslationBlock *tb, const IR1_INST *pir1)
{
    if (!pir1) {
        return -1;
    }
    ptrdiff_t index = pir1 - (IR1_INST *)tb->s_data->ir1;
    g_assert(index >= 0 && index <= INT16_MAX);
    return index;
}

static IR1_INST *tu_ir1_from_index(TranslationBlock *tb, int16_t index)
{
    return index < 0 ? NULL : (IR1_INST *)tb->s_data->ir1 + index;
}

static inline void copy_tr_data_to_s_data(TranslationBlock* tb)
{
    TRANSLATION_DATA *td = lsenv->tr_data;

    tb->s_data->tb_too_large_ir1_index =
        tu_ir1_index(tb, td->tb_too_large_pir1);
    tb->s_data->sys_eob_ir1_index = tu_ir1_index(tb, td->sys_eob_pir1);
    tb->s_data->sys_eob_can_link = td->sys_eob_can_link;
    tb->s_data->sys_eob_is_indir = td->sys_eob_is_indir;
    tb->s_data->sys_eob_use_fsht = td->sys_eob_use_fsht;
    tb->s_data->ignore_eip_update = td->ignore_eip_update;
    tb->s_data->host_addr[0] = td->host_addr[0];
    tb->s_data->host_addr[1] = td->host_addr[1];

}

static inline void copy_s_data_to_tr_data(TranslationBlock* tb)
{
    TRANSLATION_DATA *td = lsenv->tr_data;

    td->tb_too_large_pir1 =
        tu_ir1_from_index(tb, tb->s_data->tb_too_large_ir1_index);
    td->sys_eob_pir1 = tu_ir1_from_index(tb, tb->s_data->sys_eob_ir1_index);
    td->sys_eob_can_link = tb->s_data->sys_eob_can_link;
    td->sys_eob_is_indir = tb->s_data->sys_eob_is_indir;
    td->sys_eob_use_fsht = tb->s_data->sys_eob_use_fsht;
    td->ignore_eip_update = tb->s_data->ignore_eip_update;
    td->host_addr[0] = tb->s_data->host_addr[0];
    td->host_addr[1] = tb->s_data->host_addr[1];

}

#ifdef CONFIG_SOFTMMU
void *fastitlb_mmu_translate(CPUX86State *env, uint64_t addr, uint64_t *phys_pc);
#endif

TranslationBlock* tb_create(CPUState *cpu, target_ulong pc,
        target_ulong cs_base, uint32_t flags, int cflags,
        int max_insns, bool is_first_tb, TU_TB_START_TYPE mode,
        const TranslationBlock *hras_caller, int *result)
{
    TranslationBlock* tb = NULL;
    CPUArchState *env = cpu->env_ptr;
    tb_page_addr_t phys_pc;
    void *host_pc;
    int lock_result = TU_LOCK_FALLBACK;

    *result = TU_EXPLORE_OK;

    if (unlikely(pc == 0)) {
        return NULL;
    }

    uint32_t mtcc_id = curr_mtcc(env);
    tcg_ctx = tcg_ctx_mtcc[mtcc_id];

    phys_pc = get_page_addr_code_hostp(env, pc, &host_pc);

#ifdef CONFIG_USER_ONLY
    /* User-mode trylock is a no-op. Protect each member's first page before
     * decoding, including writable code reached beyond the TU entry page.
     * Cross-page decoding protects the second page via tb_lock_page1(). */
    if (phys_pc != -1) {
        tb_lock_page0(phys_pc);
    }
#endif

    if (phys_pc != -1 && cp_tu_pages()) {
        lock_result = tu_lock_page(pc, phys_pc, NULL);

        if (lock_result == TU_LOCK_RESTART_LOCK_ORDER) {
            *result = TU_EXPLORE_RESTART_LOCK_ORDER;
            return NULL;
        }
        if (lock_result == TU_LOCK_NEED_PROTECT) {
            *result = TU_EXPLORE_NEED_PROTECT;
            return NULL;
        }
        if (lock_result == TU_LOCK_FALLBACK) {
            *result = TU_EXPLORE_FALLBACK;
            return NULL;
        }
    }

    if (phys_pc == -1) {
        /* Generate a one-shot TB with 1 insn in it */
        cflags = (cflags & ~CF_COUNT_MASK) | CF_LAST_IO | 1;
    }

#ifdef CONFIG_SOFTMMU
    bool reuse_protection = false;
#ifdef CONFIG_LATX_HACK_MEM
    /* The earlier CALL member completed the TLB protection scan on this
     * page. Keep its lock and protected state: new aliases also receive
     * TLB_NOTDIRTY, so the adjacent return needs no second full scan. */
    reuse_protection = hras_caller && lock_result == TU_LOCK_ALREADY_HELD &&
        latx_hack_gpa_in_ram(phys_pc) &&
        ((tb_page_addr0(hras_caller) ^ phys_pc) & TARGET_PAGE_MASK) == 0 &&
        tlb_code_page_is_protected(phys_pc);
#endif
    if (!reuse_protection) {
        tlb_protect_code(phys_pc);
    }
#endif

    QEMU_BUILD_BUG_ON(CF_COUNT_MASK + 1 != TCG_MAX_INSNS);

    /* todo: now check 16bit(should 15bit for x86) */
    //uint32_t check_zero = cpu_lduw_code((CPUArchState *)cpu->env_ptr, pc);
    //if (unlikely(check_zero == 0)){
    //    return NULL;
    //}

    tb = &tu_tb_tmp;
    tb->s_data = &tmp_s_data;
    tb->s_data->eflag_use = __ALL_EFLAGS;
    tu_reset_tb(tb);
    tb_set_opt_ptr(tb, NULL);
    tb->pc = pc;
    tb->cs_base = cs_base;
    tb->flags = flags;
    tb->cflags = cflags;
    tb->ptn.eflag_type = PTN_EFLAG_TYPE_INVALID;
#ifdef CONFIG_LATX_DEBUG
    tb->inhibit_irq_type = 0;
#endif
    clear_misc_flags(tb);
    tb_set_page_addr0(tb, phys_pc);
    tb_next_data_reset(tb);
    if (option_fast_itlb) {
        tb->hva = phys_pc & TARGET_PAGE_MASK;
#ifdef CONFIG_SOFTMMU
    } else {
        int error = 0;
        tb->hva = latxs_tr_cvt_cpu_addr_code(env, pc, &error);
#endif
    }
    tb_set_page_addr1(tb, -1);
    set_mask_to_miscflags(tb, curr_mtcc(env),
                          MF_MTCC_ID_MASK, MF_MTCC_ID_SHIFT);
    tb->preorder_tb_offset = 0;
    tb->preorder_next = NULL;
    tb->inst_ptr_offset = 0;

#ifdef CONFIG_LATX_PROFILER
    CLN_TB_PROFILE(tb);
#endif
    tb->bool_flags &= ~IS_TU_TB;

#if defined(CONFIG_TB_FLAGS_NO_EFLAGS)
    if (tb->flags == cpu_get_tb_cpu_state_flags_no_eflags(env)) {
        add_misc_flags(tb, MF_FLAGS_NOEFLAGS);
    }
#endif

    tcg_ctx->addr_type = TARGET_LONG_BITS == 32 ? TCG_TYPE_I32 : TCG_TYPE_I64;
#ifdef CONFIG_SOFTMMU
    tcg_ctx->page_bits = TARGET_PAGE_BITS;
    tcg_ctx->page_mask = TARGET_PAGE_MASK;
    tcg_ctx->tlb_dyn_max_bits = CPU_TLB_DYN_MAX_BITS;
    tcg_ctx->tlb_fast_offset =
        (int)offsetof(ArchCPU, neg.tlb.f) - (int)offsetof(ArchCPU, env);
#endif
    tcg_ctx->insn_start_words = TARGET_INSN_START_WORDS;
#ifdef TCG_GUEST_DEFAULT_MO
    tcg_ctx->guest_mo = TCG_GUEST_DEFAULT_MO;
#else
    tcg_ctx->guest_mo = TCG_MO_ALL;
#endif
//    tb->trace_vcpu_dstate = *cpu->trace_dstate;
//    /* tb->tu_start_mode = mode; */
//    tcg_ctx->tb_cflags = cflags;
//    tcg_ctx->tb_jmp_reset_offset = tb->jmp_reset_offset;
//    if (TCG_TARGET_HAS_direct_jump) {
//        tcg_ctx->tb_jmp_insn_offset = tb->jmp_target_arg;
//        tcg_ctx->tb_jmp_target_addr = NULL;
//    } else {
//        tcg_ctx->tb_jmp_insn_offset = NULL;
//        tcg_ctx->tb_jmp_target_addr = tb->jmp_target_arg;
//    }

    latxs_tb_reset(tb);

    TRANSLATION_DATA *td = lsenv->tr_data;
    td->tb_too_large_pir1 = NULL;
    td->sys_eob_pir1 = NULL;

    latxs_tr_sys_init(tb, max_insns, tcg_ctx->code_gen_highwater, host_pc);
    latxs_tr_disasm(tb);
    if (td->deadlock_avoidance) {
        if (td->deadlock_avoidance == -5) {
            *result = TU_EXPLORE_RESTART_LOCK_ORDER;
        } else if (td->deadlock_avoidance == TU_DECODE_NEED_PROTECT) {
            *result = TU_EXPLORE_NEED_PROTECT;
        } else {
            *result = TU_EXPLORE_FALLBACK;
        }
//        printf("%s:%d tu deadlock avoidance %d\n",
//                __func__, __LINE__, td->deadlock_avoidance);
        return NULL;
    }
    copy_tr_data_to_s_data(tb);

    if (tb->icount == 0 || tb->s_data->tu_tb_mode == TU_TB_MODE_BROKEN
			|| tb->s_data->tu_tb_mode == BAD_TB) {
        return NULL;
    }
    tb = tcg_tb_alloc_full(tcg_ctx);
    if (unlikely(!tb)) {
        tb_unlock_ctx_pages(1);
#ifdef CONFIG_LATX_PRINT
        printf("%s:%d CPU[%d] fail to alloc TB.\n", __func__, __LINE__,
                cpu->cpu_index);
#endif
        tu_do_tb_flush(cpu); /* never return */
        g_assert_not_reached();
    }
    tu_tb_tmp.s_data = tb->s_data;
    memcpy(tb, &tu_tb_tmp, sizeof(TranslationBlock));
    memcpy(tb->s_data, &tmp_s_data, sizeof(struct separated_data));
//    /* tu_tree_insert(tb); */
    tu_tree_insert(tb);
//    aot_tb_insert(tb);

    return tb;
}

static inline int tb_sort_cmp(const void *ap, const void *bp)
{
    const TranslationBlock *a = *(const TranslationBlock **)ap;
    const TranslationBlock *b = *(const TranslationBlock **)bp;
    return a->pc < b->pc ? -1 : a->pc > b->pc;
}

static void tu_set_tb_to_translate_context(TranslationBlock *tb)
{
    tb->s_data->next_tb[TU_TB_INDEX_NEXT] = NULL;
    tb->s_data->next_tb[TU_TB_INDEX_TARGET] = NULL;
    tb->s_data->tu_tb_mode = TU_TB_MODE_SWITCH_TO_TB;
    tb->s_data->tu_jmp[TU_TB_INDEX_TARGET] = TB_JMP_RESET_OFFSET_INVALID;
    tb->s_data->tu_jmp[TU_TB_INDEX_NEXT] = TB_JMP_RESET_OFFSET_INVALID;
}

static bool tu_split_tb(TranslationBlock *pre_tb, TranslationBlock *tb)
{
    bool spilt_success = false;
    ADDRX pre_pc, tb_pc = ((IR1_INST *)(tb->s_data->ir1))[0].info->address;
    uint16_t split_size, split_icount;

    if (pre_tb->icount >= tb->icount) {
        uint32_t th_firs_inst = pre_tb->icount - tb->icount;
        lsassert(th_firs_inst <= pre_tb->icount);
        pre_pc = ((IR1_INST *)(pre_tb->s_data->ir1))[th_firs_inst].info->address;
        if (pre_pc == tb_pc) {
            spilt_success = true;
            split_size = pre_tb->size - tb->size;
            split_icount = pre_tb->icount - tb->icount;
        } else {
            for (int i = 0; i < pre_tb->icount; i++) {
                pre_pc = ((IR1_INST *)(pre_tb->s_data->ir1))[i].info->address;
                if (pre_pc == tb_pc) {
                    spilt_success = true;
                    split_size = i;
                    split_icount = i;
                    break;
                }
            }
        }
        #ifdef TU_DEBUG
        else {
            qemu_log_mask(LAT_LOG_AOT, "%d TU WARNING: tb->pc = "TARGET_FMT_lx" is error,"
                " skiped.\n", getpid(), tb->pc);
        }
        #endif
    } else {
        lsassert(0);
    }

    if (spilt_success) {
        /* tb->tu_start_mode = TU_TB_START_NORMAL; */
        pre_tb->s_data->next_tb[TU_TB_INDEX_NEXT] = NULL;
        pre_tb->s_data->next_tb[TU_TB_INDEX_TARGET] = NULL;
        pre_tb->size = split_size;
        pre_tb->icount = split_icount;
        pre_tb->s_data->tu_jmp[TU_TB_INDEX_TARGET] = TB_JMP_RESET_OFFSET_INVALID;
        pre_tb->s_data->tu_jmp[TU_TB_INDEX_NEXT] = TB_JMP_RESET_OFFSET_INVALID;
        pre_tb->s_data->last_ir1_type = IR1_TYPE_SPLITED;
        if (pre_tb->s_data->tb_too_large_ir1_index >= 0) {
            pre_tb->s_data->tb_too_large_ir1_index = -1;
        }
        if (pre_tb->s_data->sys_eob_ir1_index >= 0) {
            pre_tb->s_data->sys_eob_ir1_index = -1;
        }
    }
    return spilt_success;
}

static bool page_valid(CPUState *cpu, uint64_t vp)
{
    return get_page_addr_code_noexit(cpu->env_ptr, vp) != -1;
}


/* An existing owner bounds the extra exploration needed by a private return.
 * It supplies only guest edges: every copy is decoded and page-locked anew. */
static TranslationBlock *tu_hras_original_member(TranslationBlock *source,
                                                TranslationBlock *target,
                                                target_ulong pc)
{
#ifdef CONFIG_SOFTMMU
    if (source) {
        const TranslationUnitPages *pages = TB_TU_PAGES(source);
        uint32_t lo = 0, hi;

        if (!pages || !pages->hras_members ||
            (tb_cflags(source) & CF_INVALID)) {
            return NULL;
        }
        hi = pages->hras_member_count;
        target = NULL;
        while (lo < hi) {
            uint32_t mid = lo + (hi - lo) / 2;
            TranslationBlock *member = pages->hras_members[mid];

            if (member->pc < pc) {
                lo = mid + 1;
            } else if (member->pc > pc) {
                hi = mid;
            } else {
                target = member;
                break;
            }
        }
    }
    if (!target || !tb_is_tu(target) || !TB_TU_PAGES(target) ||
        (tb_cflags(target) & CF_INVALID) ||
        (target->bool_flags & IS_AOT_TB) ||
        (source && (TB_TU_PAGES(target) != TB_TU_PAGES(source) ||
                    tb_tu_identity(target) != tb_tu_identity(source)))) {
        return NULL;
    }
    return target;
#else
    (void)source;
    (void)target;
    (void)pc;
    return NULL;
#endif
}

/* Complete newly discovered CALL return chains before optional exploration.
 * Each member is inspected once, including returns appended by this walk. */
static int tu_hras_complete_returns(CPUState *cpu, target_ulong cs_base,
        uint32_t flags, int cflags, int max_insns, uint32_t *scanned,
        TranslationBlock **originals)
{
    uint32_t *tb_num_in_tu = &tu_data->tb_num;

    while (*scanned < *tb_num_in_tu) {
        uint32_t index = (*scanned)++;
        TranslationBlock *tb = tu_data->tb_list[index];
        TranslationBlock *next_tb;
        ADDRX ir1_next_pc = tb->s_data->x86_next_pc;

        if (tb->s_data->last_ir1_type != IR1_TYPE_CALL &&
            tb->s_data->last_ir1_type != IR1_TYPE_CALLIN) {
            continue;
        }
        /* A cached return entry can live anywhere in the code cache.
         * Decode an owner-local member so the CALL's PC+4 is the
         * software-RAS return entry too. Never speculate on unmapped
         * return pages. Required returns may exceed the optional
         * per-page exploration budget, but not the array capacity.
         * tb_create retains the normal page lock retry. */
        next_tb = tu_tree_lookup(ir1_next_pc);
        if (!next_tb && *tb_num_in_tu < MAX_TB_IN_TS &&
            (get_page(tb->pc) == get_page(ir1_next_pc) ||
             (tu_cross_page_enabled() && cp_tu_pages() &&
              page_valid(cpu, ir1_next_pc))) &&
            (!cp_tu_pages() ||
             tu_pages_find(ir1_next_pc) >= 0 ||
             tu_pages_append(get_page(ir1_next_pc)))) {
            int result;
            TranslationBlock *original = NULL;

            if (originals) {
                original = tu_hras_original_member(originals[index],
                    tb_htable_lookup_noexit(cpu, ir1_next_pc, cs_base,
                                           flags, cflags), ir1_next_pc);
            }
            next_tb = tb_create(cpu, ir1_next_pc, cs_base, flags,
                               cflags, max_insns, 0,
                               TU_TB_START_NORMAL, tb, &result);
            if (result != TU_EXPLORE_OK) {
                return result;
            }
            if (next_tb) {
                if (originals) {
                    originals[*tb_num_in_tu] = original;
                }
                tu_push_back(next_tb);
                tu_pages_tb_add(next_tb);
            }
        }
        tb->s_data->next_tb[TU_TB_INDEX_NEXT] = next_tb;
    }
    return TU_EXPLORE_OK;
}

static inline int get_tu_queue(CPUState *cpu,
		 target_ulong cs_base, uint32_t flags,
		 int cflags, int max_insns, TranslationBlock **originals)
{
    ADDRX ir1_next_pc, ir1_target_pc;
    TranslationBlock** tb_list = tu_data->tb_list;
    uint32_t *tb_num_in_tu = &tu_data->tb_num;
    TranslationBlock *next_tb, *target_tb, *tb;
    int is_cross_page = 0;
    bool tu_page_gen_tb = false;
    uint32_t max_tb = cp_tu_pages() ? MAX_TB_IN_TS : MAX_TB_IN_TU;
    bool hras_returns = originals != NULL;

    uint32_t returns_scanned = 0;
    int result;

    if (cp_tu_pages()) tu_pages_init();
    if (hras_returns) {
        memset(originals, 0, MAX_TB_IN_TS * sizeof(*originals));
        result = tu_hras_complete_returns(cpu, cs_base, flags, cflags,
                                          max_insns, &returns_scanned,
                                          originals);
        if (result != TU_EXPLORE_OK) {
            return result;
        }
    }

    for (int i = 0; i < *tb_num_in_tu; i++) {

        /* limit the number of TB in one page */
        if (!cp_tu_pages() && *tb_num_in_tu >= MAX_TB_IN_TU &&
            !hras_returns) {
            break;
        }
        /* limit the total number of TB in all pages */
        if (*tb_num_in_tu >= MAX_TB_IN_TS && !hras_returns) {
            break;
        }

        is_cross_page = 0;
        tb = tb_list[i];
        ir1_next_pc = tb->s_data->x86_next_pc;
        ir1_target_pc = tb->s_data->x86_target_pc;

        switch (tb->s_data->last_ir1_type) {
        case IR1_TYPE_BRANCH:
            /* Jcc next tb should be translated without checking */
            is_cross_page = 0;
            lsassert(ir1_next_pc);
            if (get_page(tb->pc) == get_page(ir1_next_pc) ||
                tu_cross_page_enabled()) {
                if (cp_tu_pages()) {
                    tu_page_gen_tb = tu_pages_append(get_page(ir1_next_pc));
                    tu_page_gen_tb = tu_page_gen_tb && page_valid(cpu, ir1_next_pc);
                }
                if (get_page(tb->pc) != get_page(ir1_next_pc)) {
                    is_cross_page = 1;
                }
                TranslationBlock *original = NULL;
                next_tb = tu_tree_lookup(ir1_next_pc);
                if (!next_tb) {
                    next_tb = tb_htable_lookup_noexit(cpu,
                        ir1_next_pc, cs_base, flags, cflags);
                    if (originals && originals[i]) {
                        original = tu_hras_original_member(originals[i],
                                                           next_tb, ir1_next_pc);
                    }
                }
                if (!next_tb || (option_tu_hras_opt_copy && original)) {
                    if (*tb_num_in_tu < max_tb &&
                        (!is_cross_page || cp_tu_gen_tb()) &&
                        (!cp_tu_pages() || tu_page_gen_tb)) {
                        int result;
                        next_tb = tb_create(cpu, ir1_next_pc, cs_base, flags,
                                       cflags, max_insns, 0, TU_TB_START_NORMAL,
                                       NULL, &result);
                        if (result != TU_EXPLORE_OK) {
                            return result;
                        }
                        if (next_tb) {
                            if (originals) {
                                originals[*tb_num_in_tu] = original;
                            }
                            tu_push_back(next_tb);
                            tu_pages_tb_add(next_tb);
                            if (hras_returns) {
                                result = tu_hras_complete_returns(cpu, cs_base,
                                    flags, cflags, max_insns, &returns_scanned,
                                    originals);
                                if (result != TU_EXPLORE_OK) {
                                    return result;
                                }
                            }
                        }
                    }
                }
                if (next_tb && next_tb->tc.ptr != NULL) {
                    tb->s_data->tu_jmp[TU_TB_INDEX_NEXT] = 0;
                }
                tb->s_data->next_tb[TU_TB_INDEX_NEXT] = next_tb;
            } else {
                tb->s_data->next_tb[TU_TB_INDEX_NEXT] = NULL;
            }
            if (tb->s_data->tu_jmp[TU_TB_INDEX_NEXT] == 0) {
                lsassert(tb->s_data->next_tb[TU_TB_INDEX_NEXT]);
            }

            is_cross_page = 0;
            lsassert(ir1_target_pc);
            /* Jcc target tb should not be translated if the distance is too far */
            if (get_page(tb->pc) == get_page(ir1_target_pc) ||
                tu_cross_page_enabled()) {
                if (cp_tu_pages()) {
                    tu_page_gen_tb = tu_pages_append(get_page(ir1_target_pc));
                    tu_page_gen_tb = tu_page_gen_tb && page_valid(cpu, ir1_target_pc);
                }
                if (get_page(tb->pc) != get_page(ir1_target_pc)) {
                    is_cross_page = 1;
                }
                TranslationBlock *original = NULL;
                target_tb = tu_tree_lookup(ir1_target_pc);
                if (!target_tb) {
                    target_tb = tb_htable_lookup_noexit(cpu,
                        ir1_target_pc, cs_base, flags, cflags);
                    if (originals && originals[i]) {
                        original = tu_hras_original_member(originals[i],
                                                           target_tb, ir1_target_pc);
                    }
                }
                if (!target_tb || (option_tu_hras_opt_copy && original)) {
                    if (*tb_num_in_tu < max_tb &&
                        (!is_cross_page || cp_tu_gen_tb()) &&
                        (!cp_tu_pages() || tu_page_gen_tb)) {
                        int result;
                        target_tb = tb_create(cpu, ir1_target_pc, cs_base, flags,
                                         cflags, max_insns, 0, TU_TB_START_JMP,
                                         NULL, &result);
                        if (result != TU_EXPLORE_OK) {
                            return result;
                        }
                        if (target_tb) {
                            if (originals) {
                                originals[*tb_num_in_tu] = original;
                            }
                            tu_push_back(target_tb);
                            tu_pages_tb_add(target_tb);
                            if (hras_returns) {
                                result = tu_hras_complete_returns(cpu, cs_base,
                                    flags, cflags, max_insns, &returns_scanned,
                                    originals);
                                if (result != TU_EXPLORE_OK) {
                                    return result;
                                }
                            }
                        }
                    }
                }
                if (target_tb && target_tb->tc.ptr != NULL) {
                    tb->s_data->tu_jmp[TU_TB_INDEX_TARGET] = 0;
                }
                tb->s_data->next_tb[TU_TB_INDEX_TARGET] = target_tb;
            } else {
                tb->s_data->next_tb[TU_TB_INDEX_TARGET] = NULL;
            }

            if (tb->s_data->tu_jmp[TU_TB_INDEX_TARGET] == 0) {
                lsassert(tb->s_data->next_tb[TU_TB_INDEX_TARGET]);
            }
            break;
        case IR1_TYPE_JUMP:
            /* JMP target tb should not be translated if the distance is too far */
            if (get_page(tb->pc) == get_page(ir1_target_pc) ||
                /* target page is already being translated here */
                (cp_tu() && cp_tu_pages() && tu_pages_find(ir1_target_pc) >= 0))
            {
                if (cp_tu_pages()) {
                    tu_page_gen_tb = tu_pages_append(get_page(ir1_target_pc));
                }
                TranslationBlock *original = NULL;
                target_tb = tu_tree_lookup(ir1_target_pc);
                if (!target_tb) {
                    target_tb = tb_htable_lookup_noexit(cpu,
                        ir1_target_pc, cs_base, flags, cflags);
                    if (originals && originals[i]) {
                        original = tu_hras_original_member(originals[i],
                                                           target_tb, ir1_target_pc);
                    }
                }
                //if (get_page(tb->pc) == get_page(ir1_target_pc) && !target_tb) {
                if (!target_tb || (option_tu_hras_opt_copy && original)) {
                    if (*tb_num_in_tu < max_tb &&
                        (!cp_tu_pages() || tu_page_gen_tb)) {
                        int result;
                        target_tb = tb_create(cpu, ir1_target_pc, cs_base, flags,
                                         cflags, max_insns, 0, TU_TB_START_JMP,
                                         NULL, &result);
                        if (result != TU_EXPLORE_OK) {
                            return result;
                        }
                        if (target_tb) {
                            if (originals) {
                                originals[*tb_num_in_tu] = original;
                            }
                            tu_push_back(target_tb);
                            tu_pages_tb_add(target_tb);
                            if (hras_returns) {
                                result = tu_hras_complete_returns(cpu, cs_base,
                                    flags, cflags, max_insns, &returns_scanned,
                                    originals);
                                if (result != TU_EXPLORE_OK) {
                                    return result;
                                }
                            }
                        }
                    }
                }
                if (target_tb && target_tb->tc.ptr != NULL) {
                    tb->s_data->tu_jmp[TU_TB_INDEX_TARGET] = 0;
                }
                tb->s_data->next_tb[TU_TB_INDEX_TARGET] = target_tb;
            } else {
                tb->s_data->next_tb[TU_TB_INDEX_TARGET] = NULL;
            }
            break;
        case IR1_TYPE_CALLIN:
        case IR1_TYPE_CALL:
            if (hras_returns) {
                /* Mandatory returns were already completed at admission. */
                break;
            }
            /* fall through */
        case IR1_TYPE_NORMAL:
        case IR1_TYPE_SYSCALL:
            lsassert(ir1_next_pc);
            if (get_page(tb->pc) == get_page(ir1_next_pc)) {
                if (cp_tu_pages()) {
                    tu_page_gen_tb = tu_pages_append(get_page(ir1_next_pc));
                }
                TranslationBlock *original = NULL;
                next_tb = tu_tree_lookup(ir1_next_pc);
                if (!next_tb) {
                    next_tb = tb_htable_lookup_noexit(cpu,
                        ir1_next_pc, cs_base, flags, cflags);
                    if (originals && originals[i]) {
                        original = tu_hras_original_member(originals[i],
                                                           next_tb, ir1_next_pc);
                    }
                }
                if (!next_tb || (option_tu_hras_opt_copy && original)) {
                    if (*tb_num_in_tu < max_tb &&
                        (!cp_tu_pages() || tu_page_gen_tb)) {
                        int result;
                        next_tb = tb_create(cpu, ir1_next_pc, cs_base, flags,
                                       cflags, max_insns, 0, TU_TB_START_NORMAL,
                                       NULL, &result);
                        if (result != TU_EXPLORE_OK) {
                            return result;
                        }
                        if (next_tb) {
                            if (originals) {
                                originals[*tb_num_in_tu] = original;
                            }
                            tu_push_back(next_tb);
                            tu_pages_tb_add(next_tb);
                            if (hras_returns) {
                                result = tu_hras_complete_returns(cpu, cs_base,
                                    flags, cflags, max_insns, &returns_scanned,
                                    originals);
                                if (result != TU_EXPLORE_OK) {
                                    return result;
                                }
                            }
                        }
                    }
                }
                tb->s_data->next_tb[TU_TB_INDEX_NEXT] = next_tb;
            } else {
                tb->s_data->next_tb[TU_TB_INDEX_NEXT] = NULL;
            }
            break;
        case IR1_TYPE_JUMPIN:
        case IR1_TYPE_RET:
        case IR1_TYPE_EOB:
            break;
        default:
            lsassert(0);
        }
    }
    return TU_EXPLORE_OK;
}

/* Keep this frame out of the ordinary exploration caller. Cache exhaustion
 * can leave via cpu_loop_exit(), and normal TU retries must reclaim it too. */
static __attribute__((noinline)) int get_tu_queue_hras(CPUState *cpu,
        target_ulong cs_base, uint32_t flags, int cflags, int max_insns)
{
    TranslationBlock *originals[MAX_TB_IN_TS];

    return get_tu_queue(cpu, cs_base, flags, cflags, max_insns, originals);
}

void solve_tb_overlap(uint tb_num_in_tu,
		TranslationBlock **tb_list, int max_insns)
{
    TranslationBlock *pre_tb = tb_list[0];
    TranslationBlock *tb;
    for (int i = 1; i < tb_num_in_tu; i++) {
        tb = tb_list[i];
        if (pre_tb->s_data->tu_tb_mode == TU_TB_MODE_BROKEN
                || pre_tb->s_data->tu_tb_mode == BAD_TB) {
            assert(0);
            pre_tb = tb;
            continue;
        }
        /* judge if TB need split
         * pre_tb-------->pc + 0
         *                pc + 1
         *                pc + 2
         * tb------------>pc + 3
         *                pc + 4
         *                pc + 5
         * pre_tb_next--->pc + 6
         */
        if (tb->pc < pre_tb->s_data->x86_next_pc && tb->pc > pre_tb->pc) {
            /* when split prefix ins, translate it two times.
             *
             * pre_tb--------> lock
             * tb------------> add
             *                 ...
             * tb_end--------> jmp
             *
             */
            if (pre_tb->icount == tb->icount) {
                /* pre_tb dont link in TU */
                if (latxs_ir1_has_prefix_lock(pre_tb->s_data->ir1)) {
                    pre_tb->s_data->tu_tb_mode = TU_TB_MODE_SWITCH_TO_TB;
                } else {
                    tu_split_tb(pre_tb, tb);
                }
                pre_tb = tb;
                continue;
            }
            if (pre_tb->icount < tb->icount) {
                /*Can't judge pre_tb or tb was broken.*/
                tu_set_tb_to_translate_context(pre_tb);
                tu_set_tb_to_translate_context(tb);
                pre_tb = tb;
                continue;
            }

            /*When pre_tb->icount achive max_insns, the end addresses of two TBS are different*/
            tu_split_tb(pre_tb, tb);
            /* Note: some message of pre_tb last ir1 ins is not modify */
        }
        if (pre_tb->s_data->tu_tb_mode != TU_TB_MODE_BROKEN) {
            pre_tb = tb;
        }
    }
}

static __thread void *tb_ir1_info;

static int tb_disasm_one(IR1_INST *pir1,
        void *addr, uint64_t eip, int codemode,
        void *pir1base, int ir1_num)
{
    int count = 0;
    cs_insn *info = NULL;
    if (codemode == 64) {
        count = latxs_decode_one(handle64, addr, eip, 64,
                          &info, ir1_num, pir1base);
    } else if (codemode == 32) {
        count = latxs_decode_one(handle, addr, eip, 32,
                          &info, ir1_num, pir1base);
    }
#ifdef CONFIG_SOFTMMU
    else {
        count = latxs_decode_one(handle16, addr, eip, 16,
                          &info, ir1_num, pir1base);
    }
#endif
    pir1->info = info;

    if (count != 1) { return 1; }

    pir1->flags = 0;
    pir1->_eflag_def = 0;
    pir1->_eflag_use = 0;
    pir1->_eflag_opt = IR1_EFOPT_INIT;
#ifdef LATXS_INSTPTN_ENABLE
    pir1->instptn.opc  = INSTPTN_OPC_NONE;
    pir1->instptn.next = -1;
#endif

    return 0;
}

static int tb_disasm_eflag_use_zero_impl(TranslationBlock *tb,
        CPUState *cpu, target_ulong pc,
        target_ulong cs_base, uint32_t flags, int cflags, bool force)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    CPUX86State *env = cpu->env_ptr;
    target_ulong curr_pc;
    tb_page_addr_t phys_pc;
    void *host_pc;
    IR1_INST ir1list[64];
    IR1_INST *pir1;
    int codemode;
    int i = 0;

    if (!force &&
            (!flag_reduction_tu_pre_disasm() ||
             (in_aot_translate && !option_aot_tu_pre_disasm))) {
        return 0;
    }

    codemode = (flags & HF_CS64_MASK) ? 64 :
               (flags & HF_CS32_MASK) ? 32 : 16;

    if (tb_ir1_info == NULL) {
        tb_ir1_info = mmap(NULL, 64* IR1_INST_SIZE,
                PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    }

    if (in_aot_translate) {
        /* Only the current immutable code-page snapshot is available offline. */
        if (get_page(pc) != get_page(tb->pc) || !tb->hva) {
            return 0;
        }
        host_pc = (void *)(tb->hva + (pc & ~TARGET_PAGE_MASK));
    } else {
        phys_pc = get_page_addr_code_hostp_noexit(env, pc, &host_pc);
        if (phys_pc == -1) {
            return 0;
        }
    }

    typeof(td->sys) saved_sys = td->sys;
    int result = 0;

    td->sys.pc = pc;
    td->sys.cs_base = cs_base;
    td->sys.flags = flags;
    td->sys.cflags = cflags;

    curr_pc = pc;
    void *addr = host_pc;
    IR1_EFLAG_USEDEF ud;
    for (i = 0; i < 64; ++i) {
        if (get_page(curr_pc) != get_page(pc) ||
                inst_may_cross_page(curr_pc)) {
            break;
        }
        pir1 = &(ir1list[i]);
        if (tb_disasm_one(pir1, addr, curr_pc, codemode, tb_ir1_info, i)) {
            break;
        }

        /* TB-mode deferred flags require the successor's first instruction
         * to be a register CMP.  Do not emit a deferred predecessor when
         * the actual TB entry guard will necessarily reject the link.  The
         * guard additionally checks the post-optimization flag mask and
         * pattern, which cannot be determined by this pre-disassembly. */
        if (force && (i != 0 || ir1_opcode(pir1) != X86_INS_CMP ||
            latxs_ir1_has_prefix_lock(pir1) ||
            ir1_opnd_num(pir1) != 2 ||
            !ir1_opnd_is_gpr(ir1_get_opnd(pir1, 0)) ||
            !(ir1_opnd_is_gpr(ir1_get_opnd(pir1, 1)) ||
              ir1_opnd_is_imm(ir1_get_opnd(pir1, 1))))) {
            break;
        }

        curr_pc += pir1->info->size;
        addr += pir1->info->size;

        if (latxs_ir1_is_illegal(pir1) &&
            pir1->flags != LATXS_IR1_FLAGS_GENNOP) {
            break;
        }
        if (ir1_is_tb_ending(pir1) ||
            latxs_ir1_is_eob_in_sys(pir1)) {
            break;
        }

        ud = latxs_ir1_get_tu_eflag_usedef(pir1);
        if (ir1_is_call(pir1) && ir1_is_indirect_call(pir1)) {
            ud.def = __ALL_EFLAGS;
        }

        if (ud.use == 0) {
            if (ud.def == __ALL_EFLAGS) {
                result = 1;
                goto out;
            }
        } else {
            break;
        }
    }

out:
    td->sys = saved_sys;
    return result;
}

/* TB-mode wrapper (option sysflagtb).  The answer is a property of the guest
 * instruction stream at @pc, not of a particular TB object, so it is stable
 * across TB retranslation; SMC invalidation is still required for the direct
 * edge and is provided by the TB-link veto in tb_add_jump(). */
int latxs_tb_disasm_eflag_use_zero(TranslationBlock *tb, CPUState *cpu,
        target_ulong pc, target_ulong cs_base, uint32_t flags, int cflags)
{
    return tb_disasm_eflag_use_zero_impl(tb, cpu, pc, cs_base, flags,
                                         cflags, true);
}

static void tu_tblist_optimize(CPUState *cpu,
		 target_ulong cs_base, uint32_t flags, int cflags)
{
    TranslationBlock **tblist = tu_data->tb_list;
    TranslationBlock *tb, *target_tb;
    TranslationBlock *ntb;
    int i = 0;
    int n = 0;
    if (!flag_reduction_tu_parse_use2()) {
        return;
    }
    for (i = 0; i < tu_data->tb_num; ++i) {
        tb = tblist[i];
        if (tb->s_data->last_ir1_type == IR1_TYPE_SPLITED &&
            tb->s_data->eflag_use &&
            tb->s_data->eflag_use_real == 0)
        {
            n = 1;
            ntb = (i + n) < tu_data->tb_num ? tblist[i+n] : NULL;
            while (ntb && ntb->s_data->last_ir1_type == IR1_TYPE_SPLITED &&
                    ntb->s_data->eflag_use &&
                    ntb->s_data->eflag_use_real == 0)
            {
                n += 1;
                ntb = (i + n) < tu_data->tb_num ? tblist[i+n] : NULL;
            }
            if (ntb && ntb->pc == (tb->pc + tb->size) &&
                ntb->s_data->eflag_use == 0)
            {
                tb->s_data->eflag_use = 0;
            }
        }
        if (tb->s_data->last_ir1_type == IR1_TYPE_CALL &&
            tb->s_data->eflag_use &&
            tb->s_data->eflag_use_real == 0)
        {
            target_tb = tb->s_data->next_tb[TU_TB_INDEX_TARGET];
            if (!target_tb && !in_aot_translate) {
                target_tb = tb_htable_lookup_noexit(cpu,
                        tb->s_data->x86_target_pc, cs_base, flags, cflags);
                tb->s_data->next_tb[TU_TB_INDEX_TARGET] = target_tb;
            }
            if (!target_tb) {
                if (tb_disasm_eflag_use_zero_impl(tb,
                        cpu, tb->s_data->x86_target_pc, cs_base, flags,
                        cflags, false))
                {
                    tb->s_data->eflag_use = 0;
                }
            }
            if (target_tb && target_tb->s_data->eflag_use == 0) {
                tb->s_data->eflag_use = 0;
            }
        }
    }
    for (i = 0; i < tu_data->tb_num; ++i) {
        tb = tblist[i];
        if (tb->s_data->last_ir1_type == IR1_TYPE_JUMP &&
            tb->s_data->eflag_use &&
            tb->s_data->eflag_use_real == 0)
        {
            target_tb = tb->s_data->next_tb[TU_TB_INDEX_TARGET];
            if (!target_tb && !in_aot_translate) {
                target_tb = tb_htable_lookup_noexit(cpu,
                        tb->s_data->x86_target_pc, cs_base, flags, cflags);
                tb->s_data->next_tb[TU_TB_INDEX_TARGET] = target_tb;
            }
            if (!target_tb) {
                if (tb_disasm_eflag_use_zero_impl(tb,
                        cpu, tb->s_data->x86_target_pc, cs_base, flags,
                        cflags, false))
                {
                    tb->s_data->eflag_use = 0;
                }
            }
            if (target_tb && target_tb->s_data->eflag_use == 0) {
                tb->s_data->eflag_use = 0;
            }
        }
    }

}

bool tu_parse_pending_use(TranslationBlock *tb)
{
    TranslationBlock *target_tb = NULL;
    target_ulong cs_base = tb->cs_base;
    uint32_t flags = tb->flags;
    uint32_t cflags = tb->cflags;
    CPUState *cpu = current_cpu;
    if (!flag_reduction_tu_parse_use2()) {
        return false;
    }
    if (tb->s_data->last_ir1_type == IR1_TYPE_CALL ||
        tb->s_data->last_ir1_type == IR1_TYPE_JUMP)
    {
        target_tb = tb->s_data->next_tb[TU_TB_INDEX_TARGET];
        if (!target_tb && !in_aot_translate) {
            target_tb = tb_htable_lookup_noexit(cpu,
                    tb->s_data->x86_target_pc, cs_base, flags, cflags);
            tb->s_data->next_tb[TU_TB_INDEX_TARGET] = target_tb;
        }
        if (!target_tb) {
            if (tb_disasm_eflag_use_zero_impl(tb,
                    cpu, tb->s_data->x86_target_pc, cs_base, flags,
                    cflags, false))
            {
                return true;
            }
        }
        if (target_tb && target_tb->s_data->eflag_use == 0) {
            return true;
        }
    }
    return false;
}

static void tu_optimize_ir1(CPUState *cpu, TranslationBlock **tb_list,
        uint32_t tb_num, target_ulong cs_base, uint32_t flags, int cflags)
{
    for (int i = 0; i < tb_num; i++) {
        lsenv->tr_data->ir1_inst_array = tb_list[i]->s_data->ir1;
        lsenv->tr_data->ir1_nr = tb_list[i]->icount;
        latxs_flag_reduction_parse_use(tb_list[i]);
    }

    tu_tblist_optimize(cpu, cs_base, flags, cflags);

    for (int i = 0; i < tb_num; i++) {
        lsenv->tr_data->ir1_inst_array = tb_list[i]->s_data->ir1;
        lsenv->tr_data->ir1_nr = tb_list[i]->icount;
        latxs_inst_pattern(tb_list[i]);
        latxs_flag_reduction(tb_list[i]);
    }
}

static TranslationBlock *tb_explore(CPUState *cpu,
                              target_ulong pc, target_ulong cs_base,
                              uint32_t flags, int cflags,
                              int *result)
{
    TranslationBlock** tb_list = tu_data->tb_list;
    uint32_t *tb_num_in_tu = &tu_data->tb_num;
    uint32_t *ir1_num_in_tu = &tu_data->ir1_num_in_tu;
    TranslationBlock *etb = NULL;
    int lookup_after_lock = lal_level(3);

    int max_insns = get_max_insns(cflags);

    *result = TU_EXPLORE_OK;
    *tb_num_in_tu = 0;
    *ir1_num_in_tu = 0;

    CPUArchState *env = cpu->env_ptr;
    uint32_t mtcc_id = curr_mtcc(env);
    tcg_ctx = tcg_ctx_mtcc[mtcc_id];
    tcg_ctx->pre_locked_page0 = -1;
    tcg_ctx->pre_locked_page1 = -1;

    void *host_pc;
    tb_page_addr_t phys_pc = get_page_addr_code_hostp(env, pc, &host_pc);

    if (phys_pc != -1) {
        if (lal_level(2) && tb_page_locked(phys_pc)) {
            lookup_after_lock = true;
        }
#ifdef CONFIG_SOFTMMU
        tlb_protect_code(phys_pc);
#endif
        if (cp_tu_pages()) {
            bool waited = false;
            int lock_result = tu_lock_page(pc, phys_pc, &waited);

            if (lock_result == TU_LOCK_RESTART_LOCK_ORDER) {
                *result = TU_EXPLORE_RESTART_LOCK_ORDER;
                return NULL;
            }
            if (lock_result == TU_LOCK_NEED_PROTECT) {
                *result = TU_EXPLORE_NEED_PROTECT;
                return NULL;
            }
            if (lock_result == TU_LOCK_FALLBACK) {
                *result = TU_EXPLORE_FALLBACK;
                return NULL;
            }
            if (waited) {
                lookup_after_lock = lal_level(1);
            }
        } else if (tb_trylock_page0(phys_pc)) {
            tb_lock_page0(phys_pc);
            lookup_after_lock = lal_level(1);
        }
        tcg_ctx->pre_locked_page0 = phys_pc;
        /* page is busy, lookup again after get the lock
         * to avoid useless translation */
        if (lookup_after_lock) {
            //etb = export_tb_lookup(cpu, pc, cs_base, flags, cflags);
            etb = tb_htable_lookup(cpu, pc, cs_base, flags, cflags);
            if (etb) {
                *result = TU_EXPLORE_EXISTED;
                tb_unlock_ctx_pages(1);
                return etb;
            }
        }
    }
    /* the entry used as return value*/
    TranslationBlock *entry = tb_create(cpu, pc, cs_base,
            flags, cflags, max_insns, true, TU_TB_START_ENTRY, NULL, result);
    if (entry == NULL) {
        return NULL;
    }
    tu_push_back(entry);

    /* search all tbs we can get */
    if (option_tu_hras && option_rastoptp && option_soft_ras &&
        !in_aot_translate && (flags & HF_CS64_MASK) &&
        (flags & HF_CPL_MASK) == HF_CPL_MASK && !cs_base) {
        *result = get_tu_queue_hras(cpu, cs_base, flags, cflags, max_insns);
    } else {
        *result = get_tu_queue(cpu, cs_base, flags, cflags, max_insns, NULL);
    }
    if (*result != TU_EXPLORE_OK) {
        return NULL;
    }

    /* sort tbs by PC */
    qsort(tb_list, *tb_num_in_tu, sizeof(TranslationBlock *), tb_sort_cmp);

    /* Some TBS may overlap. We split these overlapping TBS. */

#ifndef CONFIG_LATX_PROFILER
    solve_tb_overlap(*tb_num_in_tu, tb_list, max_insns);
#endif

    tu_optimize_ir1(cpu, tb_list, *tb_num_in_tu,
            cs_base, flags, cflags);

    return entry;
}

IR2_OPCODE bcc_opcode_convert(IR2_OPCODE bcc_op)
{
    IR2_OPCODE convert_op = LISA_INVALID;

    switch (bcc_op) {
    case LISA_BNE:
        convert_op = LISA_BEQ;
        break;
    case LISA_BEQ:
        convert_op = LISA_BNE;
        break;
    case LISA_BLT:
        convert_op = LISA_BGE;
        break;
    case LISA_BGE:
        convert_op = LISA_BLT;
        break;
    case LISA_BLTU:
        convert_op = LISA_BGEU;
        break;
    case LISA_BGEU:
        convert_op = LISA_BLTU;
        break;
    case LISA_BNEZ:
        convert_op = LISA_BEQZ;
        break;
    case LISA_BEQZ:
        convert_op = LISA_BNEZ;
        break;
    case LISA_BCEQZ:
        convert_op = LISA_BCNEZ;
        break;
    case LISA_BCNEZ:
        convert_op = LISA_BCEQZ;
        break;
    default:
        convert_op = LISA_INVALID;
        lsassert(0);
        break;
    }
    return convert_op;
}

uint bcc_ins_convert(uint convert_insn)
{
    uint fix_insn = convert_insn;
    switch (fix_insn & BCC_OPCODE) {
    case BNE_OPCODE:
        fix_insn &= 0x3ff;
        fix_insn |= BEQ_OPCODE;
        break;
    case BEQ_OPCODE:
        fix_insn &= 0x3ff;
        fix_insn |= BNE_OPCODE;
        break;
    case BLT_OPCODE:
        fix_insn &= 0x3ff;
        fix_insn |= BGE_OPCODE;
        break;
    case BGE_OPCODE:
        fix_insn &= 0x3ff;
        fix_insn |= BLT_OPCODE;
        break;
    case BLTU_OPCODE:
        fix_insn &= 0x3ff;
        fix_insn |= BGEU_OPCODE;
        break;
    case BGEU_OPCODE:
        fix_insn &= 0x3ff;
        fix_insn |= BLTU_OPCODE;
        break;
    case BNEZ_OPCODE:
        fix_insn &= 0x3ff;
        fix_insn |= BEQZ_OPCODE;
        break;
    case BEQZ_OPCODE:
        fix_insn &= 0x3ff;
        fix_insn |= BNEZ_OPCODE;
        break;
    case BCEQZ_OPCODE:
        if ((fix_insn & BFCC_OPCODE) == BCEQZ_OPCODE){
            fix_insn &= 0xe0;
            fix_insn |= BCNEZ_OPCODE;
        } else {
            fix_insn &= 0xe0;
            fix_insn |= BCEQZ_OPCODE;
        }
        break;
    default:
        lsassert(0);
        break;
    }
    return fix_insn;
}

static bool bcc_jmp_fail;

int tu_relocat_target_branch(TranslationBlock * tb)
{
    TranslationBlock *target_tb = tb->s_data->next_tb[TU_TB_INDEX_TARGET];

    uint32_t *inst_ptr;
    ptrdiff_t offset_high, offset_low;
    uint32_t inst0, inst1, inst_jirl;

    inst_ptr = (uint32_t *)((uintptr_t)tb->tc.ptr +
            tb->s_data->tu_jmp[TU_TB_INDEX_TARGET] - tb->tc.offset_in_tu);
    inst0 = inst_ptr[0];
    inst1 = inst_ptr[1];
    inst_jirl = inst_ptr[2];

    long offset = ((long)target_tb->tc.ptr - (long)inst_ptr) >> 2;

    switch (inst0 & BCC_OPCODE) {
    case BEQ_OPCODE:
    case BNE_OPCODE:
    case BLT_OPCODE:
    case BGE_OPCODE:
    case BLTU_OPCODE:
    case BGEU_OPCODE:
        if (offset < OFF26_MIN || offset > OFF26_MAX) {
            int64_t upper, lower;
            inst0 = bcc_ins_convert(inst0);
            offset--;
            upper = ((offset + (1 << 15)) >> 16) & 0xfffff;
            lower = offset & 0xffff;

            inst_ptr[2] = inst_jirl | (lower << 10);
            inst_ptr[1] = inst1 | (upper << 5); // LISA_PCADDU18I

            offset = (tb->s_data->tu_jmp[TU_TB_INDEX_NEXT] -
                    tb->s_data->tu_jmp[TU_TB_INDEX_TARGET]) >> 2;
        } else if (offset < OFF16_MIN || offset > OFF16_MAX) {
            inst0 = bcc_ins_convert(inst0);
            offset--;
            offset_high = (offset >> 16) & 0x3ff;
            offset_low = (offset & 0xffff) << 10;
            inst_ptr[1] = (inst1 & 0xfc000000) | offset_high | offset_low;

            offset = (tb->s_data->tu_jmp[TU_TB_INDEX_NEXT] -
                    tb->s_data->tu_jmp[TU_TB_INDEX_TARGET]) >> 2;
        }
        offset_low = (offset & 0xffff) << 10;
        inst_ptr[0] = (inst0 & 0xfc0003ff) | offset_low;

        break;
    case BEQZ_OPCODE:
    case BNEZ_OPCODE:
    case BCEQZ_OPCODE:
    case BCNEZ_OPCODE:
        if (offset < OFF26_MIN || offset > OFF26_MAX) {
            int64_t upper, lower;
            inst0 = bcc_ins_convert(inst0);
            offset--;
            upper = ((offset + (1 << 15)) >> 16) & 0xfffff;
            lower = offset & 0xffff;

            inst_ptr[2] = inst_jirl | (lower << 10);
            inst_ptr[1] = inst1 | (upper << 5); // LISA_PCADDU18I

            offset = (tb->s_data->tu_jmp[TU_TB_INDEX_NEXT] -
                    tb->s_data->tu_jmp[TU_TB_INDEX_TARGET]) >> 2;
        } else if (offset < OFF20_MIN || offset > OFF20_MAX) {
            inst0 = bcc_ins_convert(inst0);
            offset--;
            offset_high = (offset >> 16) & 0x3ff;
            offset_low = (offset & 0xffff) << 10;
            inst_ptr[1] = (inst1 & 0xfc000000) | offset_high | offset_low;

            offset = (tb->s_data->tu_jmp[TU_TB_INDEX_NEXT] -
                    tb->s_data->tu_jmp[TU_TB_INDEX_TARGET]) >> 2;
        } 
        offset_high = (offset >> 16) & 0x1f;
        offset_low = (offset & 0xffff) << 10;
        inst_ptr[0] = (inst0 & 0xfc0003e0) | offset_high | offset_low;
        break;
    case B_OPCODE:
        offset_high = (offset >> 16) & 0x3ff;
        offset_low = (offset & 0xffff) << 10;
        inst_ptr[0] = (inst0 & 0xfc000000) | offset_high | offset_low;
        break;
    default:
        if ((inst0 & 0x1e000000) == 0x1e000000) {
            if (offset < OFF26_MIN || offset > OFF26_MAX) {
                int64_t upper, lower;
                upper = ((offset + (1 << 15)) >> 16) & 0xfffff;
                lower = offset & 0xffff;

                inst_ptr[1] = inst1 | (lower << 10);
                inst_ptr[0] = inst0 | (upper << 5); // LISA_PCADDU18I
            } else {
                fprintf(stderr, "%s: %dinst_ptr[0] = 0x%x\n", __func__, __LINE__, inst_ptr[0]);
                exit(0);
            }
        } else {
            fprintf(stderr, "%s: %dinst_ptr[0] = 0x%x\n", __func__, __LINE__, inst_ptr[0]);
            exit(0);
        }
        break;

    }

    return 0;
}

void tu_relocat_next_branch(TranslationBlock * tb)
{
    TranslationBlock *tb_next = tb->s_data->next_tb[TU_TB_INDEX_NEXT];

    uint32_t *inst_ptr;
    ptrdiff_t offset_high, offset_low;
    uint32_t inst0, inst1;
    inst_ptr = (uint32_t *)((uintptr_t)tb->tc.ptr +
            tb->s_data->tu_jmp[TU_TB_INDEX_NEXT] - tb->tc.offset_in_tu);

    long offset = ((long)tb_next->tc.ptr - (long)inst_ptr) >> 2;
    inst0 = inst_ptr[0];
    inst1 = inst_ptr[1];

    if (offset < OFF26_MIN || offset > OFF26_MAX) {
        int64_t upper, lower;
        upper = ((offset + (1 << 15)) >> 16) & 0xfffff;
        lower = offset & 0xffff;

        inst_ptr[1] = inst1 | (lower << 10);
        inst_ptr[0] = inst0 | (upper << 5); // LISA_PCADDU18I
    } else {
        offset_high = (offset >> 16) & 0x3ff;
        offset_low = (offset & 0xffff) << 10;
        inst_ptr[0] = (inst0 & 0xfc000000) | offset_high | offset_low;
    }

    return ;
}

static bool tu_hras_local_branches_fit(uint32_t ir2_num)
{
    /* Any two words in this TU then fit a signed 26-bit B displacement,
     * including region-tree builds without the TBMini member-size bound. */
    return !option_tu_hras || ir2_num <= (UINT32_C(1) << 25);
}

#if !defined(CONFIG_TCG_REGION_TREE_ENABLE)
static bool tu_hras_member_size_fits(uint32_t ir2_num)
{
    /* Deferred HRAS exits share the last member's 16-bit TBMini size. */
    return !option_tu_hras || ir2_num <= UINT16_MAX / 4;
}

inline void
tu_tbmini_set_pointer(uint64_t* tbm, uint64_t tb_addr,  uint64_t tc_size)
{
    uint64_t a = (tb_addr & MAKE_64BIT_MASK(0, HOST_VIRT_ADDR_SPACE_BITS));
    uint64_t b =  (tc_size << HOST_VIRT_ADDR_SPACE_BITS);
    *tbm = a | b;
}
#endif

static bool tu_call_cold_exit_eligible(uint32_t tb_num_in_tu,
                                       TranslationBlock **tb_list, uint32_t i)
{
    TranslationBlock *tb = tb_list[i];
    TranslationBlock *next;
    target_ulong source_page;
    target_ulong target;

    if ((!latxs_hras_jit_translate(tb) &&
         !latxs_hras_aot_translate()) ||
        i + 1 >= tb_num_in_tu ||
        tb->s_data->last_ir1_type != IR1_TYPE_CALL ||
        (tb_cflags(tb) & CF_NO_GOTO_TB)) {
        return false;
    }

    next = tb->s_data->next_tb[TU_TB_INDEX_NEXT];
    if (next != tb_list[i + 1] || next->pc != tb->s_data->x86_next_pc) {
        return false;
    }

    if (latxs_hras_aot_translate()) {
        g_assert(next->icount && next->flags == tb->flags &&
                 next->cs_base == tb->cs_base &&
                 tb_cflags(next) == tb_cflags(tb));
    }
    source_page = get_page(tb->pc);
    target = tb->s_data->x86_target_pc;
    if (source_page != get_page(tb->s_data->x86_next_pc) ||
        source_page != get_page(target) ||
        get_page(target) != get_page(target + 15)) {
        return false;
    }

    return true;
}

#ifdef CONFIG_USER_ONLY
static bool tu_tb_is_single_page(const TranslationBlock *tb)
{
    target_ulong page_offset = tb->pc & ~TARGET_PAGE_MASK;

    /* TB page metadata is not finalized before TU linking. */
    return tb->size != 0 && tb->size <= TARGET_PAGE_SIZE - page_offset;
}
#endif

static bool tu_indirect_call_inline_exit_eligible(uint32_t tb_num_in_tu,
                                                  TranslationBlock **tb_list,
                                                  uint32_t i)
{
    TranslationBlock *tb = tb_list[i];
    TranslationBlock *next;

    if ((!latxs_hras_jit_translate(tb) &&
         !latxs_hras_aot_translate()) ||
        !latxs_check_interrupt_at_block_end() ||
        i + 1 >= tb_num_in_tu ||
        tb->s_data->last_ir1_type != IR1_TYPE_CALLIN ||
        (tb_cflags(tb) & (CF_NO_GOTO_TB | CF_NO_GOTO_PTR))) {
        return false;
    }

    next = tb->s_data->next_tb[TU_TB_INDEX_NEXT];
    if (next != tb_list[i + 1] || next->pc != tb->s_data->x86_next_pc) {
        return false;
    }

    if (latxs_hras_aot_translate()) {
        g_assert(next->icount && next->flags == tb->flags &&
                 next->cs_base == tb->cs_base &&
                 tb_cflags(next) == tb_cflags(tb));
    }
#ifndef CONFIG_USER_ONLY
    /* The TU already validates both members' pages. HRAS needs their
     * physical adjacency, including a return on the next guest page. */
    if (option_tu_hras && option_rastoptp && option_soft_ras &&
        (tb->flags & HF_CS64_MASK) && !tb->cs_base &&
        (tb->flags & HF_CPL_MASK) == HF_CPL_MASK) {
        return true;
    }
#endif

#ifdef CONFIG_USER_ONLY
    if (get_page(tb->pc) != get_page(tb->s_data->x86_next_pc) ||
        !tu_tb_is_single_page(tb) || !tu_tb_is_single_page(next)) {
        return false;
    }
#else
    if (get_page(tb->pc) != get_page(tb->s_data->x86_next_pc) ||
        tb_page_addr1(tb) != -1 || tb_page_addr1(next) != -1) {
        return false;
    }
#endif

    return true;
}

static bool tu_direct_jump_cold_exit_eligible(TranslationBlock *tb,
                                              int succ_id)
{
    if (in_aot_translate || option_aot_save ||
        (tb_cflags(tb) & CF_NO_GOTO_TB)) {
        return false;
    }

    if (tb->s_data->last_ir1_type == IR1_TYPE_JUMP) {
        return succ_id == TU_TB_INDEX_TARGET;
    }
    return tb->s_data->last_ir1_type == IR1_TYPE_BRANCH;
}

static bool tu_indirect_jump_inline_exit_eligible(TranslationBlock *tb)
{
    return !in_aot_translate && !option_aot_save &&
        latxs_check_interrupt_at_block_end() &&
        tb->s_data->last_ir1_type == IR1_TYPE_JUMPIN &&
        !(tb_cflags(tb) & (CF_NO_GOTO_TB | CF_NO_GOTO_PTR));
}

static void tu_prepare_cold_exits(uint32_t tb_num_in_tu,
                                  TranslationBlock **tb_list)
{
    uint32_t i;

    for (i = 0; i < tb_num_in_tu; i++) {
        TranslationBlock *tb = tb_list[i];
        int succ_id;

        for (succ_id = 0; succ_id < 2; succ_id++) {
            tb->s_data->tu_cold_exit_offset[succ_id] = TU_COLD_EXIT_INVALID;
        }

        if (tu_call_cold_exit_eligible(tb_num_in_tu, tb_list, i)) {
            tb->s_data->tu_cold_exit_offset[0] = TU_COLD_EXIT_DIRECT_PENDING;
        } else if (tu_indirect_call_inline_exit_eligible(
                       tb_num_in_tu, tb_list, i)) {
            tb->s_data->tu_cold_exit_offset[0] = TU_INDIRECT_EXIT_INLINE_PENDING;
        }

        for (succ_id = 0; succ_id < 2; succ_id++) {
            if (tu_direct_jump_cold_exit_eligible(tb, succ_id)) {
                tb->s_data->tu_cold_exit_offset[succ_id] =
                    TU_COLD_EXIT_DIRECT_PENDING;
            }
        }
        if (tu_indirect_jump_inline_exit_eligible(tb)) {
            tb->s_data->tu_cold_exit_offset[TU_TB_INDEX_TARGET] =
                TU_INDIRECT_EXIT_INLINE_PENDING;
        }
    }
}

static bool tu_ras_has_tb(uint32 tb_num_in_tu,
                          TranslationBlock **tb_list,
                          TranslationBlock *tb)
{
    for (uint32 i = 0; i < tb_num_in_tu; i++) {
        if (tb_list[i] == tb) {
            return true;
        }
    }
    return false;
}

static bool tu_ras_tail_target_is_published(CPUState *cpu,
                                             uint32 tb_num_in_tu,
                                             TranslationBlock **tb_list,
                                             TranslationBlock *source,
                                             TranslationBlock *next)
{
    TranslationBlock *published;

    if (!cpu || !next || !next->tc.ptr ||
        (tb_cflags(next) & CF_INVALID) ||
        get_page(source->pc) != get_page(source->s_data->x86_next_pc) ||
        next->pc != source->s_data->x86_next_pc ||
        tu_ras_has_tb(tb_num_in_tu, tb_list, next)) {
        return false;
    }

    /*
     * Pointer identity proves that this is the already-published global-hash
     * TB for the complete source identity.  latxs_ras_link() rechecks target
     * validity while holding next->jmp_lock.
     */
    published = tb_htable_lookup_noexit(cpu, source->s_data->x86_next_pc,
                                        source->cs_base, source->flags,
                                        tb_cflags(source));
    return published == next;
}

static void tu_reset_private_ras(uint32_t count, TranslationBlock **tb_list)
{
    if (!option_rastoptp) return;
    /* These members have not been published. Detach every outgoing edge
     * before clearing state or overwriting the previous attempt's code. */
    for (uint32_t i = 0; i < count; i++) {
        latxs_ras_unlink(tb_list[i]);
    }
    for (uint32_t i = 0; i < count; i++) {
        TranslationBlock *tb = tb_list[i];
        g_assert(tb->preorder_next == NULL);
        qatomic_set(&tb->ras_dest, (uintptr_t)NULL);
        remove_misc_flags(tb, MF_RAS_CLEAR_B0 | MF_RAS_LINKED);
    }
}

int translate_tu(CPUState *cpu, uint32 tb_num_in_tu,
                 TranslationBlock **tb_list)
{
    TRANSLATION_DATA *td = lsenv->tr_data;

    td->hras_long_checks = false;
    if (tb_num_in_tu == 0) {
        return TU_TRANSLATE_OK;
    }
#ifdef CONFIG_LATX_TU
    latxs_ghbr_analyze_tu(tb_num_in_tu, tb_list);
    CPUX86State *env = cpu->env_ptr;

    latxs_shbr_analyze_tu(tb_num_in_tu, tb_list,
                          env->features[FEAT_1_ECX]);
#endif
    TranslationBlock *tb;
    bcc_jmp_fail = false;
    search_id = 0;
    search_size = 0;
    void *tu_code_start = tcg_ctx->code_gen_ptr;

#if defined(CONFIG_TCG_REGION_TREE_ENABLE)
    qatomic_set(&tcg_ctx->code_gen_ptr, (void *)
            ROUND_UP((uintptr_t)tcg_ctx->code_gen_ptr, CODE_GEN_ALIGN));
#else
//    uintptr_t last_tbmini_ptr =
//        (uintptr_t)(ROUND_UP(sizeof(struct TBMini) * (tb_num_in_tu + 1) +
//        (uintptr_t)tcg_ctx->code_gen_ptr, CODE_GEN_ALIGN));
//    qatomic_set(&tcg_ctx->code_gen_ptr, (void *)last_tbmini_ptr);
//
    uintptr_t tbmini_ptr = (uintptr_t)
                ROUND_UP((uintptr_t)tcg_ctx->code_gen_ptr, CODE_GEN_ALIGN);
    uintptr_t last_tbmini_ptr = tbmini_ptr +
                sizeof(struct TBMini) * (tb_num_in_tu + 1);
    qatomic_set(&tcg_ctx->code_gen_ptr, last_tbmini_ptr);

    /* The table header stores a member count rather than a TB pointer. */
    tu_tbmini_set_pointer((uint64_t *)tbmini_ptr, tb_num_in_tu, TU_TBM_MAGIC);
#endif

    uint64_t ghbr_checkpoint = latxs_ghbr_stats_checkpoint();
    void *hras_retry_start = tcg_ctx->code_gen_ptr;
    int translate_result;
retry:
    latxs_ghbr_stats_restore(ghbr_checkpoint);
    tu_prepare_cold_exits(tb_num_in_tu, tb_list);
    translate_result = translate_tb_in_tu(tb_num_in_tu, tb_list);
    lsenv->tr_data->gpr_highbits_dynamic_tu_active = false;
    if (translate_result == TU_TRANSLATE_FALLBACK) {
        td->hras_long_checks = false;
        latxs_ghbr_stats_restore(ghbr_checkpoint);
        qatomic_set(&tcg_ctx->code_gen_ptr, tu_code_start);
        return TU_TRANSLATE_FALLBACK;
    }
    if (translate_result == TU_TRANSLATE_HRAS_LONG_CHECK) {
        if (td->hras_long_checks) {
            /* Even the cold B cannot reach: discard this unpublished TU. */
            td->hras_long_checks = false;
            latxs_ghbr_stats_restore(ghbr_checkpoint);
            qatomic_set(&tcg_ctx->code_gen_ptr, tu_code_start);
            return TU_TRANSLATE_FALLBACK;
        }
        td->hras_long_checks = true;
        qatomic_set(&tcg_ctx->code_gen_ptr, hras_retry_start);
        goto retry;
    }
    if (translate_result != TU_TRANSLATE_OK) {
        td->hras_long_checks = false;
        latxs_ghbr_stats_restore(ghbr_checkpoint);
        return TU_TRANSLATE_RETRY;
    }

    tb_list[0]->s_data->tu_size = tcg_ctx->code_gen_ptr - tb_list[0]->tc.ptr;

    /*
     * AOT retranslation builds private heap TBs for serialization.  They are
     * neither published nor executed, so do not add them to the live RAS
     * reverse lists.  The save path closes their RAS sites before writing.
     */
    bool link_runtime_ras = option_soft_ras && !in_aot_translate;

    for (int i = 0; i < tb_num_in_tu; i++) {
        tb = tb_list[i];
        if (tb->s_data->last_ir1_type == IR1_TYPE_CALL) {
            if (link_runtime_ras) {
                TranslationBlock *next = tb->s_data->next_tb[TU_TB_INDEX_NEXT];
                if (tb->inst_ptr_offset) {
                    bool internal = next && i + 1 < tb_num_in_tu;
                    bool direct_tail = i + 1 == tb_num_in_tu &&
                        tu_ras_tail_target_is_published(cpu, tb_num_in_tu,
                                                        tb_list, tb, next);
                    bool can_link = internal || direct_tail;

                    bool linked = can_link && (option_rastoptp ?
                        latxs_ras_tp_link_initial(cpu, tb, next) :
                        latxs_ras_link(tb, next));
                    if (!linked && !option_rastoptp) {
                        latxs_ras_unlink(tb);
                        tb->inst_ptr_offset = 0;
                    }
                }
                continue;
            }
        }
        if (tb->s_data->last_ir1_type == IR1_TYPE_CALLIN) {
            if (link_runtime_ras && tb->inst_ptr_offset) {
                TranslationBlock *next =
                    tb->s_data->next_tb[TU_TB_INDEX_NEXT];

                /*
                 * A RAS hit jumps to next->tc.ptr without page validation.
                 * HRAS requires all TU entry page checks; other modes
                 * retain the original single-page restriction.
                 */
                bool can_link = next &&
                      tu_ras_has_tb(tb_num_in_tu, tb_list, next) &&
                      ((option_tu_hras && TB_TU_PAGES(tb) &&
                        TB_TU_PAGES(tb) == TB_TU_PAGES(next)) ||
                       (tb_page_addr1(tb) == -1 &&
                        tb_page_addr1(next) == -1));
                bool linked = can_link && (option_rastoptp ?
                    latxs_ras_tp_link_initial(cpu, tb, next) :
                    latxs_ras_link(tb, next));
                if (!linked && !option_rastoptp) {
                    latxs_ras_unlink(tb);
                    tb->inst_ptr_offset = 0;
                }
            }
        }
        if ((tb->s_data->tu_jmp[TU_TB_INDEX_TARGET] !=
             TB_JMP_RESET_OFFSET_INVALID)) {
            if (tu_relocat_target_branch(tb)) {
                tu_reset_private_ras(tb_num_in_tu, tb_list);
                qatomic_set(&tcg_ctx->code_gen_ptr, (void *)
                    ROUND_UP((uintptr_t)(tb_list[0]->tc.ptr), CODE_GEN_ALIGN));
                assert(bcc_jmp_fail);
                goto retry;
            }
            tb->s_data->tu_link_ins = *(uint32_t *)((uintptr_t)tb->tc.ptr +
                    tb->s_data->tu_jmp[TU_TB_INDEX_TARGET] -
                    tb->tc.offset_in_tu);
        }
        TranslationBlock *next_tb = tb->s_data->next_tb[TU_TB_INDEX_NEXT];
        if (next_tb && tb->s_data->tu_jmp[TU_TB_INDEX_NEXT] !=
                TB_JMP_RESET_OFFSET_INVALID &&
            !tb_is_adjacent(tb, tb->s_data->next_tb[TU_TB_INDEX_NEXT])) {
            tu_relocat_next_branch(tb);
        }
    }

    if (!option_split_search) {
        /*search data*/
        for (int i = 0; i < tb_num_in_tu; i++) {
            tb = tb_list[i];
            tb->search_ptr = (uint8_t *)
                ((uintptr_t)tcg_ctx->code_gen_ptr + search_buff_offset[i]);
        }
        memcpy(tcg_ctx->code_gen_ptr, search_buff, search_size);
        qatomic_set(&tcg_ctx->code_gen_ptr, (void *)
                    ROUND_UP((uintptr_t)tcg_ctx->code_gen_ptr + search_size,
                             CODE_GEN_ALIGN));
    }
    tb_list[0]->s_data->tu_size = tcg_ctx->code_gen_ptr - tb_list[0]->tc.ptr;

    td->hras_long_checks = false;
    return TU_TRANSLATE_OK;
}

#ifdef CONFIG_LATX_PRINT
int tb_trans_cnt[8];
#endif

static TranslationBlock *register_tu(CPUState *cpu, TranslationBlock *entry,
                                     uint32_t count, TranslationBlock **tb_list)
{
    TranslationBlock *published_entry = entry;

    /* A reader can enter any published member and reach the whole unit. */
    for (uint32_t i = 0; i < count; i++) {
        TranslationBlock *tb = tb_list[i];

        /* tb_create rejects these before adding the member to the unit. */
        tcg_debug_assert(tb->s_data->tu_tb_mode != TU_TB_MODE_BROKEN &&
                         tb->s_data->tu_tb_mode != BAD_TB);
        tb->s_data->next_tb[0] = i ? tb_list[i - 1] : NULL;
        tb->s_data->next_tb[1] = i + 1 < count ? tb_list[i + 1] : NULL;
        tcg_tb_insert(tb);
    }

    for (uint32_t i = 0; i < count; i++) {
        TranslationBlock *tb = tb_list[i];
        TranslationBlock *published = tb_link_page(tb, count > 1);

        if (unlikely(published != tb)) {
            if (tb == entry) {
                published_entry = published;
            }
            if (count == 1) {
                /* No sibling can enter this code. Drop the outgoing RAS edge
                 * and host-PC record before returning the canonical entry. */
                latxs_ras_unlink(tb);
                tcg_tb_remove(tb);
            }
        }
#ifdef CONFIG_LATX_PRINT
        if (tb->flags & HF_CPL_MASK) {
            tb_trans_cnt[cpu->cpu_index + 4]++;
        } else {
            tb_trans_cnt[cpu->cpu_index]++;
        }
#endif
    }
    return published_entry;
}

static int tu_mapped_pages_num(void)
{
    int nr_pages = 0;

    for (int i = 0; i < tu_data->pages_num; i++) {
        if (tu_data->pages[i].pp != UINT64_MAX &&
            tu_data->pages[i].tb_num != 0) {
            nr_pages++;
        }
    }

    return nr_pages;
}

uint32_t tu_pages_alloc_id(void)
{
    uint64_t id;
    uint64_t capacity = option_tucp_generation_array_size /
        tucp_generation_entry_size();

    if (!tucp_generation_enabled()) {
        return 0;
    }

    id = qatomic_fetch_inc(&tu_pages_next_id);
    if (id >= capacity || id > UINT32_MAX) {
        if (qatomic_cmpxchg(&tu_pages_id_exhausted_warned, 0, 1) == 0) {
            fprintf(stderr, "WARNING: tucp generation ID capacity exhausted; "
                    "new TUs will use uncached page checks until TB flush.\n");
        }
        return 0;
    }
    return id;
}

static TranslationUnitPages *tu_build_pages_desc(TCGContext *tcg_ctx)
{
    TranslationUnitPages *desc;
    int nr_pages = tu_mapped_pages_num();
    if (nr_pages == 0) { return NULL; }

    QEMU_BUILD_BUG_ON(TARGET_PAGE_BITS != TU_PAGE_DESC_PAGE_BITS);
    QEMU_BUILD_BUG_ON(TARGET_PHYS_ADDR_SPACE_BITS >
                      TU_PAGE_DESC_MIX_PHYS_BITS + TU_PAGE_DESC_PAGE_BITS);

    desc = region_alloc_tu_pages(tcg_ctx);
    if (!desc) { return NULL; }

    memset(desc, 0xff, sizeof(*desc));
    desc->id = tu_pages_alloc_id();
    desc->hras_members = NULL;
    desc->hras_member_count = 0;
    desc->native = NULL;
    nr_pages = 0;
    for (int i = 0; i < tu_data->pages_num; i++) {
        if (tu_data->pages[i].pp == UINT64_MAX ||
            tu_data->pages[i].tb_num == 0) {
            continue;
        }
        uint64_t vp = tu_data->pages[i].vp & TARGET_PAGE_MASK;
        uint64_t pp = tu_data->pages[i].pp & TARGET_PAGE_MASK;

        desc->pages[nr_pages].vp_phys_hi = tu_page_desc_pack_vp(vp, pp);
        desc->pages[nr_pages].mix = tb_cal_page_addr_mix(vp, pp);
        nr_pages++;
    }

    return desc;
}

TranslationUnitPages *tb_build_pages_desc(TCGContext *tcg_ctx,
                                           const TranslationBlock *tb)
{
    TranslationUnitPages *desc;
    int nr_pages = 1;

    lsassert(tb_page_addr0(tb) != (tb_page_addr_t)-1);
    if (tb_page_addr1(tb) != (tb_page_addr_t)-1) {
        nr_pages++;
    }

    desc = region_alloc_tu_pages(tcg_ctx);
    if (!desc) {
        return NULL;
    }

    memset(desc, 0xff, sizeof(*desc));
    desc->id = 0;
    desc->hras_members = NULL;
    desc->hras_member_count = 0;
    desc->native = NULL;
    for (int i = 0; i < nr_pages; i++) {
        uint64_t vp = (tb->pc + i * TARGET_PAGE_SIZE) & TARGET_PAGE_MASK;
        uint64_t pp = (i ? tb_page_addr1(tb) : tb_page_addr0(tb)) &
                      TARGET_PAGE_MASK;

        desc->pages[i].vp_phys_hi = tu_page_desc_pack_vp(vp, pp);
        desc->pages[i].mix = tb_cal_page_addr_mix(vp, pp);
    }

    return desc;
}

static void tu_build_hras_members(TranslationBlock **tb_list, uint32_t count)
{
    TranslationUnitPages *desc = TB_TU_PAGES(tb_list[0]);
    TranslationBlock **members;

    if (!desc) {
        return;
    }
    members = region_alloc_tu_members(tcg_ctx, count);
    if (!members) {
        return;
    }
    memcpy(members, tb_list, count * sizeof(*members));
    qsort(members, count, sizeof(*members), tb_sort_cmp);
    desc->hras_members = members;
    desc->hras_member_count = count;
}

static bool tu_pages_cache_hit(CPUX86State *env,
                               const TranslationUnitPages *desc,
                               void **slot)
{
    *slot = NULL;
    if (!tucp_generation_enabled() || !env->tu_generation_array) {
        return false;
    }

    if (tucp_generation_counter()) {
        uint16_t *array = (uint16_t *)(uintptr_t)
            DMW_OUT(env->tu_generation_array);
        uint16_t *entry = &array[desc->id];

        if (*entry == env->tu_generation) {
            return true;
        }
        if (desc->id != 0) {
            *slot = entry;
        }
    } else {
        uint8_t *array = (uint8_t *)(uintptr_t)
            DMW_OUT(env->tu_generation_array);
        uint8_t *entry = &array[desc->id];

        if (*entry) {
            return true;
        }
        if (desc->id != 0) {
            *slot = entry;
        }
    }
    return false;
}

static void tu_pages_cache_update(CPUX86State *env, void *slot)
{
    if (!slot) {
        return;
    }
    if (tucp_generation_counter()) {
        *(uint16_t *)slot = env->tu_generation;
    } else {
        *(uint8_t *)slot = 1;
    }
}

bool tu_pages_valid(CPUState *cpu, const TranslationBlock *tb)
{
    CPUX86State *env = cpu->env_ptr;
    TranslationUnitPages *desc = TB_TU_PAGES(tb);
    void *generation_slot;

    if (!desc) { return true; }

    if (tu_pages_cache_hit(env, desc, &generation_slot)) {
        return true;
    }

    for (int i = 0; i < MAX_PAGE_IN_TU; i++) {
        uint64_t packed_vp = desc->pages[i].vp_phys_hi;
        uint64_t vp;
        tb_page_addr_t pp;

        if (packed_vp == UINT64_MAX) {
            tu_pages_cache_update(env, generation_slot);
            return true;
        }
        vp = tu_page_desc_vp(packed_vp);
        pp = get_page_addr_code_noexit(env, vp);
        if (pp == (tb_page_addr_t)-1 ||
            (pp & TARGET_PAGE_MASK) !=
                tu_page_desc_phys(packed_vp, desc->pages[i].mix)) {
            return false;
        }
    }

    tu_pages_cache_update(env, generation_slot);
    return true;
}

void tu_generation_invalidate(CPUState *cpu)
{
    CPUX86State *env = cpu->env_ptr;

#if defined(CONFIG_LATX_TU) && defined(CONFIG_SOFTMMU)
    latxs_aot_close_open_linked_calls(env);
#endif
    if (!tucp_generation_enabled() || !env->tu_generation_array) {
        return;
    }

    if (tucp_generation_counter()) {
        uint16_t generation = env->tu_generation + 1;

        if (generation == 0) {
            memset((void *)(uintptr_t)DMW_OUT(env->tu_generation_array), 0,
                   option_tucp_generation_array_size);
            generation = 1;
        }
        env->tu_generation = generation;
    } else {
        uint64_t clear_size = MIN(qatomic_read(&tu_pages_next_id),
                                  option_tucp_generation_array_size);

        memset((void *)(uintptr_t)DMW_OUT(env->tu_generation_array), 0,
               clear_size);
    }
}

void tu_generation_reset_ids(void)
{
    qatomic_set(&tu_pages_next_id, 1);
}

TranslationBlock *tu_gen_code(CPUState *cpu,
                              target_ulong pc, target_ulong cs_base,
                              uint32_t flags, int cflags)
{
    int explore_result;
    /* TODO: profilea support */
    assert_memory_lock();
    lsenv->tr_data->is_tu_mode = 1;

    tu_reset_tb_flush();
    tu_reset_gencode_state();
    TranslationBlock *entry;
re_translate:
    if (tu_need_tb_flush()) {
        tu_do_tb_flush(cpu); /* never return */
        g_assert_not_reached();
    }
    entry = tb_explore(cpu, pc, cs_base, flags, cflags, &explore_result);
    if (explore_result == TU_EXPLORE_NEED_PROTECT) {
        if (!tu_page_lock_snapshot_retry()) {
            explore_result = TU_EXPLORE_FALLBACK;
        } else {
            tb_unlock_ctx_pages(1);
            tu_reset_explore_state();
            if (tu_page_lock_prepare_retry(cpu)) {
                goto re_translate;
            }
            explore_result = TU_EXPLORE_FALLBACK;
        }
    }
    if (explore_result == TU_EXPLORE_RESTART_LOCK_ORDER) {
        tb_unlock_ctx_pages(1);
        tu_reset_explore_state();
        tu_page_lock_note_order_retry();
        goto re_translate;
    }
    if (explore_result == TU_EXPLORE_EXISTED) {
        goto validate_existing;
    }
    if (explore_result == TU_EXPLORE_FALLBACK || unlikely(entry == NULL)) {
        goto fallback_to_tb;
    }
    TranslationBlock** tb_list = tu_data->tb_list;
    uint32_t *tb_num_in_tu = &tu_data->tb_num;

    int mapped_pages = tu_mapped_pages_num();
    if ((mapped_pages > 1 || tucp_fast_needs_pages_desc()) &&
            (flags & HF_CPL_MASK)) {
        TranslationUnitPages *tu_pages = tu_build_pages_desc(tcg_ctx);

        if (!tu_pages && mapped_pages > 0) {
            tu_force_tb_flush();
            tb_unlock_ctx_pages(1);
            tu_reset_explore_state();
            goto re_translate;
        }
        if (tu_pages) {
            for (int i = 0; i < *tb_num_in_tu; i++) {
                TB_TU_PAGES(tb_list[i]) = tu_pages;
            }
        }
    }

    /* Member allocation may have switched the active code region. */
    latxs_tr_sync_region_offset(tcg_ctx->mtcc_id);
    int translate_result =
        translate_tu(cpu, *tb_num_in_tu, tb_list);
    if (translate_result == TU_TRANSLATE_FALLBACK) {
        goto fallback_to_tb;
    }
    if (translate_result != TU_TRANSLATE_OK) {
#ifdef CONFIG_LATX_PRINT
        printf("%s:%d CPU[%d] TU retranslation.\n", __func__, __LINE__,
                cpu->cpu_index);
#endif
        tb_unlock_ctx_pages(1);
        tu_reset_explore_state();
        goto re_translate;
    }
    if (option_tu_hras) {
        tu_build_hras_members(tb_list, *tb_num_in_tu);
    }
    TranslationBlock *published = register_tu(cpu, entry, *tb_num_in_tu,
                                              tb_list);
    latxs_ghbr_stats_commit();
    tb_unlock_ctx_pages(1);
    assert_no_pages_locked();

    if (likely(published == entry)) {
        return entry;
    }
    entry = published;
validate_existing:
    /* The QHT identity covers this member. Its owner may depend on other
     * guest pages with different mappings on the current vCPU. */
    if (tu_pages_valid(cpu, entry)) {
        return entry;
    }
#ifdef CONFIG_SOFTMMU
    flags |= HF_FORCE_TB_MODE;
    cpu->env_ptr->hflags |= HF_FORCE_TB_MODE;
#endif
fallback_to_tb:
    tb_unlock_ctx_pages(1);
    tu_reset_explore_state();
    lsenv->tr_data->is_tu_mode = 0;
    entry = tb_gen_code(cpu, pc, cs_base, flags, cflags);
    /*
     * The ordinary-TB path may already have resolved and published a
     * usable RAS return target.  Preserve that link; only permanently
     * disable a site that still has no valid target.
     */
    if (!check_misc_flags(entry, MF_RAS_LINKED)) {
        latxs_ras_unlink(entry);
    }
    return entry;
}

bool tb_is_in_tb_list(TranslationBlock *target_tb)
{
    TranslationBlock** tb_list = tu_data->tb_list;
    uint32_t tb_num_in_tu = tu_data->tb_num;

    if (option_tu_hras && !in_aot_translate) {
        return tu_sorted_tb_index(tb_num_in_tu, tb_list, target_tb) >= 0;
    }
    for (int i = 0; i < tb_num_in_tu; ++i) {
        if (tb_list[i] == target_tb) return true;
    }
    return false;
}

bool tb_is_adjacent(TranslationBlock *front_tb, TranslationBlock *back_tb)
{
    TranslationBlock** tb_list = tu_data->tb_list;
    uint32_t tb_num_in_tu = tu_data->tb_num;

    if (option_tu_hras && !in_aot_translate) {
        int index = tu_sorted_tb_index(tb_num_in_tu, tb_list, front_tb);

        return index >= 0 && index + 1 < tb_num_in_tu &&
               tb_list[index + 1] == back_tb;
    }
    for (int i = 0; i < tb_num_in_tu - 1; ++i) {
        if (tb_list[i] == front_tb) {
            if (i + 1 < tb_num_in_tu && tb_list[i + 1] == back_tb) {
                return true;
            } else {
                return false;
            }
        }
    }
    return false;
}

void tu_jcc_nop_gen(TranslationBlock *tb, int opnd_num, bool is_target)
{
    TranslationBlock *target_tb;
    uint32_t *inst_ptr;
    if (is_target) {
        target_tb = tb->s_data->next_tb[TU_TB_INDEX_TARGET];
        inst_ptr = (uint32_t *)((uintptr_t)tb->tc.ptr +
                tb->s_data->tu_jmp[TU_TB_INDEX_TARGET]);
    } else {
        target_tb = tb->s_data->next_tb[TU_TB_INDEX_NEXT];
        inst_ptr = (uint32_t *)((uintptr_t)tb->tc.ptr +
                tb->s_data->tu_jmp[TU_TB_INDEX_NEXT]);
    }

    if (tb_is_in_tb_list(target_tb)) {
        if (!is_target || ir1_opcode(lsenv->tr_data->curr_ir1_inst) == X86_INS_JMP) {
            latxs_append_ir2_opnda(LISA_B, 0);
        }
        return;
    }

    long offset = ((long)target_tb->tc.ptr - (long)inst_ptr) >> 2;

    if (offset < OFF26_MIN || offset > OFF26_MAX) {
        IR2_OPND itemp = latxs_ra_alloc_itemp();

        latxs_append_ir2_opnd1i(LISA_PCADDU18I, &itemp, 0);
        latxs_append_ir2_opnd2i(LISA_JIRL, &latxs_zero_ir2_opnd, &itemp, 0);
        latxs_ra_free_temp(&itemp);
    } else if ((opnd_num == 2 && (offset < OFF20_MIN || offset > OFF20_MAX))
               || (opnd_num == 3 && (offset < OFF16_MIN || offset > OFF16_MAX))
               || !is_target || tb->s_data->last_ir1_type == IR1_TYPE_JUMP) {
        latxs_append_ir2_opnda(LISA_B, 0);
    }
}

#ifdef CONFIG_LATX_PROFILER
void add_tu_profiler(TranslationBlock *tb)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    if (td->is_tu_mode)
    {
        latxs_tr_save_temp_register_mask(0xffff);
        td->itemp_mask = 0;

        IR2_OPND tmp0 = la_new_itmp();
        IR2_OPND tmp1 = la_new_itmp();
        IR2_OPND tmp2 = la_new_itmp();

        la_out2(LISA_RDTIME_D, &tmp1, &la_zero);
        la_out2i(LISA_LD_D, &tmp0, &la_env, offsetof(CPUX86State, rdtime));
        la_out2i(LISA_ST_D, &tmp1, &la_env, offsetof(CPUX86State, rdtime));

        // tmp0 = end - start
        la_out3 (LISA_SUB_D, &tmp0, &tmp1, &tmp0);

        // tmp1 = &(tbprofile.exec_rdtime)
        latxs_profile_address(&tmp1, tb, &tb->profile.exec_rdtime);

        // tbprofile.exec_rdtime += tmp1
        la_out3(LISA_AMADD_D, &la_zero, &tmp1, &tmp0);
    //    la_out2i(LISA_LD_D, &tmp2, &tmp1, 0);
    //    la_out3(LISA_ADD_D, &tmp2, &tmp2, &tmp0);
    //    la_out2i(LISA_ST_D, &tmp2, &tmp1, 0);
        if (option_hamt) {
            la_out2i(LISA_LD_D, &tmp1, &la_env, offsetof(CPUX86State, hamt_data_ptr));
            if (hamt_enabled()) latxs_tr_dmw_in_fix_mem(&tmp1);
            la_out2i(LISA_LD_D, &tmp1, &tmp1, HD_TLBR_RDTIME_ALL * 8);
            la_out2i(LISA_LD_D, &tmp0, &la_env, offsetof(CPUX86State, rdtime_tlbr));
            la_out3 (LISA_SUB_D, &tmp0, &tmp1, &tmp0);
            latxs_profile_address(&tmp1, tb, &tb->profile.exec_tlbr_rdtime);
            la_out3(LISA_AMADD_D, &la_zero, &tmp1, &tmp0);
        }

        la_tmp_free(tmp0);
        la_tmp_free(tmp1);
        la_tmp_free(tmp2);

        latxs_tr_restore_temp_register_mask(0xffff);
    }
}
#endif

void add_tu(IR2_OPCODE jcc_op, IR2_OPND cond0, IR2_OPND cond1, IR2_OPND target_label_opnd, IR1_INST *pir1)
{
    TranslationBlock *tb = lsenv->tr_data->curr_tb;
    TranslationBlock *next_tb = tb->s_data->next_tb[TU_TB_INDEX_NEXT];
    TranslationBlock *target_tb = tb->s_data->next_tb[TU_TB_INDEX_TARGET];

    tb->s_data->tu_jmp[TU_TB_INDEX_TARGET] = TB_JMP_RESET_OFFSET_INVALID;
    tb->s_data->tu_jmp[TU_TB_INDEX_NEXT] = TB_JMP_RESET_OFFSET_INVALID;

#ifdef CONFIG_LATX_PROFILER
    add_tu_profiler(tb);
#endif

    if (latxs_ir2_opnd_is_inv(&target_label_opnd)) {
        /* support instptn that never jmp to target */
        TU_EFLAG_OPT_GEN(tu_data);
        if (next_tb && tb_is_in_tb_list(next_tb)) {
            if (!tb_is_adjacent(tb, next_tb)) {
                tb->s_data->tu_jmp[TU_TB_INDEX_NEXT] =
                    get_current_inst_addr(tb) - (uintptr_t)tb->tc.ptr;
                tu_jcc_nop_gen(tb, 1, false);
            }
        } else {
            latxs_tr_generate_exit_tb(pir1, 0);
        }
        return;
    }

    if (latxs_ir2_opnd_is_inv(&cond0)) {
        /* support instptn that only jmp to target */
        /*
         * NEXT flags belong to the unreachable fallthrough edge.  Do not
         * carry their operands into another TB, whose registers and flags
         * may already have been replaced when add_tu() is called again.
         */
        TU_EFLAG_OPT_RESET(tu_data);
        if (target_tb && tb_is_in_tb_list(target_tb) &&
            !(latxs_check_interrupt_at_block_end() &&
              target_tb->pc <= tb->pc)) {
            tb->s_data->tu_jmp[TU_TB_INDEX_TARGET] =
                get_current_inst_addr(tb) - (uintptr_t)tb->tc.ptr;
            latxs_append_ir2_opnda(LISA_B, 0);
        } else {
            latxs_tr_generate_exit_tb(pir1, 1);
        }
        return;
    }

    if (tb->s_data->next_tb[TU_TB_INDEX_NEXT] && tb->s_data->next_tb[TU_TB_INDEX_TARGET]) {
        if ((tb_is_in_tb_list(target_tb)) && (tb_is_in_tb_list(next_tb))) {
            if (latxs_check_interrupt_at_block_end() &&
                target_tb->pc <= tb->pc) {
                /* need interrupt check before jmp to target */
                goto only_next_in_tu;
            }
            if (!tucp_fast_tu_direct() &&
                get_page(target_tb->pc) != get_page(tb->pc)) {
                /* need cross-page check before jmp to target */
                goto only_next_in_tu;
            }
            tb->s_data->tu_jmp[TU_TB_INDEX_TARGET] =
                get_current_inst_addr(tb) - (uintptr_t)tb->tc.ptr;
            if (!latxs_ir2_opnd_is_inv(&cond1)) {
                latxs_append_ir2_opnd3(jcc_op, &cond0, &cond1, &target_label_opnd);
            } else {
                latxs_append_ir2_opnd2(jcc_op, &cond0, &target_label_opnd);
            }
            if (tb_is_adjacent(tb, next_tb)) {
                tb->s_data->tu_jmp[TU_TB_INDEX_NEXT] =
                    TB_JMP_RESET_OFFSET_INVALID;
                TU_EFLAG_OPT_GEN(tu_data);
            } else {
                TU_EFLAG_OPT_GEN(tu_data);
                tb->s_data->tu_jmp[TU_TB_INDEX_NEXT] =
                    get_current_inst_addr(tb) - (uintptr_t)tb->tc.ptr;
                tu_jcc_nop_gen(tb, latxs_ir2_opnd_is_inv(&cond1) ? 2 : 3, false);
            }
            return;
        } else if (tb_is_in_tb_list(next_tb)) {
            goto only_next_in_tu;
        } else if (tb_is_in_tb_list(target_tb)) {
            goto only_target_in_tu;
        } else {
            goto no_one_in_tu;
        }
    } else if (tb->s_data->next_tb[TU_TB_INDEX_NEXT]) {
        if (tb_is_in_tb_list(next_tb)) {
            goto only_next_in_tu;
        } else {
            goto no_one_in_tu;
        }
    } else if (tb->s_data->next_tb[TU_TB_INDEX_TARGET]) {
        if (tb_is_in_tb_list(target_tb)) {
            goto only_target_in_tu;
        } else {
            goto no_one_in_tu;
        }
    } else {
        fprintf(stderr, "%s: %d, should not happen\n", __func__, __LINE__);
    }

only_next_in_tu:
    if (!tucp_fast_tu_direct() &&
        get_page(next_tb->pc) != get_page(tb->pc)) {
        /* need cross-page check before jmp to next */
        goto no_one_in_tu;
    }
    if (tb_is_adjacent(tb, next_tb)) {
        IR2_OPND next_label_opnd = latxs_ir2_opnd_new_label();
        IR2_OPCODE convert_op = bcc_opcode_convert(jcc_op);

        if (!latxs_ir2_opnd_is_inv(&cond1)) {
            latxs_append_ir2_opnd3(convert_op, &cond0, &cond1, &next_label_opnd);
        } else {
            latxs_append_ir2_opnd2(convert_op, &cond0, &next_label_opnd);
        }
        latxs_tr_generate_exit_tb(pir1, 1);
        latxs_append_ir2_opnd1(LISA_LABEL, &next_label_opnd);
        TU_EFLAG_OPT_GEN(tu_data);
    } else {
        if (!latxs_ir2_opnd_is_inv(&cond1)) {
            latxs_append_ir2_opnd3(jcc_op, &cond0, &cond1, &target_label_opnd);
        } else {
            latxs_append_ir2_opnd2(jcc_op, &cond0, &target_label_opnd);
        }
        TU_EFLAG_OPT_GEN(tu_data);
        tb->s_data->tu_jmp[TU_TB_INDEX_NEXT] =
            get_current_inst_addr(tb) - (uintptr_t)tb->tc.ptr;
        tu_jcc_nop_gen(tb, latxs_ir2_opnd_is_inv(&cond1) ? 2 : 3, false);
        latxs_append_ir2_opnd1(LISA_LABEL, &target_label_opnd);
        latxs_tr_generate_exit_tb(pir1, 1);
    }
    return;
only_target_in_tu:
    if (latxs_check_interrupt_at_block_end() && target_tb->pc <= tb->pc) {
        /* need interrupt check before jmp to target */
        goto no_one_in_tu;
    }
    if (!tucp_fast_tu_direct() &&
        get_page(target_tb->pc) != get_page(tb->pc)) {
        /* need cross-page check before jmp to target */
        goto no_one_in_tu;
    }
    tb->s_data->tu_jmp[TU_TB_INDEX_NEXT] = TB_JMP_RESET_OFFSET_INVALID;

    tb->s_data->tu_jmp[TU_TB_INDEX_TARGET] =
        get_current_inst_addr(tb) - (uintptr_t)tb->tc.ptr;
    if (!latxs_ir2_opnd_is_inv(&cond1)) {
        latxs_append_ir2_opnd3(jcc_op, &cond0, &cond1, &target_label_opnd);
    } else {
        latxs_append_ir2_opnd2(jcc_op, &cond0, &target_label_opnd);
    }
    TU_EFLAG_OPT_GEN(tu_data);
    latxs_tr_generate_exit_tb(pir1, 0);
    return;
no_one_in_tu:
    if (!latxs_ir2_opnd_is_inv(&cond1)) {
        latxs_append_ir2_opnd3(jcc_op, &cond0, &cond1, &target_label_opnd);
    } else {
        latxs_append_ir2_opnd2(jcc_op, &cond0, &target_label_opnd);
    }
    TU_EFLAG_OPT_GEN(tu_data);
    latxs_tr_generate_exit_tb(pir1, 0);
    latxs_append_ir2_opnd1(LISA_LABEL, &target_label_opnd);
    latxs_tr_generate_exit_tb(pir1, 1);
    return;

}

static bool latxs_tr_ir2_generate_in_tu(TranslationBlock *tb)
{
#ifdef LATXS_IMM_CACHE_DEBUG
    if (option_imm_debug) {
        static long hit_count = 0;
        imm_log_tb_pc(tb->pc);
        // imm_cache_set_dynamic_option(false,"enter latxs_tr_ir2_generate");
        if (imm_skip_pc || imm_pc_mask) {
            imm_cache_set_dynamic_option(false, "debug enter");
            bool hit = false;
            // unsigned long long pc =  0xfffff880012670b1;
            // unsigned long long prefix = 0xfffff80001200000;
            // unsigned long long mask =   0xfffffffffff00000;
            if (imm_skip_pc && tb->pc == imm_skip_pc) {
                hit = true;
            } else if (imm_pc_mask && ((tb->pc & imm_pc_mask) == imm_pc_prefix)) {
                hit = true;
            }
            if (hit) {
                hit_count++;
                printf("----------------------\n");
                printf(
                    "[%d][%ld]hit debug_pc %lx\n", qemu_get_thread_id(), hit_count, imm_pc_prefix);
                printf("----------------------\n");
                imm_trace("dynamic_set_option true hit\n");
                imm_cache_set_dynamic_option(true, "debug hit");
            }
        }
    }
#endif
    TRANSLATION_DATA *td = lsenv->tr_data;
    int i = 0;

    IR1_INST *ir1_list = td->ir1_inst_array;
    int ir1_nr = td->ir1_nr;

    latxs_tr_gen_tb_start();

#ifdef CONFIG_LATX_PROFILER
    per_tb_count((void *)&((tb->profile).exec_times), 1);
    {
        IR2_OPND tmp = la_new_itmp();
        // env.rdtime = start
        la_out2(LISA_RDTIME_D, &tmp, &la_zero);
        la_out2i(LISA_ST_D, &tmp, &la_env, offsetof(CPUX86State, rdtime));
        la_tmp_free(tmp);
    }
    if (option_hamt)
    {
        IR2_OPND t0 = la_new_itmp();
        la_out2i(LISA_LD_D, &t0, &la_env, offsetof(CPUX86State, hamt_data_ptr));
        if (hamt_enabled()) latxs_tr_dmw_in_fix_mem(&t0);
        la_out2i(LISA_LD_D, &t0, &t0, HD_TLBR_RDTIME_ALL * 8);
        la_out2i(LISA_ST_D, &t0, &la_env, offsetof(CPUX86State, rdtime_tlbr));
        la_tmp_free(t0);
    }
#endif
    /* PCREL trans_init, use eip instead */
    td->pc_save = get_eip_cpu_state(tb);
    td->pc_curr = td->pc_save;
    td->pc_next = td->pc_save;

    IR1_INST *pir1 = NULL;

    td->sp_patch = 0;

    for (i = 0; i < ir1_nr; ++i) {
        td->curr_ir1_nr = i;
        pir1 = ir1_list + i;

        //if (option_traceir1)
        //    latxs_option_traceir1(pir1);

#ifdef LATXS_IMM_CACHE_ENABLE
        if (can_do_imm()) {
            IMM_CACHE *imm_cache = lsenv->tr_data->imm_cache;
            imm_cache->curr_ir1_index = i;
            imm_cache->curr_ir2_index = lsenv->tr_data->ir2_cur_nr;
        }
#endif

        latxs_tr_init_translate_ir1(tb, i);
        if (pir1->shbr_restore_zero64_before) {
            latxs_tr_shbr_restore_zero64(pir1->shbr_restore_zero64_before);
        }

        if ((td->max_insns == i + 1) && (td->sys.cflags & CF_LAST_IO)) {
            latxs_tr_gen_io_start();
        } 

        if (option_dump_ir1 && qemu_log_in_addr_range(tb->pc)) {
            fprintf(stderr, "%lx ir1 translate >>> ", ir1_addr(pir1) + td->sys.cs_base);
            ir1_dump(pir1);
        }

#ifndef CONFIG_LATX_OPT
        if (td->sys.code64) {
            lsassert(((CPUX86State *)lsenv->cpu_state)->segs[cs_index].base ==
                     0);
            lsassert(((CPUX86State *)lsenv->cpu_state)->segs[ds_index].base ==
                     0);
            lsassert(((CPUX86State *)lsenv->cpu_state)->segs[es_index].base ==
                     0);
            lsassert(((CPUX86State *)lsenv->cpu_state)->segs[ss_index].base ==
                     0);
            if (ir1_opcode(pir1) != X86_INS_CALL) {
                if (latxs_ir1_has_prefix_addrsize(pir1)) {
                    ir1_dump(pir1);
                }
            }
            int opnd_num = ir1_opnd_num(pir1);
            for (size_t i = 0; i < opnd_num; i++) {
                IR1_OPND *op = ir1_get_opnd(pir1, i);
                if (ir1_opnd_is_mem(op) && ir1_opnd_has_seg(op) &&
                    !latxs_ir1_is_nop(pir1)) {
                    int seg_num = ir1_opnd_get_seg_index(op);
                    if (seg_num == cs_index || seg_num == ds_index ||
                        seg_num == es_index || seg_num == ss_index) {
                        lsassert(!lsenv->tr_data->sys.addseg);
                    }
                }
            }
        }
#endif


        td->pc_next += latxs_ir1_inst_size(pir1);
        bool clear_esp = latx_adjust_sp_before_translate(tb, pir1, i);
        bool translation_success = ir1_translate(pir1);
        if (pir1->shbr_restore_zero64_after) {
            latxs_tr_shbr_restore_zero64(pir1->shbr_restore_zero64_after);
        }
        if (option_pushpop &&
            (td->sys.flags & HF_CPL_MASK) &&
            clear_esp) {
            td->sp_patch = 0;
        }
        td->pc_curr = td->pc_next;


        (void)translation_success; /* to avoid warning  */
        lsassertm(translation_success,
                "translate failed for inst %s\n",
                pir1->info->mnemonic);
#ifdef LATXS_IMM_CACHE_ENABLE
        if (can_do_imm()) {
            IMM_CACHE *imm_cache = lsenv->tr_data->imm_cache;
            if (option_imm_pattern) {
                imm_cache_update_ir1_usage(pir1, i);
            }
            if (imm_cache->optimized_ir2) {
                imm_cache_print_ir1(pir1);
                imm_cache_print_tr_ir2();
                imm_log("-------------------------------------------------\n\n");
                imm_cache->optimized_ir2 = false;
            }
        }
#endif
        if (td->end_with_exception) {
#ifdef CONFIG_LATX_TU
            tb->s_data->tu_jmp[TU_TB_INDEX_TARGET] =
                TB_JMP_RESET_OFFSET_INVALID;
            tb->s_data->tu_jmp[TU_TB_INDEX_NEXT] =
                TB_JMP_RESET_OFFSET_INVALID;
#endif
            /* 1. get real number of IR1 */
            int real_ir1_nr = i + 1;
            td->ir1_nr = real_ir1_nr;
            /* 2. Reset related fields in ETB */
            for (++i; i < ir1_nr; ++i) {
                pir1 = ir1_list + i;
                latxs_ir1_free_info(pir1);
            }
            tb_shrink_icount(tb, real_ir1_nr,
                             option_split_search && option_pushpop);
            /* 3. ICOUNT: decrease real number of IR1 */
            if (tb->cflags & CF_USE_ICOUNT) {
                IR2_INST *pir2 = ir2_get(td->dec_icount_inst_id);
                pir2->_opnd[2] = latxs_ir2_opnd_new(IR2_OPND_IMMH,
                        0 - real_ir1_nr);
            }
            //return false;
            break;
        }
    }
    latxs_tr_ra_init(td);
    if (option_pushpop &&
        (td->sys.flags & HF_CPL_MASK) &&
        td->sp_patch) {
        int ss_addr_size = latxs_get_sys_stack_addr_size();
        latxs_update_stack_pointer(td->sp_patch, ss_addr_size);
        td->sp_patch = 0;
    }
    return true;
}

static void latxs_tr_init_in_tu(TranslationBlock *tb)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    lsassert(td);

    /* 1. set current tb and ir1 */
    latxs_tr_tb_init(td, tb);

    /* 2. register allocation init */
    latxs_tr_ra_init(td);

    /* 3. reset all registers extension mode */
    /*latxs_tr_em_init(td);*/

    ///* 4. reset ir2 array */
    //latxs_tr_ir2_array_init(td);

    /* 5. set up lsfpu */
    /*latxs_tr_fpu_init(td, tb);*/

    if (tb) {
        td->x86_ins_nr = 0;
        td->exitreq_label = latxs_ir2_opnd_new_label();
        if (latxs_check_interrupt_at_block_end()) {
            tb->s_data->label_intexit[0] =
                latxs_ir2_opnd_new_label().val;
            tb->s_data->label_intexit[1] =
                latxs_ir2_opnd_new_label().val;
        }
    }

    /* 6. label for optimization */
    if (option_int_opt == IRQ_CHECK_BLOCK_START) {
        td->int_nocheck_label = latxs_ir2_opnd_new_label();
        td->int_docheck_label = latxs_ir2_opnd_new_label();
    }
}

static void fix_jmp_insn_offset(int *label_pos, int split_ir2_num)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    TranslationBlock *tb = td->curr_tb;
    /* prologue/epilogue has no tb */
    if (tb) {
        int label_id_0 = tb->jmp_reset_offset[0];
        if (label_id_0 != TB_JMP_OFFSET_INVALID) {
            int offset = (label_pos[label_id_0] - split_ir2_num) * 4;
            unsigned call_bytes = (tb->bool_flags & IS_HRAS_CALL) ?
                tb->jmp_insn_offset[0] : 8;

            g_assert(call_bytes == 4 || call_bytes == 8);

            /* A cold trampoline may exceed the 16-bit link-slot format.
             * Keep dispatching through its original fallback in that case. */
            if (option_tu_hras && (offset < 0 || offset > 65524)) {
                tb->jmp_reset_offset[0] = TB_JMP_OFFSET_INVALID;
                tb->jmp_insn_offset[0] = TB_JMP_OFFSET_INVALID;
                tb->link_opt[0] = 0;
            } else {
                tb->jmp_reset_offset[0] = offset + call_bytes;
                tb->jmp_insn_offset[0] = offset;
            }
        }
        int label_id_1 = tb->jmp_reset_offset[1];
        if (label_id_1 != TB_JMP_OFFSET_INVALID) {
            tb->jmp_reset_offset[1] =
                ((label_pos[label_id_1] - split_ir2_num) << 2) + 8;
            tb->jmp_insn_offset[1] = ((label_pos[label_id_1] - split_ir2_num) << 2);
        }
        for (int i = 0; i < 2; i++) {
            int label_id = tb->s_data->tu_generation_offset_patch[i];

            if (label_id != TB_JMP_OFFSET_INVALID) {
                int offset = (label_pos[label_id] - split_ir2_num) * 4;

                tb->s_data->tu_generation_offset_patch[i] =
                    (tb->bool_flags & IS_HRAS_CALL) &&
                    (offset < 0 || offset > 65524) ?
                    TB_JMP_OFFSET_INVALID : offset;
            }
        }
        /*
         * +--------+    <--- label for jmp_cp_data
         * |        | \
         * |        |  | : cp offset
         * |        | /
         * +--------+    <--- label for jmp_insn_offset
         * |  b 2   |
         * |  nop   |
         * +--------+
         */
        int cp_off = 0;
        int label_id_cp0 = tb->jmp_cp_data[0];
        if (label_id_cp0 != 0) {
            cp_off = label_pos[label_id_0] - label_pos[label_id_cp0];
            g_assert((cp_off & ~TB_JMP_CP_DATA_OFF) == 0);
            tb->jmp_cp_data[0] = cp_off | TB_JMP_CP_DATA_VALID;
        }
        int label_id_cp1 = tb->jmp_cp_data[1];
        if (label_id_cp1 != 0) {
            cp_off = label_pos[label_id_1] - label_pos[label_id_cp1];
            g_assert((cp_off & ~TB_JMP_CP_DATA_OFF) == 0);
            tb->jmp_cp_data[1] = cp_off | TB_JMP_CP_DATA_VALID;
        }

        if (option_int_opt == IRQ_CHECK_BLOCK_START) {
            int nocheck = latxs_ir2_opnd_label_id(&td->int_nocheck_label);
            tb->label_pos_nocheck = label_pos[nocheck];
        }
    }
    return;
}

static bool tu_hras_njc_branches_fit(const int *label_pos)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    int position = 0;

    for (int i = 0; i < td->ir2_cur_nr; i++) {
        IR2_INST *inst = ir2_get(i);
        IR2_OPCODE opcode = ir2_opcode(inst);

        if (opcode == LISA_LABEL || opcode == LISA_X86_INST ||
            opcode == LISA_TU_SPLIT || opcode == LISA_PROFILE) {
            continue;
        }
        if (opcode == LISA_BEQ || opcode == LISA_BNE ||
            ((option_irq_outline || td->irq_backedge_short) &&
             (opcode == LISA_BLT || opcode == LISA_BGE))) {
            IR2_OPND *label = latxs_ir2_branch_get_label(inst);

            if (label && latxs_ir2_opnd_is_label(label)) {
                int id = latxs_ir2_opnd_label_id(label);
                g_assert(id > 0 && id <= td->label_num);
                if (label_pos[id] >= 0) {
                    int64_t distance = (int64_t)label_pos[id] - position;
                    if (distance < -32768 || distance > 32767) {
                        return false;
                    }
                }
            }
        }
        position++;
    }
    return true;
}

static void fix_tu_cold_exit_offsets(TranslationBlock **tb_list,
                                     int tb_num_in_tu, int *label_pos)
{
    int i;

    for (i = 0; i < tb_num_in_tu; i++) {
        TranslationBlock *tb = tb_list[i];
        int succ_id;

        for (succ_id = 0; succ_id < 2; succ_id++) {
            uint32_t label_id = tb->s_data->tu_cold_exit_offset[succ_id];

            if (label_id == TU_COLD_EXIT_INVALID) {
                continue;
            }
            if (label_id == TU_COLD_EXIT_DIRECT_PENDING ||
                label_id == TU_INDIRECT_EXIT_INLINE_PENDING) {
                tb->s_data->tu_cold_exit_offset[succ_id] = TU_COLD_EXIT_INVALID;
                continue;
            }
            g_assert(label_id != TU_COLD_EXIT_JMP_INTERRUPT_DEFERRED);
            g_assert(label_pos[label_id] >= 0);
            tb->s_data->tu_cold_exit_offset[succ_id] = label_pos[label_id] << 2;
            g_assert(tb->s_data->tu_cold_exit_offset[succ_id] !=
                     TU_COLD_EXIT_INVALID);
        }
    }
}

static void gpr_highbits_reset(uint8_t forms[32])
{
    memset(forms, LATXS_GPR_HB_UNKNOWN, 32);
}

static void gpr_highbits_ensure_tb(TRANSLATION_DATA *td,
                                   TranslationBlock *tb)
{
    if (td->gpr_highbits_tb == tb) {
        return;
    }
    gpr_highbits_reset(td->gpr_highbits_forms);
    latxs_gpr_hb_state_clear_producer(&td->gpr_highbits_state);
    td->gpr_highbits_tb = tb;
}

static LATXSGPRHighBitsForm gpr_highbits_extension(IR2_INST *ir2,
                                                   int *dest,
                                                   int *source)
{
    IR2_OPCODE opcode = ir2_opcode(ir2);

    if (opcode != LISA_BSTRPICK_D && opcode != LISA_BSTRPICK_W) {
        return LATXS_GPR_HB_UNKNOWN;
    }
    if (!latxs_ir2_opnd_is_gpr(&ir2->_opnd[0]) ||
        !latxs_ir2_opnd_is_gpr(&ir2->_opnd[1]) ||
        !latxs_ir2_opnd_is_imm(&ir2->_opnd[2]) ||
        !latxs_ir2_opnd_is_imm(&ir2->_opnd[3]) ||
        latxs_ir2_opnd_imm(&ir2->_opnd[2]) != 31 ||
        latxs_ir2_opnd_imm(&ir2->_opnd[3]) != 0) {
        return LATXS_GPR_HB_UNKNOWN;
    }

    *dest = latxs_ir2_opnd_reg(&ir2->_opnd[0]);
    *source = latxs_ir2_opnd_reg(&ir2->_opnd[1]);
    return opcode == LISA_BSTRPICK_D ? LATXS_GPR_HB_ZERO_32
                                     : LATXS_GPR_HB_SIGNED_32;
}

/*
 * Filter while IR2 is appended so ir2_asm_nr-based jump and relocation
 * offsets are born with the optimized instruction count.
 */
bool latxs_gpr_highbits_filter(IR2_INST *ir2)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    TranslationBlock *tb = td->curr_tb;
    IR2_OPCODE opcode = ir2_opcode(ir2);
    int dest = -1;
    int source = -1;
    LATXSGPRHighBitsForm form;
    bool aot_active = in_aot_translate && option_aot_save &&
                      option_aot_gpr_highbits;
    bool tu_active = td->gpr_highbits_dynamic_tu_active;

    if ((!aot_active && !tu_active) || !tb ||
        (tb->flags & HF_CPL_MASK) != HF_CPL_MASK) {
        return false;
    }

    gpr_highbits_ensure_tb(td, tb);

    if (opcode == LISA_X86_INST || opcode == LISA_PROFILE) {
        return false;
    }
    if (opcode == LISA_LABEL || opcode == LISA_TU_SPLIT) {
        gpr_highbits_reset(td->gpr_highbits_forms);
        latxs_gpr_hb_state_clear_producer(&td->gpr_highbits_state);
        return false;
    }

    form = gpr_highbits_extension(ir2, &dest, &source);
    if (tu_active) {
        /*
         * Dynamic proof covers exactly two adjacent emitted IR2 operations.
         * Metadata returned above does not break adjacency; every other real
         * operation below replaces or clears the single producer slot.
         */
        if (form != LATXS_GPR_HB_UNKNOWN && dest == source) {
            if (latxs_gpr_hb_state_can_eliminate_extension(
                    &td->gpr_highbits_state, dest, form)) {
                /*
                 * This is the most recently allocated IR2 and has not been
                 * appended yet.  Remove it from the logical array instead of
                 * leaving a tombstone for later TU passes to interpret.
                 */
                lsassert(ir2 == td->ir2_array + td->ir2_cur_nr - 1);
                td->ir2_cur_nr--;
                return true;
            }
        }
        if (form != LATXS_GPR_HB_UNKNOWN &&
            latxs_gpr_hb_reg_is_trackable(dest)) {
            latxs_gpr_hb_state_note_producer(
                &td->gpr_highbits_state, dest, form);
        } else if ((opcode == LISA_LD_WU || opcode == LISA_LD_W) &&
                   latxs_ir2_opnd_is_gpr(&ir2->_opnd[0])) {
            dest = latxs_ir2_opnd_reg(&ir2->_opnd[0]);
            latxs_gpr_hb_state_note_producer(
                &td->gpr_highbits_state, dest,
                opcode == LISA_LD_WU ? LATXS_GPR_HB_ZERO_32
                                     : LATXS_GPR_HB_SIGNED_32);
        } else {
            latxs_gpr_hb_state_clear_producer(&td->gpr_highbits_state);
        }
    }

    if (!aot_active) {
        return false;
    }
    if (form != LATXS_GPR_HB_UNKNOWN && dest > 0 && dest < 32) {
        if (dest == source &&
            td->gpr_highbits_forms[dest] == form) {
            lsassert(ir2 == td->ir2_array + td->ir2_cur_nr - 1);
            td->ir2_cur_nr--;
            return true;
        }
        td->gpr_highbits_forms[dest] = form;
    } else if (latxs_ir2_opnd_is_gpr(&ir2->_opnd[0])) {
        dest = latxs_ir2_opnd_reg(&ir2->_opnd[0]);
        if (dest > 0 && dest < 32) {
            if (opcode == LISA_LD_WU) {
                td->gpr_highbits_forms[dest] = LATXS_GPR_HB_ZERO_32;
            } else if (opcode == LISA_LD_W) {
                td->gpr_highbits_forms[dest] = LATXS_GPR_HB_SIGNED_32;
            } else {
                td->gpr_highbits_forms[dest] = LATXS_GPR_HB_UNKNOWN;
            }
        }
    }

    if (latxs_ir2_opcode_is_branch(opcode) || opcode == LISA_JIRL ||
        opcode == LISA_BL) {
        gpr_highbits_reset(td->gpr_highbits_forms);
    }

    return false;
}

static int latxs_label_dispose_in_tu(TranslationBlock **tb_list,
        const RasTPCallLabels *ras_calls, void *const *direct_calls,
        bool has_inline_hras_call)
{
#ifdef CONFIG_LATX_PROFILER
    int profile_addr_diff = 0;
#endif

#ifdef CONFIG_LATX_TU
    int tb_num_in_tu = 0;
    int split_ir2_num = 0;
#endif
    TRANSLATION_DATA *td = lsenv->tr_data;
    int label_nr = td->label_num;
    g_autofree int *tb_starts = option_tu_hras ?
        g_new0(int, tu_data->tb_num) : NULL;

#define MAX_ALLOC_SIZE 1877
    //bool use_malloc = label_nr > MAX_ALLOC_SIZE ? true : false;
    bool use_malloc = true;
    /* 1. record the positions of label */
    int *label_pos = use_malloc ? (int*)malloc(label_nr * 4 + 20) : (int *)alloca(label_nr * 4 + 20);
    memset(label_pos , -1, label_nr * 4 + 20);
    int *label_pos_id = use_malloc ? (int*)malloc(label_nr * 4 + 20) : (int *)alloca(label_nr * 4 + 20);
    memset(label_pos_id , -1, label_nr * 4 + 20);

    int ir2_num = 0;

    int ir2_id = 0;
    int ir2_nr = td->ir2_cur_nr;
    IR2_INST *ir2_current = NULL;
    IR2_OPCODE ir2_opc;
    for (ir2_id = 0; ir2_id < ir2_nr; ++ir2_id) {

        ir2_current = ir2_get(ir2_id);
        ir2_opc = ir2_opcode(ir2_current);

        if (ir2_opc  == LISA_LABEL) {
            int label_id = latxs_ir2_opnd_label_id(&ir2_current->_opnd[0]);
            lsassertm(label_pos[label_id] == -1,
                      "label %d is in multiple positions\n", label_id);
            label_pos[label_id] = ir2_num;
            label_pos_id[label_id] = ir2_id;
#ifdef CONFIG_LATX_TU
        } else if (ir2_opc == LISA_TU_SPLIT) {
            td->curr_tb = tb_list[tb_num_in_tu++];
            if (!option_tu_hras) {
                fix_jmp_insn_offset(label_pos, split_ir2_num);
            } else {
                tb_starts[tb_num_in_tu] = ir2_num;
            }
#ifdef CONFIG_LATX_PROFILER
            SET_TB_PROFILE(((TranslationBlock*)td->curr_tb), nr_code,
                    (ir2_num - split_ir2_num) - (profile_addr_diff >> 2));
            profile_addr_diff = 0;
#endif
            split_ir2_num = ir2_num;
        } else if (ir2_opc == LISA_X86_INST) {
#endif
            /* will not be assembled */
#ifdef CONFIG_LATX_PROFILER
        } else if (ir2_opc == LISA_PROFILE) {
            lsassert(label_pos[latxs_ir2_opnd_label_id(&ir2_current->_opnd[1])] != -1);
            if (ir2_current->_opnd[0].val == PROFILE_BEGIN) {
                profile_addr_diff -= label_pos[latxs_ir2_opnd_label_id(&ir2_current->_opnd[1])]<<2;
            } else {
                profile_addr_diff += label_pos[latxs_ir2_opnd_label_id(&ir2_current->_opnd[1])]<<2;
            }
#endif
        } else {
            ir2_num++;
        }
    }

    /* No TB offset has been converted from a label yet. A retry can
     * regenerate the TU without carrying partially fixed metadata. */
    if (direct_calls &&
        !latxs_hras_direct_checks_fit(tb_num_in_tu + 1, direct_calls,
                                      label_pos)) {
        if (use_malloc) {
            free(label_pos);
            free(label_pos_id);
        }
        return TU_ASSEMBLE_RETRY_HRAS_LONG_CHECK;
    }
    if (td->irq_backedge_short && !tu_hras_njc_branches_fit(label_pos)) {
        g_assert(!td->hras_long_checks);
        if (use_malloc) {
            free(label_pos);
            free(label_pos_id);
        }
        return TU_ASSEMBLE_RETRY_HRAS_LONG_CHECK;
    }
    if (((option_tu_hras && intb_njc_inline_enabled()) ||
         has_inline_hras_call) &&
        !tu_hras_njc_branches_fit(label_pos)) {
        if (use_malloc) {
            free(label_pos);
            free(label_pos_id);
        }
        return TU_ASSEMBLE_ERR_HRAS_NJC_RANGE;
    }
    fix_tu_cold_exit_offsets(tb_list, tb_num_in_tu + 1, label_pos);
    if (option_rastoptp) {
        for (int i = 0; i <= tb_num_in_tu; ++i) {
            TranslationBlock *member = tb_list[i];
            if (!ras_calls[i].valid) {
                continue;
            }
            member->inst_ptr_offset =
                label_pos[ras_calls[i].gate.val] << 2;
            g_assert(member->s_data->last_ir1_type == IR1_TYPE_CALL ||
                     member->s_data->last_ir1_type == IR1_TYPE_CALLIN);
            g_assert(LATXS_RAS_TP_COLD_OFFSET(member) == TU_COLD_EXIT_INVALID);
            LATXS_RAS_TP_COLD_OFFSET(member) =
                label_pos[ras_calls[i].miss.val] << 2;
            uint32_t cold = LATXS_RAS_TP_COLD_OFFSET(member);
            uint32_t size = LATXS_RAS_TP_COLD_SIZE(member);
            g_assert(member->inst_ptr_offset >= 0 &&
                     cold > (uint32_t)member->inst_ptr_offset && size &&
                     cold <= (uint64_t)ir2_num * 4 &&
                     size <= (uint64_t)ir2_num * 4 - cold);
        }
    }

    /*
     * 2. resolve the offset of successor linkage code
     * @jmp_target_arg recoed the jmp  inst position
     * @jmp_reset_offset record the successor inst of jmp(exclude delay slot)
     *                   when the tb is removed from buffer. the jmp inst use
     *                   this position to revert the original "fall through"
     */
    if (option_tu_hras) {
        /* Cold link labels are known only after scanning the entire TU. */
        for (int i = 0; i < tb_num_in_tu; i++) {
            td->curr_tb = tb_list[i];
            fix_jmp_insn_offset(label_pos, tb_starts[i]);
        }
    }
    td->curr_tb = tb_list[tb_num_in_tu];
#ifdef CONFIG_LATX_PROFILER
    SET_TB_PROFILE(((TranslationBlock*)td->curr_tb), nr_code,
            (ir2_num - split_ir2_num) - (profile_addr_diff >> 2));
#endif
    fix_jmp_insn_offset(label_pos, split_ir2_num);

    /* 3. resolve the branch instructions */
    ir2_num = 0;
    for (ir2_id = 0; ir2_id < ir2_nr; ++ir2_id) {

        ir2_current = ir2_get(ir2_id);
        ir2_opc = ir2_opcode(ir2_current);

        if (latxs_ir2_opcode_has_pcrel_label(ir2_opc)) {

            IR2_OPND *label_opnd = latxs_ir2_branch_get_label(ir2_current);

            /* LISA_B and LISA_BL could contain label or imm */
            if (label_opnd && latxs_ir2_opnd_is_label(label_opnd)) {
                int label_id = latxs_ir2_opnd_label_id(label_opnd);
                lsassert(label_id > 0 && label_id <= label_nr);

                ir2_current->_b_label_id = label_id;
                ir2_current->_b_ir2_id = label_pos_id[label_id];

                int label_pos_ir2_num = label_pos[label_id];

                if (label_pos_ir2_num != -1) {
                    latxs_ir2_resolve_pcrel_label(ir2_current, label_opnd,
                            label_pos_ir2_num - ir2_num);
                } else if (ir2_opc == LISA_PCADDI) {
                    /*
                     * PCADDI only carries a label here as the softmmu
                     * cold-anchor back-pointer, and that label is emitted in
                     * the same IR2 stream as this instruction.  A missing
                     * label therefore means a slow-path record survived the
                     * discard of the IR2 array it was created with; encoding
                     * the label id as si20 would point a3 at an arbitrary
                     * window and make helper_softmmu_common decode a wrong
                     * access.  Fail closed instead.
                     */
                    fprintf(stderr,
                            "IR2: LISA_PCADDI label %d was not disposed\n",
                            label_id);
                    abort();
                }
                /* If label_pos_ir2_num is -1, it means this label
                 * is not inserted into the ir2 array.
                 * In tu mode, this could happen for branch inside tu. */
            }
        }

#ifdef CONFIG_LATX_TU
        if (ir2_opc != LISA_TU_SPLIT && ir2_opc != LISA_LABEL && ir2_opc != LISA_PROFILE && ir2_opc != LISA_X86_INST)
#else
        if (ir2_opc != LISA_LABEL && ir2_opc != LISA_PROFILE && ir2_opc != LISA_X86_INST)
#endif
        {
            ir2_num++;
        }
    }

    if (use_malloc) {
        free(label_pos);
        free(label_pos_id);
    }
    return 0;
}

static inline int fix_tb_info(TranslationBlock **tb_list, int code_nr, int x86ins_idx, int x86ins_hcnt, int x86ins_nr, int x86ins_size)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    TranslationBlock *tb = td->curr_tb;

#if defined(CONFIG_TCG_REGION_TREE_ENABLE)
    tb->tc.size = code_nr << 2;
#endif
    tb->ir2_num = code_nr;
    tb->tc.offset_in_tu = tb->tc.ptr - tb_list[0]->tc.ptr;

    td->x86_ins_idx[x86ins_nr] = x86ins_idx;
    td->x86_ins_lisa_nr[x86ins_nr] = x86ins_hcnt;
    td->x86_ins_nr = x86ins_nr + 1;
    td->x86_ins_size = x86ins_size;

    tb->icount = x86ins_nr + 1;
    tb->size = x86ins_size;

    void *search_block;
    if (!option_split_search) {
        search_buff_offset[search_id] = search_size;
        search_size += latxs_tb_encode_search(tb, search_buff + search_buff_offset[search_id++]);
    } else {
        uint32_t mtcc = td->mtcc_id;
        search_block = region_alloc_search(tb, mtcc);
        if (!search_block) {
            if (in_aot_translate) {
                fprintf(stderr, "[AOT] TU search allocation failed: "
                        "pc=%" PRIx64 " mtcc=%u insns=%u\n",
                        (uint64_t)tb->pc, mtcc, tb->icount);
                print_buffer_size(mtcc);
            }
#ifdef CONFIG_LATX_PRINT
            printf("%s:%d TU fail to alloc search data.\n",
                    __func__, __LINE__);
#endif
            return 0;
        }
        latxs_tb_encode_search(tb, search_block);
    }

    return 1;
}

static inline __attribute__((always_inline))
int latxs_tr_ir2_emit_in_tu(TranslationBlock **tb_list, TRANSLATION_DATA *td,
        uint32_t (*assemble)(IR2_INST *), bool dump_host)
{
#ifdef CONFIG_LATX_TU
    int tb_num_in_tu = 0;
#endif
    int success = 1;

    IR2_INST *pir2 = NULL;
    void *code_ptr = (void *)(ADDR)(tb_list[0]->tc.ptr);
    int code_nr = 0;
    int all_code_nr = 0;

    TranslationBlock *tb = td->curr_tb;

    /* Counter for encode search */
    int x86ins_idx  = 0;
    int x86ins_nr  = -1;
    int x86ins_hcnt = 0; /* host instruction count */
    int x86ins_size = 0;

    int ir2_id = 0;
    int ir2_nr = td->ir2_cur_nr;
    IR2_INST *ir2_base = td->ir2_array;
    for (ir2_id = 0; ir2_id < ir2_nr; ++ir2_id) {

        pir2 = &ir2_base[ir2_id];
        IR2_OPCODE ir2_opc = ir2_opcode(pir2);

#ifdef CONFIG_LATX_PROFILER
        if (ir2_opc == LISA_PROFILE) {
            continue;
        }
#endif
        if (ir2_opc == LISA_X86_INST) {

            if (dump_host && qemu_log_in_addr_range(tb->pc)) {
                fprintf(stderr, "IR2 at %p LISA X86Ins = 0x%x\n",
                        code_ptr, latxs_ir2_opnd_imm(&pir2->_opnd[0]));
            }

            if (x86ins_nr >= 0) {
                td->x86_ins_idx[x86ins_nr] = x86ins_idx;
                td->x86_ins_lisa_nr[x86ins_nr] = x86ins_hcnt;
            }
            x86ins_idx = ir2_get_id(pir2);
            x86ins_nr += 1;
            /* calculate TB size */
            IR1_INST *pir1 = &td->ir1_inst_array[x86ins_nr];
            x86ins_size += latxs_ir1_inst_size(pir1);

            continue;
        }

        if (ir2_opc == LISA_LABEL) {
            if (dump_host && (!tb || qemu_log_in_addr_range(tb->pc))) {
                fprintf(stderr, "IR2 at %p LISA LABEL  = ---------- [%03d] LABEL %d",
                        code_ptr, pir2->_id,
                        latxs_ir2_opnd_imm(&pir2->_opnd[0]));
                if (pir2->_b_label_name) {
                    fprintf(stderr, " ====== %s ====== ", pir2->_b_label_name);
                }
                fprintf(stderr, "\n");
            }
            continue;
        }

        if (ir2_opc == LISA_TU_SPLIT) {
            success = fix_tb_info(tb_list, code_nr, x86ins_idx, x86ins_hcnt, x86ins_nr, x86ins_size);
            if (!success) {
                return TU_ASSEMBLE_ERR_TB_INFO;
            }

            ++tb_num_in_tu;
            x86ins_idx  = 0;
            x86ins_nr  = -1;
            x86ins_hcnt = 0; /* host instruction count */
            x86ins_size = 0;
            all_code_nr +=code_nr;
            code_nr = 0;

            td->curr_tb = tb_list[tb_num_in_tu];
            tb = td->curr_tb;

            tb_list[tb_num_in_tu]->tc.ptr = code_ptr;
            td->ir1_inst_array = tb_list[tb_num_in_tu]->s_data->ir1;
            continue;
        }

        if (ir2_opc != LISA_LABEL) {
            uint32_t ir2_binary = assemble(pir2);
            if (dump_host && (!tb || qemu_log_in_addr_range(tb->pc))) {
                fprintf(stderr, "IR2 at %p LISA Binary = 0x%08x ",
                        code_ptr, ir2_binary);
                latxs_ir2_dump(pir2);
            }

            *(uint32_t *)code_ptr = ir2_binary;
            code_ptr = code_ptr + 4;
            code_nr += 1;

            x86ins_hcnt += 1;
        }
    }

    if (td->curr_tb) {
        success = fix_tb_info(tb_list, code_nr, x86ins_idx, x86ins_hcnt, x86ins_nr, x86ins_size);
        if (!success) {
            return TU_ASSEMBLE_ERR_TB_INFO;
        }
    }

    return all_code_nr + code_nr;
}

static inline int latxs_tr_ir2_assemble_in_tu(TranslationBlock **tb_list,
        const RasTPCallLabels *ras_calls, void *const *direct_calls,
        bool has_inline_hras_call)
{
    int label_result = latxs_label_dispose_in_tu(tb_list, ras_calls,
                                                direct_calls,
                                                has_inline_hras_call);
    if (label_result) {
        return label_result;
    }
    TRANSLATION_DATA *td = lsenv->tr_data;
    td->curr_tb = tb_list[0];
    TranslationBlock *tb = td->curr_tb;
    if (tb && latxs_tr_check_buffer_overflow(tb_list[0]->tc.ptr, td)) {
        if (in_aot_translate) {
            fprintf(stderr, "[AOT] TU code buffer exhausted: pc=%" PRIx64
                    " start=%p highwater=%p ir2=%d\n",
                    (uint64_t)tb->pc, tb_list[0]->tc.ptr,
                    td->code_highwater, td->ir2_cur_nr);
        }
#ifdef CONFIG_LATX_PRINT
        printf("%s:%d TU buffer overflow.\n", __func__, __LINE__);
#endif
        return TU_ASSEMBLE_ERR_BUFFER_OVERFLOW;
    }
    if (likely(option_new_assemble && !option_dump_host)) {
        return latxs_tr_ir2_emit_in_tu(tb_list, td, ir2_assemble_new, false);
    }
    return latxs_tr_ir2_emit_in_tu(tb_list, td,
            option_new_assemble ? ir2_assemble_new : ir2_assemble,
            option_dump_host);
}

/* static void set_pg_mode_0x1000f(CPUX86State *env) */
/* { */
/*     int pg_mode = 0x1000f; */
/*     if (pg_mode & PG_MODE_LMA) { */
/*         env->hflags |= HF_LMA_MASK; */
/*         if (pg_mode & PG_MODE_PKE) { */
/*             env->cr[4] |= CR4_PKE_MASK; */
/*         } */
/*         if (pg_mode & PG_MODE_PKS) { */
/*             env->cr[4] |= CR4_PKS_MASK; */
/*         } */
/*         if (pg_mode & PG_MODE_LA57) { */
/*             env->cr[4] |= CR4_LA57_MASK; */
/*         } */
/*     } */
/*     if (pg_mode & PG_MODE_SMEP) { */
/*         env->cr[4] |= CR4_SMEP_MASK; */
/*     } */
/*     if (pg_mode & PG_MODE_PSE) { */
/*         env->cr[4] |= CR4_PSE_MASK; */
/*     } */
/*     if (pg_mode & PG_MODE_PAE) { */
/*         env->cr[4] |= CR4_PAE_MASK; */
/*         if (pg_mode & PG_MODE_NXE) { */
/*             env->efer |= MSR_EFER_NXE; */
/*         } */
/*         if (pg_mode & PG_MODE_PKE) { */
/*             env->cr[4] |= CR4_PKE_MASK; */
/*         } */
/*         if (pg_mode & PG_MODE_WP) { */
/*             env->cr[0] |= CR0_WP_MASK; */
/*         } */
/*         if (pg_mode) { */
/*             env->cr[0] |= CR0_PG_MASK; */
/*         } */
/*     } */
/* } */

int translate_tb_in_tu(uint32 tb_num_in_tu, TranslationBlock **tb_list)
{
    g_autofree RasTPCallLabels *ras_calls = option_rastoptp ?
        g_new0(RasTPCallLabels, tb_num_in_tu) : NULL;
    g_autofree HrasIndirectCallLabels *indirect_calls = option_tu_hras ?
        g_new0(HrasIndirectCallLabels, tb_num_in_tu) : NULL;
    g_autoptr(GPtrArray) direct_calls = option_tu_hras ?
        g_ptr_array_new_with_free_func(g_free) : NULL;
    int code_nr = 0;
    int code_size = 0;
    int is_buffer_overflow = 0;
    bool has_inline_hras_call = false;

    TRANSLATION_DATA *td = lsenv->tr_data;
    if (direct_calls) g_ptr_array_set_size(direct_calls, tb_num_in_tu);
    td->irq_backedge_short = false;
    td->hras_direct_call = NULL;
#if !defined(CONFIG_TCG_REGION_TREE_ENABLE)
    uintptr_t curr_tbmini_ptr = (uintptr_t)tcg_ctx->code_gen_ptr -
                        sizeof(struct TBMini) * (tb_num_in_tu + 1);
#endif

    tb_list[0]->tc.ptr = tcg_splitwx_to_rx(tcg_ctx->code_gen_ptr);

    TranslationBlock *tb;

    latxs_tr_ir2_array_init(td);
    td->gpr_highbits_dynamic_tu_active =
        latxs_gpr_hb_dynamic_tu_is_active(in_aot_translate, jit_tu_enabled(),
                                          option_tu_gpr_highbits);
    TU_EFLAG_OPT_RESET(tu_data);
    td->avoid_tu_eflag_opt = AVOID_TUEF_NONE;

    lsenv->tr_data->slow_path_rcd_nr  = 0;


    for (int i = 0; i < tb_num_in_tu; ++i) {
        tb = tb_list[i];
        typeof(tu_data->eflag_opt_data) saved_eflag_opt_data =
            tu_data->eflag_opt_data;
        uint64_t ghbr_checkpoint = latxs_ghbr_stats_checkpoint();
        int slow_path_nr_saved = 0;

retranslate_one_tb_in_tu:
        g_clear_pointer(&td->hras_direct_call, g_free);
        latxs_ghbr_stats_restore(ghbr_checkpoint);
        curr_rel_entry_num = 0;
        TB_REL_START(tb) = -1;
        TB_REL_END(tb) = -2;
        td->curr_tb = tb;
        tb->tc.ptr = tb_list[0]->tc.ptr;
        tb->bool_flags &= ~IS_HRAS_CALL;
        latxs_tr_init_in_tu(tb);
        latxs_tr_data_reset(tb);
        td->hras_indirect_call = (HrasIndirectCallLabels) {0};
        tb->s_data->tu_unlink_stub_offset = TU_UNLINK_STUB_INVALID;
        if (option_rastoptp) {
            td->ras_tp_has_call = false;
            tb->inst_ptr_offset = 0;
            ras_calls[i].valid = false;
        }

        td->ir1_inst_array = tb->s_data->ir1;
        td->ir1_nr = tb->icount;

        copy_s_data_to_tr_data(tb);
        tb->bool_flags |= IS_TU_TB;
        if (tb->s_data->tu_tb_mode == TU_TB_MODE_BROKEN) {
            fprintf(stderr, "i find it : %s: %d\n", __func__, __LINE__);
            //return tu_switch_native_tb(tb);
        } else {
#ifdef LATXS_IMM_CACHE_ENABLE
            latxs_imm_init(tb);
#endif
            uint32_t mtcc = td->mtcc_id;
            if (option_pushpop) {
                if (!region_alloc_push_pop_data(tb, mtcc, td->ir1_nr)) {
                    if (in_aot_translate) {
                        fprintf(stderr, "[AOT] TU push/pop allocation failed: "
                                "pc=%" PRIx64 " mtcc=%u insns=%d\n",
                                (uint64_t)tb->pc, mtcc, td->ir1_nr);
                        print_buffer_size(mtcc);
                    }
#ifdef CONFIG_LATX_PRINT
                    printf("%s:%d TU fail to alloc pushpop data.\n",
                            __func__, __LINE__);
#endif
                    goto unlock_reset_return_flush;
                }
            }

            /* record current ir2 position (and the slow-path records that
             * belong to it: a discarded attempt's records must not emit
             * orphaned cold-path stubs pointing at labels that only exist in
             * the discarded IR2 range) */
            slow_path_nr_saved = td->slow_path_rcd_nr;
            latxs_td_ir2_position_save(td);

            latxs_tr_ir2_generate_in_tu(tb);

            if (td->avoid_tu_eflag_opt == AVOID_TUEF_START) {
                td->avoid_tu_eflag_opt = AVOID_TUEF_VALID;
                /* restore ir2 position and the matching slow-path records */
                latxs_td_ir2_position_restore(td);
                td->slow_path_rcd_nr = slow_path_nr_saved;
                /* reset instptn eflag optimization */
                tb->ptn.eflag_type = PTN_EFLAG_TYPE_INVALID;
                tu_data->eflag_opt_data = saved_eflag_opt_data;
#ifdef CONFIG_LATX_PRINT
                printf("%s:%d tb %p avoid eflag optimize in tu\n",
                        __func__, __LINE__, tb);
#endif
                goto retranslate_one_tb_in_tu;
            }
            td->avoid_tu_eflag_opt = AVOID_TUEF_NONE;

#ifdef CONFIG_LATX_PROFILER
            if (tb->s_data->last_ir1_type == IR1_TYPE_NORMAL) {
                add_tu_profiler(tb);
            }
#endif
        }
        if (!(td->end_with_exception)) {
#ifndef CONFIG_LATX_PROFILER
            if (td->tb_too_large_pir1) {
                TranslationBlock *next_tb = tb->s_data->next_tb[TU_TB_INDEX_NEXT];
                if (!next_tb || !tb_is_adjacent(tb, next_tb)) {
                    latxs_tr_gen_eob_if_tb_too_large(td->tb_too_large_pir1);
                }
            }
#else
            latxs_tr_gen_eob_if_tb_too_large(td->tb_too_large_pir1);
#endif
            latxs_tr_gen_sys_eob(td->sys_eob_pir1);
        }
        if (option_rastoptp && td->ras_tp_has_call) {
            ras_calls[i] = (RasTPCallLabels) {
                .valid = true,
                .gate = td->ras_tp_call_gate,
                .miss = td->ras_tp_call_miss,
                .done = td->ras_tp_call_done,
            };
        }
        if (indirect_calls) {
            indirect_calls[i] = td->hras_indirect_call;
            has_inline_hras_call |= indirect_calls[i].valid ||
                                    indirect_calls[i].irq_deferred;
        }
        if (direct_calls) {
            g_ptr_array_index(direct_calls, i) = td->hras_direct_call;
        }
        td->hras_direct_call = NULL;
        if (i != tb_num_in_tu - 1)
            latxs_append_ir2_opnda(LISA_TU_SPLIT, 0);
        if (in_aot_translate && i != tb_num_in_tu - 1) {
            add_tu_rel_entry(tb);
        }
    }

    if (in_aot_translate) {
        add_tu_rel_entry(tb_list[tb_num_in_tu - 1]);
        curr_rel_entry_num = 0;
    }
    latxs_tr_gen_tu_cold_exits(tb_num_in_tu, tb_list, indirect_calls,
                             direct_calls ? direct_calls->pdata : NULL);
    tb = tb_list[tb_num_in_tu - 1];
    int last_main_start = TB_REL_START(tb), last_main_end = TB_REL_END(tb);
    if (in_aot_translate) {
        curr_rel_entry_num = 0;
    }
    latxs_tr_gen_tb_end();

#ifdef CONFIG_SOFTMMU
    tr_gen_softmmu_slow_path();
#endif
    if (in_aot_translate) {
        append_tu_ras_rel_entries(tb, last_main_start, last_main_end);
    }

    /* Keep repairs out of member fall-through paths. Each cold fragment
     * carries its own source identity, including AOT relocation records. */
    if (option_rastoptp) {
        for (int i = 0; i < tb_num_in_tu; ++i) {
            tb = tb_list[i];
            if (!ras_calls[i].valid) {
                continue;
            }
            int main_start = TB_REL_START(tb), main_end = TB_REL_END(tb);
            td->curr_tb = tb;
            td->ras_tp_has_call = true;
            td->ras_tp_call_gate = ras_calls[i].gate;
            td->ras_tp_call_miss = ras_calls[i].miss;
            td->ras_tp_call_done = ras_calls[i].done;
            if (in_aot_translate) {
                curr_rel_entry_num = 0;
                TB_REL_START(tb) = -1;
                TB_REL_END(tb) = -2;
            }
            latxs_ras_tp_emit_call_cold();
            if (in_aot_translate) {
                append_tu_ras_rel_entries(tb, main_start, main_end);
            }
        }
        td->ras_tp_has_call = false;
    }

    td->curr_tb = tb_list[0];
    td->ir1_inst_array = tb_list[0]->s_data->ir1;

    code_nr = latxs_tr_ir2_assemble_in_tu(tb_list, ras_calls,
                                         direct_calls ? direct_calls->pdata : NULL,
                                         has_inline_hras_call);
    if (code_nr == TU_ASSEMBLE_ERR_HRAS_NJC_RANGE) {
        latxs_tr_fini();
        return TU_TRANSLATE_FALLBACK;
    }
    if (code_nr == TU_ASSEMBLE_RETRY_HRAS_LONG_CHECK) {
        latxs_tr_fini();
        return TU_TRANSLATE_HRAS_LONG_CHECK;
    }


#ifdef CONFIG_LATX_PRINT
    if (code_nr == TU_ASSEMBLE_ERR_BUFFER_OVERFLOW ||
        code_nr == TU_ASSEMBLE_ERR_TB_INFO) {
        printf("%s:%d TU assemble fail. code %d\n",
                __func__, __LINE__, code_nr);
    }
#endif
    if (code_nr == TU_ASSEMBLE_ERR_BUFFER_OVERFLOW) {
        goto unlock_reset_return;
    }
    if (code_nr == TU_ASSEMBLE_ERR_TB_INFO) {
        goto unlock_reset_return_flush;
    }

    if (code_nr >= 0 && !tu_hras_local_branches_fit(code_nr)) {
        latxs_tr_fini();
        return TU_TRANSLATE_FALLBACK;
    }

#if !defined(CONFIG_TCG_REGION_TREE_ENABLE)
    if (option_tu_hras) {
        for (int i = 0; i < tb_num_in_tu; ++i) {
            if (!tu_hras_member_size_fits(tb_list[i]->ir2_num)) {
                /* No member is published yet. Reuse ordinary-TB fallback
                 * rather than truncating the size used by host-PC lookup. */
                latxs_tr_fini();
                return TU_TRANSLATE_FALLBACK;
            }
        }
    }
#endif

    for (int i = 0; i < tb_num_in_tu; ++i) {
        tb = tb_list[i];
#if !defined(CONFIG_TCG_REGION_TREE_ENABLE)
        uint64_t size = tb->ir2_num << 2;
        curr_tbmini_ptr += sizeof(struct TBMini);
        tu_tbmini_set_pointer((uint64_t *)curr_tbmini_ptr, (uint64_t)tb, size);
#endif
        /* init original jump addresses which have been set during tcg_gen_code() */
        if (tb->jmp_reset_offset[0] != TB_JMP_OFFSET_INVALID) {
            if (qemu_tcg_mode_2_lat()) {
                latx_tb_reset_jmp_target(tb, 0);
            } else {
                tb_reset_jump(tb, 0);
            }
        }
        if (tb->jmp_reset_offset[1] != TB_JMP_OFFSET_INVALID) {
            if (qemu_tcg_mode_2_lat()) {
                latx_tb_reset_jmp_target(tb, 1);
            } else {
                tb_reset_jump(tb, 1);
            }
        }
    }

    tcg_insn_unit *gen_code_buf;
    gen_code_buf = tcg_ctx->code_gen_ptr;

    uintptr_t sptr = (uintptr_t)gen_code_buf + (code_nr << 2);
    qatomic_set(&tcg_ctx->code_gen_ptr, (void *)
                ROUND_UP(sptr, CODE_GEN_ALIGN));

    code_size = code_nr * 4;

    latxs_tr_fini();

    if (tb_list[0]) {
        /* 0. IR1 will not be used again, free it */
        //int i = 0;
        //for (i = 0; i < tb->icount; ++i) {
        //    latxs_ir1_free_info(tb->_ir1_instructions + i);
        //}
        /* 1. check buffer overflow */
        if (unlikely(is_buffer_overflow)) {
            return TU_TRANSLATE_RETRY;
        }
        /* 2. TODO check TB overflow */
        /* if (code_nr >= 0x3fff) { */
            /* return -2; */
        /* } */
        /* 3. flush icache range */
        ADDR s = (ADDR)tb_list[0]->tc.ptr;
        ADDR e = s + code_size;
        flush_idcache_range((uintptr_t)s, (uintptr_t)s, (uintptr_t)e);
    }
    return TU_TRANSLATE_OK;

unlock_reset_return_flush:
    tu_force_tb_flush();
unlock_reset_return:
    qatomic_set(&tcg_ctx->code_gen_ptr, tcg_ctx->code_gen_highwater + 1);
    tu_trees_reset();
    return TU_TRANSLATE_RETRY;
}

void get_last_info(TranslationBlock *tb, IR1_INST* pir1)
{
    //if (tb->icount == 0) {
    //    return;
    //}
    tb->s_data->x86_next_pc = ir1_addr_next(pir1);

    if (ir1_is_branch(pir1)) {
        if (likely(ir1_opnd_is_imm(ir1_get_opnd(pir1, 0)))) {
            tb->s_data->last_ir1_type = (int8)IR1_TYPE_BRANCH;
            tb->s_data->x86_target_pc = ir1_target_addr(pir1);
        } else {
            assert(0);
        }
    }
    else if (ir1_is_call(pir1) && !ir1_is_indirect_call(pir1)) {
        tb->s_data->last_ir1_type = (int8)IR1_TYPE_CALL;
        tb->s_data->x86_target_pc = ir1_target_addr(pir1);
    }
    else if (ir1_is_jump(pir1) && !ir1_is_indirect_jmp(pir1)) {
        tb->s_data->last_ir1_type = (int8)IR1_TYPE_JUMP;
        tb->s_data->x86_target_pc = ir1_target_addr(pir1);
    }
    else if (ir1_is_return(pir1)) {
        tb->s_data->last_ir1_type = (int8)IR1_TYPE_RET;
    }
    else if (ir1_opcode(pir1) == X86_INS_CALL &&
        ir1_is_indirect_call(pir1)) {
        tb->s_data->last_ir1_type = (int8)IR1_TYPE_CALLIN;
    } else if (ir1_opcode(pir1) == X86_INS_JMP &&
        ir1_is_indirect_jmp(pir1)) {
        tb->s_data->last_ir1_type = (int8)IR1_TYPE_JUMPIN;
    } else if (ir1_is_eob_in_tu(pir1)) {
        tb->s_data->last_ir1_type = (int8)IR1_TYPE_EOB;
    } else {
        tb->s_data->last_ir1_type = (int8)IR1_TYPE_NORMAL;
    }
    return;
}

int ir1_is_eob_in_tu(IR1_INST *pir1)
{
    if (latxs_ir1_is_port_io(pir1)) return 1;
    if (latxs_ir1_is_sti(pir1)) return 1;
    if (latxs_ir1_is_mov_to_seg_ss(pir1)) return 1;
    if (latxs_ir1_is_pop_to_seg_ss(pir1)) return 1;
    return 0;
}

static TranslationBlock* aot_tb_create(CPUState *cpu, target_ulong pc,
        uintptr_t hva, TranslationBlock *aot_tb,
        int max_insns, bool recorded)
{
    TranslationBlock* tb = NULL;
    CPUArchState *env = cpu->env_ptr;
    /* tb_page_addr_t phys_pc, phys_p2; */
    void *host_pc = NULL;

    if (unlikely(pc == 0)) {
        fprintf(stderr, "aot_tb_create pc == 0\n");
        return NULL;
    }

    uint32_t mtcc_id = curr_mtcc(env);
    tcg_ctx = tcg_ctx_mtcc[mtcc_id];

    QEMU_BUILD_BUG_ON(CF_COUNT_MASK + 1 != TCG_MAX_INSNS);

    /* todo: now check 16bit(should 15bit for x86) */
    //uint32_t check_zero = cpu_lduw_code((CPUArchState *)cpu->env_ptr, pc);
    //if (unlikely(check_zero == 0)){
    //    return NULL;
    //}

    tb = &tu_tb_tmp;
    tb->s_data = &tmp_s_data;
    tu_reset_tb(tb);

    tb->pc = pc;
    tb->cs_base = aot_tb->cs_base;
    tb->flags = aot_tb->flags;
    tb->cflags = aot_tb->cflags;
    if (aot_file_code_view) {
        max_insns = MIN(max_insns, aot_tb->icount);
    }
    tb->hva = hva;
    tb->ptn.eflag_type = PTN_EFLAG_TYPE_INVALID;
    clear_misc_flags(tb);
    if (recorded && (pc & ~TARGET_PAGE_MASK) ==
                    (aot_tb->pc & ~TARGET_PAGE_MASK)) {
        /* Mode and source page come from this recorded template. A
         * page relocation keeps the entry offset; a speculative successor
         * must never inherit its entry evidence. */
        add_misc_flags(tb, aot_tb->misc_flags & MF_OBSERVED_CALL_ENTRY);
    }
    /* if (option_fast_itlb) { */
    /*     tb->hva = phys_pc & TARGET_PAGE_MASK; */
    /* } else { */
    /*     int error = 0; */
    /*     tb->hva = latxs_tr_cvt_cpu_addr_code(env, pc, &error); */
    /* } */
    tb_set_page_addr0(tb, (tb_page_addr0(aot_tb) & TARGET_PAGE_MASK) |
                         (pc & ~TARGET_PAGE_MASK));
    tb_next_data_reset(tb);
    if (option_fast_itlb) {
    }
    tb_set_page_addr1(tb, -1);

    set_mask_to_miscflags(tb, curr_mtcc(env),
                          MF_MTCC_ID_MASK, MF_MTCC_ID_SHIFT);
    tb->preorder_tb_offset = 0;
    tb->preorder_next = NULL;
    tb->inst_ptr_offset = 0;

#ifdef CONFIG_LATX_PROFILER
    CLN_TB_PROFILE(tb);
#endif

#if defined(CONFIG_TB_FLAGS_NO_EFLAGS)
    if (tb->flags == cpu_get_tb_cpu_state_flags_no_eflags(env)) {
        add_misc_flags(tb, MF_FLAGS_NOEFLAGS);
    }
#endif

    /* if ((tb->misc_flags & (MF_NTB_CROSSPAGE_1 | MF_NTB_CROSSPAGE_0)) != */
    /*         (aot_tb->misc_flags & (MF_NTB_CROSSPAGE_1 | MF_NTB_CROSSPAGE_0))) { */
    /*     fprintf(stderr, "misc_flags not equal %x %x\n", */
    /*             tb->misc_flags, aot_tb->misc_flags); */
    /* } */
    /* tb->misc_flags = aot_tb->misc_flags; */

    tcg_ctx->addr_type = TARGET_LONG_BITS == 32 ? TCG_TYPE_I32 : TCG_TYPE_I64;
#ifdef CONFIG_SOFTMMU
    tcg_ctx->page_bits = TARGET_PAGE_BITS;
    tcg_ctx->page_mask = TARGET_PAGE_MASK;
    tcg_ctx->tlb_dyn_max_bits = CPU_TLB_DYN_MAX_BITS;
    tcg_ctx->tlb_fast_offset =
        (int)offsetof(ArchCPU, neg.tlb.f) - (int)offsetof(ArchCPU, env);
#endif
    tcg_ctx->insn_start_words = TARGET_INSN_START_WORDS;
#ifdef TCG_GUEST_DEFAULT_MO
    tcg_ctx->guest_mo = TCG_GUEST_DEFAULT_MO;
#else
    tcg_ctx->guest_mo = TCG_MO_ALL;
#endif
    latxs_tb_reset(tb);

    TRANSLATION_DATA *td = lsenv->tr_data;
    td->tb_too_large_pir1 = NULL;
    td->sys_eob_pir1 = NULL;

    latxs_tr_sys_init(tb, max_insns, tcg_ctx->code_gen_highwater, host_pc);
    td->is_tu_mode = 1;
    latxs_tr_disasm(tb);
    if (td->deadlock_avoidance) {
//        printf("%s:%d tu deadlock avoidance %d\n",
//                __func__, __LINE__, td->deadlock_avoidance);
        return NULL;
    }
    if (tb->icount == 0 ||
        (aot_file_code_view && aot_file_code_view->failed)) {
        return NULL;
    }
    copy_tr_data_to_s_data(tb);

    if (tb->s_data->tu_tb_mode == TU_TB_MODE_BROKEN
			|| tb->s_data->tu_tb_mode == BAD_TB) {
        return NULL;
    }
    /* tcg_ctx->mtcc_id = 0; */
    /* tb = tcg_tb_alloc_full(tcg_ctx); */
    /* tcg_ctx->mtcc_id = 1; */

    /* Jump reset accesses TU link data at a fixed offset from the TB. */
    tb = g_malloc0(sizeof(*tb) + sizeof(struct separated_data) +
                   (tucp_fast_link_check() ?
                    sizeof(struct tb_tu_link_data) : 0));
    tb->s_data = (void *)(tb + 1);
    tu_tb_tmp.s_data = tb->s_data;
    memcpy(tb, &tu_tb_tmp, sizeof(TranslationBlock));
    memcpy(tb->s_data, &tmp_s_data, sizeof(struct separated_data));
    if (recorded && !aot_file_code_view) {
        tb->s_data->eflag_use = aot_tb->s_data->eflag_use;
        tb->s_data->eflag_use_real = aot_tb->s_data->eflag_use_real;
    }
    tb->tc.ptr = NULL;

    /* if (!can_do_hamt(lsenv->cpu_state) != !(aot_tb->bool_flags & CAN_DO_HAMT)) { */
    /*     fprintf(stderr, "can_do_hamt not equal %d %d\n", */
    /*             tb->bool_flags & CAN_DO_HAMT, (aot_tb->bool_flags & CAN_DO_HAMT)); */
    /*     if (aot_tb->bool_flags & CAN_DO_HAMT) { */
    /*         set_pg_mode_0x1000f(lsenv->cpu_state); */
    /*     } else { */
    /*         env->cr[0] &= ~CR0_PG_MASK; */
    /*     } */
    /* } */

    /* if (!can_do_hamt(lsenv->cpu_state) != !(aot_tb->bool_flags & CAN_DO_HAMT)) { */
    /*     fprintf(stderr, "curr env->cr[0] %lx %lx\n", */
    /*             env->cr[0], env->cr[4]); */
    /*     fprintf(stderr, "can_do_hamt not equal %d %d\n", */
    /*             tb->bool_flags & CAN_DO_HAMT, (aot_tb->bool_flags & CAN_DO_HAMT)); */
    /*     exit(0); */
    /* } */

    return tb;
}

static int get_tb_id(TranslationBlock **page_tb_list, int left_id, int right_id,
        target_ulong pc, uint32_t flags, int cflags, bool full_pc)
{
    assert(left_id >= 0 && left_id <= right_id);
    int mid_id;
    if (!full_pc) {
        pc &= ~TARGET_PAGE_MASK;
    }
    /* fprintf(stderr, "left_id %d right_id %d pc %lx\n", */
    /*         left_id, right_id, pc); */

    while (left_id <= right_id) {
        mid_id = (left_id + right_id) >> 1;
        target_ulong mid_pc = full_pc ? page_tb_list[mid_id]->pc :
            (page_tb_list[mid_id]->pc & ~TARGET_PAGE_MASK);
        if (pc > mid_pc) {
            left_id = mid_id + 1;
        } else if (pc < mid_pc) {
            right_id = mid_id - 1;
        } else {
            if (flags > page_tb_list[mid_id]->flags) {
                left_id = mid_id + 1;
            } else if (flags < page_tb_list[mid_id]->flags) {
                right_id = mid_id - 1;
            } else {
                if (cflags > page_tb_list[mid_id]->cflags) {
                    left_id = mid_id + 1;
                } else if (cflags < page_tb_list[mid_id]->cflags) {
                    right_id = mid_id - 1;
                } else {
                    return mid_id;
                }
            }
        }
    }
    return -1;
}

/*
 * The system AOT cache contains TBs from many address spaces.  Equal guest
 * PCs therefore cannot identify a successor. Use actual runtime edges for
 * supported cross-page exits and check page contents, rather than a reusable
 * CR3 value.
 */
static bool aot_templates_share_tu(const TranslationBlock *entry,
                                   const TranslationBlock *candidate)
{
    if (aot_file_code_view && aot_file_code_view->preserve_entry &&
            ((entry->pc < aot_file_code_view->entry_pc) !=
             (candidate->pc < aot_file_code_view->entry_pc) ||
             (candidate->pc == aot_file_code_view->entry_pc &&
              entry->pc != aot_file_code_view->entry_pc))) {
        return false;
    }
    return entry->cs_base == candidate->cs_base;
}

typedef struct AOTTUExplorePage {
    target_ulong guest_page_addr;
    tb_page_addr_t ram_page_addr;
    unsigned speculative;
} AOTTUExplorePage;

static bool aot_runtime_edge_supported(const TranslationBlock *source,
                                       int edge)
{
    switch (source->s_data->last_ir1_type) {
    case IR1_TYPE_BRANCH:
        return edge == TU_TB_INDEX_NEXT || edge == TU_TB_INDEX_TARGET;
    case IR1_TYPE_JUMP:
        return edge == TU_TB_INDEX_TARGET;
    case IR1_TYPE_CALLIN:
    case IR1_TYPE_CALL:
    case IR1_TYPE_NORMAL:
    case IR1_TYPE_SYSCALL:
        return edge == TU_TB_INDEX_NEXT;
    default:
        return false;
    }
}

/*
 * The caller supplies the successor PC from the decoded AOT clone. Ordinary
 * JIT TBs have shared s_data and reuse the x86_*_pc storage for other fields;
 * only their runtime link and TB identity are meaningful here. A CALL return
 * may use ras_dest, but only when the caller identified a decoded CALL and
 * its real fallthrough PC; optional branch exploration cannot use this link.
 */
static int aot_find_runtime_target(TranslationBlock **templates, int count,
                                   int source_id, int edge, target_ulong pc,
                                   bool required_return)
{
    if (source_id < 0 || source_id >= count ||
            (edge != TU_TB_INDEX_NEXT && edge != TU_TB_INDEX_TARGET)) {
        return -1;
    }
    TranslationBlock *source = templates[source_id];
    uintptr_t target = qatomic_read(&source->jmp_dest[edge]);
    uintptr_t returned = required_return && edge == TU_TB_INDEX_NEXT ?
        qatomic_read(&source->ras_dest) : 0;
    int id;

    if ((source->cflags & CF_INVALID) ||
            ((!target || (target & 1)) && (!returned || (returned & 1)))) {
        return -1;
    }

    id = get_tb_id(templates, 0, count - 1, pc, source->flags,
                   source->cflags, true);
    if (id < 0) {
        return -1;
    }
    /* Equal keys may belong to different physical pages or address spaces. */
    while (id > 0 && templates[id - 1]->pc == pc &&
            templates[id - 1]->flags == source->flags &&
            templates[id - 1]->cflags == source->cflags) {
        id--;
    }
    for (; id < count && templates[id]->pc == pc &&
            templates[id]->flags == source->flags &&
            templates[id]->cflags == source->cflags; id++) {
        /* Never dereference the raw link before finding a collected TB. */
        if ((target && !(target & 1) && (uintptr_t)templates[id] == target) ||
                (returned && !(returned & 1) &&
                 (uintptr_t)templates[id] == returned)) {
            return aot_templates_share_tu(source, templates[id]) ? id : -1;
        }
    }
    return -1;
}

static inline void *aot_ram_addr_to_host(tb_page_addr_t addr)
{
#ifdef CONFIG_USER_ONLY
    return guest_range_valid_untagged(addr, TARGET_PAGE_SIZE) ?
        g2h_untagged(addr) : NULL;
#else
    return qemu_ram_addr_to_host(addr);
#endif
}

static inline bool aot_host_page_is_valid(void *host_page)
{
#ifdef CONFIG_USER_ONLY
    return host_page != NULL;
#else
    return aot_host_pc_in_main_ram(host_page);
#endif
}

/* Commit both pages together, and leave the set unchanged on rejection. */
static bool aot_tu_add_template_pages(const TranslationBlock *tb,
        AOTTUExplorePage pages[AOT_TU_EXPLORE_MAX_PAGES], uint32_t *page_count)
{
    target_ulong guest_page_addr = tb->pc & TARGET_PAGE_MASK;
    tb_page_addr_t ram_pages[2] = {
        tb_page_addr0(tb) & TARGET_PAGE_MASK,
        tb_page_addr1(tb) & TARGET_PAGE_MASK,
    };
    uint32_t tb_page_count = tb_page_addr1(tb) == -1 ? 1 : 2;
    AOTTUExplorePage missing[2];
    uint32_t missing_count = 0;

    for (uint32_t i = 0; i < tb_page_count; i++) {
        target_ulong page_addr = guest_page_addr + i * TARGET_PAGE_SIZE;
        const void *candidate = aot_file_code_view ?
            aot_file_code_page(page_addr) : aot_ram_addr_to_host(ram_pages[i]);
        uint32_t j;

        if (!candidate || (!aot_file_code_view &&
                !aot_host_page_is_valid((void *)candidate))) {
            return false;
        }
        for (j = 0; j < *page_count; j++) {
            if (pages[j].guest_page_addr == page_addr) {
                if (pages[j].ram_page_addr != ram_pages[i]) {
                    void *recorded = aot_ram_addr_to_host(
                            pages[j].ram_page_addr);

                    if (!recorded || !aot_host_page_is_valid(recorded) ||
                            memcmp(recorded, candidate, TARGET_PAGE_SIZE)) {
                        return false;
                    }
                }
                break;
            }
        }
        if (j == *page_count) {
            missing[missing_count++] = (AOTTUExplorePage) {
                .guest_page_addr = page_addr,
                .ram_page_addr = ram_pages[i],
            };
        }
    }
    if (*page_count + missing_count > AOT_TU_EXPLORE_MAX_PAGES) {
        return false;
    }
    for (uint32_t i = 0; i < missing_count; i++) {
        pages[(*page_count)++] = missing[i];
    }
    return true;
}

/* A mandatory return belongs to the current output TU, never to a
 * previously emitted TU with the same global guest key. The page set has
 * already rejected conflicting versions before a clone enters this list. */
static TranslationBlock *aot_local_return(const TranslationBlock *source,
        target_ulong pc, uint32_t flags, int cflags)
{
    for (uint32_t i = 0; i < tu_data->tb_num; i++) {
        TranslationBlock *tb = tu_data->tb_list[i];
        if (tb->pc == pc && tb->flags == flags && tb->cflags == cflags &&
                tb->cs_base == source->cs_base) {
            return tb;
        }
    }
    return NULL;
}

static int get_aot_tb_id(TranslationBlock **tb_templates, int left_id,
        int right_id, target_ulong pc, uint32_t flags, int cflags,
        const TranslationBlock *entry_template,
        const AOTTUExplorePage *pages, uint32_t page_count, bool required_return)
{
    int id = get_tb_id(tb_templates, left_id, right_id, pc, flags, cflags,
                       true);

    if (id < 0) {
        return -1;
    }
    while (id > left_id && tb_templates[id - 1]->pc == pc &&
            tb_templates[id - 1]->flags == flags &&
            tb_templates[id - 1]->cflags == cflags) {
        id--;
    }
    while (id <= right_id && tb_templates[id]->pc == pc &&
            tb_templates[id]->flags == flags &&
            tb_templates[id]->cflags == cflags) {
        TranslationBlock *candidate = tb_templates[id];
        AOTTUExplorePage trial_pages[AOT_TU_EXPLORE_MAX_PAGES];
        uint32_t trial_count = page_count;

        memcpy(trial_pages, pages, page_count * sizeof(*pages));
        if (aot_templates_share_tu(entry_template, candidate) &&
                (required_return || !(candidate->bool_flags & IS_AOT_TB) ||
                 candidate->tc.ptr) &&
                aot_tu_add_template_pages(candidate, trial_pages,
                                           &trial_count) &&
                (aot_file_code_view || trial_count == page_count)) {
            return id;
        }
        id++;
    }
    return -1;
}

static uintptr_t aot_template_hva(TranslationBlock *tb, uintptr_t page_hva,
                                  bool cross_page)
{
    if (!cross_page) {
        return page_hva;
    }
    return aot_file_code_view ? (uintptr_t)aot_file_code_page(tb->pc) :
        (uintptr_t)aot_ram_addr_to_host(tb_page_addr0(tb) & TARGET_PAGE_MASK);
}

typedef struct AOTSpecKey {
    target_ulong pc, cs_base;
    uint32_t flags;
    int cflags;
} AOTSpecKey;

typedef struct AOTSpecState {
    GHashTable *committed;
    GHashTable *pending;
} AOTSpecState;

static guint aot_spec_key_hash(gconstpointer data)
{
    const AOTSpecKey *key = data;
    uint64_t value = key->pc ^ key->cs_base ^
                     ((uint64_t)key->flags << 32) ^ key->cflags;
    return value ^ (value >> 32);
}

static gboolean aot_spec_key_equal(gconstpointer a, gconstpointer b)
{
    const AOTSpecKey *x = a, *y = b;
    return x->pc == y->pc && x->cs_base == y->cs_base &&
           x->flags == y->flags && x->cflags == y->cflags;
}

static void aot_spec_key_store(AOTSpecState *state, const AOTSpecKey *key,
                               TranslationBlock *tb)
{
    AOTSpecKey *stored = g_new(AOTSpecKey, 1);

    *stored = *key;
    g_hash_table_insert(state->pending, stored, tb);
}

static bool aot_spec_key_claimed(AOTSpecState *state, const AOTSpecKey *key,
                                 TranslationBlock **found)
{
    gpointer value;

    if (g_hash_table_lookup_extended(state->pending, key, NULL, &value)) {
        *found = value;
        return true;
    }
    if (g_hash_table_contains(state->committed, key)) {
        *found = NULL;
        return true;
    }
    return false;
}

static void aot_spec_commit(AOTSpecState *state)
{
    GHashTableIter iter;
    gpointer key;

    g_hash_table_iter_init(&iter, state->pending);
    while (g_hash_table_iter_next(&iter, &key, NULL)) {
        g_hash_table_iter_steal(&iter);
        g_hash_table_add(state->committed, key);
    }
}

static void aot_spec_rollback(AOTSpecState *state)
{
    g_hash_table_remove_all(state->pending);
}

static bool aot_template_key_exists(TranslationBlock **templates, int count,
                                    target_ulong pc, uint32_t flags,
                                    int cflags, target_ulong cs_base)
{
    int id;

    if (count <= 0) {
        return false;
    }
    id = get_tb_id(templates, 0, count - 1, pc, flags, cflags, true);
    if (id < 0) {
        return false;
    }
    while (id > 0 && templates[id - 1]->pc == pc &&
            templates[id - 1]->flags == flags &&
            templates[id - 1]->cflags == cflags) {
        id--;
    }
    for (; id < count && templates[id]->pc == pc &&
            templates[id]->flags == flags &&
            templates[id]->cflags == cflags; id++) {
        if (templates[id]->cs_base == cs_base) {
            return true;
        }
    }
    return false;
}

static TranslationBlock *aot_speculative_successor(CPUState *cpu,
        TranslationBlock **templates, int template_count, target_ulong pc,
        uint32_t flags, int cflags, int max_insns, const TranslationBlock *entry,
        uint32_t max_tb, const TranslationBlock *source,
        AOTTUExplorePage *pages, uint32_t page_count, AOTSpecState *spec,
        int *source_ids, bool required_return)
{
    AOTTUExplorePage *page = NULL;
    TranslationBlock *found = NULL;
    TranslationBlock *tb;
    AOTSpecKey key;
    uintptr_t page_hva;

    if (!spec || (!option_aot_tu_pretranslate && !required_return) || aot_file_code_view) {
        return NULL;
    }
    if (tu_data->tb_num >= max_tb) {
        return NULL;
    }
    if (get_page(source->pc) != get_page(pc)) {
        return NULL;
    }
    for (uint32_t i = 0; i < page_count; i++) {
        if (pages[i].guest_page_addr == (pc & TARGET_PAGE_MASK)) {
            page = &pages[i];
            break;
        }
    }
    if (!page || page->speculative >= TARGET_PAGE_SIZE ||
            inst_may_cross_page(pc)) {
        return NULL;
    }

    key = (AOTSpecKey) { pc, entry->cs_base, flags, cflags };
    bool committed_return = required_return &&
        g_hash_table_contains(spec->committed, &key);
    if (aot_spec_key_claimed(spec, &key, &found) && !committed_return) {
        return found;
    }
    if (aot_template_key_exists(templates, template_count, pc, flags, cflags,
                                entry->cs_base)) {
        return NULL;
    }

    page->speculative++;
    page_hva = (uintptr_t)aot_ram_addr_to_host(page->ram_page_addr);
    tb = aot_tb_create(cpu, pc, page_hva, (TranslationBlock *)source,
                       max_insns, false);
    if (!committed_return) {
        aot_spec_key_store(spec, &key, tb);
    }
    if (!tb) {
        return NULL;
    }
    if (source_ids) {
        source_ids[tu_data->tb_num] = -1;
    }
    tu_push_back(tb);
    return tb;
}

static TranslationBlock *aot_tu_get_successor(TranslationBlock **templates,
        int template_count, int entry_id, CPUState *cpu, target_ulong pc,
        uint32_t flags, int cflags, int max_insns, TU_TB_START_TYPE mode,
        const TranslationBlock *entry, uint32_t max_tb,
        AOTTUExplorePage *pages, uint32_t *page_count,
        TranslationBlock *source, int source_id, int edge,
        int *source_ids, AOTSpecState *spec, bool required_return)
{
    if (required_return) {
        TranslationBlock *local = aot_local_return(source, pc, flags, cflags);
        if (local) {
            return local;
        }
    }
    int left_id = aot_file_code_view || required_return ? 0 : entry_id;
    bool crosses_page = get_page(source->pc) != get_page(pc);
    bool runtime_edge = !aot_file_code_view && crosses_page && source_ids &&
        aot_runtime_edge_supported(source, edge);

    int runtime_id = runtime_edge && source_ids ?
        aot_find_runtime_target(templates, template_count,
                                source_id, edge, pc, required_return) : -1;

    if (runtime_edge && runtime_id < 0) {
        return NULL;
    }

    while (left_id < template_count) {
        int id = runtime_edge ? runtime_id :
            get_aot_tb_id(templates, left_id, template_count - 1, pc,
                          flags, cflags, entry, pages, *page_count,
                          required_return);
        TranslationBlock *tb;
        AOTTUExplorePage trial_pages[AOT_TU_EXPLORE_MAX_PAGES];
        uint32_t trial_count = *page_count;

        if (id < 0) {
            if (!spec) {
                return NULL;
            }
            return aot_speculative_successor(cpu, templates, template_count,
                    pc, flags, cflags, max_insns, entry, max_tb, source,
                    pages, *page_count, spec, source_ids, required_return);
        }
        memcpy(trial_pages, pages, *page_count * sizeof(*pages));
        if (templates[id]->pc != pc || templates[id]->flags != flags ||
                templates[id]->cflags != cflags ||
                !aot_templates_share_tu(entry, templates[id]) ||
                !aot_tu_add_template_pages(templates[id], trial_pages,
                                           &trial_count)) {
            return NULL;
        }
        bool already_owned = templates[id]->bool_flags & IS_AOT_TB;
        if (already_owned && !required_return) {
            return (TranslationBlock *)templates[id]->tc.ptr;
        }
        if (tu_data->tb_num >= max_tb ||
            (aot_file_code_view && tu_data->ir1_num_in_tu +
             templates[id]->icount >= MAX_IR1_IN_TU)) {
            return NULL;
        }
        left_id = id + 1;
        tb = aot_tb_create(cpu, pc, aot_template_hva(templates[id], 0, true),
                           templates[id], max_insns, true);
        if (!tb) {
            if (runtime_edge) {
                return NULL;
            }
            continue;
        }
        /* Decode can change the page dependencies; commit only actual pages. */
        trial_count = *page_count;
        memcpy(trial_pages, pages, *page_count * sizeof(*pages));
        if (!aot_tu_add_template_pages(tb, trial_pages, &trial_count) ||
                (!aot_file_code_view && !runtime_edge &&
                 trial_count != *page_count)) {
            free(tb);
            if (runtime_edge) {
                return NULL;
            }
            continue;
        }
        memcpy(pages, trial_pages, trial_count * sizeof(*pages));
        *page_count = trial_count;
        if (source_ids) {
            source_ids[tu_data->tb_num] = id;
        }
        tu_push_back(tb);
        if (!already_owned) {
            templates[id]->bool_flags |= IS_AOT_TB;
            templates[id]->tc.ptr = tb;
        }
        return tb;
    }
    return NULL;
}

static TranslationBlock *get_ts_successor(TranslationBlock **page_tb_list,
        int page_tb_num, int entry_tb_id, CPUState *cpu, uintptr_t hva,
        TranslationBlock *tb, target_ulong pc, uint32_t flags, int cflags,
        int max_insns, TU_TB_START_TYPE mode, bool cross_page,
        const TranslationBlock *entry_template, uint32_t max_tb,
        AOTTUExplorePage *pages, uint32_t *page_count,
        int source_id, int edge, int *source_ids, AOTSpecState *spec, bool required_return)
{
    if (required_return) {
        TranslationBlock *local = aot_local_return(tb, pc, flags, cflags);
        if (local) {
            return local;
        }
    }
    if (cross_page) {
        return aot_tu_get_successor(page_tb_list, page_tb_num, entry_tb_id,
                cpu, pc, flags, cflags, max_insns, mode, entry_template,
                max_tb, pages, page_count, tb, source_id, edge, source_ids,
                spec, required_return);
    }
    if (get_page(tb->pc) != get_page(pc)) {
        return NULL;
    }
    int id = get_tb_id(page_tb_list, required_return ? 0 : entry_tb_id,
                       page_tb_num - 1,
                       pc, flags, cflags, false);
    if (required_return && id >= 0) {
        while (id > 0 &&
                (page_tb_list[id - 1]->pc & ~TARGET_PAGE_MASK) ==
                    (pc & ~TARGET_PAGE_MASK) &&
                page_tb_list[id - 1]->flags == flags &&
                page_tb_list[id - 1]->cflags == cflags) {
            id--;
        }
        for (; id < page_tb_num; id++) {
            TranslationBlock *candidate = page_tb_list[id];
            if ((candidate->pc & ~TARGET_PAGE_MASK) !=
                    (pc & ~TARGET_PAGE_MASK) || candidate->flags != flags ||
                    candidate->cflags != cflags) {
                id = -1;
                break;
            }
            if (aot_templates_share_tu(entry_template, candidate)) {
                break;
            }
        }
        if (id == page_tb_num) {
            id = -1;
        }
    }
    if (id < 0) {
        return NULL;
    }
    bool already_owned = page_tb_list[id]->bool_flags & IS_AOT_TB;
    if (already_owned && !required_return) {
        return (TranslationBlock *)page_tb_list[id]->tc.ptr;
    }
    if (tu_data->tb_num >= max_tb) {
        return NULL;
    }
    TranslationBlock *successor = aot_tb_create(cpu, pc, hva, page_tb_list[id],
                                               max_insns, true);
    tu_push_back(successor);
    if (!already_owned) {
        page_tb_list[id]->bool_flags |= IS_AOT_TB;
        page_tb_list[id]->tc.ptr = successor;
    }
    return successor;
}

/* CALL continuations are indivisible prerequisites for the paired layout.
 * Reserve them before optional branch exploration can claim their templates. */
static void aot_required_returns(TranslationBlock **templates, int count,
        int entry_id, CPUState *cpu, uintptr_t hva, uint32_t flags, int cflags,
        int max_insns, bool cross_page, const TranslationBlock *entry,
        uint32_t max_tb, AOTTUExplorePage *pages, uint32_t *page_count,
        int *source_ids, AOTSpecState *spec, int *scanned)
{
    if (!latxs_hras_aot_translate()) {
        return;
    }
    while (*scanned < tu_data->tb_num) {
        int i = (*scanned)++;
        TranslationBlock *tb = tu_data->tb_list[i];
        if (tb->s_data->last_ir1_type != IR1_TYPE_CALL &&
                tb->s_data->last_ir1_type != IR1_TYPE_CALLIN) {
            continue;
        }
        TranslationBlock *next = get_ts_successor(templates, count, entry_id,
                cpu, hva, tb, tb->s_data->x86_next_pc, flags, cflags,
                max_insns, TU_TB_START_NORMAL, cross_page, entry, max_tb,
                pages, page_count, source_ids ? source_ids[i] : -1,
                TU_TB_INDEX_NEXT, source_ids, spec, true);
        tb->s_data->next_tb[TU_TB_INDEX_NEXT] = next;
        if (!next) {
            aot_verbose_log("[AOT][Pair] missing return pc=%" PRIx64
                    " return=%" PRIx64 " flags=%x cflags=%x members=%u limit=%u\n",
                    (uint64_t)tb->pc, (uint64_t)tb->s_data->x86_next_pc,
                    flags, cflags, tu_data->tb_num, max_tb);
        }
    }
}

static inline void get_ts_queue(TranslationBlock **page_tb_list,
        int page_tb_num, int entry_tb_id, CPUState *cpu,
        uintptr_t hva, target_ulong cs_base,
        uint32_t flags, int cflags, int max_insns, bool cross_page,
        const TranslationBlock *entry_template, uint32_t max_tb,
        int *source_ids, AOTSpecState *spec)
{
    TranslationBlock **tb_list = tu_data->tb_list;
    AOTTUExplorePage aot_pages[AOT_TU_EXPLORE_MAX_PAGES];
    uint32_t aot_page_count = 0;
    int returns_scanned = 0;

    if (cross_page &&
            !aot_tu_add_template_pages(tb_list[0], aot_pages,
                                       &aot_page_count)) {
        /* Leave an invalid entry for the save-time rejection path. */
        return;
    }
    aot_required_returns(page_tb_list, page_tb_num, entry_tb_id, cpu,
                hva, flags, cflags, max_insns, cross_page, entry_template,
                max_tb, aot_pages, &aot_page_count, source_ids, spec,
                &returns_scanned);
    for (int i = 0; i < tu_data->tb_num &&
            tu_data->tb_num < max_tb; i++) {
        aot_required_returns(page_tb_list, page_tb_num, entry_tb_id, cpu,
                hva, flags, cflags, max_insns, cross_page, entry_template,
                max_tb, aot_pages, &aot_page_count, source_ids, spec,
                &returns_scanned);
        TranslationBlock *tb = tb_list[i];
        TranslationBlock *next_tb;
        TranslationBlock *target_tb;
        int source_id = source_ids ? source_ids[i] : -1;

        switch (tb->s_data->last_ir1_type) {
        case IR1_TYPE_BRANCH:
            next_tb = get_ts_successor(page_tb_list, page_tb_num, entry_tb_id,
                    cpu, hva, tb, tb->s_data->x86_next_pc, flags, cflags,
                    max_insns, TU_TB_START_NORMAL, cross_page,
                    entry_template, max_tb,
                    aot_pages, &aot_page_count, source_id,
                    TU_TB_INDEX_NEXT, source_ids, spec, false);
            if (next_tb && next_tb->tc.ptr != NULL) {
                tb->s_data->tu_jmp[TU_TB_INDEX_NEXT] = 0;
            }
            tb->s_data->next_tb[TU_TB_INDEX_NEXT] = next_tb;
            aot_required_returns(page_tb_list, page_tb_num, entry_tb_id, cpu,
                hva, flags, cflags, max_insns, cross_page, entry_template,
                max_tb, aot_pages, &aot_page_count, source_ids, spec,
                &returns_scanned);
            /* Look up the other arm against the updated page set. */
            /* fall through */
        case IR1_TYPE_JUMP:
            target_tb = get_ts_successor(page_tb_list, page_tb_num, entry_tb_id,
                    cpu, hva, tb, tb->s_data->x86_target_pc, flags, cflags,
                    max_insns,
                    tb->s_data->last_ir1_type == IR1_TYPE_JUMP ?
                    TU_TB_START_JMP : TU_TB_START_NORMAL,
                    cross_page, entry_template, max_tb,
                    aot_pages, &aot_page_count, source_id,
                    TU_TB_INDEX_TARGET, source_ids, spec, false);
            if (target_tb && target_tb->tc.ptr != NULL) {
                tb->s_data->tu_jmp[TU_TB_INDEX_TARGET] = 0;
            }
            tb->s_data->next_tb[TU_TB_INDEX_TARGET] = target_tb;
            break;
        case IR1_TYPE_CALLIN:
        case IR1_TYPE_CALL:
            if (latxs_hras_aot_translate()) {
                break;
            }
            /* fall through */
        case IR1_TYPE_NORMAL:
        case IR1_TYPE_SYSCALL:
            next_tb = get_ts_successor(page_tb_list, page_tb_num, entry_tb_id,
                    cpu, hva, tb, tb->s_data->x86_next_pc, flags, cflags,
                    max_insns, TU_TB_START_NORMAL, cross_page,
                    entry_template, max_tb,
                    aot_pages, &aot_page_count, source_id,
                    TU_TB_INDEX_NEXT, source_ids, spec, false);
            tb->s_data->next_tb[TU_TB_INDEX_NEXT] = next_tb;
            break;
        case IR1_TYPE_JUMPIN:
        case IR1_TYPE_RET:
            break;
        default:
            lsassert(0);
        }
    }

    aot_required_returns(page_tb_list, page_tb_num, entry_tb_id, cpu,
                hva, flags, cflags, max_insns, cross_page, entry_template,
                max_tb, aot_pages, &aot_page_count, source_ids, spec,
                &returns_scanned);

    if (tu_data->tb_num > max_tb) {
        lsassert(0);
    }
}

static void ts_tb_explore(CPUState *cpu, TranslationBlock **page_tb_list,
        int page_tb_num, int entry_tb_id, target_ulong page_addr, uintptr_t hva,
        bool cross_page, uint32_t max_tb, int *source_ids, AOTSpecState *spec)
{
    TranslationBlock **tb_list = tu_data->tb_list;
    uint32_t *tb_num_in_tu = &tu_data->tb_num;
    uint32_t *ir1_num_in_tu = &tu_data->ir1_num_in_tu;
    *tb_num_in_tu = 0;
    *ir1_num_in_tu = 0;

    TranslationBlock *entry_template = page_tb_list[entry_tb_id];
    TranslationBlock *entry = entry_template;
    int max_insns = get_max_insns(entry->cflags);
    target_ulong pc = cross_page ? entry->pc :
        page_addr + (entry->pc & ~TARGET_PAGE_MASK);

    /* the entry used as return value*/
    entry = aot_tb_create(cpu, pc,
            aot_template_hva(entry, hva, cross_page), entry, max_insns,
            true);
    if (entry == NULL) {
        return;
    }
    if (source_ids) {
        source_ids[0] = entry_tb_id;
    }
    tu_push_back(entry);
    /* fprintf(stderr, "ir1_num_in_tu %d\n", *ir1_num_in_tu); */
    page_tb_list[entry_tb_id]->bool_flags |= IS_AOT_TB;
    page_tb_list[entry_tb_id]->tc.ptr = entry;

    get_ts_queue(page_tb_list, page_tb_num, entry_tb_id, cpu, hva,
            entry->cs_base, entry->flags, entry->cflags, max_insns,
            cross_page, entry_template, max_tb, source_ids, spec);

    qsort(tb_list, *tb_num_in_tu, sizeof(TranslationBlock *), tb_sort_cmp);

    solve_tb_overlap(*tb_num_in_tu, tb_list, max_insns);

    tu_optimize_ir1(cpu, tb_list, *tb_num_in_tu,
            entry->cs_base, entry->flags, entry->cflags);
}

/*
 * AOT starts from observed TBs, then explores direct same-page successors just
 * like JIT TU discovery. Speculative TBs are compiler input, not execution
 * coverage. Keep them private to this page; never query the live TB hash/TLB.
 */
typedef struct AOTTUKey {
    target_ulong pc, cs_base;
    uint32_t flags, cflags;
} AOTTUKey;

typedef struct AOTTUPage {
    GHashTable *records;
    GHashTable *decoded;
    GPtrArray *blocks;
    uintptr_t hva;
    target_ulong pc;
    unsigned speculative;
} AOTTUPage;

static guint aot_tu_key_hash(gconstpointer data)
{
    const AOTTUKey *key = data;
    uint64_t value = key->pc ^ key->cs_base ^
                     ((uint64_t)key->flags << 32) ^ key->cflags;
    return value ^ (value >> 32);
}

static gboolean aot_tu_key_equal(gconstpointer a, gconstpointer b)
{
    const AOTTUKey *x = a, *y = b;
    return x->pc == y->pc && x->cs_base == y->cs_base &&
           x->flags == y->flags && x->cflags == y->cflags;
}

static AOTTUKey aot_tu_key(TranslationBlock *tb, target_ulong pc)
{
    return (AOTTUKey) { pc, tb->cs_base, tb->flags, tb->cflags };
}

static void aot_tu_insert(GHashTable *table, AOTTUKey key,
                          TranslationBlock *tb)
{
    AOTTUKey *stored = g_new(AOTTUKey, 1);
    *stored = key;
    g_hash_table_insert(table, stored, tb);
}

static TranslationBlock *aot_tu_successor(AOTTUPage *page, CPUState *cpu,
        TranslationBlock *from, target_ulong pc, bool required_return)
{
    TranslationBlock *tb, *record;
    AOTTUKey key = aot_tu_key(from, pc);
    gpointer found;

    if (!pc || get_page(pc) != page->pc) {
        return NULL;
    }
    if (required_return) {
        TranslationBlock *local = aot_local_return(from, pc,
                                                   from->flags, from->cflags);
        if (local) {
            return local;
        }
    }
    bool already_owned = false;
    if (g_hash_table_lookup_extended(page->decoded, &key, NULL, &found)) {
        tb = found;
        /*
         * TUs are loaded independently. A raw branch to a previous offline TU
         * would retain its save-time displacement after demand loading.
         */
        already_owned = tb && tb->tc.ptr;
        if (!required_return || !already_owned) {
            return already_owned ? NULL : tb;
        }
    }
    if (tu_data->tb_num >= MAX_TB_IN_AOT_TU) {
        return NULL;
    }
    record = g_hash_table_lookup(page->records, &key);
    if (!record) {
        if (!option_aot_tu_pretranslate && !required_return) {
            return NULL;
        }
        /*
         * Bound speculative work even for adversarial byte streams / modes.
         * A first instruction near the boundary could need an absent page.
         */
        if (page->speculative >= TARGET_PAGE_SIZE || inst_may_cross_page(pc)) {
            return NULL;
        }
        page->speculative++;
    }
    tb = aot_tb_create(cpu, pc, page->hva, record ? record : from,
                       get_max_insns(from->cflags), record != NULL);
    /* Remember failed decodes too, so cycles cannot repeatedly retry them. */
    if (!already_owned) {
        aot_tu_insert(page->decoded, key, tb);
    }
    if (tb) {
        g_ptr_array_add(page->blocks, tb);
        tu_push_back(tb);
    }
    return tb;
}

static void aot_page_required_returns(AOTTUPage *page, CPUState *cpu,
                                      int *scanned)
{
    if (!latxs_hras_aot_translate()) {
        return;
    }
    while (*scanned < tu_data->tb_num) {
        TranslationBlock *tb = tu_data->tb_list[(*scanned)++];
        if (tb->s_data->last_ir1_type == IR1_TYPE_CALL ||
                tb->s_data->last_ir1_type == IR1_TYPE_CALLIN) {
            TranslationBlock *next = aot_tu_successor(page, cpu, tb,
                    tb->s_data->x86_next_pc, true);
            tb->s_data->next_tb[TU_TB_INDEX_NEXT] = next;
            if (!next) {
                aot_verbose_log("[AOT][Pair] missing page return pc=%" PRIx64
                        " return=%" PRIx64 " members=%u limit=%u\n",
                        (uint64_t)tb->pc, (uint64_t)tb->s_data->x86_next_pc,
                        tu_data->tb_num, (unsigned)MAX_TB_IN_AOT_TU);
            }
        }
    }
}

static void get_aot_page_queue(AOTTUPage *page, CPUState *cpu)
{
    int returns_scanned = 0;
    aot_page_required_returns(page, cpu, &returns_scanned);
    for (int i = 0; i < tu_data->tb_num &&
                    tu_data->tb_num < MAX_TB_IN_AOT_TU; i++) {
        aot_page_required_returns(page, cpu, &returns_scanned);
        TranslationBlock *tb = tu_data->tb_list[i];
        TranslationBlock *next = NULL, *target = NULL;
        switch (tb->s_data->last_ir1_type) {
        case IR1_TYPE_BRANCH:
            next = aot_tu_successor(page, cpu, tb, tb->s_data->x86_next_pc, false);
            aot_page_required_returns(page, cpu, &returns_scanned);
            /* fall through */
        case IR1_TYPE_JUMP:
            target = aot_tu_successor(page, cpu, tb, tb->s_data->x86_target_pc, false);
            break;
        case IR1_TYPE_CALLIN:
        case IR1_TYPE_CALL:
            if (latxs_hras_aot_translate()) {
                next = tb->s_data->next_tb[TU_TB_INDEX_NEXT];
                break;
            }
            /* fall through */
        case IR1_TYPE_NORMAL:
        case IR1_TYPE_SYSCALL:
            next = aot_tu_successor(page, cpu, tb, tb->s_data->x86_next_pc, false);
            break;
        case IR1_TYPE_JUMPIN:
        case IR1_TYPE_RET:
        case IR1_TYPE_EOB:
            break;
        default:
            lsassert(0);
        }
        tb->s_data->next_tb[TU_TB_INDEX_NEXT] = next;
        tb->s_data->next_tb[TU_TB_INDEX_TARGET] = target;
    }
    aot_page_required_returns(page, cpu, &returns_scanned);
}

static bool aot_page_explore(AOTTUPage *page, CPUState *cpu,
                          TranslationBlock *record)
{
    tu_data->tb_num = 0;
    tu_data->ir1_num_in_tu = 0;
    target_ulong pc = page->pc + (record->pc & ~TARGET_PAGE_MASK);
    TranslationBlock *entry = aot_tu_successor(page, cpu, record, pc, false);
    if (!entry) {
        return false;
    }

    get_aot_page_queue(page, cpu);
    qsort(tu_data->tb_list, tu_data->tb_num,
          sizeof(TranslationBlock *), tb_sort_cmp);
    solve_tb_overlap(tu_data->tb_num, tu_data->tb_list,
                     get_max_insns(entry->cflags));
    tu_optimize_ir1(cpu, tu_data->tb_list, tu_data->tb_num,
                   entry->cs_base, entry->flags, entry->cflags);
    return true;
}

TranslationBlock **retranslate_by_tu(CPUState *cpu, target_ulong page_addr,
        uintptr_t hva, TranslationBlock **page_tb_list, int *page_tb_num)
{
    uint64_t ghbr_checkpoint = latxs_ghbr_stats_checkpoint();

    if (!*page_tb_num) {
        return NULL;
    }
    AOTTUPage page = {
        .records = g_hash_table_new_full(aot_tu_key_hash, aot_tu_key_equal,
                                         g_free, NULL),
        .decoded = g_hash_table_new_full(aot_tu_key_hash, aot_tu_key_equal,
                                         g_free, NULL),
        .blocks = g_ptr_array_new(),
        .hva = hva,
        .pc = page_addr,
    };
    /*
     * An observed page can have no decodable TBs left (e.g. discarded JIT code).
     * Keep a non-NULL empty result distinct from a translation failure.
     */
    GPtrArray *ans = g_ptr_array_sized_new(*page_tb_num);
    bool ok = false;

    for (int i = 0; i < *page_tb_num; i++) {
        TranslationBlock *tb = page_tb_list[i];
        target_ulong pc = page_addr + (tb->pc & ~TARGET_PAGE_MASK);
        aot_tu_insert(page.records, aot_tu_key(tb, pc), tb);
    }

    for (int i = 0; i < *page_tb_num; i++) {
        TranslationBlock *record = page_tb_list[i];
        target_ulong pc = page_addr + (record->pc & ~TARGET_PAGE_MASK);
        AOTTUKey key = aot_tu_key(record, pc);
        if (g_hash_table_contains(page.decoded, &key)) {
            continue;
        }
        if (!aot_page_explore(&page, cpu, record)) {
            continue;
        }
        int result = translate_tu(cpu, tu_data->tb_num, tu_data->tb_list);
        if (result != TU_TRANSLATE_OK) {
            fprintf(stderr, "[AOT] Save: TU translation failed at pc="
                    TARGET_FMT_lx " (%u TBs, result=%d)\n",
                    pc, tu_data->tb_num, result);
            goto out;
        }
        for (int j = 0; j < tu_data->tb_num; j++) {
            g_ptr_array_add(ans, tu_data->tb_list[j]);
        }
    }
    if (!ans->len) {
        aot_verbose_log("[AOT] Save: page=" TARGET_FMT_lx
                " has no translatable TBs (%d records)\n",
                page_addr, *page_tb_num);
    }
    ok = true;

out:
    if (ok) {
        latxs_ghbr_stats_commit();
    } else {
        latxs_ghbr_stats_restore(ghbr_checkpoint);
    }
    g_hash_table_destroy(page.records);
    g_hash_table_destroy(page.decoded);
    if (!ok) {
        for (int i = 0; i < page.blocks->len; i++) {
            TranslationBlock *tb = g_ptr_array_index(page.blocks, i);
            g_free(tb);
        }
    }
    g_ptr_array_free(page.blocks, true);
    *page_tb_num = ok ? ans->len : 0;
    return (TranslationBlock **)g_ptr_array_free(ans, !ok);
}

static int aot_full_pc_cmp(const void *ap, const void *bp)
{
    const TranslationBlock *a = *(TranslationBlock * const *)ap;
    const TranslationBlock *b = *(TranslationBlock * const *)bp;

    if (a->pc != b->pc) {
        return a->pc < b->pc ? -1 : 1;
    }
    if (a->flags != b->flags) {
        return a->flags < b->flags ? -1 : 1;
    }
    if (a->cflags != b->cflags) {
        return a->cflags < b->cflags ? -1 : 1;
    }
    if (a->cs_base != b->cs_base) {
        return a->cs_base < b->cs_base ? -1 : 1;
    }
    return 0;
}

static void release_cross_page_tu(TranslationBlock **tb_templates,
        int template_count, bool retry)
{
    for (int i = 0; i < tu_data->tb_num; i++) {
        TranslationBlock *tb = tu_data->tb_list[i];
        int template_id;

        if (!tb) {
            continue;
        }
        template_id = get_tb_id(tb_templates, 0, template_count - 1,
                                tb->pc, tb->flags, tb->cflags, true);
        while (template_id > 0 &&
                tb_templates[template_id - 1]->pc == tb->pc &&
                tb_templates[template_id - 1]->flags == tb->flags &&
                tb_templates[template_id - 1]->cflags == tb->cflags) {
            template_id--;
        }
        while (template_id >= 0 && template_id < template_count &&
                tb_templates[template_id]->pc == tb->pc &&
                tb_templates[template_id]->flags == tb->flags &&
                tb_templates[template_id]->cflags == tb->cflags) {
            if (tb_templates[template_id]->tc.ptr == tb) {
                tb_templates[template_id]->tc.ptr = NULL;
                if (retry) {
                    tb_templates[template_id]->bool_flags &= ~IS_AOT_TB;
                }
                break;
            }
            template_id++;
        }
        free(tb);
        tu_data->tb_list[i] = NULL;
    }
    tu_data->tb_num = 0;
}

bool retranslate_by_tu_cross_page(CPUState *cpu,
        TranslationBlock **tb_templates, int tb_num,
        AOTCrossPageTUVisitor visitor, void *opaque)
{
    int retry_count = 0;
    int recovered_count = 0;
    int entry_rejected = 0;
    int mapping_rejected = 0;
    int *source_ids = NULL;

    if (!tb_num || !visitor) {
        return false;
    }

    qsort(tb_templates, tb_num, sizeof(*tb_templates), aot_full_pc_cmp);
    source_ids = g_try_new(int, MAX_TB_IN_AOT_TU + 1);
    if (!source_ids) {
        fprintf(stderr, "[AOT][Save] Failed to initialize TU explore index\n");
        return false;
    }
    g_autoptr(GHashTable) spec_committed = NULL;
    g_autoptr(GHashTable) spec_pending = NULL;
    AOTSpecState spec_state = { 0 };
    AOTSpecState *spec = NULL;

    if ((option_aot_tu_pretranslate || latxs_hras_aot_translate()) &&
            !aot_file_code_view) {
        spec_committed = g_hash_table_new_full(aot_spec_key_hash,
                                               aot_spec_key_equal, g_free, NULL);
        spec_pending = g_hash_table_new_full(aot_spec_key_hash,
                                             aot_spec_key_equal, g_free, NULL);
        spec_state.committed = spec_committed;
        spec_state.pending = spec_pending;
        spec = &spec_state;
    }
    for (int i = 0; i < tb_num; i++) {
        tb_templates[i]->bool_flags &= ~IS_AOT_TB;
        tb_templates[i]->tc.ptr = NULL;
    }

    for (int i = 0; i < tb_num; i++) {
        if (tb_templates[i]->bool_flags & IS_AOT_TB) {
            continue;
        }
        uint32_t max_tb = MAX_TB_IN_AOT_TU;
        bool retried = false;

        for (;;) {
            uint64_t ghbr_checkpoint = latxs_ghbr_stats_checkpoint();

            ts_tb_explore(cpu, tb_templates, tb_num, i, 0, 0, true,
                          max_tb, source_ids, spec);
            uint32_t attempted = tu_data->tb_num;

            if (!attempted) {
                /* No usable TU entry; shrinking cannot help. */
                if (aot_file_code_view) {
                    g_free(source_ids);
                    return false;
                }
                entry_rejected++;
                break;
            }
            if ((aot_file_code_view && aot_file_code_view->failed) ||
                translate_tu(cpu, attempted, tu_data->tb_list) !=
                    TU_TRANSLATE_OK) {
                fprintf(stderr, "%s:%d error!\n", __func__, __LINE__);
                release_cross_page_tu(tb_templates, tb_num, false);
                g_free(source_ids); source_ids = NULL;
                return false;
            }
            AOTCrossPageTUResult result = visitor(tu_data->tb_list,
                                                  attempted, opaque);
            if (result == AOT_TU_SAVE_OK) {
                latxs_ghbr_stats_commit();
                if (spec) aot_spec_commit(spec);
            } else {
                latxs_ghbr_stats_restore(ghbr_checkpoint);
                if (spec) {
                    aot_spec_rollback(spec);
                }
            }
            bool retry = result == AOT_TU_SAVE_RETRY && attempted > 1;

            /*
             * Undo only this attempt's ownership. Excluded templates must
             * remain eligible for later TUs; already saved TUs stay marked.
             * Always rebuild IR and links, never truncate optimized code.
             */
            release_cross_page_tu(tb_templates, tb_num, retry);
            if (result == AOT_TU_SAVE_ERROR) {
                g_free(source_ids); source_ids = NULL;
                return false;
            }
            if (!retry) {
                if (result == AOT_TU_SAVE_RETRY) {
                    mapping_rejected++;
                } else if (retried) {
                    recovered_count++;
                }
                break;
            }
            /* Strict decrease bounds retries, including a two-arm branch. */
            max_tb = attempted / 2;
            retry_count++;
            retried = true;
        }
    }

    if (retry_count) {
        aot_verbose_log("[AOT][Save] TU shrink: retries=%d, recovered=%d\n",
                retry_count, recovered_count);
    }
    /* File merging checks complete coverage and reports one group summary. */
    if (!aot_file_code_view || entry_rejected || mapping_rejected) {
        aot_verbose_log("[AOT][Save] Skipped TBs=%d "
                "(entry_rejected=%d, mapping_rejected=%d)\n",
                entry_rejected + mapping_rejected,
                entry_rejected, mapping_rejected);
    }
    g_free(source_ids); source_ids = NULL;
    return true;
}

static TranslationBlock* create_check_tb(CPUState *cpu, TranslationBlock *aot_tb,
        target_ulong pc,
        target_ulong cs_base, uint32_t flags, int cflags,
        int max_insns, bool is_first_tb, TU_TB_START_TYPE mode)
{
    TranslationBlock* tb = NULL;
    CPUArchState *env = cpu->env_ptr;
    tb_page_addr_t phys_pc;
    void *host_pc;

    if (unlikely(pc == 0)) {
        return NULL;
    }

    uint32_t mtcc_id = curr_mtcc(env);
    tcg_ctx = tcg_ctx_mtcc[mtcc_id];

    phys_pc = get_page_addr_code_hostp(env, pc, &host_pc);

    if (phys_pc == -1) {
        /* Generate a one-shot TB with 1 insn in it */
        cflags = (cflags & ~CF_COUNT_MASK) | CF_LAST_IO | 1;
    }

#ifdef CONFIG_SOFTMMU
    tlb_protect_code(phys_pc);
#endif

    QEMU_BUILD_BUG_ON(CF_COUNT_MASK + 1 != TCG_MAX_INSNS);

    /* todo: now check 16bit(should 15bit for x86) */
    //uint32_t check_zero = cpu_lduw_code((CPUArchState *)cpu->env_ptr, pc);
    //if (unlikely(check_zero == 0)){
    //    return NULL;
    //}

    tb = &tu_tb_tmp;
    tb->s_data = &tmp_s_data;
    tu_reset_tb(tb);

    tb->pc = pc;
    tb->cs_base = cs_base;
    tb->flags = flags;
    tb->cflags = cflags;
    clear_misc_flags(tb);
    tb_set_page_addr0(tb, phys_pc);
    tb_next_data_reset(tb);
#ifdef CONFIG_SOFTMMU
    if (option_fast_itlb) {
        tb->hva = phys_pc & TARGET_PAGE_MASK;
    } else {
        int error = 0;
        tb->hva = latxs_tr_cvt_cpu_addr_code(env, pc, &error);
    }
#endif
    tb_set_page_addr1(tb, -1);
    set_mask_to_miscflags(tb, curr_mtcc(env),
                          MF_MTCC_ID_MASK, MF_MTCC_ID_SHIFT);
    tb->preorder_tb_offset = 0;
    tb->preorder_next = NULL;
    tb->inst_ptr_offset = 0;

#ifdef CONFIG_LATX_PROFILER
    CLN_TB_PROFILE(tb);
#endif

#if defined(CONFIG_TB_FLAGS_NO_EFLAGS)
    if (tb->flags == cpu_get_tb_cpu_state_flags_no_eflags(env)) {
        add_misc_flags(tb, MF_FLAGS_NOEFLAGS);
    }
#endif

    tcg_ctx->addr_type = TARGET_LONG_BITS == 32 ? TCG_TYPE_I32 : TCG_TYPE_I64;
#ifdef CONFIG_SOFTMMU
    tcg_ctx->page_bits = TARGET_PAGE_BITS;
    tcg_ctx->page_mask = TARGET_PAGE_MASK;
    tcg_ctx->tlb_dyn_max_bits = CPU_TLB_DYN_MAX_BITS;
    tcg_ctx->tlb_fast_offset =
        (int)offsetof(ArchCPU, neg.tlb.f) - (int)offsetof(ArchCPU, env);
#endif
    tcg_ctx->insn_start_words = TARGET_INSN_START_WORDS;
#ifdef TCG_GUEST_DEFAULT_MO
    tcg_ctx->guest_mo = TCG_GUEST_DEFAULT_MO;
#else
    tcg_ctx->guest_mo = TCG_MO_ALL;
#endif
//    tb->trace_vcpu_dstate = *cpu->trace_dstate;
//    /* tb->tu_start_mode = mode; */
//    tcg_ctx->tb_cflags = cflags;
//    tcg_ctx->tb_jmp_reset_offset = tb->jmp_reset_offset;
//    if (TCG_TARGET_HAS_direct_jump) {
//        tcg_ctx->tb_jmp_insn_offset = tb->jmp_target_arg;
//        tcg_ctx->tb_jmp_target_addr = NULL;
//    } else {
//        tcg_ctx->tb_jmp_insn_offset = NULL;
//        tcg_ctx->tb_jmp_target_addr = tb->jmp_target_arg;
//    }

    latxs_tb_reset(tb);

    TRANSLATION_DATA *td = lsenv->tr_data;
    td->tb_too_large_pir1 = NULL;
    td->sys_eob_pir1 = NULL;

    latxs_tr_sys_init(tb, max_insns, tcg_ctx->code_gen_highwater, host_pc);
    td->is_tu_mode = 1;
    latxs_tr_disasm(tb);
    if (td->deadlock_avoidance) {
//        printf("%s:%d tu deadlock avoidance %d\n",
//                __func__, __LINE__, td->deadlock_avoidance);
        return NULL;
    }
    copy_tr_data_to_s_data(tb);

    latxs_inst_pattern(tb);
    latxs_flag_reduction(tb);

    if (tb->icount == 0 || tb->s_data->tu_tb_mode == TU_TB_MODE_BROKEN
			|| tb->s_data->tu_tb_mode == BAD_TB) {
        return NULL;
    }
    tb = tcg_tb_alloc_full(tcg_ctx);
    if (unlikely(!tb)) {
        tb_unlock_ctx_pages(1);
        //qemu_log_mask(LAT_LOG_AOT, "tb flush in tu translate\n");
        /* exit(-1); */
        /* flush must be done */
        tb_flush_mtcc(cpu, mtcc_id);
        mmap_unlock();
        /* Make the execution loop process the flush as soon as possible.  */
        cpu->exception_index = EXCP_INTERRUPT;
        tu_trees_reset();
        cpu_loop_exit(cpu);
    }
    aot_tb->s_data = tb->s_data;
    tb = aot_tb;
    tu_tb_tmp.s_data = tb->s_data;
    memcpy(tb, &tu_tb_tmp, sizeof(TranslationBlock));
    memcpy(tb->s_data, &tmp_s_data, sizeof(struct separated_data));
//    /* tu_tree_insert(tb); */
    tu_tree_insert(tb);
//    aot_tb_insert(tb);

    return tb;
}

TranslationBlock *gen_check_tu(CPUState *cpu, TranslationBlock **aot_tb_list, int tb_count)
{
    uint64_t ghbr_checkpoint = latxs_ghbr_stats_checkpoint();

    tu_trees_reset();
    TranslationBlock *tb = aot_tb_list[0];
    int max_insns = get_max_insns(tb->cflags);
    uint32_t *tb_num_in_tu = &tu_data->tb_num;
    uint32_t *ir1_num_in_tu = &tu_data->ir1_num_in_tu;
    *tb_num_in_tu = 0;
    *ir1_num_in_tu = 0;

    TranslationBlock old_tb_list[tb_count];
    for (int i = 0; i < tb_count; i++) {
        memcpy(&old_tb_list[i], aot_tb_list[i], sizeof(TranslationBlock));
    }


    for (int i = 0; i < tb_count; i++) {
        tb = aot_tb_list[i];
        fprintf(stderr, "pc %lx tb->icount %d ptr %p tb_addr %p\n",
                tb->pc, tb->icount,
                tb->tc.ptr, tb);
        tb = create_check_tb(cpu, tb, tb->pc, tb->cs_base, tb->flags,
                tb->cflags, max_insns, 0, TU_TB_START_NORMAL);
        if (tb == NULL) {
            return NULL;
        }
        tu_push_back(tb);
    }


    TranslationBlock** tb_list = tu_data->tb_list;

    for (int i = 0; i < *tb_num_in_tu; i++) {
        for (int j = 0; j < *tb_num_in_tu; j++) {
            tb = tb_list[i];
            if (tb->s_data->x86_target_pc == tb_list[j]->pc) {
                tb->s_data->next_tb[TU_TB_INDEX_TARGET] = tb_list[j];
            }
            if (tb->s_data->x86_next_pc == tb_list[j]->pc) {
                tb->s_data->next_tb[TU_TB_INDEX_NEXT] = tb_list[j];
            }
        }
    }

    solve_tb_overlap(*tb_num_in_tu, tb_list, max_insns);

    int translate_result =
        translate_tu(cpu, *tb_num_in_tu, tb_list);
    if (translate_result != TU_TRANSLATE_OK) {
        latxs_ghbr_stats_restore(ghbr_checkpoint);
        //return tb_gen_code(cpu, pc, cs_base, flags, cflags);
        /* goto re_translate; */
        return NULL;
    }
    latxs_ghbr_stats_restore(ghbr_checkpoint);

    for (int i = 0; i < *tb_num_in_tu; i++) {
        tb = tb_list[i];
        latxs_ras_unlink(tb);
        if (option_rastoptp) {
            qatomic_and(&tb->misc_flags, ~MF_RAS_CLEAR_B0);
            qatomic_or(&tb->misc_flags,
                        old_tb_list[i].misc_flags & MF_RAS_CLEAR_B0);
        }
        aot_ras_clear(tb);
        /* fprintf(stderr, "i %d tb addr %p \n", i, tb); */
        /* fprintf(stderr, "check tb pc %lx tb->icount %d\n", tb->pc, tb->icount); */
        /* fprintf(stderr, "check tb misc_flags %x aot tb misc_flags %x\n", */
        /*         tb->misc_flags, old_tb_list[i].misc_flags); */
        /* fprintf(stderr, "mmio type %d %d\n", tb->opt_mmio_type, old_tb_list[i].opt_mmio_type); */
        /* tb->ir2_num = old_tb_list[i].ir2_num; */
        /* tb->size = old_tb_list[i].size; */
        /* tb->tc.ptr = old_tb_list[i].tc.ptr; */
        /* tb->jmp_reset_offset[0] = old_tb_list[i].jmp_reset_offset[0]; */
        /* tb->jmp_reset_offset[1] = old_tb_list[i].jmp_reset_offset[1]; */
        /* tb->jmp_insn_offset[0] = old_tb_list[i].jmp_insn_offset[0]; */
        /* tb->jmp_insn_offset[1] = old_tb_list[i].jmp_insn_offset[1]; */
        /* tb->link_opt[0] = old_tb_list[i].link_opt[0]; */
        /* tb->link_opt[1] = old_tb_list[i].link_opt[1]; */
        /* tb->next_pc[0] = old_tb_list[i].next_pc[0]; */
        /* tb->next_pc[1] = old_tb_list[i].next_pc[1]; */
        /* tb->misc_flags = old_tb_list[i].misc_flags; */
        /* tb->inst_ptr_offset = old_tb_list[i].inst_ptr_offset; */
        /* tb->search_ptr = old_tb_list[i].search_ptr; */
        /* check_aot_tb(&old_tb_list[i], tb); */
        tb->s_data->next_tb[0] = NULL;
        tb->s_data->next_tb[1] = NULL;
        if (i) {
            tb->s_data->next_tb[0] = tb_list[i - 1];
        }
        if (i != *tb_num_in_tu - 1) {
            tb->s_data->next_tb[1] = tb_list[i + 1];
        }
    }
    fprintf(stderr, "tb offset_in_tu %lx \n", tb->tc.offset_in_tu);
    return tb_list[0];
}
#endif
