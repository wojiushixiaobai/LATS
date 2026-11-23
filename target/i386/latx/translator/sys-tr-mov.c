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
#include "latx-sys-tr-mmio.h"
#include "latx-ghbr.h"
#include "latx-sys-inst-ptn.h"
#include "tb-ir1.h"

void latxs_sys_mov_register_ir1(void)
{
    latxs_register_ir1(X86_INS_MOV);
    latxs_register_ir1(X86_INS_MOVABS);
    latxs_register_ir1(X86_INS_MOVZX);
    latxs_register_ir1(X86_INS_MOVSX);
    latxs_register_ir1(X86_INS_MOVSXD);
    latxs_register_ir1(X86_INS_LEA);
    latxs_register_ir1(X86_INS_MOVQ);
    latxs_register_ir1(X86_INS_MOVD);

    latxs_register_ir1(X86_INS_CMOVO);
    latxs_register_ir1(X86_INS_CMOVNO);
    latxs_register_ir1(X86_INS_CMOVB);
    latxs_register_ir1(X86_INS_CMOVAE);
    latxs_register_ir1(X86_INS_CMOVE);
    latxs_register_ir1(X86_INS_CMOVNE);
    latxs_register_ir1(X86_INS_CMOVBE);
    latxs_register_ir1(X86_INS_CMOVA);
    latxs_register_ir1(X86_INS_CMOVS);
    latxs_register_ir1(X86_INS_CMOVNS);
    latxs_register_ir1(X86_INS_CMOVP);
    latxs_register_ir1(X86_INS_CMOVNP);
    latxs_register_ir1(X86_INS_CMOVL);
    latxs_register_ir1(X86_INS_CMOVGE);
    latxs_register_ir1(X86_INS_CMOVLE);
    latxs_register_ir1(X86_INS_CMOVG);

    latxs_register_ir1(X86_INS_XLATB);
}

bool latxs_translate_mov(IR1_INST *pir1)
{
    TranslationBlock *tb = lsenv->tr_data->curr_tb;
    if (tb->cflags & CF_USE_ICOUNT) {
        if (latxs_ir1_is_mov_to_cr(pir1) ||
            latxs_ir1_is_mov_from_cr(pir1) ){
            latxs_tr_gen_io_start();
        }
    }
#ifdef CONFIG_SOFTMMU
    if (latxs_translate_mov_opt_hpet(pir1)) {
        return true;
    }
    if (latxs_translate_mov_opt_apic(pir1)) {
        return true;
    }
#endif
    if (option_by_hand_64 && latxs_translate_mov_byhand64(pir1)) {
        return true;
    }

    IR2_OPND src =
        latxs_load_ireg_from_ir1(ir1_get_opnd(pir1, 1), EXMode_N);
    latxs_store_ir2_to_ir1(&src, ir1_get_opnd(pir1, 0));

    return true;
}

bool latxs_translate_movzx(IR1_INST *pir1)
{
    if (option_by_hand_64 && latxs_translate_movzx_byhand64(pir1)) {
        return true;
    }

    IR2_OPND src = latxs_ra_alloc_itemp();
    latxs_load_ir1_to_ir2(&src, ir1_get_opnd(pir1, 1), EXMode_Z);
    latxs_store_ir2_to_ir1(&src, ir1_get_opnd(pir1, 0));
    return true;
}

bool latxs_translate_movsx(IR1_INST *pir1)
{
    if (option_by_hand_64 && latxs_translate_movsx_byhand64(pir1)) {
        return true;
    }

    IR2_OPND src = latxs_ra_alloc_itemp();
    latxs_load_ir1_to_ir2(&src, ir1_get_opnd(pir1, 1), EXMode_S);
    latxs_store_ir2_to_ir1(&src, ir1_get_opnd(pir1, 0));
    return true;
}

bool latxs_translate_movsxd(IR1_INST *pir1)
{
    if (option_by_hand_64 && latxs_translate_movsxd_byhand64(pir1)) {
        return true;
    }

    IR2_OPND src = latxs_ra_alloc_itemp();
    latxs_load_ir1_to_ir2(&src, ir1_get_opnd(pir1, 1), EXMode_S);
    latxs_store_ir2_to_ir1(&src, ir1_get_opnd(pir1, 0));
    return true;
}

bool latxs_translate_lea(IR1_INST *pir1)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0); /* GPR */

    int addr_size = latxs_ir1_addr_size(pir1);

    if (option_by_hand_64 && addr_size == 8) {
        int gpr_num = ir1_opnd_base_reg_num(opnd0);
        IR2_OPND gpr = latxs_ra_alloc_gpr(gpr_num);
        int os = ir1_opnd_size(opnd0);
        if (os == 16) {
            IR2_OPND tmp = latxs_ra_alloc_itemp();
            latxs_load_ir1_mem_addr_to_ir2(&tmp,
                    ir1_get_opnd(pir1, 1), addr_size);
            latxs_store_ir2_to_ir1(&tmp, ir1_get_opnd(pir1, 0));
        } else {
            IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);
            int done = 0;
            /* os == 32/64 */
            if (!ir1_opnd_is_pc_relative(opnd1)) {
                /* base */
                if (ir1_opnd_has_base(opnd1)) {
                    IR2_OPND *ea = NULL;

                    int ir1_reg_base = ir1_opnd_base_reg_num(opnd1);
                    IR2_OPND base = latxs_ra_alloc_gpr(ir1_reg_base);
                    ea = &base;

                    /* index */
                    if (ir1_opnd_has_index(opnd1)) {
                        int ir1_reg_index = ir1_opnd_index_reg_num(opnd1);
                        IR2_OPND index = latxs_ra_alloc_gpr(ir1_reg_index);
                        switch (ir1_opnd_scale(opnd1)) {
                        case 1 : latxs_append_ir2_opnd3(LISA_ADD_D, &gpr,
                                         &base, &index); break;
                        case 2 : latxs_append_ir2_opnd3i(LISA_ALSL_D, &gpr,
                                         &index, &base, 0); break;
                        case 4 : latxs_append_ir2_opnd3i(LISA_ALSL_D, &gpr,
                                         &index, &base, 1); break;
                        case 8 : latxs_append_ir2_opnd3i(LISA_ALSL_D, &gpr,
                                         &index, &base, 2); break;
                        default: lsassertm(0, "lea unknown scale\n"); break;
                        }
                        ea = &gpr;
                    }

                    /* disp */
                    if (ir1_opnd_simm(opnd1) != 0) {
                        longx offset = ir1_opnd_simm(opnd1);
                        if (int32_in_int12(offset)) {
                            latxs_append_ir2_opnd2i(LISA_ADDI_D, &gpr, ea, offset);
                        } else {
                            IR2_OPND tmp = latxs_ra_alloc_itemp();
                            latxs_load_imm64_to_ir2(&tmp, offset);
                            latxs_append_ir2_opnd3(LISA_ADD_D, &gpr, ea, &tmp);
                            latxs_ra_free_temp(&tmp);
                        }
                        ea = &gpr;
                    }

                    /* base only, on index, on disp */
                    if (ea == &base) {
                        if (!latxs_ir2_opnd_cmp(&base, &gpr)) {
                            latxs_append_ir2_opnd2_(lisa_mov, &gpr, &base);
                        }
                    }

                    done = 1;

                    int fix = 0;
                    switch (addr_size) {
                    case 2: latxs_append_ir2_opnd2_(lisa_mov16z, &gpr, &gpr); fix = 1; break;
                    case 4: latxs_append_ir2_opnd2_(lisa_mov32z, &gpr, &gpr); fix = 1; break;
                    }
                    if (!fix && os == 32 &&
                        !latxs_ghbr_can_elide_write(pir1, gpr_num)) {
                        latxs_append_ir2_opnd2_(lisa_mov32z, &gpr, &gpr);
                    }
                }
            }

            if (!done) {
                latxs_load_ir1_mem_addr_to_ir2(&gpr, opnd1, addr_size);
                if (os == 32 && !latxs_ghbr_can_elide_write(pir1, gpr_num)) {
                    latxs_append_ir2_opnd2_(lisa_mov32z, &gpr, &gpr);
                }
            }

        }
    } else {
        IR2_OPND value_opnd = latxs_ra_alloc_itemp();
        latxs_load_ir1_mem_addr_to_ir2(&value_opnd,
                ir1_get_opnd(pir1, 1), addr_size);
        latxs_store_ir2_to_ir1(&value_opnd, opnd0);
    }

    return true;
}

bool latxs_translate_movq(IR1_INST *pir1)
{
    if (latxs_tr_gen_sse_common_excp_check(pir1)) {
        return true;
    }
    latxs_enter_mmx(pir1);

    IR1_OPND *dest = ir1_get_opnd(pir1, 0);
    IR1_OPND *src  = ir1_get_opnd(pir1, 1);

    /* XMM */

    if (lsenv->tr_data->sys.code64) {
        if (ir1_opnd_is_xmm(dest) && ir1_opnd_is_gpr(src)) {
            IR2_OPND gpr_src = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(src));
            IR2_OPND xmm_dest = latxs_ra_alloc_xmm(ir1_opnd_base_reg_num(dest));

            IR2_OPND temp = latxs_ra_alloc_ftemp();
            latxs_append_ir2_opnd2i(LISA_VINSGR2VR_D, &temp, &gpr_src, 0);
            if (option_enable_lasx) {
                latxs_append_ir2_opnd2i(LISA_XVPICKVE_D, &xmm_dest, &temp, 0);
            } else {
                latxs_append_ir2_opnd2i(LISA_VANDI_B, &xmm_dest, &xmm_dest, 0);
                latxs_append_ir2_opnd2i(LISA_VEXTRINS_D, &xmm_dest, &temp, 0);
            }

            return true;
        }
        if (ir1_opnd_is_xmm(src) && ir1_opnd_is_gpr(dest)) {
            IR2_OPND gpr_dest = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(dest));
            IR2_OPND xmm_src = latxs_ra_alloc_xmm(ir1_opnd_base_reg_num(src));
            latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_DU, &gpr_dest, &xmm_src, 0);
            return true;
        }
        if (ir1_opnd_is_mmx(dest) && ir1_opnd_is_gpr(src)) {
            IR2_OPND gpr_src = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(src));
            IR2_OPND mm_dest = latxs_ra_alloc_mmx(ir1_opnd_base_reg_num(dest));

            latxs_append_ir2_opnd2i(LISA_VINSGR2VR_D, &mm_dest, &gpr_src, 0);

            return true;
        }
        if (ir1_opnd_is_mmx(src) && ir1_opnd_is_gpr(dest)) {
            IR2_OPND gpr_dest = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(dest));
            IR2_OPND mm_src = latxs_ra_alloc_mmx(ir1_opnd_base_reg_num(src));
            latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_DU, &gpr_dest, &mm_src, 0);
            return true;
        }
    }

    if (ir1_opnd_is_xmm(dest) && ir1_opnd_is_mem(src)) {
        IR2_OPND temp = latxs_ra_alloc_itemp();
        latxs_load_ir1_mem_to_ir2(&temp, src, EXMode_Z, -1);
        /* IR2_OPND temp = latxs_load_freg_from_ir1_1(src, false, false); */
        IR2_OPND xmm_dest = latxs_ra_alloc_xmm(ir1_opnd_base_reg_num(dest));
        /* latxs_append_ir2_opnd2i(LISA_VCLRSTRI_V, &xmm_dest, &temp, 7); */
        latxs_append_ir2_opnd1i(LISA_VLDI, &xmm_dest, 0);
        latxs_append_ir2_opnd2i(LISA_VINSGR2VR_D, &xmm_dest, &temp, 0);
        return true;
    }

    if (ir1_opnd_is_mem(dest) && ir1_opnd_is_xmm(src)) {
        IR2_OPND xmm_src = latxs_ra_alloc_xmm(ir1_opnd_base_reg_num(src));
        latxs_store_freg_to_ir1(&xmm_src, dest, false);
        return true;
    }

    if (ir1_opnd_is_xmm(dest) && ir1_opnd_is_xmm(src)) {
        IR2_OPND temp = latxs_ra_alloc_itemp();
        IR2_OPND xmm_dest = latxs_ra_alloc_xmm(ir1_opnd_base_reg_num(dest));
        IR2_OPND xmm_src  = latxs_ra_alloc_xmm(ir1_opnd_base_reg_num(src));
        /* latxs_append_ir2_opnd2i(LISA_VCLRSTRI_V, &xmm_dest, &xmm_src, 7); */
        /* latxs_append_ir2_opnd2i(LISA_XVPICKVE_D, &xmm_dest, &xmm_src, 0); */
        latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_D, &temp, &xmm_src, 0);
        latxs_append_ir2_opnd1i(LISA_VLDI, &xmm_dest, 0);
        latxs_append_ir2_opnd2i(LISA_VINSGR2VR_D, &xmm_dest, &temp, 0);
        return true;
    }

    /* MMX */

    if (ir1_opnd_is_mmx(dest)) { /* dest mmx : src mmx/mem */
        IR2_OPND dest_opnd = latxs_load_freg_from_ir1_1(dest, false);
        latxs_load_freg_from_ir1_2(&dest_opnd, src, false);
        return true;
    }

    if (ir1_opnd_is_mmx(src)) { /* src mmx  : dest mmx/mem */
        IR2_OPND source_opnd = latxs_load_freg_from_ir1_1(src, false);
        latxs_store_freg_to_ir1(&source_opnd, dest, false);
        return true;
    }

    lsassertm(0, "unknown SIMD type %s.\n", __func__);

    return true;
}

bool latxs_translate_movd(IR1_INST *pir1)
{
    if (latxs_tr_gen_sse_common_excp_check(pir1)) {
        return true;
    }
    latxs_enter_mmx(pir1);

    IR1_OPND *dest = ir1_get_opnd(pir1, 0);
    IR1_OPND *src  = ir1_get_opnd(pir1, 1);

    /* XMM */

    if (ir1_opnd_is_xmm(dest) && ir1_opnd_is_mem(src)) {
        IR2_OPND temp = latxs_ra_alloc_itemp();
        latxs_load_ir1_mem_to_ir2(&temp, src, EXMode_Z, -1);
        IR2_OPND xmm_dest = latxs_ra_alloc_xmm(ir1_opnd_base_reg_num(dest));
        latxs_append_ir2_opnd1i(LISA_VLDI, &xmm_dest, 0);
        latxs_append_ir2_opnd2i(LISA_VINSGR2VR_W, &xmm_dest, &temp, 0);
        return true;
    }

    if (ir1_opnd_is_xmm(dest) && ir1_opnd_is_gpr(src)) {
        IR2_OPND gpr_src = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(src));
        IR2_OPND xmm_dest  = latxs_ra_alloc_xmm(ir1_opnd_base_reg_num(dest));
        latxs_append_ir2_opnd1i(LISA_VLDI, &xmm_dest, 0);
        latxs_append_ir2_opnd2i(LISA_VINSGR2VR_W, &xmm_dest, &gpr_src, 0);
        return true;
    }

    if (ir1_opnd_is_mem(dest) && ir1_opnd_is_xmm(src)) {
        IR2_OPND xmm_src = latxs_ra_alloc_xmm(ir1_opnd_base_reg_num(src));
        latxs_store_freg_to_ir1(&xmm_src, dest, false);
        return true;
    }

    if (ir1_opnd_is_gpr(dest) && ir1_opnd_is_xmm(src)) {
        IR2_OPND gpr_dest = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(dest));
        IR2_OPND xmm_src  = latxs_ra_alloc_xmm(ir1_opnd_base_reg_num(src));
        latxs_append_ir2_opnd2(LISA_MOVFR2GR_S, &gpr_dest, &xmm_src);
        latxs_append_ir2_opnd2_(lisa_mov32z, &gpr_dest, &gpr_dest);
        return true;
    }

    /* MMX */

    if (ir1_opnd_is_mem(dest) && ir1_opnd_is_mmx(src)) {
        IR2_OPND mmx_src = latxs_ra_alloc_mmx(ir1_opnd_base_reg_num(src));
        latxs_store_freg_to_ir1(&mmx_src, dest, false);
        return true;
    }

    if (ir1_opnd_is_gpr(dest) && ir1_opnd_is_mmx(src)) {
        IR2_OPND gpr_dest = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(dest));
        IR2_OPND mmx_src  = latxs_ra_alloc_mmx(ir1_opnd_base_reg_num(src));
        latxs_append_ir2_opnd2(LISA_MOVFR2GR_S, &gpr_dest, &mmx_src);
        latxs_append_ir2_opnd2_(lisa_mov32z, &gpr_dest, &gpr_dest);
        return true;
    }

    if (ir1_opnd_is_mmx(dest) && ir1_opnd_is_mem(src)) {
        IR2_OPND temp = latxs_ra_alloc_itemp();
        latxs_load_ir1_mem_to_ir2(&temp, src, EXMode_Z, -1);
        IR2_OPND mmx_dest = latxs_ra_alloc_mmx(ir1_opnd_base_reg_num(dest));
        latxs_append_ir2_opnd1i(LISA_VLDI, &mmx_dest, 0);
        latxs_append_ir2_opnd2i(LISA_VINSGR2VR_W, &mmx_dest, &temp, 0);
        return true;
    }

    if (ir1_opnd_is_mmx(dest) && ir1_opnd_is_gpr(src)) {
        IR2_OPND gpr_src = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(src));
        IR2_OPND mmx_dest  = latxs_ra_alloc_mmx(ir1_opnd_base_reg_num(dest));
        latxs_append_ir2_opnd1i(LISA_VLDI, &mmx_dest, 0);
        latxs_append_ir2_opnd2i(LISA_VINSGR2VR_W, &mmx_dest, &gpr_src, 0);
        return true;
    }

    lsassertm(0, "unknown SIMD type %s.\n", __func__);

    return true;
}

bool latxs_translate_xlat(IR1_INST *pir1)
{
    lsassertm(!latxs_ir1_has_prefix_seg(pir1),
              "xlat segment override unsupported");
    int addr_size = latxs_ir1_addr_size(pir1);
    (void)addr_size; /* avoid compile warning */

    if (lsenv->tr_data->sys.code64) {
        lsassert(addr_size == 8 || addr_size == 4);
    } else {
        lsassert(addr_size == 4 || addr_size == 2);
    }

    IR2_OPND addr = latxs_ra_alloc_itemp();
    IR2_OPND data = latxs_ra_alloc_itemp();
    IR2_OPND rax = latxs_ra_alloc_gpr(eax_index);
    IR2_OPND rbx = latxs_ra_alloc_gpr(ebx_index);

    switch (addr_size) {
    case 8:
        if (lsenv->tr_data->sys.addseg) {
            la_out2_(lisa_mov, &addr, &rbx); // RBX
        } else {
            la_out2_(lisa_mov8z, &addr, &rax); // AL
            la_out3 (LISA_ADD_D, &addr, &addr, &rbx); // RBX + AL
            goto do_xlat;
        }
        break;
    case 4:
        la_out2_(lisa_mov32z, &addr, &rbx); // EBX
        break;
    case 2:
        la_out2_(lisa_mov16z, &addr, &rbx); // BX
        break;
    default:
        break;
    }

    if (lsenv->tr_data->sys.addseg) {
        int seg = ds_index;
        la_out2i(LISA_LD_D, &data, &la_env,
                lsenv_offset_of_seg_base(lsenv, seg));
        la_out3 (LISA_ADD_D, &addr, &addr, &data);
        /* addr = DS:RBX/EBX/BX */
    }

    la_out2_(lisa_mov8z, &data, &rax); // AL
    la_out3 (LISA_ADD_D,  &addr, &data, &addr);

do_xlat:
    gen_ldst_softmmu_helper(LISA_LD_BU, &data, &addr, 0, 1);

    la_out2ii(LISA_BSTRINS_D, &rax, &data, 7, 0);

    latxs_ra_free_temp(&addr);
    latxs_ra_free_temp(&data);
    return true;
}


#ifdef CONFIG_SOFTMMU
#define CMOVCC_ASSERT(pir1, os0, os1) \
    do { \
        lsassertm_illop(ir1_addr(pir1), \
            (os0 == 16 || os1 == 32 || os0 == 64) && os0 == os1, \
            "cmov opnd size0 %d size1 %d is unsupported.\n", \
            os0, os1); \
    } while (0)
#else
#define CMOVCC_ASSERT(pir1, os0, os1) \
    do { \
        lsassertm((os0 == 16 || os1 == 32 || os0 == 64) && os0 == os1, \
            "cmov opnd size0 %d size1 %d is unsupported.\n", \
            os0, os1); \
    } while (0)
#endif

#define CMOVCC_STORE_IR2_TO_IR1(src, opnd00) \
    do { \
        latxs_store_ir2_to_ir1(&src, opnd00); \
    } while (0)

#define LATXS_USE_CMOVCC_OPT

#if defined(LATXS_USE_CMOVCC_OPT)
/**
 * @brief CMOVcc translation
 *
 * CMOVcc has:
 * - CMOVO    // OF = 1
 * - CMOVNO   // OF = 0
 * - CMOVB    // CF = 1
 * - CMOVAE   // CF = 0
 * - CMOVE    // ZF = 1
 * - CMOVNE   // ZF = 0
 * - CMOVBE   // CF = 1 | ZF = 1
 * - CMOVA    // CF = O & ZF = O
 * - CMOVS    // SF = 1
 * - CMOVNS   // SF = 0
 * - CMOVP    // PF = 1
 * - CMOVNP   // PF = 0
 * - CMOVL    // SF <> OF
 * - CMOVGE   // SF = OF
 * - CMOVLE   // ZF = 1 | SF <> OF
 * - CMOVG    // ZF = 0 & SF = OF
 *
 * @param pir1 input current inst
 * @return true if transalte success
 * @return false translate failure
 */
static bool __latxs_translate_cmovcc(IR1_INST *pir1)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0); /* GPR     */
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1); /* GPR/MEM */

    IR2_OPND cond = latxs_ra_alloc_itemp();
    latxs_get_eflag_condition(&cond, pir1);

#ifdef CONFIG_SOFTMMU
    int opnd_size_0 = ir1_opnd_size(opnd0);
    int opnd_size_1 = ir1_opnd_size(opnd1);
    CMOVCC_ASSERT(pir1, opnd_size_0, opnd_size_1);
#endif

    IR2_OPND src;
    if (ir1_opnd_is_gpr(opnd1)) {
        src = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd1));
    } else {
        src = latxs_load_ireg_from_ir1(opnd1, EXMode_N);
    }

    int dest_gpr = ir1_opnd_base_reg_num(opnd0);
    IR2_OPND dest = latxs_ra_alloc_gpr(dest_gpr);

    TRANSLATION_DATA *td = lsenv->tr_data;
    if (td->sys.code64 && !td->is_tu_mode && td->curr_ir1_nr > 0 &&
        ir1_opnd_size(opnd0) == 32 && ir1_opnd_is_gpr(opnd1) &&
        (ir1_opcode(pir1) == X86_INS_CMOVE ||
         ir1_opcode(pir1) == X86_INS_CMOVNE)) {
        IR1_INST *previous = ir1_inst_in_list(td->ir1_inst_array,
                                             td->curr_ir1_nr - 1);
        if (ir1_opcode(previous) == X86_INS_MOVZX &&
            previous->instptn.opc == INSTPTN_OPC_NONE &&
            ir1_opnd_is_same_reg(ir1_get_opnd(previous, 0), opnd0) &&
            ir1_opnd_is_8l(ir1_get_opnd(previous, 1)) &&
            !latxs_ghbr_can_elide_write(previous, dest_gpr)) {
            /*
             * MOVZX has already cleared the high bits on the false path.
             * CMOV's register source cannot fault when the move is skipped.
             */
            IR2_OPND skip = latxs_ir2_opnd_new_label();

            latxs_append_ir2_opnd2(LISA_BEQZ, &cond, &skip);
            latxs_store_ir2_to_ir1_gpr(&src, opnd0);
            latxs_append_ir2_opnd1(LISA_LABEL, &skip);
            return true;
        }
    }

    IR2_OPND cond1 = latxs_ra_alloc_itemp();
    IR2_OPND cond2 = latxs_ra_alloc_itemp();
    latxs_append_ir2_opnd3(LISA_MASKNEZ, &cond1, &dest, &cond);
    latxs_append_ir2_opnd3(LISA_MASKEQZ, &cond2, &src, &cond);

    if (ir1_opnd_size(opnd0) == 64) {
        latxs_append_ir2_opnd3(LISA_OR, &dest, &cond1, &cond2);
    } else {
        latxs_append_ir2_opnd3(LISA_OR, &cond1, &cond1, &cond2);
        latxs_store_ir2_to_ir1(&cond1, opnd0);
    }

    return true;
}
#endif

bool latxs_translate_cmovo(IR1_INST *pir1)
{
#ifdef LATXS_USE_CMOVCC_OPT
    return __latxs_translate_cmovcc(pir1);
#else
    IR2_OPND condition = latxs_ra_alloc_itemp(); /* OF */
    latxs_get_eflag_condition(&condition, pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0); /* GPR     */
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1); /* GPR/MEM */

    int opnd_size_0 = ir1_opnd_size(opnd0);
    int opnd_size_1 = ir1_opnd_size(opnd1);
    CMOVCC_ASSERT(pir1, opnd_size_0, opnd_size_1);

    IR2_OPND src_opnd = latxs_load_ireg_from_ir1(opnd1, EXMode_N);

    IR2_OPND no_mov = latxs_ir2_opnd_new_label(); /* mov if OF = 1 */
    latxs_append_ir2_opnd2(LISA_BEQZ, &condition, &no_mov);
    CMOVCC_STORE_IR2_TO_IR1(src_opnd, opnd0);
    latxs_append_ir2_opnd1(LISA_LABEL, &no_mov);
    /* make sure high 32 bits set to zero. TODO: simplify */
    if (ir1_opnd_size(opnd0) == 32) {
        IR2_OPND dest_opnd = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd0));
        latxs_append_ir2_opnd2_(lisa_mov32z, &dest_opnd, &dest_opnd);
    }
    latxs_ra_free_temp(&condition);
    return true;
#endif
}

bool latxs_translate_cmovno(IR1_INST *pir1)
{
#ifdef LATXS_USE_CMOVCC_OPT
    return __latxs_translate_cmovcc(pir1);
#else
    IR2_OPND condition = latxs_ra_alloc_itemp(); /* OF */
    latxs_get_eflag_condition(&condition, pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0); /* GPR     */
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1); /* GPR/MEM */

    int opnd_size_0 = ir1_opnd_size(opnd0);
    int opnd_size_1 = ir1_opnd_size(opnd1);
    CMOVCC_ASSERT(pir1, opnd_size_0, opnd_size_1);

    IR2_OPND src_opnd = latxs_load_ireg_from_ir1(opnd1, EXMode_N);

    IR2_OPND no_mov = latxs_ir2_opnd_new_label(); /* mov if OF = 0 */
    latxs_append_ir2_opnd2(LISA_BNEZ, &condition, &no_mov);
    CMOVCC_STORE_IR2_TO_IR1(src_opnd, opnd0);
    latxs_append_ir2_opnd1(LISA_LABEL, &no_mov);
    /* make sure high 32 bits set to zero. TODO: simplify */
    if (ir1_opnd_size(opnd0) == 32) {
        IR2_OPND dest_opnd = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd0));
        latxs_append_ir2_opnd2_(lisa_mov32z, &dest_opnd, &dest_opnd);
    }
    latxs_ra_free_temp(&condition);
    return true;
#endif
}

bool latxs_translate_cmovb(IR1_INST *pir1)
{
#ifdef LATXS_USE_CMOVCC_OPT
    return __latxs_translate_cmovcc(pir1);
#else
    IR2_OPND condition = latxs_ra_alloc_itemp(); /* CF */
    latxs_get_eflag_condition(&condition, pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0); /* GPR     */
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1); /* GPR/MEM */

    int opnd_size_0 = ir1_opnd_size(opnd0);
    int opnd_size_1 = ir1_opnd_size(opnd1);
    CMOVCC_ASSERT(pir1, opnd_size_0, opnd_size_1);

    IR2_OPND src_opnd = latxs_load_ireg_from_ir1(opnd1, EXMode_N);

    IR2_OPND no_mov = latxs_ir2_opnd_new_label(); /* mov if CF = 1 */
    latxs_append_ir2_opnd2(LISA_BEQZ, &condition, &no_mov);
    CMOVCC_STORE_IR2_TO_IR1(src_opnd, opnd0);
    latxs_append_ir2_opnd1(LISA_LABEL, &no_mov);
    /* make sure high 32 bits set to zero. TODO: simplify */
    if (ir1_opnd_size(opnd0) == 32) {
        IR2_OPND dest_opnd = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd0));
        latxs_append_ir2_opnd2_(lisa_mov32z, &dest_opnd, &dest_opnd);
    }
    latxs_ra_free_temp(&condition);
    return true;
#endif
}

bool latxs_translate_cmovae(IR1_INST *pir1)
{
#ifdef LATXS_USE_CMOVCC_OPT
    return __latxs_translate_cmovcc(pir1);
#else
    IR2_OPND condition = latxs_ra_alloc_itemp(); /* CF */
    latxs_get_eflag_condition(&condition, pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0); /* GPR     */
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1); /* GPR/MEM */

    int opnd_size_0 = ir1_opnd_size(opnd0);
    int opnd_size_1 = ir1_opnd_size(opnd1);
    CMOVCC_ASSERT(pir1, opnd_size_0, opnd_size_1);

    IR2_OPND src_opnd = latxs_load_ireg_from_ir1(opnd1, EXMode_N);

    IR2_OPND no_mov = latxs_ir2_opnd_new_label(); /* mov if CF = 0 */
    latxs_append_ir2_opnd2(LISA_BNEZ, &condition, &no_mov);
    CMOVCC_STORE_IR2_TO_IR1(src_opnd, opnd0);
    latxs_append_ir2_opnd1(LISA_LABEL, &no_mov);
    /* make sure high 32 bits set to zero. TODO: simplify */
    if (ir1_opnd_size(opnd0) == 32) {
        IR2_OPND dest_opnd = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd0));
        latxs_append_ir2_opnd2_(lisa_mov32z, &dest_opnd, &dest_opnd);
    }
    latxs_ra_free_temp(&condition);
    return true;
#endif
}

bool latxs_translate_cmovz(IR1_INST *pir1)
{
#ifdef LATXS_USE_CMOVCC_OPT
    return __latxs_translate_cmovcc(pir1);
#else
    IR2_OPND condition = latxs_ra_alloc_itemp(); /* ZF */
    latxs_get_eflag_condition(&condition, pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0); /* GPR     */
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1); /* GPR/MEM */

    int opnd_size_0 = ir1_opnd_size(opnd0);
    int opnd_size_1 = ir1_opnd_size(opnd1);
    CMOVCC_ASSERT(pir1, opnd_size_0, opnd_size_1);

    IR2_OPND src_opnd = latxs_load_ireg_from_ir1(opnd1, EXMode_N);

    IR2_OPND no_mov = latxs_ir2_opnd_new_label(); /* mov if ZF = 1 */
    latxs_append_ir2_opnd2(LISA_BEQZ, &condition, &no_mov);
    CMOVCC_STORE_IR2_TO_IR1(src_opnd, opnd0);
    latxs_append_ir2_opnd1(LISA_LABEL, &no_mov);
    /* make sure high 32 bits set to zero. TODO: simplify */
    if (ir1_opnd_size(opnd0) == 32) {
        IR2_OPND dest_opnd = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd0));
        latxs_append_ir2_opnd2_(lisa_mov32z, &dest_opnd, &dest_opnd);
    }
    latxs_ra_free_temp(&condition);
    return true;
#endif
}

bool latxs_translate_cmovnz(IR1_INST *pir1)
{
#ifdef LATXS_USE_CMOVCC_OPT
    return __latxs_translate_cmovcc(pir1);
#else
    IR2_OPND condition = latxs_ra_alloc_itemp(); /* ZF */
    latxs_get_eflag_condition(&condition, pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0); /* GPR     */
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1); /* GPR/MEM */

    int opnd_size_0 = ir1_opnd_size(opnd0);
    int opnd_size_1 = ir1_opnd_size(opnd1);
    CMOVCC_ASSERT(pir1, opnd_size_0, opnd_size_1);

    IR2_OPND src_opnd = latxs_load_ireg_from_ir1(opnd1, EXMode_N);

    IR2_OPND no_mov = latxs_ir2_opnd_new_label(); /* mov if ZF = 0 */
    latxs_append_ir2_opnd2(LISA_BNEZ, &condition, &no_mov);
    CMOVCC_STORE_IR2_TO_IR1(src_opnd, opnd0);
    latxs_append_ir2_opnd1(LISA_LABEL, &no_mov);
    /* make sure high 32 bits set to zero. TODO: simplify */
    if (ir1_opnd_size(opnd0) == 32) {
        IR2_OPND dest_opnd = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd0));
        latxs_append_ir2_opnd2_(lisa_mov32z, &dest_opnd, &dest_opnd);
    }
    latxs_ra_free_temp(&condition);
    return true;
#endif
}

bool latxs_translate_cmovbe(IR1_INST *pir1)
{
#ifdef LATXS_USE_CMOVCC_OPT
    return __latxs_translate_cmovcc(pir1);
#else
    IR2_OPND condition = latxs_ra_alloc_itemp(); /* ZF, CF */
    latxs_get_eflag_condition(&condition, pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0); /* GPR     */
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1); /* GPR/MEM */

    int opnd_size_0 = ir1_opnd_size(opnd0);
    int opnd_size_1 = ir1_opnd_size(opnd1);
    CMOVCC_ASSERT(pir1, opnd_size_0, opnd_size_1);

    IR2_OPND src_opnd = latxs_load_ireg_from_ir1(opnd1, EXMode_N);

    IR2_OPND no_mov = latxs_ir2_opnd_new_label(); /* mov if ZF = 1 or CF = 1 */
    latxs_append_ir2_opnd2(LISA_BEQZ, &condition, &no_mov);
    CMOVCC_STORE_IR2_TO_IR1(src_opnd, opnd0);
    latxs_append_ir2_opnd1(LISA_LABEL, &no_mov);
    /* make sure high 32 bits set to zero. TODO: simplify */
    if (ir1_opnd_size(opnd0) == 32) {
        IR2_OPND dest_opnd = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd0));
        latxs_append_ir2_opnd2_(lisa_mov32z, &dest_opnd, &dest_opnd);
    }
    latxs_ra_free_temp(&condition);
    return true;
#endif
}

bool latxs_translate_cmova(IR1_INST *pir1)
{
#ifdef LATXS_USE_CMOVCC_OPT
    return __latxs_translate_cmovcc(pir1);
#else
    IR2_OPND condition = latxs_ra_alloc_itemp(); /* ZF, CF */
    latxs_get_eflag_condition(&condition, pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0); /* GPR     */
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1); /* GPR/MEM */

    int opnd_size_0 = ir1_opnd_size(opnd0);
    int opnd_size_1 = ir1_opnd_size(opnd1);
    CMOVCC_ASSERT(pir1, opnd_size_0, opnd_size_1);

    IR2_OPND src_opnd = latxs_load_ireg_from_ir1(opnd1, EXMode_N);

    IR2_OPND no_mov = latxs_ir2_opnd_new_label(); /* mov if ZF = 0 && CF = 0 */
    latxs_append_ir2_opnd2(LISA_BNEZ, &condition, &no_mov);
    CMOVCC_STORE_IR2_TO_IR1(src_opnd, opnd0);
    latxs_append_ir2_opnd1(LISA_LABEL, &no_mov);
    /* make sure high 32 bits set to zero. TODO: simplify */
    if (ir1_opnd_size(opnd0) == 32) {
        IR2_OPND dest_opnd = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd0));
        latxs_append_ir2_opnd2_(lisa_mov32z, &dest_opnd, &dest_opnd);
    }
    latxs_ra_free_temp(&condition);
    return true;
#endif
}

bool latxs_translate_cmovs(IR1_INST *pir1)
{
#ifdef LATXS_USE_CMOVCC_OPT
    return __latxs_translate_cmovcc(pir1);
#else
    IR2_OPND condition = latxs_ra_alloc_itemp(); /* SF */
    latxs_get_eflag_condition(&condition, pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0); /* GPR     */
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1); /* GPR/MEM */

    int opnd_size_0 = ir1_opnd_size(opnd0);
    int opnd_size_1 = ir1_opnd_size(opnd1);
    CMOVCC_ASSERT(pir1, opnd_size_0, opnd_size_1);

    IR2_OPND src_opnd = latxs_load_ireg_from_ir1(opnd1, EXMode_N);

    IR2_OPND no_mov = latxs_ir2_opnd_new_label(); /* mov if SF = 1 */
    latxs_append_ir2_opnd2(LISA_BEQZ, &condition, &no_mov);
    CMOVCC_STORE_IR2_TO_IR1(src_opnd, opnd0);
    latxs_append_ir2_opnd1(LISA_LABEL, &no_mov);
    /* make sure high 32 bits set to zero. TODO: simplify */
    if (ir1_opnd_size(opnd0) == 32) {
        IR2_OPND dest_opnd = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd0));
        latxs_append_ir2_opnd2_(lisa_mov32z, &dest_opnd, &dest_opnd);
    }
    latxs_ra_free_temp(&condition);
    return true;
#endif
}

bool latxs_translate_cmovns(IR1_INST *pir1)
{
#ifdef LATXS_USE_CMOVCC_OPT
    return __latxs_translate_cmovcc(pir1);
#else
    IR2_OPND condition = latxs_ra_alloc_itemp(); /* SF */
    latxs_get_eflag_condition(&condition, pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0); /* GPR     */
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1); /* GPR/MEM */

    int opnd_size_0 = ir1_opnd_size(opnd0);
    int opnd_size_1 = ir1_opnd_size(opnd1);
    CMOVCC_ASSERT(pir1, opnd_size_0, opnd_size_1);

    IR2_OPND src_opnd = latxs_load_ireg_from_ir1(opnd1, EXMode_N);

    IR2_OPND no_mov = latxs_ir2_opnd_new_label(); /* mov if SF = 0 */
    latxs_append_ir2_opnd2(LISA_BNEZ, &condition, &no_mov);
    CMOVCC_STORE_IR2_TO_IR1(src_opnd, opnd0);
    latxs_append_ir2_opnd1(LISA_LABEL, &no_mov);
    /* make sure high 32 bits set to zero. TODO: simplify */
    if (ir1_opnd_size(opnd0) == 32) {
        IR2_OPND dest_opnd = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd0));
        latxs_append_ir2_opnd2_(lisa_mov32z, &dest_opnd, &dest_opnd);
    }
    latxs_ra_free_temp(&condition);
    return true;
#endif
}

bool latxs_translate_cmovp(IR1_INST *pir1)
{
#ifdef LATXS_USE_CMOVCC_OPT
    return __latxs_translate_cmovcc(pir1);
#else
    IR2_OPND condition = latxs_ra_alloc_itemp(); /* PF */
    latxs_get_eflag_condition(&condition, pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0); /* GPR     */
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1); /* GPR/MEM */

    int opnd_size_0 = ir1_opnd_size(opnd0);
    int opnd_size_1 = ir1_opnd_size(opnd1);
    CMOVCC_ASSERT(pir1, opnd_size_0, opnd_size_1);

    IR2_OPND src_opnd = latxs_load_ireg_from_ir1(opnd1, EXMode_N);

    IR2_OPND no_mov = latxs_ir2_opnd_new_label(); /* mov if PF = 1 */
    latxs_append_ir2_opnd2(LISA_BEQZ, &condition, &no_mov);
    CMOVCC_STORE_IR2_TO_IR1(src_opnd, opnd0);
    latxs_append_ir2_opnd1(LISA_LABEL, &no_mov);
    /* make sure high 32 bits set to zero. TODO: simplify */
    if (ir1_opnd_size(opnd0) == 32) {
        IR2_OPND dest_opnd = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd0));
        latxs_append_ir2_opnd2_(lisa_mov32z, &dest_opnd, &dest_opnd);
    }
    latxs_ra_free_temp(&condition);
    return true;
#endif
}

bool latxs_translate_cmovnp(IR1_INST *pir1)
{
#ifdef LATXS_USE_CMOVCC_OPT
    return __latxs_translate_cmovcc(pir1);
#else
    IR2_OPND condition = latxs_ra_alloc_itemp(); /* PF */
    latxs_get_eflag_condition(&condition, pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0); /* GPR     */
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1); /* GPR/MEM */

    int opnd_size_0 = ir1_opnd_size(opnd0);
    int opnd_size_1 = ir1_opnd_size(opnd1);
    CMOVCC_ASSERT(pir1, opnd_size_0, opnd_size_1);

    IR2_OPND src_opnd = latxs_load_ireg_from_ir1(opnd1, EXMode_N);

    IR2_OPND no_mov = latxs_ir2_opnd_new_label(); /* mov if PF = 0 */
    latxs_append_ir2_opnd2(LISA_BNEZ, &condition, &no_mov);
    CMOVCC_STORE_IR2_TO_IR1(src_opnd, opnd0);
    latxs_append_ir2_opnd1(LISA_LABEL, &no_mov);
    /* make sure high 32 bits set to zero. TODO: simplify */
    if (ir1_opnd_size(opnd0) == 32) {
        IR2_OPND dest_opnd = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd0));
        latxs_append_ir2_opnd2_(lisa_mov32z, &dest_opnd, &dest_opnd);
    }
    latxs_ra_free_temp(&condition);
    return true;
#endif
}

bool latxs_translate_cmovl(IR1_INST *pir1)
{ /* sf != of */
#ifdef LATXS_USE_CMOVCC_OPT
    return __latxs_translate_cmovcc(pir1);
#else
    IR2_OPND condition = latxs_ra_alloc_itemp(); /* cond = SF xor OF */
    latxs_get_eflag_condition(&condition, pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0); /* GPR     */
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1); /* GPR/MEM */

    int opnd_size_0 = ir1_opnd_size(opnd0);
    int opnd_size_1 = ir1_opnd_size(opnd1);
    CMOVCC_ASSERT(pir1, opnd_size_0, opnd_size_1);

    IR2_OPND src_opnd = latxs_load_ireg_from_ir1(opnd1, EXMode_N);

    IR2_OPND no_mov = latxs_ir2_opnd_new_label(); /* mov if cond = 1 */
    latxs_append_ir2_opnd2(LISA_BEQZ, &condition, &no_mov);
    CMOVCC_STORE_IR2_TO_IR1(src_opnd, opnd0);
    latxs_append_ir2_opnd1(LISA_LABEL, &no_mov);
    /* make sure high 32 bits set to zero. TODO: simplify */
    if (ir1_opnd_size(opnd0) == 32) {
        IR2_OPND dest_opnd = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd0));
        latxs_append_ir2_opnd2_(lisa_mov32z, &dest_opnd, &dest_opnd);
    }
    latxs_ra_free_temp(&condition);
    return true;
#endif
}

bool latxs_translate_cmovge(IR1_INST *pir1)
{ /* sf == of */
#ifdef LATXS_USE_CMOVCC_OPT
    return __latxs_translate_cmovcc(pir1);
#else
    IR2_OPND condition = latxs_ra_alloc_itemp(); /* cond = SF xor OF */
    latxs_get_eflag_condition(&condition, pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0); /* GPR     */
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1); /* GPR/MEM */

    int opnd_size_0 = ir1_opnd_size(opnd0);
    int opnd_size_1 = ir1_opnd_size(opnd1);
    CMOVCC_ASSERT(pir1, opnd_size_0, opnd_size_1);

    IR2_OPND src_opnd = latxs_load_ireg_from_ir1(opnd1, EXMode_N);

    IR2_OPND no_mov = latxs_ir2_opnd_new_label(); /* mov if cond = 0 */
    latxs_append_ir2_opnd2(LISA_BNEZ, &condition, &no_mov);
    CMOVCC_STORE_IR2_TO_IR1(src_opnd, opnd0);
    latxs_append_ir2_opnd1(LISA_LABEL, &no_mov);
    /* make sure high 32 bits set to zero. TODO: simplify */
    if (ir1_opnd_size(opnd0) == 32) {
        IR2_OPND dest_opnd = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd0));
        latxs_append_ir2_opnd2_(lisa_mov32z, &dest_opnd, &dest_opnd);
    }
    latxs_ra_free_temp(&condition);
    return true;
#endif
}

bool latxs_translate_cmovle(IR1_INST *pir1)
{ /* zf==1 || sf!=of */
#ifdef LATXS_USE_CMOVCC_OPT
    return __latxs_translate_cmovcc(pir1);
#else
    IR2_OPND condition = latxs_ra_alloc_itemp(); /* ZF, OF xor SF */
    latxs_get_eflag_condition(&condition, pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0); /* GPR     */
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1); /* GPR/MEM */

    int opnd_size_0 = ir1_opnd_size(opnd0);
    int opnd_size_1 = ir1_opnd_size(opnd1);
    CMOVCC_ASSERT(pir1, opnd_size_0, opnd_size_1);

    IR2_OPND src_opnd = latxs_load_ireg_from_ir1(opnd1, EXMode_N);

    IR2_OPND no_mov = latxs_ir2_opnd_new_label(); /* mov if cond != 0 */
    latxs_append_ir2_opnd2(LISA_BEQZ, &condition, &no_mov);
    CMOVCC_STORE_IR2_TO_IR1(src_opnd, opnd0);
    latxs_append_ir2_opnd1(LISA_LABEL, &no_mov);
    /* make sure high 32 bits set to zero. TODO: simplify */
    if (ir1_opnd_size(opnd0) == 32) {
        IR2_OPND dest_opnd = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd0));
        latxs_append_ir2_opnd2_(lisa_mov32z, &dest_opnd, &dest_opnd);
    }
    latxs_ra_free_temp(&condition);
    return true;
#endif
}

bool latxs_translate_cmovg(IR1_INST *pir1)
{ /* zf==0 && sf==of */
#ifdef LATXS_USE_CMOVCC_OPT
    return __latxs_translate_cmovcc(pir1);
#else
    IR2_OPND condition = latxs_ra_alloc_itemp(); /* ZF, OF xor SF */
    latxs_get_eflag_condition(&condition, pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0); /* GPR     */
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1); /* GPR/MEM */

    int opnd_size_0 = ir1_opnd_size(opnd0);
    int opnd_size_1 = ir1_opnd_size(opnd1);
    CMOVCC_ASSERT(pir1, opnd_size_0, opnd_size_1);

    IR2_OPND src_opnd = latxs_load_ireg_from_ir1(opnd1, EXMode_N);

    IR2_OPND no_mov = latxs_ir2_opnd_new_label(); /* mov if cond = 0 */
    latxs_append_ir2_opnd2(LISA_BNEZ, &condition, &no_mov);
    CMOVCC_STORE_IR2_TO_IR1(src_opnd, opnd0);
    latxs_append_ir2_opnd1(LISA_LABEL, &no_mov);
    /* make sure high 32 bits set to zero. TODO: simplify */
    if (ir1_opnd_size(opnd0) == 32) {
        IR2_OPND dest_opnd = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd0));
        latxs_append_ir2_opnd2_(lisa_mov32z, &dest_opnd, &dest_opnd);
    }
    latxs_ra_free_temp(&condition);
    return true;
#endif
}
