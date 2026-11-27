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
#include "sys-aot.h"
#include "sys-aot-offline.h"

void latxs_sys_arith_register_ir1(void)
{
    latxs_register_ir1(X86_INS_ADD);
    latxs_register_ir1(X86_INS_ADC);
    latxs_register_ir1(X86_INS_SUB);
    latxs_register_ir1(X86_INS_SBB);
    latxs_register_ir1(X86_INS_INC);
    latxs_register_ir1(X86_INS_DEC);
    latxs_register_ir1(X86_INS_NEG);
    latxs_register_ir1(X86_INS_CMP);
    latxs_register_ir1(X86_INS_MUL);
    latxs_register_ir1(X86_INS_IMUL);
    latxs_register_ir1(X86_INS_DIV);
    latxs_register_ir1(X86_INS_IDIV);

    latxs_register_ir1(X86_INS_AAA);
    latxs_register_ir1(X86_INS_AAD);
    latxs_register_ir1(X86_INS_AAM);
    latxs_register_ir1(X86_INS_AAS);
    latxs_register_ir1(X86_INS_DAA);
    latxs_register_ir1(X86_INS_DAS);

    latxs_register_ir1(X86_INS_XADD);
    latxs_register_ir1(X86_INS_MULX);
}

bool latxs_translate_add(IR1_INST *pir1)
{
    if (latxs_ir1_has_prefix_lock(pir1) &&
        (lsenv->tr_data->sys.cflags & CF_PARALLEL)) {
        return latxs_translate_lock_add(pir1);
    }
    if (option_by_hand_64 && latxs_translate_add_byhand64(pir1)) {
        return true;
    }

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    IR2_OPND src0 = latxs_ra_alloc_itemp();
    IR2_OPND src1 = latxs_ra_alloc_itemp();

    latxs_load_ir1_to_ir2(&src0, opnd0, EXMode_S);
    latxs_load_ir1_to_ir2(&src1, opnd1, EXMode_S);

    IR2_OPND dest = latxs_ra_alloc_itemp();

    latxs_append_ir2_opnd3(LISA_ADD_D, &dest, &src0, &src1);

    if (ir1_opnd_is_mem(opnd0)) {
        latxs_store_ir2_to_ir1(&dest, opnd0);
        /* calculate elfags after store */
        latxs_generate_eflag_calculation(&dest, &src0, &src1, pir1, true);
    } else {
        /* Destination is GPR, no exception will generate */
        latxs_generate_eflag_calculation(&dest, &src0, &src1, pir1, true);
        latxs_store_ir2_to_ir1(&dest, opnd0);
    }

    latxs_ra_free_temp(&dest);
    latxs_ra_free_temp(&src0);
    latxs_ra_free_temp(&src1);
    return true;
}

bool latxs_translate_adc(IR1_INST *pir1)
{
    if (latxs_ir1_has_prefix_lock(pir1) &&
        (lsenv->tr_data->sys.cflags & CF_PARALLEL)) {
        return latxs_translate_lock_adc(pir1);
    }
    if (option_by_hand_64 && latxs_translate_adc_byhand64(pir1)) {
        return true;
    }

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    IR2_OPND src0 = latxs_ra_alloc_itemp();
    IR2_OPND src1 = latxs_ra_alloc_itemp();

    latxs_load_ir1_to_ir2(&src0, opnd0, EXMode_S);
    latxs_load_ir1_to_ir2(&src1, opnd1, EXMode_S);

    IR2_OPND dest = latxs_ra_alloc_itemp();
    latxs_load_eflags_cf_to_ir2(&dest);

    latxs_append_ir2_opnd3(LISA_ADD_D, &dest, &dest, &src0);
    latxs_append_ir2_opnd3(LISA_ADD_D, &dest, &dest, &src1);

    if (ir1_opnd_is_mem(opnd0)) {
        latxs_store_ir2_to_ir1(&dest, opnd0);
        /* calculate elfags after store */
        latxs_generate_eflag_calculation(&dest, &src0, &src1, pir1, true);
    } else {
        /* Destination is GPR, no exception will generate */
        latxs_generate_eflag_calculation(&dest, &src0, &src1, pir1, true);
        latxs_store_ir2_to_ir1(&dest, opnd0);
    }

    latxs_ra_free_temp(&dest);
    latxs_ra_free_temp(&src0);
    latxs_ra_free_temp(&src1);
    return true;
}

bool latxs_translate_sub(IR1_INST *pir1)
{
    if (latxs_ir1_has_prefix_lock(pir1) &&
        (lsenv->tr_data->sys.cflags & CF_PARALLEL)) {
        return latxs_translate_lock_sub(pir1);
    }
    if (option_by_hand_64 && latxs_translate_sub_byhand64(pir1)) {
        return true;
    }

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    IR2_OPND src0 = latxs_ra_alloc_itemp();
    IR2_OPND src1 = latxs_ra_alloc_itemp();

    latxs_load_ir1_to_ir2(&src0, opnd0, EXMode_S);
    latxs_load_ir1_to_ir2(&src1, opnd1, EXMode_S);

    IR2_OPND dest = latxs_ra_alloc_itemp();

    latxs_append_ir2_opnd3(LISA_SUB_D, &dest, &src0, &src1);

    if (ir1_opnd_is_mem(opnd0)) {
        latxs_store_ir2_to_ir1(&dest, opnd0);
        /* calculate elfags after store */
        latxs_generate_eflag_calculation(&dest, &src0, &src1, pir1, true);
    } else {
        /* Destination is GPR, no exception will generate */
        latxs_generate_eflag_calculation(&dest, &src0, &src1, pir1, true);
        latxs_store_ir2_to_ir1(&dest, opnd0);
    }

    latxs_ra_free_temp(&dest);
    latxs_ra_free_temp(&src0);
    latxs_ra_free_temp(&src1);
    return true;
}

bool latxs_translate_sbb(IR1_INST *pir1)
{
    if (latxs_ir1_has_prefix_lock(pir1) &&
        (lsenv->tr_data->sys.cflags & CF_PARALLEL)) {
        return latxs_translate_lock_sbb(pir1);
    }
    if (option_by_hand_64 && latxs_translate_sbb_byhand64(pir1)) {
        return true;
    }

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    IR2_OPND src0 = latxs_ra_alloc_itemp();
    IR2_OPND src1 = latxs_ra_alloc_itemp();

    latxs_load_ir1_to_ir2(&src0, opnd0, EXMode_S);
    latxs_load_ir1_to_ir2(&src1, opnd1, EXMode_S);

    IR2_OPND dest = latxs_ra_alloc_itemp();
    latxs_load_eflags_cf_to_ir2(&dest);

    latxs_append_ir2_opnd3(LISA_SUB_D, &dest, &src0, &dest);
    latxs_append_ir2_opnd3(LISA_SUB_D, &dest, &dest, &src1);

    if (ir1_opnd_is_mem(opnd0)) {
        latxs_store_ir2_to_ir1(&dest, opnd0);
        /* calculate elfags after store */
        latxs_generate_eflag_calculation(&dest, &src0, &src1, pir1, true);
    } else {
        /* Destination is GPR, no exception will generate */
        latxs_generate_eflag_calculation(&dest, &src0, &src1, pir1, true);
        latxs_store_ir2_to_ir1(&dest, opnd0);
    }

    latxs_ra_free_temp(&dest);
    latxs_ra_free_temp(&src0);
    latxs_ra_free_temp(&src1);
    return true;
}

bool latxs_translate_inc(IR1_INST *pir1)
{
    if (latxs_ir1_has_prefix_lock(pir1) &&
        (lsenv->tr_data->sys.cflags & CF_PARALLEL)) {
        return latxs_translate_lock_inc(pir1);
    }

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR2_OPND src0, dest, mem_opnd;
    IR2_OPND imm1 = latxs_ir2_opnd_new(IR2_OPND_IMMH, 1);
    int opnd0_size = ir1_opnd_size(opnd0);
    int mem_offset;

    if (ir1_opnd_is_gpr(opnd0)) {
        src0 = latxs_load_ireg_from_ir1(opnd0, EXMode_N);
        if (opnd0_size >= 32) {
            dest = src0;
        } else {
            dest = latxs_ra_alloc_itemp();
        }
    } else {
        mem_opnd = latxs_ra_alloc_itemp();
        latxs_get_mem_opnd(&mem_opnd, &mem_offset, opnd0, -1);
        src0 = latxs_ra_alloc_itemp();
        la_ld_by_op_size(src0, mem_opnd, mem_offset, opnd0_size, EXMode_S);
        dest = src0;
    }

    latxs_generate_eflag_calculation(&dest, &src0, &imm1, pir1, true);

    latxs_append_ir2_opnd2i(LISA_ADDI_D, &dest, &src0, 1);

    if (ir1_opnd_is_gpr(opnd0)) {
        latxs_store_ir2_to_ir1(&dest, opnd0);
    } else {
        la_st_by_op_size(dest, mem_opnd, mem_offset, opnd0_size);
        latxs_ra_free_temp(&mem_opnd);
    }

    latxs_ra_free_temp(&src0);
    latxs_ra_free_temp(&dest);
    return true;
}

bool latxs_translate_dec(IR1_INST *pir1)
{
    if (latxs_ir1_has_prefix_lock(pir1) &&
        (lsenv->tr_data->sys.cflags & CF_PARALLEL)) {
        return latxs_translate_lock_dec(pir1);
    }

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR2_OPND src0, dest, mem_opnd;
    IR2_OPND imm1 = latxs_ir2_opnd_new(IR2_OPND_IMMH, 1);
    int opnd0_size = ir1_opnd_size(opnd0);
    int mem_offset;

    if (ir1_opnd_is_gpr(opnd0)) {
        src0 = latxs_load_ireg_from_ir1(opnd0, EXMode_N);
        if (opnd0_size >= 32) {
            dest = src0;
        } else {
            dest = latxs_ra_alloc_itemp();
        }
    } else {
        mem_opnd = latxs_ra_alloc_itemp();
        latxs_get_mem_opnd(&mem_opnd, &mem_offset, opnd0, -1);
        src0 = latxs_ra_alloc_itemp();
        la_ld_by_op_size(src0, mem_opnd, mem_offset, opnd0_size, EXMode_S);
        dest = src0;
    }

    latxs_generate_eflag_calculation(&dest, &src0, &imm1, pir1, true);

    latxs_append_ir2_opnd2i(LISA_ADDI_D, &dest, &src0, -1);

    if (ir1_opnd_is_gpr(opnd0)) {
        latxs_store_ir2_to_ir1(&dest, opnd0);
    } else {
        la_st_by_op_size(dest, mem_opnd, mem_offset, opnd0_size);
        latxs_ra_free_temp(&mem_opnd);
    }

    latxs_ra_free_temp(&src0);
    latxs_ra_free_temp(&dest);

    return true;
}

bool latxs_translate_neg(IR1_INST *pir1)
{
    if (latxs_ir1_has_prefix_lock(pir1) &&
        (lsenv->tr_data->sys.cflags & CF_PARALLEL)) {
        return latxs_translate_lock_neg(pir1);
    }
    if (option_by_hand_64 && latxs_translate_neg_byhand64(pir1)) {
        return true;
    }

    IR2_OPND *zero = &latxs_zero_ir2_opnd;

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);

    IR2_OPND src0 = latxs_ra_alloc_itemp();
    latxs_load_ir1_to_ir2(&src0, opnd0, EXMode_S);

    IR2_OPND dest = latxs_ra_alloc_itemp();
    latxs_append_ir2_opnd3(LISA_SUB_D, &dest, zero, &src0);

    if (ir1_opnd_is_mem(opnd0)) {
        latxs_store_ir2_to_ir1(&dest, opnd0);
        /* calculate elfags after store */
        latxs_generate_eflag_calculation(&dest, zero, &src0, pir1, true);
    } else {
        /* Destination is GPR, no exception will generate */
        latxs_generate_eflag_calculation(&dest, zero, &src0, pir1, true);
        latxs_store_ir2_to_ir1(&dest, opnd0);
    }

    latxs_ra_free_temp(&dest);
    latxs_ra_free_temp(&src0);
    return true;
}

bool latxs_translate_cmp(IR1_INST *pir1)
{
    if (option_by_hand_64 && latxs_translate_cmp_byhand64(pir1)) {
        return true;
    }

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    IR2_OPND src0 = latxs_ra_alloc_itemp();
    IR2_OPND src1 = latxs_ra_alloc_itemp();

    latxs_load_ir1_to_ir2(&src0, opnd0, EXMode_S);
    latxs_load_ir1_to_ir2(&src1, opnd1, EXMode_S);

    IR2_OPND dest = latxs_ra_alloc_itemp();

    latxs_append_ir2_opnd3(LISA_SUB_D, &dest, &src0, &src1);

    latxs_generate_eflag_calculation(&dest, &src0, &src1, pir1, true);

    latxs_ra_free_temp(&dest);
    latxs_ra_free_temp(&src0);
    latxs_ra_free_temp(&src1);
    return true;
}

bool latxs_translate_mul(IR1_INST *pir1)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);

    IR2_OPND src0 = latxs_ra_alloc_itemp();
    IR2_OPND src1 = latxs_ra_alloc_itemp();

    latxs_load_ir1_to_ir2(&src0, opnd0, EXMode_Z);

    IR2_OPND dest = latxs_ra_alloc_itemp();
    IR2_OPND dest_hi = latxs_ra_alloc_itemp();

    int opnd_size = ir1_opnd_size(opnd0);
    switch (opnd_size) {
    case 8:
        latxs_load_ir1_gpr_to_ir2(&src1, &al_ir1_opnd, EXMode_Z);
        latxs_append_ir2_opnd3(LISA_MUL_W, &dest, &src1, &src0);
        latxs_store_ir2_to_ir1(&dest, &ax_ir1_opnd);
        break;
    case 16:
        latxs_load_ir1_gpr_to_ir2(&src1, &ax_ir1_opnd, EXMode_Z);
        latxs_append_ir2_opnd3(LISA_MUL_W, &dest, &src1, &src0);
        latxs_store_ir2_to_ir1(&dest, &ax_ir1_opnd);
        latxs_append_ir2_opnd2i(LISA_SRLI_W, &dest, &dest, 16);
        latxs_store_ir2_to_ir1(&dest, &dx_ir1_opnd);
        break;
    case 32:
        latxs_load_ir1_gpr_to_ir2(&src1, &eax_ir1_opnd, EXMode_Z);
        latxs_append_ir2_opnd3(LISA_MUL_D, &dest, &src1, &src0);
        latxs_store_ir2_to_ir1(&dest, &eax_ir1_opnd);
        latxs_append_ir2_opnd2i(LISA_SRLI_D, &dest, &dest, 32);
        latxs_store_ir2_to_ir1(&dest, &edx_ir1_opnd);
        break;
    case 64:
        latxs_load_ir1_gpr_to_ir2(&src1, &rax_ir1_opnd, EXMode_Z);
        latxs_append_ir2_opnd3(LISA_MUL_D, &dest, &src1, &src0);
        latxs_append_ir2_opnd3(LISA_MULH_DU, &dest_hi, &src1, &src0);
        latxs_store_ir2_to_ir1(&dest, &rax_ir1_opnd);
        latxs_store_ir2_to_ir1(&dest_hi, &rdx_ir1_opnd);
        break;
    default:
        lsassert(0);
        break;
    }

    latxs_generate_eflag_calculation(&dest, &src0, &src1, pir1, true);
    latxs_ra_free_temp(&dest_hi);
    latxs_ra_free_temp(&dest);
    latxs_ra_free_temp(&src0);
    latxs_ra_free_temp(&src1);
    return true;
}

static bool latxs_translate_imul_1_opnd(IR1_INST *pir1)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);

    IR2_OPND src0 = latxs_ra_alloc_itemp();
    IR2_OPND src1 = latxs_ra_alloc_itemp();

    IR2_OPND dest = latxs_ra_alloc_itemp();
    IR2_OPND dest_hi = latxs_ra_alloc_itemp();

    /* Destination is always GPR, no exception will generate */
    int opnd_size = ir1_opnd_size(opnd0);
    switch (opnd_size) {
    case 8:
        latxs_load_ir1_to_ir2(&src0, opnd0, EXMode_S);
        latxs_load_ir1_gpr_to_ir2(&src1, &al_ir1_opnd, EXMode_S);
        latxs_append_ir2_opnd3(LISA_MUL_W, &dest, &src1, &src0);
        latxs_generate_eflag_calculation(&dest, &src0, &src1, pir1, true);
        latxs_store_ir2_to_ir1(&dest, &ax_ir1_opnd);
        break;
    case 16:
        latxs_load_ir1_to_ir2(&src0, opnd0, EXMode_S);
        latxs_load_ir1_gpr_to_ir2(&src1, &ax_ir1_opnd, EXMode_S);
        latxs_append_ir2_opnd3(LISA_MUL_W, &dest, &src1, &src0);
        latxs_generate_eflag_calculation(&dest, &src0, &src1, pir1, true);
        latxs_store_ir2_to_ir1(&dest, &ax_ir1_opnd);
        latxs_append_ir2_opnd2i(LISA_SRLI_D, &dest, &dest, 16);
        latxs_store_ir2_to_ir1(&dest, &dx_ir1_opnd);
        break;
    case 32:
        latxs_load_ir1_to_ir2(&src0, opnd0, EXMode_S);
        latxs_load_ir1_gpr_to_ir2(&src1, &eax_ir1_opnd, EXMode_S);
        latxs_append_ir2_opnd3(LISA_MUL_D, &dest, &src1, &src0);
        latxs_generate_eflag_calculation(&dest, &src0, &src1, pir1, true);
        latxs_store_ir2_to_ir1(&dest, &eax_ir1_opnd);
        latxs_append_ir2_opnd2i(LISA_SRLI_D, &dest, &dest, 32);
        latxs_store_ir2_to_ir1(&dest, &edx_ir1_opnd);
        break;
    case 64:
#if 1
        if (ir1_opnd_is_gpr(opnd0)) {
            src0 = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd0));
        } else {
            latxs_load_ir1_to_ir2(&src0, opnd0, EXMode_S);
        }
        src1 = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(&rax_ir1_opnd));
        dest_hi = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(&rdx_ir1_opnd));
        la_out3(LISA_MUL_D, &dest, &src1, &src0);
        latxs_generate_eflag_calculation(&dest, &src0, &src1, pir1, true);
        la_out3(LISA_MULH_D, &dest_hi, &src1, &src0);
        la_out3(LISA_OR, &src1, &dest, &la_zero);
#else
        latxs_load_ir1_to_ir2(&src0, opnd0, EXMode_S);
        latxs_load_ir1_gpr_to_ir2(&src1, &rax_ir1_opnd, EXMode_Z);
        latxs_append_ir2_opnd3(LISA_MUL_D, &dest, &src1, &src0);
        latxs_generate_eflag_calculation(&dest, &src0, &src1, pir1, true);
        latxs_append_ir2_opnd3(LISA_MULH_D, &dest_hi, &src1, &src0);
        latxs_store_ir2_to_ir1(&dest, &rax_ir1_opnd);
        latxs_store_ir2_to_ir1(&dest_hi, &rdx_ir1_opnd);
#endif
        break;
    default:
        lsassert(0);
        break;
    }

    latxs_ra_free_temp(&dest_hi);

    latxs_ra_free_temp(&dest);
    latxs_ra_free_temp(&src0);
    latxs_ra_free_temp(&src1);
    return true;
}

static bool translate_imul_(IR1_INST *pir1,
                            IR1_OPND *dest, IR1_OPND *src0, IR1_OPND *src1)
{
    /*
     * IMUL r{16,32,64}, r{16,32,64}, r/m{16,32,64} <- hahaha
     *
     * IMUL r{16,32,64}, r/m{16,32,64}, imm8
     * IMUL r16,         r/m16,         imm16
     * IMUL r{32,64},    r/m{32,64},    imm32
     *
     * 1. IMUL r{16,32,64},  r{16,32,64}, imm{8,16,32}
     *   1.1. IMUL r{64},    r{64},       imm{8,32}    < mul.d reg1, reg2, temp
     *   1.2. IMUL r{32},    r{32},       imm{8,32}    < mul.w temp, reg2, temp'
     *   1.3. IMUL r{16},    r{16},       imm{8,16}    < default
     * 2. IMUL r{16,32,64},  m{16,32,64}, imm{8,16,32}
     *   1.1. IMUL r{64},    m{64},       imm{8,32}    < mul.d reg1, temp, temp
     *   1.2. IMUL r{16,32}, m{16,32},    imm{8,16,32} < default
     * 3. IMUL r{16,32,64}, r{16,32,64}, r/m{16,32,64}
     *   3.1. IMUL r{64}, r{64}, r{64}                 < mul.d reg1, reg2, reg3
     *   3.2. IMUL r{32}, r{32}, r{32}                 < mul.w temp, reg2, reg3
     *   3.3. IMUL r{64}, r{64}, m{64}                 < mul.d reg1, reg2, temp
     *   3.4. IMUL r{32}, r{32}, m{32}                 < mul.w temp, reg2, temp'
     *   3.5. IMUL r{16}, r{16}, r/m{16}               < default
     *
     * ---------- collect all switch ----------
     * 0. IMUL r{16},    r/m{16},  r/m/imm{?}          < default
     * 1. IMUL r{32,64}, r{32,64}, r{32,64}
     *   1.1. IMUL r{64}, r{64}, r{64}                 < mul.d reg1, reg2, reg3
     *   1.2. IMUL r{32}, r{32}, r{32}                 < mul.w temp, reg2, reg3
     * 2. IMUL r{32,64}, r{32,64}, imm{8,32}/m{32,64}
     *   2.1. IMUL r{64}, r{64}, imm{8,32}/m{64}       < mul.d reg1, reg2, temp
     *   2.2. IMUL r{32}, r{32}, imm{8,32}/m{32}       < mul.w temp, reg2, temp'
     * 3. IMUL r{32,64}, m{32,64}, imm{8,32}
     *   3.1. IMUL r{64}, m{64}, imm{8,32}             < mul.d reg1, temp, temp
     *   3.2. IMUL r{32}, m{32}, imm{8,32}             < default
     */
    lsassert(ir1_opnd_is_gpr(dest));
    int dest_size = ir1_opnd_size(dest);
    IR2_OPND dest_ir2, src0_ir2, src1_ir2;
    if (dest_size == 16) {
        /* 0. IMUL r{16},    r/m{16},  r/m/imm{?} */
        goto _default;
    }
    if (ir1_opnd_is_gpr(src0)) {
        //dest_ir2 = ra_alloc_gpr(ir1_opnd_base_reg_num(dest));
        dest_ir2 = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(dest));
        //src0_ir2 = ra_alloc_gpr(ir1_opnd_base_reg_num(src0));
        src0_ir2 = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(src0));
        if (ir1_opnd_is_gpr(src1)) {
            /* 1. IMUL r{32,64}, r{32,64}, r{32,64} */
            //src1_ir2 = ra_alloc_gpr(ir1_opnd_base_reg_num(src1));
            src1_ir2 = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(src1));
        } else {
            /* 2. IMUL r{32,64}, r{32,64}, imm{8,32}/m{32,64} */
            //src1_ir2 =
            //    load_ireg_from_ir1(src1, SIGN_EXTENSION, false);
            src1_ir2 = latxs_ra_alloc_itemp();
            latxs_load_ir1_to_ir2(&src1_ir2, src1, EXMode_S);
        }
        latxs_generate_eflag_calculation(&dest_ir2, &src0_ir2, &src1_ir2, pir1, true);
        if (dest_size == 64) {
            /* 1.1. IMUL r{64}, r{64}, r{64} */
            /* -> mul.d reg1, reg2, reg3 */
            /* 2.1. IMUL r{64}, r{64}, imm{8,32}/m{64} */
            /* -> mul.d reg1, reg2, temp */
            //la_mul_d(dest_ir2, src0_ir2, src1_ir2);
            latxs_append_ir2_opnd3(LISA_MUL_D, &dest_ir2, &src0_ir2, &src1_ir2);
        } else {
            lsassert(dest_size == 32);
            /* 1.2. IMUL r{32}, r{32}, r{32} */
            /* -> mul.w temp, reg2, reg3 */
            /* 2.2. IMUL r{32}, r{32}, imm{8,32}/m{32} */
            /* -> mul.w temp, reg2, temp' */
            IR2_OPND temp = latxs_ra_alloc_itemp();
            //la_mul_w(temp, src0_ir2, src1_ir2);
            latxs_append_ir2_opnd3(LISA_MUL_W, &temp, &src0_ir2, &src1_ir2);
            //la_mov32_zx(dest_ir2, temp);
            latxs_append_ir2_opnd2ii(LISA_BSTRPICK_D, &dest_ir2, &temp, 31, 0);
            //ra_free_temp(temp);
            latxs_ra_free_temp(&temp);
        }

        if (latxs_ir2_opnd_is_itemp(&src1_ir2)) {
            latxs_ra_free_temp(&src1_ir2);
        }
    } else {
        /* 3. IMUL r{32,64}, m{32,64}, imm{8,32} */
        lsassert(ir1_opnd_is_mem(src0) && ir1_opnd_is_imm(src1));
        if (dest_size == 64) {
            /* 3.1. IMUL r{64}, m{64}, imm{8,32} */
            /* -> mul.d reg1, temp, temp */
            dest_ir2 = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(dest));
            //src0_ir2 = load_ireg_from_ir1(src0, SIGN_EXTENSION, false);
            src0_ir2 = latxs_ra_alloc_itemp();
            latxs_load_ir1_to_ir2(&src0_ir2, src0, EXMode_S);
            //src1_ir2 = load_ireg_from_ir1(src1, SIGN_EXTENSION, false);
            src1_ir2 = latxs_ra_alloc_itemp();
            latxs_load_ir1_to_ir2(&src1_ir2, src1, EXMode_S);
            latxs_generate_eflag_calculation(&dest_ir2, &src0_ir2, &src1_ir2, pir1, true);

            //la_mul_d(dest_ir2, src0_ir2, src1_ir2);
            latxs_append_ir2_opnd3(LISA_MUL_D, &dest_ir2, &src0_ir2, &src1_ir2);

            latxs_ra_free_temp(&src0_ir2);
            latxs_ra_free_temp(&src1_ir2);
        } else {
            /* 3.2. IMUL r{32}, m{32}, imm{8,32} */
            lsassert(dest_size == 32);
            goto _default;
        }
    }

    return true;
_default:
    /*
     * IMUL r{16}, r/m{16},  r/m/imm{?}
     * IMUL r{32}, m{32},    imm{8,32}
     */
    //src0_ir2 = load_ireg_from_ir1(src0, SIGN_EXTENSION, false);
    src0_ir2 = latxs_ra_alloc_itemp();
    latxs_load_ir1_to_ir2(&src0_ir2, src0, EXMode_S);
    //src1_ir2 = load_ireg_from_ir1(src1, SIGN_EXTENSION, false);
    src1_ir2 = latxs_ra_alloc_itemp();
    latxs_load_ir1_to_ir2(&src1_ir2, src1, EXMode_S);

    dest_ir2 = latxs_ra_alloc_itemp();
    //la_mul_w(dest_ir2, src0_ir2, src1_ir2);
    latxs_append_ir2_opnd3(LISA_MUL_W, &dest_ir2, &src0_ir2, &src1_ir2);
    latxs_generate_eflag_calculation(&dest_ir2, &src0_ir2, &src1_ir2, pir1, true);
    /*
     * SF is architecturally undefined after IMUL: the usedef table marks
     * SF as undef for IMUL, so ir1_need_calculate_sf() is false here and
     * the main IMUL paths never materialise SF either.  Mirror the main
     * path and only write it when a flag consumer actually needs it
     * instead of paying an unconditional 3 ALU + M-EFLAGS RMW.
     */
    if (ir1_need_calculate_sf(pir1)) {
        IR2_OPND eflag_tmp_ir2 = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd3(LISA_XOR, &eflag_tmp_ir2, &src0_ir2, &src1_ir2);
        latxs_append_ir2_opnd2i(LISA_SRLI_W, &eflag_tmp_ir2, &eflag_tmp_ir2, 0x8);
        latxs_append_ir2_opnd1i(LISA_X86MTFLAG, &eflag_tmp_ir2, 0x10);
        latxs_ra_free_temp(&eflag_tmp_ir2);
    }
    //store_ireg_to_ir1(dest_ir2, dest, false);
    latxs_store_ir2_to_ir1(&dest_ir2, dest);
    //ra_free_temp(dest_ir2);
    latxs_ra_free_temp(&dest_ir2);
    latxs_ra_free_temp(&src1_ir2);
    latxs_ra_free_temp(&src0_ir2);
    return true;
}

bool latxs_translate_imul(IR1_INST *pir1)
{
    if (ir1_opnd_num(pir1) == 1) {
        return latxs_translate_imul_1_opnd(pir1);
    } else if (ir1_opnd_num(pir1) == 2) {
        /*
         * IMUL r{16,32,64}, r/m{16,32,64}
         */
        IR1_OPND *src0 = ir1_get_opnd(pir1, 0);
        IR1_OPND *src1 = ir1_get_opnd(pir1, 1);
        return translate_imul_(pir1, src0, src0, src1);
    } else {
        lsassert(ir1_opnd_num(pir1) == 3);
        IR1_OPND *dest = ir1_get_opnd(pir1, 0);
        IR1_OPND *src0 = ir1_get_opnd(pir1, 1);
        IR1_OPND *src1 = ir1_get_opnd(pir1, 2);
        return translate_imul_(pir1, dest, src0, src1);
    }

    return true;
}

void latxs_tr_gen_div_result_check(IR1_INST *pir1,
        IR2_OPND result, int size, int is_idiv)
{
#ifdef CONFIG_SOFTMMU
    TRANSLATION_DATA *td = lsenv->tr_data;
    /*
     * u8   [0,     2^8 -1] = [0x0,         0xff        ]
     * u16  [0,     2^16-1] = [0x0,         0xffff      ]
     * u32  [0,     2^32-1] = [0x0,         0xffff_ffff ]
     * s8   [-2^7,  2^7 -1] = [0xffff_ff80, 0x7f        ]
     * s16  [-2^15, 2^15-1] = [0xffff_8000, 0x7fff      ]
     * s32  [-2^31, 2^31-1] = [0x8000_0000, 0x7fff_ffff ]
     */
    IR2_OPND label_no_excp = latxs_ir2_opnd_new_label();
    IR2_OPND tmp = latxs_ra_alloc_itemp();

    /* 1. chechk and branch to 'label_no_excp' */
    switch (size) {
    case 8:
        if (is_idiv) { /* signed 8-bit */
            latxs_append_ir2_opnd2_(lisa_mov8s, &tmp, &result);
        } else { /* unsigned 8-bit */
            latxs_append_ir2_opnd2_(lisa_mov8z, &tmp, &result);
        }
        break;
    case 16:
        if (is_idiv) { /* signed 16-bit */
            latxs_append_ir2_opnd2_(lisa_mov16s, &tmp, &result);
        } else { /* unsigned 16-bit */
            latxs_append_ir2_opnd2_(lisa_mov16z, &tmp, &result);
        }
        break;
    case 32:
        if (is_idiv) { /* signed 32-bit */
            latxs_append_ir2_opnd2_(lisa_mov32s, &tmp, &result);
        } else { /* unsigned 32-bit */
            latxs_append_ir2_opnd2_(lisa_mov32z, &tmp, &result);
        }
        break;
    default:
        /* should never reach here */
        lsassertm(0, "should never reach here [%s:%d].\n",
                __func__, __LINE__);
        break;
    }
    latxs_append_ir2_opnd3(LISA_BEQ, &result, &tmp, &label_no_excp);
    latxs_ra_free_temp(&tmp);

    /* 2. not branch: generate exception */
    td->force_curr_top_save_bak = td->force_curr_top_save;
    td->force_curr_top_save = 1;
    td->ignore_rcd_curr_top = 1;
    latxs_tr_gen_excp_divz(pir1, 0);
    td->force_curr_top_save = td->force_curr_top_save_bak;
    td->ignore_rcd_curr_top = 0;

    /* 3. branch: no exception */
    latxs_append_ir2_opnd1(LISA_LABEL, &label_no_excp);
#endif
}

bool latxs_translate_div(IR1_INST *pir1)
{
#ifdef CONFIG_SOFTMMU
    if (aot_offline_div_exception && ir1_opnd_num(pir1) == 1 &&
        ir1_opnd_is_mem(ir1_get_opnd(pir1, 0))) {
        latxs_tr_gen_excp_divz(pir1, 1);
        aot_offline_div_exception_translated++;
        return true;
    }
#endif

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);

    IR2_OPND src0 = latxs_ra_alloc_itemp();
    IR2_OPND src1 = latxs_ra_alloc_itemp();
    IR2_OPND src2 = latxs_ra_alloc_itemp();

    latxs_load_ir1_to_ir2(&src0, opnd0, EXMode_Z);

#ifdef CONFIG_SOFTMMU
    /* 1. if div zero, generate DIVZ exception */
    IR2_OPND label_not_zero = latxs_ir2_opnd_new_label();
    latxs_append_ir2_opnd3(LISA_BNE, &src0,
            &latxs_zero_ir2_opnd, &label_not_zero);
    /* 1.1 not branch : generate exception and never return */
    latxs_tr_gen_excp_divz(pir1, 0);
    /* 1.2 branch: go on finish div operation */
    latxs_append_ir2_opnd1(LISA_LABEL, &label_not_zero);
#endif

    IR2_OPND div_res = latxs_ra_alloc_itemp();
    IR2_OPND div_mod = latxs_ir2_opnd_new_inv();

    IR2_OPND src_tmp = latxs_ir2_opnd_new_inv();

    /* 2. do div operation */
    int opnd_size = ir1_opnd_size(opnd0);
    switch (opnd_size) {
    case 8:
        latxs_load_ir1_gpr_to_ir2(&src1, &ax_ir1_opnd, EXMode_Z);
        div_mod = latxs_ra_alloc_itemp();

        /* 2.1 do div operation and get result */
        latxs_append_ir2_opnd3(LISA_DIV_DU, &div_res, &src1, &src0);
        latxs_append_ir2_opnd3(LISA_MOD_DU, &div_mod, &src1, &src0);
        latxs_ra_free_temp(&src0);
        latxs_ra_free_temp(&src1);

        /* 2.2 check if result is valid and generate exception if not */
        latxs_tr_gen_div_result_check(pir1, div_res, 8, 0);
        /* 2.3 store the results into destination */
        latxs_append_ir2_opnd2ii(LISA_BSTRINS_D, &div_res, &div_mod, 15, 8);
        latxs_ra_free_temp(&div_mod);
        latxs_store_ir2_to_ir1(&div_res, &ax_ir1_opnd);
        break;
    case 16:
        latxs_load_ir1_gpr_to_ir2(&src1, &ax_ir1_opnd, EXMode_Z);
        latxs_load_ir1_gpr_to_ir2(&src2, &dx_ir1_opnd, EXMode_Z);
        src_tmp = latxs_ra_alloc_itemp();
        div_mod = latxs_ra_alloc_itemp();

        /* 2.1 do div operation and get result */
        latxs_append_ir2_opnd2i(LISA_SLLI_D, &src_tmp, &src2, 16);
        latxs_append_ir2_opnd3(LISA_OR, &src_tmp, &src_tmp, &src1);
        latxs_ra_free_temp(&src1);
        latxs_ra_free_temp(&src2);

        latxs_append_ir2_opnd3(LISA_DIV_DU, &div_res, &src_tmp, &src0);
        latxs_append_ir2_opnd3(LISA_MOD_DU, &div_mod, &src_tmp, &src0);
        latxs_ra_free_temp(&src_tmp);
        latxs_ra_free_temp(&src0);

        /* 2.2 check if result is valid and generate exception if not */
        latxs_tr_gen_div_result_check(pir1, div_res, 16, 0);
        /* 2.3 store the results into destination */
        latxs_store_ir2_to_ir1(&div_mod, &dx_ir1_opnd);
        latxs_ra_free_temp(&div_mod);
        latxs_store_ir2_to_ir1(&div_res, &ax_ir1_opnd);
        break;
    case 32:
        latxs_load_ir1_gpr_to_ir2(&src1, &eax_ir1_opnd, EXMode_Z);
        latxs_load_ir1_gpr_to_ir2(&src2, &edx_ir1_opnd, EXMode_N);
        src_tmp = latxs_ra_alloc_itemp();
        div_mod = latxs_ra_alloc_itemp();

        /* 2.1 do div operation and get result */
        latxs_append_ir2_opnd2i(LISA_SLLI_D, &src_tmp, &src2, 32);
        latxs_append_ir2_opnd3(LISA_OR, &src_tmp, &src_tmp, &src1);
        latxs_ra_free_temp(&src1);
        latxs_ra_free_temp(&src2);

        latxs_append_ir2_opnd3(LISA_DIV_DU, &div_res, &src_tmp, &src0);
        latxs_append_ir2_opnd3(LISA_MOD_DU, &div_mod, &src_tmp, &src0);
        latxs_ra_free_temp(&src_tmp);
        latxs_ra_free_temp(&src0);

        /* 2.2 check if result is valid and generate exception if not */
        latxs_tr_gen_div_result_check(pir1, div_res, 32, 0);
        /* 2.3 store the results into destination */
        latxs_store_ir2_to_ir1(&div_mod, &edx_ir1_opnd);
        latxs_ra_free_temp(&div_mod);
        latxs_store_ir2_to_ir1(&div_res, &eax_ir1_opnd);
        break;
    case 64: {
        IR2_OPND *zero = &latxs_zero_ir2_opnd;
        /*
         * src0 is load from opnd0
         * src1 and src2 is free to use as temp registers
         */
        /*ir1_dump(pir1);*/
        IR2_OPND small_opnd         = src0;
        IR2_OPND result             = src1;
        IR2_OPND result_remainder   = src2;

        /* huqi: 64 bits div is diffcult */
        IR2_OPND divisor_high   = small_opnd;
        IR2_OPND dividend_low   = latxs_ra_alloc_gpr(eax_index);
        IR2_OPND dividend_high  = latxs_ra_alloc_gpr(edx_index);

        /* 0. if RDX = 0, using `ddivu` directly */
        IR2_OPND label_begin  = latxs_ir2_opnd_new_label();
        IR2_OPND label_finish = latxs_ir2_opnd_new_label();
//      la_bne(dividend_high, zero_ir2_opnd, label_begin);
        latxs_append_ir2_opnd3(LISA_BNE, &dividend_high, zero, &label_begin);
        /* we need to make divisor not be changed by others */
//      la_add_d(result, divisor_high, zero_ir2_opnd);
        latxs_append_ir2_opnd3(LISA_ADD_D, &result, &divisor_high, zero);
//      la_mod_du(dividend_high, dividend_low, result);
        latxs_append_ir2_opnd3(LISA_MOD_DU, &dividend_high, &dividend_low, &result);
//      la_div_du(dividend_low, dividend_low, result);
        latxs_append_ir2_opnd3(LISA_DIV_DU, &dividend_low, &dividend_low, &result);
//      la_b(label_finish);
        latxs_append_ir2_opnd1(LISA_B, &label_finish);

        /* 1. init count result */
    /* label_begin: */
//      la_label(label_begin);
        latxs_append_ir2_opnd1(LISA_LABEL, &label_begin);
        /* first we need make divisor_high as a temp */
        if (!latxs_ir2_opnd_is_itemp(&divisor_high)) {
            divisor_high = latxs_ra_alloc_itemp();
            /* mov divisor to divisor_high */
//          la_add_d(divisor_high, zero_ir2_opnd, small_opnd);
            latxs_append_ir2_opnd3(LISA_ADD_D, &divisor_high, zero, &small_opnd);
        }
        IR2_OPND count = latxs_ra_alloc_itemp();
        IR2_OPND divisor = result_remainder;
//      la_addi_d(count, zero_ir2_opnd, 65);
        latxs_append_ir2_opnd2i(LISA_ADDI_D, &count, zero, 65);
//      la_add_d(result, zero_ir2_opnd, zero_ir2_opnd);
        latxs_append_ir2_opnd3(LISA_ADD_D, &result, zero, zero);
//      la_add_d(divisor, zero_ir2_opnd, zero_ir2_opnd);
        latxs_append_ir2_opnd3(LISA_ADD_D, &divisor, zero, zero);

        IR2_OPND dividend_less_divisor  = latxs_ra_alloc_itemp();
        IR2_OPND label_next_turn        = latxs_ir2_opnd_new_label();
        IR2_OPND label_do_check         = latxs_ir2_opnd_new_label();
        IR2_OPND label_do_div           = latxs_ir2_opnd_new_label();
        IR2_OPND label_save_result      = latxs_ir2_opnd_new_label();
        /* 2. identify next turn need calculate or not */
    /* label_next_turn: */
//      la_label(label_next_turn);
        latxs_append_ir2_opnd1(LISA_LABEL, &label_next_turn);
        /* 2.1 make count-- */
//      la_addi_d(count, count, -1);
        latxs_append_ir2_opnd2i(LISA_ADDI_D, &count, &count, -1);
        /* 2.2 for next turn, result will shift left 1 bit */
//      la_slli_d(result, result, 1);
        latxs_append_ir2_opnd2i(LISA_SLLI_D, &result, &result, 1);
        /* 2.3 if dividend < divisor, it means result is zero */
        /*     check if need do next turn, also shift divisor */
        /* 2.3.1 dividend_high < divisor_high */
//      la_sltu(dividend_less_divisor, dividend_high, divisor_high);
        latxs_append_ir2_opnd3(LISA_SLTU, &dividend_less_divisor,
                                          &dividend_high, &divisor_high);
//      la_bne(dividend_less_divisor, zero_ir2_opnd, label_do_check);
        latxs_append_ir2_opnd3(LISA_BNE, &dividend_less_divisor, zero, &label_do_check);
        /* 2.3.2 dividend_high = divisor_high but */
        /*       dividend_low < divisor_low */
//      la_sltu(dividend_less_divisor, dividend_low, divisor);
        latxs_append_ir2_opnd3(LISA_SLTU, &dividend_less_divisor,
                                          &dividend_low, &divisor);
//      la_bne(dividend_high, divisor_high, label_do_div);
        latxs_append_ir2_opnd3(LISA_BNE, &dividend_high, &divisor_high, &label_do_div);
//      la_bne(dividend_less_divisor, zero_ir2_opnd, label_do_check);
        latxs_append_ir2_opnd3(LISA_BNE, &dividend_less_divisor, zero, &label_do_check);

        /* 3. doing this turn div (sub) */
    /* label_do_div: */
//      la_label(label_do_div);
        latxs_append_ir2_opnd1(LISA_LABEL, &label_do_div);
        /* 3.1 result + 1 */
//      la_addi_d(result, result, 1);
        latxs_append_ir2_opnd2i(LISA_ADDI_D, &result, &result, 1);
        /* 3.2 remainder */
        /* 3.2.1 if dividend_low < divisor_low, dividend_high-- */
        /*       `dividend_less_divisor` is caculate before */
//      la_sub_d(dividend_high, dividend_high, dividend_less_divisor);
        latxs_append_ir2_opnd3(LISA_SUB_D, &dividend_high,
                                           &dividend_high, &dividend_less_divisor);
        /* 3.2.2 dividend_low - divisor_low */
//      la_sub_d(dividend_low, dividend_low, divisor);
        latxs_append_ir2_opnd3(LISA_SUB_D, &dividend_low,
                                           &dividend_low, &divisor);
        /* 3.2.2 dividend_high - divisor_high */
//      la_sub_d(dividend_high, dividend_high, divisor_high);
        latxs_append_ir2_opnd3(LISA_SUB_D, &dividend_high,
                                           &dividend_high, &divisor_high);
//      ra_free_temp(dividend_less_divisor);
        latxs_ra_free_temp(&dividend_less_divisor);

    /* label_do_check: */
//      la_label(label_do_check);
        latxs_append_ir2_opnd1(LISA_LABEL, &label_do_check);
        /* 4. if count == 0, break; */
//      la_beq(count, zero_ir2_opnd, label_save_result);
        latxs_append_ir2_opnd3(LISA_BEQ, &count, zero, &label_save_result);

        /* 5. Divisor >> 1 */
        IR2_OPND divisor_first_bit = latxs_ra_alloc_itemp();
//      la_srli_d(divisor, divisor, 1);
        latxs_append_ir2_opnd2i(LISA_SRLI_D, &divisor, &divisor, 1);
//      la_slli_d(divisor_first_bit, divisor_high, 63);
        latxs_append_ir2_opnd2i(LISA_SLLI_D, &divisor_first_bit, &divisor_high, 63);
//      la_or(divisor, divisor, divisor_first_bit);
        latxs_append_ir2_opnd3(LISA_OR, &divisor, &divisor, &divisor_first_bit);
//      la_srli_d(divisor_high, divisor_high, 1);
        latxs_append_ir2_opnd2i(LISA_SRLI_D, &divisor_high, &divisor_high, 1);
        /* 6. branch to next turn */
//      la_b(label_next_turn);
        latxs_append_ir2_opnd1(LISA_B, &label_next_turn);
        latxs_ra_free_temp(&divisor_first_bit);

    /* label_save_result: */
//      la_label(label_save_result);
        latxs_append_ir2_opnd1(LISA_LABEL, &label_save_result);
        /* 7. save to regs */
        /*    Known: dividend_low maybe RAX */
        /*    So we need save dividend_low to RDX first */
        IR2_OPND ir2_edx = latxs_ra_alloc_gpr(edx_index);
        IR2_OPND ir2_eax = latxs_ra_alloc_gpr(eax_index);
//      la_or(ir2_edx, zero_ir2_opnd, dividend_low);
        latxs_append_ir2_opnd3(LISA_OR, &ir2_edx, zero, &dividend_low);
//      la_or(ir2_eax, zero_ir2_opnd, result);
        latxs_append_ir2_opnd3(LISA_OR, &ir2_eax, zero, &result);
        /* 8. finish the DIV */
    /* label_finish: */
//      la_label(label_finish);
        latxs_append_ir2_opnd1(LISA_LABEL, &label_finish);

        latxs_ra_free_temp(&count);
        if (latxs_ir2_opnd_is_itemp(&divisor_high)) {
            latxs_ra_free_temp(&divisor_high);
        }
        break;
    }
    default:
        lsassert(0);
        break;
    }

    latxs_ra_free_temp(&div_res);
    return true;
}

bool latxs_translate_idiv(IR1_INST *pir1)
{
#ifdef CONFIG_SOFTMMU
    if (aot_offline_div_exception && ir1_opnd_num(pir1) == 1 &&
        ir1_opnd_is_mem(ir1_get_opnd(pir1, 0))) {
        latxs_tr_gen_excp_divz(pir1, 1);
        aot_offline_div_exception_translated++;
        return true;
    }
#endif

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);

    IR2_OPND src0 = latxs_ra_alloc_itemp();
    IR2_OPND src1 = latxs_ra_alloc_itemp();
    IR2_OPND src2 = latxs_ra_alloc_itemp();

    latxs_load_ir1_to_ir2(&src0, opnd0, EXMode_S);

#ifdef CONFIG_SOFTMMU
    /* 1. if div zero, generate DIVZ exception */
    IR2_OPND label_not_zero = latxs_ir2_opnd_new_label();
    latxs_append_ir2_opnd3(LISA_BNE, &src0,
            &latxs_zero_ir2_opnd, &label_not_zero);
    /* 1.1 not branch : generate exception and never return */
    latxs_tr_gen_excp_divz(pir1, 0);
    /* 1.2 branch: go on finish div operation */
    latxs_append_ir2_opnd1(LISA_LABEL, &label_not_zero);
#endif

    IR2_OPND div_res = latxs_ra_alloc_itemp();
    IR2_OPND div_mod = latxs_ir2_opnd_new_inv();

    IR2_OPND src_tmp = latxs_ir2_opnd_new_inv();

    /* 2. do div operation */
    int opnd_size = ir1_opnd_size(opnd0);
    switch (opnd_size) {
    case 8:
        latxs_load_ir1_gpr_to_ir2(&src1, &ax_ir1_opnd, EXMode_S);
        div_mod = latxs_ra_alloc_itemp();

        /* 2.1 do div operation and get result */
        latxs_append_ir2_opnd3(LISA_DIV_D, &div_res, &src1, &src0);
        latxs_append_ir2_opnd3(LISA_MOD_D, &div_mod, &src1, &src0);
        latxs_ra_free_temp(&src0);
        latxs_ra_free_temp(&src1);

        /* 2.2 check if result is valid and generate exception if not */
        latxs_tr_gen_div_result_check(pir1, div_res, 8, 1);
        /* 2.3 store the results into destination */
        latxs_append_ir2_opnd2ii(LISA_BSTRINS_D, &div_res, &div_mod, 15, 8);
        latxs_ra_free_temp(&div_mod);
        latxs_store_ir2_to_ir1(&div_res, &ax_ir1_opnd);
        break;
    case 16:
        latxs_load_ir1_gpr_to_ir2(&src1, &ax_ir1_opnd, EXMode_Z);
        latxs_load_ir1_gpr_to_ir2(&src2, &dx_ir1_opnd, EXMode_S);
        src_tmp = latxs_ra_alloc_itemp();
        div_mod = latxs_ra_alloc_itemp();

        /* 2.1 do div operation and get result */
        latxs_append_ir2_opnd2i(LISA_SLLI_D, &src_tmp, &src2, 16);
        latxs_append_ir2_opnd3(LISA_OR, &src_tmp, &src_tmp, &src1);
        latxs_ra_free_temp(&src1);
        latxs_ra_free_temp(&src2);

        latxs_append_ir2_opnd3(LISA_DIV_D, &div_res, &src_tmp, &src0);
        latxs_append_ir2_opnd3(LISA_MOD_D, &div_mod, &src_tmp, &src0);
        latxs_ra_free_temp(&src_tmp);
        latxs_ra_free_temp(&src0);

        /* 2.2 check if result is valid and generate exception if not */
        latxs_tr_gen_div_result_check(pir1, div_res, 16, 1);
        /* 2.3 store the results into destination */
        latxs_store_ir2_to_ir1(&div_mod, &dx_ir1_opnd);
        latxs_ra_free_temp(&div_mod);
        latxs_store_ir2_to_ir1(&div_res, &ax_ir1_opnd);
        break;
    case 32:
        latxs_load_ir1_gpr_to_ir2(&src1, &eax_ir1_opnd, EXMode_Z);
        latxs_load_ir1_gpr_to_ir2(&src2, &edx_ir1_opnd, EXMode_N);
        src_tmp = latxs_ra_alloc_itemp();
        div_mod = latxs_ra_alloc_itemp();

        /* 2.1 do div operation and get result */
        latxs_append_ir2_opnd2i(LISA_SLLI_D, &src_tmp, &src2, 32);
        latxs_append_ir2_opnd3(LISA_OR, &src_tmp, &src_tmp, &src1);
        latxs_ra_free_temp(&src1);
        latxs_ra_free_temp(&src2);

        latxs_append_ir2_opnd3(LISA_DIV_D, &div_res, &src_tmp, &src0);
        latxs_append_ir2_opnd3(LISA_MOD_D, &div_mod, &src_tmp, &src0);
        latxs_ra_free_temp(&src_tmp);
        latxs_ra_free_temp(&src0);

        /* 2.2 check if result is valid and generate exception if not */
        latxs_tr_gen_div_result_check(pir1, div_res, 32, 1);
        /* 2.3 store the results into destination */
        latxs_store_ir2_to_ir1(&div_mod, &edx_ir1_opnd);
        latxs_ra_free_temp(&div_mod);
        latxs_store_ir2_to_ir1(&div_res, &eax_ir1_opnd);
        break;
    case 64: {
        IR2_OPND *zero = &latxs_zero_ir2_opnd;
        IR2_OPND rdx = latxs_ra_alloc_gpr(edx_index);
        IR2_OPND rax = latxs_ra_alloc_gpr(eax_index);
        /*
         * src0 is load from opnd0
         * src1 and src2 is free to use as temp registers
         */
        IR2_OPND dest_opnd = src1;
        latxs_ra_free_temp(&src2);
        latxs_ra_free_temp(&div_res);

        /*ir1_dump(pir1);*/

        /* first we need make divisor_high a temp */
        IR2_OPND divisor_high = src0;
        if (!latxs_ir2_opnd_is_itemp(&divisor_high)) {
            divisor_high = latxs_ra_alloc_itemp();
            /* mov divisor_temp to divisor */
//          la_add_d(divisor_high, zero_ir2_opnd, src_opnd_0);
            latxs_append_ir2_opnd3(LISA_ADD_D, &divisor_high, zero, &src0);
        }
        /* for IDIV, sign need to be considered */
        IR2_OPND dividend_low   = latxs_ra_alloc_gpr(eax_index);
        IR2_OPND dividend_high  = latxs_ra_alloc_gpr(edx_index);
        IR2_OPND flags          = latxs_ra_alloc_itemp();
        /* clean flags, bit1 is result, bit 2 is remainder */
//      la_add_d(flags, zero_ir2_opnd, zero_ir2_opnd);
        latxs_append_ir2_opnd3(LISA_ADD_D, &flags, zero, zero);
        IR2_OPND label_check_divisor            = latxs_ir2_opnd_new_label();
        IR2_OPND label_dividend_low_not_zero    = latxs_ir2_opnd_new_label();
        IR2_OPND label_div                      = latxs_ir2_opnd_new_label();

        /* 0. pre-work: sign change */
        /* In this part, all the data will change to positive number */
        /* 0.1 check dividend */
        /* 0.1.1 if dividend_high >= 0, mark need not change the dividend */
//      la_bge(dividend_high, zero_ir2_opnd, label_check_divisor);
        latxs_append_ir2_opnd3(LISA_BGE, &dividend_high, zero, &label_check_divisor);
        /* 0.1.2 then dividend_high < 0, mark this is negative number */
        /*       we need change the number to positive */
        /* 0.1.2.1 we can set the flag */
        /*         bit1 and bit2 all set 1 */
//      la_xori(flags, flags, 1);
//      la_xori(flags, flags, 2);
        latxs_append_ir2_opnd2i(LISA_XORI, &flags, &flags, 0x1);
        latxs_append_ir2_opnd2i(LISA_XORI, &flags, &flags, 0x2);
        /* 0.1.2.2 when dividend_low == 0 */
        /*         dividend_high = ~(dividend_high - 1) */
        /*         dividend_low not change */
//      la_bne(dividend_low, zero_ir2_opnd, label_dividend_low_not_zero);
        latxs_append_ir2_opnd3(LISA_BNE, &dividend_low, zero, &label_dividend_low_not_zero);
//      la_addi_d(dividend_high, dividend_high, -1);
        latxs_append_ir2_opnd2i(LISA_ADDI_D, &dividend_high, &dividend_high, -1);

    /* label_dividend_low_not_zero: */
//      la_label(label_dividend_low_not_zero);
        latxs_append_ir2_opnd1(LISA_LABEL, &label_dividend_low_not_zero);
        /* 0.1.2.3 when dividend_low != 0 */
        /*         dividend_high = ~dividend_high */
        /*         dividend_low = -dividend_low */
//      la_nor(dividend_high, dividend_high, zero_ir2_opnd);
        latxs_append_ir2_opnd3(LISA_NOR, &dividend_high, &dividend_high, zero);
//      la_sub_d(dividend_low, zero_ir2_opnd, dividend_low);
        latxs_append_ir2_opnd3(LISA_SUB_D, &dividend_low, zero, &dividend_low);

    /* label_check_divisor: */
        /* 0.2 check divisor */
//      la_label(label_check_divisor);
        latxs_append_ir2_opnd1(LISA_LABEL, &label_check_divisor);
        /* 0.2.3 if divisor >= 0, mark need not change  */
//      la_bge(divisor_high, zero_ir2_opnd, label_div);
        latxs_append_ir2_opnd3(LISA_BGE, &divisor_high, zero, &label_div);
        /* else divisor = -divisor */
//      la_sub_d(divisor_high, zero_ir2_opnd, divisor_high);
        latxs_append_ir2_opnd3(LISA_SUB_D, &divisor_high, zero, &divisor_high);
        /* also mark the flags */
//      la_xori(flags, flags, 1);
        latxs_append_ir2_opnd2i(LISA_XORI, &flags, &flags, 0x1);
    /* label_div: */
//      la_label(label_div);
        latxs_append_ir2_opnd1(LISA_LABEL, &label_div);
        /* begin div */

        /* 1. if RDX = 0, using `ddivu` directly */
        IR2_OPND label_begin    = latxs_ir2_opnd_new_label();
        IR2_OPND label_finish   = latxs_ir2_opnd_new_label();
//      la_bne(dividend_high, zero_ir2_opnd, label_begin);
        latxs_append_ir2_opnd3(LISA_BNE, &dividend_high, zero, &label_begin);
//      la_mod_du(ra_alloc_gpr(edx_index), dividend_low, divisor_high);
//      la_div_du(ra_alloc_gpr(eax_index), dividend_low, divisor_high);
        latxs_append_ir2_opnd3(LISA_MOD_DU, &rdx, &dividend_low, &divisor_high);
        latxs_append_ir2_opnd3(LISA_DIV_DU, &rax, &dividend_low, &divisor_high);

        /* 1.1 if flags & 2, RDX = -RDX */
        IR2_OPND ax_flag            = latxs_ra_alloc_itemp();
        IR2_OPND label_no_change    = latxs_ir2_opnd_new_label();
//      la_andi(ax_flag, flags, 2);
        latxs_append_ir2_opnd2i(LISA_ANDI, &ax_flag, &flags, 0x2);
//      la_beq(ax_flag, zero_ir2_opnd, label_no_change);
        latxs_append_ir2_opnd3(LISA_BEQ, &ax_flag, zero, &label_no_change);
        /* RDX = -RDX */
//      la_sub_d(ra_alloc_gpr(edx_index), zero_ir2_opnd, ra_alloc_gpr(edx_index));
        latxs_append_ir2_opnd3(LISA_SUB_D, &rdx, zero, &rdx);

    /* label_no_change: */
//      la_label(label_no_change);
        latxs_append_ir2_opnd1(LISA_LABEL, &label_no_change);
        /* 1.2 if flags & 1, RAX = -RAX */
//      la_andi(ax_flag, flags, 1);
        latxs_append_ir2_opnd2i(LISA_ANDI, &ax_flag, &flags, 1);
//      la_beq(ax_flag, zero_ir2_opnd, label_finish);
        latxs_append_ir2_opnd3(LISA_BEQ, &ax_flag, zero, &label_finish);
        /* RAX = -RAX */
//      la_sub_d(ra_alloc_gpr(eax_index), zero_ir2_opnd, ra_alloc_gpr(eax_index));
        latxs_append_ir2_opnd3(LISA_SUB_D, &rax, zero, &rax);
        latxs_ra_free_temp(&ax_flag);
//      la_b(label_finish);
        latxs_append_ir2_opnd1(LISA_B, &label_finish);

        /* 2. init count result */
    /* label_begin: */
//      la_label(label_begin);
        latxs_append_ir2_opnd1(LISA_LABEL, &label_begin);
        IR2_OPND count      = latxs_ra_alloc_itemp();
        IR2_OPND divisor    = latxs_ra_alloc_itemp();
        IR2_OPND result     = dest_opnd;
//      la_addi_d(count, zero_ir2_opnd, 65);
//      la_add_d(result, zero_ir2_opnd, zero_ir2_opnd);
//      la_add_d(divisor, zero_ir2_opnd, zero_ir2_opnd);
        latxs_append_ir2_opnd2i(LISA_ADDI_D, &count, zero, 65);
        latxs_append_ir2_opnd3(LISA_ADD_D, &result, zero, zero);
        latxs_append_ir2_opnd3(LISA_ADD_D, &divisor, zero, zero);

        IR2_OPND dividend_less_divisor = latxs_ra_alloc_itemp();
        IR2_OPND label_next_turn    = latxs_ir2_opnd_new_label();
        IR2_OPND label_do_check     = latxs_ir2_opnd_new_label();
        IR2_OPND label_do_div       = latxs_ir2_opnd_new_label();
        IR2_OPND label_save_result  = latxs_ir2_opnd_new_label();
        /* 3. identify next turn need caculate or not */
    /* label_next_turn: */
//      la_label(label_next_turn);
        latxs_append_ir2_opnd1(LISA_LABEL, &label_next_turn);
        /* 3.1 make count-- */
//      la_addi_d(count, count, -1);
        latxs_append_ir2_opnd2i(LISA_ADDI_D, &count, &count, -1);
        /* 3.2 for next turn, result will shift left 1 bit */
//      la_slli_d(result, result, 1);
        latxs_append_ir2_opnd2i(LISA_SLLI_D, &result, &result, 0x1);
        /* 3.3 if dividend < divisor, it means result is zero */
        /*     check if need do next turn, also shift divisor */
        /* 3.3.1 dividend_high < divisor_high */
//      la_sltu(dividend_less_divisor, dividend_high, divisor_high);
//      la_bne(dividend_less_divisor, zero_ir2_opnd, label_do_check);
        latxs_append_ir2_opnd3(LISA_SLTU, &dividend_less_divisor,
                                          &dividend_high, &divisor_high);
        latxs_append_ir2_opnd3(LISA_BNE,  &dividend_less_divisor,
                                          zero, &label_do_check);
        /* 3.3.2 dividend_high = divisor_high but */
        /*       dividend_low < divisor_low */
        /* no d_slot anymore, so exec before branch */
//      la_sltu(dividend_less_divisor, dividend_low, divisor);
//      la_bne(dividend_high, divisor_high, label_do_div);
//      la_bne(dividend_less_divisor, zero_ir2_opnd, label_do_check);
        latxs_append_ir2_opnd3(LISA_SLTU, &dividend_less_divisor,
                                          &dividend_low, &divisor);
        latxs_append_ir2_opnd3(LISA_BNE,  &dividend_high,
                                          &divisor_high, &label_do_div);
        latxs_append_ir2_opnd3(LISA_BNE,  &dividend_less_divisor,
                                          zero, &label_do_check);

        /* 4. doing this turn div (sub) */
    /* label_do_div: */
//      la_label(label_do_div);
        latxs_append_ir2_opnd1(LISA_LABEL, &label_do_div);
        /* 4.1 result + 1 */
//      la_addi_d(result, result, 1);
        latxs_append_ir2_opnd2i(LISA_ADDI_D, &result, &result, 0x1);
        /* 4.2 remainder */
        /* 4.2.1 if dividend_low < divisor_low, dividend_high-- */
        /*       `dividend_less_divisor` is caculate before */
//      la_sub_d(dividend_high, dividend_high, dividend_less_divisor);
        latxs_append_ir2_opnd3(LISA_SUB_D, &dividend_high,
                                           &dividend_high, &dividend_less_divisor);
        /* 4.2.2 dividend_low - divisor_low */
//      la_sub_d(dividend_low, dividend_low, divisor);
        latxs_append_ir2_opnd3(LISA_SUB_D, &dividend_low,
                                           &dividend_low, &divisor);
        /* 4.2.2 dividend_high - divisor_high */
//      la_sub_d(dividend_high, dividend_high, divisor_high);
        latxs_append_ir2_opnd3(LISA_SUB_D, &dividend_high,
                                           &dividend_high, &divisor_high);
        latxs_ra_free_temp(&dividend_less_divisor);

    /* label_do_check: */
//      la_label(label_do_check);
        latxs_append_ir2_opnd1(LISA_LABEL, &label_do_check);
        /* 5. if count == 0, break; */
//      la_beq(count, zero_ir2_opnd, label_save_result);
        latxs_append_ir2_opnd3(LISA_BEQ, &count, zero, &label_save_result);

        /* 6. Divisor >> 1 */
        IR2_OPND divisor_first_bit = latxs_ra_alloc_itemp();
//      la_srli_d(divisor, divisor, 1);
//      la_slli_d(divisor_first_bit, divisor_high, 63);
//      la_or(divisor, divisor, divisor_first_bit);
        latxs_append_ir2_opnd2i(LISA_SRLI_D, &divisor, &divisor, 0x1);
        latxs_append_ir2_opnd2i(LISA_SLLI_D, &divisor_first_bit, &divisor_high, 63);
        latxs_append_ir2_opnd3(LISA_OR, &divisor, &divisor, &divisor_first_bit);
        /* 7. branch to next turn */
//      la_srli_d(divisor_high, divisor_high, 1);
//      la_b(label_next_turn);
        latxs_append_ir2_opnd2i(LISA_SRLI_D, &divisor_high, &divisor_high, 0x1);
        latxs_append_ir2_opnd1(LISA_B, &label_next_turn);
        latxs_ra_free_temp(&divisor_first_bit);

    /* label_save_result: */
//      la_label(label_save_result);
        latxs_append_ir2_opnd1(LISA_LABEL, &label_save_result);
        /* 8. save to regs */
        /* 8.1 if flags & 2, RDX = -RDX */
        IR2_OPND ex_flag             = latxs_ra_alloc_itemp();
        IR2_OPND label_rdx_no_change = latxs_ir2_opnd_new_label();
        IR2_OPND ir2_edx             = latxs_ra_alloc_gpr(edx_index);
        IR2_OPND ir2_eax             = latxs_ra_alloc_gpr(eax_index);
//      la_andi(ex_flag, flags, 2);
//      la_or(ir2_edx, zero_ir2_opnd, dividend_low);
//      la_beq(ex_flag, zero_ir2_opnd, label_rdx_no_change);
        latxs_append_ir2_opnd2i(LISA_ANDI, &ex_flag, &flags, 0x2);
        latxs_append_ir2_opnd3(LISA_OR, &ir2_edx, zero, &dividend_low);
        latxs_append_ir2_opnd3(LISA_BEQ, &ex_flag, zero, &label_rdx_no_change);
        /* RDX = -dividend_low(RDX) */
//      la_sub_d(ir2_edx, zero_ir2_opnd, dividend_low);
        latxs_append_ir2_opnd3(LISA_SUB_D, &ir2_edx, zero, &dividend_low);
    /* label_rdx_no_change: */
//      la_label(label_rdx_no_change);
        latxs_append_ir2_opnd1(LISA_LABEL, &label_rdx_no_change);
        /* 8.2 if flags & 1, RAX = -RAX */
//      la_andi(ex_flag, flags, 1);
//      la_or(ir2_eax, zero_ir2_opnd, result);
//      la_beq(ex_flag, zero_ir2_opnd, label_finish);
        latxs_append_ir2_opnd2i(LISA_ANDI, &ex_flag, &flags, 0x1);
        latxs_append_ir2_opnd3(LISA_OR, &ir2_eax, zero, &result);
        latxs_append_ir2_opnd3(LISA_BEQ, &ex_flag, zero, &label_finish);
        /* RAX = -result(RAX) */
//      la_sub_d(ir2_eax, zero_ir2_opnd, result);
        latxs_append_ir2_opnd3(LISA_SUB_D, &ir2_eax, zero, &result);
        latxs_ra_free_temp(&ex_flag);
        /* 9. finish the DIV */
    /* label_finish: */
//      la_label(label_finish);
        latxs_append_ir2_opnd1(LISA_LABEL, &label_finish);

        latxs_ra_free_temp(&flags);
        latxs_ra_free_temp(&count);
        latxs_ra_free_temp(&divisor);
        if (latxs_ir2_opnd_is_itemp(&divisor_high)) {
            latxs_ra_free_temp(&divisor_high);
        }
        break;
    }
    default:
        lsassert(0);
        break;
    }

    latxs_ra_free_temp(&div_res);
    return true;
}

static bool latxs_do_translate_bcd(IR1_INST *pir1, int val)
{
    /* 1. only need to save eflags */
    // helper_cfg_t cfg = default_helper_cfg;
    // cfg.cfg.sv_eflags = 1;

    /*
     * 2. call helper and sync the eflags
     *    > EAX is used (caller-saved, always saved)
     *    > eflags is used and updated
     *
     * target/i386/int_helper.c
     * void helper_daa(CPUX86State *env)
     * void helper_das(CPUX86State *env)
     * void helper_aaa(CPUX86State *env)
     * void helper_aas(CPUX86State *env)
     * void helper_aam(CPUX86State *env, int base)
     * void helper_aad(CPUX86State *env, int base)
     */
    switch (ir1_opcode(pir1)) {
    case X86_INS_DAA:
        latxs_tr_gen_call_to_helper1_cfg((ADDR)helper_daa, nofp_helper_cfg, HELPER_DAA);
        break;
    case X86_INS_DAS:
        latxs_tr_gen_call_to_helper1_cfg((ADDR)helper_das, nofp_helper_cfg, HELPER_DAS);
        break;
    case X86_INS_AAA:
        latxs_tr_gen_call_to_helper1_cfg((ADDR)helper_aaa, nofp_helper_cfg, HELPER_AAA);
        break;
    case X86_INS_AAS:
        latxs_tr_gen_call_to_helper1_cfg((ADDR)helper_aas, nofp_helper_cfg, HELPER_AAS);
        break;
    case X86_INS_AAM:
        latxs_tr_gen_call_to_helper2_cfg((ADDR)helper_aam, val, nofp_helper_cfg, HELPER_AAM);
        break;
    case X86_INS_AAD:
        latxs_tr_gen_call_to_helper2_cfg((ADDR)helper_aad, val, nofp_helper_cfg, HELPER_AAD);
        break;
    default:
#ifdef CONFIG_SOFTMMU
        lsassertm_illop(ir1_addr(pir1), 0,
                "unknown bcd instruction.\n");
#else
        lsassertm(0, "unknown bcd instruction.\n");
#endif
        break;
    }

    return true;
}

bool latxs_translate_aaa(IR1_INST *pir1)
{
    return latxs_do_translate_bcd(pir1, 1);
}
bool latxs_translate_aad(IR1_INST *pir1)
{
    int val;
    if (!ir1_opnd_num(pir1)) {
        val = 0x0A;
    } else {
        val = ir1_opnd_uimm(ir1_get_opnd(pir1, 0));
    }

    return latxs_do_translate_bcd(pir1, val);
}
bool latxs_translate_aam(IR1_INST *pir1)
{
    int val;
    if (!ir1_opnd_num(pir1)) {
        val = 0x0A;
    } else {
        val = ir1_opnd_uimm(ir1_get_opnd(pir1, 0));
    }

    return latxs_do_translate_bcd(pir1, val);
}
bool latxs_translate_aas(IR1_INST *pir1)
{
    return latxs_do_translate_bcd(pir1, 1);
}
bool latxs_translate_daa(IR1_INST *pir1)
{
    return latxs_do_translate_bcd(pir1, 1);
}
bool latxs_translate_das(IR1_INST *pir1)
{
    return latxs_do_translate_bcd(pir1, 1);
}

bool latxs_translate_xadd(IR1_INST *pir1)
{
    if (latxs_ir1_has_prefix_lock(pir1) &&
        (lsenv->tr_data->sys.cflags & CF_PARALLEL)) {
        return latxs_translate_lock_xadd(pir1);
    }

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0); /* dest: GPR/MEM */
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1); /* src : GPR     */

    IR2_OPND src0 = latxs_ra_alloc_itemp();
    IR2_OPND src1 = latxs_ra_alloc_itemp();

    latxs_load_ir1_to_ir2(&src1, opnd1, EXMode_S);

    IR2_OPND sum = latxs_ra_alloc_itemp();

    if (ir1_opnd_is_gpr(opnd0)) {
        latxs_load_ir1_to_ir2(&src0, opnd0, EXMode_S);
        latxs_append_ir2_opnd3(LISA_ADD_D, &sum, &src0, &src1);
        latxs_store_ir2_to_ir1(&src0, opnd1);
        latxs_store_ir2_to_ir1(&sum, opnd0);
    } else {
        latxs_load_ir1_to_ir2(&src0, opnd0, EXMode_S);
        latxs_append_ir2_opnd3(LISA_ADD_D, &sum, &src0, &src1);
        latxs_store_ir2_to_ir1(&sum, opnd0);
        latxs_store_ir2_to_ir1(&src0, opnd1);
    }

    latxs_generate_eflag_calculation(&sum, &src0, &src1, pir1, true);

    latxs_ra_free_temp(&sum);

    return true;
}

bool latxs_translate_mulx(IR1_INST *pir1)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);
    IR1_OPND *opnd2 = ir1_get_opnd(pir1, 2);

    IR2_OPND dest0 = latxs_load_ireg_from_ir1(opnd0, EXMode_N);
    IR2_OPND dest1 = latxs_load_ireg_from_ir1(opnd1, EXMode_N);
    IR2_OPND src0 = latxs_load_ireg_from_ir1(opnd2, EXMode_N);
    IR2_OPND src1 = latxs_ra_alloc_itemp();
    //IR2_OPND src1 = load_ireg_from_ir1(&rdx_ir1_opnd, ZERO_EXTENSION, false);
    latxs_load_ir1_to_ir2(&src1, &rdx_ir1_opnd, EXMode_Z);
    /* TODO : if dest == src, this calculate will wrong. */
    if(dest0.val == src0.val || dest1.val == src0.val) {
        IR2_OPND src0_tmp = latxs_ra_alloc_itemp();
        //la_or(src0_tmp, zero_ir2_opnd, src0);
        latxs_append_ir2_opnd3(LISA_OR, &src0_tmp, &latxs_zero_ir2_opnd, &src0);
        src0 = src0_tmp;
    }
    if(dest0.val == src1.val || dest1.val == src1.val) {
        IR2_OPND src1_tmp = latxs_ra_alloc_itemp();
        //la_or(src1_tmp, zero_ir2_opnd, src1);
        latxs_append_ir2_opnd3(LISA_OR, &src1_tmp, &latxs_zero_ir2_opnd, &src1);
        src1 = src1_tmp;
    }

    if (ir1_opnd_size(opnd0) == 32) {
        /*
         * NOTE: for X86 MUL insn, we have to make sure src_opnd_0 data is zero
         * extension, otherwise, mul data will be incorrect.
         * la_mov32_zx(src_opnd_0, src_opnd_0);
         *
         * LYZ： but I think it is right, let us have a try.
         */
        //la_mul_w(dest1, src0, src1);
        latxs_append_ir2_opnd3(LISA_MUL_W, &dest1, &src0, &src1);
        //la_mulh_wu(dest0, src0, src1);
        latxs_append_ir2_opnd3(LISA_MULH_WU, &dest0, &src0, &src1);
        //la_bstrins_d(dest1, zero_ir2_opnd, 63, 32);
        latxs_append_ir2_opnd2ii(LISA_BSTRINS_D, &dest1, &latxs_zero_ir2_opnd, 63, 32);
        //la_bstrins_d(dest0, zero_ir2_opnd, 63, 32);
        latxs_append_ir2_opnd2ii(LISA_BSTRINS_D, &dest0, &latxs_zero_ir2_opnd, 63, 32);
    } else {
        /* 64-bit mode */
        //la_mul_d(dest1, src0, src1);
        latxs_append_ir2_opnd3(LISA_MUL_D, &dest1, &src0, &src1);
        //la_mulh_du(dest0, src0, src1);
        latxs_append_ir2_opnd3(LISA_MULH_DU, &dest0, &src0, &src1);
    }
    latxs_store_ir2_to_ir1(&dest1, opnd1);
    latxs_store_ir2_to_ir1(&dest0, opnd0);

    return true;
}
