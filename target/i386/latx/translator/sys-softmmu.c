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
#include "translate.h"
#include "latx-native-asm.h"
#include "latx-sys-hamt-dmw.h"
#include "hamt-dmw.h"
#include "sys-aot.h"
#include "sysemu/rr.h"
#include "sysemu/sysemu.h"
#include "tcg/tcg-ldst.h"

#ifdef CONFIG_SOFTMMU

typedef struct SoftmmuRmwHitCounter {
    uint64_t pc;
    uint64_t hits;
} SoftmmuRmwHitCounter;

static GHashTable *softmmu_rmw_hit_counters;
static GMutex softmmu_rmw_hit_lock;
static gsize softmmu_rmw_hit_initialized;

static void softmmu_rmw_hit_report(Notifier *notifier, void *opaque)
{
    GHashTableIter iter;
    gpointer value;
    (void)notifier;
    (void)opaque;
    g_mutex_lock(&softmmu_rmw_hit_lock);
    g_hash_table_iter_init(&iter, softmmu_rmw_hit_counters);
    while (g_hash_table_iter_next(&iter, NULL, &value)) {
        SoftmmuRmwHitCounter *counter = value;
        uint64_t hits = qatomic_read(&counter->hits);
        if (hits) {
            fprintf(stderr, "[LATX-SYS] smmurmw fast-hit pc=%#" PRIx64
                    " hits=%" PRIu64 "\n", counter->pc, hits);
        }
    }
    g_mutex_unlock(&softmmu_rmw_hit_lock);
}

static Notifier softmmu_rmw_hit_notifier = {
    .notify = softmmu_rmw_hit_report,
};

/* Optional diagnostic only.  Counters are per guest PC and deliberately
 * persist until process exit so already translated code keeps valid pointers. */
static uint64_t *latxs_softmmu_rmw_hit_counter(void)
{
    const char *enabled = getenv("LATS_SMMURMW_HIT");
    if (!enabled || strcmp(enabled, "1")) {
        return NULL;
    }
    if (g_once_init_enter(&softmmu_rmw_hit_initialized)) {
        softmmu_rmw_hit_counters = g_hash_table_new(g_int64_hash, g_int64_equal);
        qemu_add_exit_notifier(&softmmu_rmw_hit_notifier);
        g_once_init_leave(&softmmu_rmw_hit_initialized, 1);
    }
    uint64_t pc = lsenv->tr_data->curr_ir1_inst->info->address;
    g_mutex_lock(&softmmu_rmw_hit_lock);
    SoftmmuRmwHitCounter *counter = g_hash_table_lookup(softmmu_rmw_hit_counters, &pc);
    if (!counter) {
        counter = g_new0(SoftmmuRmwHitCounter, 1);
        counter->pc = pc;
        g_hash_table_insert(softmmu_rmw_hit_counters, &counter->pc, counter);
    }
    g_mutex_unlock(&softmmu_rmw_hit_lock);
    return &counter->hits;
}

inline int can_do_hamt(CPUX86State *env)
{
    int do_hamt = hamt_enabled();

    TRANSLATION_DATA *td = lsenv->tr_data;
    if (td->opt_mmio && td->inst_do_opt_mmio) do_hamt = 0;

    if (!hamt_do_cpl(env->hflags & 0x3)) do_hamt = 0;
    if (env->hflags & HF_HAMT_EN_MASK) {
        assert(x86_hamt_data_mode_supported(get_pg_mode(env)));
    } else {
        do_hamt = 0;
    }
    if (hamt_disable_win32k() &&
            env->eip >= 0xfffff96000000000ULL &&
            env->eip <= 0xfffff97FFFFFFFFFULL)
    {
        do_hamt = 0;
    }

    return do_hamt;
}

static int is_ldst_realized_by_softmmu(IR2_OPCODE op)
{
    if (option_fast_fpr_ldst) {
        switch (op) {
        default: break;
        case LISA_VLD:
        case LISA_VST:
        case LISA_FLD_S:
        case LISA_FLD_D:
        case LISA_FST_S:
        case LISA_FST_D:
            return true;
        }
    }
    switch (op) {
    case LISA_LD_B:
    case LISA_LD_BU:
    case LISA_LD_H:
    case LISA_LD_HU:
    case LISA_LD_W:
    case LISA_LD_WU:
    case LISA_LD_D:
    case LISA_ST_B:
    case LISA_ST_H:
    case LISA_ST_W:
    case LISA_ST_D:
        return 1;
    default:
        return 0;
    }
}

static int convert_to_tcgmemop(IR2_OPCODE op)
{
    switch (op) {
    case LISA_LD_B:
        return MO_SB;
    case LISA_LD_BU:
    case LISA_ST_B:
        return MO_UB;
    case LISA_LD_H:
        return MO_LESW;
    case LISA_LD_HU:
    case LISA_ST_H:
        return MO_LEUW;
    case LISA_LD_W:
        return MO_LESL;
    case LISA_FLD_S:
    case LISA_FST_S:
    case LISA_LD_WU:
    case LISA_ST_W:
        return MO_LEUL;
    case LISA_FLD_D:
    case LISA_FST_D:
    case LISA_LD_D:
    case LISA_ST_D:
        return MO_LEUQ;
    case LISA_VLD:
    case LISA_VST:
        return MO_LEUQ;
    default:
        lsassertm(0, "not support IR2 %d in convert_to_tcgmemop.\n", op);
        return -1;
    }
}

static int get_ldst_align_bits(IR2_OPCODE opc)
{
    if (option_fast_fpr_ldst) {
        if (opc == LISA_VLD || opc == LISA_VST) {
            return 4;
        }
        if (opc == LISA_FLD_S || opc == LISA_FST_S) {
            return 2;
        }
        if (opc == LISA_FLD_D || opc == LISA_FST_D) {
            return 3;
        }
    }
    switch (opc) {
    case LISA_LD_B:
    case LISA_LD_BU:
    case LISA_ST_B:
        return 0; /* MemOp & MO_SIZE == MO_8  == 0 */
    case LISA_LD_H:
    case LISA_LD_HU:
    case LISA_ST_H:
        return 1; /* MemOp & MO_SIZE == MO_16 == 1 */
    case LISA_LD_W:
    case LISA_LD_WU:
    case LISA_ST_W:
        return 2; /* MemOp & MO_SIZE == MO_32 == 2 */
    case LISA_LD_D:
    case LISA_ST_D:
        return 3; /* MemOp & MO_SIZE == MO_64 == 3 */
    default:
        return -1; /* no support for unaligned access */
    }
}

/*
 * LATS: with HAMT DMW active cputlb.c keeps CPUTLBDescFast.table_dmw, a
 * pre-tagged copy of .table.  Loading it directly removes the Lu52i that the
 * softmmu fast paths used to emit after every table load.
 */
static inline int latx_tlb_table_off(int fast_off)
{
    if (hamt_enabled() && hamt_mode_dmw()) {
        return fast_off + (int)offsetof(CPUTLBDescFast, table_dmw);
    }
    return fast_off + (int)offsetof(CPUTLBDescFast, table);
}

#define TLB_MASK_TABLE_OFS(IDX) \
    ((int)offsetof(ArchCPU, neg.tlb.f[IDX]) - (int)offsetof(ArchCPU, env))

void tr_refill_code_fast_itlb(IR2_OPND *gva, IR2_OPND *gp12,
        IR2_OPND *tmp0, IR2_OPND *tmp1, IR2_OPND *tmp2,
        int mmu, int ps)
{
    int fast_off  = TLB_MASK_TABLE_OFS(mmu);
    int table_off = fast_off + offsetof(CPUTLBDescFast, itlb);

    if (int32_in_int12(table_off)) {
        la_out2i(LISA_LD_D, tmp0, &la_env, table_off);
    } else {
        la_imm(tmp0, (int64_t)table_off);
        la_out3(LISA_ADD_D, tmp0, &la_env, tmp0);
        la_out2i(LISA_LD_D, tmp0, tmp0, 0);
    }
    if (hamt_enabled()) latxs_tr_dmw_in_fix_mem(tmp0); /* &iTLB */
    la_out2ii(LISA_BSTRPICK_D, tmp1, gva, 21, 12); /* index */

    la_out3i(LISA_ALSL_D, tmp0, tmp1, tmp0, 2); /* &iTLB[index] */
    if (ps == 4) {
        /* gva[47:12] , gp12[39:12] */
        if (hamt_enabled() && option_table_addr) {
            la_out2ii(LISA_BSTRPICK_D, gp12, gp12, 39, 12);
            la_out2i(LISA_SLLI_D, tmp1, gva, 16);
            la_out2ii(LISA_BSTRINS_D, tmp1, gp12, 27, 0);
        } else {
            la_out2ii(LISA_BSTRPICK_D, tmp1, gva, 47, 12);
            la_out2ii(LISA_BSTRPICK_D, gp12, gp12, 39, 12);
            la_out2i(LISA_SLLI_D, tmp1, tmp1, 28);
            la_out3(LISA_OR, tmp1, tmp1, gp12);
        }
    } else if (ps == 2) {
        /* gva[47:12] , gp12[39:21] gva[20:12] */
        la_out2ii(LISA_BSTRPICK_D, gp12, gp12, 39, 21);
        la_out2i (LISA_SLLI_D,     gp12, gp12, 9);
        la_out2ii(LISA_BSTRPICK_D, tmp1, gva,  20, 12);
        la_out3  (LISA_OR,         gp12, gp12, tmp1);
        if (hamt_enabled() && option_table_addr) {
            la_out2i(LISA_SLLI_D, tmp1, gva, 16);
            la_out2ii(LISA_BSTRINS_D, tmp1, gp12, 27, 0);
        } else {
            la_out2ii(LISA_BSTRPICK_D, tmp1, gva, 47, 12);
            la_out2i(LISA_SLLI_D, tmp1, tmp1, 28);
            la_out3(LISA_OR, tmp1, tmp1, gp12);
        }
    } else {
        g_assert_not_reached();
    }
    /* store mixed data into itlb entry */
    la_out2i(LISA_ST_D, tmp1, tmp0, 0);
}

void tr_lookup_code_fast_itlb(IR2_OPND *hva,
                              IR2_OPND *gva, int mmu, IR2_OPND label)
{
    int fast_off  = TLB_MASK_TABLE_OFS(mmu);
    int table_off = fast_off + offsetof(CPUTLBDescFast, itlb);

    IR2_OPND itlb  = latxs_arg0_ir2_opnd;
    IR2_OPND index = latxs_arg1_ir2_opnd;
    IR2_OPND tmp   = latxs_arg2_ir2_opnd;

    if (int32_in_int12(table_off)) {
        la_out2i(LISA_LD_D, &itlb, &la_env, table_off);
    } else {
        la_imm(&itlb, (int64_t)table_off);
        la_out3(LISA_ADD_D, &itlb, &la_env, &itlb);
        la_out2i(LISA_LD_D, &itlb, &itlb, 0);
    }
    if (hamt_enabled()) latxs_tr_dmw_in_fix_mem(&itlb);

    la_out2ii(LISA_BSTRPICK_D, &index, gva, 21, 12);

    la_out3i(LISA_ALSL_D, &tmp, &index, &itlb, 2);
    la_out2i(LISA_LD_D, hva, &tmp, 0);
}

/* return label point to slow path */
void tr_lookup_code_hva(IR2_OPND *hva, IR2_OPND *gva, int mmu, IR2_OPND label)
{
    IR2_OPND *env = &la_env;

    /* offset from ENV */
    int fast_off  = TLB_MASK_TABLE_OFS(mmu);
    int mask_off  = fast_off + offsetof(CPUTLBDescFast, mask);  /* 64-bit */
    int table_off = latx_tlb_table_off(fast_off); /* DMW-ready */
    /* offset from tlb entry */
    int add_off   = offsetof(CPUTLBEntry, addend); /* 64-bit */
    int tag_off   = offsetof(CPUTLBEntry, addr_code); /* 32-bit */

    /* 1. load f[mmu].mask */
    IR2_OPND mask_opnd = latxs_arg0_ir2_opnd;
    if (int32_in_int12(mask_off)) {
        latxs_append_ir2_opnd2i(LISA_LD_D, &mask_opnd, env, mask_off);
    } else {
        latxs_load_imm32_to_ir2(&mask_opnd, mask_off, EXMode_S);
        latxs_append_ir2_opnd3(LISA_ADD_D, &mask_opnd, env, &mask_opnd);
        latxs_append_ir2_opnd2i(LISA_LD_D, &mask_opnd, &mask_opnd, 0);
    }

    /* 2. extract tlb index */
    IR2_OPND index_opnd = latxs_arg1_ir2_opnd;
    /* 2.1 index = address >> shift */
    int shift = TARGET_PAGE_BITS - CPU_TLB_ENTRY_BITS;
    latxs_append_ir2_opnd2i(LISA_SRLI_W, &index_opnd, gva, shift);

    /* 2.2 index = index & mask */
    latxs_append_ir2_opnd3(LISA_AND, &index_opnd, &index_opnd, &mask_opnd);
    latxs_ra_free_temp(&mask_opnd);

    /* 3. load f[mmu].table */
    IR2_OPND table_opnd = latxs_arg2_ir2_opnd;
    if (int32_in_int12(table_off)) {
        latxs_append_ir2_opnd2i(LISA_LD_D, &table_opnd, env, table_off);
    } else {
        latxs_load_imm32_to_ir2(&table_opnd, table_off, EXMode_S);
        latxs_append_ir2_opnd3(LISA_ADD_D, &table_opnd, env, &table_opnd);
        latxs_append_ir2_opnd2i(LISA_LD_D, &table_opnd, &table_opnd, 0);
    }
    /* 4. tlb entry = table + index */
    latxs_append_ir2_opnd3(LISA_ADD_D, &table_opnd, &table_opnd, &index_opnd);
    latxs_ra_free_temp(&index_opnd);

    /* 5. load compare part from tlb entry */
    IR2_OPND tag_opnd = latxs_arg0_ir2_opnd;
    if (TARGET_LONG_BITS == 32) {
        latxs_append_ir2_opnd2i(LISA_LD_WU, &tag_opnd, &table_opnd, tag_off);
    } else {
        latxs_append_ir2_opnd2i(LISA_LD_D, &tag_opnd, &table_opnd, tag_off);
    }

    IR2_OPND cmp_opnd = latxs_arg1_ir2_opnd;

    latxs_append_ir2_opnd2i(LISA_ADDI_D, &cmp_opnd, gva, 0);
    latxs_append_ir2_opnd2ii(LISA_BSTRINS_D, &cmp_opnd, &la_zero,
            TARGET_PAGE_BITS - 1, 0);

    /* 7. compare cmp and tag */
    latxs_append_ir2_opnd3(LISA_BNE,
            &cmp_opnd, &tag_opnd, &label);

    /* 7.1 ================= SoftTLB hit ============== */
    latxs_ra_free_temp(&tag_opnd);
    latxs_ra_free_temp(&cmp_opnd);

    /* 8. load addend from tlb entry */
    latxs_append_ir2_opnd2i(LISA_LD_D, hva, &table_opnd, add_off);
    latxs_ra_free_temp(&table_opnd);

    latxs_append_ir2_opnd3(LISA_ADD_D, hva, hva, gva);
}

/* return label point to slow path */
static void tr_gen_lookup_qemu_tlb(
        IR2_OPCODE op,
        IR2_OPND *gpr_opnd,
        IR2_OPND *mem,
        int mmu_index,
        bool is_load,
        IR2_OPND label_slow_path,
        IR2_OPND *label_fast_path)
{
    IR2_OPND *zero = &latxs_zero_ir2_opnd;
    IR2_OPND *env = &latxs_env_ir2_opnd;

    /*
     * option_softmmu_pcaddi_cold: move the per-access "pcaddi a3, 0" anchor
     * out of the hot path and into the per-access cold stub generated by
     * tr_gen_softmmu_slow_path_common().  This is only well defined together
     * with the common slow-path helper: option_common_slowpath == 0 calls a
     * per-access specific helper with a translation-time retaddr and needs no
     * anchor at all, so keep that mode byte-identical.
     */
    bool pcaddi_cold = option_softmmu_pcaddi_cold && option_common_slowpath;

#define TLB_MASK_TABLE_OFS(IDX) \
    ((int)offsetof(ArchCPU, neg.tlb.f[IDX]) - (int)offsetof(ArchCPU, env))

    /* offset from ENV */
    int fast_off  = TLB_MASK_TABLE_OFS(mmu_index);
    int mask_off  = fast_off + offsetof(CPUTLBDescFast, mask);  /* 64-bit */
    int table_off = latx_tlb_table_off(fast_off); /* DMW-ready */
    /* offset from tlb entry */
    int add_off   = offsetof(CPUTLBEntry, addend); /* 64-bit */
    int tag_off   = (is_load ? offsetof(CPUTLBEntry, addr_read)
                   : offsetof(CPUTLBEntry, addr_write)); /* 32-bit */

    if (rr_counter_enabled()) {
        int offset = 0;
        switch (op) {
        case LISA_LD_B:
        case LISA_LD_BU:
        case LISA_LD_H:
        case LISA_LD_HU:
        case LISA_LD_W:
        case LISA_LD_WU:
        case LISA_LD_D: offset = rr_offsetof(RR_EVENT_STLB_LD); break;
        case LISA_ST_B:
        case LISA_ST_H:
        case LISA_ST_W:
        case LISA_ST_D: offset = rr_offsetof(RR_EVENT_STLB_ST); break;
        case LISA_FLD_S:
        case LISA_FLD_D: offset = rr_offsetof(RR_EVENT_STLB_FLD); break;
        case LISA_FST_S:
        case LISA_FST_D: offset = rr_offsetof(RR_EVENT_STLB_FST); break;
        case LISA_VLD: offset = rr_offsetof(RR_EVENT_STLB_VLD); break;
        case LISA_VST: offset = rr_offsetof(RR_EVENT_STLB_VST); break;
        default: assert(0); break;
        }
        IR2_OPND *base = &latxs_arg0_ir2_opnd;
        IR2_OPND *data = &latxs_arg1_ir2_opnd;
        latxs_append_ir2_opnd2i(LISA_LD_D, base, env, offsetof(CPUX86State, rr_counter_ptr));
        if (hamt_enabled() && hamt_mode_dmw()) {
            latxs_tr_dmw_in_fix_mem(base);
        }
        assert(int32_in_int12(offset));
        latxs_append_ir2_opnd2i(LISA_LD_D, data, base, offset);
        latxs_append_ir2_opnd2i(LISA_ADDI_D, data, data, 0x1);
        latxs_append_ir2_opnd2i(LISA_ST_D, data, base, offset);
    }

    /* 1. load f[mmu].mask */
    IR2_OPND mask_opnd = latxs_arg0_ir2_opnd;
    if (int32_in_int12(mask_off)) {
        latxs_append_ir2_opnd2i(LISA_LD_D, &mask_opnd, env, mask_off);
    } else {
        latxs_load_imm32_to_ir2(&mask_opnd, mask_off, EXMode_S);
        latxs_append_ir2_opnd3(LISA_ADD_D, &mask_opnd, env, &mask_opnd);
        latxs_append_ir2_opnd2i(LISA_LD_D, &mask_opnd, &mask_opnd, 0);
    }

    /* 2. extract tlb index */
    IR2_OPND index_opnd = latxs_arg1_ir2_opnd;
    /* 2.1 index = address >> shift */
    int shift = TARGET_PAGE_BITS - CPU_TLB_ENTRY_BITS;
    latxs_append_ir2_opnd2i(LISA_SRLI_W, &index_opnd, mem, shift);

    /* 2.2 index = index & mask */
    latxs_append_ir2_opnd3(LISA_AND, &index_opnd, &index_opnd, &mask_opnd);

    /* 3. load f[mmu].table */
    IR2_OPND table_opnd = latxs_arg2_ir2_opnd;
    if (int32_in_int12(table_off)) {
        latxs_append_ir2_opnd2i(LISA_LD_D, &table_opnd, env, table_off);
    } else {
        latxs_load_imm32_to_ir2(&table_opnd, table_off, EXMode_S);
        latxs_append_ir2_opnd3(LISA_ADD_D, &table_opnd, env, &table_opnd);
        latxs_append_ir2_opnd2i(LISA_LD_D, &table_opnd, &table_opnd, 0);
    }
    /* 4. tlb entry = table + index */
    latxs_append_ir2_opnd3(LISA_ADD_D, &table_opnd, &table_opnd, &index_opnd);
    IR2_OPND tlb_opnd = table_opnd;

    /* 5. load compare part from tlb entry */
    IR2_OPND tag_opnd = latxs_arg0_ir2_opnd;
    if (TARGET_LONG_BITS == 32) {
        latxs_append_ir2_opnd2i(LISA_LD_WU, &tag_opnd, &tlb_opnd, tag_off);
    } else {
        latxs_append_ir2_opnd2i(LISA_LD_D, &tag_opnd, &tlb_opnd, tag_off);
    }
    /* 6. cmp = vaddr & mask */
    /*
     *                      12 11 8 7  4 3  0
     * vaddr 0x 1111 .... 1111 0000 0000 0000
     *                       ^              \- align_bits = 0 ; B  8-bits
     *                       |                              1 ; H 16-bits
     *          i386.TARGET_PAGE_BITS = 12                  2 ; W 32-bits
     *                                                      3 ; D 64-bits
     */
    int align_bits = get_ldst_align_bits(op);
    IR2_OPND cmp_opnd = latxs_arg1_ir2_opnd;

#if 0
    latxs_append_ir2_opnd2_(lisa_mov, &cmp_opnd, mem);
    latxs_append_ir2_opnd2ii(LISA_BSTRINS_D, &cmp_opnd, zero,
            TARGET_PAGE_BITS - 1, align_bits);
#else
    int align_mask = (1 << align_bits) - 1;
    latxs_append_ir2_opnd2i(LISA_ADDI_D, &cmp_opnd, mem, align_mask);
    latxs_append_ir2_opnd2ii(LISA_BSTRINS_D, &cmp_opnd, zero,
            TARGET_PAGE_BITS - 1, 0);
#endif

    /* before branch to common softmmu helper */

    /* 7. compare cmp and tag */
    if (!option_smmu_slow) {
        if (lsenv->tr_data->is_tu_mode) {
            IR2_OPND temp_label = latxs_ir2_opnd_new_label();
            latxs_append_ir2_opnd3(LISA_BEQ,
                                   &cmp_opnd, &tag_opnd, &temp_label);

            if (option_common_slowpath && !pcaddi_cold) {
                IR2_OPND ptr_opnd = latxs_arg3_ir2_opnd;
                latxs_append_ir2_opnd1i(LISA_PCADDI, &ptr_opnd, 0);
                /*
                 * tag_opnd(arg0) = softtlb.addr_read/write
                 * cmp_opnd(arg1) = vaddr & mask
                 * tlb_opnd(arg2) = &softtlb
                 * ptr_opnd(arg3) = here
                 */
            }
            latxs_append_ir2_opnd1(LISA_B, &label_slow_path);
            latxs_append_ir2_opnd1(LISA_LABEL, &temp_label);
        } else {
            if (option_common_slowpath && !pcaddi_cold) {
                IR2_OPND ptr_opnd = latxs_arg3_ir2_opnd;
                latxs_append_ir2_opnd1i(LISA_PCADDI, &ptr_opnd, 0);
                /*
                 * tag_opnd(arg0) = softtlb.addr_read/write
                 * cmp_opnd(arg1) = vaddr & mask
                 * tlb_opnd(arg2) = &softtlb
                 * ptr_opnd(arg3) = here
                 */
            }
            latxs_append_ir2_opnd3(LISA_BNE,
                                   &cmp_opnd, &tag_opnd, &label_slow_path);

        }
    } else {
        /* Always jump to Softmmu Slow Path : mainly for debug */
        if (option_common_slowpath && !pcaddi_cold) {
            IR2_OPND ptr_opnd = latxs_arg3_ir2_opnd;
            latxs_append_ir2_opnd1i(LISA_PCADDI, &ptr_opnd, 0);
        }
        latxs_append_ir2_opnd3(LISA_BEQ,
                zero, zero, &label_slow_path);
    }

    /*
     * option_softmmu_pcaddi_cold: the hot path carries no anchor any more.
     * Place a label right in front of the addend load instead; the per-access
     * cold stub materialises this address into a3 with a label-form pcaddi
     * only when the soft-TLB lookup actually misses.
     *
     * helper_softmmu_common() therefore decodes the following window
     * (p = label_fast_path address, in instructions):
     *     p[0] ld.d   addend
     *     p[1] lu52i  addend, 0x900      (HAMT DMW only)
     *     p[?] ldx/stx access
     * and returns to the instruction after the access (p + 8 non-DMW,
     * p + 12 DMW) so the fast path continues after the access as before.
     *
     * This anchor is inside the generated code of the same guest instruction
     * as the access, which is what cpu_restore_state(retaddr) needs.  It is
     * also positioned after all value-producing work of the lookup, so the
     * pcaddi added by the cold stub cannot disturb any live register: a3 is a
     * scratch/argument register, not an itemp.
     */
    if (pcaddi_cold) {
        *label_fast_path = latxs_ir2_opnd_new_label();
        latxs_append_ir2_opnd1(LISA_LABEL, label_fast_path);
    } else {
        *label_fast_path = latxs_invalid_ir2_opnd;
    }

    /* 7.1 ================= SoftTLB hit ============== */

    /* 8. load addend from tlb entry */
    IR2_OPND add_opnd = latxs_arg1_ir2_opnd;
    latxs_append_ir2_opnd2i(LISA_LD_D, &add_opnd, &tlb_opnd, add_off);

    if (hamt_enabled()) {
        latxs_tr_dmw_in_fix_mem(&add_opnd);
    }
    /* 9. get hvaddr if not branch */
    switch (op) {
    case LISA_LD_B:
        latxs_append_ir2_opnd3(LISA_LDX_B, gpr_opnd, mem, &add_opnd);
        break;
    case LISA_LD_H:
        latxs_append_ir2_opnd3(LISA_LDX_H, gpr_opnd, mem, &add_opnd);
        break;
    case LISA_LD_W:
        latxs_append_ir2_opnd3(LISA_LDX_W, gpr_opnd, mem, &add_opnd);
        break;
    case LISA_LD_D:
        latxs_append_ir2_opnd3(LISA_LDX_D, gpr_opnd, mem, &add_opnd);
        break;
    case LISA_LD_BU:
        latxs_append_ir2_opnd3(LISA_LDX_BU, gpr_opnd, mem, &add_opnd);
        break;
    case LISA_LD_HU:
        latxs_append_ir2_opnd3(LISA_LDX_HU, gpr_opnd, mem, &add_opnd);
        break;
    case LISA_LD_WU:
        latxs_append_ir2_opnd3(LISA_LDX_WU, gpr_opnd, mem, &add_opnd);
        break;
    case LISA_ST_B:
        latxs_append_ir2_opnd3(LISA_STX_B, gpr_opnd, mem, &add_opnd);
        break;
    case LISA_ST_H:
        latxs_append_ir2_opnd3(LISA_STX_H, gpr_opnd, mem, &add_opnd);
        break;
    case LISA_ST_W:
        latxs_append_ir2_opnd3(LISA_STX_W, gpr_opnd, mem, &add_opnd);
        break;
    case LISA_ST_D:
        latxs_append_ir2_opnd3(LISA_STX_D, gpr_opnd, mem, &add_opnd);
        break;
    /* option_fast_fpr_ldst */
    case LISA_VLD:
        latxs_append_ir2_opnd3(LISA_VLDX, gpr_opnd, mem, &add_opnd);
        break;
    case LISA_VST:
        latxs_append_ir2_opnd3(LISA_VSTX, gpr_opnd, mem, &add_opnd);
        break;
    case LISA_FLD_S:
        latxs_append_ir2_opnd3(LISA_FLDX_S, gpr_opnd, mem, &add_opnd);
        break;
    case LISA_FLD_D:
        latxs_append_ir2_opnd3(LISA_FLDX_D, gpr_opnd, mem, &add_opnd);
        break;
    case LISA_FST_S:
        latxs_append_ir2_opnd3(LISA_FSTX_S, gpr_opnd, mem, &add_opnd);
        break;
    case LISA_FST_D:
        latxs_append_ir2_opnd3(LISA_FSTX_D, gpr_opnd, mem, &add_opnd);
        break;
    default:
        lsassertm(0, "wrong in softmmu\n");
        break;
    }
}

static bool tr_gen_hamt_atomic_ldst(
        IR2_OPCODE op,
        IR2_OPND *rd,
        IR2_OPND *gpr_opnd,
        IR2_OPND *mem,
        IR2_OPND *label_atomic_helper)
{
    bool res = false;

    switch (op) {
    case LISA_AMCAS_B:     latxs_append_ir2_opnd3(LISA_AMCAS_B,     rd, mem, gpr_opnd); res = true; break;
    case LISA_AMCAS_H:     latxs_append_ir2_opnd3(LISA_AMCAS_H,     rd, mem, gpr_opnd); res = true; break;
    case LISA_AMCAS_W:     latxs_append_ir2_opnd3(LISA_AMCAS_W,     rd, mem, gpr_opnd); res = true; break;
    case LISA_AMCAS_D:     latxs_append_ir2_opnd3(LISA_AMCAS_D,     rd, mem, gpr_opnd); res = true; break;
    case LISA_AMSWAP_B:    latxs_append_ir2_opnd3(LISA_AMSWAP_B,    rd, mem, gpr_opnd); res = true; break;
    case LISA_AMSWAP_H:    latxs_append_ir2_opnd3(LISA_AMSWAP_H,    rd, mem, gpr_opnd); res = true; break;
    case LISA_AMSWAP_W:    latxs_append_ir2_opnd3(LISA_AMSWAP_W,    rd, mem, gpr_opnd); res = true; break;
    case LISA_AMSWAP_D:    latxs_append_ir2_opnd3(LISA_AMSWAP_D,    rd, mem, gpr_opnd); res = true; break;
    case LISA_AMADD_B:     latxs_append_ir2_opnd3(LISA_AMADD_B,     rd, mem, gpr_opnd); res = true; break;
    case LISA_AMADD_H:     latxs_append_ir2_opnd3(LISA_AMADD_H,     rd, mem, gpr_opnd); res = true; break;
    case LISA_AMADD_W:     latxs_append_ir2_opnd3(LISA_AMADD_W,     rd, mem, gpr_opnd); res = true; break;
    case LISA_AMADD_D:     latxs_append_ir2_opnd3(LISA_AMADD_D,     rd, mem, gpr_opnd); res = true; break;
    case LISA_AMAND_W:     latxs_append_ir2_opnd3(LISA_AMAND_W,     rd, mem, gpr_opnd); res = true; break;
    case LISA_AMAND_D:     latxs_append_ir2_opnd3(LISA_AMAND_D,     rd, mem, gpr_opnd); res = true; break;
    case LISA_AMOR_W:      latxs_append_ir2_opnd3(LISA_AMOR_W,      rd, mem, gpr_opnd); res = true; break;
    case LISA_AMOR_D:      latxs_append_ir2_opnd3(LISA_AMOR_D,      rd, mem, gpr_opnd); res = true; break;
    case LISA_AMXOR_W:     latxs_append_ir2_opnd3(LISA_AMXOR_W,     rd, mem, gpr_opnd); res = true; break;
    case LISA_AMXOR_D:     latxs_append_ir2_opnd3(LISA_AMXOR_D,     rd, mem, gpr_opnd); res = true; break;
    case LISA_AMMAX_W:     latxs_append_ir2_opnd3(LISA_AMMAX_W,     rd, mem, gpr_opnd); res = true; break;
    case LISA_AMMAX_D:     latxs_append_ir2_opnd3(LISA_AMMAX_D,     rd, mem, gpr_opnd); res = true; break;
    case LISA_AMMAX_WU:    latxs_append_ir2_opnd3(LISA_AMMAX_WU,    rd, mem, gpr_opnd); res = true; break;
    case LISA_AMMAX_DU:    latxs_append_ir2_opnd3(LISA_AMMAX_DU,    rd, mem, gpr_opnd); res = true; break;
    case LISA_AMMIN_W:     latxs_append_ir2_opnd3(LISA_AMMIN_W,     rd, mem, gpr_opnd); res = true; break;
    case LISA_AMMIN_D:     latxs_append_ir2_opnd3(LISA_AMMIN_D,     rd, mem, gpr_opnd); res = true; break;
    case LISA_AMMIN_WU:    latxs_append_ir2_opnd3(LISA_AMMIN_WU,    rd, mem, gpr_opnd); res = true; break;
    case LISA_AMMIN_DU:    latxs_append_ir2_opnd3(LISA_AMMIN_DU,    rd, mem, gpr_opnd); res = true; break;
    case LISA_AMCAS_DB_B:  latxs_append_ir2_opnd3(LISA_AMCAS_DB_B,  rd, mem, gpr_opnd); res = true; break;
    case LISA_AMCAS_DB_H:  latxs_append_ir2_opnd3(LISA_AMCAS_DB_H,  rd, mem, gpr_opnd); res = true; break;
    case LISA_AMCAS_DB_W:  latxs_append_ir2_opnd3(LISA_AMCAS_DB_W,  rd, mem, gpr_opnd); res = true; break;
    case LISA_AMCAS_DB_D:  latxs_append_ir2_opnd3(LISA_AMCAS_DB_D,  rd, mem, gpr_opnd); res = true; break;
    case LISA_AMSWAP_DB_B: latxs_append_ir2_opnd3(LISA_AMSWAP_DB_B, rd, mem, gpr_opnd); res = true; break;
    case LISA_AMSWAP_DB_H: latxs_append_ir2_opnd3(LISA_AMSWAP_DB_H, rd, mem, gpr_opnd); res = true; break;
    case LISA_AMSWAP_DB_W: latxs_append_ir2_opnd3(LISA_AMSWAP_DB_W, rd, mem, gpr_opnd); res = true; break;
    case LISA_AMSWAP_DB_D: latxs_append_ir2_opnd3(LISA_AMSWAP_DB_D, rd, mem, gpr_opnd); res = true; break;
    case LISA_AMADD_DB_B:  latxs_append_ir2_opnd3(LISA_AMADD_DB_B,  rd, mem, gpr_opnd); res = true; break;
    case LISA_AMADD_DB_H:  latxs_append_ir2_opnd3(LISA_AMADD_DB_H,  rd, mem, gpr_opnd); res = true; break;
    case LISA_AMADD_DB_W:  latxs_append_ir2_opnd3(LISA_AMADD_DB_W,  rd, mem, gpr_opnd); res = true; break;
    case LISA_AMADD_DB_D:  latxs_append_ir2_opnd3(LISA_AMADD_DB_D,  rd, mem, gpr_opnd); res = true; break;
    case LISA_AMAND_DB_W:  latxs_append_ir2_opnd3(LISA_AMAND_DB_W,  rd, mem, gpr_opnd); res = true; break;
    case LISA_AMAND_DB_D:  latxs_append_ir2_opnd3(LISA_AMAND_DB_D,  rd, mem, gpr_opnd); res = true; break;
    case LISA_AMOR_DB_W:   latxs_append_ir2_opnd3(LISA_AMOR_DB_W,   rd, mem, gpr_opnd); res = true; break;
    case LISA_AMOR_DB_D:   latxs_append_ir2_opnd3(LISA_AMOR_DB_D,   rd, mem, gpr_opnd); res = true; break;
    case LISA_AMXOR_DB_W:  latxs_append_ir2_opnd3(LISA_AMXOR_DB_W,  rd, mem, gpr_opnd); res = true; break;
    case LISA_AMXOR_DB_D:  latxs_append_ir2_opnd3(LISA_AMXOR_DB_D,  rd, mem, gpr_opnd); res = true; break;
    case LISA_AMMAX_DB_W:  latxs_append_ir2_opnd3(LISA_AMMAX_DB_W,  rd, mem, gpr_opnd); res = true; break;
    case LISA_AMMAX_DB_D:  latxs_append_ir2_opnd3(LISA_AMMAX_DB_D,  rd, mem, gpr_opnd); res = true; break;
    case LISA_AMMAX_DB_WU: latxs_append_ir2_opnd3(LISA_AMMAX_DB_WU, rd, mem, gpr_opnd); res = true; break;
    case LISA_AMMAX_DB_DU: latxs_append_ir2_opnd3(LISA_AMMAX_DB_DU, rd, mem, gpr_opnd); res = true; break;
    case LISA_AMMIN_DB_W:  latxs_append_ir2_opnd3(LISA_AMMIN_DB_W,  rd, mem, gpr_opnd); res = true; break;
    case LISA_AMMIN_DB_D:  latxs_append_ir2_opnd3(LISA_AMMIN_DB_D,  rd, mem, gpr_opnd); res = true; break;
    case LISA_AMMIN_DB_WU: latxs_append_ir2_opnd3(LISA_AMMIN_DB_WU, rd, mem, gpr_opnd); res = true; break;
    case LISA_AMMIN_DB_DU: latxs_append_ir2_opnd3(LISA_AMMIN_DB_DU, rd, mem, gpr_opnd); res = true; break;
    default:
        res = false;
        break;
    }

    /*
     *      +----------------------+
     *      | address calculation  |
     *      +----------------------+
     *      | AMADD                | # when hamt success, EPC not changed
     *      +----------------------+ # when hamt fails,   EPC = EPC + 8
     * +--- | B 2                  |                       |
     * |    +----------------------+                       |
     * |    | B atomic_helper      | ---+ <----------------+
     * |    +----------------------+    |
     * +--> | EFLAGS               |    |
     *      +----------------------+    |
     * +--- | B atomic_done        |    |
     * |    +----------------------+    |
     * |    | call atomic helper   | <--+
     * |    +----------------------+
     * +-->
     */

    if (res) {
        IR2_OPND goon_atomic_codes = latxs_ir2_opnd_new_label();
        latxs_append_ir2_opnd1(LISA_B, &goon_atomic_codes);
        latxs_append_ir2_opnd1(LISA_B, label_atomic_helper);
        latxs_append_ir2_opnd1(LISA_LABEL, &goon_atomic_codes);
    }

    return res;
}

/* return label point to slow path */
static void tr_gen_atomic_softmmu(
        IR2_OPCODE op,
        IR2_OPND *rd,
        IR2_OPND *gpr_opnd,
        IR2_OPND *mem,
        int mmu_index,
        bool is_load,
        bool read_write,
        IR2_OPND *label_atomic_helper)
{
    IR2_OPND *zero = &latxs_zero_ir2_opnd;
    IR2_OPND *env = &latxs_env_ir2_opnd;

#define TLB_MASK_TABLE_OFS(IDX) \
    ((int)offsetof(ArchCPU, neg.tlb.f[IDX]) - (int)offsetof(ArchCPU, env))

    /* offset from ENV */
    int fast_off  = TLB_MASK_TABLE_OFS(mmu_index);
    int mask_off  = fast_off + offsetof(CPUTLBDescFast, mask);  /* 64-bit */
    int table_off = latx_tlb_table_off(fast_off); /* DMW-ready */
    /* offset from tlb entry */
    int add_off   = offsetof(CPUTLBEntry, addend); /* 64-bit */
    int tag_off   = (is_load ? offsetof(CPUTLBEntry, addr_read)
                   : offsetof(CPUTLBEntry, addr_write)); /* 32-bit */

    /* 1. load f[mmu].mask */
    IR2_OPND mask_opnd = latxs_arg0_ir2_opnd;
    if (int32_in_int12(mask_off)) {
        latxs_append_ir2_opnd2i(LISA_LD_D, &mask_opnd, env, mask_off);
    } else {
        latxs_load_imm32_to_ir2(&mask_opnd, mask_off, EXMode_S);
        latxs_append_ir2_opnd3(LISA_ADD_D, &mask_opnd, env, &mask_opnd);
        latxs_append_ir2_opnd2i(LISA_LD_D, &mask_opnd, &mask_opnd, 0);
    }

    /* 2. extract tlb index */
    IR2_OPND index_opnd = latxs_arg1_ir2_opnd;
    /* 2.1 index = address >> shift */
    int shift = TARGET_PAGE_BITS - CPU_TLB_ENTRY_BITS;
    latxs_append_ir2_opnd2i(LISA_SRLI_W, &index_opnd, mem, shift);

    /* 2.2 index = index & mask */
    latxs_append_ir2_opnd3(LISA_AND, &index_opnd, &index_opnd, &mask_opnd);

    /* 3. load f[mmu].table */
    IR2_OPND table_opnd = latxs_arg2_ir2_opnd;
    if (int32_in_int12(table_off)) {
        latxs_append_ir2_opnd2i(LISA_LD_D, &table_opnd, env, table_off);
    } else {
        latxs_load_imm32_to_ir2(&table_opnd, table_off, EXMode_S);
        latxs_append_ir2_opnd3(LISA_ADD_D, &table_opnd, env, &table_opnd);
        latxs_append_ir2_opnd2i(LISA_LD_D, &table_opnd, &table_opnd, 0);
    }
    /* 4. tlb entry = table + index */
    latxs_append_ir2_opnd3(LISA_ADD_D, &table_opnd, &table_opnd, &index_opnd);
    IR2_OPND tlb_opnd = table_opnd;

    /* 5. load compare part from tlb entry */
    IR2_OPND tag_opnd = latxs_arg0_ir2_opnd;
    if (TARGET_LONG_BITS == 32) {
        latxs_append_ir2_opnd2i(LISA_LD_WU, &tag_opnd, &tlb_opnd, tag_off);
    } else {
        latxs_append_ir2_opnd2i(LISA_LD_D, &tag_opnd, &tlb_opnd, tag_off);
    }
    /* 6. cmp = vaddr & mask */
    /*
     *                      12 11 8 7  4 3  0
     * vaddr 0x 1111 .... 1111 0000 0000 0000
     *                       ^              \- align_bits = 0 ; B  8-bits
     *                       |                              1 ; H 16-bits
     *          i386.TARGET_PAGE_BITS = 12                  2 ; W 32-bits
     *                                                      3 ; D 64-bits
     */
    IR2_OPND cmp_opnd = latxs_arg1_ir2_opnd;

#if 0
    latxs_append_ir2_opnd2_(lisa_mov, &cmp_opnd, mem);
    latxs_append_ir2_opnd2ii(LISA_BSTRINS_D, &cmp_opnd, zero,
            TARGET_PAGE_BITS - 1, align_bits);
#else
    latxs_append_ir2_opnd2i(LISA_ADDI_D, &cmp_opnd, mem, 0);
    latxs_append_ir2_opnd2ii(LISA_BSTRINS_D, &cmp_opnd, zero,
            TARGET_PAGE_BITS - 1, 0);
#endif

    /* 7. compare cmp and tag */
    if (!option_smmu_slow) {
        latxs_append_ir2_opnd3(LISA_BNE,
                &cmp_opnd, &tag_opnd, label_atomic_helper);
    } else {
        /* Always jump to Softmmu Slow Path : mainly for debug */
        latxs_append_ir2_opnd3(LISA_BEQ,
                zero, zero, label_atomic_helper);
    }

    /* 7.1 ================= SoftTLB hit ============== */

    if (read_write) {
        /* Match atomic_mmu_lookup: a writable page may still have a read
         * watchpoint or lack read permission. All slow tags use the helper. */
        latxs_append_ir2_opnd2i(LISA_LD_D, &tag_opnd, &tlb_opnd,
                                offsetof(CPUTLBEntry, addr_read));
        latxs_append_ir2_opnd3(LISA_BNE, &cmp_opnd, &tag_opnd,
                                label_atomic_helper);
    }

    /* 8. load addend from tlb entry */
    IR2_OPND add_opnd = latxs_arg1_ir2_opnd;
    latxs_append_ir2_opnd2i(LISA_LD_D, &add_opnd, &tlb_opnd, add_off);

    if (hamt_enabled()) {
        latxs_tr_dmw_in_fix_mem(&add_opnd);
    }

    latxs_append_ir2_opnd3(LISA_ADD_D, &add_opnd, &add_opnd, mem);
    /* 9. get hvaddr if not branch */
    switch (op) {
    case LISA_LD_D:
        /* only called from cmpxchg16b ld load high 64bit of 128bit */
        latxs_append_ir2_opnd2i(LISA_LD_D, rd, &add_opnd, 8);
        break;
    case LISA_LL_D:
        latxs_append_ir2_opnd2i(LISA_LL_D, rd, &add_opnd, 0);
        break;
    case LISA_SC_Q:
        latxs_append_ir2_opnd3(LISA_SC_Q, rd, &add_opnd, gpr_opnd);
        break;
    case LISA_AMCAS_B:
        latxs_append_ir2_opnd3(LISA_AMCAS_B, rd, &add_opnd, gpr_opnd);
        break;
    case LISA_AMCAS_H:
        latxs_append_ir2_opnd3(LISA_AMCAS_H, rd, &add_opnd, gpr_opnd);
        break;
    case LISA_AMCAS_W:
        latxs_append_ir2_opnd3(LISA_AMCAS_W, rd, &add_opnd, gpr_opnd);
        break;
    case LISA_AMCAS_D:
        latxs_append_ir2_opnd3(LISA_AMCAS_D, rd, &add_opnd, gpr_opnd);
        break;
    case LISA_AMSWAP_B:
        latxs_append_ir2_opnd3(LISA_AMSWAP_B, rd, &add_opnd, gpr_opnd);
        break;
    case LISA_AMSWAP_H:
        latxs_append_ir2_opnd3(LISA_AMSWAP_H, rd, &add_opnd, gpr_opnd);
        break;
    case LISA_AMSWAP_W:
        latxs_append_ir2_opnd3(LISA_AMSWAP_W, rd, &add_opnd, gpr_opnd);
        break;
    case LISA_AMSWAP_D:
        latxs_append_ir2_opnd3(LISA_AMSWAP_D, rd, &add_opnd, gpr_opnd);
        break;
    case LISA_AMADD_B:
        latxs_append_ir2_opnd3(LISA_AMADD_B, rd, &add_opnd, gpr_opnd);
        break;
    case LISA_AMADD_H:
        latxs_append_ir2_opnd3(LISA_AMADD_H, rd, &add_opnd, gpr_opnd);
        break;
    case LISA_AMADD_W:
        latxs_append_ir2_opnd3(LISA_AMADD_W, rd, &add_opnd, gpr_opnd);
        break;
    case LISA_AMADD_D:
        latxs_append_ir2_opnd3(LISA_AMADD_D, rd, &add_opnd, gpr_opnd);
        break;
    case LISA_AMAND_W:
        latxs_append_ir2_opnd3(LISA_AMAND_W, rd, &add_opnd, gpr_opnd);
        break;
    case LISA_AMAND_D:
        latxs_append_ir2_opnd3(LISA_AMAND_D, rd, &add_opnd, gpr_opnd);
        break;
    case LISA_AMOR_W:
        latxs_append_ir2_opnd3(LISA_AMOR_W, rd, &add_opnd, gpr_opnd);
        break;
    case LISA_AMOR_D:
        latxs_append_ir2_opnd3(LISA_AMOR_D, rd, &add_opnd, gpr_opnd);
        break;
    case LISA_AMXOR_W:
        latxs_append_ir2_opnd3(LISA_AMXOR_W, rd, &add_opnd, gpr_opnd);
        break;
    case LISA_AMXOR_D:
        latxs_append_ir2_opnd3(LISA_AMXOR_D, rd, &add_opnd, gpr_opnd);
        break;
    case LISA_AMMAX_W:
        latxs_append_ir2_opnd3(LISA_AMMAX_W, rd, &add_opnd, gpr_opnd);
        break;
    case LISA_AMMAX_D:
        latxs_append_ir2_opnd3(LISA_AMMAX_D, rd, &add_opnd, gpr_opnd);
        break;
    case LISA_AMMIN_W:
        latxs_append_ir2_opnd3(LISA_AMMIN_W, rd, &add_opnd, gpr_opnd);
        break;
    case LISA_AMMIN_D:
        latxs_append_ir2_opnd3(LISA_AMMIN_D, rd, &add_opnd, gpr_opnd);
        break;
    case LISA_AMMAX_WU:
        latxs_append_ir2_opnd3(LISA_AMMAX_WU, rd, &add_opnd, gpr_opnd);
        break;
    case LISA_AMMAX_DU:
        latxs_append_ir2_opnd3(LISA_AMMAX_DU, rd, &add_opnd, gpr_opnd);
        break;
    case LISA_AMMIN_WU:
        latxs_append_ir2_opnd3(LISA_AMMIN_WU, rd, &add_opnd, gpr_opnd);
        break;
    case LISA_AMMIN_DU:
        latxs_append_ir2_opnd3(LISA_AMMIN_DU, rd, &add_opnd, gpr_opnd);
        break;
    case LISA_AMCAS_DB_B:
        latxs_append_ir2_opnd3(LISA_AMCAS_DB_B, rd, &add_opnd, gpr_opnd);
        break;
    case LISA_AMCAS_DB_H:
        latxs_append_ir2_opnd3(LISA_AMCAS_DB_H, rd, &add_opnd, gpr_opnd);
        break;
    case LISA_AMCAS_DB_W:
        latxs_append_ir2_opnd3(LISA_AMCAS_DB_W, rd, &add_opnd, gpr_opnd);
        break;
    case LISA_AMCAS_DB_D:
        latxs_append_ir2_opnd3(LISA_AMCAS_DB_D, rd, &add_opnd, gpr_opnd);
        break;
    case LISA_AMSWAP_DB_B:
        latxs_append_ir2_opnd3(LISA_AMSWAP_DB_B, rd, &add_opnd, gpr_opnd);
        break;
    case LISA_AMSWAP_DB_H:
        latxs_append_ir2_opnd3(LISA_AMSWAP_DB_H, rd, &add_opnd, gpr_opnd);
        break;
    case LISA_AMSWAP_DB_W:
        latxs_append_ir2_opnd3(LISA_AMSWAP_DB_W, rd, &add_opnd, gpr_opnd);
        break;
    case LISA_AMSWAP_DB_D:
        latxs_append_ir2_opnd3(LISA_AMSWAP_DB_D, rd, &add_opnd, gpr_opnd);
        break;
    case LISA_AMADD_DB_B:
        latxs_append_ir2_opnd3(LISA_AMADD_DB_B, rd, &add_opnd, gpr_opnd);
        break;
    case LISA_AMADD_DB_H:
        latxs_append_ir2_opnd3(LISA_AMADD_DB_H, rd, &add_opnd, gpr_opnd);
        break;
    case LISA_AMADD_DB_W:
        latxs_append_ir2_opnd3(LISA_AMADD_DB_W, rd, &add_opnd, gpr_opnd);
        break;
    case LISA_AMADD_DB_D:
        latxs_append_ir2_opnd3(LISA_AMADD_DB_D, rd, &add_opnd, gpr_opnd);
        break;
    case LISA_AMAND_DB_W:
        latxs_append_ir2_opnd3(LISA_AMAND_DB_W, rd, &add_opnd, gpr_opnd);
        break;
    case LISA_AMAND_DB_D:
        latxs_append_ir2_opnd3(LISA_AMAND_DB_D, rd, &add_opnd, gpr_opnd);
        break;
    case LISA_AMOR_DB_W:
        latxs_append_ir2_opnd3(LISA_AMOR_DB_W, rd, &add_opnd, gpr_opnd);
        break;
    case LISA_AMOR_DB_D:
        latxs_append_ir2_opnd3(LISA_AMOR_DB_D, rd, &add_opnd, gpr_opnd);
        break;
    case LISA_AMXOR_DB_W:
        latxs_append_ir2_opnd3(LISA_AMXOR_DB_W, rd, &add_opnd, gpr_opnd);
        break;
    case LISA_AMXOR_DB_D:
        latxs_append_ir2_opnd3(LISA_AMXOR_DB_D, rd, &add_opnd, gpr_opnd);
        break;
    case LISA_AMMAX_DB_W:
        latxs_append_ir2_opnd3(LISA_AMMAX_DB_W, rd, &add_opnd, gpr_opnd);
        break;
    case LISA_AMMAX_DB_D:
        latxs_append_ir2_opnd3(LISA_AMMAX_DB_D, rd, &add_opnd, gpr_opnd);
        break;
    case LISA_AMMIN_DB_W:
        latxs_append_ir2_opnd3(LISA_AMMIN_DB_W, rd, &add_opnd, gpr_opnd);
        break;
    case LISA_AMMIN_DB_D:
        latxs_append_ir2_opnd3(LISA_AMMIN_DB_D, rd, &add_opnd, gpr_opnd);
        break;
    case LISA_AMMAX_DB_WU:
        latxs_append_ir2_opnd3(LISA_AMMAX_DB_WU, rd, &add_opnd, gpr_opnd);
        break;
    case LISA_AMMAX_DB_DU:
        latxs_append_ir2_opnd3(LISA_AMMAX_DB_DU, rd, &add_opnd, gpr_opnd);
        break;
    case LISA_AMMIN_DB_WU:
        latxs_append_ir2_opnd3(LISA_AMMIN_DB_WU, rd, &add_opnd, gpr_opnd);
        break;
    case LISA_AMMIN_DB_DU:
        latxs_append_ir2_opnd3(LISA_AMMIN_DB_DU, rd, &add_opnd, gpr_opnd);
        break;

    default:
        lsassertm(0, "wrong in softmmu\n");
        break;
    }
}

void tr_gen_atomic_lookup_qemu_tlb(IR2_OPCODE op, IR2_OPND *rd,
        IR2_OPND *value, IR2_OPND *mem, int mmu_index, bool is_load,
        IR2_OPND *slow)
{
    if (can_do_hamt(lsenv->cpu_state) &&
        tr_gen_hamt_atomic_ldst(op, rd, value, mem, slow)) {
        return;
    }
    tr_gen_atomic_softmmu(op, rd, value, mem, mmu_index, is_load, false, slow);
}

/* Static LLVM entries have no source TB or generation-time guest MMU state.
 * Use an explicit user MMU index and the same read/write tags as C atomics. */
void tr_gen_atomic_rmw_softmmu(IR2_OPCODE op, IR2_OPND *rd,
        IR2_OPND *value, IR2_OPND *mem, int mmu_index, IR2_OPND *slow)
{
    tr_gen_atomic_softmmu(op, rd, value, mem, mmu_index, false, true, slow);
}

static void td_rcd_softmmu_slow_path(
        IR2_OPCODE op,
        IR2_OPND *gpr_ir2_opnd,
        IR2_OPND *mem_ir2_opnd,
        IR2_OPND *label_slow_path,
        IR2_OPND *label_exit,
        IR2_OPND *label_fast_path,
        int mmu_index,
        int save_tmp,
        int is_load)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    TranslationBlock *tb = td->curr_tb;

    int sp_nr  = td->slow_path_rcd_nr;
    int sp_max = td->slow_path_rcd_max;

    /* get Slow Path Record entry */

    softmmu_sp_rcd_t *sp = NULL;

    if (sp_nr < 0 || sp_max < 0) {
        g_assert_not_reached();
        return;
    }

    if (sp_nr >= sp_max) {
        sp_max <<= 1;
        sp = mm_realloc(td->slow_path_rcd, sp_max * sizeof(*sp));
        if (unlikely(sp == NULL)) {
            g_assert_not_reached();
            return;
        }
        td->slow_path_rcd = sp;
        td->slow_path_rcd_max = sp_max;
    }

    sp = &((softmmu_sp_rcd_t *)td->slow_path_rcd)[sp_nr];

    td->slow_path_rcd_nr += 1;

    /* save information about slow path */
    sp->op = op;
    sp->gpr_ir2_opnd = *gpr_ir2_opnd;
    sp->mem_ir2_opnd = *mem_ir2_opnd;
    sp->label_slow_path = *label_slow_path;
    sp->label_exit = *label_exit;
    sp->label_fast_path = *label_fast_path;
    sp->rmw_valid = latxs_invalid_ir2_opnd;
    sp->mmu_index = mmu_index;
    sp->tmp_need_save = save_tmp || td->ptn_itemp_mask;
    sp->is_load = is_load;

    sp->retaddr = (ADDR)(tb->tc.ptr) +
                  (ADDR)((td->ir2_asm_nr - 2) << 2);

    sp->itmp_mask = td->itemp_mask | td->ptn_itemp_mask;
    sp->ftmp_mask = td->ftemp_mask;
}

static void tr_gen_ldst_slow_path(
        IR2_OPCODE op,
        IR2_OPND  *gpr_opnd, /* temp(t0-t7) or mapping(s1-s8) */
        IR2_OPND  *mem_opnd,
        IR2_OPND  *label_slow_path,
        IR2_OPND  *label_exit,
        IR2_OPND  *label_fast_path,
        int        mmu_index,
        int        is_load,
        int        save_temp)
{
    td_rcd_softmmu_slow_path(
            op, gpr_opnd, mem_opnd,
            label_slow_path, label_exit, label_fast_path,
            mmu_index, save_temp, is_load);
}


/* option_fast_fpr_ldst : opnd_gpr can be fpr */
static void __gen_ldst_softmmu_helper_native(
        IR2_OPCODE op,
        IR2_OPND *opnd_gpr,
        IR2_OPND *mem_base,
        int mem_offset,
        int save_temp)
{
    TRANSLATION_DATA *td = lsenv->tr_data;

    int mmu_index = td->sys.mem_index;
    bool is_load  = latxs_ir2_opcode_is_load(op);

    IR2_OPND mem = *mem_base;
    int mem_is_new_temp = 0;

    /* 0. format the mem opnd */
    if (mem_offset != 0) {
        mem = latxs_ra_alloc_itemp();
        mem_is_new_temp = 1;
        latxs_append_ir2_opnd2i(LISA_ADDI_D, &mem, mem_base, mem_offset);
    }
    /* TODO: addr_size */
    if (!lsenv->tr_data->sys.code64) {
        latxs_append_ir2_opnd2_(lisa_mov32z, &mem, &mem);
    }

    /*
     * 1. lookup QEMU TLB in native code
     *    and finish memory access if TLB hit
     *    temp register is free to use inside.
     */
    IR2_OPND label_slow_path = latxs_ir2_opnd_new_label();
    IR2_OPND label_fast_path = latxs_invalid_ir2_opnd;
    tr_gen_lookup_qemu_tlb(op, opnd_gpr, &mem, mmu_index,
                           is_load, label_slow_path, &label_fast_path);

    /* 2. memory access finish. jump slow path. */
    IR2_OPND label_exit = latxs_ir2_opnd_new_label();

    /*
     * 3. slow path : call QEMU's helper
     *    Here we just record the data to generate slow path
     *    The real slow path will be generated at the end of TB
     */
    tr_gen_ldst_slow_path(op, opnd_gpr, &mem,
            &label_slow_path, &label_exit, &label_fast_path,
            mmu_index, is_load, save_temp);

    /* 4. exit from fast path or return from slow path */
    latxs_append_ir2_opnd1(LISA_LABEL, &label_exit);

    if (mem_is_new_temp) {
        latxs_ra_free_temp(&mem);
    }
}

/* generate call to each different softmmu helper for each load/store */
static void __tr_gen_softmmu_sp_rcd(softmmu_sp_rcd_t *sp)
{
    IR2_OPND *zero = &latxs_zero_ir2_opnd;
    IR2_OPND *env = &latxs_env_ir2_opnd;

    IR2_OPND *arg0 = &latxs_arg0_ir2_opnd;
    IR2_OPND *arg1 = &latxs_arg1_ir2_opnd;
    IR2_OPND *arg2 = &latxs_arg2_ir2_opnd;
    IR2_OPND *arg3 = &latxs_arg3_ir2_opnd;
    /* latxs_arg4_ir2_opnd is itemp in x86_64, please be careful */
    IR2_OPND *arg4 = &latxs_arg4_ir2_opnd;

    latxs_append_ir2_opnd1(LISA_LABEL, &sp->label_slow_path);

    TRANSLATION_DATA *td = lsenv->tr_data;
    int itmp_mask_bak = td->itemp_mask;
    int ftmp_mask_bak = td->ftemp_mask;
    td->itemp_mask = sp->itmp_mask;
    td->ftemp_mask = sp->ftmp_mask;

    if (sp->tmp_need_save) {
        latxs_tr_save_temp_register_mask(sp->itmp_mask);
    }

    /* option_fast_fpr_ldst : mov fpr to ftemp, avoid top mode */
    IR2_OPND ftemp = latxs_zero_ir2_opnd;
    if (sp->op == LISA_FST_S || sp->op == LISA_FST_D) {
        ftemp = latxs_ra_alloc_ftemp();
        latxs_append_ir2_opnd2(LISA_MOVFR2GR_D, &latxs_ra_ir2_opnd,
                &sp->gpr_ir2_opnd);
        latxs_append_ir2_opnd2(LISA_MOVGR2FR_D, &ftemp,
                &latxs_ra_ir2_opnd);
    }

    /* 1. save native context */
    td->force_curr_top_save_bak = td->force_curr_top_save;
    td->force_curr_top_save = 1;
    latxs_tr_gen_call_to_helper_prologue_cfg(nofp_helper_cfg);
    td->force_curr_top_save = td->force_curr_top_save_bak;

    /* 2. prepare arguments for softmmu helper */

    /*
     * build arguemnts for the helper
     *               LOAD     |  STORE
     * ---------------------------------------
     * helper arg0 : env      |  env
     * helper arg1 : x86vaddr |  x86vaddr
     * helper arg2 : memop    |  data
     * helper arg3 : retaddr  |  memop
     * helper arg4 :          |  retaddr
     */

    IR2_OPND mem = sp->mem_ir2_opnd;
    MemOp memop = convert_to_tcgmemop(sp->op);
    MemOpIdx memopidx = (memop << 4) | sp->mmu_index;

    /* 3.1 arg1: mem address */
    /* latxs_append_ir2_opnd2_(lisa_mov, arg1, &mem); */
    /* TODO: addr_size */
    if (lsenv->tr_data->sys.code64) {
        latxs_append_ir2_opnd2_(lisa_mov, arg1, &mem);
    } else {
        latxs_append_ir2_opnd2_(lisa_mov32z, arg1, &mem);
    }

    /* 3.2 arg0 : env */
    latxs_append_ir2_opnd2_(lisa_mov, arg0, env);
    /* 3.3 arg2 : memop(LOAD) data(STORE) */
    if (sp->is_load) {
        latxs_append_ir2_opnd2i(LISA_ORI, arg2, zero, memopidx);
    } else {
        if (sp->op == LISA_VST) {
            latxs_append_ir2_opnd2i(LISA_VST, &sp->gpr_ir2_opnd,
                                    &latxs_env_ir2_opnd,
                                    offsetof(CPUX86State, temp_xmm));
        } else if (sp->op == LISA_FST_S) {
            /* option_fast_fpr_ldst : data mov to ftemp before prologue */
            latxs_append_ir2_opnd2(LISA_MOVFR2GR_S, arg2, &ftemp);
        } else if (sp->op == LISA_FST_D) {
            latxs_append_ir2_opnd2(LISA_MOVFR2GR_D, arg2, &ftemp);
        } else {
            latxs_append_ir2_opnd2_(lisa_mov, arg2, &sp->gpr_ir2_opnd);
        }
    }
    /* 3.4 arg3 : retaddr(LOAD) memop(STORE) */
    if (sp->is_load) {
        latxs_load_imm64_to_ir2(arg3, sp->retaddr);
    } else {
        latxs_append_ir2_opnd2i(LISA_ORI, arg3, zero, memopidx);
        /* 3.5 arg4 : retaddr(STORE) */
        latxs_load_imm64_to_ir2(arg4, sp->retaddr);
    }

    /* 4. call the helper */
    switch (sp->op) {
    case LISA_ST_B:
        latxs_tr_gen_call_to_helper((ADDR)helper_stb_mmu, HELPER_STB_MMU);
        break;
    case LISA_ST_H:
        latxs_tr_gen_call_to_helper((ADDR)helper_stw_mmu, HELPER_STW_MMU);
        break;
    case LISA_FST_S:
    case LISA_ST_W:
        latxs_tr_gen_call_to_helper((ADDR)helper_stl_mmu, HELPER_STL_MMU);
        break;
    case LISA_FST_D:
    case LISA_ST_D:
        latxs_tr_gen_call_to_helper((ADDR)helper_stq_mmu, HELPER_STQ_MMU);
        break;
    case LISA_LD_B:
        latxs_tr_gen_call_to_helper((ADDR)helper_ldsb_mmu, HELPER_LDSB_MMU);
        break;
    case LISA_LD_BU:
        latxs_tr_gen_call_to_helper((ADDR)helper_ldub_mmu, HELPER_LDUB_MMU);
        break;
    case LISA_LD_H:
        latxs_tr_gen_call_to_helper((ADDR)helper_ldsw_mmu, HELPER_LDSW_MMU);
        break;
    case LISA_LD_HU:
        latxs_tr_gen_call_to_helper((ADDR)helper_lduw_mmu, HELPER_LDUW_MMU);
        break;
    case LISA_LD_W:
        latxs_tr_gen_call_to_helper((ADDR)helper_ldsl_mmu, HELPER_LDSL_MMU);
        break;
    case LISA_FLD_S:
    case LISA_LD_WU:
        latxs_tr_gen_call_to_helper((ADDR)helper_ldul_mmu, HELPER_LDUL_MMU);
        break;
    case LISA_FLD_D:
    case LISA_LD_D:
        latxs_tr_gen_call_to_helper((ADDR)helper_ldq_mmu, HELPER_LDQ_MMU);
        break;
    case LISA_VLD:
        latxs_tr_gen_call_to_helper((ADDR)latxs_helper_lddq_mmu, LATXS_HELPER_LDDQ_MMU);
        break;
    case LISA_VST:
        latxs_tr_gen_call_to_helper((ADDR)latxs_helper_stdq_mmu, LATXS_HELPER_STDQ_MMU);
        break;
    default:
        lsassert(0);
        break;
    }

    /* 6. restore context */
    latxs_tr_gen_call_to_helper_epilogue_cfg(nofp_helper_cfg);

    if (sp->tmp_need_save) {
        latxs_tr_restore_temp_register_mask(sp->itmp_mask);
    }

    if (sp->is_load) {
        if (sp->op == LISA_VLD) {
            latxs_append_ir2_opnd2i(LISA_VLD, &sp->gpr_ir2_opnd,
                                    &latxs_env_ir2_opnd,
                                    offsetof(CPUX86State, temp_xmm));
        } else if (sp->op == LISA_FLD_S) {
            latxs_append_ir2_opnd2(LISA_MOVGR2FR_W, &sp->gpr_ir2_opnd,
                                   &latxs_ret0_ir2_opnd);
        } else if (sp->op == LISA_FLD_D) {
            latxs_append_ir2_opnd2(LISA_MOVGR2FR_D, &sp->gpr_ir2_opnd,
                                   &latxs_ret0_ir2_opnd);
        } else {
            latxs_append_ir2_opnd2_(lisa_mov, &sp->gpr_ir2_opnd,
                                    &latxs_ret0_ir2_opnd);
        }
    }

    td->itemp_mask = itmp_mask_bak;
    td->ftemp_mask = ftmp_mask_bak;

    latxs_append_ir2_opnd1(LISA_B, &sp->label_exit);

    /* option_fast_fpr_ldst : free ftemp */
    if (sp->op == LISA_FST_S || sp->op == LISA_FST_D) {
        latxs_ra_free_temp(&ftemp);
    }
}

uint64_t helper_softmmu_common(CPUX86State *env)
{
    int mmu_index = cpu_mmu_index(env, false);

    __attribute__((__unused__)) uint64_t tag_opnd = env->save_regs[4]; /* a0 */
    __attribute__((__unused__)) uint64_t cmp_opnd = env->save_regs[5]; /* a1 */
    //uint64_t tlb_opnd = env->save_regs[6]; /* a2 */
    uint64_t ptr_opnd = env->save_regs[7]; /* a3 */

    /* The forced-slow debug mode enters here even for a matching tag. */
    lsassert(option_smmu_slow || tag_opnd != cmp_opnd);

    /*
     * Cold anchor layout (option_softmmu_pcaddi_cold):
     *
     *   bne      a1, a0, go_to_call_this_helper -----+
     +-> label_fast_path: <-------------------- ptr_opnd(a3)
     |   ld.d     a1, a2, offsetof(tlb, addon)       |
     |   [lu52i.d a1, a1, 0x900]     (HAMT DMW only) |
     |   ldx/stx  reg, mem, a1                       |
     |   ... <--- return ptr_opnd(a3) + 8 / 12 ------+
     |   ...
     |   go_to_call_this_helper: <-------------------+
     |       pcaddi a3, label_fast_path   (cold stub)
     |       b common_body
     |   common_body:
     |       save a0/a1/a2/a3 into env->save_regs
     |       save itemps      into env->save_regs
     |       helper prologue (save mapping registers)
     |       a0 = env
     |       call helper_softmmu_common
     |       helper epilogue (restore mapping registers)
     |       restore itemps   from env->save_regs
     +--     b v0(a0)
     *
     * Legacy layout (option off, and any AOT code generated with it):
     *
     *   pcaddi   a3, 0 <------------------------ ptr_opnd(a3)
     *   bne      a1, a0, go_to_call_this_helper
     *   ld.d     a1, a2, offsetof(tlb, addon)
     *   [lu52i.d a1, a1, 0x900]        (HAMT DMW only)
     *   ldx/stx  reg, mem, a1
     *   ... <----- return ptr_opnd(a3) + 16 / 20
     *
     * The two layouts are told apart from the major opcode of p[0]:
     * 0x6 = pcaddi (legacy anchor), 0xA = ld.d addend load (cold anchor).
     * Keeping both decodable means an AOT image built with the option off (or
     * on) still runs with either run-time setting; the option only controls
     * what the translator emits, the shared helper understands both.
     */
    uint32_t *p = (void*)ptr_opnd;
    p = (void *)DMW_OUT(p);

    bool dmw = hamt_enabled() && hamt_mode_dmw();
    uint32_t inst_ldst;
    int ret_off;
    if ((p[0] >> 26) == 0x6) {
        /* legacy hot-path anchor: p[0] is "pcaddi a3, 0" */
        inst_ldst = dmw ? p[4] : p[3];
        ret_off   = dmw ? 20 : 16;
    } else if (unlikely((p[0] >> 26) != 0xA)) {
        /*
         * Fail closed: the cold-path anchor must point at the "ld.d addend"
         * word.  lsassertm() is compiled out in production, and decoding a
         * foreign window would select a bogus access instruction and perform
         * it, so make the contract violation fatal and diagnosable instead.
         */
        fprintf(stderr, "%s: bad softmmu anchor %#x at %p\n",
                __func__, p[0], p);
        abort();
    } else {
        /* cold-path anchor: p[0] is the addend load */
        inst_ldst = dmw ? p[2] : p[1];
        ret_off   = dmw ? 12 : 8;
    }

    int rd = (inst_ldst >>  0) & 0x1f; /* load dest or store source val */
    int rj = (inst_ldst >>  5) & 0x1f; /* vaddr */
    __attribute__((__unused__)) int rk = (inst_ldst >> 10) & 0x1f; /* addon */
    lsassert(rk == 5);

#define IS_MAPPING_R9_R15(r)    (12 <= (r) && (r) <= 19)
#define TO_R9_R15(r)            ((r) - 12 + 8)
#define IS_MAPPING_RAX_RDI(r)   (24 <= (r) && (r) <= 31)
#define TO_RAX_RDI(r)           ((r) - 24)
#define IS_ITEMP_ASSERT(r) do {                                 \
    IR2_OPND ir2_rj = latxs_ir2_opnd_new(IR2_OPND_GPR, (r));    \
    lsassertm(latxs_ir2_opnd_is_reg_temp(&ir2_rj),              \
            "%s inst_ldst %x ptr %p", __func__, inst_ldst, p);  \
    (void)ir2_rj;                                               \
} while (0)
#define IS_ITEMP_ASSERT_ZERO(r) do {                            \
    IR2_OPND ir2_rj = latxs_ir2_opnd_new(IR2_OPND_GPR, (r));    \
    lsassertm(latxs_ir2_opnd_is_reg_temp(&ir2_rj) || (r) == 0,  \
            "%s inst_ldst %x ptr %p", __func__, inst_ldst, p);  \
    (void)ir2_rj;                                               \
} while (0)
#define IS_MAPPING_FP(r)    ( 0 <= (r) && (r) <= 7)
#define IS_TEMP_FP(r)       ( 9 <= (r) && (r) <= 15)
#define IS_MAPPING_VEC(r)   (16 <= (r) && (r) <= 31)

#define READ_REG(v, r) do {                 \
    if (IS_MAPPING_R9_R15((r))) {           \
        v = env->regs[TO_R9_R15((r))];      \
    } else if (IS_MAPPING_RAX_RDI((r))) {   \
        v = env->regs[TO_RAX_RDI((r))];     \
    } else {                                \
        IS_ITEMP_ASSERT_ZERO((r));          \
        v = env->save_regs[(r)];            \
    }                                       \
} while (0)

#define WRITE_REG(v, r) do {                \
    if (IS_MAPPING_R9_R15((r))) {           \
        env->regs[TO_R9_R15((r))] = (v);    \
    } else if (IS_MAPPING_RAX_RDI((r))) {   \
        env->regs[TO_RAX_RDI((r))] = (v);   \
    } else {                                \
        IS_ITEMP_ASSERT((r));               \
        env->save_regs[(r)] = (v);          \
    }                                       \
} while (0)

#ifdef CONFIG_LATX_OPT_NOFP

#define READ_FREG_32(v, r) do {                             \
    if (IS_MAPPING_FP((r))) {                               \
        lsassert(option_lsfpu);                             \
        r = ((r) + env->fpstt) & 0x7;                       \
		v =  __movfr2gr32_native_freg_temp((r));            \
    } else if (IS_MAPPING_VEC((r))) {                       \
        v = __movfr2gr32_native_freg_temp((r));             \
    } else if (IS_TEMP_FP((r))) {                           \
        v = __movfr2gr32_native_freg_temp((r));             \
    } else {                                                \
        lsassert(0);                                        \
    }                                                       \
} while (0)

#define READ_FREG_64(v, r) do {                             \
    if (IS_MAPPING_FP((r))) {                               \
        lsassert(option_lsfpu);                             \
		v =  __movfr2gr64_native_freg_temp((r));            \
    } else if (IS_MAPPING_VEC((r))) {                       \
        v = __movfr2gr64_native_freg_temp((r));             \
    } else if (IS_TEMP_FP((r))) {                           \
        v = __movfr2gr64_native_freg_temp((r));             \
    } else {                                                \
        lsassert(0);                                        \
    }                                                       \
} while (0)

#define WRITE_FREG_32(v, r) do {                                    \
    if (IS_MAPPING_FP((r))) {                                       \
        lsassert(option_lsfpu);                                     \
		*((uint32_t*)&env->fpregs[(r)].d.low) = (v) & 0xffffffff;   \
        __movgr2fr32_native_freg_temp(v, r);                        \
    } else if (IS_MAPPING_VEC((r))) {                               \
	    env->xmm_regs[r - 16]._q_ZMMReg[0] = (v) & 0xffffffff;      \
        __movgr2fr32_native_freg_temp(v, r);                        \
    } else if (IS_TEMP_FP((r))) {                                   \
        __movgr2fr32_native_freg_temp(v, r);                        \
    } else {                                                        \
        lsassert(0);                                                \
    }                                                               \
} while (0)

#define WRITE_FREG_64(v, r) do {                                    \
    if (IS_MAPPING_FP((r))) {                                       \
        lsassert(option_lsfpu);                                     \
		*((uint32_t*)&env->fpregs[(r)].d.low) = (v);                \
        __movgr2fr64_native_freg_temp((v), r);                      \
    } else if (IS_MAPPING_VEC(r)) {                                 \
	    env->xmm_regs[r - 16]._q_ZMMReg[0] = (v);                   \
        __movgr2fr64_native_freg_temp((v), r);                      \
    } else if (IS_TEMP_FP(r)) {                                     \
        __movgr2fr64_native_freg_temp((v), r);                      \
    } else {                                                        \
        lsassert(0);                                                \
    }                                                               \
} while (0)

#define READ_VREG_128(ptr, r) do {                      \
    uint64_t *p = (void *)(ptr);                        \
    if (IS_MAPPING_VEC(r)) {                                 \
        __vst128_native_vreg_temp(p, (r));              \
    } else if (IS_TEMP_FP(r)) {                                     \
        __vst128_native_vreg_temp(p, (r));              \
    } else {                                                        \
        lsassert(0);                                                \
    }                                                               \
} while (0)

#define WRITE_VREG_128(ptr, r) do {                     \
    uint64_t *p = (void *)(ptr);                        \
    if (IS_MAPPING_VEC(r)) {                                 \
        env->xmm_regs[(r) - 16]._q_ZMMReg[0] = p[0];    \
        env->xmm_regs[(r) - 16]._q_ZMMReg[1] = p[1];    \
        __vld128_native_vreg_temp(p, (r));              \
    } else if (IS_TEMP_FP(r)) {                                     \
        __vld128_native_vreg_temp(p, (r));              \
    } else {                                                        \
        lsassert(0);                                                \
    }                                                               \
} while (0)
#else

#define READ_FREG_32(v, r) do {                             \
    if (IS_MAPPING_FP((r))) {                               \
        lsassert(option_lsfpu);                             \
        r = ((r) + env->fpstt) & 0x7;                       \
        v = env->fpregs[(r)].d.low & (uint64_t)0xffffffff;  \
    } else if (IS_MAPPING_VEC((r))) {                       \
        v = env->xmm_regs[(r) - 16]._l_ZMMReg[0];           \
    } else if (IS_TEMP_FP((r))) {                           \
        v = __movfr2gr32_native_freg_temp((r));             \
    } else {                                                \
        lsassert(0);                                        \
    }                                                       \
} while (0)

#define READ_FREG_64(v, r) do {                             \
    if (IS_MAPPING_FP((r))) {                               \
        lsassert(option_lsfpu);                             \
        r = ((r) + env->fpstt) & 0x7;                       \
        v = env->fpregs[(r)].d.low;                         \
    } else if (IS_MAPPING_VEC((r))) {                       \
        v = env->xmm_regs[(r) - 16]._q_ZMMReg[0];           \
    } else if (IS_TEMP_FP((r))) {                           \
        v = __movfr2gr64_native_freg_temp((r));             \
    } else {                                                \
        lsassert(0);                                        \
    }                                                       \
} while (0)

#define WRITE_FREG_32(v, r) do {                                    \
    if (IS_MAPPING_FP((r))) {                                       \
        lsassert(option_lsfpu);                                     \
        r = ((r) + env->fpstt) & 0x7;                               \
        *((uint32_t*)&env->fpregs[(r)].d.low) = (v) & 0xffffffff;   \
    } else if (IS_MAPPING_VEC((r))) {                               \
        env->xmm_regs[(r) - 16]._l_ZMMReg[0] = (v) & 0xffffffff;    \
    } else if (IS_TEMP_FP((r))) {                                   \
        __movgr2fr32_native_freg_temp(v, r);                        \
    } else {                                                        \
        lsassert(0);                                                \
    }                                                               \
} while (0)

#define WRITE_FREG_64(v, r) do {                                    \
    if (IS_MAPPING_FP((r))) {                                       \
        lsassert(option_lsfpu);                                     \
        r = ((r) + env->fpstt) & 0x7;                               \
        *((uint64_t*)&env->fpregs[r].d.low) = (v);                  \
    } else if (IS_MAPPING_VEC(r)) {                                 \
        env->xmm_regs[r - 16]._q_ZMMReg[0] = (v);                   \
    } else if (IS_TEMP_FP(r)) {                                     \
        __movgr2fr64_native_freg_temp((v), r);                      \
    } else {                                                        \
        lsassert(0);                                                \
    }                                                               \
} while (0)

#define READ_VREG_128(ptr, r) do {                      \
    uint64_t *p = (void *)(ptr);                        \
    if (IS_MAPPING_VEC(r)) {                            \
        p[0] = env->xmm_regs[(r) - 16]._q_ZMMReg[0];    \
        p[1] = env->xmm_regs[(r) - 16]._q_ZMMReg[1];    \
    } else if (IS_TEMP_FP(r)) {                         \
        __vst128_native_vreg_temp(p, (r));              \
    } else {                                            \
        lsassert(0);                                    \
    }                                                   \
} while (0)

#define WRITE_VREG_128(ptr, r) do {                     \
    uint64_t *p = (void *)(ptr);                        \
    if (IS_MAPPING_VEC(r)) {                            \
        env->xmm_regs[(r) - 16]._q_ZMMReg[0] = p[0];    \
        env->xmm_regs[(r) - 16]._q_ZMMReg[1] = p[1];    \
    } else if (IS_TEMP_FP(r)) {                         \
        __vld128_native_vreg_temp(p, (r));              \
    } else {                                            \
        lsassert(0);                                    \
    }                                                   \
} while (0)
#endif

    uint64_t vaddr = 0;
    READ_REG(vaddr, rj);

#ifdef CONFIG_LATX_PRINT
    CPUState *cpu = env_cpu(env);
    uint16_t h48 = (vaddr >> 48) & 0xffff;
    if (h48 != 0 && h48 != 0xffff) {
        printf("%s:%d CPU %d ADE error : vaddr %p ptr %p\n", __func__, __LINE__,
                cpu->cpu_index, (void *)vaddr, p);
    }
#endif

#if 0
    static uint64_t asd = 0;
    fprintf(stderr,
            "%s inst_ldst %x ptr %p envjmpcache %p vaddr %#lx asd %d r %d %d %d\n",
            __func__, inst_ldst, p,
            env->tb_jmp_cache_ptr,
            vaddr, asd++, rd, rj, rk);
#endif

    uint64_t st_val = 0, ld_val = 0;
    int is_load = 0, is_load_fp = 0, is_load_vec = 0;
    int memop = 0;

    switch (inst_ldst & ~(0x7fff)) {
    case 0x38000000: /* LISA_LDX_B */
        is_load = 1;
        memop = convert_to_tcgmemop(LISA_LD_B);
        memop = (memop << 4) | mmu_index;
        rr_counter_record(RR_EVENT_STLB_LD_MISS);
        ld_val = helper_ldsb_mmu(env, vaddr, memop, (uintptr_t)p);
        break;
    case 0x38200000: /* LISA_LDX_BU */
        is_load = 1;
        memop = convert_to_tcgmemop(LISA_LD_BU);
        memop = (memop << 4) | mmu_index;
        rr_counter_record(RR_EVENT_STLB_LD_MISS);
        ld_val = helper_ldub_mmu(env, vaddr, memop, (uintptr_t)p);
        break;
    case 0x38040000: /* LISA_LDX_H */
        is_load = 1;
        memop = convert_to_tcgmemop(LISA_LD_H);
        memop = (memop << 4) | mmu_index;
        rr_counter_record(RR_EVENT_STLB_LD_MISS);
        ld_val = helper_ldsw_mmu(env, vaddr, memop, (uintptr_t)p);
        break;
    case 0x38240000: /* LISA_LDX_HU */
        is_load = 1;
        memop = convert_to_tcgmemop(LISA_LD_HU);
        memop = (memop << 4) | mmu_index;
        rr_counter_record(RR_EVENT_STLB_LD_MISS);
        ld_val = helper_lduw_mmu(env, vaddr, memop, (uintptr_t)p);
        break;
    case 0x38080000: /* LISA_LDX_W */
        is_load = 1;
        memop = convert_to_tcgmemop(LISA_LD_W);
        memop = (memop << 4) | mmu_index;
        rr_counter_record(RR_EVENT_STLB_LD_MISS);
        ld_val = helper_ldsl_mmu(env, vaddr, memop, (uintptr_t)p);
        break;
    case 0x38280000: /* LISA_LDX_WU */
        is_load = 1;
        memop = convert_to_tcgmemop(LISA_LD_WU);
        memop = (memop << 4) | mmu_index;
        rr_counter_record(RR_EVENT_STLB_LD_MISS);
        ld_val = helper_ldul_mmu(env, vaddr, memop, (uintptr_t)p);
        break;
    case 0x380C0000: /* LISA_LDX_D */
        is_load = 1;
        memop = convert_to_tcgmemop(LISA_LD_D);
        memop = (memop << 4) | mmu_index;
        rr_counter_record(RR_EVENT_STLB_LD_MISS);
        ld_val = helper_ldq_mmu(env, vaddr, memop, (uintptr_t)p);
        break;
    case 0x38100000: /* LISA_STX_B */
        READ_REG(st_val, rd);
        memop = convert_to_tcgmemop(LISA_ST_B);
        memop = (memop << 4) | mmu_index;
        rr_counter_record(RR_EVENT_STLB_ST_MISS);
        helper_stb_mmu(env, vaddr, st_val, memop, (uintptr_t)p);
        break;
    case 0x38140000: /* LISA_STX_H */
        READ_REG(st_val, rd);
        memop = convert_to_tcgmemop(LISA_ST_H);
        memop = (memop << 4) | mmu_index;
        rr_counter_record(RR_EVENT_STLB_ST_MISS);
        helper_stw_mmu(env, vaddr, st_val, memop, (uintptr_t)p);
        break;
    case 0x38180000: /* LISA_STX_W */
        READ_REG(st_val, rd);
        memop = convert_to_tcgmemop(LISA_ST_W);
        memop = (memop << 4) | mmu_index;
        rr_counter_record(RR_EVENT_STLB_ST_MISS);
        helper_stl_mmu(env, vaddr, st_val, memop, (uintptr_t)p);
        break;
    case 0x381C0000: /* LISA_STX_D */
        READ_REG(st_val, rd);
        memop = convert_to_tcgmemop(LISA_ST_D);
        memop = (memop << 4) | mmu_index;
        rr_counter_record(RR_EVENT_STLB_ST_MISS);
        helper_stq_mmu(env, vaddr, st_val, memop, (uintptr_t)p);
        break;

    case 0x38300000: /* LISA_FLDX_S */
        is_load = 1; is_load_fp = 1;
        memop = convert_to_tcgmemop(LISA_FLD_S);
        memop = (memop << 4) | mmu_index;
        rr_counter_record(RR_EVENT_STLB_FLD_MISS);
        ld_val = helper_ldul_mmu(env, vaddr, memop, (uintptr_t)p);
        break;
    case 0x38340000: /* LISA_FLDX_D */
        is_load = 1; is_load_fp = 2;
        memop = convert_to_tcgmemop(LISA_FLD_D);
        memop = (memop << 4) | mmu_index;
        rr_counter_record(RR_EVENT_STLB_FLD_MISS);
        ld_val = helper_ldq_mmu(env, vaddr, memop, (uintptr_t)p);
        break;
    case 0x38380000: /* LISA_FSTX_S */
        /* get value from CPU float point register */
        READ_FREG_32(st_val, rd);
        memop = convert_to_tcgmemop(LISA_FST_S);
        memop = (memop << 4) | mmu_index;
        rr_counter_record(RR_EVENT_STLB_FST_MISS);
        helper_stl_mmu(env, vaddr, st_val, memop, (uintptr_t)p);
        break;
    case 0x383C0000: /* LISA_FSTX_D */
        /* get value from CPU float point register */
        READ_FREG_64(st_val, rd);
        memop = convert_to_tcgmemop(LISA_FST_D);
        memop = (memop << 4) | mmu_index;
        rr_counter_record(RR_EVENT_STLB_FST_MISS);
        helper_stq_mmu(env, vaddr, st_val, memop, (uintptr_t)p);
        break;

    case 0x38400000: /* LISA_VLDX */
        is_load = 1; is_load_vec = 1;
        memop = convert_to_tcgmemop(LISA_VLD);
        memop = (memop << 4) | mmu_index;
        rr_counter_record(RR_EVENT_STLB_VLD_MISS);
        latxs_helper_lddq_mmu(env, vaddr, memop, (uintptr_t)p);
        break;
    case 0x38440000: /* LISA_VSTX */
        /* get value from CPU vector register */
        /* and write into env->temp_xmm, which is used by the helper  */
        READ_VREG_128(&(env->temp_xmm), rd); /*&env->temp_xmm;*/
        memop = convert_to_tcgmemop(LISA_VST);
        memop = (memop << 4) | mmu_index;
        rr_counter_record(RR_EVENT_STLB_VST_MISS);
        latxs_helper_stdq_mmu(env, vaddr, 0, memop, (uintptr_t)p);
        break;

    default:
        /*
         * The decoded word is not one of the access instructions emitted by
         * tr_gen_lookup_qemu_tlb().  That means the anchor window and the
         * helper contract disagree (layout/configuration mismatch or corrupted
         * generated code).  lsassertm() is compiled out in production, and
         * falling through here would silently drop the access and return into
         * the fast path, so fail closed instead.
         */
        fprintf(stderr, "%s: bad softmmu access word %#x at %p\n",
                __func__, inst_ldst, p);
        abort();
    }

    if (is_load) {
        if (is_load_fp == 1) { /* LISA_FLDX_S */
            WRITE_FREG_32(ld_val, rd);
        } else if (is_load_fp == 2) { /* LISA_FLDX_D */
            WRITE_FREG_64(ld_val, rd);
        } else if (is_load_vec) { /* LISA_VLDX */
            WRITE_VREG_128(&(env->temp_xmm), rd);
        } else {
            WRITE_REG(ld_val, rd);
        }
    }

    return ptr_opnd + ret_off;
}

/* generate one call to a common helper for all load/store */
static void tr_gen_softmmu_slow_path_common(TRANSLATION_DATA *td,
        softmmu_sp_rcd_t *sp_array, int n)
{
    if (n == 0) {
        return;
    }

    softmmu_sp_rcd_t *sp = NULL;

    if (option_softmmu_pcaddi_cold && option_common_slowpath) {
        /*
         * Cold anchor.  Every access gets a two-instruction stub:
         *
         *     label_slow_path:
         *         pcaddi a3, label_fast_path   ; label -> fast-path window
         *         b       common_body
         *
         * Only the stub (i.e. only the soft-TLB miss path) now materialises
         * the a3 instruction-window pointer; the hot path no longer executes
         * the old unconditional "pcaddi a3, 0".  a3 is a scratch/argument
         * register (not an itemp), so clobbering it here, before the common
         * body saves the argument registers, is safe.  The common body itself
         * is unchanged and still starts with label_body.
         */
        IR2_OPND label_body = latxs_ir2_opnd_new_label();
        for (int i = 0; i < n; ++i) {
            sp = &sp_array[i];
            lsassertm(latxs_ir2_opnd_is_label(&sp->label_fast_path),
                    "softmmu slow path %d has no fast-path anchor\n", i);
            latxs_append_ir2_opnd1(LISA_LABEL, &sp->label_slow_path);
            if (!latxs_ir2_opnd_is_inv(&sp->rmw_valid)) {
                /* A load miss may refill or resize its TLB.  Invalidate the
                 * saved entry before the shared body saves live itemps. */
                latxs_append_ir2_opnd2i(LISA_ADDI_D, &sp->rmw_valid,
                                        &latxs_zero_ir2_opnd, 0);
            }
            latxs_append_ir2_opnd2(LISA_PCADDI,
                    &latxs_arg3_ir2_opnd, &sp->label_fast_path);
            latxs_append_ir2_opnd1(LISA_B, &label_body);
        }
        latxs_append_ir2_opnd1(LISA_LABEL, &label_body);
    } else {
        for (int i = 0; i < n; ++i) {
            sp = &sp_array[i];
            latxs_append_ir2_opnd1(LISA_LABEL, &sp->label_slow_path);
        }
    }

    IR2_OPND *env = &latxs_env_ir2_opnd;
    IR2_OPND *zero = &latxs_zero_ir2_opnd;
    IR2_OPND *arg0 = &latxs_arg0_ir2_opnd;
    IR2_OPND *arg1 = &latxs_arg1_ir2_opnd;
    IR2_OPND *arg2 = &latxs_arg2_ir2_opnd;
    IR2_OPND *arg3 = &latxs_arg3_ir2_opnd;

#define REGOFF(n)   (offsetof(CPUX86State, save_regs[(n)]))

    latxs_tr_save_temp_register_mask(0xffff);
    latxs_append_ir2_opnd2i(LISA_ST_D, zero, env, REGOFF(0));
    latxs_append_ir2_opnd2i(LISA_ST_D, arg0, env, REGOFF(4));
    latxs_append_ir2_opnd2i(LISA_ST_D, arg1, env, REGOFF(5));
    latxs_append_ir2_opnd2i(LISA_ST_D, arg2, env, REGOFF(6));
    latxs_append_ir2_opnd2i(LISA_ST_D, arg3, env, REGOFF(7));
#ifdef CONFIG_LATX_OPT_NOFP
    latxs_tr_gen_call_to_helper_prologue_cfg(nofp_helper_cfg);
#else
    latxs_tr_gen_call_to_helper_prologue_cfg(default_helper_cfg);
#endif

    latxs_append_ir2_opnd2_(lisa_mov, arg0, env);
    if (hamt_enabled()) {
        latxs_tr_dmw_out_fix_mem(arg0);
    }
    latxs_tr_gen_call_to_helper((ADDR)helper_softmmu_common, HELPER_SOFTMMU_COMMON);

#ifdef CONFIG_LATX_OPT_NOFP
    latxs_tr_gen_call_to_helper_epilogue_cfg(nofp_helper_cfg);
#else
    latxs_tr_gen_call_to_helper_epilogue_cfg(default_helper_cfg);
#endif
    latxs_tr_restore_temp_register_mask(0xffff);

    /* back to code cahce, v0(a0) contains the pointer */
    latxs_append_ir2_opnd2i(LISA_JIRL, &latxs_zero_ir2_opnd,
            &latxs_arg0_ir2_opnd, 0);
}

void tr_gen_softmmu_slow_path(void)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    td->in_gen_slow_path = 1;

    /*int real_ir2_before_slow_path = td->ir2_asm_nr;*/

    int i = 0;
    int sp_nr = td->slow_path_rcd_nr;

    softmmu_sp_rcd_t *sp = NULL;
    softmmu_sp_rcd_t *sp_array = td->slow_path_rcd;

    if (option_common_slowpath) {
        tr_gen_softmmu_slow_path_common(td, sp_array, sp_nr);
        return;
    }

    for (i = 0; i < sp_nr; ++i) {
        sp = &sp_array[i];
        __tr_gen_softmmu_sp_rcd(sp);
    }

    /*int real_ir2_after_slow_path = td->ir2_asm_nr;*/

    /*TranslationBlock *curr_tb = td->curr_tb;*/
    /*curr_tb->slow_path_icount = real_ir2_after_slow_path - real_ir2_before_slow_path;*/

    td->in_gen_slow_path = 0;
}

void gen_ldst_softmmu_helper(
        IR2_OPCODE op,
        IR2_OPND *opnd_gpr,
        IR2_OPND *mem_base,
        int mem_offset,
        int save_temp)
{
#ifdef CONFIG_SOFTMMU
    if (is_ldst_realized_by_softmmu(op)) {
        if (can_do_hamt(lsenv->cpu_state)) {
            lsassertm(int32_in_int12(mem_offset),
                    "memory offset out of range 0x%x\n", mem_offset);
            latxs_append_ir2_opnd2i(op, opnd_gpr, mem_base, mem_offset);
        } else {
            __gen_ldst_softmmu_helper_native(op, opnd_gpr, mem_base, mem_offset, save_temp);
        }
        return;
    }
    lsassertm(0, "Softmmu not support non-load/store instruction.");
#else
    latxs_append_ir2_opnd2i(op, opnd_gpr, mem_base, mem_offset);
#endif
}

#ifdef CONFIG_SOFTMMU
/* A single-instruction prototype: reuse the read lookup only on its fast hit.
 * No helper or TB boundary may occur between the load and the later store. */
bool latxs_softmmu_rmw_reuse_eligible(IR1_INST *pir1)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    return option_softmmu_rmw_reuse && option_softmmu_pcaddi_cold &&
           option_common_slowpath && !option_smmu_slow &&
           !rr_counter_enabled() && !td->is_tu_mode && td->sys.code64 &&
           !in_aot_translate && !option_sys_aot && !option_aot_save &&
           !option_aot_load &&
           __builtin_popcount(td->itemp_mask | td->ptn_itemp_mask) + 3 <=
               latxs_itemp_status_num &&
           !latxs_ir1_has_prefix_lock(pir1) && !can_do_hamt(lsenv->cpu_state);
}

void latxs_softmmu_rmw_load64(IR2_OPND *dst, IR2_OPND *base, int offset,
                              IR2_OPND *valid)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    int record = td->slow_path_rcd_nr;
    latxs_append_ir2_opnd2i(LISA_ADDI_D, valid, &latxs_zero_ir2_opnd, 1);
    __gen_ldst_softmmu_helper_native(LISA_LD_D, dst, base, offset, 1);
    /* The newly created read miss stub clears the validity flag. */
    assert(td->slow_path_rcd_nr == record + 1);
    ((softmmu_sp_rcd_t *)td->slow_path_rcd)[record].rmw_valid = *valid;
}

void latxs_softmmu_rmw_store64(IR2_OPND *value, IR2_OPND *base, int offset,
                               IR2_OPND *valid)
{
    IR2_OPND fallback = latxs_ir2_opnd_new_label();
    IR2_OPND done = latxs_ir2_opnd_new_label();
    IR2_OPND addr = *base;
    bool allocated = false;
    IR2_OPND *read_tag = &latxs_arg0_ir2_opnd;
    IR2_OPND *addend = &latxs_arg1_ir2_opnd;
    IR2_OPND *entry = &latxs_arg2_ir2_opnd;
    IR2_OPND *write_tag = &latxs_arg3_ir2_opnd;

    /* The read helper may have invalidated its entry pointer. */
    latxs_append_ir2_opnd3(LISA_BEQ, valid, &latxs_zero_ir2_opnd, &fallback);
    if (offset) {
        addr = latxs_ra_alloc_itemp();
        allocated = true;
        latxs_append_ir2_opnd2i(LISA_ADDI_D, &addr, base, offset);
    }
    /* Restricted to byhand 64-bit ADD/SUB: the read hit leaves a0 as
     * the validated 8-byte end-page key, a1 as the DMW-ready addend and a2
     * as the entry.  Only ADD_D or SUB_D to an itemp is emitted between that hit and
     * here; a3 is neither a guest mapping nor an itemp.  A read slow path can
     * clobber or invalidate all three values and must take fallback above.
     *
     * Re-read the write tag immediately before the store: another vCPU can
     * set TLB_NOTDIRTY.  A remote dirty reset changes that tag, not addend;
     * TLB refill, resize and PCID bank selection run on the owning vCPU and
     * cannot occur between the two accesses on this helper-free fast path. */
    latxs_append_ir2_opnd2i(LISA_LD_D, write_tag, entry,
                            offsetof(CPUTLBEntry, addr_write));
    latxs_append_ir2_opnd3(LISA_BNE, read_tag, write_tag, &fallback);
    latxs_append_ir2_opnd3(LISA_STX_D, value, &addr, addend);
    uint64_t *hit_counter = latxs_softmmu_rmw_hit_counter();
    if (hit_counter) {
        /* a0/a3 are scratch and no longer hold guest state after the store;
         * AMADD does not change LBT flags.  Diagnostics perturb timing and
         * must not be used for production performance measurements. */
        latxs_load_imm64_to_ir2(read_tag, (uintptr_t)hit_counter);
        if (hamt_enabled()) {
            latxs_tr_dmw_in_fix_mem(read_tag);
        }
        latxs_append_ir2_opnd2i(LISA_ADDI_D, write_tag,
                                &latxs_zero_ir2_opnd, 1);
        latxs_append_ir2_opnd3(LISA_AMADD_D, &latxs_zero_ir2_opnd,
                               read_tag, write_tag);
    }
    latxs_append_ir2_opnd1(LISA_B, &done);
    if (allocated) {
        latxs_ra_free_temp(&addr);
    }
    latxs_append_ir2_opnd1(LISA_LABEL, &fallback);
    /* Only the store is retried: the guest read is never repeated. */
    __gen_ldst_softmmu_helper_native(LISA_ST_D, value, base, offset, 1);
    latxs_append_ir2_opnd1(LISA_LABEL, &done);
}
#endif

void gen_ldst_c1_softmmu_helper(
        IR2_OPCODE op,
        IR2_OPND *opnd_fpr,
        IR2_OPND *mem_base,
        int mem_offset,
        int save_temp)
{
    if (option_fast_fpr_ldst) {
        gen_ldst_softmmu_helper(op, opnd_fpr, mem_base, mem_offset, 1);
        return;
    }

    IR2_OPCODE ldst_op = LISA_INVALID;
    IR2_OPCODE mfmt_op = LISA_INVALID;

    switch (op) {
    case LISA_FLD_S:
        ldst_op = LISA_LD_WU;
        mfmt_op = LISA_MOVGR2FR_W;
        break;
    case LISA_FLD_D:
        ldst_op = LISA_LD_D;
        mfmt_op = LISA_MOVGR2FR_D;
        break;
    case LISA_FST_S:
        ldst_op = LISA_ST_W;
        mfmt_op = LISA_MOVFR2GR_S;
        break;
    case LISA_FST_D:
        ldst_op = LISA_ST_D;
        mfmt_op = LISA_MOVFR2GR_D;
        break;
    default:
        lsassert(0);
        break;
    }

    IR2_OPND tmp = latxs_ra_alloc_itemp();
    if (latxs_ir2_opcode_is_load(op)) {
        /* 1. load to GPR from memory */
        gen_ldst_softmmu_helper(ldst_op, &tmp, mem_base, mem_offset, save_temp);
        /* 2. move GPR to FPR */
        latxs_append_ir2_opnd2(mfmt_op, opnd_fpr, &tmp);
    } else {
        /* 1. move FPR to GPR */
        latxs_append_ir2_opnd2(mfmt_op, &tmp, opnd_fpr);
        /* 2. store GPR to to memory */
        gen_ldst_softmmu_helper(ldst_op, &tmp, mem_base, mem_offset, save_temp);
    }
}

#else /* no CONFIT_SOFTMMU */

void gen_ldst_softmmu_helper(
        IR2_OPCODE op,
        IR2_OPND *opnd_gpr,
        IR2_OPND *mem_base,
        int mem_offset,
        int save_temp)
{
    lsassertm(int32_in_int12(mem_offset),
            "memory offset out of range 0x%x\n", mem_offset);
    latxs_append_ir2_opnd2i(op, opnd_gpr, mem_base, mem_offset);
}

void gen_ldst_c1_softmmu_helper(
        IR2_OPCODE op,
        IR2_OPND *opnd_fpr,
        IR2_OPND *mem_base,
        int mem_offset,
        int save_temp)
{
    if (option_fast_fpr_ldst) {
        gen_ldst_softmmu_helper(op, opnd_fpr, mem_base, mem_offset, 1);
        return;
    }

    IR2_OPCODE ldst_op = LISA_INVALID;
    IR2_OPCODE mfmt_op = LISA_INVALID;

    switch (op) {
    case LISA_FLD_S:
        ldst_op = LISA_LD_WU;
        mfmt_op = LISA_MOVGR2FR_W;
        break;
    case LISA_FLD_D:
        ldst_op = LISA_LD_D;
        mfmt_op = LISA_MOVGR2FR_D;
        break;
    case LISA_FST_S:
        ldst_op = LISA_ST_W;
        mfmt_op = LISA_MOVFR2GR_S;
        break;
    case LISA_FST_D:
        ldst_op = LISA_ST_D;
        mfmt_op = LISA_MOVFR2GR_D;
        break;
    default:
        lsassert(0);
        break;
    }

    IR2_OPND tmp = latxs_ra_alloc_itemp();
    if (latxs_ir2_opcode_is_load(op)) {
        /* 1. load to GPR from memory */
        gen_ldst_softmmu_helper(ldst_op, &tmp, mem_base, mem_offset, save_temp);
        /* 2. move GPR to FPR */
        latxs_append_ir2_opnd2(mfmt_op, opnd_fpr, &tmp);
    } else {
        /* 1. move FPR to GPR */
        latxs_append_ir2_opnd2(mfmt_op, &tmp, opnd_fpr);
        /* 2. store GPR to to memory */
        gen_ldst_softmmu_helper(ldst_op, &tmp, mem_base, mem_offset, save_temp);
    }
}

#endif
