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
#include "flag-lbt.h"
#include "sys-excp.h"
#include "latx-sys-flag-rdtn.h"

static void __tr_byhand64_mov_gpr_gpr(IR1_INST *pir1)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);
    int r0 = ir1_opnd_base_reg_num(opnd0);
    int r1 = ir1_opnd_base_reg_num(opnd1);
    IR2_OPND gpr0 = latxs_ra_alloc_gpr(r0);
    IR2_OPND gpr1 = latxs_ra_alloc_gpr(r1);

    IR2_OPND tmp = latxs_ra_alloc_itemp();
    int is_opnd0_8h = ir1_opnd_is_8h(opnd0);
    int is_opnd1_8h = ir1_opnd_is_8h(opnd1);

    if (is_opnd0_8h || is_opnd1_8h) {
        if (is_opnd0_8h && is_opnd1_8h && r0 == r1) {
            return;
        }
        if (is_opnd1_8h) {
            latxs_append_ir2_opnd2ii(LISA_BSTRPICK_D, &tmp, &gpr1, 15, 8);
            latxs_store_ir2_to_ir1_gpr(&tmp, opnd0);
            return;
        }
        latxs_append_ir2_opnd2ii(LISA_BSTRINS_D, &gpr0, &gpr1, 15, 8);
        return;
    }

    latxs_store_ir2_to_ir1_gpr(&gpr1, opnd0);
}

static void __tr_byhand64_mov_gpr_imm(IR1_INST *pir1)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    int r0 = ir1_opnd_base_reg_num(opnd0);
    IR2_OPND gpr0 = latxs_ra_alloc_gpr(r0);

    uint64_t uimm = ir1_opnd_uimm(opnd1);

    int os = ir1_opnd_size(opnd0);
    if (os == 64) {
        latxs_load_imm64_to_ir2(&gpr0, uimm);
    } else if (os == 32) {
        latxs_load_imm64_to_ir2(&gpr0, uimm);
        if ((uimm >> 32) != 0) {
            latxs_store_ir2_to_ir1_gpr(&gpr0, opnd0);
        }
    } else {
        IR2_OPND tmp = latxs_ra_alloc_itemp();
        latxs_load_imm64_to_ir2(&tmp, uimm);
        latxs_store_ir2_to_ir1_gpr(&tmp, opnd0);
    }
}

static void __tr_byhand64_mov_gpr_mem(IR1_INST *pir1)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    int r0 = ir1_opnd_base_reg_num(opnd0);
    IR2_OPND gpr0 = latxs_ra_alloc_gpr(r0);

    int os = ir1_opnd_size(opnd0);
    if (os == 64) {
        latxs_load_ir1_mem_to_ir2(&gpr0, opnd1, EXMode_N, -1);
    } else if (os == 32) {
        latxs_load_ir1_mem_to_ir2(&gpr0, opnd1, EXMode_Z, -1);
    } else {
        IR2_OPND tmp = latxs_ra_alloc_itemp();
        latxs_load_ir1_mem_to_ir2(&tmp, opnd1, EXMode_Z, -1);
        latxs_store_ir2_to_ir1_gpr(&tmp, opnd0);
    }
}

static void __tr_byhand64_mov_mem_gpr(IR1_INST *pir1)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    if (ir1_opnd_is_8h(opnd1)) {
        IR2_OPND gpr1 = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd1));
        IR2_OPND tmp = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd2ii(LISA_BSTRPICK_D, &tmp, &gpr1, 15, 8);
        latxs_store_ir2_to_ir1_mem(&tmp, opnd0, -1);
    } else {
        int r1 = ir1_opnd_base_reg_num(opnd1);
        IR2_OPND gpr1 = latxs_ra_alloc_gpr(r1);
        latxs_store_ir2_to_ir1_mem(&gpr1, opnd0, -1);
    }
}

static void __tr_byhand64_mov_mem_imm(IR1_INST *pir1)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    uint64_t uimm = ir1_opnd_uimm(opnd1);

    if (!uimm) {
        latxs_store_ir2_to_ir1_mem(&latxs_zero_ir2_opnd, opnd0, -1);
    } else {
        IR2_OPND tmp = latxs_ra_alloc_itemp();
        latxs_load_imm64_to_ir2(&tmp, uimm);
        latxs_store_ir2_to_ir1_mem(&tmp, opnd0, -1);
    }
}

bool latxs_translate_mov_byhand64(IR1_INST *pir1)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    if (ir1_opnd_is_gpr(opnd0)) {
        if (ir1_opnd_is_gpr(opnd1)) {
            __tr_byhand64_mov_gpr_gpr(pir1); return true;
        } else if (ir1_opnd_is_imm(opnd1)) {
            __tr_byhand64_mov_gpr_imm(pir1); return true;
        } else if (ir1_opnd_is_mem(opnd1)) {
            __tr_byhand64_mov_gpr_mem(pir1); return true;
        }
    } else if (ir1_opnd_is_mem(opnd0)) {
        if (ir1_opnd_is_gpr(opnd1)) {
            __tr_byhand64_mov_mem_gpr(pir1); return true;
        } else if (ir1_opnd_is_imm(opnd1)) {
            __tr_byhand64_mov_mem_imm(pir1); return true;
        }
    }

    return false;
}

static void __tr_byhand64_movzx_gpr(IR1_INST *pir1,
        IR1_OPND *opnd0, IR1_OPND *opnd1)
{
    /* opnd0: 16, 32, 64 */
    int r0 = ir1_opnd_base_reg_num(opnd0);
    IR2_OPND gpr0 = latxs_ra_alloc_gpr(r0);
    int os0 = ir1_opnd_size(opnd0);

    /* opnd1:  8, 16 */
    int r1 = ir1_opnd_base_reg_num(opnd1);
    IR2_OPND gpr1 = latxs_ra_alloc_gpr(r1);
    int os1 = ir1_opnd_size(opnd1);

    if (ir1_opnd_is_8h(opnd1)) {
        IR2_OPND tmp = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd2ii(LISA_BSTRPICK_D, &tmp, &gpr1, 15, 8);
        if (os0 == 16) {
            latxs_append_ir2_opnd2ii(LISA_BSTRINS_D, &gpr0, &tmp, 15, 0);
        } else {
            latxs_append_ir2_opnd2_(lisa_mov, &gpr0, &tmp);
        }
        return;
    }

    IR2_OPND *zero = &latxs_zero_ir2_opnd;

    if (os0 == 16) { /* r16 <= r8 */
        if (r0 == r1) {
            latxs_append_ir2_opnd2ii(LISA_BSTRINS_D, &gpr0, zero, 15, 8);
        } else {
            IR2_OPND tmp = latxs_ra_alloc_itemp();
            latxs_append_ir2_opnd2ii(LISA_BSTRPICK_D, &tmp, &gpr1, 7, 0);
            latxs_append_ir2_opnd2ii(LISA_BSTRINS_D, &gpr0, &tmp, 15, 0);
        }
    } else { /* r32/64 <= r8/r16 */
        if (os1 == 8) {
            latxs_append_ir2_opnd2ii(LISA_BSTRPICK_D, &gpr0, &gpr1,  7, 0);
        } else if (os1 == 16) {
            latxs_append_ir2_opnd2ii(LISA_BSTRPICK_D, &gpr0, &gpr1, 15, 0);
        } else {
            ir1_dump(pir1);
            lsassertm(0, "movzx wrong opnd1 size.\n");
        }
    }
}

static void __tr_byhand64_movzx_mem(IR1_INST *pir1,
        IR1_OPND *opnd0, IR1_OPND *opnd1)
{
    /* opnd0: 16, 32, 64 */
    int r0 = ir1_opnd_base_reg_num(opnd0);
    IR2_OPND gpr0 = latxs_ra_alloc_gpr(r0);
    int os0 = ir1_opnd_size(opnd0);

    if (os0 == 16) {
        IR2_OPND tmp = latxs_ra_alloc_itemp();
        latxs_load_ir1_mem_to_ir2(&tmp, opnd1, EXMode_Z, -1);
        latxs_append_ir2_opnd2ii(LISA_BSTRINS_D, &gpr0, &tmp, 15, 0);
    } else {
        latxs_load_ir1_mem_to_ir2(&gpr0, opnd1, EXMode_Z, -1);
    }
}

bool latxs_translate_movzx_byhand64(IR1_INST *pir1)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    if (ir1_opnd_is_gpr(opnd1)) {
        __tr_byhand64_movzx_gpr(pir1, opnd0, opnd1);
    } else {
        __tr_byhand64_movzx_mem(pir1, opnd0, opnd1);
    }

    return true;
}

static void __tr_byhand64_movsx_gpr(IR1_INST *pir1,
        IR1_OPND *opnd0, IR1_OPND *opnd1)
{
    /* opnd0: 16, 32, 64 */
    int r0 = ir1_opnd_base_reg_num(opnd0);
    IR2_OPND gpr0 = latxs_ra_alloc_gpr(r0);
    int os0 = ir1_opnd_size(opnd0);

    /* opnd1:  8, 16 */
    int r1 = ir1_opnd_base_reg_num(opnd1);
    IR2_OPND gpr1 = latxs_ra_alloc_gpr(r1);
    int os1 = ir1_opnd_size(opnd1); (void)os1;

    if (ir1_opnd_is_8h(opnd1)) {
        IR2_OPND tmp = latxs_ra_alloc_itemp();
        latxs_load_ir1_gpr_to_ir2(&tmp, opnd1, EXMode_S);
        if (os0 == 16) { /* r16 <= r8h */
            latxs_append_ir2_opnd2ii(LISA_BSTRINS_D, &gpr0, &tmp, 15, 0);
        } else if (os0 == 32) { /* r32 <= r8h */
            latxs_store_ir2_to_ir1_gpr(&tmp, opnd0);
        } else { /* r64 <= r8h */
            latxs_append_ir2_opnd2_(lisa_mov, &gpr0, &tmp);
        }
        return;
    }

    if (os0 == 16) { /* r16 <= r8l */
        lsassertm(os1 == 8, "movsx wrong opnd1 size\n");
        IR2_OPND tmp = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd2_(lisa_mov8s, &tmp, &gpr1);
        latxs_append_ir2_opnd2ii(LISA_BSTRINS_D, &gpr0, &tmp, 15, 0);
    } else if (os0 == 32) { /* r32 <= r8l/r16 */
        lsassertm(os1 == 8 || os1 == 16, "movsx wrong opnd1 size\n");
        IR2_OPND tmp = latxs_ra_alloc_itemp();
        latxs_load_ir1_gpr_to_ir2(&tmp, opnd1, EXMode_S);
        latxs_store_ir2_to_ir1_gpr(&tmp, opnd0);
    } else { /* r64 <= r8l/r16 */
        lsassertm(os1 == 8 || os1 == 16, "movsx wrong opnd1 size\n");
        latxs_load_ir1_gpr_to_ir2(&gpr0, opnd1, EXMode_S);
    }
}

static void __tr_byhand64_movsx_mem(IR1_INST *pir1,
        IR1_OPND *opnd0, IR1_OPND *opnd1)
{
    /* opnd0: 16, 32, 64 */
    int r0 = ir1_opnd_base_reg_num(opnd0);
    IR2_OPND gpr0 = latxs_ra_alloc_gpr(r0);
    int os0 = ir1_opnd_size(opnd0);

    if (os0 == 16) {
        IR2_OPND tmp = latxs_ra_alloc_itemp();
        latxs_load_ir1_mem_to_ir2(&tmp, opnd1, EXMode_S, -1);
        latxs_append_ir2_opnd2ii(LISA_BSTRINS_D, &gpr0, &tmp, 15, 0);
    } else if (os0 == 32) {
        IR2_OPND tmp = latxs_ra_alloc_itemp();
        latxs_load_ir1_mem_to_ir2(&tmp, opnd1, EXMode_S, -1);
        latxs_append_ir2_opnd2ii(LISA_BSTRINS_D, &tmp, &latxs_zero_ir2_opnd, 63, 32);
        latxs_append_ir2_opnd3(LISA_OR, &gpr0, &tmp, &latxs_zero_ir2_opnd);
    } else {
        latxs_load_ir1_mem_to_ir2(&gpr0, opnd1, EXMode_S, -1);
    }
}

bool latxs_translate_movsx_byhand64(IR1_INST *pir1)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    if (ir1_opnd_is_gpr(opnd1)) {
        __tr_byhand64_movsx_gpr(pir1, opnd0, opnd1);
    } else {
        __tr_byhand64_movsx_mem(pir1, opnd0, opnd1);
    }

    return true;
}

static void __tr_byhand64_movsxd_gpr(IR1_INST *pir1,
        IR1_OPND *opnd0, IR1_OPND *opnd1)
{
    /* opnd0: 16, 32, 64 */
    int r0 = ir1_opnd_base_reg_num(opnd0);
    IR2_OPND gpr0 = latxs_ra_alloc_gpr(r0);
    int os0 = ir1_opnd_size(opnd0);

    if (os0 == 64) { /* r64 <= r32 */
        int r1 = ir1_opnd_base_reg_num(opnd1);
        IR2_OPND gpr1 = latxs_ra_alloc_gpr(r1);
        lsassertm(ir1_opnd_size(opnd1) == 32, "movsxd wrong opnd1 size\n");
        latxs_append_ir2_opnd2_(lisa_mov32s, &gpr0, &gpr1);
    } else { /* r16 <= r16, r32 <= r32 */
        if (os0 == 32) {
            latxs_store_ir2_to_ir1_gpr(&gpr0, opnd0);
        }
    }
}

static void __tr_byhand64_movsxd_mem(IR1_INST *pir1,
        IR1_OPND *opnd0, IR1_OPND *opnd1)
{
    /* opnd0: 16, 32, 64 */
    int r0 = ir1_opnd_base_reg_num(opnd0);
    IR2_OPND gpr0 = latxs_ra_alloc_gpr(r0);
    int os0 = ir1_opnd_size(opnd0);

    if (os0 == 16) {
        IR2_OPND tmp = latxs_ra_alloc_itemp();
        latxs_load_ir1_mem_to_ir2(&tmp, opnd1, EXMode_S, -1);
        latxs_append_ir2_opnd2ii(LISA_BSTRINS_D, &gpr0, &tmp, 15, 0);
    } else if (os0 == 32) {
        IR2_OPND tmp = latxs_ra_alloc_itemp();
        latxs_load_ir1_mem_to_ir2(&tmp, opnd1, EXMode_S, -1);
        latxs_store_ir2_to_ir1_gpr(&tmp, opnd0);
    } else {
        latxs_load_ir1_mem_to_ir2(&gpr0, opnd1, EXMode_S, -1);
    }
}

bool latxs_translate_movsxd_byhand64(IR1_INST *pir1)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    if (ir1_opnd_is_gpr(opnd1)) {
        __tr_byhand64_movsxd_gpr(pir1, opnd0, opnd1);
    } else {
        __tr_byhand64_movsxd_mem(pir1, opnd0, opnd1);
    }

    return true;
}

static void __tr_byhand64_or_8h_8h(IR1_INST *pir1,
        IR2_OPND *gpr0, IR2_OPND *gpr1)
{
    IR2_OPND tmp0 = latxs_ra_alloc_itemp();
    IR2_OPND tmp1 = latxs_ra_alloc_itemp();
    latxs_append_ir2_opnd2i(LISA_SRLI_D, &tmp0, gpr0, 8);
    latxs_append_ir2_opnd2i(LISA_SRLI_D, &tmp1, gpr1, 8);
    LATXS_FLAG_REDUCTION_CHECKIF(pir1)
    latxs_append_ir2_opnd2(LISA_X86OR_B,   &tmp0, &tmp1);
    latxs_append_ir2_opnd3(LISA_OR, &tmp0, &tmp0, &tmp1);
    latxs_append_ir2_opnd2ii(LISA_BSTRINS_D, gpr0, &tmp0, 15, 8);
}

static void __tr_byhand64_or_8h_gpr(IR1_INST *pir1,
        IR2_OPND *gpr0, IR2_OPND *gpr1)
{
    IR2_OPND tmp0 = latxs_ra_alloc_itemp();
    latxs_append_ir2_opnd2i(LISA_SRLI_D, &tmp0, gpr0, 8);
    LATXS_FLAG_REDUCTION_CHECKIF(pir1)
    latxs_append_ir2_opnd2(LISA_X86OR_B,   &tmp0, gpr1);
    latxs_append_ir2_opnd3(LISA_OR, &tmp0, &tmp0, gpr1);
    latxs_append_ir2_opnd2ii(LISA_BSTRINS_D, gpr0, &tmp0, 15, 8);
}

static void __tr_byhand64_or_gpr_8h(IR1_INST *pir1,
        IR2_OPND *gpr0, IR2_OPND *gpr1)
{
    IR2_OPND tmp1 = latxs_ra_alloc_itemp();
    latxs_append_ir2_opnd2i(LISA_SRLI_D, &tmp1, gpr1, 8);
    LATXS_FLAG_REDUCTION_CHECKIF(pir1)
    latxs_append_ir2_opnd2(LISA_X86OR_B,   gpr0, &tmp1);
    latxs_append_ir2_opnd3(LISA_OR, &tmp1, gpr0, &tmp1);
    latxs_store_ir2_to_ir1_gpr(&tmp1, ir1_get_opnd(pir1, 0));
}

static void __tr_byhand64_or_gpr_gpr(IR1_INST *pir1)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);
    int r0 = ir1_opnd_base_reg_num(opnd0);
    int r1 = ir1_opnd_base_reg_num(opnd1);
    IR2_OPND reg0 = latxs_ra_alloc_gpr(r0);
    IR2_OPND reg1 = latxs_ra_alloc_gpr(r1);

    if (ir1_opnd_is_8h(opnd0)) {
        if (ir1_opnd_is_8h(opnd1)) {
            __tr_byhand64_or_8h_8h(pir1, &reg0, &reg1); /* xor 8H, 8H */
        } else {
            __tr_byhand64_or_8h_gpr(pir1, &reg0, &reg1); /* xor, 8H, reg8 */
        }
    } else {
        if (ir1_opnd_is_8h(opnd1)) {
            __tr_byhand64_or_gpr_8h(pir1, &reg0, &reg1); /* xor reg8, 8H */
        } else {
            /* xor reg8/16/32/64, reg8/16/32/64 */
            int os = ir1_opnd_size(opnd0); /* 8, 16, 32, 64 */
            if (os == 64) {
                LATXS_FLAG_REDUCTION_CHECKIF(pir1)
                latxs_append_ir2_opnd2(LISA_X86OR_D,   &reg0, &reg1);
                latxs_append_ir2_opnd3(LISA_OR, &reg0, &reg0, &reg1);
            } else {
                if (os == 32 && r0 == r1) {
                    LATXS_FLAG_REDUCTION_CHECKIF(pir1)
                    latxs_append_ir2_opnd2(LISA_X86OR_W, &reg0, &reg1);
                    latxs_store_ir2_to_ir1_gpr(&reg0, opnd0);
                } else {
                    LATXS_FLAG_REDUCTION_CHECKIF(pir1)
                    latxs_append_ir2_opnd2(LISA_X86OR_B + (os>>4), &reg0, &reg1);
                    IR2_OPND tmp0 = latxs_ra_alloc_itemp();
                    latxs_append_ir2_opnd3(LISA_OR, &tmp0, &reg0, &reg1);
                    latxs_store_ir2_to_ir1_gpr(&tmp0, opnd0);
                }
            }
        }
    }
}

static void __tr_byhand64_or_gpr_imm(IR1_INST *pir1)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    IR2_OPND reg0 = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd0));
    IR2_OPND tmp1 = latxs_ra_alloc_itemp();
    latxs_load_imm64_to_ir2(&tmp1, ir1_opnd_uimm(opnd1));

    if (ir1_opnd_is_8h(opnd0)) {
        /* xor 8H, imm */
        IR2_OPND tmp0 = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd2i(LISA_SRLI_D, &tmp0, &reg0, 8);
        LATXS_FLAG_REDUCTION_CHECKIF(pir1)
        latxs_append_ir2_opnd2(LISA_X86OR_B,    &tmp0, &tmp1);
        latxs_append_ir2_opnd3(LISA_OR, &tmp0,  &tmp0, &tmp1);
        latxs_append_ir2_opnd2ii(LISA_BSTRINS_D, &reg0, &tmp0, 15, 8);
    } else {
        /* xor reg8/16/32/64, imm */
        int os = ir1_opnd_size(opnd0); /* 8, 16, 32, 64 */
        if (os == 64) {
            LATXS_FLAG_REDUCTION_CHECKIF(pir1)
            latxs_append_ir2_opnd2(LISA_X86OR_D,   &reg0, &tmp1);
            latxs_append_ir2_opnd3(LISA_OR, &reg0, &reg0, &tmp1);
        } else {
            LATXS_FLAG_REDUCTION_CHECKIF(pir1)
            latxs_append_ir2_opnd2(LISA_X86OR_B + (os >> 4), &reg0, &tmp1);
            IR2_OPND tmp0 = latxs_ra_alloc_itemp();
            latxs_append_ir2_opnd3(LISA_OR, &tmp0, &reg0, &tmp1);
            latxs_store_ir2_to_ir1_gpr(&tmp0, opnd0);
        }
    }
}

static void __tr_byhand64_or_gpr_mem(IR1_INST *pir1)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    IR2_OPND reg0 = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd0));
    IR2_OPND tmp1 = latxs_ra_alloc_itemp();
    latxs_load_ir1_mem_to_ir2(&tmp1, opnd1, EXMode_Z, -1);

    if (ir1_opnd_is_8h(opnd0)) {
        /* xor 8H, mem */
        IR2_OPND tmp0 = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd2i(LISA_SRLI_D, &tmp0, &reg0, 8);
        LATXS_FLAG_REDUCTION_CHECKIF(pir1)
        latxs_append_ir2_opnd2(LISA_X86OR_B,    &tmp0, &tmp1);
        latxs_append_ir2_opnd3(LISA_OR, &tmp0,  &tmp0, &tmp1);
        latxs_append_ir2_opnd2ii(LISA_BSTRINS_D, &reg0, &tmp0, 15, 8);
    } else {
        /* xor reg8/16/32/64, mem */
        int os = ir1_opnd_size(opnd0); /* 8, 16, 32, 64 */
        if (os == 64) {
            LATXS_FLAG_REDUCTION_CHECKIF(pir1)
            latxs_append_ir2_opnd2(LISA_X86OR_D,   &reg0, &tmp1);
            latxs_append_ir2_opnd3(LISA_OR, &reg0, &reg0, &tmp1);
        } else {
            LATXS_FLAG_REDUCTION_CHECKIF(pir1)
            latxs_append_ir2_opnd2(LISA_X86OR_B + (os >> 4), &reg0, &tmp1);
            IR2_OPND tmp0 = latxs_ra_alloc_itemp();
            latxs_append_ir2_opnd3(LISA_OR, &tmp0, &reg0, &tmp1);
            latxs_store_ir2_to_ir1_gpr(&tmp0, opnd0);
        }
    }
}

static void __tr_byhand64_or_mem_gpr(IR1_INST *pir1)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    IR2_OPND tmp0 = latxs_ra_alloc_itemp();
    IR2_OPND mem_opnd;
    int mem_imm, opnd_size;
    opnd_size = ir1_opnd_size(opnd0);
    latxs_get_mem_opnd(&mem_opnd, &mem_imm, opnd0, -1);
    la_ld_by_op_size(tmp0, mem_opnd, mem_imm, opnd_size, EXMode_N);

    IR2_OPND reg1 = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd1));

    if (ir1_opnd_is_8h(opnd1)) {
        /* xor mem, 8H */
        IR2_OPND tmp1 = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd2i(LISA_SRLI_D, &tmp1, &reg1, 8);

        IR2_OPND dest = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd3(LISA_OR, &dest, &tmp0, &tmp1);
        la_st_by_op_size(dest, mem_opnd, mem_imm, opnd_size);
        latxs_ra_free_temp(&mem_opnd);

        LATXS_FLAG_REDUCTION_CHECKIF(pir1)
        latxs_append_ir2_opnd2(LISA_X86OR_B, &tmp0, &tmp1);
    } else {
        /* xor mem, reg8/16/32/64 */
        IR2_OPND dest = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd3(LISA_OR, &dest, &tmp0, &reg1);
        la_st_by_op_size(dest, mem_opnd, mem_imm, opnd_size);
        latxs_ra_free_temp(&mem_opnd);

        if (opnd_size == 64) {
            LATXS_FLAG_REDUCTION_CHECKIF(pir1)
            latxs_append_ir2_opnd2(LISA_X86OR_D, &tmp0, &reg1);
        } else {
            LATXS_FLAG_REDUCTION_CHECKIF(pir1)
            latxs_append_ir2_opnd2(LISA_X86OR_B + (opnd_size >> 4), &tmp0, &reg1);
        }
    }
}

static void __tr_byhand64_or_mem_imm(IR1_INST *pir1)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    IR2_OPND tmp0 = latxs_ra_alloc_itemp();
    /* latxs_load_ir1_mem_to_ir2(&tmp0, opnd0, EXMode_N, -1); */
    IR2_OPND mem_opnd;
    int mem_imm, opnd_size;
    opnd_size = ir1_opnd_size(opnd0);
    latxs_get_mem_opnd(&mem_opnd, &mem_imm, opnd0, -1);
    la_ld_by_op_size(tmp0, mem_opnd, mem_imm, opnd_size, EXMode_N);


    IR2_OPND tmp1 = latxs_ra_alloc_itemp();
    latxs_load_imm64_to_ir2(&tmp1, ir1_opnd_uimm(opnd1));

    IR2_OPND dest = latxs_ra_alloc_itemp();
    latxs_append_ir2_opnd3(LISA_OR, &dest, &tmp0, &tmp1);
    la_st_by_op_size(dest, mem_opnd, mem_imm, opnd_size);
    latxs_ra_free_temp(&mem_opnd);
    int os = ir1_opnd_size(opnd0); /* 8, 16, 32, 64 */
    if (os == 64) {
        LATXS_FLAG_REDUCTION_CHECKIF(pir1)
        latxs_append_ir2_opnd2(LISA_X86OR_D, &tmp0, &tmp1);
    } else {
        LATXS_FLAG_REDUCTION_CHECKIF(pir1)
        latxs_append_ir2_opnd2(LISA_X86OR_B + (os >> 4), &tmp0, &tmp1);
    }
}

bool latxs_translate_or_byhand64(IR1_INST *pir1)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    if (ir1_opnd_is_gpr(opnd0)) {
        if (ir1_opnd_is_gpr(opnd1)) {
            __tr_byhand64_or_gpr_gpr(pir1);
        } else if (ir1_opnd_is_imm(opnd1)) {
            __tr_byhand64_or_gpr_imm(pir1);
        } else {
            __tr_byhand64_or_gpr_mem(pir1);
        }
    } else {
        if (ir1_opnd_is_gpr(opnd1)) {
            __tr_byhand64_or_mem_gpr(pir1);
        } else {
            __tr_byhand64_or_mem_imm(pir1);
        }
    }

    return true;
}

static void __tr_byhand64_xor_8h_8h(IR1_INST *pir1,
        IR2_OPND *gpr0, IR2_OPND *gpr1)
{
    IR2_OPND tmp0 = latxs_ra_alloc_itemp();
    IR2_OPND tmp1 = latxs_ra_alloc_itemp();
    latxs_append_ir2_opnd2i(LISA_SRLI_D, &tmp0, gpr0, 8);
    latxs_append_ir2_opnd2i(LISA_SRLI_D, &tmp1, gpr1, 8);
    LATXS_FLAG_REDUCTION_CHECKIF(pir1)
    latxs_append_ir2_opnd2(LISA_X86XOR_B,   &tmp0, &tmp1);
    latxs_append_ir2_opnd3(LISA_XOR, &tmp0, &tmp0, &tmp1);
    latxs_append_ir2_opnd2ii(LISA_BSTRINS_D, gpr0, &tmp0, 15, 8);
}

static void __tr_byhand64_xor_8h_gpr(IR1_INST *pir1,
        IR2_OPND *gpr0, IR2_OPND *gpr1)
{
    IR2_OPND tmp0 = latxs_ra_alloc_itemp();
    latxs_append_ir2_opnd2i(LISA_SRLI_D, &tmp0, gpr0, 8);
    LATXS_FLAG_REDUCTION_CHECKIF(pir1)
    latxs_append_ir2_opnd2(LISA_X86XOR_B,   &tmp0, gpr1);
    latxs_append_ir2_opnd3(LISA_XOR, &tmp0, &tmp0, gpr1);
    latxs_append_ir2_opnd2ii(LISA_BSTRINS_D, gpr0, &tmp0, 15, 8);
}

static void __tr_byhand64_xor_gpr_8h(IR1_INST *pir1,
        IR2_OPND *gpr0, IR2_OPND *gpr1)
{
    IR2_OPND tmp1 = latxs_ra_alloc_itemp();
    latxs_append_ir2_opnd2i(LISA_SRLI_D, &tmp1, gpr1, 8);
    LATXS_FLAG_REDUCTION_CHECKIF(pir1)
    latxs_append_ir2_opnd2(LISA_X86XOR_B,   gpr0, &tmp1);
    latxs_append_ir2_opnd3(LISA_XOR, &tmp1, gpr0, &tmp1);
    latxs_store_ir2_to_ir1_gpr(&tmp1, ir1_get_opnd(pir1, 0));
}

static void __tr_byhand64_xor_gpr_gpr(IR1_INST *pir1)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);
    int r0 = ir1_opnd_base_reg_num(opnd0);
    int r1 = ir1_opnd_base_reg_num(opnd1);
    IR2_OPND reg0 = latxs_ra_alloc_gpr(r0);
    IR2_OPND reg1 = latxs_ra_alloc_gpr(r1);

    if (ir1_opnd_is_8h(opnd0)) {
        if (ir1_opnd_is_8h(opnd1)) {
            __tr_byhand64_xor_8h_8h(pir1, &reg0, &reg1); /* xor 8H, 8H */
        } else {
            __tr_byhand64_xor_8h_gpr(pir1, &reg0, &reg1); /* xor, 8H, reg8 */
        }
    } else {
        if (ir1_opnd_is_8h(opnd1)) {
            __tr_byhand64_xor_gpr_8h(pir1, &reg0, &reg1); /* xor reg8, 8H */
        } else {
            /* xor reg8/16/32/64, reg8/16/32/64 */
            int os = ir1_opnd_size(opnd0); /* 8, 16, 32, 64 */
            IR2_OPND *zero = &latxs_zero_ir2_opnd;
            if (os == 64) {
                if (r0 == r1) {
                    LATXS_FLAG_REDUCTION_CHECKIF(pir1)
                    latxs_append_ir2_opnd2(LISA_X86XOR_D, zero, zero);
                    latxs_append_ir2_opnd2_(lisa_mov, &reg0, zero);
                } else {
                    LATXS_FLAG_REDUCTION_CHECKIF(pir1)
                    latxs_append_ir2_opnd2(LISA_X86XOR_D, &reg0, &reg1);
                    latxs_append_ir2_opnd3(LISA_XOR, &reg0, &reg0, &reg1);
                }
            } else {
                if (os == 32 && r0 == r1) {
                    LATXS_FLAG_REDUCTION_CHECKIF(pir1)
                    latxs_append_ir2_opnd2(LISA_X86XOR_W, zero, zero);
                    latxs_append_ir2_opnd2_(lisa_mov, &reg0, zero);
                } else {
                    LATXS_FLAG_REDUCTION_CHECKIF(pir1)
                    latxs_append_ir2_opnd2(LISA_X86XOR_B + (os>>4), &reg0, &reg1);
                    IR2_OPND tmp0 = latxs_ra_alloc_itemp();
                    latxs_append_ir2_opnd3(LISA_XOR, &tmp0, &reg0, &reg1);
                    latxs_store_ir2_to_ir1_gpr(&tmp0, opnd0);
                }
            }
        }
    }
}

static void __tr_byhand64_xor_gpr_imm(IR1_INST *pir1)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    IR2_OPND reg0 = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd0));
    IR2_OPND tmp1 = latxs_ra_alloc_itemp();
    latxs_load_imm64_to_ir2(&tmp1, ir1_opnd_uimm(opnd1));

    if (ir1_opnd_is_8h(opnd0)) {
        /* xor 8H, imm */
        IR2_OPND tmp0 = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd2i(LISA_SRLI_D, &tmp0, &reg0, 8);
        LATXS_FLAG_REDUCTION_CHECKIF(pir1)
        latxs_append_ir2_opnd2(LISA_X86XOR_B,    &tmp0, &tmp1);
        latxs_append_ir2_opnd3(LISA_XOR, &tmp0,  &tmp0, &tmp1);
        latxs_append_ir2_opnd2ii(LISA_BSTRINS_D, &reg0, &tmp0, 15, 8);
    } else {
        /* xor reg8/16/32/64, imm */
        int os = ir1_opnd_size(opnd0); /* 8, 16, 32, 64 */
        if (os == 64) {
            LATXS_FLAG_REDUCTION_CHECKIF(pir1)
            latxs_append_ir2_opnd2(LISA_X86XOR_D,   &reg0, &tmp1);
            latxs_append_ir2_opnd3(LISA_XOR, &reg0, &reg0, &tmp1);
        } else {
            LATXS_FLAG_REDUCTION_CHECKIF(pir1)
            latxs_append_ir2_opnd2(LISA_X86XOR_B + (os >> 4), &reg0, &tmp1);
            IR2_OPND tmp0 = latxs_ra_alloc_itemp();
            latxs_append_ir2_opnd3(LISA_XOR, &tmp0, &reg0, &tmp1);
            latxs_store_ir2_to_ir1_gpr(&tmp0, opnd0);
        }
    }
}

static void __tr_byhand64_xor_gpr_mem(IR1_INST *pir1)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    IR2_OPND reg0 = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd0));
    IR2_OPND tmp1 = latxs_ra_alloc_itemp();
    latxs_load_ir1_mem_to_ir2(&tmp1, opnd1, EXMode_Z, -1);

    if (ir1_opnd_is_8h(opnd0)) {
        /* xor 8H, mem */
        IR2_OPND tmp0 = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd2i(LISA_SRLI_D, &tmp0, &reg0, 8);
        LATXS_FLAG_REDUCTION_CHECKIF(pir1)
        latxs_append_ir2_opnd2(LISA_X86XOR_B,    &tmp0, &tmp1);
        latxs_append_ir2_opnd3(LISA_XOR, &tmp0,  &tmp0, &tmp1);
        latxs_append_ir2_opnd2ii(LISA_BSTRINS_D, &reg0, &tmp0, 15, 8);
    } else {
        /* xor reg8/16/32/64, mem */
        int os = ir1_opnd_size(opnd0); /* 8, 16, 32, 64 */
        if (os == 64) {
            LATXS_FLAG_REDUCTION_CHECKIF(pir1)
            latxs_append_ir2_opnd2(LISA_X86XOR_D,   &reg0, &tmp1);
            latxs_append_ir2_opnd3(LISA_XOR, &reg0, &reg0, &tmp1);
        } else {
            LATXS_FLAG_REDUCTION_CHECKIF(pir1)
            latxs_append_ir2_opnd2(LISA_X86XOR_B + (os >> 4), &reg0, &tmp1);
            IR2_OPND tmp0 = latxs_ra_alloc_itemp();
            latxs_append_ir2_opnd3(LISA_XOR, &tmp0, &reg0, &tmp1);
            latxs_store_ir2_to_ir1_gpr(&tmp0, opnd0);
        }
    }
}

static void __tr_byhand64_xor_mem_gpr(IR1_INST *pir1)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    IR2_OPND tmp0 = latxs_ra_alloc_itemp();
    /* latxs_load_ir1_mem_to_ir2(&tmp0, opnd0, EXMode_N, -1); */
    IR2_OPND mem_opnd;
    int mem_imm, opnd_size;
    opnd_size = ir1_opnd_size(opnd0);
    latxs_get_mem_opnd(&mem_opnd, &mem_imm, opnd0, -1);
    la_ld_by_op_size(tmp0, mem_opnd, mem_imm, opnd_size, EXMode_N);

    IR2_OPND reg1 = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd1));

    if (ir1_opnd_is_8h(opnd1)) {
        /* xor mem, 8H */
        IR2_OPND tmp1 = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd2i(LISA_SRLI_D, &tmp1, &reg1, 8);

        IR2_OPND dest = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd3(LISA_XOR, &dest, &tmp0, &tmp1);
        la_st_by_op_size(dest, mem_opnd, mem_imm, opnd_size);
        latxs_ra_free_temp(&mem_opnd);

        LATXS_FLAG_REDUCTION_CHECKIF(pir1)
        latxs_append_ir2_opnd2(LISA_X86XOR_B, &tmp0, &tmp1);
    } else {
        /* xor mem, reg8/16/32/64 */
        IR2_OPND dest = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd3(LISA_XOR, &dest, &tmp0, &reg1);
        la_st_by_op_size(dest, mem_opnd, mem_imm, opnd_size);
        latxs_ra_free_temp(&mem_opnd);

        int os = ir1_opnd_size(opnd0); /* 8, 16, 32, 64 */
        if (os == 64) {
            LATXS_FLAG_REDUCTION_CHECKIF(pir1)
            latxs_append_ir2_opnd2(LISA_X86XOR_D, &tmp0, &reg1);
        } else {
            LATXS_FLAG_REDUCTION_CHECKIF(pir1)
            latxs_append_ir2_opnd2(LISA_X86XOR_B + (os >> 4), &tmp0, &reg1);
        }
    }
}

static void __tr_byhand64_xor_mem_imm(IR1_INST *pir1)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    IR2_OPND tmp0 = latxs_ra_alloc_itemp();
    /* latxs_load_ir1_mem_to_ir2(&tmp0, opnd0, EXMode_N, -1); */
    IR2_OPND mem_opnd;
    int mem_imm, opnd_size;
    opnd_size = ir1_opnd_size(opnd0);
    latxs_get_mem_opnd(&mem_opnd, &mem_imm, opnd0, -1);
    la_ld_by_op_size(tmp0, mem_opnd, mem_imm, opnd_size, EXMode_N);


    IR2_OPND tmp1 = latxs_ra_alloc_itemp();
    latxs_load_imm64_to_ir2(&tmp1, ir1_opnd_uimm(opnd1));

    IR2_OPND dest = latxs_ra_alloc_itemp();
    latxs_append_ir2_opnd3(LISA_XOR, &dest, &tmp0, &tmp1);
    la_st_by_op_size(dest, mem_opnd, mem_imm, opnd_size);
    latxs_ra_free_temp(&mem_opnd);

    int os = ir1_opnd_size(opnd0); /* 8, 16, 32, 64 */
    if (os == 64) {
        LATXS_FLAG_REDUCTION_CHECKIF(pir1)
        latxs_append_ir2_opnd2(LISA_X86XOR_D, &tmp0, &tmp1);
    } else {
        LATXS_FLAG_REDUCTION_CHECKIF(pir1)
        latxs_append_ir2_opnd2(LISA_X86XOR_B + (os >> 4), &tmp0, &tmp1);
    }
}

bool latxs_translate_xor_byhand64(IR1_INST *pir1)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    if (ir1_opnd_is_gpr(opnd0)) {
        if (ir1_opnd_is_gpr(opnd1)) {
            __tr_byhand64_xor_gpr_gpr(pir1);
        } else if (ir1_opnd_is_imm(opnd1)) {
            __tr_byhand64_xor_gpr_imm(pir1);
        } else {
            __tr_byhand64_xor_gpr_mem(pir1);
        }
    } else {
        if (ir1_opnd_is_gpr(opnd1)) {
            __tr_byhand64_xor_mem_gpr(pir1);
        } else {
            __tr_byhand64_xor_mem_imm(pir1);
        }
    }

    return true;
}

static void __tr_byhand64_and_8h_8h(IR1_INST *pir1,
        IR2_OPND *gpr0, IR2_OPND *gpr1)
{
    IR2_OPND tmp0 = latxs_ra_alloc_itemp();
    IR2_OPND tmp1 = latxs_ra_alloc_itemp();
    latxs_append_ir2_opnd2i(LISA_SRLI_D, &tmp0, gpr0, 8);
    latxs_append_ir2_opnd2i(LISA_SRLI_D, &tmp1, gpr1, 8);
    LATXS_FLAG_REDUCTION_CHECKIF(pir1)
    latxs_append_ir2_opnd2(LISA_X86AND_B,   &tmp0, &tmp1);
    latxs_append_ir2_opnd3(LISA_AND, &tmp0, &tmp0, &tmp1);
    latxs_append_ir2_opnd2ii(LISA_BSTRINS_D, gpr0, &tmp0, 15, 8);
}

static void __tr_byhand64_and_8h_gpr(IR1_INST *pir1,
        IR2_OPND *gpr0, IR2_OPND *gpr1)
{
    IR2_OPND tmp0 = latxs_ra_alloc_itemp();
    latxs_append_ir2_opnd2i(LISA_SRLI_D, &tmp0, gpr0, 8);
    LATXS_FLAG_REDUCTION_CHECKIF(pir1)
    latxs_append_ir2_opnd2(LISA_X86AND_B,   &tmp0, gpr1);
    latxs_append_ir2_opnd3(LISA_AND, &tmp0, &tmp0, gpr1);
    latxs_append_ir2_opnd2ii(LISA_BSTRINS_D, gpr0, &tmp0, 15, 8);
}

static void __tr_byhand64_and_gpr_8h(IR1_INST *pir1,
        IR2_OPND *gpr0, IR2_OPND *gpr1)
{
    IR2_OPND tmp1 = latxs_ra_alloc_itemp();
    latxs_append_ir2_opnd2i(LISA_SRLI_D, &tmp1, gpr1, 8);
    LATXS_FLAG_REDUCTION_CHECKIF(pir1)
    latxs_append_ir2_opnd2(LISA_X86AND_B,   gpr0, &tmp1);
    latxs_append_ir2_opnd3(LISA_AND, &tmp1, gpr0, &tmp1);
    latxs_store_ir2_to_ir1_gpr(&tmp1, ir1_get_opnd(pir1, 0));
}

static bool __tr_byhand64_and_gpr_gpr(IR1_INST *pir1)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);
    IR2_OPND reg0 = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd0));
    IR2_OPND reg1 = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd1));

    if (ir1_opnd_is_8h(opnd0)) {
        if (ir1_opnd_is_8h(opnd1)) {
            __tr_byhand64_and_8h_8h(pir1, &reg0, &reg1); /* xor 8H, 8H */
        } else {
            __tr_byhand64_and_8h_gpr(pir1, &reg0, &reg1); /* xor, 8H, reg8 */
        }
    } else {
        if (ir1_opnd_is_8h(opnd1)) {
            __tr_byhand64_and_gpr_8h(pir1, &reg0, &reg1); /* xor reg8, 8H */
        } else {
            /* xor reg8/16/32/64, reg8/16/32/64 */
            int os = ir1_opnd_size(opnd0); /* 8, 16, 32, 64 */
            if (os == 64) {
                latxs_append_ir2_opnd2(LISA_X86AND_B + 3, &reg0, &reg1);
                latxs_append_ir2_opnd3(LISA_AND, &reg0, &reg0, &reg1);
            } else {
                LATXS_FLAG_REDUCTION_CHECKIF(pir1)
                latxs_append_ir2_opnd2(LISA_X86AND_B + (os >> 4), &reg0, &reg1);
                IR2_OPND tmp0 = latxs_ra_alloc_itemp();
                latxs_append_ir2_opnd3(LISA_AND, &tmp0, &reg0, &reg1);
                latxs_store_ir2_to_ir1_gpr(&tmp0, opnd0);
            }
        }
    }

    return true;
}

static bool __tr_byhand64_and_gpr_imm(IR1_INST *pir1)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    IR2_OPND reg0 = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd0));
    IR2_OPND tmp1 = latxs_ra_alloc_itemp();
    latxs_load_imm64_to_ir2(&tmp1, ir1_opnd_uimm(opnd1));

    if (ir1_opnd_is_8h(opnd0)) {
        /* xor 8H, imm */
        IR2_OPND tmp0 = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd2i(LISA_SRLI_D, &tmp0, &reg0, 8);
        LATXS_FLAG_REDUCTION_CHECKIF(pir1)
        latxs_append_ir2_opnd2(LISA_X86AND_B,    &tmp0, &tmp1);
        latxs_append_ir2_opnd3(LISA_AND, &tmp0,  &tmp0, &tmp1);
        latxs_append_ir2_opnd2ii(LISA_BSTRINS_D, &reg0, &tmp0, 15, 8);
    } else {
        /* xor reg8/16/32/64, imm */
        int os = ir1_opnd_size(opnd0); /* 8, 16, 32, 64 */
        if (os == 64) {
            LATXS_FLAG_REDUCTION_CHECKIF(pir1)
            latxs_append_ir2_opnd2(LISA_X86AND_B + 3, &reg0, &tmp1);
            latxs_append_ir2_opnd3(LISA_AND, &reg0,   &reg0, &tmp1);
        } else {
            LATXS_FLAG_REDUCTION_CHECKIF(pir1)
            latxs_append_ir2_opnd2(LISA_X86AND_B + (os >> 4), &reg0, &tmp1);
            IR2_OPND tmp0 = latxs_ra_alloc_itemp();
            latxs_append_ir2_opnd3(LISA_AND, &tmp0, &reg0, &tmp1);
            latxs_store_ir2_to_ir1_gpr(&tmp0, opnd0);
        }
    }

    return true;
}

static bool __tr_byhand64_and_gpr_mem(IR1_INST *pir1)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    IR2_OPND reg0 = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd0));
    IR2_OPND tmp1 = latxs_ra_alloc_itemp();
    latxs_load_ir1_mem_to_ir2(&tmp1, opnd1, EXMode_Z, -1);

    if (ir1_opnd_is_8h(opnd0)) {
        /* xor 8H, mem */
        IR2_OPND tmp0 = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd2i(LISA_SRLI_D, &tmp0, &reg0, 8);
        LATXS_FLAG_REDUCTION_CHECKIF(pir1)
        latxs_append_ir2_opnd2(LISA_X86AND_B,    &tmp0, &tmp1);
        latxs_append_ir2_opnd3(LISA_AND, &tmp0,  &tmp0, &tmp1);
        latxs_append_ir2_opnd2ii(LISA_BSTRINS_D, &reg0, &tmp0, 15, 8);
    } else {
        /* xor reg8/16/32/64, mem */
        int os = ir1_opnd_size(opnd0); /* 8, 16, 32, 64 */
        if (os == 64) {
            LATXS_FLAG_REDUCTION_CHECKIF(pir1)
            latxs_append_ir2_opnd2(LISA_X86AND_D,   &reg0, &tmp1);
            latxs_append_ir2_opnd3(LISA_AND, &reg0, &reg0, &tmp1);
        } else {
            LATXS_FLAG_REDUCTION_CHECKIF(pir1)
            latxs_append_ir2_opnd2(LISA_X86AND_B + (os >> 4), &reg0, &tmp1);
            IR2_OPND tmp0 = latxs_ra_alloc_itemp();
            latxs_append_ir2_opnd3(LISA_AND, &tmp0, &reg0, &tmp1);
            latxs_store_ir2_to_ir1_gpr(&tmp0, opnd0);
        }
    }

    return true;
}

static bool __tr_byhand64_and_mem_gpr(IR1_INST *pir1)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    IR2_OPND tmp0 = latxs_ra_alloc_itemp();
    /* latxs_load_ir1_mem_to_ir2(&tmp0, opnd0, EXMode_N, -1); */
    IR2_OPND mem_opnd;
    int mem_imm, opnd_size;
    opnd_size = ir1_opnd_size(opnd0);
    latxs_get_mem_opnd(&mem_opnd, &mem_imm, opnd0, -1);
    la_ld_by_op_size(tmp0, mem_opnd, mem_imm, opnd_size, EXMode_N);

    IR2_OPND reg1 = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd1));

    if (ir1_opnd_is_8h(opnd1)) {
        /* xor mem, 8H */
        IR2_OPND tmp1 = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd2i(LISA_SRLI_D, &tmp1, &reg1, 8);

        IR2_OPND dest = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd3(LISA_AND, &dest, &tmp0, &tmp1);
        la_st_by_op_size(dest, mem_opnd, mem_imm, opnd_size);
        latxs_ra_free_temp(&mem_opnd);

        LATXS_FLAG_REDUCTION_CHECKIF(pir1)
        latxs_append_ir2_opnd2(LISA_X86AND_B, &tmp0, &tmp1);
    } else {
        /* xor mem, reg8/16/32/64 */
        IR2_OPND dest = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd3(LISA_AND, &dest, &tmp0, &reg1);
        la_st_by_op_size(dest, mem_opnd, mem_imm, opnd_size);
        latxs_ra_free_temp(&mem_opnd);

        int os = ir1_opnd_size(opnd0); /* 8, 16, 32, 64 */
        if (os == 64) {
            LATXS_FLAG_REDUCTION_CHECKIF(pir1)
            latxs_append_ir2_opnd2(LISA_X86AND_D, &tmp0, &reg1);
        } else {
            LATXS_FLAG_REDUCTION_CHECKIF(pir1)
            latxs_append_ir2_opnd2(LISA_X86AND_B + (os >> 4), &tmp0, &reg1);
        }
    }

    return true;
}

static bool __tr_byhand64_and_mem_imm(IR1_INST *pir1)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    IR2_OPND tmp0 = latxs_ra_alloc_itemp();
    /* latxs_load_ir1_mem_to_ir2(&tmp0, opnd0, EXMode_N, -1); */
    IR2_OPND mem_opnd;
    int mem_imm, opnd_size;
    opnd_size = ir1_opnd_size(opnd0);
    latxs_get_mem_opnd(&mem_opnd, &mem_imm, opnd0, -1);
    la_ld_by_op_size(tmp0, mem_opnd, mem_imm, opnd_size, EXMode_N);


    IR2_OPND tmp1 = latxs_ra_alloc_itemp();
    latxs_load_imm64_to_ir2(&tmp1, ir1_opnd_uimm(opnd1));

    IR2_OPND dest = latxs_ra_alloc_itemp();
    latxs_append_ir2_opnd3(LISA_AND, &dest, &tmp0, &tmp1);
    la_st_by_op_size(dest, mem_opnd, mem_imm, opnd_size);
    latxs_ra_free_temp(&mem_opnd);

    int os = ir1_opnd_size(opnd0); /* 8, 16, 32, 64 */
    if (os == 64) {
        LATXS_FLAG_REDUCTION_CHECKIF(pir1)
        latxs_append_ir2_opnd2(LISA_X86AND_D, &tmp0, &tmp1);
    } else {
        LATXS_FLAG_REDUCTION_CHECKIF(pir1)
        latxs_append_ir2_opnd2(LISA_X86AND_B + (os >> 4), &tmp0, &tmp1);
    }

    return true;
}

bool latxs_translate_and_byhand64(IR1_INST *pir1)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    bool res = false;

    if (ir1_opnd_is_gpr(opnd0)) {
        if (ir1_opnd_is_gpr(opnd1)) {
            res = __tr_byhand64_and_gpr_gpr(pir1);
        } else if (ir1_opnd_is_imm(opnd1)) {
            res = __tr_byhand64_and_gpr_imm(pir1);
        } else {
            res = __tr_byhand64_and_gpr_mem(pir1);
        }
    } else {
        if (ir1_opnd_is_gpr(opnd1)) {
            res = __tr_byhand64_and_mem_gpr(pir1);
        } else {
            res = __tr_byhand64_and_mem_imm(pir1);
        }
    }

    return res;
}

static void _tr_byhand64_cmp_gpr_gpr(IR1_OPND *opnd0, IR1_OPND *opnd1)
{
    IR2_OPND gpr0 = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd0));
    IR2_OPND gpr1 = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd1));

    if (ir1_opnd_is_8h(opnd0)) {
        IR2_OPND tmp0 = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd2i(LISA_SRLI_D, &tmp0, &gpr0, 8);
        if (ir1_opnd_is_8h(opnd1)) {
            IR2_OPND tmp1 = latxs_ra_alloc_itemp();
            latxs_append_ir2_opnd2i(LISA_SRLI_D,  &tmp1, &gpr1, 8);
            latxs_append_ir2_opnd2(LISA_X86SUB_B, &tmp0, &tmp1);
        } else {
            latxs_append_ir2_opnd2(LISA_X86SUB_B, &tmp0, &gpr1);
        }
    } else {
        if (ir1_opnd_is_8h(opnd1)) {
            IR2_OPND tmp1 = latxs_ra_alloc_itemp();
            latxs_append_ir2_opnd2i(LISA_SRLI_D,  &tmp1, &gpr1, 8);
            latxs_append_ir2_opnd2(LISA_X86SUB_B, &gpr0, &tmp1);
        } else {
            int os = ir1_opnd_size(opnd0); /* 8, 16, 32, 64 */
            latxs_append_ir2_opnd2(LISA_X86SUB_B + (os >> 4) - (os >> 6),
                    &gpr0, &gpr1);
        }
    }
}

static void _tr_byhand64_cmp_gpr_imm(IR1_OPND *opnd0, IR1_OPND *opnd1)
{
    IR2_OPND gpr0 = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd0));

    IR2_OPND reg1;
    if (ir1_opnd_is_imm(opnd1) && ir1_opnd_simm(opnd1) == 0) {
        reg1 = latxs_zero_ir2_opnd;
    } else {
        reg1 = latxs_ra_alloc_itemp();
        latxs_load_ir1_imm_to_ir2(&reg1, opnd1, EXMode_S);
    }

    if (ir1_opnd_is_8h(opnd0)) {
        IR2_OPND tmp0 = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd2i(LISA_SRLI_D,  &tmp0, &gpr0, 8);
        latxs_append_ir2_opnd2(LISA_X86SUB_B, &tmp0, &reg1);
    } else {
        int os = ir1_opnd_size(opnd0); /* 8, 16, 32, 64 */
        latxs_append_ir2_opnd2(LISA_X86SUB_B + (os >> 4) - (os >> 6),
                &gpr0, &reg1);
    }
}

static void _tr_byhand64_cmp_gpr_mem(IR1_OPND *opnd0, IR1_OPND *opnd1)
{
    IR2_OPND gpr0 = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd0));

    IR2_OPND tmp1 = latxs_ra_alloc_itemp();
    latxs_load_ir1_mem_to_ir2(&tmp1, opnd1, EXMode_N, -1);

    if (ir1_opnd_is_8h(opnd0)) {
        IR2_OPND tmp0 = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd2i(LISA_SRLI_D,  &tmp0, &gpr0, 8);
        latxs_append_ir2_opnd2(LISA_X86SUB_B, &tmp0, &tmp1);
    } else {
        int os = ir1_opnd_size(opnd0); /* 8, 16, 32, 64 */
        latxs_append_ir2_opnd2(LISA_X86SUB_B + (os >> 4) - (os >> 6),
                &gpr0, &tmp1);
    }
}

static void _tr_byhand64_cmp_mem_gpr(IR1_OPND *opnd0, IR1_OPND *opnd1)
{
    IR2_OPND tmp0 = latxs_ra_alloc_itemp();
    latxs_load_ir1_mem_to_ir2(&tmp0, opnd0, EXMode_N, -1);

    IR2_OPND gpr1 = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd1));

    if (ir1_opnd_is_8h(opnd1)) {
        IR2_OPND tmp1 = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd2i(LISA_SRLI_D, &tmp1, &gpr1, 8);
        latxs_append_ir2_opnd2(LISA_X86SUB_B, &tmp0, &tmp1);
    } else {
        int os = ir1_opnd_size(opnd0); /* 8, 16, 32, 64 */
        latxs_append_ir2_opnd2(LISA_X86SUB_B + (os >> 4) - (os >> 6),
                &tmp0, &gpr1);
    }
}

static void _tr_byhand64_cmp_mem_imm(IR1_OPND *opnd0, IR1_OPND *opnd1)
{
    IR2_OPND tmp0 = latxs_ra_alloc_itemp();
    latxs_load_ir1_mem_to_ir2(&tmp0, opnd0, EXMode_N, -1);

    IR2_OPND reg1;
    if (ir1_opnd_is_imm(opnd1) && ir1_opnd_simm(opnd1) == 0) {
        reg1 = latxs_zero_ir2_opnd;
    } else {
        reg1 = latxs_ra_alloc_itemp();
        latxs_load_ir1_imm_to_ir2(&reg1, opnd1, EXMode_S);
    }

    int os = ir1_opnd_size(opnd0); /* 8, 16, 32, 64 */
    latxs_append_ir2_opnd2(LISA_X86SUB_B + (os >> 4) - (os >> 6),
            &tmp0, &reg1);
}

bool latxs_translate_cmp_byhand64(IR1_INST *pir1)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    if (ir1_opnd_is_gpr(opnd0)) {
        if (ir1_opnd_is_gpr(opnd1)) {
            _tr_byhand64_cmp_gpr_gpr(opnd0, opnd1);
        } else if (ir1_opnd_is_imm(opnd1)) {
            _tr_byhand64_cmp_gpr_imm(opnd0, opnd1);
        } else {
            _tr_byhand64_cmp_gpr_mem(opnd0, opnd1);
        }
    } else {
        if (ir1_opnd_is_gpr(opnd1)) {
            _tr_byhand64_cmp_mem_gpr(opnd0, opnd1);
        } else {
            _tr_byhand64_cmp_mem_imm(opnd0, opnd1);
        }
    }

    return true;
}
static void __tr_byhand64_test_gpr_gpr(IR1_OPND *opnd0, IR1_OPND *opnd1)
{
    IR2_OPND gpr0 = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd0));
    IR2_OPND gpr1 = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd1));

    if (ir1_opnd_is_8h(opnd0)) {
        IR2_OPND tmp0 = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd2i(LISA_SRLI_D, &tmp0, &gpr0, 8);
        if (ir1_opnd_is_8h(opnd1)) {
            IR2_OPND tmp1 = latxs_ra_alloc_itemp();
            latxs_append_ir2_opnd2i(LISA_SRLI_D, &tmp1, &gpr1, 8);
            latxs_append_ir2_opnd2(LISA_X86AND_B, &tmp0, &tmp1);
        } else {
            latxs_append_ir2_opnd2(LISA_X86AND_B, &tmp0, &gpr1);
        }
    } else {
        if (ir1_opnd_is_8h(opnd1)) {
            IR2_OPND tmp1 = latxs_ra_alloc_itemp();
            latxs_append_ir2_opnd2i(LISA_SRLI_D, &tmp1, &gpr1, 8);
            latxs_append_ir2_opnd2(LISA_X86AND_B, &gpr0, &tmp1);
        } else {
            int os = ir1_opnd_size(opnd0); /* 8, 16, 32, 64 */
            latxs_append_ir2_opnd2(LISA_X86AND_B + (os >> 4) - (os >> 6),
                    &gpr0, &gpr1);
        }
    }
}

static void __tr_byhand64_test_gpr_imm(IR1_OPND *opnd0, IR1_OPND *opnd1)
{
    IR2_OPND gpr0 = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd0));

    IR2_OPND tmp1 = latxs_ra_alloc_itemp();
    latxs_load_ir1_imm_to_ir2(&tmp1, opnd1, EXMode_N);

    if (ir1_opnd_is_8h(opnd0)) {
        IR2_OPND tmp0 = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd2i(LISA_SRLI_D, &tmp0, &gpr0, 8);
        latxs_append_ir2_opnd2(LISA_X86AND_B, &tmp0, &tmp1);
    } else {
        int os = ir1_opnd_size(opnd0); /* 8, 16, 32, 64 */
        latxs_append_ir2_opnd2(LISA_X86AND_B + (os >> 4) - (os >> 6),
                &gpr0, &tmp1);
    }
}

static void __tr_byhand64_test_mem_gpr(IR1_OPND *opnd0, IR1_OPND *opnd1)
{
    IR2_OPND tmp0 = latxs_ra_alloc_itemp();
    latxs_load_ir1_mem_to_ir2(&tmp0, opnd0, EXMode_N, -1);

    IR2_OPND gpr1 = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd1));

    if (ir1_opnd_is_8h(opnd1)) {
        IR2_OPND tmp1 = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd2i(LISA_SRLI_D, &tmp1, &gpr1, 8);
        latxs_append_ir2_opnd2(LISA_X86AND_B, &tmp0, &tmp1);
    } else {
        int os = ir1_opnd_size(opnd0); /* 8, 16, 32, 64 */
        latxs_append_ir2_opnd2(LISA_X86AND_B + (os >> 4) - (os >> 6),
                &tmp0, &gpr1);
    }
}

static void __tr_byhand64_test_mem_imm(IR1_OPND *opnd0, IR1_OPND *opnd1)
{
    IR2_OPND tmp0 = latxs_ra_alloc_itemp();
    latxs_load_ir1_mem_to_ir2(&tmp0, opnd0, EXMode_N, -1);

    IR2_OPND tmp1 = latxs_ra_alloc_itemp();
    latxs_load_ir1_imm_to_ir2(&tmp1, opnd1, EXMode_N);

    int os = ir1_opnd_size(opnd0); /* 8, 16, 32, 64 */
    latxs_append_ir2_opnd2(LISA_X86AND_B + (os >> 4) - (os >> 6),
            &tmp0, &tmp1);
}

bool latxs_translate_test_byhand64(IR1_INST *pir1)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    if (ir1_opnd_is_gpr(opnd0)) {
        if (ir1_opnd_is_gpr(opnd1)) {
            __tr_byhand64_test_gpr_gpr(opnd0, opnd1);
        } else {
            lsassertm(ir1_opnd_is_imm(opnd1), "[trbh64] test opnd1 not imm.\n");
            __tr_byhand64_test_gpr_imm(opnd0, opnd1);
        }
    } else {
        if (ir1_opnd_is_gpr(opnd1)) {
            __tr_byhand64_test_mem_gpr(opnd0, opnd1);
        } else {
            lsassertm(ir1_opnd_is_imm(opnd1), "[trbh64] test opnd1 not imm.\n");
            __tr_byhand64_test_mem_imm(opnd0, opnd1);
        }
    }

    return true;
}

static void __tr_byhand64_addsub_gpr_gpr(IR1_INST *pir1, int is_add)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);
    int r0 = ir1_opnd_base_reg_num(opnd0);
    int r1 = ir1_opnd_base_reg_num(opnd1);
    IR2_OPND gpr0 = latxs_ra_alloc_gpr(r0);
    IR2_OPND gpr1 = latxs_ra_alloc_gpr(r1);

    IR2_OPCODE cal = is_add ? LISA_ADD_W : LISA_SUB_W;
    IR2_OPCODE lbt = is_add ? LISA_X86ADD_B : LISA_X86SUB_B;

    int os = ir1_opnd_size(opnd0); /* 8, 16, 32, 64 */
    if (os == 64) {
        LATXS_FLAG_REDUCTION_CHECKIF(pir1)
        latxs_append_ir2_opnd2(lbt + 3,        &gpr0, &gpr1);
        latxs_append_ir2_opnd3(cal + 1, &gpr0, &gpr0, &gpr1);
    } else if (os == 32) {
        LATXS_FLAG_REDUCTION_CHECKIF(pir1)
        latxs_append_ir2_opnd2(lbt + 2,    &gpr0, &gpr1);
        latxs_append_ir2_opnd3(cal, &gpr0, &gpr0, &gpr1);
        latxs_store_ir2_to_ir1_gpr(&gpr0, opnd0);
    } else {
        IR2_OPND t0 = latxs_ra_alloc_itemp();
        IR2_OPND t1 = latxs_ra_alloc_itemp();
        latxs_load_ir1_gpr_to_ir2(&t0, opnd0, EXMode_S);
        latxs_load_ir1_gpr_to_ir2(&t1, opnd1, EXMode_S);
        LATXS_FLAG_REDUCTION_CHECKIF(pir1)
        latxs_append_ir2_opnd2(lbt + (os >> 4), &t0, &t1);
        latxs_append_ir2_opnd3(cal, &t0, &t0, &t1);
        latxs_store_ir2_to_ir1_gpr(&t0, opnd0);
    }
}

static void __tr_byhand64_addsub_gpr_imm(IR1_INST *pir1, int is_add)
{
    IR2_OPND *zero = &latxs_zero_ir2_opnd;

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    int r0 = ir1_opnd_base_reg_num(opnd0);
    IR2_OPND gpr0 = latxs_ra_alloc_gpr(r0);

    IR2_OPND i = latxs_ra_alloc_itemp();
    int64_t imm = ir1_opnd_simm(opnd1);
    int64_t caimm = is_add ? imm : -imm;
    bool imm12 = int32_in_int12(imm) && int32_in_int12(caimm);
    IR2_OPCODE cai = LISA_ADDI_W;
    IR2_OPCODE cal = is_add ? LISA_ADD_W : LISA_SUB_W;
    IR2_OPCODE lbt = is_add ? LISA_X86ADD_B : LISA_X86SUB_B;

    int os = ir1_opnd_size(opnd0); /* 8, 16, 32, 64 */
    if (os == 64) {
        if (imm12) {
            LATXS_FLAG_REDUCTION_CHECKIF(pir1) {
                latxs_append_ir2_opnd2i(LISA_ADDI_D, &i, zero, imm);
                latxs_append_ir2_opnd2(lbt + 3, &gpr0, &i);
            }
            latxs_append_ir2_opnd2i(cai + 1, &gpr0, &gpr0, caimm);
        } else {
            latxs_load_imm64_to_ir2(&i, imm);
            LATXS_FLAG_REDUCTION_CHECKIF(pir1) 
            latxs_append_ir2_opnd2(lbt + 3,        &gpr0, &i);
            latxs_append_ir2_opnd3(cal + 1, &gpr0, &gpr0, &i);
        }
    } else if (os == 32) {
        if (imm12) {
            LATXS_FLAG_REDUCTION_CHECKIF(pir1) {
                latxs_append_ir2_opnd2i(LISA_ADDI_D, &i, zero, imm);
                latxs_append_ir2_opnd2(lbt + 2, &gpr0, &i);
            }
            latxs_append_ir2_opnd2i(cai, &gpr0, &gpr0, caimm);
            latxs_store_ir2_to_ir1_gpr(&gpr0, opnd0);
        } else {
            latxs_load_imm64_to_ir2(&i, imm);
            LATXS_FLAG_REDUCTION_CHECKIF(pir1) 
            latxs_append_ir2_opnd2(lbt + 2,    &gpr0, &i);
            latxs_append_ir2_opnd3(cal, &gpr0, &gpr0, &i);
            latxs_store_ir2_to_ir1_gpr(&gpr0, opnd0);
        }
    } else {
        IR2_OPND t = latxs_ra_alloc_itemp();
        latxs_load_ir1_gpr_to_ir2(&t, opnd0, EXMode_S);
        if (imm12) {
            LATXS_FLAG_REDUCTION_CHECKIF(pir1) {
                latxs_append_ir2_opnd2i(LISA_ADDI_D, &i, zero, imm);
                latxs_append_ir2_opnd2(lbt + (os >> 4), &t, &i);
            }
            latxs_append_ir2_opnd2i(cai, &t, &t, caimm);
            latxs_store_ir2_to_ir1_gpr(&t, opnd0);
        } else {
            latxs_load_imm64_to_ir2(&i, imm);
            LATXS_FLAG_REDUCTION_CHECKIF(pir1) 
            latxs_append_ir2_opnd2(lbt + (os >> 4), &t, &i);
            latxs_append_ir2_opnd3(cal, &t, &t, &i);
            latxs_store_ir2_to_ir1_gpr(&t, opnd0);
        }
    }
}

static void __tr_byhand64_addsub_gpr_mem(IR1_INST *pir1, int is_add)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    int r0 = ir1_opnd_base_reg_num(opnd0);
    IR2_OPND gpr0 = latxs_ra_alloc_gpr(r0);

    IR2_OPND tmp1 = latxs_ra_alloc_itemp();
    latxs_load_ir1_mem_to_ir2(&tmp1, opnd1, EXMode_S, -1);

    IR2_OPCODE cal = is_add ? LISA_ADD_W : LISA_SUB_W;
    IR2_OPCODE lbt = is_add ? LISA_X86ADD_B : LISA_X86SUB_B;

    int os = ir1_opnd_size(opnd0); /* 8, 16, 32, 64 */
    if (os == 64) {
        LATXS_FLAG_REDUCTION_CHECKIF(pir1)
        latxs_append_ir2_opnd2(lbt + 3,        &gpr0, &tmp1);
        latxs_append_ir2_opnd3(cal + 1, &gpr0, &gpr0, &tmp1);
    } else if (os == 32) {
        LATXS_FLAG_REDUCTION_CHECKIF(pir1)
        latxs_append_ir2_opnd2(lbt + 2,    &gpr0, &tmp1);
        latxs_append_ir2_opnd3(cal, &gpr0, &gpr0, &tmp1);
        latxs_store_ir2_to_ir1_gpr(&gpr0, opnd0);
    } else {
        IR2_OPND tmp0 = latxs_ra_alloc_itemp();
        latxs_load_ir1_gpr_to_ir2(&tmp0, opnd0, EXMode_S);
        LATXS_FLAG_REDUCTION_CHECKIF(pir1)
        latxs_append_ir2_opnd2(lbt + (os >> 4), &tmp0, &tmp1);
        latxs_append_ir2_opnd3(cal, &tmp0, &tmp0, &tmp1);
        latxs_store_ir2_to_ir1_gpr(&tmp0, opnd0);
    }
}

static void __tr_byhand64_addsub_mem_gpr(IR1_INST *pir1, int is_add)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    IR2_OPND tmp0 = latxs_ra_alloc_itemp();
    /* latxs_load_ir1_mem_to_ir2(&tmp0, opnd0, EXMode_S, -1); */
    IR2_OPND mem_opnd;
    int mem_imm, opnd_size;
    opnd_size = ir1_opnd_size(opnd0);
    latxs_get_mem_opnd(&mem_opnd, &mem_imm, opnd0, -1);
#ifdef CONFIG_SOFTMMU
    bool reuse = opnd_size == 64 &&
                 latxs_softmmu_rmw_reuse_eligible(pir1);
    IR2_OPND rmw_valid = latxs_invalid_ir2_opnd;
    if (reuse) {
        /* Translation-only diagnostic; it does not instrument the hot path. */
        if (getenv("LATS_SMMURMW_LOG")) {
            fprintf(stderr, "[LATX-SYS] smmurmw translated pc=%#llx\n",
                    (unsigned long long)pir1->info->address);
        }
        rmw_valid = latxs_ra_alloc_itemp();
        latxs_softmmu_rmw_load64(&tmp0, &mem_opnd, mem_imm, &rmw_valid);
    } else
#endif
    la_ld_by_op_size(tmp0, mem_opnd, mem_imm, opnd_size, EXMode_S);


    int r1 = ir1_opnd_base_reg_num(opnd1);
    IR2_OPND gpr1 = latxs_ra_alloc_gpr(r1);

    IR2_OPCODE cal = is_add ? LISA_ADD_W : LISA_SUB_W;
    IR2_OPCODE lbt = is_add ? LISA_X86ADD_B : LISA_X86SUB_B;

    IR2_OPND dest = latxs_ra_alloc_itemp();
    int os = ir1_opnd_size(opnd0); /* 8, 16, 32, 64 */
    if (os == 64) {
        latxs_append_ir2_opnd3(cal + 1, &dest, &tmp0, &gpr1);
    } else {
        IR2_OPND tmp1 = latxs_ra_alloc_itemp();
        latxs_load_ir1_gpr_to_ir2(&tmp1, opnd1, EXMode_S);
        latxs_append_ir2_opnd3(cal, &dest, &tmp0, &tmp1);
    }

#ifdef CONFIG_SOFTMMU
    if (reuse) {
        latxs_softmmu_rmw_store64(&dest, &mem_opnd, mem_imm, &rmw_valid);
        latxs_ra_free_temp(&rmw_valid);
    } else
#endif
    la_st_by_op_size(dest, mem_opnd, mem_imm, opnd_size);
    latxs_ra_free_temp(&mem_opnd);

    LATXS_FLAG_REDUCTION_CHECKIF(pir1) {
        if (os == 64) {
            latxs_append_ir2_opnd2(lbt + 3, &tmp0, &gpr1);
        } else {
            IR2_OPND tmp1 = latxs_ra_alloc_itemp();
            latxs_load_ir1_gpr_to_ir2(&tmp1, opnd1, EXMode_S);
            latxs_append_ir2_opnd2(lbt + (os >> 4), &tmp0, &tmp1);
        }
    }
}

static void __tr_byhand64_addsub_mem_imm(IR1_INST *pir1, int is_add)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    int mem_imm, opnd_size;
    opnd_size = ir1_opnd_size(opnd0);
    IR2_OPND src0 = latxs_ra_alloc_itemp();
    /* IR2_OPND mem_opnd= convert_mem(opnd0, &mem_imm); */
    IR2_OPND mem_opnd;
    latxs_get_mem_opnd(&mem_opnd, &mem_imm, opnd0, -1);
    la_ld_by_op_size(src0, mem_opnd, mem_imm, opnd_size, EXMode_S);

    IR2_OPND i = latxs_ra_alloc_itemp();
    int64_t imm = ir1_opnd_simm(opnd1);
    int64_t caimm = is_add ? imm : -imm;
    bool imm12 = int32_in_int12(imm) && int32_in_int12(caimm);
    IR2_OPCODE cai = LISA_ADDI_W;
    IR2_OPCODE cal = is_add ? LISA_ADD_W : LISA_SUB_W;
    IR2_OPCODE lbt = is_add ? LISA_X86ADD_B : LISA_X86SUB_B;

    IR2_OPND dest = latxs_ra_alloc_itemp();
    if (opnd_size == 64) {
        if (imm12) {
            latxs_append_ir2_opnd2i(cai + 1, &dest, &src0, caimm);
        } else {
            latxs_load_imm64_to_ir2(&i, imm);
            latxs_append_ir2_opnd3(cal + 1, &dest, &src0, &i);
        }
    } else {
        if (imm12) {
            latxs_append_ir2_opnd2i(cai, &dest, &src0, caimm);
        } else {
            latxs_load_imm64_to_ir2(&i, imm);
            latxs_append_ir2_opnd3(cal, &dest, &src0, &i);
        }
    }

    la_st_by_op_size(dest, mem_opnd, mem_imm, opnd_size);
    latxs_ra_free_temp(&mem_opnd);

    LATXS_FLAG_REDUCTION_CHECKIF(pir1) {
        if (imm12) latxs_load_imm64_to_ir2(&i, imm);
        if (opnd_size == 64) {
            latxs_append_ir2_opnd2(lbt + 3, &src0, &i);
        } else {
            latxs_append_ir2_opnd2(lbt + (opnd_size >> 4), &src0, &i);
        }
    }
}

bool latxs_translate_add_byhand64(IR1_INST *pir1)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    if (ir1_opnd_is_gpr(opnd0)) {
        if (ir1_opnd_is_gpr(opnd1)) {
            __tr_byhand64_addsub_gpr_gpr(pir1, 1);
        } else if (ir1_opnd_is_imm(opnd1)) {
            __tr_byhand64_addsub_gpr_imm(pir1, 1);
        } else {
            __tr_byhand64_addsub_gpr_mem(pir1, 1);
        }
    } else {
        if (ir1_opnd_is_gpr(opnd1)) {
            __tr_byhand64_addsub_mem_gpr(pir1, 1);
        } else {
            __tr_byhand64_addsub_mem_imm(pir1, 1);
        }
    }

    return true;
}

bool latxs_translate_sub_byhand64(IR1_INST *pir1)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    if (ir1_opnd_is_gpr(opnd0)) {
        if (ir1_opnd_is_gpr(opnd1)) {
            __tr_byhand64_addsub_gpr_gpr(pir1, 0);
        } else if (ir1_opnd_is_imm(opnd1)) {
            __tr_byhand64_addsub_gpr_imm(pir1, 0);
        } else {
            __tr_byhand64_addsub_gpr_mem(pir1, 0);
        }
    } else {
        if (ir1_opnd_is_gpr(opnd1)) {
            __tr_byhand64_addsub_mem_gpr(pir1, 0);
        } else {
            __tr_byhand64_addsub_mem_imm(pir1, 0);
        }
    }

    return true;
}

static void __tr_byhand64_adcsbb_gpr_gpr(IR1_INST *pir1, int is_adc)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    IR2_OPND gpr0, gpr1;
    if(ir1_opnd_is_8h(opnd0)) {
        gpr0 = latxs_ra_alloc_itemp();
        latxs_load_ir1_to_ir2(&gpr0, opnd0, EXMode_N);
    } else {
        int r0 = ir1_opnd_base_reg_num(opnd0);
        gpr0 = latxs_ra_alloc_gpr(r0);
    }

    if(ir1_opnd_is_8h(opnd1)) {
        gpr1 = latxs_ra_alloc_itemp();
        latxs_load_ir1_to_ir2(&gpr1, opnd1, EXMode_N);
    } else {
        int r1 = ir1_opnd_base_reg_num(opnd1);
        gpr1 = latxs_ra_alloc_gpr(r1);
    }

    IR2_OPCODE cal = is_adc ? LISA_ADC_B    : LISA_SBC_B;

    IR2_OPND res = latxs_ra_alloc_itemp();
    int os = ir1_opnd_size(opnd0); /* 8, 16, 32, 64 */
    if (os == 64) {
        latxs_append_ir2_opnd3(cal + 3, &res, &gpr0, &gpr1);
    } else {
        latxs_append_ir2_opnd3(cal + (os >> 4), &res, &gpr0, &gpr1);
    }

    LATXS_FLAG_REDUCTION_CHECKIF(pir1)
    latxs_generate_eflag_calculation(&res, &gpr0, &gpr1, pir1, true);

    latxs_store_ir2_to_ir1_gpr(&res, opnd0);
}

static void __tr_byhand64_adcsbb_gpr_imm(IR1_INST *pir1, int is_adc)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    IR2_OPND gpr0;
    if(ir1_opnd_is_8h(opnd0)) {
        gpr0 = latxs_ra_alloc_itemp();
        latxs_load_ir1_to_ir2(&gpr0, opnd0, EXMode_N);
    } else {
        int r0 = ir1_opnd_base_reg_num(opnd0);
        gpr0 = latxs_ra_alloc_gpr(r0);
    }

    IR2_OPND i = latxs_ra_alloc_itemp();
    int64_t imm = ir1_opnd_simm(opnd1);
    latxs_load_imm64_to_ir2(&i, imm);

    IR2_OPCODE cal = is_adc ? LISA_ADC_B    : LISA_SBC_B;

    IR2_OPND res = latxs_ra_alloc_itemp();
    int os = ir1_opnd_size(opnd0); /* 8, 16, 32, 64 */
    if (os == 64) {
        latxs_append_ir2_opnd3(cal + 3, &res, &gpr0, &i);
    } else {
        latxs_append_ir2_opnd3(cal + (os >> 4), &res, &gpr0, &i);
    }

    LATXS_FLAG_REDUCTION_CHECKIF(pir1)
    latxs_generate_eflag_calculation(&res, &gpr0, &i, pir1, true);
    latxs_ra_free_temp(&i);

    latxs_store_ir2_to_ir1_gpr(&res, opnd0);
}

static void __tr_byhand64_adcsbb_gpr_mem(IR1_INST *pir1, int is_adc)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    IR2_OPND gpr0;
    if(ir1_opnd_is_8h(opnd0)) {
        gpr0 = latxs_ra_alloc_itemp();
        latxs_load_ir1_to_ir2(&gpr0, opnd0, EXMode_N);
    } else {
        int r0 = ir1_opnd_base_reg_num(opnd0);
        gpr0 = latxs_ra_alloc_gpr(r0);
    }

    IR2_OPND tmp1 = latxs_ra_alloc_itemp();
    latxs_load_ir1_mem_to_ir2(&tmp1, opnd1, EXMode_S, -1);

    IR2_OPCODE cal = is_adc ? LISA_ADC_B    : LISA_SBC_B;

    IR2_OPND res = latxs_ra_alloc_itemp();
    int os = ir1_opnd_size(opnd0); /* 8, 16, 32, 64 */
    if (os == 64) {
        latxs_append_ir2_opnd3(cal + 3, &res, &gpr0, &tmp1);
    } else {
        latxs_append_ir2_opnd3(cal + (os >> 4), &res, &gpr0, &tmp1);
    }

    LATXS_FLAG_REDUCTION_CHECKIF(pir1)
    latxs_generate_eflag_calculation(&res, &gpr0, &tmp1, pir1, true);
    latxs_ra_free_temp(&tmp1);

    latxs_store_ir2_to_ir1_gpr(&res, opnd0);
}

static void __tr_byhand64_adcsbb_mem_gpr(IR1_INST *pir1, int is_adc)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    IR2_OPND tmp0 = latxs_ra_alloc_itemp();
    /* latxs_load_ir1_mem_to_ir2(&tmp0, opnd0, EXMode_S, -1); */
    IR2_OPND mem_opnd;
    int mem_imm, opnd_size;
    opnd_size = ir1_opnd_size(opnd0);
    latxs_get_mem_opnd(&mem_opnd, &mem_imm, opnd0, -1);
    la_ld_by_op_size(tmp0, mem_opnd, mem_imm, opnd_size, EXMode_S);


    IR2_OPND gpr1;
    if(ir1_opnd_is_8h(opnd1)) {
        gpr1 = latxs_ra_alloc_itemp();
        latxs_load_ir1_to_ir2(&gpr1, opnd1, EXMode_N);
    } else {
        int r1 = ir1_opnd_base_reg_num(opnd1);
        gpr1 = latxs_ra_alloc_gpr(r1);
	}

    IR2_OPCODE cal = is_adc ? LISA_ADC_B    : LISA_SBC_B;

    IR2_OPND res = latxs_ra_alloc_itemp();
    int os = ir1_opnd_size(opnd0); /* 8, 16, 32, 64 */
    if (os == 64) {
        latxs_append_ir2_opnd3(cal + 3, &res, &tmp0, &gpr1);
    } else {
        latxs_append_ir2_opnd3(cal + (os >> 4), &res, &tmp0, &gpr1);
    }

    LATXS_FLAG_REDUCTION_CHECKIF(pir1)
    latxs_generate_eflag_calculation(&res, &tmp0, &gpr1, pir1, true);
    latxs_ra_free_temp(&tmp0);

    la_st_by_op_size(res, mem_opnd, mem_imm, opnd_size);
    latxs_ra_free_temp(&mem_opnd);
}

static void __tr_byhand64_adcsbb_mem_imm(IR1_INST *pir1, int is_adc)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    IR2_OPND tmp0 = latxs_ra_alloc_itemp();
    /* latxs_load_ir1_mem_to_ir2(&tmp0, opnd0, EXMode_S, -1); */
    IR2_OPND mem_opnd;
    int mem_imm, opnd_size;
    opnd_size = ir1_opnd_size(opnd0);
    latxs_get_mem_opnd(&mem_opnd, &mem_imm, opnd0, -1);
    la_ld_by_op_size(tmp0, mem_opnd, mem_imm, opnd_size, EXMode_S);


    IR2_OPND i = latxs_ra_alloc_itemp();
    int64_t imm = ir1_opnd_simm(opnd1);
    latxs_load_imm64_to_ir2(&i, imm);

    IR2_OPCODE cal = is_adc ? LISA_ADC_B    : LISA_SBC_B;

    IR2_OPND res = latxs_ra_alloc_itemp();
    int os = ir1_opnd_size(opnd0); /* 8, 16, 32, 64 */
    if (os == 64) {
        latxs_append_ir2_opnd3(cal + 3, &res, &tmp0, &i);
    } else {
        latxs_append_ir2_opnd3(cal + (os >> 4), &res, &tmp0, &i);
    }

    LATXS_FLAG_REDUCTION_CHECKIF(pir1)
    latxs_generate_eflag_calculation(&res, &tmp0, &i, pir1, true);
    latxs_ra_free_temp(&i);
    latxs_ra_free_temp(&tmp0);

    la_st_by_op_size(res, mem_opnd, mem_imm, opnd_size);
    latxs_ra_free_temp(&mem_opnd);
}

bool latxs_translate_adc_byhand64(IR1_INST *pir1)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    if (ir1_opnd_is_gpr(opnd0)) {
        if (ir1_opnd_is_gpr(opnd1)) {
            __tr_byhand64_adcsbb_gpr_gpr(pir1, 1);
        } else if (ir1_opnd_is_imm(opnd1)) {
            __tr_byhand64_adcsbb_gpr_imm(pir1, 1);
        } else {
            __tr_byhand64_adcsbb_gpr_mem(pir1, 1);
        }
    } else {
        if (ir1_opnd_is_gpr(opnd1)) {
            __tr_byhand64_adcsbb_mem_gpr(pir1, 1);
        } else {
            __tr_byhand64_adcsbb_mem_imm(pir1, 1);
        }
    }

    return true;
}

bool latxs_translate_sbb_byhand64(IR1_INST *pir1)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    if (ir1_opnd_is_gpr(opnd0)) {
        if (ir1_opnd_is_gpr(opnd1)) {
            __tr_byhand64_adcsbb_gpr_gpr(pir1, 0);
        } else if (ir1_opnd_is_imm(opnd1)) {
            __tr_byhand64_adcsbb_gpr_imm(pir1, 0);
        } else {
            __tr_byhand64_adcsbb_gpr_mem(pir1, 0);
        }
    } else {
        if (ir1_opnd_is_gpr(opnd1)) {
            __tr_byhand64_adcsbb_mem_gpr(pir1, 0);
        } else {
            __tr_byhand64_adcsbb_mem_imm(pir1, 0);
        }
    }

    return true;
}

bool latxs_translate_not_byhand64(IR1_INST *pir1)
{
    IR2_OPND *zero = &latxs_zero_ir2_opnd;
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    int os = ir1_opnd_size(opnd0);

    if (ir1_opnd_is_gpr(opnd0)) {
        /* not r8/r16/r32/r65 */
        IR2_OPND gpr = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd0));
        if (ir1_opnd_is_8h(opnd0)) {
            IR2_OPND tmp = latxs_ra_alloc_itemp();
            latxs_append_ir2_opnd2i(LISA_SRLI_D, &tmp, &gpr, 8);
            latxs_append_ir2_opnd3(LISA_NOR, &tmp, &tmp, zero);
            latxs_append_ir2_opnd2ii(LISA_BSTRINS_D, &gpr, &tmp, 15, 8);
        } else {
            if (os == 64) {
                latxs_append_ir2_opnd3(LISA_NOR, &gpr, &gpr, zero);
            } else if (os == 32) {
                latxs_append_ir2_opnd3(LISA_NOR, &gpr, &gpr, zero);
                latxs_store_ir2_to_ir1_gpr(&gpr, opnd0);
            } else {
                IR2_OPND tmp = latxs_ra_alloc_itemp();
                latxs_append_ir2_opnd3(LISA_NOR, &tmp, &gpr, zero);
                latxs_store_ir2_to_ir1_gpr(&tmp, opnd0);
            }
        }
    } else {
        /* not mem */
        IR2_OPND mem; int off;
        latxs_convert_mem_opnd(&mem, &off, opnd0, -1);
        IR2_OPND tmp = latxs_ra_alloc_itemp();

        IR2_OPCODE load  = LISA_INVALID;
        IR2_OPCODE store = LISA_INVALID;
        switch (os) {
        case 64: load = LISA_LD_D;  store = LISA_ST_D; break;
        case 32: load = LISA_LD_WU; store = LISA_ST_W; break;
        case 16: load = LISA_LD_HU; store = LISA_ST_H; break;
        case  8: load = LISA_LD_BU; store = LISA_ST_B; break;
        default: break;
        }

        gen_ldst_softmmu_helper(load, &tmp, &mem, off, 1);
        latxs_append_ir2_opnd3(LISA_NOR, &tmp, &tmp, zero);
        gen_ldst_softmmu_helper(store, &tmp, &mem, off, 1);
    }

    return true;
}

bool latxs_translate_neg_byhand64(IR1_INST *pir1)
{
    IR2_OPND *zero = &latxs_zero_ir2_opnd;
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    int os = ir1_opnd_size(opnd0);

    if (ir1_opnd_is_gpr(opnd0)) {
        /* neg r8/r16/r32/r65 */
        if (os == 64) {
            IR2_OPND gpr = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd0));
            LATXS_FLAG_REDUCTION_CHECKIF(pir1)
            latxs_append_ir2_opnd2(LISA_X86SUB_D,    zero, &gpr);
            latxs_append_ir2_opnd3(LISA_SUB_D, &gpr, zero, &gpr);
        } else if (os == 32){
            IR2_OPND gpr = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd0));
            LATXS_FLAG_REDUCTION_CHECKIF(pir1)
            latxs_append_ir2_opnd2(LISA_X86SUB_W,    zero, &gpr);
            latxs_append_ir2_opnd3(LISA_SUB_W, &gpr, zero, &gpr);
            latxs_store_ir2_to_ir1_gpr(&gpr, opnd0);
        } else {
            IR2_OPND tmp = latxs_ra_alloc_itemp();
            latxs_load_ir1_gpr_to_ir2(&tmp, opnd0, EXMode_S);
            LATXS_FLAG_REDUCTION_CHECKIF(pir1)
            latxs_append_ir2_opnd2(LISA_X86SUB_B + (os >> 4), zero, &tmp);
            latxs_append_ir2_opnd3(LISA_SUB_W, &tmp, zero, &tmp);
            latxs_store_ir2_to_ir1_gpr(&tmp, opnd0);
        }
    } else {
        /* neg mem */
        IR2_OPND tmp = latxs_ra_alloc_itemp();
        /* latxs_load_ir1_mem_to_ir2(&tmp, opnd0, EXMode_S, -1); */
        IR2_OPND mem_opnd;
        int mem_imm, opnd_size;
        opnd_size = ir1_opnd_size(opnd0);
        latxs_get_mem_opnd(&mem_opnd, &mem_imm, opnd0, -1);
        la_ld_by_op_size(tmp, mem_opnd, mem_imm, opnd_size, EXMode_S);


        IR2_OPND dest = latxs_ra_alloc_itemp();
        if (os == 64) {
            latxs_append_ir2_opnd3(LISA_SUB_D, &dest, zero, &tmp);
        } else {
            latxs_append_ir2_opnd3(LISA_SUB_W, &dest, zero, &tmp);
        }

        la_st_by_op_size(dest, mem_opnd, mem_imm, opnd_size);
        latxs_ra_free_temp(&mem_opnd);

        LATXS_FLAG_REDUCTION_CHECKIF(pir1)
        latxs_append_ir2_opnd2(LISA_X86SUB_B + (os >> 4) - (os >> 6),
                zero, &tmp);
    }

    return true;
}

static bool __latxs_translate_shift_byhand64_noflag(IR1_INST *pir1, int mode,
        uint32_t imm, int os, int is_64)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR2_OPND tmp = la_new_itmp();
    IR2_OPND src = la_new_itmp();

    int need_store = 0;

    if (ir1_opnd_is_gpr(opnd0) &&
       !ir1_opnd_is_8h(opnd0) && (is_64 || mode == 1))
    {
        /*
         * mode == 1 shl: 64/32/16/8l
         * mode == 2 shr: 64
         * mode == 3 sar: 64
         */
        src = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd0));
        if (is_64) {
            tmp = src;
            need_store = 0;
        } else {
            need_store = 1;
        }
    } else {
        need_store = 1;
        if (mode == 3) { /* sar */
            latxs_load_ir1_to_ir2(&src, opnd0, EXMode_S);
        } else {
            latxs_load_ir1_to_ir2(&src, opnd0, EXMode_Z);
        }
    }

    switch (mode) {
    case 1: /* shl */
        la_out2i(is_64 ? LISA_SLLI_D : LISA_SLLI_W, &tmp, &src, imm);
        break;
    case 2: /* shr */
        la_out2i(is_64 ? LISA_SRLI_D : LISA_SRLI_W, &tmp, &src, imm);
        break;
    case 3: /* sar */
        la_out2i(is_64 ? LISA_SRAI_D : LISA_SRAI_W, &tmp, &src, imm);
        break;
    default:
        lsassertm(0, "%s wrong mode %d\n", __func__, mode);
        break;
    }

    if (need_store) latxs_store_ir2_to_ir1(&tmp, opnd0);

    return true;
}

static bool __latxs_translate_shift_byhand64(IR1_INST *pir1, int mode)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    if (!ir1_opnd_is_imm(opnd1)) return false;

    TRANSLATION_DATA *td = lsenv->tr_data;
    int os = ir1_opnd_size(opnd0);
    int is_64 = (td->sys.code64 && os == 64) ? 1 : 0;

    uint32_t imm = ir1_opnd_uimm(opnd1);
    imm &= is_64 ? 0x3f : 0x1f;

    if (!imm) {
        /* imm is 0 no shift, no eflags aftected */ 
        if (td->sys.code64 && os != 64 && ir1_opnd_is_gpr(opnd0)) {
            /* but dest will be aftected! */
            int r0 = ir1_opnd_base_reg_num(opnd0);
            IR2_OPND gpr0 = latxs_ra_alloc_gpr(r0);
            latxs_store_ir2_to_ir1_gpr(&gpr0, opnd0);
        }
        return true;
    }

    if (!ir1_need_calculate_any_flag(pir1)) {
        return __latxs_translate_shift_byhand64_noflag(pir1, mode, imm, os, is_64);
    }

    IR2_OPND shift = latxs_ra_alloc_itemp();
    IR2_OPND dest = latxs_ra_alloc_itemp();
    IR2_OPND src = latxs_ra_alloc_itemp();

    IR2_OPCODE lbt = LISA_INVALID;

    latxs_append_ir2_opnd2i(LISA_ORI, &shift, &latxs_zero_ir2_opnd, imm);
    switch (mode) {
    /* shl */
    case 1:
        latxs_load_ir1_to_ir2(&src, opnd0, EXMode_Z);
        latxs_append_ir2_opnd3(is_64 ? LISA_SLL_D : LISA_SLL_W,
                &dest, &src, &shift);
        lbt = LISA_X86SLL_B;
        break;
    /* shr */
    case 2:
        latxs_load_ir1_to_ir2(&src, opnd0, EXMode_Z);
        latxs_append_ir2_opnd3(is_64 ? LISA_SRL_D : LISA_SRL_W,
                &dest, &src, &shift);
        lbt = LISA_X86SRL_B;
        break;
    /* sar */
    case 3:
        latxs_load_ir1_to_ir2(&src, opnd0, EXMode_S);
        latxs_append_ir2_opnd3(is_64 ? LISA_SRA_D : LISA_SRA_W,
                &dest, &src, &shift);
        lbt = LISA_X86SRA_B;
        break;
    default:
        lsassertm(0, "%s wrong mode %d\n", __func__, mode);
        break;
    }

    if (ir1_opnd_is_mem(opnd0)) {
        latxs_store_ir2_to_ir1(&dest, opnd0);
        latxs_ra_free_temp(&dest);
        /* calculate elfags after store */
        LATXS_FLAG_REDUCTION_CHECKIF(pir1)
        latxs_append_ir2_opnd2(lbt + (os >> 4) - (os >> 6), &src, &shift);
        latxs_ra_free_temp(&shift);
    } else {
        /* Destination is GPR, no exception will generate */
        LATXS_FLAG_REDUCTION_CHECKIF(pir1)
        latxs_append_ir2_opnd2(lbt + (os >> 4) - (os >> 6), &src, &shift);
        latxs_ra_free_temp(&src);
        latxs_ra_free_temp(&shift);

        latxs_store_ir2_to_ir1_gpr(&dest, opnd0);
        latxs_ra_free_temp(&dest);
    }

    TranslationBlock *tb = td->curr_tb;
    if (tb && tb->cflags & CF_USE_ICOUNT) {
        /* align with TCG : AF is undefined, set to 0 */
        latxs_append_ir2_opnd1i(LISA_X86MTFLAG,
                &latxs_zero_ir2_opnd, (0x1 << 2));
    }

    return true;
}

bool latxs_translate_shl_byhand64(IR1_INST *pir1)
{
    return __latxs_translate_shift_byhand64(pir1, 1);
}

bool latxs_translate_shr_byhand64(IR1_INST *pir1)
{
    return __latxs_translate_shift_byhand64(pir1, 2);
}

bool latxs_translate_sar_byhand64(IR1_INST *pir1)
{
    return __latxs_translate_shift_byhand64(pir1, 3);
}
