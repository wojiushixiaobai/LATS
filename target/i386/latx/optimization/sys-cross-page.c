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
#include "latx-perfmap.h"
#include "latx-intb-sys.h"
#include "latx-helper.h"
#include "latx-static-codes.h"
#include "latx-shadow-htable.h"
#include "latx-intb-counter.h"
#include "latx-sys-hamt-dmw.h"
#include "latx-hack.h"
#include "latx-ptw-address.h"
#include "tcg/tcg-split-tb.h"
#include "latx-cp-counter.h"
#include <string.h>

#ifdef CONFIG_SOFTMMU

int gen_latxs_cross_page_check(void *code_ptr,
        int n, int mmu, int mode, int ptw);

static inline void tr_check_pte_execute(IR2_OPND *pte, IR2_OPND *tmp,
                                        IR2_OPND *do_fault)
{
    la_out2i(LISA_SRLI_D, tmp, pte, PG_NX_BIT);
    la_out2(LISA_BNEZ, tmp, do_fault);
}

static void tr_lookup_code_fast_itlb_ptw(IR2_OPND *pc,
        IR2_OPND *tmp0, IR2_OPND *tmp1, IR2_OPND *tmp2, IR2_OPND *tmp3,
        IR2_OPND do_fault, int mmu)
{
    IR2_OPND do_page2m = la_new_label();
    IR2_OPND all_done  = la_new_label();

    /*
     * The fast walk does not accumulate the effective U/S bit across all
     * levels. Let the full x86 walk check supervisor fetches under SMEP.
     */
    if (mmu != MMU_USER_IDX) {
        la_out2i(LISA_LD_WU, tmp1, &la_env, offsetof(CPUX86State, cr[4]));
        la_out2ii(LISA_BSTRPICK_D, tmp1, tmp1, 20, 20);
        la_out2(LISA_BNEZ, tmp1, &do_fault);
    }

    la_imm(tmp0, HACK_RAMBASE);
    la_out2i(LISA_LD_D, tmp1, &la_env, offsetof(CPUX86State, cr[3]));
    /* === Page Table Level 4 === */
    /* ((CR3 & PG_ADDRESS_MASK) + (((pc >> 39) & 0x1ff) << 3));*/
    latxs_ptw_address(tmp1, tmp1, tmp2, pc, 39);
    /* GPA to HVA : addr += HACK_RAMBASE */
    la_out3  (LISA_ADD_D, tmp1, tmp1, tmp0);
    if (hamt_enabled()) latxs_tr_dmw_in_fix_mem(tmp1);
    la_out2i  (LISA_LD_D, tmp1, tmp1, 0); /* P4D = *(PGD + index) */
    /* check PG_PRESENT_MASK */
    la_out2i(LISA_ANDI, tmp2, tmp1, PG_PRESENT_MASK);
    la_out2 (LISA_BEQZ, tmp2, &do_fault);
    tr_check_pte_execute(tmp1, tmp2, &do_fault);
    /* === Page Table Level 3 === */
    /* ((P4D & PG_ADDRESS_MASK) + (((pc >> 30) & 0x1ff) << 3));*/
    latxs_ptw_address(tmp1, tmp1, tmp2, pc, 30);
    /* GPA to HVA : addr += HACK_RAMBASE */
    la_out3  (LISA_ADD_D, tmp1, tmp1, tmp0);
    if (hamt_enabled()) latxs_tr_dmw_in_fix_mem(tmp1);
    la_out2i  (LISA_LD_D, tmp1, tmp1, 0); /* PUD = *(P4D + index) */
    /* check PG_PRESENT_MASK */
    la_out2i(LISA_ANDI, tmp2, tmp1, PG_PRESENT_MASK);
    la_out2 (LISA_BEQZ, tmp2, &do_fault);
    tr_check_pte_execute(tmp1, tmp2, &do_fault);
    la_out2i(LISA_ANDI, tmp2, tmp1, PG_PSE_MASK); /* 1GB page */
    la_out2 (LISA_BNEZ, tmp2, &do_fault);
    /* === Page Table Level 2 === */
    /* ((PUD & PG_ADDRESS_MASK) + (((pc >> 21) & 0x1ff) << 3));*/
    latxs_ptw_address(tmp1, tmp1, tmp2, pc, 21);
    /* GPA to HVA : addr += HACK_RAMBASE */
    la_out3  (LISA_ADD_D, tmp1, tmp1, tmp0);
    if (hamt_enabled()) latxs_tr_dmw_in_fix_mem(tmp1);
    la_out2i  (LISA_LD_D, tmp1, tmp1, 0); /* PMD = *(PUD + index) */
    /* check PG_PRESENT_MASK */
    la_out2i(LISA_ANDI, tmp2, tmp1, PG_PRESENT_MASK);
    la_out2 (LISA_BEQZ, tmp2, &do_fault);
    tr_check_pte_execute(tmp1, tmp2, &do_fault);
    la_out2i(LISA_ANDI, tmp2, tmp1, PG_PSE_MASK); /* 2MB page */
    la_out2 (LISA_BNEZ, tmp2, &do_page2m);
    /* === Page Table Level 1 === */
    latxs_ptw_address(tmp1, tmp1, tmp2, pc, 12);
    /* GPA to HVA : addr += HACK_RAMBASE */
    la_out3  (LISA_ADD_D, tmp1, tmp1, tmp0);
    if (hamt_enabled()) latxs_tr_dmw_in_fix_mem(tmp1);
    la_out2i  (LISA_LD_D, tmp2, tmp1, 0); /* PTE = *(PMD + index) */
    /* check PG_PRESENT_MASK */
    la_out2i(LISA_ANDI, tmp1, tmp2, PG_PRESENT_MASK);
    la_out2 (LISA_BEQZ, tmp1, &do_fault);
    tr_check_pte_execute(tmp2, tmp1, &do_fault);
    if (mmu == MMU_USER_IDX) {
        la_out2i(LISA_ANDI, tmp1, tmp2, PG_USER_MASK);
        la_out2 (LISA_BEQZ, tmp1, &do_fault);
    }
    /* === refill into fast iTLB 4K page === */
    tr_refill_code_fast_itlb(pc, tmp2, tmp0, tmp1, tmp3, mmu, 4);
    la_out1(LISA_B, &all_done);
    /* === refill into fast iTLB 2M page === */
    la_label(do_page2m);
    if (mmu == MMU_USER_IDX) {
        la_out2i(LISA_ANDI, tmp2, tmp1, PG_USER_MASK);
        la_out2 (LISA_BEQZ, tmp2, &do_fault);
    }
    la_out2_(lisa_mov, tmp2, tmp1); /* tmp2 = PTE 2M */
    tr_refill_code_fast_itlb(pc, tmp2, tmp0, tmp1, tmp3, mmu, 2);
    la_label(all_done);
    /* return tmp2 = physical page >> 12 */
}

int gen_latxs_cross_page_check_ptw(void *code_ptr, int mmu);
static void tr_do_fastitlb_ptw(void *code_ptr, int mmu);
static void tr_context_switch(void *code_ptr, int mmu, int n);
static void gen_intcheck(IR2_OPND *tmp, IR2_OPND *label_exit);

/*
 * TU page descriptors carry a runtime virtual page, so their fast iTLB
 * lookup cannot use sys-eob's per-TB ldptr.d immediate directly.  Keep the
 * negative iTLB table base in a register instead: it is directly adjacent
 * to env and does not require loading a CPUTLBDescFast table pointer.
 */
static void tr_prepare_code_fast_itlb_user_neg(IR2_OPND *itlb)
{
    if (option_pcid_cache) {
        la_out2i(LISA_LD_D, itlb, &la_env,
                 offsetof(CPUX86State, pcid_itlb_user));
        return;
    }

    int off = (int)offsetof(ArchCPU, neg.itlb_user) -
              (int)offsetof(ArchCPU, env);

    if (int32_in_int12(off)) {
        la_out2i(LISA_ADDI_D, itlb, &la_env, off);
    } else {
        la_imm(itlb, (int64_t)off);
        la_out3(LISA_ADD_D, itlb, &la_env, itlb);
    }
}

static void tr_lookup_code_fast_itlb_user_neg(IR2_OPND *entry,
                                               IR2_OPND *gva,
                                               IR2_OPND *itlb)
{
    IR2_OPND index = latxs_arg1_ir2_opnd;
    IR2_OPND addr = latxs_arg2_ir2_opnd;

    la_out2ii(LISA_BSTRPICK_D, &index, gva, 21, 12);
    la_out3i(LISA_ALSL_D, &addr, &index, itlb, 2);
    la_out2i(LISA_LD_D, entry, &addr, 0);
}

/*
 * Skip a full TU page walk when this vCPU has already validated the TU.
 * Array slot zero is never updated, so ID zero always misses without an
 * explicit ID check.
 */
static void tr_gen_tu_memset_cache_lookup_pages(IR2_OPND *pages,
                                                IR2_OPND *t0,
                                                IR2_OPND *t1,
                                                IR2_OPND *t2)
{
    IR2_OPND label_miss = la_new_label();

    la_out2i(LISA_LD_WU, t0, pages, offsetof(TranslationUnitPages, id));
    la_out2i(LISA_LD_D, t1, &la_env,
             offsetof(CPUX86State, tu_generation_array));
    la_out3(LISA_ADD_D, t1, t1, t0);
    la_out2i(LISA_LD_BU, t2, t1, 0);
    la_out2(LISA_BEQZ, t2, &label_miss);
    la_out0_(lisa_return);
    la_label_name(label_miss, "TU generation miss");
    /* Keep array[0] read-only so ID zero remains a permanent miss. */
    la_out3(LISA_MASKEQZ, t1, t1, t0);
    la_out2i(LISA_ST_D, t1, &la_env,
             offsetof(CPUX86State, save_regs[9]));
}

static void tr_gen_tu_counter_cache_lookup_pages(IR2_OPND *pages,
                                                 IR2_OPND *t0,
                                                 IR2_OPND *t1,
                                                 IR2_OPND *t2)
{
    IR2_OPND label_miss = la_new_label();

    la_out2i(LISA_LD_WU, t0, pages, offsetof(TranslationUnitPages, id));
    la_out2i(LISA_LD_D, t1, &la_env,
             offsetof(CPUX86State, tu_generation_array));
    la_out3i(LISA_ALSL_D, t1, t0, t1, 0);
    la_out2i(LISA_LD_HU, t2, &la_env,
             offsetof(CPUX86State, tu_generation));
    la_out2i(LISA_LD_HU, t0, t1, 0);
    la_out3(LISA_BNE, t2, t0, &label_miss);
    la_out0_(lisa_return);
    la_label_name(label_miss, "TU generation miss");
    la_out2i(LISA_LD_WU, t0, pages, offsetof(TranslationUnitPages, id));
    /* Keep array[0] read-only so ID zero remains a permanent miss. */
    la_out3(LISA_MASKEQZ, t1, t1, t0);
    la_out2i(LISA_ST_D, t1, &la_env,
             offsetof(CPUX86State, save_regs[9]));
}

static void tr_gen_tu_generation_lookup_pages(IR2_OPND *pages,
                                              IR2_OPND *t0, IR2_OPND *t1,
                                              IR2_OPND *t2)
{
    if (tucp_generation_counter()) {
        tr_gen_tu_counter_cache_lookup_pages(pages, t0, t1, t2);
    } else {
        tr_gen_tu_memset_cache_lookup_pages(pages, t0, t1, t2);
    }
}

/* The direct TUCHECK stub has already established a cache miss. */
static void tr_gen_tu_memset_cache_prepare_update(IR2_OPND *pages,
                                                  IR2_OPND *t0,
                                                  IR2_OPND *t1)
{
    la_out2i(LISA_LD_WU, t0, pages, offsetof(TranslationUnitPages, id));
    la_out2i(LISA_LD_D, t1, &la_env,
             offsetof(CPUX86State, tu_generation_array));
    la_out3(LISA_ADD_D, t1, t1, t0);
    /* Keep array[0] read-only so ID zero remains a permanent miss. */
    la_out3(LISA_MASKEQZ, t1, t1, t0);
    la_out2i(LISA_ST_D, t1, &la_env,
             offsetof(CPUX86State, save_regs[9]));
}

static void tr_gen_tu_counter_cache_prepare_update(IR2_OPND *pages,
                                                   IR2_OPND *t0,
                                                   IR2_OPND *t1)
{
    la_out2i(LISA_LD_WU, t0, pages, offsetof(TranslationUnitPages, id));
    la_out2i(LISA_LD_D, t1, &la_env,
             offsetof(CPUX86State, tu_generation_array));
    la_out3i(LISA_ALSL_D, t1, t0, t1, 0);
    /* Keep array[0] read-only so ID zero remains a permanent miss. */
    la_out3(LISA_MASKEQZ, t1, t1, t0);
    la_out2i(LISA_ST_D, t1, &la_env,
             offsetof(CPUX86State, save_regs[9]));
}

static void tr_gen_tu_generation_prepare_update(IR2_OPND *pages,
                                                IR2_OPND *t0,
                                                IR2_OPND *t1)
{
    if (tucp_generation_counter()) {
        tr_gen_tu_counter_cache_prepare_update(pages, t0, t1);
    } else {
        tr_gen_tu_memset_cache_prepare_update(pages, t0, t1);
    }
}

static void tr_gen_tu_memset_cache_update(IR2_OPND *t0, IR2_OPND *t1,
                                          IR2_OPND *label_done)
{
    la_out2i(LISA_LD_D, t0, &la_env,
             offsetof(CPUX86State, save_regs[9]));
    la_out3(LISA_BEQ, t0, &la_zero, label_done);
    la_out2i(LISA_ADDI_D, t1, &la_zero, 1);
    la_out2i(LISA_ST_B, t1, t0, 0);
    la_out1(LISA_B, label_done);
}

static void tr_gen_tu_counter_cache_update(IR2_OPND *t0, IR2_OPND *t1,
                                           IR2_OPND *label_done)
{
    la_out2i(LISA_LD_D, t0, &la_env,
             offsetof(CPUX86State, save_regs[9]));
    la_out3(LISA_BEQ, t0, &la_zero, label_done);
    la_out2i(LISA_LD_HU, t1, &la_env,
             offsetof(CPUX86State, tu_generation));
    la_out2i(LISA_ST_H, t1, t0, 0);
    la_out1(LISA_B, label_done);
}

static void tr_gen_tu_generation_update(IR2_OPND *t0, IR2_OPND *t1,
                                        IR2_OPND *label_done)
{
    if (tucp_generation_counter()) {
        tr_gen_tu_counter_cache_update(t0, t1, label_done);
    } else {
        tr_gen_tu_memset_cache_update(t0, t1, label_done);
    }
}

int gen_latxs_cross_page_check_ptw(void *code_ptr, int mmu)
{
    int code_nr = 0;
    latxs_tr_init(NULL);

    /* keep regs same with gen_latxs_cross_page_check */
    IR2_OPND tb = latxs_ra_alloc_dbt_arg1(); (void)tb;
    IR2_OPND t0 = la_new_itmp();
    IR2_OPND t1 = la_new_itmp();
    IR2_OPND t2 = la_new_itmp();
    IR2_OPND t3 = la_new_itmp();
    IR2_OPND t4 = la_new_itmp(); (void)t4;

    IR2_OPND do_fault = la_new_label();
    tr_lookup_code_fast_itlb_ptw(&t0, &t1, &t2, &t3, &t4, do_fault, mmu);
    la_out0_(lisa_return);

    la_label_name(do_fault, "itlb ptw fault");
    la_out2_(lisa_mov, &t3, &la_zero);
    la_out0_(lisa_return);

    code_nr = latxs_tr_ir2_assemble(code_ptr);
    latxs_tr_fini();

    return code_nr;
}

int gen_latxs_tu_cross_page_check(void *code_ptr, int n)
{
    int code_nr;

    latxs_tr_init(NULL);

    IR2_OPND tb = latxs_ra_alloc_dbt_arg1();
    IR2_OPND t0 = la_new_itmp();
    IR2_OPND t1 = la_new_itmp();
    IR2_OPND t2 = la_new_itmp();
    IR2_OPND t3 = la_new_itmp();
    IR2_OPND pages = la_new_itmp();
    IR2_OPND itlb = latxs_arg0_ir2_opnd;
    IR2_OPND label_exit = la_new_label();
    IR2_OPND label_done = la_new_label();
    IR2_OPND label_checked = la_new_label();
    IR2_OPND label_loop = la_new_label();
    IR2_OPND label_miss = la_new_label();
    IR2_OPND label_hit = la_new_label();

    gen_intcheck(&t0, &label_exit);

    if (split_tb_base_ok()) {
        la_out1i(LISA_LU32I_D, &tb, SPLIT_TB_BASE >> 32);
    }
    if (hamt_enabled()) {
        latxs_tr_dmw_in_fix_mem(&tb);
    }
    la_out2i(LISA_LD_D, &pages, &tb,
             TB_TU_LINK_DATA_OFFSET(next_tu_pages[0]) +
             sizeof(TranslationUnitPages *) * n);
    la_out2(LISA_BEQZ, &pages, &label_done);
    if (hamt_enabled()) {
        latxs_tr_dmw_in_fix_mem(&pages);
    }
    if (tucp_generation_enabled()) {
        tr_gen_tu_generation_prepare_update(&pages, &t0, &t1);
    }
    tr_prepare_code_fast_itlb_user_neg(&itlb);
    la_out2i(LISA_ADDI_D, &t3, &la_zero, -1);

    la_label_name(label_loop, "TU page check loop");
    la_out2i(LISA_LD_D, &t0, &pages,
             offsetof(TranslationUnitPages, pages[0].vp_phys_hi));
    la_out3(LISA_BEQ, &t0, &t3,
            tucp_generation_enabled() ? &label_checked : &label_done);
    la_out2(LISA_BEQZ, &itlb, &label_miss);
    la_out2i(LISA_LD_D, &t1, &pages,
             offsetof(TranslationUnitPages, pages[0].mix));
    tr_lookup_code_fast_itlb_user_neg(&t2, &t0, &itlb);
    la_out3(LISA_BNE, &t2, &t1, &label_miss);

    la_label(label_hit);
    la_out2i(LISA_ADDI_D, &pages, &pages,
             sizeof(((TranslationUnitPages *)0)->pages[0]));
    la_out1(LISA_B, &label_loop);

    if (tucp_generation_enabled()) {
        la_label_name(label_checked, "TU page check validated");
        tr_gen_tu_generation_update(&t0, &t1, &label_done);
    }
    la_label_name(label_done, "TU page check done");
    la_out0_(lisa_return);

    la_label_name(label_miss, "TU page check PTW");
    la_out2i(LISA_ST_D, &pages, &la_env,
             offsetof(CPUX86State, save_regs[8]));
    /* Clear the packed physical bits in the page offset, not the GVA. */
    la_out2ii(LISA_BSTRINS_D, &t0, &la_zero, 11, 0);
    tr_do_fastitlb_ptw(code_ptr, MMU_USER_IDX);
    la_out2i(LISA_LD_D, &pages, &la_env,
             offsetof(CPUX86State, save_regs[8]));
    /* Rebuild the full physical page number only on the PTW miss path. */
    la_out2i(LISA_LD_D, &t1, &pages,
             offsetof(TranslationUnitPages, pages[0].vp_phys_hi));
    la_out2ii(LISA_BSTRPICK_D, &t1, &t1, 11, 0);
    la_out2i(LISA_SLLI_D, &t1, &t1, 28);
    la_out2i(LISA_LD_D, &t2, &pages,
             offsetof(TranslationUnitPages, pages[0].mix));
    la_out2ii(LISA_BSTRPICK_D, &t2, &t2, 27, 0);
    la_out3(LISA_OR, &t1, &t1, &t2);
    la_out3(LISA_BNE, &t1, &t3, &label_exit);
    tr_prepare_code_fast_itlb_user_neg(&itlb);
    la_out2i(LISA_ADDI_D, &t3, &la_zero, -1);
    la_out1(LISA_B, &label_hit);

    la_label_name(label_exit, "TU page check fail");
    tr_context_switch(code_ptr, MMU_USER_IDX, n);

    code_nr = latxs_tr_ir2_assemble(code_ptr);
    latxs_tr_fini();
    return code_nr;
}

int gen_latxs_tu_cross_page_check_ptr(void *code_ptr)
{
    int code_nr;

    latxs_tr_init(NULL);

    IR2_OPND tb = latxs_ra_alloc_dbt_arg1();
    IR2_OPND t0 = la_new_itmp();
    IR2_OPND t1 = la_new_itmp();
    IR2_OPND t2 = la_new_itmp();
    IR2_OPND t3 = la_new_itmp();
    IR2_OPND pages = la_new_itmp();
    IR2_OPND itlb = latxs_arg0_ir2_opnd;
    IR2_OPND label_exit = la_new_label();
    IR2_OPND label_done = la_new_label();
    IR2_OPND label_checked = la_new_label();
    IR2_OPND label_loop = la_new_label();
    IR2_OPND label_miss = la_new_label();
    IR2_OPND label_hit = la_new_label();

    if (hamt_enabled()) {
        latxs_tr_dmw_in_fix_mem(&tb);
    }
    la_out2i(LISA_LD_D, &pages, &tb,
             TB_PRIVATE_S_DATA_OFFSET(tu_pages));
    la_out2(LISA_BEQZ, &pages, &label_done);
    if (hamt_enabled()) {
        latxs_tr_dmw_in_fix_mem(&pages);
    }
    if (tucp_generation_enabled()) {
        tr_gen_tu_generation_lookup_pages(&pages, &t0, &t1, &t2);
    }
    tr_prepare_code_fast_itlb_user_neg(&itlb);
    la_out2i(LISA_ADDI_D, &t3, &la_zero, -1);

    la_label_name(label_loop, "TU pointer page check loop");
    la_out2i(LISA_LD_D, &t0, &pages,
             offsetof(TranslationUnitPages, pages[0].vp_phys_hi));
    la_out3(LISA_BEQ, &t0, &t3,
            tucp_generation_enabled() ? &label_checked : &label_done);
    la_out2(LISA_BEQZ, &itlb, &label_miss);
    la_out2i(LISA_LD_D, &t1, &pages,
             offsetof(TranslationUnitPages, pages[0].mix));
    tr_lookup_code_fast_itlb_user_neg(&t2, &t0, &itlb);
    la_out3(LISA_BNE, &t2, &t1, &label_miss);

    la_label(label_hit);
    la_out2i(LISA_ADDI_D, &pages, &pages,
             sizeof(((TranslationUnitPages *)0)->pages[0]));
    la_out1(LISA_B, &label_loop);

    if (tucp_generation_enabled()) {
        la_label_name(label_checked, "TU pointer page check validated");
        tr_gen_tu_generation_update(&t0, &t1, &label_done);
    }
    la_label_name(label_done, "TU pointer page check done");
    la_out0_(lisa_return);

    la_label_name(label_miss, "TU pointer page check PTW");
    la_out2i(LISA_ST_D, &pages, &la_env,
             offsetof(CPUX86State, save_regs[8]));
    la_out2ii(LISA_BSTRINS_D, &t0, &la_zero, 11, 0);
    tr_do_fastitlb_ptw(code_ptr, MMU_USER_IDX);
    la_out2i(LISA_LD_D, &pages, &la_env,
             offsetof(CPUX86State, save_regs[8]));
    /* Keep all reconstruction outside the normal cache-hit loop. */
    la_out2i(LISA_LD_D, &t1, &pages,
             offsetof(TranslationUnitPages, pages[0].vp_phys_hi));
    la_out2ii(LISA_BSTRPICK_D, &t1, &t1, 11, 0);
    la_out2i(LISA_SLLI_D, &t1, &t1, 28);
    la_out2i(LISA_LD_D, &t2, &pages,
             offsetof(TranslationUnitPages, pages[0].mix));
    la_out2ii(LISA_BSTRPICK_D, &t2, &t2, 27, 0);
    la_out3(LISA_OR, &t1, &t1, &t2);
    la_out3(LISA_BNE, &t1, &t3, &label_exit);
    tr_prepare_code_fast_itlb_user_neg(&itlb);
    la_out2i(LISA_ADDI_D, &t3, &la_zero, -1);
    la_out1(LISA_B, &label_hit);

    la_label_name(label_exit, "TU pointer page check fail");
    {
        TRANSLATION_DATA *td = lsenv->tr_data;
        int offset = td->ir2_asm_nr << 2;
        int ins_offset = GET_SC_TABLE(1, cs_native_to_bt_ret_0) -
                         (int64_t)code_ptr - offset;

        la_outa(LISA_B, ins_offset >> 2);
    }

    code_nr = latxs_tr_ir2_assemble(code_ptr);
    latxs_tr_fini();
    return code_nr;
}

static void tr_do_fastitlb_ptw(void *code_ptr, int mmu)
{
    TRANSLATION_DATA *td = lsenv->tr_data;

    la_out2i(LISA_ST_D, &la_ra, &la_env,
            offsetof(CPUX86State, save_regs[1]));

    int offset = td->ir2_asm_nr << 2;
    int ins_offset = 0;
    if (mmu == MMU_USER_IDX) {
        ins_offset = GET_SC_TABLE(1, cp_check_ptw_user)
                     - (int64_t)code_ptr - offset;
    } else if (mmu == MMU_KNOSMAP_IDX) {
        ins_offset = GET_SC_TABLE(0, cp_check_ptw_knosmap)
                     - (int64_t)code_ptr - offset;
    } else {
        g_assert_not_reached();
    }
    ins_offset = ins_offset >> 2;
    la_outa(LISA_BL, ins_offset);

    la_out2i(LISA_LD_D, &la_ra, &la_env,
            offsetof(CPUX86State, save_regs[1]));
}

static void tr_context_switch(void *code_ptr, int mmu, int n)
{
    TRANSLATION_DATA *td = lsenv->tr_data;

    int offset = td->ir2_asm_nr << 2;
    int ins_offset = 0;
    int rid = (mmu == MMU_USER_IDX) ? 1 : 0;
    if (n) {
        ins_offset = GET_SC_TABLE(rid, cs_native_to_bt_ret_n1)
                     - (int64_t)code_ptr - offset;
    } else {
        ins_offset = GET_SC_TABLE(rid, cs_native_to_bt_ret_n0)
                     - (int64_t)code_ptr - offset;
    }
    ins_offset = ins_offset >> 2;
    la_outa(LISA_B, ins_offset);
}

static void gen_intcheck(IR2_OPND *tmp, IR2_OPND *label_exit)
{
    int intoff = (int)offsetof(X86CPU, neg.icount_decr.u32) -
                 (int)offsetof(X86CPU, env);
    la_out2i(LISA_LD_W, tmp, &la_env, intoff);
    la_out3(LISA_BLT, tmp, &la_zero, label_exit);
}

int gen_latxs_cross_page_check(void *code_ptr,
        int n, int mmu, int mode, int ptw)
{
    TRANSLATION_DATA *td = lsenv->tr_data; (void)td;
    int code_nr = 0;

    /*
     * mode[0] = 1 : check page 0
     * mode[1] = 1 : check page 1
     */

    latxs_tr_init(NULL);

    IR2_OPND tb = latxs_ra_alloc_dbt_arg1(); /* t8($24) */
    IR2_OPND t0 = la_new_itmp();
    IR2_OPND t1 = la_new_itmp();
    IR2_OPND t2 = la_new_itmp();
    IR2_OPND t3 = la_new_itmp(); (void)t3;
    IR2_OPND t4 = la_new_itmp(); (void)t4;

    IR2_OPND itlb = la_a0;

    IR2_OPND label_exit = la_new_label();

    gen_intcheck(&t0, &label_exit);

#ifdef CONFIG_LATX_PRINT
    if (mmu == MMU_KNOSMAP_IDX) cp_counter_la_out_inc(t2, t3, cp_num[0]);
    if (mmu == MMU_USER_IDX) cp_counter_la_out_inc(t2, t3, cp_num[1]);
#endif

    /* 1. load next tb */
    if (split_tb_base_ok()) {
        la_out1i(LISA_LU32I_D, &tb, SPLIT_TB_BASE >> 32);
    }
    if (hamt_enabled()) latxs_tr_dmw_in_fix_mem(&tb);

#ifdef CONFIG_LATX_PRINT
    if (option_cross_page_counter) {
        /* The NULL successor check is shared with the normal path above. */
    }

    if (mmu == MMU_KNOSMAP_IDX) cp_counter_la_out_inc(t2, t3, cp_tb_hit[0]);
    if (mmu == MMU_USER_IDX) cp_counter_la_out_inc(t2, t3, cp_tb_hit[1]);
#endif

    int page0_need_ptw = 0;
    int page1_need_ptw = 0;

    IR2_OPND itlb_miss0 = la_new_label();
    IR2_OPND itlb_ok0 = la_new_label();
    IR2_OPND itlb_miss1 = la_new_label();
    IR2_OPND itlb_ok1 = la_new_label();

    /* 2. check page 0 */
    if (mode & 0x1) {
        if (ptw) {
            assert(option_fast_itlb_ptw);
            page0_need_ptw = 1;
            /* itlb hit */
#ifdef CONFIG_LATX_PRINT
            if (mmu == MMU_KNOSMAP_IDX) cp_counter_la_out_inc(t2, t3, cp_itlb_hit[0]);
            if (mmu == MMU_USER_IDX) cp_counter_la_out_inc(t2, t3, cp_itlb_hit[1]);
#endif
            la_out2i(LISA_LD_D, &t2, &tb,
                     TB_NEXT_DATA_OFF(page_addr_cache[0], n));
            la_out3(LISA_BNE, &t2, &itlb, &itlb_miss0);
            la_label_name(itlb_ok0, "fastitlb page 0 ok");
        } else {
#ifdef CONFIG_LATX_PRINT
            if (mmu == MMU_KNOSMAP_IDX) cp_counter_la_out_inc(t2, t3, cp_itlb_hit[0]);
            if (mmu == MMU_USER_IDX) cp_counter_la_out_inc(t2, t3, cp_itlb_hit[1]);
#endif
            la_out2i(LISA_LD_D, &t2, &tb,
                     TB_NEXT_DATA_OFF(page_addr_cache[0], n));
            la_out3(LISA_BNE, &t2, &itlb, &label_exit);
        }
    }

    /* 3. check page 1 */
    IR2_OPND label_no_page1 = la_new_label();
    if ((mode & 0x3) == 3) {
        la_out2i(LISA_LD_D, &t2, &tb,
                 TB_NEXT_DATA_OFF(page_addr_cache[1], n));
        la_out3(LISA_BEQ, &t2, &la_zero, &label_no_page1);
    }

    if (mode & 0x2) {
        la_out2i(LISA_LD_D, &t0, &tb, TB_NEXT_DATA_OFF(pc, n));
        la_imm(&t1, 0x1000);
        la_out3(LISA_ADD_D, &t0, &t0, &t1); la_comment("pc2 = pc + 4K");
        if (ptw) {
            page1_need_ptw = 1;
            if (option_fast_itlb) {
                tr_lookup_code_fast_itlb(&t1, &t0, mmu, itlb_miss1);
            } else {
                printf("%s:%d todo\n", __func__, __LINE__); exit(-1);
            }
            la_out2i(LISA_LD_D, &t2, &tb,
                     TB_NEXT_DATA_OFF(page_addr_cache[1], n));
            la_out3(LISA_BNE, &t2, &t1, &itlb_miss1);
            la_label_name(itlb_ok1, "fastitlb page 1 ok");
        } else {
            if (option_fast_itlb) {
                tr_lookup_code_fast_itlb(&t1, &t0, mmu, label_exit);
            } else {
                printf("%s:%d todo\n", __func__, __LINE__); exit(-1);
            }
            la_out2i(LISA_LD_D, &t2, &tb,
                     TB_NEXT_DATA_OFF(page_addr_cache[1], n));
            la_out3(LISA_BNE, &t2, &t1, &label_exit);
        }
    }

    if ((mode & 0x3) == 3) la_label(label_no_page1);

    /* 4. ok, return */
#ifdef CONFIG_LATX_PRINT
    if (mmu == MMU_KNOSMAP_IDX) cp_counter_la_out_inc(t2, t3, cp_check_hit[0]);
    if (mmu == MMU_USER_IDX) cp_counter_la_out_inc(t2, t3, cp_check_hit[1]);
#endif
//    if (hamt_enabled()) latxs_tr_dmw_in_fix_mem(&la_ra);
    la_out0_(lisa_return);

    /* 0. fail, return ra + 12, bypass the tblink, go to context switch */
    la_label_name(label_exit, "cp check fail !!!");
//    if (hamt_enabled()) latxs_tr_dmw_in_fix_mem(&la_ra);
//    la_out2i(LISA_ADDI_D, &la_ra, &la_ra, 12);
//    la_out0_(lisa_return);
    tr_context_switch(code_ptr, mmu, n);

    /* ====== PTW for itlb ====== */

    if (page0_need_ptw) {
        la_label_name(itlb_miss0, "fastitlb ptw page 0");

        la_out2i(LISA_LD_D, &t0, &tb, TB_NEXT_DATA_OFF(pc, n));

        tr_do_fastitlb_ptw(code_ptr, mmu);

        la_out2i(LISA_LD_D, &t1, &tb,
                 TB_NEXT_DATA_OFF(page_addr_ptw[0], n));
        la_out3 (LISA_BNE, &t1, &t3, &label_exit);
#ifdef CONFIG_LATX_PRINT
        if (mmu == MMU_KNOSMAP_IDX) cp_counter_la_out_inc(t2, t3, cp_ptw_hit[0]);
        if (mmu == MMU_USER_IDX) cp_counter_la_out_inc(t2, t3, cp_ptw_hit[1]);
#endif
        la_out1(LISA_B, &itlb_ok0);
    }

    if (page1_need_ptw) {
        la_label_name(itlb_miss1, "fastitlb ptw page 1");

        tr_do_fastitlb_ptw(code_ptr, mmu);

        la_out2i(LISA_LD_D, &t1, &tb,
                 TB_NEXT_DATA_OFF(page_addr_ptw[1], n));
        la_out3 (LISA_BNE, &t1, &t3, &label_exit);
#ifdef CONFIG_LATX_PRINT
        if (mmu == MMU_KNOSMAP_IDX) cp_counter_la_out_inc(t2, t3, cp_ptw_hit[0]);
        if (mmu == MMU_USER_IDX) cp_counter_la_out_inc(t2, t3, cp_ptw_hit[1]);
#endif
        la_out1(LISA_B, &itlb_ok1);
    }

    code_nr = latxs_tr_ir2_assemble(code_ptr);
    latxs_tr_fini();

    return code_nr;
}

#endif /* CONFIG_SOFTMMU */
