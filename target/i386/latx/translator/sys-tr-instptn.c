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
#include "sys-excp.h"
#include "latx-sys-inst-ptn.h"
#include "tb-ir1.h"
#ifdef CONFIG_LATX_TU
#include "latx-tu.h"
#endif
#include "latxs-imm-cache.h"
#include "latx-sys-flag-rdtn.h"

#ifdef LATXS_INSTPTN_ENABLE

#define WRAP(ins) (X86_INS_##ins)

#define LOAD_TB_PTR_TO_DBT_ARG1() do {                      \
    if (!option_lsfpu && !option_soft_fpu) {                \
        IR2_OPND tb_ptr_opnd = latxs_ra_alloc_dbt_arg1();   \
        latxs_tr_gen_exit_tb_load_tb_addr(&tb_ptr_opnd,     \
                (ADDR)lsenv->tr_data->curr_tb);             \
    }                                                       \
} while (0)

static IR2_OPND prepare_cmp_sf(IR2_OPND *src0, IR2_OPND *src1, int os)
{
    IR2_OPND result = latxs_ra_alloc_itemp();

    lsassert(os == 8 || os == 16 || os == 32 || os == 64);
    latxs_append_ir2_opnd3(LISA_SUB_D, &result, src0, src1);
    if (os != 64) {
        IR2_OPND truncated = latxs_ra_alloc_itemp();

        latxs_append_ir2_opnd2_(os == 32 ? lisa_mov32s :
                                os == 16 ? lisa_mov16s : lisa_mov8s,
                                &truncated, &result);
        result = truncated;
    }

    return result;
}

static
bool translate_cmp_jcc(IR1_INST *pir1)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    IR1_INST *list = td->ir1_inst_array;
    IR1_INST *nir1 = NULL;

#ifdef CONFIG_LATX_TU
    TranslationBlock *tb = td->curr_tb;
    TranslationBlock *tu_next_tb = tb->s_data->next_tb[TU_TB_INDEX_NEXT];
    TranslationBlock *tu_target_tb = tb->s_data->next_tb[TU_TB_INDEX_TARGET];
#endif

    lsassert(pir1->instptn.next >= 0);

    nir1 = ir1_inst_in_list(list, pir1->instptn.next);

#if 0
    latxs_translate_cmp_byhand(pir1);
    switch (ir1_opcode(nir1)) {
    case WRAP(JB):  latxs_translate_jb(nir1); break;
    case WRAP(JAE): latxs_translate_jae(nir1); break;
    case WRAP(JE):  latxs_translate_jz(nir1); break;
    case WRAP(JNE): latxs_translate_jnz(nir1); break;
    case WRAP(JBE): latxs_translate_jbe(nir1); break;
    case WRAP(JA):  latxs_translate_ja(nir1); break;
    case WRAP(JL):  latxs_translate_jl(nir1); break;
    case WRAP(JGE): latxs_translate_jge(nir1); break;
    case WRAP(JLE): latxs_translate_jle(nir1); break;
    case WRAP(JG):  latxs_translate_jg(nir1); break;
    default:        lsassert(0);        break;
    }
    return true;
#endif

#if 0
    ir1_dump(pir1);
    ir1_dump(nir1);
    fprintf(stderr, "---------------\n");
#endif

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    int tu_next_eflag_use = 1;
#ifdef CONFIG_LATX_TU
    if (td->is_tu_mode && flag_reduction_tu_cmp_jcc() &&
        tu_next_tb    && tu_next_tb->s_data->eflag_use == 0 &&
        tu_target_tb  && tu_target_tb->s_data->eflag_use == 0)
    {
        tu_next_eflag_use = 0;
    }
#endif

    int tu_eflag_opt = 0;
#ifdef CONFIG_LATX_TU
    TU_EFLAG_OPT_RESET(tu_data);
    if (td->is_tu_mode && tu_next_eflag_use == 1 &&
        flag_reduction_tu_cmp_jcc() && flag_reduction_tu_next() &&
        tu_next_tb && tu_next_tb->s_data->eflag_use != 0 &&
        tb_is_adjacent(tb, tu_next_tb) &&
        tu_target_tb && tu_target_tb->s_data->eflag_use == 0)
    {
        tu_eflag_opt = 1;
    }
#endif

    EXMode em = EXMode_S;

    IR2_OPND src0, src1;
    if (ir1_opnd_is_gpr(opnd0)) {
        if (ir1_opnd_size(opnd0) == 64) {
            src0 = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd0));
        } else {
            src0 = latxs_ra_alloc_itemp();
            latxs_load_ir1_gpr_to_ir2(&src0, opnd0, em);
        }
        if (ir1_opnd_is_gpr(opnd1)) { /* gpr, gpr */
            if (ir1_opnd_size(opnd1) == 64) {
                src1 = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd1));
            } else {
                src1 = latxs_ra_alloc_itemp();
                latxs_load_ir1_gpr_to_ir2(&src1, opnd1, em);
            }
        } else if (ir1_opnd_is_imm(opnd1)) { /* gpr, imm */
            if (ir1_opnd_simm(opnd1) == 0) {
                src1 = latxs_zero_ir2_opnd;
            } else {
                src1 = latxs_ra_alloc_itemp();
                latxs_load_imm64(&src1, ir1_opnd_simm(opnd1));
            }
        } else { /* gpr, mem */
            src1 = latxs_ra_alloc_itemp();
            latxs_load_ir1_mem_to_ir2(&src1, opnd1, em, -1);
        }
    } else {
        lsassert(ir1_opnd_is_mem(opnd0));
        src0 = latxs_ra_alloc_itemp();
        latxs_load_ir1_mem_to_ir2(&src0, opnd0, em, -1);
        if (ir1_opnd_is_gpr(opnd1)) { /* mem, gpr */
            if (ir1_opnd_size(opnd1) == 64) {
                src1 = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd1));
            } else {
                src1 = latxs_ra_alloc_itemp();
                latxs_load_ir1_gpr_to_ir2(&src1, opnd1, em);
            }
        } else { /* mem, imm */
            if (ir1_opnd_simm(opnd1) == 0) {
                src1 = latxs_zero_ir2_opnd;
            } else {
                src1 = latxs_ra_alloc_itemp();
                latxs_load_imm64(&src1, ir1_opnd_simm(opnd1));
            }
        }
    }

    IR2_OPND target_label = latxs_ir2_opnd_new_label();
    IR2_OPCODE jcc_op = LISA_INVALID;

    int swap_opnd = 0;
    int test_sf = 0;
    switch (ir1_opcode(nir1)) {
    case WRAP(JB):  jcc_op = LISA_BLTU; break;
    case WRAP(JAE): jcc_op = LISA_BGEU; break;
    case WRAP(JE):  jcc_op = LISA_BEQ;  break;
    case WRAP(JNE): jcc_op = LISA_BNE;  break;
    case WRAP(JBE): jcc_op = LISA_BGEU; swap_opnd = 1; break;
    case WRAP(JA):  jcc_op = LISA_BLTU; swap_opnd = 1; break;
    case WRAP(JL):  jcc_op = LISA_BLT;  break;
    case WRAP(JGE): jcc_op = LISA_BGE;  break;
    case WRAP(JLE): jcc_op = LISA_BGE;  swap_opnd = 1; break;
    case WRAP(JG):  jcc_op = LISA_BLT;  swap_opnd = 1; break;
    case WRAP(JS):
        jcc_op = LISA_BLT;
        test_sf = 1;
        break;
    case WRAP(JNS):
        jcc_op = LISA_BGE;
        test_sf = 1;
        break;
    default:        lsassert(0);        break;
    }

    if (tu_next_eflag_use == 1) {
        if (tu_eflag_opt) {
#ifdef CONFIG_LATX_TU
            TU_EFLAG_OPT_SET(tu_data);
            int os = ir1_opnd_size(opnd0); /* 8, 16, 32, 64 */
            tu_data->eflag_opt_data.ef_opcode = LISA_X86SUB_B + (os >> 4) - (os >> 6);
            tu_data->eflag_opt_data.ef_opnd0 = src0;
            tu_data->eflag_opt_data.ef_opnd1 = src1;
#endif
        } else {
            LATXS_FLAG_REDUCTION_CHECKIF(pir1) {
                int os = ir1_opnd_size(opnd0); /* 8, 16, 32, 64 */
                latxs_append_ir2_opnd2(LISA_X86SUB_B + (os >> 4) - (os >> 6), &src0, &src1);
            }
        }
    }

    IR2_OPND jcc_src0 = src0;
    IR2_OPND jcc_src1 = src1;
    if (test_sf) {
        /* JS/JNS test the subtraction result's SF, independently of OF. */
        jcc_src0 = prepare_cmp_sf(&src0, &src1, ir1_opnd_size(opnd0));
        jcc_src1 = latxs_zero_ir2_opnd;
    }

    latxs_tr_gen_eob();
    LOAD_TB_PTR_TO_DBT_ARG1();

#ifdef CONFIG_LATX_TU
    if (swap_opnd) {
        GEN_TU_FUN_AND_RETURN2(jcc_op, jcc_src1, jcc_src0, target_label, nir1);
    } else {
        GEN_TU_FUN_AND_RETURN2(jcc_op, jcc_src0, jcc_src1, target_label, nir1);
    }
#endif
    if (swap_opnd) {
        latxs_append_ir2_opnd3(jcc_op, &jcc_src1, &jcc_src0, &target_label);
    } else {
        latxs_append_ir2_opnd3(jcc_op, &jcc_src0, &jcc_src1, &target_label);
    }

    /* not taken */
    latxs_tr_generate_exit_tb(nir1, 0);

    latxs_append_ir2_opnd1(LISA_LABEL, &target_label);

    /* taken */
    latxs_tr_generate_exit_tb(nir1, 1);

    return true;

#if 0
pattern_fail:
    pir1->instptn.opc = INSTPTN_OPC_NONE;
    nir1->instptn.opc = INSTPTN_OPC_NONE;
    return false;
#endif
}

static bool ir1_is_same_opnd(IR1_OPND *opnd0, IR1_OPND *opnd1)
{
    if (ir1_opnd_is_same_reg(opnd0, opnd1)) {
        if (opnd0->reg != X86_REG_RIP && opnd0->reg != X86_REG_EIP)
            return true;
    } else if (ir1_opnd_is_mem(opnd0) && ir1_opnd_is_mem(opnd1)) {
        if (ir1_opnd_size(opnd0) == ir1_opnd_size(opnd1) &&
            opnd0->mem.base == opnd1->mem.base &&
            opnd0->mem.index == opnd1->mem.index &&
            opnd0->mem.segment == opnd1->mem.segment &&
            opnd0->mem.scale == opnd1->mem.scale &&
            opnd0->mem.disp == opnd1->mem.disp) {
            if (!ir1_opnd_is_pc_relative(opnd0))
                return true;
        }
    }
    return false;
}

static
bool translate_cmp_xxcc_con(IR1_INST *pir1)
{
    IR1_INST *curr = pir1;
    TRANSLATION_DATA *td = lsenv->tr_data;
    IR1_INST *list = td->ir1_inst_array;

    lsassert(curr->instptn.next >= 0);

    IR1_OPND *opnd0 = ir1_get_opnd(curr, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(curr, 1);

    EXMode em = EXMode_S;

    IR2_OPND src0, src1;
    if (ir1_opnd_is_gpr(opnd0)) {
        if (ir1_opnd_size(opnd0) == 64) {
            src0 = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd0));
        } else {
            src0 = latxs_ra_alloc_itemp();
            latxs_load_ir1_gpr_to_ir2(&src0, opnd0, em);
        }
        if (ir1_opnd_is_gpr(opnd1)) { /* gpr, gpr */
            if (ir1_opnd_size(opnd1) == 64) {
                src1 = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd1));
            } else {
                src1 = latxs_ra_alloc_itemp();
                latxs_load_ir1_gpr_to_ir2(&src1, opnd1, em);
            }
        } else if (ir1_opnd_is_imm(opnd1)) { /* gpr, imm */
            if (ir1_opnd_simm(opnd1) == 0) {
                src1 = latxs_zero_ir2_opnd;
            } else {
                src1 = latxs_ra_alloc_itemp();
                latxs_load_imm64(&src1, ir1_opnd_simm(opnd1));
            }
        } else { /* gpr, mem */
            src1 = latxs_ra_alloc_itemp();
            latxs_load_ir1_mem_to_ir2(&src1, opnd1, em, -1);
        }
    } else {
        lsassert(ir1_opnd_is_mem(opnd0));
        src0 = latxs_ra_alloc_itemp();
        latxs_load_ir1_mem_to_ir2(&src0, opnd0, em, -1);
        if (ir1_opnd_is_gpr(opnd1)) { /* mem, gpr */
            if (ir1_opnd_size(opnd1) == 64) {
                src1 = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd1));
            } else {
                src1 = latxs_ra_alloc_itemp();
                latxs_load_ir1_gpr_to_ir2(&src1, opnd1, em);
            }
        } else { /* mem, imm */
            if (ir1_opnd_simm(opnd1) == 0) {
                src1 = latxs_zero_ir2_opnd;
            } else {
                src1 = latxs_ra_alloc_itemp();
                latxs_load_imm64(&src1, ir1_opnd_simm(opnd1));
            }
        }
    }

    int src0_change = 0;
    int src1_change = 0;

    LATXS_FLAG_REDUCTION_CHECKIF(pir1) {
        int os = ir1_opnd_size(opnd0); /* 8, 16, 32, 64 */
        latxs_append_ir2_opnd2(LISA_X86SUB_B + (os >> 4) - (os >> 6), &src0, &src1);
    }

    for (; curr->instptn.next >= 0;) {
        curr = ir1_inst_in_list(list, curr->instptn.next);
        lsenv->tr_data->curr_ir1_inst = curr;
        latxs_append_ir2_opnda(LISA_X86_INST, ir1_addr(curr));

        IR1_OPND *next_opnd0 = ir1_get_opnd(curr, 0);
        if (!src0_change && ir1_opnd_is_gpr(opnd0) && ir1_opnd_is_gpr(next_opnd0) &&
            ir1_opnd_is_same_reg_without_width(opnd0, next_opnd0)) {
            src0_change = 1;
            IR2_OPND src0_t = latxs_ra_alloc_itemp();
            latxs_append_ir2_opnd3(LISA_OR, &src0_t, &latxs_zero_ir2_opnd, &src0);
            latxs_ra_free_temp(&src0);
            src0 = src0_t;
        }
        if (!src1_change && ir1_opnd_is_gpr(opnd1) && ir1_opnd_is_gpr(next_opnd0) &&
            ir1_opnd_is_same_reg_without_width(opnd1, next_opnd0)) {
            src1_change = 1;
            IR2_OPND src1_t = latxs_ra_alloc_itemp();
            latxs_append_ir2_opnd3(LISA_OR, &src1_t, &latxs_zero_ir2_opnd, &src1);
            latxs_ra_free_temp(&src1);
            src1 = src1_t;
        }

        IR2_OPCODE xxcc_op = LISA_INVALID;
        int swap_opnd = 0;
        int is_cmovcc = 0;
        switch (ir1_opcode(curr)) {
        case WRAP(CMOVB):
            is_cmovcc = 1;  __attribute__((fallthrough));
        case WRAP(SETB):
            xxcc_op = LISA_BLTU; break;
        case WRAP(CMOVAE):
            is_cmovcc = 1;  __attribute__((fallthrough));
        case WRAP(SETAE):
            xxcc_op = LISA_BGEU; break;
        case WRAP(CMOVE):
            is_cmovcc = 1;  __attribute__((fallthrough));
        case WRAP(SETE):
            xxcc_op = LISA_BEQ;  break;
        case WRAP(CMOVNE):
            is_cmovcc = 1;  __attribute__((fallthrough));
        case WRAP(SETNE):
            xxcc_op = LISA_BNE;  break;
        case WRAP(CMOVBE):
            is_cmovcc = 1;  __attribute__((fallthrough));
        case WRAP(SETBE):
            xxcc_op = LISA_BGEU; swap_opnd = 1; break;
        case WRAP(CMOVA):
            is_cmovcc = 1;  __attribute__((fallthrough));
        case WRAP(SETA):
            xxcc_op = LISA_BLTU; swap_opnd = 1; break;
        case WRAP(CMOVL):
            is_cmovcc = 1;  __attribute__((fallthrough));
        case WRAP(SETL):
            xxcc_op = LISA_BLT;  break;
        case WRAP(CMOVGE):
            is_cmovcc = 1;  __attribute__((fallthrough));
        case WRAP(SETGE):
            xxcc_op = LISA_BGE;  break;
        case WRAP(CMOVLE):
            is_cmovcc = 1;  __attribute__((fallthrough));
        case WRAP(SETLE):
            xxcc_op = LISA_BGE;  swap_opnd = 1; break;
        case WRAP(CMOVG):
            is_cmovcc = 1;  __attribute__((fallthrough));
        case WRAP(SETG):
            xxcc_op = LISA_BLT;  swap_opnd = 1; break;
        default:        lsassert(0);        break;
        }

        if (is_cmovcc) {
            IR1_OPND *next_opnd1 = ir1_get_opnd(curr, 1);
            IR2_OPND dest_opnd = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(next_opnd0));

            IR2_OPND *src_opnd_t = NULL;
            if (ir1_is_same_opnd(next_opnd1, opnd0) && !src0_change) {
                src_opnd_t = &src0;
            } else if (ir1_is_same_opnd(next_opnd1, opnd1) && !src1_change) {
                src_opnd_t = &src1;
            }

            IR2_OPND src_opnd;
            int free_src_opnd = 0;
            if (src_opnd_t) {
                src_opnd = *src_opnd_t;
            } else if (ir1_opnd_is_gpr(next_opnd1) &&
                       ir1_opnd_size(next_opnd1) == 64) {
                src_opnd = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(next_opnd1));
            } else {
                src_opnd = latxs_ra_alloc_itemp();
                if (ir1_opnd_is_mem(next_opnd1)) {
                    int os = ir1_opnd_size(opnd0);
                    latxs_append_ir2_opnd2(
                        LISA_X86SUB_B + (os >> 4) - (os >> 6),
                        &src0, &src1);
                }
                latxs_load_ir1_to_ir2(&src_opnd, next_opnd1, EXMode_S);
                free_src_opnd = 1;
            }

            IR2_OPND condition = latxs_ra_alloc_itemp();
            int condition_is_zero = 0;
            switch (xxcc_op) {
            case LISA_BLTU:
                xxcc_op = LISA_SLTU;
                break;
            case LISA_BGEU:
                xxcc_op = LISA_SLTU;
                condition_is_zero = 1;
                break;
            case LISA_BLT:
                xxcc_op = LISA_SLT;
                break;
            case LISA_BGE:
                xxcc_op = LISA_SLT;
                condition_is_zero = 1;
                break;
            case LISA_BEQ:
                xxcc_op = LISA_SUB_D;
                condition_is_zero = 1;
                break;
            case LISA_BNE:
                xxcc_op = LISA_SUB_D;
                break;
            default:
                lsassert(0);
                break;
            }

            if (swap_opnd) {
                latxs_append_ir2_opnd3(xxcc_op, &condition, &src1, &src0);
            } else {
                latxs_append_ir2_opnd3(xxcc_op, &condition, &src0, &src1);
            }

            IR2_OPND old_dest = latxs_ra_alloc_itemp();
            if (condition_is_zero) {
                latxs_append_ir2_opnd3(LISA_MASKEQZ,
                        &old_dest, &dest_opnd, &condition);
                latxs_append_ir2_opnd3(LISA_MASKNEZ,
                        &condition, &src_opnd, &condition);
            } else {
                latxs_append_ir2_opnd3(LISA_MASKNEZ,
                        &old_dest, &dest_opnd, &condition);
                latxs_append_ir2_opnd3(LISA_MASKEQZ,
                        &condition, &src_opnd, &condition);
            }

            if (ir1_opnd_size(next_opnd0) == 64) {
                latxs_append_ir2_opnd3(LISA_OR,
                        &dest_opnd, &old_dest, &condition);
            } else {
                latxs_append_ir2_opnd3(LISA_OR,
                        &old_dest, &old_dest, &condition);
                latxs_store_ir2_to_ir1(&old_dest, next_opnd0);
            }

            latxs_ra_free_temp(&old_dest);
            latxs_ra_free_temp(&condition);
            if (free_src_opnd) {
                latxs_ra_free_temp(&src_opnd);
            }
        } else {
            /* Materialize the existing comparison predicate as 0/1.  Keep
             * CMP's flag handling and the original byte-store helper. */
            IR2_OPND condition = latxs_ra_alloc_itemp();
            IR2_OPCODE condition_op = LISA_INVALID;
            int invert_condition = 0;
            int equality_condition = 0;
            switch (xxcc_op) {
            case LISA_BEQ:
            case LISA_BNE:
                condition_op = LISA_SUB_D;
                equality_condition = 1;
                invert_condition = xxcc_op == LISA_BEQ;
                break;
            case LISA_BLTU:
            case LISA_BGEU:
                condition_op = LISA_SLTU;
                invert_condition = xxcc_op == LISA_BGEU;
                break;
            case LISA_BLT:
            case LISA_BGE:
                condition_op = LISA_SLT;
                invert_condition = xxcc_op == LISA_BGE;
                break;
            default:
                lsassert(0);
                break;
            }

            if (swap_opnd) {
                latxs_append_ir2_opnd3(condition_op, &condition, &src1, &src0);
            } else {
                latxs_append_ir2_opnd3(condition_op, &condition, &src0, &src1);
            }
            if (equality_condition) {
                latxs_append_ir2_opnd3(LISA_SLTU, &condition,
                        &latxs_zero_ir2_opnd, &condition);
            }
            if (invert_condition) {
                latxs_append_ir2_opnd2i(LISA_XORI, &condition, &condition, 1);
            }
            latxs_store_ir2_to_ir1(&condition, next_opnd0);
            latxs_ra_free_temp(&condition);
        }
    }

    return true;
}


static
bool translate_test_xxcc_con(IR1_INST *pir1)
{
    IR1_INST *curr = pir1;
    TRANSLATION_DATA *td = lsenv->tr_data;
    IR1_INST *list = td->ir1_inst_array;

#ifdef CONFIG_LATX_TU
    uint32_t test_xxcc_con_ptn_mask = 0;
    TranslationBlock *tb = td->curr_tb;
    TranslationBlock *tu_next_tb = tb->s_data->next_tb[TU_TB_INDEX_NEXT];
    TranslationBlock *tu_target_tb = tb->s_data->next_tb[TU_TB_INDEX_TARGET];
#endif

    lsassert(pir1->instptn.next >= 0);

    uint8_t live_flags = ir1_get_eflag_def(pir1);
    int curr_idx = pir1->instptn.next;
    IR1_INST *last_ptn_ir1 = pir1;
    while (curr_idx >= 0) {
        last_ptn_ir1 = ir1_inst_in_list(list, curr_idx);
        curr_idx = last_ptn_ir1->instptn.next;
    }
    for (int i = last_ptn_ir1 - list + 1; live_flags && i < td->ir1_nr; i++) {
        IR1_INST *next_ir1 = ir1_inst_in_list(list, i);
        if (ir1_get_eflag_use(next_ir1) & live_flags) {
            live_flags = 0;
            break;
        }
        live_flags &= ~ir1_get_eflag_def(next_ir1);
    }

    int tu_next_eflag_use = 1;
#ifdef CONFIG_LATX_TU
    if (live_flags &&
        td->is_tu_mode && flag_reduction_tu_test_xxcc_con() &&
#ifdef CONFIG_SOFTMMU
        (tb->flags & HF_HAMT_EN_MASK) &&
#endif
        tu_next_tb && tu_next_tb->s_data->eflag_use == 0 &&
        (!tu_target_tb || tu_target_tb->s_data->eflag_use == 0))
    {
        tu_next_eflag_use = 0;
    }
#endif

    int tu_eflag_opt = 0;
#ifdef CONFIG_LATX_TU
    TU_EFLAG_OPT_RESET(tu_data);
    if (live_flags &&
        td->is_tu_mode && tu_next_eflag_use == 1 &&
        flag_reduction_tu_test_xxcc_con() && flag_reduction_tu_next() &&
#ifdef CONFIG_SOFTMMU
        (tb->flags & HF_HAMT_EN_MASK) &&
#endif
        tu_next_tb && tu_next_tb->s_data->eflag_use != 0 &&
        tb_is_adjacent(tb, tu_next_tb) &&
        (!tu_target_tb || tu_target_tb->s_data->eflag_use == 0))
    {
        tu_eflag_opt = 1;
    }

    if (td->avoid_tu_eflag_opt) {
        tu_next_eflag_use = 1;
        tu_eflag_opt = 0;
    }
#endif

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);
    IR2_OPND src0;
    IR2_OPND src1;
    IR2_OPND temp;
    if (ir1_opnd_is_gpr(opnd0) && ir1_opnd_size(opnd0) == 64) {
        src0 = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd0));
    } else {
        src0 = latxs_ra_alloc_itemp();
        latxs_load_ir1_to_ir2(&src0, opnd0, EXMode_S);
    }
    int src0_change = 0;
    int src1_change = 0;
    int is_same_reg = ir1_opnd_is_same_reg(opnd0, opnd1);
    int os = ir1_opnd_size(opnd0); /* 8, 16, 32, 64 */
    if (is_same_reg) {
        LATXS_FLAG_REDUCTION_CHECKIF(pir1) {
#ifdef CONFIG_LATX_PROFILER
            TranslationBlock *tb = lsenv->tr_data->curr_tb;
            per_tb_count((void *)&((tb->profile).eflags_num), 1);
#endif
            if (tu_next_eflag_use == 1 && !tu_eflag_opt) {
                latxs_append_ir2_opnd2(
                    LISA_X86AND_B + (os >> 4) - (os >> 6),
                    &src0, &src0);
            }
        }
    } else {
        if (ir1_opnd_is_gpr(opnd1) &&
            ir1_opnd_size(opnd1) == 64) {
            src1 = latxs_ra_alloc_gpr(
                ir1_opnd_base_reg_num(opnd1));
        } else {
            src1 = latxs_ra_alloc_itemp();
            latxs_load_ir1_to_ir2(&src1, opnd1, EXMode_S);
        }
        LATXS_FLAG_REDUCTION_CHECKIF(pir1) {
#ifdef CONFIG_LATX_PROFILER
            TranslationBlock *tb = lsenv->tr_data->curr_tb;
            per_tb_count((void *)&((tb->profile).eflags_num), 1);
#endif
            if (tu_next_eflag_use == 1 && !tu_eflag_opt) {
                latxs_append_ir2_opnd2(
                    LISA_X86AND_B + (os >> 4) - (os >> 6),
                    &src0, &src1);
            }
        }
        temp = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd3(LISA_AND, &temp, &src0, &src1);
    }

#ifdef CONFIG_LATX_TU
    if (live_flags && (!tu_next_eflag_use || tu_eflag_opt)) {
        IR2_OPND *cond = is_same_reg ? &src0 : &temp;
        uint32_t old_ptn_itemp_mask = td->ptn_itemp_mask;

        td->ptn_itemp0 = latxs_ra_alloc_ptn_itemp();
        test_xxcc_con_ptn_mask =
            td->ptn_itemp_mask & ~old_ptn_itemp_mask;
        latxs_append_ir2_opnd2_(lisa_mov, &td->ptn_itemp0, cond);

        if (last_ptn_ir1 != &list[td->ir1_nr - 1]) {
            fprintf(stderr,
                    "warning: test_xxcc_con ptn itemp crosses %ld "
                    "trailing ir1, tb=0x%lx test=0x%lx\n",
                    &list[td->ir1_nr - 1] - last_ptn_ir1,
                    tb->pc, ir1_addr(pir1));
        }

        if (tu_eflag_opt) {
            TU_EFLAG_OPT_SET(tu_data);
            tu_data->eflag_opt_data.ef_opcode =
                LISA_X86AND_B + (os >> 4) - (os >> 6);
            tu_data->eflag_opt_data.ef_opnd0 = td->ptn_itemp0;
            tu_data->eflag_opt_data.ef_opnd1 = td->ptn_itemp0;
        }

        tb->ptn.eflag_type = PTN_EFLAG_TYPE_X86AND;
        tb->ptn.os = os;
        tb->ptn.opnd0 = td->ptn_itemp0.val;
        tb->ptn.opnd1 = td->ptn_itemp0.val;
        tb_set_ptn_pcoffset(tb,
                ir1_addr(pir1) - (tb->pc - tb->cs_base));
    }
#endif

    for (; curr->instptn.next >= 0;) {
        curr = ir1_inst_in_list(list, curr->instptn.next);
        lsenv->tr_data->curr_ir1_inst = curr;
        latxs_append_ir2_opnda(LISA_X86_INST, ir1_addr(curr));

        IR1_OPND *next_opnd0 = ir1_get_opnd(curr, 0);
        if (!src0_change && ir1_opnd_is_gpr(opnd0) && ir1_opnd_is_gpr(next_opnd0) &&
            ir1_opnd_is_same_reg_without_width(opnd0, next_opnd0)) {
            src0_change = 1;
            IR2_OPND src0_t = latxs_ra_alloc_itemp();
            latxs_append_ir2_opnd3(LISA_OR, &src0_t, &latxs_zero_ir2_opnd, &src0);
            latxs_ra_free_temp(&src0);
            src0 = src0_t;
        }
        if (!is_same_reg && !src1_change && ir1_opnd_is_gpr(opnd1) &&
            ir1_opnd_is_gpr(next_opnd0) &&
            ir1_opnd_is_same_reg_without_width(opnd1, next_opnd0)) {
            src1_change = 1;
            IR2_OPND src1_t = latxs_ra_alloc_itemp();
            latxs_append_ir2_opnd3(LISA_OR, &src1_t, &latxs_zero_ir2_opnd, &src1);
            latxs_ra_free_temp(&src1);
            src1 = src1_t;
        }

        IR2_OPND *zero = &latxs_zero_ir2_opnd;
        int is_cmovcc = 0;
        switch (ir1_opcode(curr)) {
        case WRAP(CMOVE):
        case WRAP(CMOVNE):
        case WRAP(CMOVS):
        case WRAP(CMOVNS):
        case WRAP(CMOVLE):
        case WRAP(CMOVG):
        case WRAP(CMOVNO):
        case WRAP(CMOVO):
        case WRAP(CMOVB):
        case WRAP(CMOVBE):
        case WRAP(CMOVA):
        case WRAP(CMOVAE):
            is_cmovcc = 1;
            break;
        case WRAP(SETE):
        case WRAP(SETNE):
        case WRAP(SETS):
        case WRAP(SETNS):
        case WRAP(SETLE):
        case WRAP(SETG):
        case WRAP(SETNO):
        case WRAP(SETO):
        case WRAP(SETB):
        case WRAP(SETBE):
        case WRAP(SETA):
        case WRAP(SETAE):
            break;
        default:
            lsassert(0);
            break;
        }

        if (is_cmovcc) {
            IR1_OPND *next_opnd1 = ir1_get_opnd(curr, 1);
            IR2_OPND dest_opnd = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(next_opnd0));

            IR2_OPND *src_opnd_t = NULL;
            if (ir1_is_same_opnd(next_opnd1, opnd0) && !src0_change) {
                src_opnd_t = &src0;
            } else if (!is_same_reg &&
                       ir1_is_same_opnd(next_opnd1, opnd1) && !src1_change) {
                src_opnd_t = &src1;
            }

            IR2_OPND src_opnd;
            int free_src_opnd = 0;
            if (src_opnd_t) {
                src_opnd = *src_opnd_t;
            } else if (ir1_opnd_is_gpr(next_opnd1) &&
                       ir1_opnd_size(next_opnd1) == 64) {
                src_opnd = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(next_opnd1));
            } else {
                src_opnd = latxs_ra_alloc_itemp();
                if (ir1_opnd_is_mem(next_opnd1)) {
                    latxs_append_ir2_opnd2(
                        LISA_X86AND_B + (os >> 4) - (os >> 6),
                        &src0, is_same_reg ? &src0 : &src1);
                }
                latxs_load_ir1_to_ir2(&src_opnd, next_opnd1, EXMode_S);
                free_src_opnd = 1;
            }

            IR2_OPND *condition = is_same_reg ? &src0 : &temp;
            IR2_OPND condition_temp;
            int free_condition = 0;
            int condition_is_zero = 0;
            int constant_condition = -1;
            switch (ir1_opcode(curr)) {
            case WRAP(CMOVE):
            case WRAP(CMOVBE):
                condition_is_zero = 1;
                break;
            case WRAP(CMOVNE):
            case WRAP(CMOVA):
                break;
            case WRAP(CMOVS):
            case WRAP(CMOVNS):
                lsassert(is_same_reg);
                condition_temp = latxs_ra_alloc_itemp();
                latxs_append_ir2_opnd3(LISA_SLT,
                        &condition_temp, &src0, zero);
                condition = &condition_temp;
                free_condition = 1;
                condition_is_zero = ir1_opcode(curr) == WRAP(CMOVNS);
                break;
            case WRAP(CMOVLE):
            case WRAP(CMOVG):
                lsassert(is_same_reg);
                condition_temp = latxs_ra_alloc_itemp();
                latxs_append_ir2_opnd3(LISA_SLT,
                        &condition_temp, zero, &src0);
                condition = &condition_temp;
                free_condition = 1;
                condition_is_zero = ir1_opcode(curr) == WRAP(CMOVLE);
                break;
            case WRAP(CMOVNO):
            case WRAP(CMOVAE):
                constant_condition = 1;
                break;
            case WRAP(CMOVO):
            case WRAP(CMOVB):
                constant_condition = 0;
                break;
            default:
                lsassert(0);
                break;
            }

            if (constant_condition == 1) {
                if (ir1_opnd_size(next_opnd0) == 64) {
                    latxs_append_ir2_opnd3(LISA_OR,
                            &dest_opnd, zero, &src_opnd);
                } else {
                    latxs_store_ir2_to_ir1(&src_opnd, next_opnd0);
                }
            } else if (constant_condition == 0) {
                if (ir1_opnd_size(next_opnd0) == 32) {
                    latxs_append_ir2_opnd2_(lisa_mov32z,
                            &dest_opnd, &dest_opnd);
                }
            } else {
                IR2_OPND old_dest = latxs_ra_alloc_itemp();
                IR2_OPND new_value = latxs_ra_alloc_itemp();
                if (condition_is_zero) {
                    latxs_append_ir2_opnd3(LISA_MASKEQZ,
                            &old_dest, &dest_opnd, condition);
                    latxs_append_ir2_opnd3(LISA_MASKNEZ,
                            &new_value, &src_opnd, condition);
                } else {
                    latxs_append_ir2_opnd3(LISA_MASKNEZ,
                            &old_dest, &dest_opnd, condition);
                    latxs_append_ir2_opnd3(LISA_MASKEQZ,
                            &new_value, &src_opnd, condition);
                }

                if (ir1_opnd_size(next_opnd0) == 64) {
                    latxs_append_ir2_opnd3(LISA_OR,
                            &dest_opnd, &old_dest, &new_value);
                } else {
                    latxs_append_ir2_opnd3(LISA_OR,
                            &old_dest, &old_dest, &new_value);
                    latxs_store_ir2_to_ir1(&old_dest, next_opnd0);
                }

                latxs_ra_free_temp(&new_value);
                latxs_ra_free_temp(&old_dest);
            }

            if (free_condition) {
                latxs_ra_free_temp(&condition_temp);
            }
            if (free_src_opnd) {
                latxs_ra_free_temp(&src_opnd);
            }
        } else {
            IR2_OPND condition = latxs_ra_alloc_itemp();
            IR2_OPND *test_value = is_same_reg ? &src0 : &temp;
            int invert_condition = 0;

            switch (ir1_opcode(curr)) {
            case WRAP(SETE):
            case WRAP(SETBE):
                invert_condition = 1;
                __attribute__((fallthrough));
            case WRAP(SETNE):
            case WRAP(SETA):
                latxs_append_ir2_opnd3(LISA_SLTU,
                        &condition, zero, test_value);
                break;
            case WRAP(SETS):
            case WRAP(SETNS):
                lsassert(is_same_reg);
                latxs_append_ir2_opnd3(LISA_SLT,
                        &condition, &src0, zero);
                invert_condition = ir1_opcode(curr) == WRAP(SETNS);
                break;
            case WRAP(SETLE):
            case WRAP(SETG):
                lsassert(is_same_reg);
                latxs_append_ir2_opnd3(LISA_SLT,
                        &condition, zero, &src0);
                invert_condition = ir1_opcode(curr) == WRAP(SETLE);
                break;
            case WRAP(SETNO):
            case WRAP(SETAE):
                latxs_append_ir2_opnd2i(LISA_ORI, &condition, zero, 1);
                break;
            case WRAP(SETO):
            case WRAP(SETB):
                latxs_append_ir2_opnd2i(LISA_ORI, &condition, zero, 0);
                break;
            default:
                lsassert(0);
                break;
            }

            if (invert_condition) {
                latxs_append_ir2_opnd2i(LISA_XORI, &condition, &condition, 1);
            }
            latxs_store_ir2_to_ir1(&condition, next_opnd0);
            latxs_ra_free_temp(&condition);
        }
    }

#ifdef CONFIG_LATX_TU
    if (test_xxcc_con_ptn_mask &&
        !(td->ptn_itemp_mask & test_xxcc_con_ptn_mask)) {
        fprintf(stderr,
                "error: test_xxcc_con ptn itemp was reused, "
                "tb=0x%lx test=0x%lx reg=%d "
                "saved_mask=0x%x current_mask=0x%x\n",
                tb->pc, ir1_addr(pir1), td->ptn_itemp0.val,
                test_xxcc_con_ptn_mask, td->ptn_itemp_mask);
    }
#endif

    return true;
}

static
bool translate_test_jcc(IR1_INST *pir1)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    IR1_INST *list = td->ir1_inst_array;
    IR1_INST *nir1 = NULL;

#ifdef CONFIG_LATX_TU
    TranslationBlock *tb = td->curr_tb;
    TranslationBlock *tu_next_tb = tb->s_data->next_tb[TU_TB_INDEX_NEXT];
    TranslationBlock *tu_target_tb = tb->s_data->next_tb[TU_TB_INDEX_TARGET];
#endif

    lsassert(pir1->instptn.next >= 0);

    nir1 = ir1_inst_in_list(list, pir1->instptn.next);

#if 0
    latxs_translate_test_byhand(pir1);
    switch (ir1_opcode(nir1)) {
    case WRAP(JB):  latxs_translate_jb(nir1); break;
    case WRAP(JAE): latxs_translate_jae(nir1); break;
    case WRAP(JE):  latxs_translate_jz(nir1); break;
    case WRAP(JNE): latxs_translate_jnz(nir1); break;
    case WRAP(JBE): latxs_translate_jbe(nir1); break;
    case WRAP(JA):  latxs_translate_ja(nir1); break;
    case WRAP(JL):  latxs_translate_jl(nir1); break;
    case WRAP(JGE): latxs_translate_jge(nir1); break;
    case WRAP(JLE): latxs_translate_jle(nir1); break;
    case WRAP(JG):  latxs_translate_jg(nir1); break;
    default:        lsassert(0);        break;
    }
    return true;
#endif

#if 0
    ir1_dump(pir1);
    ir1_dump(nir1);
    fprintf(stderr, "---------------\n");
#endif
    /*TEMP := SRC1 AND SRC2;
    SF := MSB(TEMP);
    IF TEMP = 0
        THEN ZF := 1;
        ELSE ZF := 0;
    FI:
    PF := BitwiseXNOR(TEMP[0:7]);
    CF := 0;
    OF := 0;
    (AF is undefined)*/

    int tu_next_eflag_use = 1;
#ifdef CONFIG_LATX_TU
    if (td->is_tu_mode && flag_reduction_tu_test_jcc() &&
        tu_next_tb    && tu_next_tb->s_data->eflag_use == 0 &&
        tu_target_tb  && tu_target_tb->s_data->eflag_use == 0)
    {
        tu_next_eflag_use = 0;
    }
#endif

    int tu_eflag_opt = 0;
#ifdef CONFIG_LATX_TU
    TU_EFLAG_OPT_RESET(tu_data);
    if (td->is_tu_mode && tu_next_eflag_use == 1 &&
        flag_reduction_tu_test_jcc() && flag_reduction_tu_next() &&
        tu_next_tb && tu_next_tb->s_data->eflag_use != 0 &&
        tb_is_adjacent(tb, tu_next_tb) &&
        tu_target_tb && tu_target_tb->s_data->eflag_use == 0)
    {
        tu_eflag_opt = 1;
    }
#endif

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);
    IR2_OPND src0;
    IR2_OPND src1;
    IR2_OPND temp;
    bool byte_immediate = !td->is_tu_mode && ir1_opnd_is_8l(opnd0) &&
                          ir1_opnd_is_imm(opnd1) &&
                          (ir1_opcode(nir1) == WRAP(JE) ||
                           ir1_opcode(nir1) == WRAP(JNE));
    if (byte_immediate) {
        IR2_OPND reg = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd0));

        /* TEST and its branch consume the same masked byte. */
        src0 = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd2i(LISA_ANDI, &src0, &reg,
                               ir1_opnd_uimm(opnd1) & 0xff);
        temp = src0;
    } else if (ir1_opnd_is_gpr(opnd0) && (ir1_opnd_size(opnd0) == 64)) {
        src0 = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd0));
    } else {
        src0 = latxs_ra_alloc_itemp();
        latxs_load_ir1_to_ir2(&src0, opnd0, EXMode_S);
    }
    int is_same_reg = ir1_opnd_is_same_reg(opnd0, opnd1);
    int os = ir1_opnd_size(opnd0); /* 8, 16, 32, 64 */
    if (is_same_reg || byte_immediate) {
        if (tu_next_eflag_use) {
            if (tu_eflag_opt) {
#ifdef CONFIG_LATX_TU
                TU_EFLAG_OPT_SET(tu_data);
                tu_data->eflag_opt_data.ef_opcode = LISA_X86AND_B + (os >> 4) - (os >> 6);
                tu_data->eflag_opt_data.ef_opnd0 = src0;
                tu_data->eflag_opt_data.ef_opnd1 = src0;
#endif
            } else {
                LATXS_FLAG_REDUCTION_CHECKIF(pir1)
                latxs_append_ir2_opnd2(LISA_X86AND_B + (os >> 4) - (os >> 6), &src0, &src0);
            }
        }
    } else {
        if (ir1_opnd_is_gpr(opnd1) && (ir1_opnd_size(opnd1) == 64)) {
            src1 = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd1));
        } else {
            src1 = latxs_ra_alloc_itemp();
            latxs_load_ir1_to_ir2(&src1, opnd1, EXMode_S);
        }
        if (tu_next_eflag_use) {
            if (tu_eflag_opt) {
#ifdef CONFIG_LATX_TU
                TU_EFLAG_OPT_SET(tu_data);
                tu_data->eflag_opt_data.ef_opcode = LISA_X86AND_B + (os >> 4) - (os >> 6);
                tu_data->eflag_opt_data.ef_opnd0 = src0;
                tu_data->eflag_opt_data.ef_opnd1 = src1;
#endif
            } else {
                LATXS_FLAG_REDUCTION_CHECKIF(pir1)
                latxs_append_ir2_opnd2(LISA_X86AND_B + (os >> 4) - (os >> 6), &src0, &src1);
            }
        }
        temp = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd3(LISA_AND, &temp, &src0, &src1);
    }

    IR2_OPND *zero = &latxs_zero_ir2_opnd;
    IR2_OPND target_label = latxs_ir2_opnd_new_label();

    latxs_tr_gen_eob();
    LOAD_TB_PTR_TO_DBT_ARG1();

    switch (ir1_opcode(nir1)) {
    case WRAP(JE):
        /*ZF=1 allow diff*/
#ifdef CONFIG_LATX_TU
        if (is_same_reg) {
            GEN_TU_FUN_AND_RETURN(LISA_BEQZ, src0, target_label, nir1);
        } else {
            GEN_TU_FUN_AND_RETURN(LISA_BEQZ, temp, target_label, nir1);
        }
#endif
        latxs_append_ir2_opnd2(LISA_BEQZ, is_same_reg ? &src0 : &temp,
                               &target_label);
        break;
    case WRAP(JNE):
        /*ZF=0 allow diff*/
#ifdef CONFIG_LATX_TU
        if (is_same_reg) {
            GEN_TU_FUN_AND_RETURN(LISA_BNEZ, src0, target_label, nir1);
        } else {
            GEN_TU_FUN_AND_RETURN(LISA_BNEZ, temp, target_label, nir1);
        }
#endif
        latxs_append_ir2_opnd2(LISA_BNEZ, is_same_reg ? &src0 : &temp,
                               &target_label);
        break;
    case WRAP(JS):
        /*SF=1*/
        lsassert(ir1_opnd_is_same_reg(opnd0, opnd1));
#ifdef CONFIG_LATX_TU
        GEN_TU_FUN_AND_RETURN2(LISA_BLT, src0, *zero, target_label, nir1);
#endif
        latxs_append_ir2_opnd3(LISA_BLT, &src0, zero, &target_label);
        break;
    case WRAP(JNS):
        /*SF=0*/
        lsassert(ir1_opnd_is_same_reg(opnd0, opnd1));
#ifdef CONFIG_LATX_TU
        GEN_TU_FUN_AND_RETURN2(LISA_BGE, src0, *zero, target_label, nir1);
#endif
        latxs_append_ir2_opnd3(LISA_BGE, &src0, zero, &target_label);
        break;
    case WRAP(JLE):
        /*ZF=1 or SF≠ OF*/
        lsassert(ir1_opnd_is_same_reg(opnd0, opnd1));
#ifdef CONFIG_LATX_TU
        GEN_TU_FUN_AND_RETURN2(LISA_BGE, *zero, src0, target_label, nir1);
#endif
        latxs_append_ir2_opnd3(LISA_BGE, zero, &src0, &target_label);
        break;
    case WRAP(JG):
        /*ZF=0 and SF=OF*/
        lsassert(ir1_opnd_is_same_reg(opnd0, opnd1));
#ifdef CONFIG_LATX_TU
        GEN_TU_FUN_AND_RETURN2(LISA_BLT, *zero, src0, target_label, nir1);
#endif
        latxs_append_ir2_opnd3(LISA_BLT, zero, &src0, &target_label);
        break;
    case WRAP(JNO):
        /*OF=0 allow diff*/
#ifdef CONFIG_LATX_TU
        {
            IR2_OPND no_next = {._type = IR2_OPND_INV};
            GEN_TU_FUN_AND_RETURN(LISA_B, no_next, target_label, nir1);
        }
#endif
        latxs_append_ir2_opnd1(LISA_B, &target_label);
        break;
    case WRAP(JO):
        /*OF=1 allow diff*/
    case WRAP(JB):
        /*CF=1 allow diff*/
#ifdef CONFIG_LATX_TU
        {
            IR2_OPND no_target = {._type = IR2_OPND_INV};
            GEN_TU_FUN_AND_RETURN(LISA_INVALID, no_target, no_target, nir1);
        }
#endif
        break;
    case WRAP(JBE):
        /*CF=1 or ZF=1 allow diff*/
#ifdef CONFIG_LATX_TU
        if (is_same_reg) {
            GEN_TU_FUN_AND_RETURN(LISA_BEQZ, src0, target_label, nir1);
        } else {
            GEN_TU_FUN_AND_RETURN(LISA_BEQZ, temp, target_label, nir1);
        }
#endif
        latxs_append_ir2_opnd2(LISA_BEQZ, is_same_reg ? &src0 : &temp,
                               &target_label);
        break;
    case WRAP(JA):
        /*CF=0 and ZF=0 allow diff*/
#ifdef CONFIG_LATX_TU
        if (is_same_reg) {
            GEN_TU_FUN_AND_RETURN(LISA_BNEZ, src0, target_label, nir1);
        } else {
            GEN_TU_FUN_AND_RETURN(LISA_BNEZ, temp, target_label, nir1);
        }
#endif
        latxs_append_ir2_opnd2(LISA_BNEZ, is_same_reg ? &src0 : &temp,
                               &target_label);
        break;
    case WRAP(JAE):
        /*CF=0 allow diff*/
#ifdef CONFIG_LATX_TU
        {
            IR2_OPND no_next = {._type = IR2_OPND_INV};
            GEN_TU_FUN_AND_RETURN(LISA_B, no_next, target_label, nir1);
        }
#endif
        latxs_append_ir2_opnd1(LISA_B, &target_label);
        break;
    default:
        lsassert(0);
        break;
    }

    /* not taken */
    latxs_tr_generate_exit_tb(nir1, 0);

    latxs_append_ir2_opnd1(LISA_LABEL, &target_label);

    /* taken */
    latxs_tr_generate_exit_tb(nir1, 1);

    return true;

#if 0
pattern_fail:
    pir1->instptn.opc = INSTPTN_OPC_NONE;
    nir1->instptn.opc = INSTPTN_OPC_NONE;
    return false;
#endif
}

static
bool translate_and_jcc(IR1_INST *pir1)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    IR1_INST *list = td->ir1_inst_array;
    IR1_INST *nir1 = NULL;

#ifdef CONFIG_LATX_TU
    TranslationBlock *tb = td->curr_tb;
    TranslationBlock *tu_next_tb = tb->s_data->next_tb[TU_TB_INDEX_NEXT];
    TranslationBlock *tu_target_tb = tb->s_data->next_tb[TU_TB_INDEX_TARGET];
#endif

    lsassert(pir1->instptn.next >= 0);

    nir1 = ir1_inst_in_list(list, pir1->instptn.next);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);
    int os = ir1_opnd_size(opnd0); /* 8, 16, 32, 64 */

    int tu_next_eflag_use = 1;
#ifdef CONFIG_LATX_TU
    if (td->is_tu_mode && flag_reduction_tu_and_jcc() &&
        tu_next_tb    && tu_next_tb->s_data->eflag_use == 0 &&
        tu_target_tb  && tu_target_tb->s_data->eflag_use == 0)
    {
        tu_next_eflag_use = 0;
    }
#endif

    int tu_eflag_opt = 0;
#ifdef CONFIG_LATX_TU
    TU_EFLAG_OPT_RESET(tu_data);
    if (td->is_tu_mode && tu_next_eflag_use == 1 &&
        flag_reduction_tu_and_jcc() && flag_reduction_tu_next() &&
        tu_next_tb && tu_next_tb->s_data->eflag_use != 0 &&
        tb_is_adjacent(tb, tu_next_tb) &&
        tu_target_tb && tu_target_tb->s_data->eflag_use == 0)
    {
        tu_eflag_opt = 1;
    }
#endif

    IR2_OPND src0;
    IR2_OPND src1;
    IR2_OPND dest;
    IR2_OPND mem_opnd;
    int imm = 0;

    if (ir1_opnd_is_gpr(opnd0)) {
        src0 = latxs_load_ireg_from_ir1(opnd0, EXMode_N);
        dest = os >= 32 ? src0 : latxs_ra_alloc_itemp();
    } else {
        src0 = latxs_ra_alloc_itemp();
        dest = src0;
        mem_opnd = latxs_ra_alloc_itemp();
        latxs_get_mem_opnd(&mem_opnd, &imm, opnd0, -1);
        la_ld_by_op_size(src0, mem_opnd, imm, os, EXMode_N);
    }

    src1 = latxs_load_ireg_from_ir1(opnd1, EXMode_N);
    latxs_append_ir2_opnd3(LISA_AND, &dest, &src0, &src1);

    if (ir1_opnd_is_gpr(opnd0)) {
        if (os == 32) {
            latxs_append_ir2_opnd2ii(LISA_BSTRPICK_D, &dest, &dest, 31, 0);
        } else if (os < 32) {
            latxs_store_ir2_to_ir1(&dest, opnd0);
        }
    } else {
        la_st_by_op_size(dest, mem_opnd, imm, os);
    }

    IR2_OPND cond_z;
    IR2_OPND cond_s;
    switch (os) {
    case 64:
        cond_z = dest;
        cond_s = dest;
        break;
    case 32:
        cond_z = latxs_ra_alloc_itemp();
        cond_s = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd2_(lisa_mov32z, &cond_z, &dest);
        latxs_append_ir2_opnd2_(lisa_mov32s, &cond_s, &dest);
        break;
    case 16:
        cond_z = latxs_ra_alloc_itemp();
        cond_s = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd2_(lisa_mov16z, &cond_z, &dest);
        latxs_append_ir2_opnd2_(lisa_mov16s, &cond_s, &dest);
        break;
    case 8:
        cond_z = latxs_ra_alloc_itemp();
        cond_s = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd2_(lisa_mov8z, &cond_z, &dest);
        latxs_append_ir2_opnd2_(lisa_mov8s, &cond_s, &dest);
        break;
    default:
        lsassert(0);
        break;
    }

    if (tu_next_eflag_use == 1) {
        if (tu_eflag_opt) {
#ifdef CONFIG_LATX_TU
            TU_EFLAG_OPT_SET(tu_data);
            tu_data->eflag_opt_data.ef_opcode =
                LISA_X86AND_B + (os >> 4) - (os >> 6);
            tu_data->eflag_opt_data.ef_opnd0 = cond_z;
            tu_data->eflag_opt_data.ef_opnd1 = cond_z;
#endif
        } else {
            LATXS_FLAG_REDUCTION_CHECKIF(pir1)
            latxs_append_ir2_opnd2(LISA_X86AND_B + (os >> 4) - (os >> 6),
                                   &cond_z, &cond_z);
        }
    }

    IR2_OPND *zero = &latxs_zero_ir2_opnd;
    IR2_OPND target_label = latxs_ir2_opnd_new_label();

    latxs_tr_gen_eob();
    LOAD_TB_PTR_TO_DBT_ARG1();

    switch (ir1_opcode(nir1)) {
    case WRAP(JE):
    case WRAP(JBE):
#ifdef CONFIG_LATX_TU
        GEN_TU_FUN_AND_RETURN(LISA_BEQZ, cond_z, target_label, nir1);
#endif
        latxs_append_ir2_opnd2(LISA_BEQZ, &cond_z, &target_label);
        break;
    case WRAP(JNE):
    case WRAP(JA):
#ifdef CONFIG_LATX_TU
        GEN_TU_FUN_AND_RETURN(LISA_BNEZ, cond_z, target_label, nir1);
#endif
        latxs_append_ir2_opnd2(LISA_BNEZ, &cond_z, &target_label);
        break;
    case WRAP(JS):
#ifdef CONFIG_LATX_TU
        GEN_TU_FUN_AND_RETURN2(LISA_BLT, cond_s, *zero, target_label, nir1);
#endif
        latxs_append_ir2_opnd3(LISA_BLT, &cond_s, zero, &target_label);
        break;
    case WRAP(JNS):
#ifdef CONFIG_LATX_TU
        GEN_TU_FUN_AND_RETURN2(LISA_BGE, cond_s, *zero, target_label, nir1);
#endif
        latxs_append_ir2_opnd3(LISA_BGE, &cond_s, zero, &target_label);
        break;
    case WRAP(JLE):
#ifdef CONFIG_LATX_TU
        GEN_TU_FUN_AND_RETURN2(LISA_BGE, *zero, cond_s, target_label, nir1);
#endif
        latxs_append_ir2_opnd3(LISA_BGE, zero, &cond_s, &target_label);
        break;
    case WRAP(JG):
#ifdef CONFIG_LATX_TU
        GEN_TU_FUN_AND_RETURN2(LISA_BLT, *zero, cond_s, target_label, nir1);
#endif
        latxs_append_ir2_opnd3(LISA_BLT, zero, &cond_s, &target_label);
        break;
    case WRAP(JNO):
    case WRAP(JAE):
#ifdef CONFIG_LATX_TU
        {
            IR2_OPND no_next = {._type = IR2_OPND_INV};
            GEN_TU_FUN_AND_RETURN(LISA_B, no_next, target_label, nir1);
        }
#endif
        latxs_append_ir2_opnd1(LISA_B, &target_label);
        break;
    case WRAP(JO):
    case WRAP(JB):
#ifdef CONFIG_LATX_TU
        {
            IR2_OPND no_target = {._type = IR2_OPND_INV};
            GEN_TU_FUN_AND_RETURN(LISA_INVALID, no_target, no_target, nir1);
        }
#endif
        break;
    default:
        lsassert(0);
        break;
    }

    /* not taken */
    latxs_tr_generate_exit_tb(nir1, 0);

    latxs_append_ir2_opnd1(LISA_LABEL, &target_label);

    /* taken */
    latxs_tr_generate_exit_tb(nir1, 1);

    return true;
}

static
bool translate_and_xx_jcc(IR1_INST *pir1)
{
    TRANSLATION_DATA *td = lsenv->tr_data;

    lsassert(pir1->instptn.next >= 0);

    int tu_next_eflag_use = 1;
#ifdef CONFIG_LATX_TU
    TranslationBlock *tb = td->curr_tb;
    TranslationBlock *tu_next_tb = tb->s_data->next_tb[TU_TB_INDEX_NEXT];
    TranslationBlock *tu_target_tb = tb->s_data->next_tb[TU_TB_INDEX_TARGET];
    if (ir1_opcode(pir1) == WRAP(AND) && td->is_tu_mode &&
        flag_reduction_tu_and_xx_jcc() &&
#ifdef CONFIG_SOFTMMU
        (tb->flags & HF_HAMT_EN_MASK) &&
#endif
        tu_next_tb && tu_next_tb->s_data->eflag_use == 0 &&
        tu_target_tb && tu_target_tb->s_data->eflag_use == 0) {
        tu_next_eflag_use = 0;
    }
#endif

    int tu_eflag_opt = 0;
#ifdef CONFIG_LATX_TU
    if (ir1_opcode(pir1) == WRAP(AND)) {
        TU_EFLAG_OPT_RESET(tu_data);
    }
    if (td->is_tu_mode && tu_next_eflag_use == 1 &&
        flag_reduction_tu_and_xx_jcc() && flag_reduction_tu_next() &&
#ifdef CONFIG_SOFTMMU
        (tb->flags & HF_HAMT_EN_MASK) &&
#endif
        tu_next_tb && tu_next_tb->s_data->eflag_use != 0 &&
        tb_is_adjacent(tb, tu_next_tb) &&
        tu_target_tb && tu_target_tb->s_data->eflag_use == 0) {
        tu_eflag_opt = 1;
    }

    if (td->avoid_tu_eflag_opt) {
        tu_next_eflag_use = 1;
        tu_eflag_opt = 0;
    }
#endif

    if (ir1_opcode(pir1) == WRAP(AND)) {
        IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
        IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);
        int os = ir1_opnd_size(opnd0); /* 8, 16, 32, 64 */

        IR2_OPND src0;
        IR2_OPND src1;
        IR2_OPND dest;
        IR2_OPND mem_opnd;
        int imm = 0;

        if (ir1_opnd_is_gpr(opnd0)) {
            src0 = latxs_load_ireg_from_ir1(opnd0, EXMode_N);
            dest = os >= 32 ? src0 : latxs_ra_alloc_itemp();
        } else {
            src0 = latxs_ra_alloc_itemp();
            dest = src0;
            mem_opnd = latxs_ra_alloc_itemp();
            latxs_get_mem_opnd(&mem_opnd, &imm, opnd0, -1);
            la_ld_by_op_size(src0, mem_opnd, imm, os, EXMode_N);
        }

        src1 = latxs_load_ireg_from_ir1(opnd1, EXMode_N);
        latxs_append_ir2_opnd3(LISA_AND, &dest, &src0, &src1);

        if (ir1_opnd_is_gpr(opnd0)) {
            if (os == 32) {
                latxs_append_ir2_opnd2ii(LISA_BSTRPICK_D, &dest, &dest,
                                         31, 0);
            } else if (os < 32) {
                latxs_store_ir2_to_ir1(&dest, opnd0);
            }
        } else {
            la_st_by_op_size(dest, mem_opnd, imm, os);
        }

        td->ptn_itemp0 = latxs_ra_alloc_ptn_itemp();
        switch (os) {
        case 64:
            latxs_append_ir2_opnd2_(lisa_mov, &td->ptn_itemp0, &dest);
            break;
        case 32:
            latxs_append_ir2_opnd2_(lisa_mov32s, &td->ptn_itemp0, &dest);
            break;
        case 16:
            latxs_append_ir2_opnd2_(lisa_mov16s, &td->ptn_itemp0, &dest);
            break;
        case 8:
            latxs_append_ir2_opnd2_(lisa_mov8s, &td->ptn_itemp0, &dest);
            break;
        default:
            lsassert(0);
            break;
        }

        if (tu_next_eflag_use == 1 && !tu_eflag_opt) {
            LATXS_FLAG_REDUCTION_CHECKIF(pir1) {
                latxs_append_ir2_opnd2(LISA_X86AND_B + (os >> 4) -
                                       (os >> 6), &td->ptn_itemp0,
                                       &td->ptn_itemp0);
            }
        }

#ifdef CONFIG_LATX_TU
        if (tu_eflag_opt) {
            TU_EFLAG_OPT_SET(tu_data);
            tu_data->eflag_opt_data.ef_opcode =
                LISA_X86AND_B + (os >> 4) - (os >> 6);
            tu_data->eflag_opt_data.ef_opnd0 = td->ptn_itemp0;
            tu_data->eflag_opt_data.ef_opnd1 = td->ptn_itemp0;
        }
#endif
        if (!tu_next_eflag_use || tu_eflag_opt) {
            tb->ptn.eflag_type = PTN_EFLAG_TYPE_X86AND;
            tb->ptn.os = os;
            tb->ptn.opnd0 = td->ptn_itemp0.val;
            tb->ptn.opnd1 = td->ptn_itemp0.val;
            tb_set_ptn_pcoffset(tb,
                    ir1_addr(pir1) - (tb->pc - tb->cs_base));
        }
    } else {
        if (!td->ptn_itemp_mask) {
            if (tb->ptn.eflag_type != PTN_EFLAG_TYPE_INVALID) {
                td->avoid_tu_eflag_opt = AVOID_TUEF_START;
            }
            switch (ir1_opcode(pir1)) {
            case WRAP(JE):
                latxs_translate_jz(pir1);
                break;
            case WRAP(JNE):
                latxs_translate_jnz(pir1);
                break;
            default:
                lsassert(0);
                break;
            }
            return true;
        }

        IR2_OPND target_label = latxs_ir2_opnd_new_label();

        latxs_tr_gen_eob();
        LOAD_TB_PTR_TO_DBT_ARG1();

        switch (ir1_opcode(pir1)) {
        case WRAP(JE):
#ifdef CONFIG_LATX_TU
            GEN_TU_FUN_AND_RETURN(LISA_BEQZ, td->ptn_itemp0,
                                  target_label, pir1);
#endif
            latxs_append_ir2_opnd2(LISA_BEQZ, &td->ptn_itemp0,
                                   &target_label);
            break;
        case WRAP(JNE):
#ifdef CONFIG_LATX_TU
            GEN_TU_FUN_AND_RETURN(LISA_BNEZ, td->ptn_itemp0,
                                  target_label, pir1);
#endif
            latxs_append_ir2_opnd2(LISA_BNEZ, &td->ptn_itemp0,
                                   &target_label);
            break;
        default:
            lsassert(0);
            break;
        }

        td->ptn_itemp_mask = 0;

        /* not taken */
        latxs_tr_generate_exit_tb(pir1, 0);

        latxs_append_ir2_opnd1(LISA_LABEL, &target_label);

        /* taken */
        latxs_tr_generate_exit_tb(pir1, 1);
    }

    return true;
}

static
bool translate_or_jcc(IR1_INST *pir1)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    IR1_INST *list = td->ir1_inst_array;
    IR1_INST *nir1 = NULL;

#ifdef CONFIG_LATX_TU
    TranslationBlock *tb = td->curr_tb;
    TranslationBlock *tu_next_tb = tb->s_data->next_tb[TU_TB_INDEX_NEXT];
    TranslationBlock *tu_target_tb = tb->s_data->next_tb[TU_TB_INDEX_TARGET];
#endif

    lsassert(pir1->instptn.next >= 0);

    nir1 = ir1_inst_in_list(list, pir1->instptn.next);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);
    int os = ir1_opnd_size(opnd0); /* 8, 16, 32, 64 */

    int tu_next_eflag_use = 1;
#ifdef CONFIG_LATX_TU
    if (td->is_tu_mode && flag_reduction_tu_or_jcc() &&
        tu_next_tb    && tu_next_tb->s_data->eflag_use == 0 &&
        tu_target_tb  && tu_target_tb->s_data->eflag_use == 0)
    {
        tu_next_eflag_use = 0;
    }
#endif

    int tu_eflag_opt = 0;
#ifdef CONFIG_LATX_TU
    TU_EFLAG_OPT_RESET(tu_data);
    if (td->is_tu_mode && tu_next_eflag_use == 1 &&
        flag_reduction_tu_or_jcc() && flag_reduction_tu_next() &&
        tu_next_tb && tu_next_tb->s_data->eflag_use != 0 &&
        tb_is_adjacent(tb, tu_next_tb) &&
        tu_target_tb && tu_target_tb->s_data->eflag_use == 0)
    {
        tu_eflag_opt = 1;
    }
#endif

    IR2_OPND src0;
    IR2_OPND src1;
    IR2_OPND dest;
    IR2_OPND mem_opnd;
    int imm = 0;

    if (ir1_opnd_is_gpr(opnd0)) {
        src0 = latxs_load_ireg_from_ir1(opnd0, EXMode_N);
        dest = os >= 32 ? src0 : latxs_ra_alloc_itemp();
    } else {
        src0 = latxs_ra_alloc_itemp();
        dest = src0;
        mem_opnd = latxs_ra_alloc_itemp();
        latxs_get_mem_opnd(&mem_opnd, &imm, opnd0, -1);
        la_ld_by_op_size(src0, mem_opnd, imm, os, EXMode_N);
    }

    src1 = latxs_load_ireg_from_ir1(opnd1, EXMode_N);
    latxs_append_ir2_opnd3(LISA_OR, &dest, &src0, &src1);

    if (ir1_opnd_is_gpr(opnd0)) {
        if (os == 32) {
            latxs_append_ir2_opnd2ii(LISA_BSTRPICK_D, &dest, &dest, 31, 0);
        } else if (os < 32) {
            latxs_store_ir2_to_ir1(&dest, opnd0);
        }
    } else {
        la_st_by_op_size(dest, mem_opnd, imm, os);
    }

    IR2_OPND cond_z;
    IR2_OPND cond_s;
    switch (os) {
    case 64:
        cond_z = dest;
        cond_s = dest;
        break;
    case 32:
        cond_z = latxs_ra_alloc_itemp();
        cond_s = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd2_(lisa_mov32z, &cond_z, &dest);
        latxs_append_ir2_opnd2_(lisa_mov32s, &cond_s, &dest);
        break;
    case 16:
        cond_z = latxs_ra_alloc_itemp();
        cond_s = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd2_(lisa_mov16z, &cond_z, &dest);
        latxs_append_ir2_opnd2_(lisa_mov16s, &cond_s, &dest);
        break;
    case 8:
        cond_z = latxs_ra_alloc_itemp();
        cond_s = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd2_(lisa_mov8z, &cond_z, &dest);
        latxs_append_ir2_opnd2_(lisa_mov8s, &cond_s, &dest);
        break;
    default:
        lsassert(0);
        break;
    }

    if (tu_next_eflag_use == 1) {
        if (tu_eflag_opt) {
#ifdef CONFIG_LATX_TU
            TU_EFLAG_OPT_SET(tu_data);
            tu_data->eflag_opt_data.ef_opcode =
                LISA_X86OR_B + (os >> 4) - (os >> 6);
            tu_data->eflag_opt_data.ef_opnd0 = cond_z;
            tu_data->eflag_opt_data.ef_opnd1 = cond_z;
#endif
        } else {
            LATXS_FLAG_REDUCTION_CHECKIF(pir1)
            latxs_append_ir2_opnd2(LISA_X86OR_B + (os >> 4) - (os >> 6),
                                   &cond_z, &cond_z);
        }
    }

    IR2_OPND *zero = &latxs_zero_ir2_opnd;
    IR2_OPND target_label = latxs_ir2_opnd_new_label();

    latxs_tr_gen_eob();
    LOAD_TB_PTR_TO_DBT_ARG1();

    switch (ir1_opcode(nir1)) {
    case WRAP(JE):
    case WRAP(JBE):
#ifdef CONFIG_LATX_TU
        GEN_TU_FUN_AND_RETURN(LISA_BEQZ, cond_z, target_label, nir1);
#endif
        latxs_append_ir2_opnd2(LISA_BEQZ, &cond_z, &target_label);
        break;
    case WRAP(JNE):
    case WRAP(JA):
#ifdef CONFIG_LATX_TU
        GEN_TU_FUN_AND_RETURN(LISA_BNEZ, cond_z, target_label, nir1);
#endif
        latxs_append_ir2_opnd2(LISA_BNEZ, &cond_z, &target_label);
        break;
    case WRAP(JS):
#ifdef CONFIG_LATX_TU
        GEN_TU_FUN_AND_RETURN2(LISA_BLT, cond_s, *zero, target_label, nir1);
#endif
        latxs_append_ir2_opnd3(LISA_BLT, &cond_s, zero, &target_label);
        break;
    case WRAP(JNS):
#ifdef CONFIG_LATX_TU
        GEN_TU_FUN_AND_RETURN2(LISA_BGE, cond_s, *zero, target_label, nir1);
#endif
        latxs_append_ir2_opnd3(LISA_BGE, &cond_s, zero, &target_label);
        break;
    case WRAP(JLE):
#ifdef CONFIG_LATX_TU
        GEN_TU_FUN_AND_RETURN2(LISA_BGE, *zero, cond_s, target_label, nir1);
#endif
        latxs_append_ir2_opnd3(LISA_BGE, zero, &cond_s, &target_label);
        break;
    case WRAP(JG):
#ifdef CONFIG_LATX_TU
        GEN_TU_FUN_AND_RETURN2(LISA_BLT, *zero, cond_s, target_label, nir1);
#endif
        latxs_append_ir2_opnd3(LISA_BLT, zero, &cond_s, &target_label);
        break;
    case WRAP(JNO):
    case WRAP(JAE):
#ifdef CONFIG_LATX_TU
        {
            IR2_OPND no_next = {._type = IR2_OPND_INV};
            GEN_TU_FUN_AND_RETURN(LISA_B, no_next, target_label, nir1);
        }
#endif
        latxs_append_ir2_opnd1(LISA_B, &target_label);
        break;
    case WRAP(JO):
    case WRAP(JB):
#ifdef CONFIG_LATX_TU
        {
            IR2_OPND no_target = {._type = IR2_OPND_INV};
            GEN_TU_FUN_AND_RETURN(LISA_INVALID, no_target, no_target, nir1);
        }
#endif
        break;
    default:
        lsassert(0);
        break;
    }

    /* not taken */
    latxs_tr_generate_exit_tb(nir1, 0);

    latxs_append_ir2_opnd1(LISA_LABEL, &target_label);

    /* taken */
    latxs_tr_generate_exit_tb(nir1, 1);

    return true;
}

static
bool translate_or_xx_jcc(IR1_INST *pir1)
{
    TRANSLATION_DATA *td = lsenv->tr_data;

    lsassert(pir1->instptn.next >= 0);

    int tu_next_eflag_use = 1;
#ifdef CONFIG_LATX_TU
    TranslationBlock *tb = td->curr_tb;
    TranslationBlock *tu_next_tb = tb->s_data->next_tb[TU_TB_INDEX_NEXT];
    TranslationBlock *tu_target_tb = tb->s_data->next_tb[TU_TB_INDEX_TARGET];
    if (ir1_opcode(pir1) == WRAP(OR) && td->is_tu_mode &&
        flag_reduction_tu_or_xx_jcc() &&
#ifdef CONFIG_SOFTMMU
        (tb->flags & HF_HAMT_EN_MASK) &&
#endif
        tu_next_tb    && tu_next_tb->s_data->eflag_use == 0 &&
        tu_target_tb  && tu_target_tb->s_data->eflag_use == 0)
    {
        tu_next_eflag_use = 0;
    }
#endif

    int tu_eflag_opt = 0;
#ifdef CONFIG_LATX_TU
    if (ir1_opcode(pir1) == WRAP(OR)) {
        TU_EFLAG_OPT_RESET(tu_data);
    }
    if (td->is_tu_mode && tu_next_eflag_use == 1 &&
        flag_reduction_tu_or_xx_jcc() && flag_reduction_tu_next() &&
#ifdef CONFIG_SOFTMMU
        (tb->flags & HF_HAMT_EN_MASK) &&
#endif
        tu_next_tb && tu_next_tb->s_data->eflag_use != 0 &&
        tb_is_adjacent(tb, tu_next_tb) &&
        tu_target_tb && tu_target_tb->s_data->eflag_use == 0)
    {
        tu_eflag_opt = 1;
    }
#endif

#ifdef CONFIG_LATX_TU
    if (td->avoid_tu_eflag_opt) {
        tu_next_eflag_use = 1;
        tu_eflag_opt = 0;
    }
#endif

    if (ir1_opcode(pir1) == WRAP(OR)) {
        IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
        IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);
        int os = ir1_opnd_size(opnd0); /* 8, 16, 32, 64 */

        IR2_OPND src0;
        IR2_OPND src1;
        IR2_OPND dest;
        IR2_OPND mem_opnd;
        int imm = 0;

        if (ir1_opnd_is_gpr(opnd0)) {
            src0 = latxs_load_ireg_from_ir1(opnd0, EXMode_N);
            dest = os >= 32 ? src0 : latxs_ra_alloc_itemp();
        } else {
            src0 = latxs_ra_alloc_itemp();
            dest = src0;
            mem_opnd = latxs_ra_alloc_itemp();
            latxs_get_mem_opnd(&mem_opnd, &imm, opnd0, -1);
            la_ld_by_op_size(src0, mem_opnd, imm, os, EXMode_N);
        }

        src1 = latxs_load_ireg_from_ir1(opnd1, EXMode_N);
        latxs_append_ir2_opnd3(LISA_OR, &dest, &src0, &src1);

        if (ir1_opnd_is_gpr(opnd0)) {
            if (os == 32) {
                latxs_append_ir2_opnd2ii(LISA_BSTRPICK_D, &dest, &dest, 31, 0);
            } else if (os < 32) {
                latxs_store_ir2_to_ir1(&dest, opnd0);
            }
        } else {
            la_st_by_op_size(dest, mem_opnd, imm, os);
        }

        td->ptn_itemp0 = latxs_ra_alloc_ptn_itemp();
        switch (os) {
        case 64:
            latxs_append_ir2_opnd2_(lisa_mov, &td->ptn_itemp0, &dest);
            break;
        case 32:
            latxs_append_ir2_opnd2_(lisa_mov32s, &td->ptn_itemp0, &dest);
            break;
        case 16:
            latxs_append_ir2_opnd2_(lisa_mov16s, &td->ptn_itemp0, &dest);
            break;
        case 8:
            latxs_append_ir2_opnd2_(lisa_mov8s, &td->ptn_itemp0, &dest);
            break;
        default:
            lsassert(0);
            break;
        }

        if (tu_next_eflag_use == 1 && !tu_eflag_opt) {
            LATXS_FLAG_REDUCTION_CHECKIF(pir1)
            latxs_append_ir2_opnd2(LISA_X86OR_B + (os >> 4) - (os >> 6),
                                   &td->ptn_itemp0, &td->ptn_itemp0);
        }

#ifdef CONFIG_LATX_TU
        if (tu_eflag_opt) {
            TU_EFLAG_OPT_SET(tu_data);
            tu_data->eflag_opt_data.ef_opcode =
                LISA_X86OR_B + (os >> 4) - (os >> 6);
            tu_data->eflag_opt_data.ef_opnd0 = td->ptn_itemp0;
            tu_data->eflag_opt_data.ef_opnd1 = td->ptn_itemp0;
        }
#endif
        if (!tu_next_eflag_use || tu_eflag_opt) {
            tb->ptn.eflag_type = PTN_EFLAG_TYPE_X86OR;
            tb->ptn.os = os;
            tb->ptn.opnd0 = td->ptn_itemp0.val;
            tb->ptn.opnd1 = td->ptn_itemp0.val;
            tb_set_ptn_pcoffset(tb,
                    ir1_addr(pir1) - (tb->pc - tb->cs_base));
        }
    } else {
        if (!td->ptn_itemp_mask) {
            if (tb->ptn.eflag_type != PTN_EFLAG_TYPE_INVALID) {
                td->avoid_tu_eflag_opt = AVOID_TUEF_START;
            }
            switch (ir1_opcode(pir1)) {
            case WRAP(JE):
                latxs_translate_jz(pir1);
                break;
            case WRAP(JNE):
                latxs_translate_jnz(pir1);
                break;
            case WRAP(JS):
                latxs_translate_js(pir1);
                break;
            case WRAP(JNS):
                latxs_translate_jns(pir1);
                break;
            case WRAP(JLE):
                latxs_translate_jle(pir1);
                break;
            case WRAP(JG):
                latxs_translate_jg(pir1);
                break;
            case WRAP(JNO):
                latxs_translate_jno(pir1);
                break;
            case WRAP(JO):
                latxs_translate_jo(pir1);
                break;
            case WRAP(JB):
                latxs_translate_jb(pir1);
                break;
            case WRAP(JBE):
                latxs_translate_jbe(pir1);
                break;
            case WRAP(JA):
                latxs_translate_ja(pir1);
                break;
            case WRAP(JAE):
                latxs_translate_jae(pir1);
                break;
            default:
                lsassert(0);
                break;
            }
            return true;
        }
        IR2_OPND *zero = &latxs_zero_ir2_opnd;
        IR2_OPND target_label = latxs_ir2_opnd_new_label();

        latxs_tr_gen_eob();
        LOAD_TB_PTR_TO_DBT_ARG1();

        switch (ir1_opcode(pir1)) {
        case WRAP(JE):
#ifdef CONFIG_LATX_TU
            GEN_TU_FUN_AND_RETURN(LISA_BEQZ, td->ptn_itemp0,
                                  target_label, pir1);
#endif
            latxs_append_ir2_opnd2(LISA_BEQZ, &td->ptn_itemp0, &target_label);
            break;
        case WRAP(JNE):
#ifdef CONFIG_LATX_TU
            GEN_TU_FUN_AND_RETURN(LISA_BNEZ, td->ptn_itemp0,
                                  target_label, pir1);
#endif
            latxs_append_ir2_opnd2(LISA_BNEZ, &td->ptn_itemp0, &target_label);
            break;
        case WRAP(JS):
#ifdef CONFIG_LATX_TU
            GEN_TU_FUN_AND_RETURN2(LISA_BLT, td->ptn_itemp0, *zero,
                                   target_label, pir1);
#endif
            latxs_append_ir2_opnd3(LISA_BLT, &td->ptn_itemp0, zero,
                                   &target_label);
            break;
        case WRAP(JNS):
#ifdef CONFIG_LATX_TU
            GEN_TU_FUN_AND_RETURN2(LISA_BGE, td->ptn_itemp0, *zero,
                                   target_label, pir1);
#endif
            latxs_append_ir2_opnd3(LISA_BGE, &td->ptn_itemp0, zero,
                                   &target_label);
            break;
        case WRAP(JLE):
#ifdef CONFIG_LATX_TU
            GEN_TU_FUN_AND_RETURN2(LISA_BGE, *zero, td->ptn_itemp0,
                                   target_label, pir1);
#endif
            latxs_append_ir2_opnd3(LISA_BGE, zero, &td->ptn_itemp0,
                                   &target_label);
            break;
        case WRAP(JG):
#ifdef CONFIG_LATX_TU
            GEN_TU_FUN_AND_RETURN2(LISA_BLT, *zero, td->ptn_itemp0,
                                   target_label, pir1);
#endif
            latxs_append_ir2_opnd3(LISA_BLT, zero, &td->ptn_itemp0,
                                   &target_label);
            break;
        case WRAP(JNO):
        case WRAP(JAE):
#ifdef CONFIG_LATX_TU
            {
                IR2_OPND no_next = {._type = IR2_OPND_INV};
                GEN_TU_FUN_AND_RETURN(LISA_B, no_next, target_label, pir1);
            }
#endif
            latxs_append_ir2_opnd1(LISA_B, &target_label);
            break;
        case WRAP(JO):
        case WRAP(JB):
#ifdef CONFIG_LATX_TU
            {
                IR2_OPND no_target = {._type = IR2_OPND_INV};
                GEN_TU_FUN_AND_RETURN(LISA_INVALID, no_target, no_target, pir1);
            }
#endif
            break;
        case WRAP(JBE):
#ifdef CONFIG_LATX_TU
            GEN_TU_FUN_AND_RETURN(LISA_BEQZ, td->ptn_itemp0,
                                  target_label, pir1);
#endif
            latxs_append_ir2_opnd2(LISA_BEQZ, &td->ptn_itemp0, &target_label);
            break;
        case WRAP(JA):
#ifdef CONFIG_LATX_TU
            GEN_TU_FUN_AND_RETURN(LISA_BNEZ, td->ptn_itemp0,
                                  target_label, pir1);
#endif
            latxs_append_ir2_opnd2(LISA_BNEZ, &td->ptn_itemp0, &target_label);
            break;
        default:
            lsassert(0);
            break;
        }

        td->ptn_itemp_mask = 0;

        /* not taken */
        latxs_tr_generate_exit_tb(pir1, 0);

        latxs_append_ir2_opnd1(LISA_LABEL, &target_label);

        /* taken */
        latxs_tr_generate_exit_tb(pir1, 1);
    }

    return true;
}

static bool translate_xor_jcc(IR1_INST *pir1)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    IR1_INST *list = td->ir1_inst_array;
    IR1_INST *nir1 = NULL;

#ifdef CONFIG_LATX_TU
    TranslationBlock *tb = td->curr_tb;
    TranslationBlock *tu_next_tb = tb->s_data->next_tb[TU_TB_INDEX_NEXT];
    TranslationBlock *tu_target_tb = tb->s_data->next_tb[TU_TB_INDEX_TARGET];
#endif

    lsassert(pir1->instptn.next >= 0);

    nir1 = ir1_inst_in_list(list, pir1->instptn.next);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);
    int os = ir1_opnd_size(opnd0); /* 8, 16, 32, 64 */

    int tu_next_eflag_use = 1;
#ifdef CONFIG_LATX_TU
    if (td->is_tu_mode && flag_reduction_tu_xor_jcc() &&
        tu_next_tb    && tu_next_tb->s_data->eflag_use == 0 &&
        tu_target_tb  && tu_target_tb->s_data->eflag_use == 0)
    {
        tu_next_eflag_use = 0;
    }
#endif

    int tu_eflag_opt = 0;
#ifdef CONFIG_LATX_TU
    TU_EFLAG_OPT_RESET(tu_data);
    if (td->is_tu_mode && tu_next_eflag_use == 1 &&
        flag_reduction_tu_xor_jcc() && flag_reduction_tu_next() &&
        tu_next_tb && tu_next_tb->s_data->eflag_use != 0 &&
        tb_is_adjacent(tb, tu_next_tb) &&
        tu_target_tb && tu_target_tb->s_data->eflag_use == 0)
    {
        tu_eflag_opt = 1;
    }
#endif

    IR2_OPND src0;
    IR2_OPND src1;
    IR2_OPND dest;
    IR2_OPND mem_opnd;
    int imm = 0;

    if (ir1_opnd_is_gpr(opnd0)) {
        src0 = latxs_load_ireg_from_ir1(opnd0, EXMode_N);
        dest = os >= 32 ? src0 : latxs_ra_alloc_itemp();
    } else {
        src0 = latxs_ra_alloc_itemp();
        dest = src0;
        mem_opnd = latxs_ra_alloc_itemp();
        latxs_get_mem_opnd(&mem_opnd, &imm, opnd0, -1);
        la_ld_by_op_size(src0, mem_opnd, imm, os, EXMode_N);
    }

    src1 = latxs_load_ireg_from_ir1(opnd1, EXMode_N);
    latxs_append_ir2_opnd3(LISA_XOR, &dest, &src0, &src1);

    if (ir1_opnd_is_gpr(opnd0)) {
        if (os == 32) {
            latxs_append_ir2_opnd2ii(LISA_BSTRPICK_D, &dest, &dest, 31, 0);
        } else if (os < 32) {
            latxs_store_ir2_to_ir1(&dest, opnd0);
        }
    } else {
        la_st_by_op_size(dest, mem_opnd, imm, os);
    }

    IR2_OPND cond_z;
    IR2_OPND cond_s;
    switch (os) {
    case 64:
        cond_z = dest;
        cond_s = dest;
        break;
    case 32:
        cond_z = latxs_ra_alloc_itemp();
        cond_s = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd2_(lisa_mov32z, &cond_z, &dest);
        latxs_append_ir2_opnd2_(lisa_mov32s, &cond_s, &dest);
        break;
    case 16:
        cond_z = latxs_ra_alloc_itemp();
        cond_s = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd2_(lisa_mov16z, &cond_z, &dest);
        latxs_append_ir2_opnd2_(lisa_mov16s, &cond_s, &dest);
        break;
    case 8:
        cond_z = latxs_ra_alloc_itemp();
        cond_s = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd2_(lisa_mov8z, &cond_z, &dest);
        latxs_append_ir2_opnd2_(lisa_mov8s, &cond_s, &dest);
        break;
    default:
        lsassert(0);
        break;
    }

    if (tu_next_eflag_use == 1) {
        if (tu_eflag_opt) {
#ifdef CONFIG_LATX_TU
            TU_EFLAG_OPT_SET(tu_data);
            tu_data->eflag_opt_data.ef_opcode =
                LISA_X86OR_B + (os >> 4) - (os >> 6);
            tu_data->eflag_opt_data.ef_opnd0 = cond_z;
            tu_data->eflag_opt_data.ef_opnd1 = cond_z;
#endif
        } else {
            LATXS_FLAG_REDUCTION_CHECKIF(pir1)
            latxs_append_ir2_opnd2(LISA_X86OR_B + (os >> 4) - (os >> 6),
                                   &cond_z, &cond_z);
        }
    }

    IR2_OPND *zero = &latxs_zero_ir2_opnd;
    IR2_OPND target_label = latxs_ir2_opnd_new_label();

    latxs_tr_gen_eob();
    LOAD_TB_PTR_TO_DBT_ARG1();

    switch (ir1_opcode(nir1)) {
    case WRAP(JE):
    case WRAP(JBE):
#ifdef CONFIG_LATX_TU
        GEN_TU_FUN_AND_RETURN(LISA_BEQZ, cond_z, target_label, nir1);
#endif
        latxs_append_ir2_opnd2(LISA_BEQZ, &cond_z, &target_label);
        break;
    case WRAP(JNE):
    case WRAP(JA):
#ifdef CONFIG_LATX_TU
        GEN_TU_FUN_AND_RETURN(LISA_BNEZ, cond_z, target_label, nir1);
#endif
        latxs_append_ir2_opnd2(LISA_BNEZ, &cond_z, &target_label);
        break;
    case WRAP(JS):
#ifdef CONFIG_LATX_TU
        GEN_TU_FUN_AND_RETURN2(LISA_BLT, cond_s, *zero, target_label, nir1);
#endif
        latxs_append_ir2_opnd3(LISA_BLT, &cond_s, zero, &target_label);
        break;
    case WRAP(JNS):
#ifdef CONFIG_LATX_TU
        GEN_TU_FUN_AND_RETURN2(LISA_BGE, cond_s, *zero, target_label, nir1);
#endif
        latxs_append_ir2_opnd3(LISA_BGE, &cond_s, zero, &target_label);
        break;
    case WRAP(JLE):
#ifdef CONFIG_LATX_TU
        GEN_TU_FUN_AND_RETURN2(LISA_BGE, *zero, cond_s, target_label, nir1);
#endif
        latxs_append_ir2_opnd3(LISA_BGE, zero, &cond_s, &target_label);
        break;
    case WRAP(JG):
#ifdef CONFIG_LATX_TU
        GEN_TU_FUN_AND_RETURN2(LISA_BLT, *zero, cond_s, target_label, nir1);
#endif
        latxs_append_ir2_opnd3(LISA_BLT, zero, &cond_s, &target_label);
        break;
    case WRAP(JNO):
    case WRAP(JAE):
#ifdef CONFIG_LATX_TU
        {
            IR2_OPND no_next = {._type = IR2_OPND_INV};
            GEN_TU_FUN_AND_RETURN(LISA_B, no_next, target_label, nir1);
        }
#endif
        latxs_append_ir2_opnd1(LISA_B, &target_label);
        break;
    case WRAP(JO):
    case WRAP(JB):
#ifdef CONFIG_LATX_TU
        {
            IR2_OPND no_target = {._type = IR2_OPND_INV};
            GEN_TU_FUN_AND_RETURN(LISA_INVALID, no_target, no_target, nir1);
        }
#endif
        break;
    default:
        lsassert(0);
        break;
    }

    /* not taken */
    latxs_tr_generate_exit_tb(nir1, 0);

    latxs_append_ir2_opnd1(LISA_LABEL, &target_label);

    /* taken */
    latxs_tr_generate_exit_tb(nir1, 1);

    return true;
}

static
bool translate_xor_xx_jcc(IR1_INST *pir1)
{
    TRANSLATION_DATA *td = lsenv->tr_data;

    lsassert(pir1->instptn.next >= 0);

    int tu_next_eflag_use = 1;
#ifdef CONFIG_LATX_TU
    TranslationBlock *tb = td->curr_tb;
    TranslationBlock *tu_next_tb = tb->s_data->next_tb[TU_TB_INDEX_NEXT];
    TranslationBlock *tu_target_tb = tb->s_data->next_tb[TU_TB_INDEX_TARGET];
    if (ir1_opcode(pir1) == WRAP(XOR) && td->is_tu_mode &&
        flag_reduction_tu_xor_xx_jcc() &&
#ifdef CONFIG_SOFTMMU
        (tb->flags & HF_HAMT_EN_MASK) &&
#endif
        tu_next_tb    && tu_next_tb->s_data->eflag_use == 0 &&
        tu_target_tb  && tu_target_tb->s_data->eflag_use == 0)
    {
        tu_next_eflag_use = 0;
    }
#endif

    int tu_eflag_opt = 0;
#ifdef CONFIG_LATX_TU
    if (ir1_opcode(pir1) == WRAP(XOR)) {
        TU_EFLAG_OPT_RESET(tu_data);
    }
    if (td->is_tu_mode && tu_next_eflag_use == 1 &&
        flag_reduction_tu_xor_xx_jcc() && flag_reduction_tu_next() &&
#ifdef CONFIG_SOFTMMU
        (tb->flags & HF_HAMT_EN_MASK) &&
#endif
        tu_next_tb && tu_next_tb->s_data->eflag_use != 0 &&
        tb_is_adjacent(tb, tu_next_tb) &&
        tu_target_tb && tu_target_tb->s_data->eflag_use == 0)
    {
        tu_eflag_opt = 1;
    }
#endif

#ifdef CONFIG_LATX_TU
    if (td->avoid_tu_eflag_opt) {
        tu_next_eflag_use = 1;
        tu_eflag_opt = 0;
    }
#endif

    if (ir1_opcode(pir1) == WRAP(XOR)) {
        IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
        IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);
        int os = ir1_opnd_size(opnd0); /* 8, 16, 32, 64 */

        IR2_OPND src0;
        IR2_OPND src1;
        IR2_OPND dest;
        IR2_OPND mem_opnd;
        int imm = 0;

        if (ir1_opnd_is_gpr(opnd0)) {
            src0 = latxs_load_ireg_from_ir1(opnd0, EXMode_N);
            dest = os >= 32 ? src0 : latxs_ra_alloc_itemp();
        } else {
            src0 = latxs_ra_alloc_itemp();
            dest = src0;
            mem_opnd = latxs_ra_alloc_itemp();
            latxs_get_mem_opnd(&mem_opnd, &imm, opnd0, -1);
            la_ld_by_op_size(src0, mem_opnd, imm, os, EXMode_N);
        }

        src1 = latxs_load_ireg_from_ir1(opnd1, EXMode_N);
        latxs_append_ir2_opnd3(LISA_XOR, &dest, &src0, &src1);

        if (ir1_opnd_is_gpr(opnd0)) {
            if (os == 32) {
                latxs_append_ir2_opnd2ii(LISA_BSTRPICK_D, &dest, &dest, 31, 0);
            } else if (os < 32) {
                latxs_store_ir2_to_ir1(&dest, opnd0);
            }
        } else {
            la_st_by_op_size(dest, mem_opnd, imm, os);
        }

        td->ptn_itemp0 = latxs_ra_alloc_ptn_itemp();
        switch (os) {
        case 64:
            latxs_append_ir2_opnd2_(lisa_mov, &td->ptn_itemp0, &dest);
            break;
        case 32:
            latxs_append_ir2_opnd2_(lisa_mov32s, &td->ptn_itemp0, &dest);
            break;
        case 16:
            latxs_append_ir2_opnd2_(lisa_mov16s, &td->ptn_itemp0, &dest);
            break;
        case 8:
            latxs_append_ir2_opnd2_(lisa_mov8s, &td->ptn_itemp0, &dest);
            break;
        default:
            lsassert(0);
            break;
        }

        if (tu_next_eflag_use == 1 && !tu_eflag_opt) {
            LATXS_FLAG_REDUCTION_CHECKIF(pir1)
            latxs_append_ir2_opnd2(LISA_X86OR_B + (os >> 4) - (os >> 6),
                                   &td->ptn_itemp0, &td->ptn_itemp0);
        }

#ifdef CONFIG_LATX_TU
        if (tu_eflag_opt) {
            TU_EFLAG_OPT_SET(tu_data);
            tu_data->eflag_opt_data.ef_opcode =
                LISA_X86OR_B + (os >> 4) - (os >> 6);
            tu_data->eflag_opt_data.ef_opnd0 = td->ptn_itemp0;
            tu_data->eflag_opt_data.ef_opnd1 = td->ptn_itemp0;
        }
#endif
        if (!tu_next_eflag_use || tu_eflag_opt) {
            tb->ptn.eflag_type = PTN_EFLAG_TYPE_X86OR;
            tb->ptn.os = os;
            tb->ptn.opnd0 = td->ptn_itemp0.val;
            tb->ptn.opnd1 = td->ptn_itemp0.val;
            tb_set_ptn_pcoffset(tb,
                    ir1_addr(pir1) - (tb->pc - tb->cs_base));
        }
    } else {
        if (!td->ptn_itemp_mask) {
            if (tb->ptn.eflag_type != PTN_EFLAG_TYPE_INVALID) {
                td->avoid_tu_eflag_opt = AVOID_TUEF_START;
            }
            switch (ir1_opcode(pir1)) {
            case WRAP(JE):
                latxs_translate_jz(pir1);
                break;
            case WRAP(JNE):
                latxs_translate_jnz(pir1);
                break;
            case WRAP(JS):
                latxs_translate_js(pir1);
                break;
            case WRAP(JNS):
                latxs_translate_jns(pir1);
                break;
            case WRAP(JLE):
                latxs_translate_jle(pir1);
                break;
            case WRAP(JG):
                latxs_translate_jg(pir1);
                break;
            case WRAP(JNO):
                latxs_translate_jno(pir1);
                break;
            case WRAP(JO):
                latxs_translate_jo(pir1);
                break;
            case WRAP(JB):
                latxs_translate_jb(pir1);
                break;
            case WRAP(JBE):
                latxs_translate_jbe(pir1);
                break;
            case WRAP(JA):
                latxs_translate_ja(pir1);
                break;
            case WRAP(JAE):
                latxs_translate_jae(pir1);
                break;
            default:
                lsassert(0);
                break;
            }
            return true;
        }
        IR2_OPND *zero = &latxs_zero_ir2_opnd;
        IR2_OPND target_label = latxs_ir2_opnd_new_label();

        latxs_tr_gen_eob();
        LOAD_TB_PTR_TO_DBT_ARG1();

        switch (ir1_opcode(pir1)) {
        case WRAP(JE):
#ifdef CONFIG_LATX_TU
            GEN_TU_FUN_AND_RETURN(LISA_BEQZ, td->ptn_itemp0,
                                  target_label, pir1);
#endif
            latxs_append_ir2_opnd2(LISA_BEQZ, &td->ptn_itemp0, &target_label);
            break;
        case WRAP(JNE):
#ifdef CONFIG_LATX_TU
            GEN_TU_FUN_AND_RETURN(LISA_BNEZ, td->ptn_itemp0,
                                  target_label, pir1);
#endif
            latxs_append_ir2_opnd2(LISA_BNEZ, &td->ptn_itemp0, &target_label);
            break;
        case WRAP(JS):
#ifdef CONFIG_LATX_TU
            GEN_TU_FUN_AND_RETURN2(LISA_BLT, td->ptn_itemp0, *zero,
                                   target_label, pir1);
#endif
            latxs_append_ir2_opnd3(LISA_BLT, &td->ptn_itemp0, zero,
                                   &target_label);
            break;
        case WRAP(JNS):
#ifdef CONFIG_LATX_TU
            GEN_TU_FUN_AND_RETURN2(LISA_BGE, td->ptn_itemp0, *zero,
                                   target_label, pir1);
#endif
            latxs_append_ir2_opnd3(LISA_BGE, &td->ptn_itemp0, zero,
                                   &target_label);
            break;
        case WRAP(JLE):
#ifdef CONFIG_LATX_TU
            GEN_TU_FUN_AND_RETURN2(LISA_BGE, *zero, td->ptn_itemp0,
                                   target_label, pir1);
#endif
            latxs_append_ir2_opnd3(LISA_BGE, zero, &td->ptn_itemp0,
                                   &target_label);
            break;
        case WRAP(JG):
#ifdef CONFIG_LATX_TU
            GEN_TU_FUN_AND_RETURN2(LISA_BLT, *zero, td->ptn_itemp0,
                                   target_label, pir1);
#endif
            latxs_append_ir2_opnd3(LISA_BLT, zero, &td->ptn_itemp0,
                                   &target_label);
            break;
        case WRAP(JNO):
        case WRAP(JAE):
#ifdef CONFIG_LATX_TU
            {
                IR2_OPND no_next = {._type = IR2_OPND_INV};
                GEN_TU_FUN_AND_RETURN(LISA_B, no_next, target_label, pir1);
            }
#endif
            latxs_append_ir2_opnd1(LISA_B, &target_label);
            break;
        case WRAP(JO):
        case WRAP(JB):
#ifdef CONFIG_LATX_TU
            {
                IR2_OPND no_target = {._type = IR2_OPND_INV};
                GEN_TU_FUN_AND_RETURN(LISA_INVALID, no_target, no_target, pir1);
            }
#endif
            break;
        case WRAP(JBE):
#ifdef CONFIG_LATX_TU
            GEN_TU_FUN_AND_RETURN(LISA_BEQZ, td->ptn_itemp0,
                                  target_label, pir1);
#endif
            latxs_append_ir2_opnd2(LISA_BEQZ, &td->ptn_itemp0, &target_label);
            break;
        case WRAP(JA):
#ifdef CONFIG_LATX_TU
            GEN_TU_FUN_AND_RETURN(LISA_BNEZ, td->ptn_itemp0,
                                  target_label, pir1);
#endif
            latxs_append_ir2_opnd2(LISA_BNEZ, &td->ptn_itemp0, &target_label);
            break;
        default:
            lsassert(0);
            break;
        }

        td->ptn_itemp_mask = 0;

        /* not taken */
        latxs_tr_generate_exit_tb(pir1, 0);

        latxs_append_ir2_opnd1(LISA_LABEL, &target_label);

        /* taken */
        latxs_tr_generate_exit_tb(pir1, 1);
    }

    return true;
}

static bool translate_cmp_sbb(IR1_INST *pir1)
{
    IR1_INST *curr = pir1;
    TRANSLATION_DATA *td = lsenv->tr_data;
    IR1_INST *list = td->ir1_inst_array;
    IR1_INST *next = NULL;

    lsassert(curr->instptn.next >= 0);

    next = ir1_inst_in_list(list, curr->instptn.next);

    IR1_OPND *cmp_opnd0 = ir1_get_opnd(curr, 0);
    IR1_OPND *cmp_opnd1 = ir1_get_opnd(curr, 1);


    bool cmp_opnd1_is_imm = ir1_opnd_is_simm_within_12bit(cmp_opnd1);

    IR2_OPND cmp_opnd_0;
    if (ir1_opnd_is_gpr(cmp_opnd0) && (ir1_opnd_size(cmp_opnd0) == 64)) {
        cmp_opnd_0 = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(cmp_opnd0));
    } else {
        cmp_opnd_0 = latxs_ra_alloc_itemp();
        latxs_load_ir1_to_ir2(&cmp_opnd_0, cmp_opnd0, EXMode_S);
    }

    /* sbb */
    IR1_OPND *sbb_opnd0 = ir1_get_opnd(next, 0);
    lsassert(ir1_opnd_is_same_reg(sbb_opnd0, ir1_get_opnd(next, 1)));
    bool opnd_clobber = ir1_opnd_size(sbb_opnd0) != 64;

    IR2_OPND cond = opnd_clobber
                        ? latxs_ra_alloc_itemp()
                        : latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(sbb_opnd0));
    /* caculate cmp */
    if (cmp_opnd1_is_imm) {
        latxs_append_ir2_opnd2i(LISA_SLTUI, &cond, &cmp_opnd_0, ir1_opnd_simm(cmp_opnd1));
    } else {
        IR2_OPND cmp_opnd_1;
        if (ir1_opnd_is_gpr(cmp_opnd1) && (ir1_opnd_size(cmp_opnd1) == 64)) {
            cmp_opnd_1 = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(cmp_opnd1));
        } else {
            cmp_opnd_1 = latxs_ra_alloc_itemp();
            latxs_load_ir1_to_ir2(&cmp_opnd_1, cmp_opnd1, EXMode_S);
        }
        latxs_append_ir2_opnd3(LISA_SLTU, &cond, &cmp_opnd_0, &cmp_opnd_1);
        latxs_ra_free_temp(&cmp_opnd_1);
    }

    /* we need change to sub because sbb uses CF (not calculate) */
    next->info->id = WRAP(SUB);
    latxs_generate_eflag_calculation(&latxs_zero_ir2_opnd, &latxs_zero_ir2_opnd, &cond, next, true);

    latxs_append_ir2_opnd3(LISA_SUB_D, &cond, &latxs_zero_ir2_opnd, &cond);
    if (opnd_clobber) {
        latxs_store_ir2_to_ir1(&cond, sbb_opnd0);
        latxs_ra_free_temp(&cond);
    }
    latxs_ra_free_temp(&cmp_opnd_0);

    return true;
}

static
bool translate_cqo_idiv(IR1_INST *ir1)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    IR1_INST *list = td->ir1_inst_array;
    IR1_INST *next = NULL;
    lsassert(ir1->instptn.next >= 0);

    next = ir1_inst_in_list(list, ir1->instptn.next);
    lsenv->tr_data->curr_ir1_inst = next;
    IR1_OPND *next_opnd_0 = ir1_get_opnd(next, 0);
    lsassert(ir1_opnd_size(next_opnd_0) == 64);
    IR2_OPND src_opnd_0;
    if (ir1_opnd_is_gpr(next_opnd_0)) {
        src_opnd_0 = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(next_opnd_0));
    } else {
        src_opnd_0 = latxs_ra_alloc_itemp();
        latxs_load_ir1_to_ir2(&src_opnd_0, next_opnd_0, EXMode_S);
    }

    IR2_OPND label_z = latxs_ir2_opnd_new_label();
    latxs_append_ir2_opnd3(LISA_BNE, &src_opnd_0,
            &latxs_zero_ir2_opnd, &label_z);
    latxs_tr_gen_excp_divz(next, 0);
    latxs_append_ir2_opnd1(LISA_LABEL, &label_z);

    IR2_OPND src_opnd_1 = latxs_ra_alloc_gpr(rax_index);
    IR2_OPND temp_src = latxs_ra_alloc_itemp();
    IR2_OPND temp1_opnd = latxs_ra_alloc_itemp();

    latxs_append_ir2_opnd3(LISA_OR, &temp_src, &latxs_zero_ir2_opnd, &src_opnd_1);

    latxs_append_ir2_opnd3(LISA_MOD_D, &temp1_opnd, &temp_src, &src_opnd_0);
    latxs_append_ir2_opnd3(LISA_DIV_D, &temp_src, &temp_src, &src_opnd_0);

    latxs_store_ir2_to_ir1(&temp_src, &rax_ir1_opnd);
    latxs_store_ir2_to_ir1(&temp1_opnd, &rdx_ir1_opnd);

    latxs_ra_free_temp(&temp_src);
    latxs_ra_free_temp(&temp1_opnd);
    if (!ir1_opnd_is_gpr(next_opnd_0)) {
        latxs_ra_free_temp(&src_opnd_0);
    }
    return true;
}

static
bool translate_bt_jcc(IR1_INST *pir1)
{
    IR1_INST *curr = pir1;
    TRANSLATION_DATA *td = lsenv->tr_data;
    IR1_INST *list = td->ir1_inst_array;
    IR1_INST *next = NULL;

    lsassert(curr->instptn.next >= 0);

    next = ir1_inst_in_list(list, curr->instptn.next);

#ifdef CONFIG_LATX_TU
    TranslationBlock *tb = td->curr_tb;
    TranslationBlock *tu_next_tb = tb->s_data->next_tb[TU_TB_INDEX_NEXT];
    TranslationBlock *tu_target_tb = tb->s_data->next_tb[TU_TB_INDEX_TARGET];
#endif

    int tu_next_eflag_use = 1;
#ifdef CONFIG_LATX_TU
    if (td->is_tu_mode && flag_reduction_tu_bt_jcc() &&
        tu_next_tb    && tu_next_tb->s_data->eflag_use == 0 &&
        tu_target_tb  && tu_target_tb->s_data->eflag_use == 0)
    {
        tu_next_eflag_use = 0;
    }
#endif

    int tu_eflag_opt = 0;
#ifdef CONFIG_LATX_TU
    TU_EFLAG_OPT_RESET(tu_data);
    if (td->is_tu_mode && tu_next_eflag_use == 1 &&
        flag_reduction_tu_bt_jcc() && flag_reduction_tu_next() &&
        tu_next_tb && tu_next_tb->s_data->eflag_use != 0 &&
        tb_is_adjacent(tb, tu_next_tb) &&
        tu_target_tb && tu_target_tb->s_data->eflag_use == 0)
    {
        tu_eflag_opt = 1;
    }
#endif

    IR1_OPND *bt_opnd0 = ir1_get_opnd(curr, 0);
    IR1_OPND *bt_opnd1 = ir1_get_opnd(curr, 1);

    int t_imm = 0;
    int t_addr_shift = 0;
    switch (ir1_opnd_size(bt_opnd0)) {
    case 16:
        t_imm = 0xf;
        t_addr_shift = 1;
        break;
    case 32:
        t_imm = 0x1f;
        t_addr_shift = 2;
        break;
    case 64:
        t_imm = 0x3f;
        t_addr_shift = 3;
        break;
    default:
        lsassertm(0, "Invalid operand size (%d) in translate_btx.\n",
                ir1_opnd_size(bt_opnd0));
        break;
    }

    IR2_OPND bit_base = latxs_ra_alloc_itemp();
    IR2_OPND bit_off = latxs_ra_alloc_itemp();
    IR2_OPND mem_base;
    int mem_offset;

    bool immediate_register = ir1_opnd_is_gpr(bt_opnd0) &&
                              ir1_opnd_is_imm(bt_opnd1);
    IR2_OPND bit_eflags;

    if (immediate_register) {
        unsigned bit = ir1_opnd_uimm(bt_opnd1) & t_imm;
        IR2_OPND reg = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(bt_opnd0));

        /* Register BT takes the index modulo the operand width. */
        bit_eflags = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd2ii(LISA_BSTRPICK_D, &bit_eflags, &reg, bit, bit);
    } else if (ir1_opnd_is_gpr(bt_opnd0)) {
        latxs_load_ir1_to_ir2(&bit_base, bt_opnd0, EXMode_Z);
        latxs_load_ir1_to_ir2(&bit_off,  bt_opnd1, EXMode_Z);
        latxs_append_ir2_opnd2i(LISA_ANDI, &bit_off, &bit_off, t_imm);
    } else {
        /* bt_opnd0 is memory operand */
        if (ir1_opnd_is_imm(bt_opnd1)) {
            latxs_load_ir1_to_ir2(&bit_off, bt_opnd1, EXMode_Z);
            latxs_append_ir2_opnd2i(LISA_ANDI, &bit_off, &bit_off, t_imm);
        } else {
            latxs_load_ir1_to_ir2(&bit_off, bt_opnd1, EXMode_S);
        }

        latxs_convert_mem_opnd(&mem_base, &mem_offset, bt_opnd0, -1);
        if (mem_offset != 0) {
            latxs_append_ir2_opnd2i(LISA_ADDI_D, &mem_base, &mem_base, mem_offset);
#ifdef LATXS_IMM_CACHE_ENABLE
            imm_cache_addi_update(mem_offset);
#endif
        }

        IR2_OPND byte_nr = latxs_ra_alloc_itemp();

        latxs_append_ir2_opnd2i(LISA_SRAI_D, &byte_nr, &bit_off, 3 + t_addr_shift);
        latxs_append_ir2_opnd2i(LISA_SLLI_D, &byte_nr, &byte_nr, t_addr_shift);
        latxs_append_ir2_opnd2i(LISA_ANDI, &bit_off, &bit_off, t_imm);

        latxs_append_ir2_opnd3(LISA_ADD_D, &mem_base, &mem_base, &byte_nr);
        latxs_ra_free_temp(&byte_nr);

        int addr_size = latxs_ir1_addr_size(curr);
        if (addr_size == 4) {
            latxs_append_ir2_opnd2_(lisa_mov32z, &mem_base, &mem_base);
        } else if (addr_size == 2) {
            latxs_append_ir2_opnd2_(lisa_mov16z, &mem_base, &mem_base);
        }

        switch (ir1_opnd_size(bt_opnd0)) {
        case 16:
            gen_ldst_softmmu_helper(LISA_LD_HU, &bit_base, &mem_base, 0, 1);
            break;
        case 32:
            gen_ldst_softmmu_helper(LISA_LD_WU, &bit_base, &mem_base, 0, 1);
            break;
        case 64:
            gen_ldst_softmmu_helper(LISA_LD_D, &bit_base, &mem_base, 0, 1);
            break;
        default:
            lsassertm(0, "Invalid operand size (%d) in translate_btx.\n",
                    ir1_opnd_size(bt_opnd0));
            break;
        }
    }

    if (!immediate_register) {
        bit_eflags = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd3(LISA_SRL_D, &bit_eflags, &bit_base, &bit_off);
        latxs_append_ir2_opnd2i(LISA_ANDI, &bit_eflags, &bit_eflags, 1);
    }
    latxs_ra_free_temp(&bit_off);
    latxs_ra_free_temp(&bit_base);

    /* Update CF. */
    if (tu_next_eflag_use == 1) {
        if (tu_eflag_opt) {
#ifdef CONFIG_LATX_TU
            TU_EFLAG_OPT_SET_I(tu_data, LISA_X86MTFLAG, bit_eflags, 0x1);
#endif
        } else {
            latxs_append_ir2_opnd1i(LISA_X86MTFLAG, &bit_eflags, 0x1);
        }
    }

    IR2_OPND target_label_opnd = latxs_ir2_opnd_new_label();

    switch (ir1_opcode(next)) {
    case WRAP(JB):   /*CF=1*/
#ifdef CONFIG_LATX_TU
        GEN_TU_FUN_AND_RETURN2(LISA_BNE, bit_eflags, latxs_zero_ir2_opnd, target_label_opnd, next);
#endif
        latxs_append_ir2_opnd3(LISA_BNE, &bit_eflags, &latxs_zero_ir2_opnd, &target_label_opnd);
        break;
    case WRAP(JAE):  /*CF=0*/
#ifdef CONFIG_LATX_TU
        GEN_TU_FUN_AND_RETURN2(LISA_BEQ, bit_eflags, latxs_zero_ir2_opnd, target_label_opnd, next);
#endif
        latxs_append_ir2_opnd3(LISA_BEQ, &bit_eflags, &latxs_zero_ir2_opnd, &target_label_opnd);
        break;
    default:
        lsassert(0);
        break;
    }
    latxs_ra_free_temp(&bit_eflags);

    latxs_tr_gen_eob();
    LOAD_TB_PTR_TO_DBT_ARG1();

    latxs_tr_generate_exit_tb(next, 0);

    latxs_append_ir2_opnd1(LISA_LABEL, &target_label_opnd);

    /* taken */
    latxs_tr_generate_exit_tb(next, 1);

    return true;
}

static inline bool xcomisx_jcc(IR1_INST *pir1, bool is_double, bool qnan_exp)
{
    IR1_INST *curr = pir1;
    TRANSLATION_DATA *td = lsenv->tr_data;
    IR1_INST *list = td->ir1_inst_array;
    IR1_INST *next = NULL;

    lsassert(curr->instptn.next >= 0);

    next = ir1_inst_in_list(list, curr->instptn.next);

    IR1_OPND *opnd0 = ir1_get_opnd(curr, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(curr, 1);
    IR2_OPND dest = latxs_load_freg128_from_ir1(opnd0);
    IR2_OPND src = latxs_load_freg128_from_ir1(opnd1);
    /* 0. set flag = 0 */
    IR2_OPND flag_zf = latxs_ra_alloc_itemp();
    IR2_OPND flag_pf = latxs_ra_alloc_itemp();
    IR2_OPND flag = latxs_ra_alloc_itemp();
    latxs_append_ir2_opnd2_(lisa_mov, &flag, &latxs_zero_ir2_opnd);

    /* 1. check ZF, are they equal & unordered? */
    latxs_append_ir2_opnd3i(LISA_FCMP_COND_S + is_double, &latxs_fcc0_ir2_opnd, &dest, &src,
                            FCMP_COND_CUEQ + qnan_exp);
    latxs_append_ir2_opnd2(LISA_MOVCF2GR, &flag_zf, &latxs_fcc0_ir2_opnd);

    /* 2. check CF, are they less & unordered? */
    latxs_append_ir2_opnd3i(LISA_FCMP_COND_S + is_double, &latxs_fcc2_ir2_opnd, &dest, &src,
                            FCMP_COND_CULT + qnan_exp);
    latxs_append_ir2_opnd2(LISA_MOVCF2GR, &flag, &latxs_fcc2_ir2_opnd);

    /* 3. check PF, are they unordered? (= ZF & CF) */
    latxs_append_ir2_opnd3(LISA_AND, &flag_pf, &flag, &flag_zf);

    latxs_append_ir2_opnd2ii(LISA_BSTRINS_W, &flag, &flag_zf, ZF_BIT_INDEX,
                             ZF_BIT_INDEX);
    latxs_append_ir2_opnd2ii(LISA_BSTRINS_W, &flag, &flag_pf, PF_BIT_INDEX,
                             PF_BIT_INDEX);

    latxs_ra_free_temp(&flag_zf);
    latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
    /* 4. mov flag to EFLAGS */
    latxs_append_ir2_opnd1i(LISA_X86MTFLAG, &flag, 0x3f);
    latxs_ra_free_temp(&flag);

    IR2_OPND* cj = NULL;
    switch (ir1_opcode(next)) {
    case WRAP(JA):
        latxs_append_ir2_opnd3i(LISA_FCMP_COND_S + is_double, &latxs_fcc0_ir2_opnd, &src, &dest,
                            FCMP_COND_CLT + qnan_exp);
        cj = &latxs_fcc0_ir2_opnd;
        break;
    case WRAP(JAE):
        latxs_append_ir2_opnd3i(LISA_FCMP_COND_S + is_double, &latxs_fcc0_ir2_opnd, &src, &dest,
                            FCMP_COND_CLE + qnan_exp);
        cj = &latxs_fcc0_ir2_opnd;
        break;
    case WRAP(JB):
	/* below or NAN, x86 special define */
        cj = &latxs_fcc2_ir2_opnd;
        break;
    case WRAP(JBE):
	/* below or equal or NAN, x86 special define */
        latxs_append_ir2_opnd3i(LISA_FCMP_COND_S + is_double, &latxs_fcc0_ir2_opnd, &dest, &src,
                            FCMP_COND_CULE + qnan_exp);
        cj = &latxs_fcc0_ir2_opnd;
        break;
    case WRAP(JE):
    case WRAP(JLE):
	/* equal or NAN, x86 special define */
        cj = &latxs_fcc0_ir2_opnd;
        break;
    case WRAP(JNE):
    case WRAP(JG):
        latxs_append_ir2_opnd3i(LISA_FCMP_COND_S + is_double, &latxs_fcc0_ir2_opnd, &dest, &src,
                            FCMP_COND_CNE + qnan_exp);
        cj = &latxs_fcc0_ir2_opnd;
        break;
    case WRAP(JP):
    case WRAP(JL):
    case WRAP(JGE):
        break;
    default:
        lsassert(0);
        break;
    }
    latxs_tr_gen_eob();
    LOAD_TB_PTR_TO_DBT_ARG1();

    IR2_OPND target_label = latxs_ir2_opnd_new_label();
#ifdef CONFIG_LATX_TU
    if (ir1_opcode(next) == WRAP(JP)) {
        GEN_TU_FUN_AND_RETURN(LISA_BNEZ, flag_pf, target_label, next);
        latxs_append_ir2_opnd2(LISA_BNEZ, &flag_pf, &target_label);
    } else if (ir1_opcode(next) == WRAP(JGE)) {
        GEN_TU_FUN_AND_RETURN(LISA_BEQZ, latxs_zero_ir2_opnd, target_label, next);
        latxs_append_ir2_opnd1(LISA_B, &target_label);
    } else if (ir1_opcode(next) == WRAP(JL)) {
        IR2_OPND no_target = {._type = IR2_OPND_INV};
        GEN_TU_FUN_AND_RETURN(LISA_INVALID, no_target, no_target, next);
    } else {
        GEN_TU_FUN_AND_RETURN(LISA_BCNEZ, *cj, target_label, next);
        latxs_append_ir2_opnd2(LISA_BCNEZ, cj, &target_label);
    }
#else
    if (ir1_opcode(next) == WRAP(JP)) {
        latxs_append_ir2_opnd2(LISA_BNEZ, &flag_pf, &target_label);
    } else if (ir1_opcode(next) == WRAP(JGE)) {
        latxs_append_ir2_opnd1(LISA_B, &target_label);
    } else if (ir1_opcode(next) == WRAP(JL)) {
    } else {
        latxs_append_ir2_opnd2(LISA_BCNEZ, cj, &target_label);
    }
#endif
    latxs_ra_free_temp(&flag_pf);

    /* not taken */
    latxs_tr_generate_exit_tb(next, 0);

    latxs_append_ir2_opnd1(LISA_LABEL, &target_label);

    /* taken */
    latxs_tr_generate_exit_tb(next, 1);

    return true;
}

static bool translate_comisd_jcc(IR1_INST *pir1)
{
    return xcomisx_jcc(pir1, true, true);
}

static bool translate_comiss_jcc(IR1_INST *pir1)
{
    return xcomisx_jcc(pir1, false, true);
}

static bool translate_ucomisd_jcc(IR1_INST *pir1)
{
    return xcomisx_jcc(pir1, true, false);
}

static bool translate_ucomiss_jcc(IR1_INST *pir1)
{
    return xcomisx_jcc(pir1, false, false);
}

static bool translate_xor_div(IR1_INST *pir1)
{
    IR1_INST *curr = pir1;
    TRANSLATION_DATA *td = lsenv->tr_data;
    IR1_INST *list = td->ir1_inst_array;
    IR1_INST *next = NULL;
    lsassert(curr->instptn.next >= 0);

    next = ir1_inst_in_list(list, curr->instptn.next);

    IR1_OPND *div_opnd0 = ir1_get_opnd(next, 0);
    IR2_OPND src_opnd_0 = latxs_zero_ir2_opnd;
    if (ir1_opnd_size(div_opnd0) == 64) {
        src_opnd_0 = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(div_opnd0));
    } else {
        src_opnd_0 = latxs_ra_alloc_itemp();
        latxs_load_ir1_to_ir2(&src_opnd_0, div_opnd0, EXMode_Z);
    }

    IR2_OPND label_z = latxs_ir2_opnd_new_label();
    latxs_append_ir2_opnd3(LISA_BNE, &src_opnd_0,
            &latxs_zero_ir2_opnd, &label_z);
    latxs_tr_gen_excp_divz(next, 0);
    latxs_append_ir2_opnd1(LISA_LABEL, &label_z);

    IR2_OPND temp_src = latxs_ra_alloc_itemp();
    IR2_OPND temp1_opnd = latxs_ra_alloc_itemp();
    if (ir1_opnd_size(ir1_get_opnd(next, 0)) == 32) {
        IR2_OPND src_opnd_1 = latxs_ra_alloc_itemp();
        latxs_load_ir1_gpr_to_ir2(&src_opnd_1, &eax_ir1_opnd, EXMode_Z);

        latxs_append_ir2_opnd2ii(LISA_BSTRPICK_D, &temp_src, &src_opnd_1, 31, 0);

        latxs_append_ir2_opnd3(LISA_MOD_DU, &temp1_opnd, &temp_src, &src_opnd_0);
        latxs_append_ir2_opnd3(LISA_DIV_DU, &temp_src, &temp_src, &src_opnd_0);

        latxs_store_ir2_to_ir1(&temp_src, &eax_ir1_opnd);
        latxs_store_ir2_to_ir1(&temp1_opnd, &edx_ir1_opnd);
        latxs_ra_free_temp(&src_opnd_1);
    } else if (ir1_opnd_size(ir1_get_opnd(next, 0)) == 64) {
        IR2_OPND src_opnd_1 = latxs_ra_alloc_itemp();
        latxs_load_ir1_gpr_to_ir2(&src_opnd_1, &rax_ir1_opnd, EXMode_Z);

        latxs_append_ir2_opnd3(LISA_OR, &temp_src, &latxs_zero_ir2_opnd, &src_opnd_1);

        latxs_append_ir2_opnd3(LISA_MOD_DU, &temp1_opnd, &temp_src, &src_opnd_0);
        latxs_append_ir2_opnd3(LISA_DIV_DU, &temp_src, &temp_src, &src_opnd_0);

        latxs_store_ir2_to_ir1(&temp_src, &rax_ir1_opnd);
        latxs_store_ir2_to_ir1(&temp1_opnd, &rdx_ir1_opnd);
        latxs_ra_free_temp(&src_opnd_1);
    } else {
        lsassert(0);
    }
    latxs_ra_free_temp(&src_opnd_0);
    latxs_ra_free_temp(&temp_src);
    latxs_ra_free_temp(&temp1_opnd);

    return true;
}

static bool translate_cdq_idiv(IR1_INST *pir1)
{
    IR1_INST *curr = pir1;
    TRANSLATION_DATA *td = lsenv->tr_data;
    IR1_INST *list = td->ir1_inst_array;
    IR1_INST *next = NULL;
    lsassert(curr->instptn.next >= 0);

    next = ir1_inst_in_list(list, curr->instptn.next);
    lsenv->tr_data->curr_ir1_inst = next;
    IR1_OPND *next_opnd_0 = ir1_get_opnd(next, 0);
    lsassert(ir1_opnd_size(next_opnd_0) == 32);

    IR2_OPND src_opnd_0 = latxs_ra_alloc_itemp();
    latxs_load_ir1_to_ir2(&src_opnd_0, next_opnd_0, EXMode_S);

    IR2_OPND label_z = latxs_ir2_opnd_new_label();
    latxs_append_ir2_opnd3(LISA_BNE, &src_opnd_0,
            &latxs_zero_ir2_opnd, &label_z);
    latxs_tr_gen_excp_divz(next, 0);
    latxs_append_ir2_opnd1(LISA_LABEL, &label_z);

    IR2_OPND src_opnd_1 = latxs_ra_alloc_itemp();
    latxs_load_ir1_gpr_to_ir2(&src_opnd_1, &eax_ir1_opnd, EXMode_S);
    IR2_OPND temp_src = latxs_ra_alloc_itemp();
    latxs_append_ir2_opnd3(LISA_OR, &temp_src, &latxs_zero_ir2_opnd, &src_opnd_1);
    latxs_ra_free_temp(&src_opnd_1);
    IR2_OPND temp1_opnd = latxs_ra_alloc_itemp();

    latxs_append_ir2_opnd3(LISA_MOD_D, &temp1_opnd, &temp_src, &src_opnd_0);
    latxs_append_ir2_opnd3(LISA_DIV_D, &temp_src, &temp_src, &src_opnd_0);

    latxs_store_ir2_to_ir1(&temp_src, &eax_ir1_opnd);
    latxs_store_ir2_to_ir1(&temp1_opnd, &edx_ir1_opnd);

    latxs_ra_free_temp(&src_opnd_0);
    latxs_ra_free_temp(&src_opnd_1);
    latxs_ra_free_temp(&temp_src);
    latxs_ra_free_temp(&temp1_opnd);

    return true;
}

static bool translate_ucomisd_seta(IR1_INST *pir1)
{
    IR1_INST *curr = pir1;
    TRANSLATION_DATA *td = lsenv->tr_data;
    IR1_INST *list = td->ir1_inst_array;
    IR1_INST *next = NULL;

    lsassert(curr->instptn.next >= 0);

    next = ir1_inst_in_list(list, curr->instptn.next);

    IR1_OPND *opnd0 = ir1_get_opnd(curr, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(curr, 1);
    IR2_OPND dest = latxs_load_freg128_from_ir1(opnd0);
    IR2_OPND src = latxs_load_freg128_from_ir1(opnd1);
    /* 0. set flag = 0 */
    IR2_OPND flag_zf = latxs_ra_alloc_itemp();
    IR2_OPND flag_pf = latxs_ra_alloc_itemp();
    IR2_OPND flag = latxs_ra_alloc_itemp();
    latxs_append_ir2_opnd2_(lisa_mov, &flag, &latxs_zero_ir2_opnd);

    /* 1. check ZF, are they equal & unordered? */
    latxs_append_ir2_opnd3i(LISA_FCMP_COND_D, &latxs_fcc0_ir2_opnd, &dest, &src,
                            FCMP_COND_CUEQ);
    latxs_append_ir2_opnd2(LISA_MOVCF2GR, &flag_zf, &latxs_fcc0_ir2_opnd);

    /* 2. check CF, are they less & unordered? */
    latxs_append_ir2_opnd3i(LISA_FCMP_COND_D, &latxs_fcc2_ir2_opnd, &dest, &src,
                            FCMP_COND_CULT);
    latxs_append_ir2_opnd2(LISA_MOVCF2GR, &flag, &latxs_fcc2_ir2_opnd);

    /* 3. check PF, are they unordered? (= ZF & CF) */
    latxs_append_ir2_opnd3(LISA_AND, &flag_pf, &flag, &flag_zf);

    latxs_append_ir2_opnd2ii(LISA_BSTRINS_W, &flag, &flag_zf, ZF_BIT_INDEX,
                             ZF_BIT_INDEX);
    latxs_append_ir2_opnd2ii(LISA_BSTRINS_W, &flag, &flag_pf, PF_BIT_INDEX,
                             PF_BIT_INDEX);

    latxs_ra_free_temp(&flag_pf);
    latxs_ra_free_temp(&flag_zf);
    latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
    /* 4. mov flag to EFLAGS */
    latxs_append_ir2_opnd1i(LISA_X86MTFLAG, &flag, 0x3f);
    latxs_ra_free_temp(&flag);

    IR2_OPND* cj = NULL;
    switch (ir1_opcode(next)) {
    case WRAP(SETA):
        latxs_append_ir2_opnd3i(LISA_FCMP_COND_D, &latxs_fcc0_ir2_opnd, &src, &dest,
                            FCMP_COND_CLT);
        cj = &latxs_fcc0_ir2_opnd;
        break;
    default:
        lsassert(0);
        break;
    }

    IR2_OPND condition = latxs_ra_alloc_itemp();
    latxs_append_ir2_opnd2(LISA_MOVCF2GR, &condition, cj);
    latxs_store_ir2_to_ir1(&condition, ir1_get_opnd(next, 0));
    latxs_ra_free_temp(&condition);

    return true;
}

#if 0
#define PPTN(temp1, temp2) do { \
    fprintf(stderr, "temp1: temp2  pid:%d\n", getpid());        \
} while (0)
#else
#define PPTN(temp1, temp2) 
#endif

static
bool translate_cmp_xx_jcc(IR1_INST *pir1)
{
    TRANSLATION_DATA *td = lsenv->tr_data;

    lsassert(pir1->instptn.next >= 0);

    int tu_next_eflag_use = 1;
#ifdef CONFIG_LATX_TU
    TranslationBlock *tb = td->curr_tb;
    TranslationBlock *tu_next_tb = tb->s_data->next_tb[TU_TB_INDEX_NEXT];
    TranslationBlock *tu_target_tb = tb->s_data->next_tb[TU_TB_INDEX_TARGET];
    if (ir1_opcode(pir1) == WRAP(CMP))
    if (td->is_tu_mode && flag_reduction_tu_cmp_xx_jcc() &&
#ifdef CONFIG_SOFTMMU
        (tb->flags & HF_HAMT_EN_MASK) &&
#endif
        tu_next_tb    && tu_next_tb->s_data->eflag_use == 0 &&
        tu_target_tb  && tu_target_tb->s_data->eflag_use == 0)
    {
        tu_next_eflag_use = 0;
    }
#endif

    int tu_eflag_opt = 0;
#ifdef CONFIG_LATX_TU
    if (ir1_opcode(pir1) == WRAP(CMP)) {
        TU_EFLAG_OPT_RESET(tu_data);
    }
    if (td->is_tu_mode && tu_next_eflag_use == 1 &&
        flag_reduction_tu_cmp_xx_jcc() && flag_reduction_tu_next() &&
#ifdef CONFIG_SOFTMMU
        (tb->flags & HF_HAMT_EN_MASK) &&
#endif
        tu_next_tb && tu_next_tb->s_data->eflag_use != 0 &&
        tb_is_adjacent(tb, tu_next_tb) &&
        tu_target_tb && tu_target_tb->s_data->eflag_use == 0)
    {
        tu_eflag_opt = 1;
    }
#endif

#ifdef CONFIG_LATX_TU
    if (td->avoid_tu_eflag_opt) {
        /* do not optimize eflag as request */
        tu_next_eflag_use = 1;
        tu_eflag_opt = 0;
    }
#endif

    if (ir1_opcode(pir1) == WRAP(CMP)) {
        PPTN(cmp_xx_jcc, cmp);
        IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
        IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

        EXMode em = EXMode_S;

        IR2_OPND src0, src1;
        if (ir1_opnd_is_gpr(opnd0)) {
            if (ir1_opnd_size(opnd0) == 64) {
                src0 = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd0));
            } else {
                src0 = latxs_ra_alloc_ptn_itemp();
                latxs_load_ir1_gpr_to_ir2(&src0, opnd0, em);
            }
            if (ir1_opnd_is_gpr(opnd1)) { /* gpr, gpr */
                if (ir1_opnd_size(opnd1) == 64) {
                    src1 = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd1));
                } else {
                    src1 = latxs_ra_alloc_ptn_itemp();
                    latxs_load_ir1_gpr_to_ir2(&src1, opnd1, em);
                }
            } else if (ir1_opnd_is_imm(opnd1)) { /* gpr, imm */
                if (ir1_opnd_simm(opnd1) == 0) {
                    src1 = latxs_zero_ir2_opnd;
                } else {
                    src1 = latxs_ra_alloc_ptn_itemp();
                    latxs_load_imm64(&src1, ir1_opnd_simm(opnd1));
                }
            } else { /* gpr, mem */
                src1 = latxs_ra_alloc_ptn_itemp();
                latxs_load_ir1_mem_to_ir2(&src1, opnd1, em, -1);
            }
        } else {
            lsassert(ir1_opnd_is_mem(opnd0));
            src0 = latxs_ra_alloc_ptn_itemp();
            latxs_load_ir1_mem_to_ir2(&src0, opnd0, em, -1);
            if (ir1_opnd_is_gpr(opnd1)) { /* mem, gpr */
                if (ir1_opnd_size(opnd1) == 64) {
                    src1 = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd1));
                } else {
                    src1 = latxs_ra_alloc_ptn_itemp();
                    latxs_load_ir1_gpr_to_ir2(&src1, opnd1, em);
                }
            } else { /* mem, imm */
                if (ir1_opnd_simm(opnd1) == 0) {
                    src1 = latxs_zero_ir2_opnd;
                } else {
                    src1 = latxs_ra_alloc_ptn_itemp();
                    latxs_load_imm64(&src1, ir1_opnd_simm(opnd1));
                }
            }
        }
        if (tu_next_eflag_use == 1 && !tu_eflag_opt) {
            LATXS_FLAG_REDUCTION_CHECKIF(pir1) {
                int os = ir1_opnd_size(opnd0); /* 8, 16, 32, 64 */
                latxs_append_ir2_opnd2(LISA_X86SUB_B + (os >> 4) - (os >> 6), &src0, &src1);
            }
        }

        if (ir1_opnd_is_gpr(opnd0) && ir1_opnd_size(opnd0) == 64) {
            td->ptn_itemp0 = latxs_ra_alloc_ptn_itemp();
            latxs_append_ir2_opnd2_(lisa_mov, &td->ptn_itemp0, &src0);
        } else {
            td->ptn_itemp0 = src0;
        }

        if (ir1_opnd_is_gpr(opnd1) && ir1_opnd_size(opnd1) == 64) {
            td->ptn_itemp1 = latxs_ra_alloc_ptn_itemp();
            latxs_append_ir2_opnd2_(lisa_mov, &td->ptn_itemp1, &src1);
        } else {
            td->ptn_itemp1 = src1;
        }

#ifdef CONFIG_LATX_TU
        if (tu_eflag_opt) {
            TU_EFLAG_OPT_SET(tu_data);
            int os = ir1_opnd_size(opnd0); /* 8, 16, 32, 64 */
            tu_data->eflag_opt_data.ef_opcode = LISA_X86SUB_B + (os >> 4) - (os >> 6);
            tu_data->eflag_opt_data.ef_opnd0 = td->ptn_itemp0;
            tu_data->eflag_opt_data.ef_opnd1 = td->ptn_itemp1;
        }
#endif
        if (!tu_next_eflag_use || tu_eflag_opt){
            tb->ptn.eflag_type = PTN_EFLAG_TYPE_X86SUB;
            tb->ptn.os = ir1_opnd_size(opnd0); /* 8, 16, 32, 64 */
            tb->ptn.opnd0 = td->ptn_itemp0.val;
            tb->ptn.opnd1 = td->ptn_itemp1.val;
            tb_set_ptn_pcoffset(tb,
                    ir1_addr(pir1) - (tb->pc - tb->cs_base));
        }
    } else {
        if (!td->ptn_itemp_mask) {
            if (tb->ptn.eflag_type != PTN_EFLAG_TYPE_INVALID) {
                /* The eflags calculation is eliminated according to
                 * the next/target tb during tu translation.
                 * Here we can not use X86_SETJ to get the condition.
                 * Go back to translate this tb without optimization. */
                td->avoid_tu_eflag_opt = AVOID_TUEF_START;
            }
            PPTN(cmp_xx_jcc, jcc_cancel);
            switch (ir1_opcode(pir1)) {
            case WRAP(JB):  latxs_translate_jb(pir1); break;
            case WRAP(JAE): latxs_translate_jae(pir1); break;
            case WRAP(JE):  latxs_translate_jz(pir1); break;
            case WRAP(JNE): latxs_translate_jnz(pir1); break;
            case WRAP(JBE): latxs_translate_jbe(pir1); break;
            case WRAP(JA):  latxs_translate_ja(pir1); break;
            case WRAP(JL):  latxs_translate_jl(pir1); break;
            case WRAP(JGE): latxs_translate_jge(pir1); break;
            case WRAP(JLE): latxs_translate_jle(pir1); break;
            case WRAP(JG):  latxs_translate_jg(pir1); break;
            case WRAP(JS):
                latxs_translate_js(pir1);
                break;
            case WRAP(JNS):
                latxs_translate_jns(pir1);
                break;
            default:        lsassert(0);        break;
            }
            return true;
        }
        PPTN(cmp_xx_jcc, jcc);
        IR2_OPND target_label = latxs_ir2_opnd_new_label();
        IR2_OPCODE jcc_op = LISA_INVALID;

        int swap_opnd = 0;
        int test_sf = 0;
        switch (ir1_opcode(pir1)) {
        case WRAP(JB):  jcc_op = LISA_BLTU; break;
        case WRAP(JAE): jcc_op = LISA_BGEU; break;
        case WRAP(JE):  jcc_op = LISA_BEQ;  break;
        case WRAP(JNE): jcc_op = LISA_BNE;  break;
        case WRAP(JBE): jcc_op = LISA_BGEU; swap_opnd = 1; break;
        case WRAP(JA):  jcc_op = LISA_BLTU; swap_opnd = 1; break;
        case WRAP(JL):  jcc_op = LISA_BLT;  break;
        case WRAP(JGE): jcc_op = LISA_BGE;  break;
        case WRAP(JLE): jcc_op = LISA_BGE;  swap_opnd = 1; break;
        case WRAP(JG):  jcc_op = LISA_BLT;  swap_opnd = 1; break;
        case WRAP(JS):
            jcc_op = LISA_BLT;
            test_sf = 1;
            break;
        case WRAP(JNS):
            jcc_op = LISA_BGE;
            test_sf = 1;
            break;
        default:        lsassert(0);        break;
        }

        IR2_OPND jcc_src0 = td->ptn_itemp0;
        IR2_OPND jcc_src1 = td->ptn_itemp1;
        if (test_sf) {
            IR1_INST *cmp = ir1_inst_in_list(td->ir1_inst_array,
                                             pir1->instptn.next);
            int os = ir1_opnd_size(ir1_get_opnd(cmp, 0));

            /* JS/JNS test the subtraction result's SF, independently of OF. */
            jcc_src0 = prepare_cmp_sf(&td->ptn_itemp0,
                                      &td->ptn_itemp1, os);
            jcc_src1 = latxs_zero_ir2_opnd;
        }

        latxs_tr_gen_eob();
        LOAD_TB_PTR_TO_DBT_ARG1();

#ifdef CONFIG_LATX_TU
        if (swap_opnd) {
            GEN_TU_FUN_AND_RETURN2(jcc_op, jcc_src1, jcc_src0,
                                   target_label, pir1);
        } else {
            GEN_TU_FUN_AND_RETURN2(jcc_op, jcc_src0, jcc_src1,
                                   target_label, pir1);
        }
#endif
        if (swap_opnd) {
            latxs_append_ir2_opnd3(jcc_op, &jcc_src1, &jcc_src0,
                                   &target_label);
        } else {
            latxs_append_ir2_opnd3(jcc_op, &jcc_src0, &jcc_src1,
                                   &target_label);
        }
        td->ptn_itemp_mask = 0;
        /* not taken */
        latxs_tr_generate_exit_tb(pir1, 0);

        latxs_append_ir2_opnd1(LISA_LABEL, &target_label);

        /* taken */
        latxs_tr_generate_exit_tb(pir1, 1);
    }

    return true;
}

static
bool translate_test_xx_jcc(IR1_INST *pir1)
{
    TRANSLATION_DATA *td = lsenv->tr_data;

    lsassert(pir1->instptn.next >= 0);

    int tu_next_eflag_use = 1;
#ifdef CONFIG_LATX_TU
    TranslationBlock *tb = td->curr_tb;
    TranslationBlock *tu_next_tb = tb->s_data->next_tb[TU_TB_INDEX_NEXT];
    TranslationBlock *tu_target_tb = tb->s_data->next_tb[TU_TB_INDEX_TARGET];
    if (ir1_opcode(pir1) == WRAP(TEST))
    if (td->is_tu_mode && flag_reduction_tu_test_xx_jcc() &&
#ifdef CONFIG_SOFTMMU
        (tb->flags & HF_HAMT_EN_MASK) &&
#endif
        tu_next_tb    && tu_next_tb->s_data->eflag_use == 0 &&
        tu_target_tb  && tu_target_tb->s_data->eflag_use == 0)
    {
        tu_next_eflag_use = 0;
    }
#endif

    int tu_eflag_opt = 0;
#ifdef CONFIG_LATX_TU
    if (ir1_opcode(pir1) == WRAP(TEST)) {
        TU_EFLAG_OPT_RESET(tu_data);
    }
    if (td->is_tu_mode && tu_next_eflag_use == 1 &&
        flag_reduction_tu_test_xx_jcc() && flag_reduction_tu_next() &&
#ifdef CONFIG_SOFTMMU
        (tb->flags & HF_HAMT_EN_MASK) &&
#endif
        tu_next_tb && tu_next_tb->s_data->eflag_use != 0 &&
        tb_is_adjacent(tb, tu_next_tb) &&
        tu_target_tb && tu_target_tb->s_data->eflag_use == 0)
    {
        tu_eflag_opt = 1;
    }
#endif

#ifdef CONFIG_LATX_TU
    if (td->avoid_tu_eflag_opt) {
        /* do not optimize eflag as request */
        tu_next_eflag_use = 1;
        tu_eflag_opt = 0;
    }
#endif

    if (ir1_opcode(pir1) == WRAP(TEST)) {
        PPTN(test_xx_jcc, test);
        IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
        IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

        int is_same_reg = ir1_opnd_is_same_reg(opnd0, opnd1);
        int os = ir1_opnd_size(opnd0); /* 8, 16, 32, 64 */
        if (is_same_reg) {
            IR2_OPND src0;
            if (ir1_opnd_is_gpr(opnd0) && ir1_opnd_size(opnd0) == 64) {
                src0 = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd0));
            } else {
                src0 = latxs_ra_alloc_ptn_itemp();
                latxs_load_ir1_gpr_to_ir2(&src0, opnd0, EXMode_S);
            }
            if (tu_next_eflag_use == 1 && !tu_eflag_opt) {
                LATXS_FLAG_REDUCTION_CHECKIF(pir1)
                latxs_append_ir2_opnd2(LISA_X86AND_B + (os >> 4) - (os >> 6), &src0, &src0);
            }

            if (ir1_opnd_is_gpr(opnd0) && ir1_opnd_size(opnd0) == 64) {
                td->ptn_itemp0 = latxs_ra_alloc_ptn_itemp();
                latxs_append_ir2_opnd2_(lisa_mov, &td->ptn_itemp0, &src0);
            } else {
                td->ptn_itemp0 = src0;
            }
        } else {
            IR2_OPND src0;
            IR2_OPND src1;
            if (ir1_opnd_is_gpr(opnd0) && (ir1_opnd_size(opnd0) == 64)) {
                src0 = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd0));
            } else {
                src0 = latxs_ra_alloc_itemp();
                latxs_load_ir1_to_ir2(&src0, opnd0, EXMode_S);
            }
            if (ir1_opnd_is_gpr(opnd1) && (ir1_opnd_size(opnd1) == 64)) {
                src1 = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd1));
            } else {
                src1 = latxs_ra_alloc_itemp();
                latxs_load_ir1_to_ir2(&src1, opnd1, EXMode_S);
            }
            if (tu_next_eflag_use == 1 && !tu_eflag_opt) {
                LATXS_FLAG_REDUCTION_CHECKIF(pir1)
                latxs_append_ir2_opnd2(LISA_X86AND_B + (os >> 4) - (os >> 6), &src0, &src1);
            }
            td->ptn_itemp0 = latxs_ra_alloc_ptn_itemp();
            latxs_append_ir2_opnd3(LISA_AND, &(td->ptn_itemp0), &src0, &src1);
        }
#ifdef CONFIG_LATX_TU
        if (tu_eflag_opt) {
            TU_EFLAG_OPT_SET(tu_data);
            int os = ir1_opnd_size(opnd0); /* 8, 16, 32, 64 */
            tu_data->eflag_opt_data.ef_opcode = LISA_X86AND_B + (os >> 4) - (os >> 6);
            tu_data->eflag_opt_data.ef_opnd0 = td->ptn_itemp0;
            tu_data->eflag_opt_data.ef_opnd1 = td->ptn_itemp0;
        }
#endif
        if (!tu_next_eflag_use || tu_eflag_opt){
            tb->ptn.eflag_type = PTN_EFLAG_TYPE_X86AND;
            tb->ptn.os = ir1_opnd_size(opnd0); /* 8, 16, 32, 64 */
            tb->ptn.opnd0 = td->ptn_itemp0.val;
            tb->ptn.opnd1 = td->ptn_itemp0.val;
            tb_set_ptn_pcoffset(tb,
                    ir1_addr(pir1) - (tb->pc - tb->cs_base));
        }
    } else {
        if (!td->ptn_itemp_mask) {
            if (tb->ptn.eflag_type != PTN_EFLAG_TYPE_INVALID) {
                /* The eflags calculation is eliminated according to
                 * the next/target tb during tu translation.
                 * Here we can not use X86_SETJ to get the condition.
                 * Go back to translate this tb without optimization. */
                td->avoid_tu_eflag_opt = AVOID_TUEF_START;
            }
            PPTN(test_xx_jcc, jcc_cancel);
            switch (ir1_opcode(pir1)) {
            case WRAP(JE):  latxs_translate_jz(pir1); break;
            case WRAP(JNE): latxs_translate_jnz(pir1); break;
            case WRAP(JS):  latxs_translate_js(pir1); break;
            case WRAP(JNS): latxs_translate_jns(pir1); break;
            case WRAP(JLE): latxs_translate_jle(pir1); break;
            case WRAP(JG):  latxs_translate_jg(pir1); break;
            case WRAP(JNO): latxs_translate_jno(pir1); break;
            case WRAP(JO): latxs_translate_jo(pir1); break;
            case WRAP(JB): latxs_translate_jb(pir1); break;
            case WRAP(JBE): latxs_translate_jbe(pir1); break;
            case WRAP(JA): latxs_translate_ja(pir1); break;
            case WRAP(JAE): latxs_translate_jae(pir1); break;
            default:        lsassert(0);        break;
            }
            return true;
        }
        PPTN(test_xx_jcc, jcc);
        IR2_OPND *zero = &latxs_zero_ir2_opnd;
        IR2_OPND target_label = latxs_ir2_opnd_new_label();

        latxs_tr_gen_eob();
        LOAD_TB_PTR_TO_DBT_ARG1();

        switch (ir1_opcode(pir1)) {
        case WRAP(JE):
#ifdef CONFIG_LATX_TU
            GEN_TU_FUN_AND_RETURN(LISA_BEQZ, td->ptn_itemp0, target_label, pir1);
#endif
            latxs_append_ir2_opnd2(LISA_BEQZ, &td->ptn_itemp0, &target_label);
            break;
        case WRAP(JNE):
#ifdef CONFIG_LATX_TU
            GEN_TU_FUN_AND_RETURN(LISA_BNEZ, td->ptn_itemp0, target_label, pir1);
#endif
            latxs_append_ir2_opnd2(LISA_BNEZ, &td->ptn_itemp0, &target_label);
            break;
        case WRAP(JS):
#ifdef CONFIG_LATX_TU
            GEN_TU_FUN_AND_RETURN2(LISA_BLT, td->ptn_itemp0, *zero, target_label, pir1);
#endif
            latxs_append_ir2_opnd3(LISA_BLT, &td->ptn_itemp0, zero, &target_label);
            break;
        case WRAP(JNS):
#ifdef CONFIG_LATX_TU
            GEN_TU_FUN_AND_RETURN2(LISA_BGE, td->ptn_itemp0, *zero, target_label, pir1);
#endif
            latxs_append_ir2_opnd3(LISA_BGE, &td->ptn_itemp0, zero, &target_label);
            break;
        case WRAP(JLE):
#ifdef CONFIG_LATX_TU
            GEN_TU_FUN_AND_RETURN2(LISA_BGE, *zero, td->ptn_itemp0, target_label, pir1);
#endif
            latxs_append_ir2_opnd3(LISA_BGE, zero, &td->ptn_itemp0, &target_label);
            break;
        case WRAP(JG):
#ifdef CONFIG_LATX_TU
            GEN_TU_FUN_AND_RETURN2(LISA_BLT, *zero, td->ptn_itemp0, target_label, pir1);
#endif
            latxs_append_ir2_opnd3(LISA_BLT, zero, &td->ptn_itemp0, &target_label);
            break;
        case WRAP(JNO):
            /* OF = 0 For compatibility with bcc+b */
#ifdef CONFIG_LATX_TU
            {
                IR2_OPND no_next = {._type = IR2_OPND_INV};
                GEN_TU_FUN_AND_RETURN(LISA_B, no_next, target_label, pir1);
            }
#endif
            latxs_append_ir2_opnd1(LISA_B, &target_label);
            break;
        case WRAP(JO):
            /* TODO tb->opt_bcc = false;*/
            /* OF = 1 */
        case WRAP(JB):
            /* TODO tb->opt_bcc = false;*/
            /* CF = 1 */
#ifdef CONFIG_LATX_TU
            {
                IR2_OPND no_target = {._type = IR2_OPND_INV};
                GEN_TU_FUN_AND_RETURN(LISA_INVALID, no_target, no_target, pir1);
            }
#endif
            break;
        case WRAP(JBE):
            /* CF = 1 or ZF = 1 */
#ifdef CONFIG_LATX_TU
            GEN_TU_FUN_AND_RETURN(LISA_BEQZ, td->ptn_itemp0, target_label, pir1);
#endif
            latxs_append_ir2_opnd2(LISA_BEQZ, &td->ptn_itemp0, &target_label);
            break;
        case WRAP(JA):
            /* CF = 0 and ZF = 0 */
#ifdef CONFIG_LATX_TU
            GEN_TU_FUN_AND_RETURN(LISA_BNEZ, td->ptn_itemp0, target_label, pir1);
#endif
            latxs_append_ir2_opnd2(LISA_BNEZ, &td->ptn_itemp0, &target_label);
            break;
        case WRAP(JAE):
            /* CF = 0 For compatibility with bcc+b */
#ifdef CONFIG_LATX_TU
            {
                IR2_OPND no_next = {._type = IR2_OPND_INV};
                GEN_TU_FUN_AND_RETURN(LISA_B, no_next, target_label, pir1);
            }
#endif
            latxs_append_ir2_opnd1(LISA_B, &target_label);
            break;
        default:
            lsassert(0);
            break;
        }

        td->ptn_itemp_mask = 0;

        /* not taken */
        latxs_tr_generate_exit_tb(pir1, 0);

        latxs_append_ir2_opnd1(LISA_LABEL, &target_label);

        /* taken */
        latxs_tr_generate_exit_tb(pir1, 1);
    }

    return true;
}

static
bool translate_bt_xx_jcc(IR1_INST *pir1)
{
    IR1_INST *curr = pir1;
    TRANSLATION_DATA *td = lsenv->tr_data;

    lsassert(curr->instptn.next >= 0);

#ifdef CONFIG_LATX_TU
    TranslationBlock *tb = td->curr_tb;
    TranslationBlock *tu_next_tb = tb->s_data->next_tb[TU_TB_INDEX_NEXT];
    TranslationBlock *tu_target_tb = tb->s_data->next_tb[TU_TB_INDEX_TARGET];
#endif

    int tu_next_eflag_use = 1;
#ifdef CONFIG_LATX_TU
    if (ir1_opcode(curr) == WRAP(BT) && td->is_tu_mode &&
        flag_reduction_tu_bt_xx_jcc() &&
#ifdef CONFIG_SOFTMMU
        (tb->flags & HF_HAMT_EN_MASK) &&
#endif
        tu_next_tb    && tu_next_tb->s_data->eflag_use == 0 &&
        tu_target_tb  && tu_target_tb->s_data->eflag_use == 0)
    {
        tu_next_eflag_use = 0;
    }
#endif

    int tu_eflag_opt = 0;
#ifdef CONFIG_LATX_TU
    if (ir1_opcode(curr) == WRAP(BT)) {
        TU_EFLAG_OPT_RESET(tu_data);
    }
    if (td->is_tu_mode && tu_next_eflag_use == 1 &&
        flag_reduction_tu_bt_xx_jcc() && flag_reduction_tu_next() &&
#ifdef CONFIG_SOFTMMU
        (tb->flags & HF_HAMT_EN_MASK) &&
#endif
        tu_next_tb && tu_next_tb->s_data->eflag_use != 0 &&
        tb_is_adjacent(tb, tu_next_tb) &&
        tu_target_tb && tu_target_tb->s_data->eflag_use == 0)
    {
        tu_eflag_opt = 1;
    }

    if (td->avoid_tu_eflag_opt) {
        tu_next_eflag_use = 1;
        tu_eflag_opt = 0;
    }
#endif

    if (ir1_opcode(curr) == WRAP(BT)) {
        PPTN(bt_xx_jcc, bt);
        IR1_OPND *bt_opnd0 = ir1_get_opnd(curr, 0);
        IR1_OPND *bt_opnd1 = ir1_get_opnd(curr, 1);

        int t_imm = 0;
        int t_addr_shift = 0;
        switch (ir1_opnd_size(bt_opnd0)) {
        case 16:
            t_imm = 0xf;
            t_addr_shift = 1;
            break;
        case 32:
            t_imm = 0x1f;
            t_addr_shift = 2;
            break;
        case 64:
            t_imm = 0x3f;
            t_addr_shift = 3;
            break;
        default:
            lsassertm(0, "Invalid operand size (%d) in translate_btx.\n",
                    ir1_opnd_size(bt_opnd0));
            break;
        }

        IR2_OPND bit_base = latxs_ra_alloc_itemp();
        IR2_OPND bit_off = latxs_ra_alloc_itemp();
        IR2_OPND mem_base;
        int mem_offset;

        /* get bit_base and bit_off */
        if (ir1_opnd_is_gpr(bt_opnd0)) {
            latxs_load_ir1_to_ir2(&bit_base, bt_opnd0, EXMode_Z);
            latxs_load_ir1_to_ir2(&bit_off,  bt_opnd1, EXMode_Z);
            latxs_append_ir2_opnd2i(LISA_ANDI, &bit_off, &bit_off, t_imm);
        } else {
            /* bt_opnd0 is memory operand */
            if (ir1_opnd_is_imm(bt_opnd1)) {
                latxs_load_ir1_to_ir2(&bit_off, bt_opnd1, EXMode_Z);
                latxs_append_ir2_opnd2i(LISA_ANDI, &bit_off, &bit_off, t_imm);
            } else {
                latxs_load_ir1_to_ir2(&bit_off, bt_opnd1, EXMode_S);
            }

            latxs_convert_mem_opnd(&mem_base, &mem_offset, bt_opnd0, -1);
            if (mem_offset != 0) {
                latxs_append_ir2_opnd2i(LISA_ADDI_D, &mem_base, &mem_base, mem_offset);
#ifdef LATXS_IMM_CACHE_ENABLE
                imm_cache_addi_update(mem_offset);
#endif
            }

            IR2_OPND byte_nr = latxs_ra_alloc_itemp();

            latxs_append_ir2_opnd2i(LISA_SRAI_D, &byte_nr, &bit_off, 3 + t_addr_shift);
            latxs_append_ir2_opnd2i(LISA_SLLI_D, &byte_nr, &byte_nr, t_addr_shift);
            latxs_append_ir2_opnd2i(LISA_ANDI, &bit_off, &bit_off, t_imm);

            latxs_append_ir2_opnd3(LISA_ADD_D, &mem_base, &mem_base, &byte_nr);
            latxs_ra_free_temp(&byte_nr);

            int addr_size = latxs_ir1_addr_size(curr);
            if (addr_size == 4) {
                latxs_append_ir2_opnd2_(lisa_mov32z, &mem_base, &mem_base);
            } else if (addr_size == 2) {
                latxs_append_ir2_opnd2_(lisa_mov16z, &mem_base, &mem_base);
            }

            switch (ir1_opnd_size(bt_opnd0)) {
            case 16:
                gen_ldst_softmmu_helper(LISA_LD_HU, &bit_base, &mem_base, 0, 1);
                break;
            case 32:
                gen_ldst_softmmu_helper(LISA_LD_WU, &bit_base, &mem_base, 0, 1);
                break;
            case 64:
                gen_ldst_softmmu_helper(LISA_LD_D, &bit_base, &mem_base, 0, 1);
                break;
            default:
                lsassertm(0, "Invalid operand size (%d) in translate_btx.\n",
                        ir1_opnd_size(bt_opnd0));
                break;
            }
        }

        IR2_OPND bit_eflags = latxs_ra_alloc_ptn_itemp();
        latxs_append_ir2_opnd3(LISA_SRL_D, &bit_eflags, &bit_base, &bit_off);

        latxs_ra_free_temp(&bit_off);
        latxs_ra_free_temp(&bit_base);

        /* update EFALGS */
        latxs_append_ir2_opnd2i(LISA_ANDI, &bit_eflags, &bit_eflags, 1);
        if (tu_next_eflag_use == 1 && !tu_eflag_opt) {
            latxs_append_ir2_opnd1i(LISA_X86MTFLAG, &bit_eflags, 0x1);
        }

        td->ptn_itemp0 = bit_eflags;
#ifdef CONFIG_LATX_TU
        if (tu_eflag_opt) {
            TU_EFLAG_OPT_SET_I(tu_data, LISA_X86MTFLAG, bit_eflags, 0x1);
        }
#endif
        if (!tu_next_eflag_use || tu_eflag_opt) {
            tb->ptn.eflag_type = PTN_EFLAG_TYPE_X86MTFLAG;
            tb->ptn.os = ir1_opnd_size(bt_opnd0);
            tb->ptn.opnd0 = td->ptn_itemp0.val;
            tb->ptn.opnd1 = 0x1;
            tb_set_ptn_pcoffset(tb,
                    ir1_addr(curr) - (tb->pc - tb->cs_base));
        }
    } else {
        if (!td->ptn_itemp_mask) {
            if (tb->ptn.eflag_type != PTN_EFLAG_TYPE_INVALID) {
                /*
                 * The eflags calculation is eliminated according to
                 * the next/target tb during tu translation.
                 * Go back to translate this tb without optimization.
                 */
                td->avoid_tu_eflag_opt = AVOID_TUEF_START;
            }
            PPTN(bt_xx_jcc, jcc_cancel);
            switch (ir1_opcode(curr)) {
            case WRAP(JB):  latxs_translate_jb(curr); break;
            case WRAP(JAE): latxs_translate_jae(curr); break;
            default:        lsassert(0);        break;
            }
            return true;
        }
        PPTN(bt_xx_jcc, jcc);
        IR2_OPND target_label_opnd = latxs_ir2_opnd_new_label();

        switch (ir1_opcode(curr)) {
        case WRAP(JB):   /*CF=1*/
#ifdef CONFIG_LATX_TU
            GEN_TU_FUN_AND_RETURN2(LISA_BNE, td->ptn_itemp0, latxs_zero_ir2_opnd, target_label_opnd, pir1);
#endif
            latxs_append_ir2_opnd3(LISA_BNE, &td->ptn_itemp0, &latxs_zero_ir2_opnd, &target_label_opnd);
            break;
        case WRAP(JAE):  /*CF=0*/
#ifdef CONFIG_LATX_TU
            GEN_TU_FUN_AND_RETURN2(LISA_BEQ, td->ptn_itemp0, latxs_zero_ir2_opnd, target_label_opnd, pir1);
#endif
            latxs_append_ir2_opnd3(LISA_BEQ, &td->ptn_itemp0, &latxs_zero_ir2_opnd, &target_label_opnd);
            break;
        default:
            lsassert(0);
            break;
        }

        td->ptn_itemp_mask = 0;

        latxs_tr_gen_eob();
        LOAD_TB_PTR_TO_DBT_ARG1();

        latxs_tr_generate_exit_tb(curr, 0);

        latxs_append_ir2_opnd1(LISA_LABEL, &target_label_opnd);

        /* taken */
        latxs_tr_generate_exit_tb(curr, 1);
    }

    return true;
}

static inline bool xcomisx_xx_jcc(IR1_INST *pir1, bool is_jcc, bool is_double, bool qnan_exp)
{
    IR1_INST *curr = pir1;
    TRANSLATION_DATA *td = lsenv->tr_data;
    IR1_INST *list = td->ir1_inst_array;
    IR1_INST *next = NULL;

    lsassert(curr->instptn.next >= 0);

    next = ir1_inst_in_list(list, curr->instptn.next);

    if (!is_jcc) {
        IR1_OPND *opnd0 = ir1_get_opnd(curr, 0);
        IR1_OPND *opnd1 = ir1_get_opnd(curr, 1);
        IR2_OPND dest = latxs_load_freg128_from_ir1(opnd0);
        IR2_OPND src = latxs_load_freg128_from_ir1(opnd1);
        /* 0. set flag = 0 */
        IR2_OPND flag_zf = latxs_ra_alloc_itemp();
        IR2_OPND flag_pf = latxs_ra_alloc_ptn_itemp();
        IR2_OPND flag = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd2_(lisa_mov, &flag, &latxs_zero_ir2_opnd);

        /* 1. check ZF, are they equal & unordered? */
        latxs_append_ir2_opnd3i(LISA_FCMP_COND_S + is_double, &latxs_fcc0_ir2_opnd, &dest, &src,
                                FCMP_COND_CUEQ + qnan_exp);
        latxs_append_ir2_opnd2(LISA_MOVCF2GR, &flag_zf, &latxs_fcc0_ir2_opnd);

        /* 2. check CF, are they less & unordered? */
        latxs_append_ir2_opnd3i(LISA_FCMP_COND_S + is_double, &latxs_fcc2_ir2_opnd, &dest, &src,
                                FCMP_COND_CULT + qnan_exp);
        latxs_append_ir2_opnd2(LISA_MOVCF2GR, &flag, &latxs_fcc2_ir2_opnd);

        /* 3. check PF, are they unordered? (= ZF & CF) */
        latxs_append_ir2_opnd3(LISA_AND, &flag_pf, &flag, &flag_zf);

        latxs_append_ir2_opnd2ii(LISA_BSTRINS_W, &flag, &flag_zf, ZF_BIT_INDEX,
                                ZF_BIT_INDEX);
        latxs_append_ir2_opnd2ii(LISA_BSTRINS_W, &flag, &flag_pf, PF_BIT_INDEX,
                                PF_BIT_INDEX);

        latxs_ra_free_temp(&flag_zf);
        latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
        /* 4. mov flag to EFLAGS */
        latxs_append_ir2_opnd1i(LISA_X86MTFLAG, &flag, 0x3f);
        latxs_ra_free_temp(&flag);

        switch (ir1_opcode(next)) {
        case WRAP(JA):
            latxs_append_ir2_opnd3i(LISA_FCMP_COND_S + is_double, &latxs_fcc0_ir2_opnd, &src, &dest,
                                FCMP_COND_CLT + qnan_exp);
            break;
        case WRAP(JAE):
            latxs_append_ir2_opnd3i(LISA_FCMP_COND_S + is_double, &latxs_fcc0_ir2_opnd, &src, &dest,
                                FCMP_COND_CLE + qnan_exp);
            break;
        case WRAP(JB):
        /* below or NAN, x86 special define */
            break;
        case WRAP(JBE):
        /* below or equal or NAN, x86 special define */
            latxs_append_ir2_opnd3i(LISA_FCMP_COND_S + is_double, &latxs_fcc0_ir2_opnd, &dest, &src,
                                FCMP_COND_CULE + qnan_exp);
            break;
        case WRAP(JE):
        case WRAP(JLE):
        /* equal or NAN, x86 special define */
            break;
        case WRAP(JNE):
        case WRAP(JG):
            latxs_append_ir2_opnd3i(LISA_FCMP_COND_S + is_double, &latxs_fcc0_ir2_opnd, &dest, &src,
                                FCMP_COND_CNE + qnan_exp);
            break;
        case WRAP(JP):
            td->ptn_itemp0 = flag_pf;
            break;
        case WRAP(JL):
        case WRAP(JGE):
            break;
        default:
            lsassert(0);
            break;
        }
    } else {
        IR2_OPND* cj = NULL;
        switch (ir1_opcode(curr)) {
        case WRAP(JA):
            cj = &latxs_fcc0_ir2_opnd;
            break;
        case WRAP(JAE):
            cj = &latxs_fcc0_ir2_opnd;
            break;
        case WRAP(JB):
        /* below or NAN, x86 special define */
            cj = &latxs_fcc2_ir2_opnd;
            break;
        case WRAP(JBE):
        /* below or equal or NAN, x86 special define */
            cj = &latxs_fcc0_ir2_opnd;
            break;
        case WRAP(JE):
        case WRAP(JLE):
        /* equal or NAN, x86 special define */
            cj = &latxs_fcc0_ir2_opnd;
            break;
        case WRAP(JNE):
        case WRAP(JG):
            cj = &latxs_fcc0_ir2_opnd;
            break;
        case WRAP(JP):
            if (!td->ptn_itemp_mask) {
                return latxs_translate_jp(curr);
            }
            break;
        case WRAP(JL):
        case WRAP(JGE):
            break;
        default:
            lsassert(0);
            break;
        }
        latxs_tr_gen_eob();
        LOAD_TB_PTR_TO_DBT_ARG1();

        IR2_OPND target_label = latxs_ir2_opnd_new_label();
#ifdef CONFIG_LATX_TU
        if (ir1_opcode(curr) == WRAP(JP)) {
            GEN_TU_FUN_AND_RETURN(LISA_BNEZ, td->ptn_itemp0, target_label,curr);
            latxs_append_ir2_opnd2(LISA_BNEZ, &td->ptn_itemp0, &target_label);
        } else if (ir1_opcode(curr) == WRAP(JGE)) {
            GEN_TU_FUN_AND_RETURN(LISA_BEQZ, latxs_zero_ir2_opnd, target_label, curr);
            latxs_append_ir2_opnd1(LISA_B, &target_label);
        } else if (ir1_opcode(curr) == WRAP(JL)) {
            IR2_OPND no_target = {._type = IR2_OPND_INV};
            GEN_TU_FUN_AND_RETURN(LISA_INVALID, no_target, no_target, curr);
        } else {
            GEN_TU_FUN_AND_RETURN(LISA_BCNEZ, *cj, target_label, curr);
            latxs_append_ir2_opnd2(LISA_BCNEZ, cj, &target_label);
        }
#else
        if (ir1_opcode(curr) == WRAP(JP)) {
            latxs_append_ir2_opnd2(LISA_BNEZ, &td->ptn_itemp0, &target_label);
            td->ptn_itemp_mask = 0;
        } else if (ir1_opcode(curr) == WRAP(JGE)) {
            latxs_append_ir2_opnd1(LISA_B, &target_label);
        } else if (ir1_opcode(curr) == WRAP(JL)) {
        } else {
            latxs_append_ir2_opnd2(LISA_BCNEZ, cj, &target_label);
        }
#endif

        /* not taken */
        latxs_tr_generate_exit_tb(curr, 0);

        latxs_append_ir2_opnd1(LISA_LABEL, &target_label);

        /* taken */
        latxs_tr_generate_exit_tb(curr, 1);

    }
    return true;
}

static bool translate_comisd_xx_jcc(IR1_INST *pir1)
{
    if (ir1_opcode(pir1) == WRAP(COMISD)) {
        PPTN(comisd_xx_jcc, comisd);
        return xcomisx_xx_jcc(pir1, false, true, true);
    } else {
        lsassert(ir1_opcode(pir1) == WRAP(JA) ||
            ir1_opcode(pir1) == WRAP(JAE) ||
            ir1_opcode(pir1) == WRAP(JB) ||
            ir1_opcode(pir1) == WRAP(JBE) ||
            ir1_opcode(pir1) == WRAP(JNE) ||
            ir1_opcode(pir1) == WRAP(JE) ||
            ir1_opcode(pir1) == WRAP(JL) ||
            ir1_opcode(pir1) == WRAP(JGE) ||
            ir1_opcode(pir1) == WRAP(JLE) ||
            ir1_opcode(pir1) == WRAP(JG));
        PPTN(comisd_xx_jcc, jcc);
        return xcomisx_xx_jcc(pir1, true, true, true);
    }
}

static bool translate_comiss_xx_jcc(IR1_INST *pir1)
{
    if (ir1_opcode(pir1) == WRAP(COMISS)) {
        PPTN(comiss_xx_jcc, comiss);
        return xcomisx_xx_jcc(pir1, false, false, true);
    } else {
        lsassert(ir1_opcode(pir1) == WRAP(JA) ||
            ir1_opcode(pir1) == WRAP(JAE) ||
            ir1_opcode(pir1) == WRAP(JB) ||
            ir1_opcode(pir1) == WRAP(JBE) ||
            ir1_opcode(pir1) == WRAP(JNE) ||
            ir1_opcode(pir1) == WRAP(JE) ||
            ir1_opcode(pir1) == WRAP(JL) ||
            ir1_opcode(pir1) == WRAP(JGE) ||
            ir1_opcode(pir1) == WRAP(JLE) ||
            ir1_opcode(pir1) == WRAP(JG));
        PPTN(comiss_xx_jcc, jcc);
        return xcomisx_xx_jcc(pir1, true, false, true);
    }
}

static bool translate_ucomisd_xx_jcc(IR1_INST *pir1)
{
    if (ir1_opcode(pir1) == WRAP(UCOMISD)) {
        PPTN(ucomisd_xx_jcc, ucomisd);
        return xcomisx_xx_jcc(pir1, false, true, false);
    } else {
        lsassert(ir1_opcode(pir1) == WRAP(JA) ||
            ir1_opcode(pir1) == WRAP(JAE) ||
            ir1_opcode(pir1) == WRAP(JB) ||
            ir1_opcode(pir1) == WRAP(JBE) ||
            ir1_opcode(pir1) == WRAP(JNE) ||
            ir1_opcode(pir1) == WRAP(JE) ||
            ir1_opcode(pir1) == WRAP(JL) ||
            ir1_opcode(pir1) == WRAP(JGE) ||
            ir1_opcode(pir1) == WRAP(JLE) ||
            ir1_opcode(pir1) == WRAP(JG));
        PPTN(ucomisd_xx_jcc, jcc);
        return xcomisx_xx_jcc(pir1, true, true, false);
    }
}

static bool translate_ucomiss_xx_jcc(IR1_INST *pir1)
{
    if (ir1_opcode(pir1) == WRAP(UCOMISS)) {
        PPTN(ucomiss_xx_jcc, ucomiss);
        return xcomisx_xx_jcc(pir1, false, false, false);
    } else {
        lsassert(ir1_opcode(pir1) == WRAP(JA) ||
            ir1_opcode(pir1) == WRAP(JAE) ||
            ir1_opcode(pir1) == WRAP(JB) ||
            ir1_opcode(pir1) == WRAP(JBE) ||
            ir1_opcode(pir1) == WRAP(JNE) ||
            ir1_opcode(pir1) == WRAP(JE) ||
            ir1_opcode(pir1) == WRAP(JL) ||
            ir1_opcode(pir1) == WRAP(JGE) ||
            ir1_opcode(pir1) == WRAP(JLE) ||
            ir1_opcode(pir1) == WRAP(JG));
        PPTN(ucomiss_xx_jcc, jcc);
        return xcomisx_xx_jcc(pir1, true, false, false);
    }
}

static
bool translate_dec_jcc(IR1_INST *pir1)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    IR1_INST *list = td->ir1_inst_array;

#ifdef CONFIG_LATX_TU
    TranslationBlock *tb = td->curr_tb;
    TranslationBlock *tu_next_tb =
        tb->s_data->next_tb[TU_TB_INDEX_NEXT];
    TranslationBlock *tu_target_tb =
        tb->s_data->next_tb[TU_TB_INDEX_TARGET];
#endif

    lsassert(pir1->instptn.next >= 0);

    IR1_INST *jcc = ir1_inst_in_list(list, pir1->instptn.next);
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    int os = ir1_opnd_size(opnd0); /* 8, 16, 32, 64 */

    lsassert(ir1_opnd_is_gpr(opnd0));
    lsassert(ir1_opcode(jcc) == WRAP(JE) ||
             ir1_opcode(jcc) == WRAP(JNE));

    int tu_next_eflag_use = 1;
#ifdef CONFIG_LATX_TU
    if (td->is_tu_mode && flag_reduction_tu_dec_jcc() &&
        tu_next_tb && tu_next_tb->s_data->eflag_use == 0 &&
        tu_target_tb && tu_target_tb->s_data->eflag_use == 0) {
        tu_next_eflag_use = 0;
    }
#endif

    IR2_OPND src = latxs_ra_alloc_itemp();
    IR2_OPND dest = latxs_ra_alloc_itemp();
    latxs_load_ir1_to_ir2(&src, opnd0, EXMode_Z);

    if (tu_next_eflag_use) {
        LATXS_FLAG_REDUCTION_CHECKIF(pir1) {
            latxs_append_ir2_opnd1(LISA_X86DEC_B + (os >> 4) -
                                   (os >> 6), &src);
        }
    }

    latxs_append_ir2_opnd2i(LISA_ADDI_D, &dest, &src, -1);
    latxs_store_ir2_to_ir1(&dest, opnd0);

    IR2_OPND cond = dest;
    if (os != 64) {
        cond = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd2_(os == 32 ? lisa_mov32z :
                                os == 16 ? lisa_mov16z : lisa_mov8z,
                                &cond, &dest);
    }

    IR2_OPND target_label = latxs_ir2_opnd_new_label();

    latxs_tr_gen_eob();
    LOAD_TB_PTR_TO_DBT_ARG1();

    if (ir1_opcode(jcc) == WRAP(JE)) {
#ifdef CONFIG_LATX_TU
        GEN_TU_FUN_AND_RETURN(LISA_BEQZ, cond, target_label, jcc);
#endif
        latxs_append_ir2_opnd2(LISA_BEQZ, &cond, &target_label);
    } else {
#ifdef CONFIG_LATX_TU
        GEN_TU_FUN_AND_RETURN(LISA_BNEZ, cond, target_label, jcc);
#endif
        latxs_append_ir2_opnd2(LISA_BNEZ, &cond, &target_label);
    }

    /* not taken */
    latxs_tr_generate_exit_tb(jcc, 0);

    latxs_append_ir2_opnd1(LISA_LABEL, &target_label);

    /* taken */
    latxs_tr_generate_exit_tb(jcc, 1);

    return true;
}

static
bool translate_add_jcc(IR1_INST *pir1)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    IR1_INST *list = td->ir1_inst_array;
    IR1_INST *nir1 = NULL;

#ifdef CONFIG_LATX_TU
    TranslationBlock *tb = td->curr_tb;
    TranslationBlock *tu_next_tb = tb->s_data->next_tb[TU_TB_INDEX_NEXT];
    TranslationBlock *tu_target_tb = tb->s_data->next_tb[TU_TB_INDEX_TARGET];
#endif

    lsassert(pir1->instptn.next >= 0);

    nir1 = ir1_inst_in_list(list, pir1->instptn.next);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    int tu_next_eflag_use = 1;
#ifdef CONFIG_LATX_TU
    if (td->is_tu_mode && flag_reduction_tu_add_jcc() &&
        tu_next_tb    && tu_next_tb->s_data->eflag_use == 0 &&
        tu_target_tb  && tu_target_tb->s_data->eflag_use == 0)
    {
        tu_next_eflag_use = 0;
    }
#endif

    IR2_OPND src0;
    IR2_OPND src1;
    IR2_OPND temp;
    if (ir1_opnd_is_gpr(opnd0) && (ir1_opnd_size(opnd0) == 64)) {
        src0 = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd0));
    } else {
        src0 = latxs_ra_alloc_itemp();
        latxs_load_ir1_to_ir2(&src0, opnd0, EXMode_S);
    }
    int is_same_reg = ir1_opnd_is_same_reg(opnd0, opnd1);
    int os = ir1_opnd_size(opnd0); /* 8, 16, 32, 64 */
    if (is_same_reg) {
        if (ir1_opnd_is_gpr(opnd0) && (ir1_opnd_size(opnd0) == 64)) {
            if (tu_next_eflag_use == 1) {
                LATXS_FLAG_REDUCTION_CHECKIF(pir1)
                latxs_append_ir2_opnd2(LISA_X86ADD_D, &src0, &src0);
            }
            latxs_append_ir2_opnd3(LISA_ADD_D, &src0, &src0, &src0);
        } else {
            if (tu_next_eflag_use == 1) {
                LATXS_FLAG_REDUCTION_CHECKIF(pir1)
                latxs_append_ir2_opnd2(LISA_X86ADD_B + (os >> 4), &src0, &src0);
            }
            latxs_append_ir2_opnd3(LISA_ADD_W, &src0, &src0, &src0);
            latxs_store_ir2_to_ir1_gpr(&src0, opnd0);
        }
    } else {
        if (ir1_opnd_is_gpr(opnd1) && (ir1_opnd_size(opnd1) == 64)) {
            src1 = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd1));
        } else {
            src1 = latxs_ra_alloc_itemp();
            latxs_load_ir1_to_ir2(&src1, opnd1, EXMode_S);
        }
        if (tu_next_eflag_use == 1) {
            LATXS_FLAG_REDUCTION_CHECKIF(pir1)
            latxs_append_ir2_opnd2(LISA_X86ADD_B + (os >> 4) - (os >> 6), &src0, &src1);
        }
        temp = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd3(LISA_ADD_D, &temp, &src0, &src1);
        latxs_store_ir2_to_ir1(&temp, opnd0);
    }

    IR2_OPND target_label = latxs_ir2_opnd_new_label();

    latxs_tr_gen_eob();
    LOAD_TB_PTR_TO_DBT_ARG1();

    /* prepare the condition regsiter */
    IR2_OPND cond;
    switch (os) {
    case 64:
        cond = is_same_reg ? src0 : temp;
        break;
    case 32:
    case 16:
    case 8:
        cond = latxs_ra_alloc_itemp();
        la_out2_(ir1_opcode(nir1) == WRAP(JNS) ?
                 (os == 32 ? lisa_mov32s :
                  os == 16 ? lisa_mov16s : lisa_mov8s) :
                 (os == 32 ? lisa_mov32z :
                  os == 16 ? lisa_mov16z : lisa_mov8z),
                 &cond, is_same_reg ? &src0 : &temp);
        break;
    default:
        lsassert(0);
        break;
    }

    switch (ir1_opcode(nir1)) {
    case WRAP(JE):
        /*ZF=1 allow diff*/
#ifdef CONFIG_LATX_TU
        GEN_TU_FUN_AND_RETURN(LISA_BEQZ, cond, target_label, nir1);
#endif
        latxs_append_ir2_opnd2(LISA_BEQZ, &cond, &target_label);
        break;
    case WRAP(JNE):
        /*ZF=0 allow diff*/
#ifdef CONFIG_LATX_TU
        GEN_TU_FUN_AND_RETURN(LISA_BNEZ, cond, target_label, nir1);
#endif
        latxs_append_ir2_opnd2(LISA_BNEZ, &cond, &target_label);
        break;
    case WRAP(JNS):
        /*SF=0*/
#ifdef CONFIG_LATX_TU
        GEN_TU_FUN_AND_RETURN2(LISA_BGE, cond, latxs_zero_ir2_opnd,
                               target_label, nir1);
#endif
        latxs_append_ir2_opnd3(LISA_BGE, &cond, &latxs_zero_ir2_opnd,
                               &target_label);
        break;
    default:
        lsassert(0);
        break;
    }

    /* not taken */
    latxs_tr_generate_exit_tb(nir1, 0);

    latxs_append_ir2_opnd1(LISA_LABEL, &target_label);

    /* taken */
    latxs_tr_generate_exit_tb(nir1, 1);

    return true;
}

static
bool translate_sub_jcc(IR1_INST *pir1)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    IR1_INST *list = td->ir1_inst_array;
    IR1_INST *nir1 = NULL;

#ifdef CONFIG_LATX_TU
    TranslationBlock *tb = td->curr_tb;
    TranslationBlock *tu_next_tb = tb->s_data->next_tb[TU_TB_INDEX_NEXT];
    TranslationBlock *tu_target_tb = tb->s_data->next_tb[TU_TB_INDEX_TARGET];
#endif

    lsassert(pir1->instptn.next >= 0);

    nir1 = ir1_inst_in_list(list, pir1->instptn.next);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    int tu_next_eflag_use = 1;
#ifdef CONFIG_LATX_TU
    if (td->is_tu_mode && flag_reduction_tu_sub_jcc() &&
        tu_next_tb    && tu_next_tb->s_data->eflag_use == 0 &&
        tu_target_tb  && tu_target_tb->s_data->eflag_use == 0)
    {
        tu_next_eflag_use = 0;
    }
#endif

    IR2_OPND src0;
    IR2_OPND src1;
    IR2_OPND temp;
    if (ir1_opnd_is_gpr(opnd0) && (ir1_opnd_size(opnd0) == 64)) {
        src0 = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd0));
    } else {
        src0 = latxs_ra_alloc_itemp();
        latxs_load_ir1_to_ir2(&src0, opnd0, EXMode_S);
    }
    int is_same_reg = ir1_opnd_is_same_reg(opnd0, opnd1);
    int os = ir1_opnd_size(opnd0); /* 8, 16, 32, 64 */
    if (is_same_reg) {
        if (ir1_opnd_is_gpr(opnd0) && (ir1_opnd_size(opnd0) == 64)) {
            if (tu_next_eflag_use == 1) {
                LATXS_FLAG_REDUCTION_CHECKIF(pir1)
                latxs_append_ir2_opnd2(LISA_X86SUB_D, &src0, &src0);
            }
            latxs_append_ir2_opnd3(LISA_SUB_D, &src0, &src0, &src0);
        } else {
            if (tu_next_eflag_use == 1) {
                LATXS_FLAG_REDUCTION_CHECKIF(pir1)
                latxs_append_ir2_opnd2(LISA_X86SUB_B + (os >> 4), &src0, &src0);
            }
            latxs_append_ir2_opnd3(LISA_SUB_W, &src0, &src0, &src0);
            latxs_store_ir2_to_ir1_gpr(&src0, opnd0);
        }
    } else {
        if (ir1_opnd_is_gpr(opnd1) && (ir1_opnd_size(opnd1) == 64)) {
            src1 = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd1));
        } else {
            src1 = latxs_ra_alloc_itemp();
            latxs_load_ir1_to_ir2(&src1, opnd1, EXMode_S);
        }
        if (tu_next_eflag_use == 1) {
            LATXS_FLAG_REDUCTION_CHECKIF(pir1)
            latxs_append_ir2_opnd2(LISA_X86SUB_B + (os >> 4) - (os >> 6), &src0, &src1);
        }
        temp = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd3(LISA_SUB_D, &temp, &src0, &src1);
        latxs_store_ir2_to_ir1(&temp, opnd0);
    }

    IR2_OPND target_label = latxs_ir2_opnd_new_label();

    latxs_tr_gen_eob();
    LOAD_TB_PTR_TO_DBT_ARG1();

    /* prepare the condition register */
    IR2_OPND cond = is_same_reg ? src0 : temp;
    if ((ir1_opcode(nir1) == WRAP(JS) ||
         ir1_opcode(nir1) == WRAP(JNS)) && os != 64) {
        cond = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd2_(os == 32 ? lisa_mov32s :
                                os == 16 ? lisa_mov16s : lisa_mov8s,
                                &cond, is_same_reg ? &src0 : &temp);
    }

    switch (ir1_opcode(nir1)) {
    case WRAP(JE):
        /*ZF=0 allow diff*/
#ifdef CONFIG_LATX_TU
        GEN_TU_FUN_AND_RETURN(LISA_BEQZ, cond, target_label, nir1);
#endif
        latxs_append_ir2_opnd2(LISA_BEQZ, &cond, &target_label);
        break;
    case WRAP(JNE):
        /*ZF=0 allow diff*/
#ifdef CONFIG_LATX_TU
        GEN_TU_FUN_AND_RETURN(LISA_BNEZ, cond, target_label, nir1);
#endif
        latxs_append_ir2_opnd2(LISA_BNEZ, &cond, &target_label);
        break;
    case WRAP(JS):
        /* SF = 1 */
#ifdef CONFIG_LATX_TU
        GEN_TU_FUN_AND_RETURN2(LISA_BLT, cond, latxs_zero_ir2_opnd,
                               target_label, nir1);
#endif
        latxs_append_ir2_opnd3(LISA_BLT, &cond, &latxs_zero_ir2_opnd,
                               &target_label);
        break;
    case WRAP(JNS):
        /* SF = 0 */
#ifdef CONFIG_LATX_TU
        GEN_TU_FUN_AND_RETURN2(LISA_BGE, cond, latxs_zero_ir2_opnd,
                               target_label, nir1);
#endif
        latxs_append_ir2_opnd3(LISA_BGE, &cond, &latxs_zero_ir2_opnd,
                               &target_label);
        break;
    default:
        lsassert(0);
        break;
    }

    /* not taken */
    latxs_tr_generate_exit_tb(nir1, 0);

    latxs_append_ir2_opnd1(LISA_LABEL, &target_label);

    /* taken */
    latxs_tr_generate_exit_tb(nir1, 1);

    return true;
}

static
bool translate_sub_xx_jcc(IR1_INST *pir1)
{
    TRANSLATION_DATA *td = lsenv->tr_data;

    lsassert(pir1->instptn.next >= 0);

    int tu_next_eflag_use = 1;
#ifdef CONFIG_LATX_TU
    TranslationBlock *tb = td->curr_tb;
    TranslationBlock *tu_next_tb = tb->s_data->next_tb[TU_TB_INDEX_NEXT];
    TranslationBlock *tu_target_tb = tb->s_data->next_tb[TU_TB_INDEX_TARGET];
    if (ir1_opcode(pir1) == WRAP(SUB) && td->is_tu_mode &&
        flag_reduction_tu_sub_xx_jcc() &&
#ifdef CONFIG_SOFTMMU
        (tb->flags & HF_HAMT_EN_MASK) &&
#endif
        tu_next_tb && tu_next_tb->s_data->eflag_use == 0 &&
        tu_target_tb && tu_target_tb->s_data->eflag_use == 0) {
        tu_next_eflag_use = 0;
    }
#endif

    int tu_eflag_opt = 0;
#ifdef CONFIG_LATX_TU
    if (ir1_opcode(pir1) == WRAP(SUB)) {
        TU_EFLAG_OPT_RESET(tu_data);
    }
    if (td->is_tu_mode && tu_next_eflag_use == 1 &&
        flag_reduction_tu_sub_xx_jcc() && flag_reduction_tu_next() &&
#ifdef CONFIG_SOFTMMU
        (tb->flags & HF_HAMT_EN_MASK) &&
#endif
        tu_next_tb && tu_next_tb->s_data->eflag_use != 0 &&
        tb_is_adjacent(tb, tu_next_tb) &&
        tu_target_tb && tu_target_tb->s_data->eflag_use == 0) {
        tu_eflag_opt = 1;
    }

    if (td->avoid_tu_eflag_opt) {
        tu_next_eflag_use = 1;
        tu_eflag_opt = 0;
    }
#endif

    if (ir1_opcode(pir1) == WRAP(SUB)) {
        IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
        IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);
        int os = ir1_opnd_size(opnd0); /* 8, 16, 32, 64 */

        lsassert(ir1_opnd_is_gpr(opnd0));

        td->ptn_itemp0 = latxs_ra_alloc_ptn_itemp();
        latxs_load_ir1_to_ir2(&td->ptn_itemp0, opnd0, EXMode_S);
        td->ptn_itemp1 = latxs_ra_alloc_ptn_itemp();
        latxs_load_ir1_to_ir2(&td->ptn_itemp1, opnd1, EXMode_S);

        if (tu_next_eflag_use == 1 && !tu_eflag_opt) {
            LATXS_FLAG_REDUCTION_CHECKIF(pir1) {
                latxs_append_ir2_opnd2(LISA_X86SUB_B + (os >> 4) -
                                       (os >> 6), &td->ptn_itemp0,
                                       &td->ptn_itemp1);
            }
        }

        IR2_OPND dest = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd3(LISA_SUB_D, &dest, &td->ptn_itemp0,
                               &td->ptn_itemp1);
        latxs_store_ir2_to_ir1(&dest, opnd0);

#ifdef CONFIG_LATX_TU
        if (tu_eflag_opt) {
            TU_EFLAG_OPT_SET(tu_data);
            tu_data->eflag_opt_data.ef_opcode =
                LISA_X86SUB_B + (os >> 4) - (os >> 6);
            tu_data->eflag_opt_data.ef_opnd0 = td->ptn_itemp0;
            tu_data->eflag_opt_data.ef_opnd1 = td->ptn_itemp1;
        }
#endif
        if (!tu_next_eflag_use || tu_eflag_opt) {
            tb->ptn.eflag_type = PTN_EFLAG_TYPE_X86SUB;
            tb->ptn.os = os;
            tb->ptn.opnd0 = td->ptn_itemp0.val;
            tb->ptn.opnd1 = td->ptn_itemp1.val;
            tb_set_ptn_pcoffset(tb,
                    ir1_addr(pir1) - (tb->pc - tb->cs_base));
        }
    } else {
        if (!td->ptn_itemp_mask) {
            if (tb->ptn.eflag_type != PTN_EFLAG_TYPE_INVALID) {
                td->avoid_tu_eflag_opt = AVOID_TUEF_START;
            }
            switch (ir1_opcode(pir1)) {
            case WRAP(JS):
                latxs_translate_js(pir1);
                break;
            case WRAP(JNS):
                latxs_translate_jns(pir1);
                break;
            default:
                lsassert(0);
                break;
            }
            return true;
        }

        IR1_INST *list = td->ir1_inst_array;
        IR1_INST *sub = ir1_inst_in_list(list, pir1->instptn.next);
        int os = ir1_opnd_size(ir1_get_opnd(sub, 0));
        IR2_OPND result = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd3(LISA_SUB_D, &result, &td->ptn_itemp0,
                               &td->ptn_itemp1);

        IR2_OPND cond = result;
        if (os != 64) {
            cond = latxs_ra_alloc_itemp();
            latxs_append_ir2_opnd2_(os == 32 ? lisa_mov32s :
                                    os == 16 ? lisa_mov16s : lisa_mov8s,
                                    &cond, &result);
        }

        IR2_OPND target_label = latxs_ir2_opnd_new_label();

        latxs_tr_gen_eob();
        LOAD_TB_PTR_TO_DBT_ARG1();

        switch (ir1_opcode(pir1)) {
        case WRAP(JS):
#ifdef CONFIG_LATX_TU
            GEN_TU_FUN_AND_RETURN2(LISA_BLT, cond, latxs_zero_ir2_opnd,
                                   target_label, pir1);
#endif
            latxs_append_ir2_opnd3(LISA_BLT, &cond, &latxs_zero_ir2_opnd,
                                   &target_label);
            break;
        case WRAP(JNS):
#ifdef CONFIG_LATX_TU
            GEN_TU_FUN_AND_RETURN2(LISA_BGE, cond, latxs_zero_ir2_opnd,
                                   target_label, pir1);
#endif
            latxs_append_ir2_opnd3(LISA_BGE, &cond, &latxs_zero_ir2_opnd,
                                   &target_label);
            break;
        default:
            lsassert(0);
            break;
        }

        td->ptn_itemp_mask = 0;

        /* not taken */
        latxs_tr_generate_exit_tb(pir1, 0);

        latxs_append_ir2_opnd1(LISA_LABEL, &target_label);

        /* taken */
        latxs_tr_generate_exit_tb(pir1, 1);
    }

    return true;
}

static
bool translate_sar_jcc(IR1_INST *pir1)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    IR1_INST *list = td->ir1_inst_array;
    IR1_INST *nir1 = NULL;

#ifdef CONFIG_LATX_TU
    TranslationBlock *tb = td->curr_tb;
    TranslationBlock *tu_next_tb = tb->s_data->next_tb[TU_TB_INDEX_NEXT];
    TranslationBlock *tu_target_tb = tb->s_data->next_tb[TU_TB_INDEX_TARGET];
#endif

    lsassert(pir1->instptn.next >= 0);

    nir1 = ir1_inst_in_list(list, pir1->instptn.next);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);
    int os = ir1_opnd_size(opnd0); /* 8, 16, 32, 64 */
    int shift_mask = (lsenv->tr_data->sys.code64 && os == 64) ? 0x3f : 0x1f;
    int shift_imm = ir1_opnd_uimm(opnd1) & shift_mask;

    lsassert(ir1_opnd_is_imm(opnd1));
    lsassert(shift_imm != 0);

    int tu_next_eflag_use = 1;
#ifdef CONFIG_LATX_TU
    if (td->is_tu_mode && flag_reduction_tu_sar_jcc() &&
        tu_next_tb    && tu_next_tb->s_data->eflag_use == 0 &&
        tu_target_tb  && tu_target_tb->s_data->eflag_use == 0)
    {
        tu_next_eflag_use = 0;
    }
#endif

    int tu_eflag_opt = 0;
#ifdef CONFIG_LATX_TU
    TU_EFLAG_OPT_RESET(tu_data);
    if (td->is_tu_mode && tu_next_eflag_use == 1 &&
        flag_reduction_tu_sar_jcc() && flag_reduction_tu_next() &&
        tu_next_tb && tu_next_tb->s_data->eflag_use != 0 &&
        tb_is_adjacent(tb, tu_next_tb) &&
        tu_target_tb && tu_target_tb->s_data->eflag_use == 0)
    {
        tu_eflag_opt = 1;
    }

    if (tb->cflags & CF_USE_ICOUNT) {
        tu_next_eflag_use = 1;
        tu_eflag_opt = 0;
    }
#endif

    IR2_OPND src = latxs_ra_alloc_itemp();
    IR2_OPND shift = latxs_ra_alloc_itemp();
    IR2_OPND dest = latxs_ra_alloc_itemp();

    latxs_load_ir1_to_ir2(&src, opnd0, EXMode_S);
    latxs_append_ir2_opnd2i(LISA_ORI, &shift, &latxs_zero_ir2_opnd,
                            shift_imm);

    if (lsenv->tr_data->sys.code64 && os == 64) {
        latxs_append_ir2_opnd2i(LISA_SRAI_D, &dest, &src, shift_imm);
    } else {
        latxs_append_ir2_opnd2i(LISA_SRAI_W, &dest, &src, shift_imm);
    }

    latxs_store_ir2_to_ir1(&dest, opnd0);

    IR2_OPND cond_z;
    IR2_OPND cond_s;
    switch (os) {
    case 64:
        cond_z = dest;
        cond_s = dest;
        break;
    case 32:
        cond_z = latxs_ra_alloc_itemp();
        cond_s = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd2_(lisa_mov32z, &cond_z, &dest);
        latxs_append_ir2_opnd2_(lisa_mov32s, &cond_s, &dest);
        break;
    case 16:
        cond_z = latxs_ra_alloc_itemp();
        cond_s = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd2_(lisa_mov16z, &cond_z, &dest);
        latxs_append_ir2_opnd2_(lisa_mov16s, &cond_s, &dest);
        break;
    case 8:
        cond_z = latxs_ra_alloc_itemp();
        cond_s = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd2_(lisa_mov8z, &cond_z, &dest);
        latxs_append_ir2_opnd2_(lisa_mov8s, &cond_s, &dest);
        break;
    default:
        lsassert(0);
        break;
    }

    if (ir1_opnd_is_gpr(opnd0) && os == 32) {
        IR2_OPND dest_gpr = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd0));
        latxs_append_ir2_opnd2_(lisa_mov32z, &dest_gpr, &dest_gpr);
    }

    if (tu_next_eflag_use == 1) {
        if (tu_eflag_opt) {
#ifdef CONFIG_LATX_TU
            TU_EFLAG_OPT_SET(tu_data);
            tu_data->eflag_opt_data.ef_opcode =
                LISA_X86SRA_B + (os >> 4) - (os >> 6);
            tu_data->eflag_opt_data.ef_opnd0 = src;
            tu_data->eflag_opt_data.ef_opnd1 = shift;
#endif
        } else {
            LATXS_FLAG_REDUCTION_CHECKIF(pir1) {
                latxs_append_ir2_opnd2(
                    LISA_X86SRA_B + (os >> 4) - (os >> 6), &src, &shift);
                if (tb->cflags & CF_USE_ICOUNT) {
                    latxs_append_ir2_opnd1i(LISA_X86MTFLAG,
                            &latxs_zero_ir2_opnd, 0x1 << 2);
                }
            }
        }
    }

    IR2_OPND *zero = &latxs_zero_ir2_opnd;
    IR2_OPND target_label = latxs_ir2_opnd_new_label();

    latxs_tr_gen_eob();
    LOAD_TB_PTR_TO_DBT_ARG1();

    switch (ir1_opcode(nir1)) {
    case WRAP(JE):
#ifdef CONFIG_LATX_TU
        GEN_TU_FUN_AND_RETURN(LISA_BEQZ, cond_z, target_label, nir1);
#endif
        latxs_append_ir2_opnd2(LISA_BEQZ, &cond_z, &target_label);
        break;
    case WRAP(JNE):
#ifdef CONFIG_LATX_TU
        GEN_TU_FUN_AND_RETURN(LISA_BNEZ, cond_z, target_label, nir1);
#endif
        latxs_append_ir2_opnd2(LISA_BNEZ, &cond_z, &target_label);
        break;
    case WRAP(JS):
#ifdef CONFIG_LATX_TU
        GEN_TU_FUN_AND_RETURN2(LISA_BLT, cond_s, *zero, target_label, nir1);
#endif
        latxs_append_ir2_opnd3(LISA_BLT, &cond_s, zero, &target_label);
        break;
    case WRAP(JNS):
#ifdef CONFIG_LATX_TU
        GEN_TU_FUN_AND_RETURN2(LISA_BGE, cond_s, *zero, target_label, nir1);
#endif
        latxs_append_ir2_opnd3(LISA_BGE, &cond_s, zero, &target_label);
        break;
    default:
        lsassert(0);
        break;
    }

    /* not taken */
    latxs_tr_generate_exit_tb(nir1, 0);

    latxs_append_ir2_opnd1(LISA_LABEL, &target_label);

    /* taken */
    latxs_tr_generate_exit_tb(nir1, 1);

    return true;
}

static
bool translate_shr_jcc(IR1_INST *pir1)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    IR1_INST *list = td->ir1_inst_array;
    IR1_INST *nir1 = NULL;

#ifdef CONFIG_LATX_TU
    TranslationBlock *tb = td->curr_tb;
    TranslationBlock *tu_next_tb = tb->s_data->next_tb[TU_TB_INDEX_NEXT];
    TranslationBlock *tu_target_tb = tb->s_data->next_tb[TU_TB_INDEX_TARGET];
#endif

    lsassert(pir1->instptn.next >= 0);

    nir1 = ir1_inst_in_list(list, pir1->instptn.next);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);
    int os = ir1_opnd_size(opnd0); /* 8, 16, 32, 64 */
    int shift_mask = (lsenv->tr_data->sys.code64 && os == 64) ? 0x3f : 0x1f;
    int shift_imm = ir1_opnd_uimm(opnd1) & shift_mask;

    lsassert(ir1_opnd_is_imm(opnd1));
    lsassert(shift_imm != 0);

    int tu_next_eflag_use = 1;
#ifdef CONFIG_LATX_TU
    if (td->is_tu_mode && flag_reduction_tu_shr_jcc() &&
        tu_next_tb    && tu_next_tb->s_data->eflag_use == 0 &&
        tu_target_tb  && tu_target_tb->s_data->eflag_use == 0)
    {
        tu_next_eflag_use = 0;
    }
#endif

    int tu_eflag_opt = 0;
#ifdef CONFIG_LATX_TU
    TU_EFLAG_OPT_RESET(tu_data);
    if (td->is_tu_mode && tu_next_eflag_use == 1 &&
        flag_reduction_tu_shr_jcc() && flag_reduction_tu_next() &&
        tu_next_tb && tu_next_tb->s_data->eflag_use != 0 &&
        tb_is_adjacent(tb, tu_next_tb) &&
        tu_target_tb && tu_target_tb->s_data->eflag_use == 0)
    {
        tu_eflag_opt = 1;
    }

    if (tb->cflags & CF_USE_ICOUNT) {
        tu_next_eflag_use = 1;
        tu_eflag_opt = 0;
    }
#endif

    IR2_OPND src = latxs_ra_alloc_itemp();
    IR2_OPND shift = latxs_ra_alloc_itemp();
    IR2_OPND dest = latxs_ra_alloc_itemp();

    latxs_load_ir1_to_ir2(&src, opnd0, EXMode_Z);
    latxs_append_ir2_opnd2i(LISA_ORI, &shift, &latxs_zero_ir2_opnd,
                            shift_imm);

    if (lsenv->tr_data->sys.code64 && os == 64) {
        latxs_append_ir2_opnd2i(LISA_SRLI_D, &dest, &src, shift_imm);
    } else {
        latxs_append_ir2_opnd2i(LISA_SRLI_W, &dest, &src, shift_imm);
    }

    latxs_store_ir2_to_ir1(&dest, opnd0);

    IR2_OPND cond_z;
    switch (os) {
    case 64:
        cond_z = dest;
        break;
    case 32:
        cond_z = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd2_(lisa_mov32z, &cond_z, &dest);
        break;
    case 16:
        cond_z = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd2_(lisa_mov16z, &cond_z, &dest);
        break;
    case 8:
        cond_z = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd2_(lisa_mov8z, &cond_z, &dest);
        break;
    default:
        lsassert(0);
        break;
    }

    if (ir1_opnd_is_gpr(opnd0) && os == 32) {
        IR2_OPND dest_gpr = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd0));
        latxs_append_ir2_opnd2_(lisa_mov32z, &dest_gpr, &dest_gpr);
    }

    if (tu_next_eflag_use == 1) {
        if (tu_eflag_opt) {
#ifdef CONFIG_LATX_TU
            TU_EFLAG_OPT_SET(tu_data);
            tu_data->eflag_opt_data.ef_opcode =
                LISA_X86SRL_B + (os >> 4) - (os >> 6);
            tu_data->eflag_opt_data.ef_opnd0 = src;
            tu_data->eflag_opt_data.ef_opnd1 = shift;
#endif
        } else {
            LATXS_FLAG_REDUCTION_CHECKIF(pir1) {
                latxs_append_ir2_opnd2(
                    LISA_X86SRL_B + (os >> 4) - (os >> 6), &src, &shift);
                if (tb->cflags & CF_USE_ICOUNT) {
                    latxs_append_ir2_opnd1i(LISA_X86MTFLAG,
                            &latxs_zero_ir2_opnd, 0x1 << 2);
                }
            }
        }
    }

    IR2_OPND target_label = latxs_ir2_opnd_new_label();

    latxs_tr_gen_eob();
    LOAD_TB_PTR_TO_DBT_ARG1();

    switch (ir1_opcode(nir1)) {
    case WRAP(JE):
#ifdef CONFIG_LATX_TU
        GEN_TU_FUN_AND_RETURN(LISA_BEQZ, cond_z, target_label, nir1);
#endif
        latxs_append_ir2_opnd2(LISA_BEQZ, &cond_z, &target_label);
        break;
    case WRAP(JNE):
#ifdef CONFIG_LATX_TU
        GEN_TU_FUN_AND_RETURN(LISA_BNEZ, cond_z, target_label, nir1);
#endif
        latxs_append_ir2_opnd2(LISA_BNEZ, &cond_z, &target_label);
        break;
    default:
        lsassert(0);
        break;
    }

    /* not taken */
    latxs_tr_generate_exit_tb(nir1, 0);

    latxs_append_ir2_opnd1(LISA_LABEL, &target_label);

    /* taken */
    latxs_tr_generate_exit_tb(nir1, 1);

    return true;
}

static
bool translate_shr_xx_jcc(IR1_INST *pir1)
{
    TRANSLATION_DATA *td = lsenv->tr_data;

    lsassert(pir1->instptn.next >= 0);

    int tu_next_eflag_use = 1;
#ifdef CONFIG_LATX_TU
    TranslationBlock *tb = td->curr_tb;
    TranslationBlock *tu_next_tb = tb->s_data->next_tb[TU_TB_INDEX_NEXT];
    TranslationBlock *tu_target_tb =
        tb->s_data->next_tb[TU_TB_INDEX_TARGET];
    if (ir1_opcode(pir1) == WRAP(SHR) && td->is_tu_mode &&
        flag_reduction_tu_shr_xx_jcc() &&
#ifdef CONFIG_SOFTMMU
        (tb->flags & HF_HAMT_EN_MASK) &&
#endif
        tu_next_tb && tu_next_tb->s_data->eflag_use == 0 &&
        tu_target_tb && tu_target_tb->s_data->eflag_use == 0) {
        tu_next_eflag_use = 0;
    }
#endif

    int tu_eflag_opt = 0;
#ifdef CONFIG_LATX_TU
    if (ir1_opcode(pir1) == WRAP(SHR)) {
        TU_EFLAG_OPT_RESET(tu_data);
    }
    if (td->is_tu_mode && tu_next_eflag_use == 1 &&
        flag_reduction_tu_shr_xx_jcc() && flag_reduction_tu_next() &&
#ifdef CONFIG_SOFTMMU
        (tb->flags & HF_HAMT_EN_MASK) &&
#endif
        tu_next_tb && tu_next_tb->s_data->eflag_use != 0 &&
        tb_is_adjacent(tb, tu_next_tb) &&
        tu_target_tb && tu_target_tb->s_data->eflag_use == 0) {
        tu_eflag_opt = 1;
    }

    if (td->avoid_tu_eflag_opt || (tb->cflags & CF_USE_ICOUNT)) {
        tu_next_eflag_use = 1;
        tu_eflag_opt = 0;
    }
#endif

    if (ir1_opcode(pir1) == WRAP(SHR)) {
        IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
        IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);
        int os = ir1_opnd_size(opnd0); /* 8, 16, 32, 64 */
        int shift_mask = (td->sys.code64 && os == 64) ? 0x3f : 0x1f;
        int shift_imm = ir1_opnd_uimm(opnd1) & shift_mask;

        lsassert(ir1_opnd_is_imm(opnd1));
        lsassert(shift_imm != 0);

        td->ptn_itemp0 = latxs_ra_alloc_ptn_itemp();
        latxs_load_ir1_to_ir2(&td->ptn_itemp0, opnd0, EXMode_Z);
        td->ptn_itemp1 = latxs_ra_alloc_ptn_itemp();
        latxs_append_ir2_opnd2i(LISA_ORI, &td->ptn_itemp1,
                                &latxs_zero_ir2_opnd, shift_imm);

        IR2_OPND dest = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd2i(td->sys.code64 && os == 64 ?
                                LISA_SRLI_D : LISA_SRLI_W,
                                &dest, &td->ptn_itemp0, shift_imm);
        latxs_store_ir2_to_ir1(&dest, opnd0);

        if (ir1_opnd_is_gpr(opnd0) && os == 32) {
            IR2_OPND dest_gpr =
                latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd0));
            latxs_append_ir2_opnd2_(lisa_mov32z, &dest_gpr, &dest_gpr);
        }

        if (tu_next_eflag_use == 1 && !tu_eflag_opt) {
            LATXS_FLAG_REDUCTION_CHECKIF(pir1) {
                latxs_append_ir2_opnd2(LISA_X86SRL_B + (os >> 4) -
                                       (os >> 6), &td->ptn_itemp0,
                                       &td->ptn_itemp1);
                if (tb->cflags & CF_USE_ICOUNT) {
                    latxs_append_ir2_opnd1i(LISA_X86MTFLAG,
                                            &latxs_zero_ir2_opnd,
                                            0x1 << 2);
                }
            }
        }

#ifdef CONFIG_LATX_TU
        if (tu_eflag_opt) {
            TU_EFLAG_OPT_SET(tu_data);
            tu_data->eflag_opt_data.ef_opcode =
                LISA_X86SRL_B + (os >> 4) - (os >> 6);
            tu_data->eflag_opt_data.ef_opnd0 = td->ptn_itemp0;
            tu_data->eflag_opt_data.ef_opnd1 = td->ptn_itemp1;
        }
#endif
        if (!tu_next_eflag_use || tu_eflag_opt) {
            tb->ptn.eflag_type = PTN_EFLAG_TYPE_X86SRL;
            tb->ptn.os = os;
            tb->ptn.opnd0 = td->ptn_itemp0.val;
            tb->ptn.opnd1 = td->ptn_itemp1.val;
            tb_set_ptn_pcoffset(tb,
                    ir1_addr(pir1) - (tb->pc - tb->cs_base));
        }
    } else {
        if (!td->ptn_itemp_mask) {
            if (tb->ptn.eflag_type != PTN_EFLAG_TYPE_INVALID) {
                td->avoid_tu_eflag_opt = AVOID_TUEF_START;
            }
            lsassert(ir1_opcode(pir1) == WRAP(JE));
            latxs_translate_jz(pir1);
            return true;
        }

        IR1_INST *list = td->ir1_inst_array;
        IR1_INST *shr = ir1_inst_in_list(list, pir1->instptn.next);
        IR1_OPND *opnd0 = ir1_get_opnd(shr, 0);
        int os = ir1_opnd_size(opnd0); /* 8, 16, 32, 64 */

        IR2_OPND result = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd3(td->sys.code64 && os == 64 ?
                               LISA_SRL_D : LISA_SRL_W,
                               &result, &td->ptn_itemp0,
                               &td->ptn_itemp1);

        IR2_OPND cond_z = result;
        if (os != 64) {
            cond_z = latxs_ra_alloc_itemp();
            latxs_append_ir2_opnd2_(os == 32 ? lisa_mov32z :
                                    os == 16 ? lisa_mov16z : lisa_mov8z,
                                    &cond_z, &result);
        }

        IR2_OPND target_label = latxs_ir2_opnd_new_label();

        latxs_tr_gen_eob();
        LOAD_TB_PTR_TO_DBT_ARG1();

        lsassert(ir1_opcode(pir1) == WRAP(JE));
#ifdef CONFIG_LATX_TU
        GEN_TU_FUN_AND_RETURN(LISA_BEQZ, cond_z, target_label, pir1);
#endif
        latxs_append_ir2_opnd2(LISA_BEQZ, &cond_z, &target_label);

        td->ptn_itemp_mask = 0;

        /* not taken */
        latxs_tr_generate_exit_tb(pir1, 0);

        latxs_append_ir2_opnd1(LISA_LABEL, &target_label);

        /* taken */
        latxs_tr_generate_exit_tb(pir1, 1);
    }

    return true;
}

bool try_translate_instptn(IR1_INST *pir1)
{
    if (ir1_opcode(pir1) == X86_INS_INVALID) {
        return latxs_translate_invalid(pir1);
    }
    latxs_instptn_check_false();

    switch (pir1->instptn.opc) {
    case INSTPTN_OPC_NONE:
        return false;
    case INSTPTN_OPC_NOP:
    case INSTPTN_OPC_NOP_FOR_EXP:
        return true;
    case INSTPTN_OPC_CMP_JCC:
        return translate_cmp_jcc(pir1);
    case INSTPTN_OPC_TEST_JCC:
        return translate_test_jcc(pir1);
    case INSTPTN_OPC_CQO_IDIV:
        return translate_cqo_idiv(pir1);
    case INSTPTN_OPC_BT_JCC:
        return translate_bt_jcc(pir1);
    case INSTPTN_OPC_COMISD_JCC:
        return translate_comisd_jcc(pir1);
    case INSTPTN_OPC_COMISS_JCC:
        return translate_comiss_jcc(pir1);
    case INSTPTN_OPC_UCOMISD_JCC:
        return translate_ucomisd_jcc(pir1);
    case INSTPTN_OPC_UCOMISS_JCC:
        return translate_ucomiss_jcc(pir1);
    case INSTPTN_OPC_XOR_DIV:
        return translate_xor_div(pir1);
    case INSTPTN_OPC_CDQ_IDIV:
        return translate_cdq_idiv(pir1);
    case INSTPTN_OPC_CMP_SBB:
        return translate_cmp_sbb(pir1);
    case INSTPTN_OPC_CMP_XXCC_CON:
        return translate_cmp_xxcc_con(pir1);
    case INSTPTN_OPC_TEST_XXCC_CON:
        return translate_test_xxcc_con(pir1);

    case INSTPTN_OPC_UCOMISD_SETA:
        return translate_ucomisd_seta(pir1);

    case INSTPTN_OPC_CMP_XX_JCC:
        return translate_cmp_xx_jcc(pir1);
    case INSTPTN_OPC_TEST_XX_JCC:
        return translate_test_xx_jcc(pir1);
    case INSTPTN_OPC_BT_XX_JCC:
        return translate_bt_xx_jcc(pir1);
    case INSTPTN_OPC_COMISD_XX_JCC:
        return translate_comisd_xx_jcc(pir1);
    case INSTPTN_OPC_COMISS_XX_JCC:
        return translate_comiss_xx_jcc(pir1);
    case INSTPTN_OPC_UCOMISD_XX_JCC:
        return translate_ucomisd_xx_jcc(pir1);
    case INSTPTN_OPC_UCOMISS_XX_JCC:
        return translate_ucomiss_xx_jcc(pir1);

    case INSTPTN_OPC_ADD_JCC:
        return translate_add_jcc(pir1);
    case INSTPTN_OPC_SUB_JCC:
        return translate_sub_jcc(pir1);
    case INSTPTN_OPC_SUB_XX_JCC:
        return translate_sub_xx_jcc(pir1);
    case INSTPTN_OPC_AND_JCC:
        return translate_and_jcc(pir1);
    case INSTPTN_OPC_AND_XX_JCC:
        return translate_and_xx_jcc(pir1);
    case INSTPTN_OPC_OR_JCC:
        return translate_or_jcc(pir1);
    case INSTPTN_OPC_OR_XX_JCC:
        return translate_or_xx_jcc(pir1);
    case INSTPTN_OPC_XOR_JCC:
        return translate_xor_jcc(pir1);
    case INSTPTN_OPC_XOR_XX_JCC:
        return translate_xor_xx_jcc(pir1);
    case INSTPTN_OPC_SAR_JCC:
        return translate_sar_jcc(pir1);
    case INSTPTN_OPC_SHR_JCC:
        return translate_shr_jcc(pir1);
    case INSTPTN_OPC_SHR_XX_JCC:
        return translate_shr_xx_jcc(pir1);
    case INSTPTN_OPC_DEC_JCC:
        return translate_dec_jcc(pir1);
    default:
        lsassert(0);
        break;
    }

    return false;
}

#endif
