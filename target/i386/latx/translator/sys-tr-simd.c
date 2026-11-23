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
#include "latx-shbr.h"
#include "translate.h"
#include "sys-excp.h"
#include "sys-aot.h"
#include "latx-sys-hamt-dmw.h"
#include "crypto/aes.h"

void latxs_sys_simd_register_ir1(void)
{
    latxs_register_ir1(X86_INS_POR);
    latxs_register_ir1(X86_INS_PXOR);

    latxs_register_ir1(X86_INS_PACKUSWB);
    latxs_register_ir1(X86_INS_PACKSSWB);
    latxs_register_ir1(X86_INS_PACKSSDW);

    latxs_register_ir1(X86_INS_PADDB);
    latxs_register_ir1(X86_INS_PADDW);
    latxs_register_ir1(X86_INS_PADDD);
    latxs_register_ir1(X86_INS_PADDQ);

    latxs_register_ir1(X86_INS_PADDUSB);
    latxs_register_ir1(X86_INS_PADDUSW);
    latxs_register_ir1(X86_INS_PADDSW);
    latxs_register_ir1(X86_INS_PADDSB);

    latxs_register_ir1(X86_INS_PSUBUSB);
    latxs_register_ir1(X86_INS_PSUBUSW);
    latxs_register_ir1(X86_INS_PSUBSW);
    latxs_register_ir1(X86_INS_PSUBSB);

    latxs_register_ir1(X86_INS_PAND);
    latxs_register_ir1(X86_INS_PANDN);

    latxs_register_ir1(X86_INS_PCMPEQB);
    latxs_register_ir1(X86_INS_PCMPEQW);
    latxs_register_ir1(X86_INS_PCMPEQD);
    latxs_register_ir1(X86_INS_PCMPGTB);
    latxs_register_ir1(X86_INS_PCMPGTW);
    latxs_register_ir1(X86_INS_PCMPGTD);

    latxs_register_ir1(X86_INS_PMULLW);

    latxs_register_ir1(X86_INS_PSLLW);
    latxs_register_ir1(X86_INS_PSLLD);
    latxs_register_ir1(X86_INS_PSLLQ);
    latxs_register_ir1(X86_INS_PSLLDQ);

    latxs_register_ir1(X86_INS_PSRLW);
    latxs_register_ir1(X86_INS_PSRLD);
    latxs_register_ir1(X86_INS_PSRLQ);
    latxs_register_ir1(X86_INS_PSRLDQ);

    latxs_register_ir1(X86_INS_PSRAW);
    latxs_register_ir1(X86_INS_PSRAD);

    latxs_register_ir1(X86_INS_PSUBB);
    latxs_register_ir1(X86_INS_PSUBW);
    latxs_register_ir1(X86_INS_PSUBD);
    latxs_register_ir1(X86_INS_PSUBQ);

    latxs_register_ir1(X86_INS_PUNPCKHBW);
    latxs_register_ir1(X86_INS_PUNPCKHWD);
    latxs_register_ir1(X86_INS_PUNPCKHDQ);
    latxs_register_ir1(X86_INS_PUNPCKLBW);
    latxs_register_ir1(X86_INS_PUNPCKLWD);
    latxs_register_ir1(X86_INS_PUNPCKLDQ);

    latxs_register_ir1(X86_INS_PUNPCKHQDQ);
    latxs_register_ir1(X86_INS_PUNPCKLQDQ);

    latxs_register_ir1(X86_INS_ADDSD);
    latxs_register_ir1(X86_INS_ADDSS);

    latxs_register_ir1(X86_INS_ANDPS);
    latxs_register_ir1(X86_INS_ANDNPS);

    latxs_register_ir1(X86_INS_CMPEQPD);
    latxs_register_ir1(X86_INS_CMPLTPD);
    latxs_register_ir1(X86_INS_CMPLEPD);
    latxs_register_ir1(X86_INS_CMPUNORDPD);
    latxs_register_ir1(X86_INS_CMPNEQPD);
    latxs_register_ir1(X86_INS_CMPNLTPD);
    latxs_register_ir1(X86_INS_CMPNLEPD);
    latxs_register_ir1(X86_INS_CMPORDPD);
    latxs_register_ir1(X86_INS_CMPEQPS);
    latxs_register_ir1(X86_INS_CMPLTPS);
    latxs_register_ir1(X86_INS_CMPLEPS);
    latxs_register_ir1(X86_INS_CMPUNORDPS);
    latxs_register_ir1(X86_INS_CMPNEQPS);
    latxs_register_ir1(X86_INS_CMPNLTPS);
    latxs_register_ir1(X86_INS_CMPNLEPS);
    latxs_register_ir1(X86_INS_CMPORDPS);
    latxs_register_ir1(X86_INS_CMPEQSD);
    latxs_register_ir1(X86_INS_CMPLTSD);
    latxs_register_ir1(X86_INS_CMPLESD);
    latxs_register_ir1(X86_INS_CMPUNORDSD);
    latxs_register_ir1(X86_INS_CMPNEQSD);
    latxs_register_ir1(X86_INS_CMPNLTSD);
    latxs_register_ir1(X86_INS_CMPNLESD);
    latxs_register_ir1(X86_INS_CMPORDSD);
    latxs_register_ir1(X86_INS_CMPEQSS);
    latxs_register_ir1(X86_INS_CMPLTSS);
    latxs_register_ir1(X86_INS_CMPLESS);
    latxs_register_ir1(X86_INS_CMPUNORDSS);
    latxs_register_ir1(X86_INS_CMPNEQSS);
    latxs_register_ir1(X86_INS_CMPNLTSS);
    latxs_register_ir1(X86_INS_CMPNLESS);
    latxs_register_ir1(X86_INS_CMPORDSS);

    latxs_register_ir1(X86_INS_COMISD);
    latxs_register_ir1(X86_INS_COMISS);
    latxs_register_ir1(X86_INS_UCOMISD);
    latxs_register_ir1(X86_INS_UCOMISS);

    latxs_register_ir1(X86_INS_CVTDQ2PD);
    latxs_register_ir1(X86_INS_CVTDQ2PS);
    latxs_register_ir1(X86_INS_CVTPD2PS);
    latxs_register_ir1(X86_INS_CVTSD2SS);
    latxs_register_ir1(X86_INS_CVTSI2SD);
    latxs_register_ir1(X86_INS_CVTSS2SD);
    latxs_register_ir1(X86_INS_CVTTSS2SI);

    latxs_register_ir1(X86_INS_CVTPI2PS);
    latxs_register_ir1(X86_INS_CVTPS2PI);
    latxs_register_ir1(X86_INS_CVTSI2SS);
    latxs_register_ir1(X86_INS_CVTSS2SI);
    latxs_register_ir1(X86_INS_CVTTPS2PI);
    latxs_register_ir1(X86_INS_CVTPI2PD);
    latxs_register_ir1(X86_INS_CVTPD2PI);
    latxs_register_ir1(X86_INS_CVTSD2SI);
    latxs_register_ir1(X86_INS_CVTTPD2PI);
    latxs_register_ir1(X86_INS_CVTPD2DQ);
    latxs_register_ir1(X86_INS_CVTPS2DQ);
    latxs_register_ir1(X86_INS_CVTTPS2DQ);
    latxs_register_ir1(X86_INS_CVTTPD2DQ);

    latxs_register_ir1(X86_INS_DIVPD);
    latxs_register_ir1(X86_INS_DIVPS);
    latxs_register_ir1(X86_INS_DIVSD);
    latxs_register_ir1(X86_INS_DIVSS);

    latxs_register_ir1(X86_INS_MAXPD);
    latxs_register_ir1(X86_INS_MAXPS);
    latxs_register_ir1(X86_INS_MAXSD);
    latxs_register_ir1(X86_INS_MAXSS);
    latxs_register_ir1(X86_INS_MINPD);
    latxs_register_ir1(X86_INS_MINPS);
    latxs_register_ir1(X86_INS_MINSD);
    latxs_register_ir1(X86_INS_MINSS);

    latxs_register_ir1(X86_INS_MOVNTDQ);
    latxs_register_ir1(X86_INS_MOVNTPD);
    latxs_register_ir1(X86_INS_MOVNTPS);
    latxs_register_ir1(X86_INS_MOVNTQ);
    latxs_register_ir1(X86_INS_MOVNTI);

    latxs_register_ir1(X86_INS_MULPD);
    latxs_register_ir1(X86_INS_MULPS);
    latxs_register_ir1(X86_INS_MULSD);
    latxs_register_ir1(X86_INS_MULSS);

    latxs_register_ir1(X86_INS_ORPD);
    latxs_register_ir1(X86_INS_ORPS);

    latxs_register_ir1(X86_INS_PAVGB);
    latxs_register_ir1(X86_INS_PAVGW);

    latxs_register_ir1(X86_INS_PEXTRW);
    latxs_register_ir1(X86_INS_PINSRW);

    latxs_register_ir1(X86_INS_PMAXSW);
    latxs_register_ir1(X86_INS_PMAXUB);
    latxs_register_ir1(X86_INS_PMINSW);
    latxs_register_ir1(X86_INS_PMINUB);

    latxs_register_ir1(X86_INS_PSADBW);
    latxs_register_ir1(X86_INS_PMOVMSKB);
    latxs_register_ir1(X86_INS_MASKMOVQ);
    latxs_register_ir1(X86_INS_MASKMOVDQU);
    latxs_register_ir1(X86_INS_PMULHW);
    latxs_register_ir1(X86_INS_PMULHUW);
    latxs_register_ir1(X86_INS_PMULUDQ);

    latxs_register_ir1(X86_INS_PMADDWD);

    latxs_register_ir1(X86_INS_PSHUFD);
    latxs_register_ir1(X86_INS_PSHUFW);
    latxs_register_ir1(X86_INS_PSHUFLW);
    latxs_register_ir1(X86_INS_PSHUFHW);

    /* SSE */
    latxs_register_ir1(X86_INS_MOVUPD);
    latxs_register_ir1(X86_INS_MOVDQA);
    latxs_register_ir1(X86_INS_MOVDQU);
    latxs_register_ir1(X86_INS_MOVUPS);
    latxs_register_ir1(X86_INS_MOVAPD);
    latxs_register_ir1(X86_INS_MOVAPS);
    latxs_register_ir1(X86_INS_MOVBE);

    latxs_register_ir1(X86_INS_MOVLHPS);
    latxs_register_ir1(X86_INS_MOVHLPS);
    latxs_register_ir1(X86_INS_MOVHPD);
    latxs_register_ir1(X86_INS_MOVHPS);
    latxs_register_ir1(X86_INS_MOVLPD);
    latxs_register_ir1(X86_INS_MOVLPS);

    latxs_register_ir1(X86_INS_MOVQ2DQ);
    latxs_register_ir1(X86_INS_MOVDQ2Q);

    latxs_register_ir1(X86_INS_MOVMSKPS);
    latxs_register_ir1(X86_INS_MOVMSKPD);

    latxs_register_ir1(X86_INS_MOVSD);
    latxs_register_ir1(X86_INS_MOVSS);

    latxs_register_ir1(X86_INS_ADDPS);
    latxs_register_ir1(X86_INS_ADDPD);

    latxs_register_ir1(X86_INS_ANDPD);
    latxs_register_ir1(X86_INS_ANDNPD);

    latxs_register_ir1(X86_INS_UNPCKLPD);
    latxs_register_ir1(X86_INS_UNPCKHPD);
    latxs_register_ir1(X86_INS_UNPCKHPS);
    latxs_register_ir1(X86_INS_UNPCKLPS);

    latxs_register_ir1(X86_INS_SHUFPS);
    latxs_register_ir1(X86_INS_SHUFPD);

    latxs_register_ir1(X86_INS_XORPS);
    latxs_register_ir1(X86_INS_XORPD);

    latxs_register_ir1(X86_INS_SUBSS);
    latxs_register_ir1(X86_INS_SUBSD);
    latxs_register_ir1(X86_INS_SUBPS);
    latxs_register_ir1(X86_INS_SUBPD);

    latxs_register_ir1(X86_INS_RCPPS);
    latxs_register_ir1(X86_INS_RCPSS);

    latxs_register_ir1(X86_INS_SQRTPD);
    latxs_register_ir1(X86_INS_SQRTPS);
    latxs_register_ir1(X86_INS_SQRTSD);
    latxs_register_ir1(X86_INS_SQRTSS);

    latxs_register_ir1(X86_INS_RSQRTPS);
    latxs_register_ir1(X86_INS_RSQRTSS);

    latxs_register_ir1(X86_INS_CVTPS2PD);
    latxs_register_ir1(X86_INS_CVTTSD2SI);

    latxs_register_ir1(X86_INS_LDDQU);

    latxs_register_ir1(X86_INS_AESDEC);
    latxs_register_ir1(X86_INS_AESDECLAST);
    latxs_register_ir1(X86_INS_AESENC);
    latxs_register_ir1(X86_INS_AESENCLAST);
    latxs_register_ir1(X86_INS_AESIMC);
    latxs_register_ir1(X86_INS_AESKEYGENASSIST);
    latxs_register_ir1(X86_INS_ANDN);
    latxs_register_ir1(X86_INS_PCLMULQDQ);
    latxs_register_ir1(X86_INS_SHA1MSG1);
    latxs_register_ir1(X86_INS_SHA1MSG2);
    latxs_register_ir1(X86_INS_SHA1NEXTE);
    latxs_register_ir1(X86_INS_SHA1RNDS4);
    latxs_register_ir1(X86_INS_SHA256MSG1);
    latxs_register_ir1(X86_INS_SHA256MSG2);
    latxs_register_ir1(X86_INS_SHA256RNDS2);
}

#define XMM_REG(o) ir1_opnd_base_reg_num(o)
#define XMM_LOAD128(o) latxs_load_freg128_from_ir1(o)
#define XMM_LOADFP1(o) latxs_load_freg_from_ir1_1(o, true)

#define SHBR_ACTIVE_32(ir1) \
    (option_shbr && lsenv->tr_data->is_tu_mode && LATXS_SHBR_ON_32(ir1))
#define SHBR_ACTIVE_64(ir1) \
    (option_shbr && lsenv->tr_data->is_tu_mode && LATXS_SHBR_ON_64(ir1))

static void shbr_write_scalar32(IR1_INST *ir1, IR2_OPND *dest,
                                IR2_OPND *value)
{
    if (SHBR_ACTIVE_32(ir1)) {
        latxs_append_ir2_opnd2(LISA_FMOV_S, dest, value);
    } else if (option_enable_lasx) {
        latxs_append_ir2_opnd2i(LISA_XVINSVE0_W, dest, value, 0);
    } else {
        latxs_append_ir2_opnd2i(LISA_VEXTRINS_W, dest, value, 0);
    }
}

static void shbr_write_scalar64(IR1_INST *ir1, IR2_OPND *dest,
                                IR2_OPND *value)
{
    if (SHBR_ACTIVE_64(ir1)) {
        latxs_append_ir2_opnd2(LISA_FMOV_D, dest, value);
    } else if (option_enable_lasx) {
        latxs_append_ir2_opnd2i(LISA_XVINSVE0_D, dest, value, 0);
    } else {
        latxs_append_ir2_opnd2i(LISA_VEXTRINS_D, dest, value, 0);
    }
}

#define XMM_EXCP(pir1) do {                             \
    if (latxs_tr_gen_sse_common_excp_check(pir1)) {     \
        return true;                                    \
    }                                                   \
} while (0)

#define SIMD_FPE_CHECK (option_sigfpe && lsenv->tr_data->sys.mxcsr_on)

void latxs_tr_gen_simd_fpe_check_t(IR1_INST *pir1, IR2_OPND *dest_t)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    if (!SIMD_FPE_CHECK) return;

    /*
    MXCSR :
    FTZ|  RC |PM|UM|OM|ZM|DM|IM|  |PE|UE|OE|ZE|DE|IE
    ┌──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┐
    │15│14│13│12│11│10| 9│ 8│ 7│ 6│ 5│ 4│ 3│ 2│ 1│ 0│
    └──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┘
    P <----> I
    U <----> U
    O <----> O
    Z <----> Z
    I <----> V

    FCSR :
          Cause               Flags                       Enables
      V| Z| O| U| I|     V| Z| O| U| I|     RM        V| Z| O| U| I|
    ┌──┬──┬──┬──┬──┬───┬──┬──┬──┬──┬──┬───┬──┬──┬───┬──┬──┬──┬──┬──┐
    │28│27│26│25│24│...|20│19│18│17│16│...│ 9│ 8│...│ 4│ 3│ 2│ 1│ 0│
    └──┴──┴──┴──┴──┴───┴──┴──┴──┴──┴──┴───┴──┴──┴───┴──┴──┴──┴──┴──┘
    */

    IR2_OPND excp_divz = latxs_ra_alloc_itemp();
    IR2_OPND excp = latxs_ir2_opnd_new_label();
    if(option_fast_masked_v) {
        latxs_append_ir2_opnd2i(LISA_LD_WU, &excp_divz, &latxs_env_ir2_opnd, lsenv_offset_of_excp_divz(lsenv));
        latxs_append_ir2_opnd3(LISA_BNE, &excp_divz, &latxs_zero_ir2_opnd, &excp);
    }
    latxs_ra_free_temp(&excp_divz);

    IR2_OPND env_mxcsr = latxs_ra_alloc_itemp();
    IR2_OPND fcsr1 = latxs_ra_alloc_itemp();
    IR2_OPND temp = latxs_ra_alloc_itemp();
    latxs_append_ir2_opnd2i(LISA_LD_WU, &env_mxcsr, &latxs_env_ir2_opnd, lsenv_offset_of_mxcsr(lsenv));
    /* PM */
    latxs_append_ir2_opnd2ii(LISA_BSTRPICK_W, &temp, &env_mxcsr, 12, 12);
    latxs_append_ir2_opnd2ii(LISA_BSTRINS_W, &fcsr1, &temp, 0, 0);
    /* UM */
    latxs_append_ir2_opnd2ii(LISA_BSTRPICK_W, &temp, &env_mxcsr, 11, 11);
    latxs_append_ir2_opnd2ii(LISA_BSTRINS_W, &fcsr1, &temp, 1, 1);
    /* OM */
    latxs_append_ir2_opnd2ii(LISA_BSTRPICK_W, &temp, &env_mxcsr, 10, 10);
    latxs_append_ir2_opnd2ii(LISA_BSTRINS_W, &fcsr1, &temp, 2, 2);
    /* ZM */
    latxs_append_ir2_opnd2ii(LISA_BSTRPICK_W, &temp, &env_mxcsr, 9, 9);
    latxs_append_ir2_opnd2ii(LISA_BSTRINS_W, &fcsr1, &temp, 3, 3);
    /* IM */
    latxs_append_ir2_opnd2ii(LISA_BSTRPICK_W, &temp, &env_mxcsr, 7, 7);
    latxs_append_ir2_opnd2ii(LISA_BSTRINS_W, &fcsr1, &temp, 4, 4);

    latxs_append_ir2_opnd3(LISA_NOR, &fcsr1, &latxs_zero_ir2_opnd, &fcsr1);
    latxs_append_ir2_opnd2ii(LISA_BSTRPICK_W, &fcsr1, &fcsr1, 4, 0);
    /*fcsr2-flags*/
    latxs_append_ir2_opnd2(LISA_MOVFCSR2GR, &temp, &latxs_fcsr2_ir2_opnd);
    latxs_append_ir2_opnd2i(LISA_SRLI_W, &temp, &temp, 24);

    if ((ir1_opcode(pir1) == X86_INS_ROUNDPD || ir1_opcode(pir1) == X86_INS_ROUNDPS ||
            ir1_opcode(pir1) == X86_INS_ROUNDSD || ir1_opcode(pir1) == X86_INS_ROUNDSS) &&
            (ir1_opnd_uimm(ir1_get_opnd(pir1, 2)) & 0x8)) {
        /*if imm[3] = 1,then the Precision Mask in the MXSCSR is ignored and precision exception is not signaled.*/
        latxs_append_ir2_opnd2ii(LISA_BSTRINS_W, &temp, &latxs_zero_ir2_opnd, 24, 24);
    }

    latxs_append_ir2_opnd3(LISA_AND, &temp, &temp, &fcsr1);

    latxs_ra_free_temp(&fcsr1);
    IR2_OPND exit = latxs_ir2_opnd_new_label();
    latxs_append_ir2_opnd3(LISA_BEQ, &temp, &latxs_zero_ir2_opnd, &exit);

    if (dest_t) {
        lsassert(dest_t->_type == IR2_OPND_FPR);
        IR2_OPND dest = XMM_LOAD128(ir1_get_opnd(pir1, 0));
        if(option_enable_lasx) {
            latxs_append_ir2_opnd3(LISA_XVOR_V, &dest, dest_t, dest_t);
        } else {
            latxs_append_ir2_opnd3(LISA_VOR_V, &dest, dest_t, dest_t);
        }
        latxs_ra_free_temp(dest_t);
    }

    IR2_OPND fcsr = latxs_ra_alloc_itemp();
    latxs_append_ir2_opnd2(LISA_MOVFCSR2GR, &fcsr, &latxs_fcsr2_ir2_opnd);
    /* PE */
    latxs_append_ir2_opnd2ii(LISA_BSTRPICK_W, &temp, &fcsr, 24, 24);
    latxs_append_ir2_opnd2ii(LISA_BSTRINS_W, &env_mxcsr, &temp, 5, 5);
    /* UE */
    latxs_append_ir2_opnd2ii(LISA_BSTRPICK_W, &temp, &fcsr, 25, 25);
    latxs_append_ir2_opnd2ii(LISA_BSTRINS_W, &env_mxcsr, &temp, 4, 4);
    /* OE */
    latxs_append_ir2_opnd2ii(LISA_BSTRPICK_W, &temp, &fcsr, 26, 26);
    latxs_append_ir2_opnd2ii(LISA_BSTRINS_W, &env_mxcsr, &temp, 3, 3);
    /* ZE */
    latxs_append_ir2_opnd2ii(LISA_BSTRPICK_W, &temp, &fcsr, 27, 27);
    latxs_append_ir2_opnd2ii(LISA_BSTRINS_W, &env_mxcsr, &temp, 2, 2);
    /* IE */
    latxs_append_ir2_opnd2ii(LISA_BSTRPICK_W, &temp, &fcsr, 28, 28);
    latxs_append_ir2_opnd2ii(LISA_BSTRINS_W, &env_mxcsr, &temp, 0, 0);
    latxs_append_ir2_opnd2i(LISA_ST_W, &env_mxcsr, &latxs_env_ir2_opnd, lsenv_offset_of_mxcsr(lsenv));
    latxs_ra_free_temp(&temp);
    latxs_ra_free_temp(&env_mxcsr);
    latxs_ra_free_temp(&fcsr);

    //latxs_append_ir2_opnd2(LISA_MOVGR2FCSR, &latxs_fcsr2_ir2_opnd, &latxs_zero_ir2_opnd);
    latxs_append_ir2_opnd1(LISA_LABEL, &excp);
    td->force_curr_top_save_bak = td->force_curr_top_save;
    td->force_curr_top_save = 1;
    td->ignore_rcd_curr_top = 1;
    latxs_tr_gen_excp_divz(pir1, 0);
    td->force_curr_top_save = td->force_curr_top_save_bak;
    td->ignore_rcd_curr_top = 0;

    latxs_append_ir2_opnd1(LISA_LABEL, &exit);
}

bool latxs_translate_por(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    if (ir1_opnd_is_xmm(opnd0)) {
        IR2_OPND dest = XMM_LOAD128(opnd0);
        IR2_OPND src  = XMM_LOAD128(opnd1);
        latxs_append_ir2_opnd3(LISA_VOR_V, &dest, &dest, &src);
        return true;
    }

    IR2_OPND dest_lo = XMM_LOADFP1(opnd0);
    IR2_OPND src_lo  = XMM_LOADFP1(opnd1);
    latxs_append_ir2_opnd3(LISA_VOR_V, &dest_lo, &dest_lo, &src_lo);

    return true;
}

bool latxs_translate_pxor(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    if (ir1_opnd_is_xmm(opnd0) && ir1_opnd_is_mem(opnd1)) {
        IR2_OPND temp = latxs_ra_alloc_ftemp();
        latxs_load_freg128_from_ir1_mem(&temp, opnd1);
        IR2_OPND xmm_dest = latxs_ra_alloc_xmm(XMM_REG(opnd0));
        latxs_append_ir2_opnd3(LISA_VXOR_V, &xmm_dest, &xmm_dest, &temp);
        return true;
    } else if (ir1_opnd_is_xmm(opnd0) && ir1_opnd_is_xmm(opnd1)) {
        IR2_OPND xmm_dest = latxs_ra_alloc_xmm(XMM_REG(opnd0));
        IR2_OPND xmm_src  = latxs_ra_alloc_xmm(XMM_REG(opnd1));
        latxs_append_ir2_opnd3(LISA_VXOR_V, &xmm_dest, &xmm_dest, &xmm_src);
        return true;
    }

    IR2_OPND dest_lo = XMM_LOADFP1(opnd0);
    IR2_OPND src_lo  = XMM_LOADFP1(opnd1);

    latxs_append_ir2_opnd3(LISA_VXOR_V, &dest_lo, &dest_lo, &src_lo);

    return true;
}

bool latxs_translate_packuswb(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    if (ir1_opnd_is_xmm(opnd0)) {
        IR2_OPND dest = XMM_LOAD128(opnd0);
        IR2_OPND src  = XMM_LOAD128(opnd1);
        if (ir1_opnd_is_xmm(opnd1) &&
                (XMM_REG(opnd0) == XMM_REG(opnd1))) {
            latxs_append_ir2_opnd2i(LISA_VSSRANI_BU_H, &dest, &dest, 0);
            latxs_append_ir2_opnd2i(LISA_VEXTRINS_D, &dest, &dest,
                    VEXTRINS_IMM_4_0(1, 0));
        } else {
            latxs_append_ir2_opnd2i(LISA_VSSRANI_BU_H, &dest, &dest, 0);
            IR2_OPND temp = latxs_ra_alloc_ftemp();
            latxs_append_ir2_opnd2i(LISA_VSSRANI_BU_H, &temp, &src, 0);
            latxs_append_ir2_opnd2i(LISA_VEXTRINS_D, &dest, &temp,
                    VEXTRINS_IMM_4_0(1, 0));
        }
    } else {
        IR2_OPND dest = XMM_LOADFP1(opnd0);
        IR2_OPND src  = XMM_LOADFP1(opnd1);

        latxs_append_ir2_opnd3(LISA_VPACKEV_D, &dest, &src, &dest);
        latxs_append_ir2_opnd2i(LISA_VSSRANI_BU_H, &dest, &dest, 0);
    }

    return true;
}

bool latxs_translate_packsswb(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    if (ir1_opnd_is_xmm(opnd0)) {
        IR2_OPND dest = XMM_LOAD128(opnd0);
        IR2_OPND src  = XMM_LOAD128(opnd1);
        if (ir1_opnd_is_xmm(opnd1) &&
                (XMM_REG(opnd0) == XMM_REG(opnd1))) {
            latxs_append_ir2_opnd2i(LISA_VSSRANI_B_H, &dest, &dest, 0);
        } else {
            latxs_append_ir2_opnd2i(LISA_VSSRANI_B_H, &dest, &dest, 0);
            IR2_OPND temp = latxs_ra_alloc_ftemp();
            latxs_append_ir2_opnd2i(LISA_VSSRANI_B_H, &temp, &src, 0);
            latxs_append_ir2_opnd2i(LISA_VEXTRINS_D, &dest, &temp,
                    VEXTRINS_IMM_4_0(1, 0));
        }
    } else {
        IR2_OPND dest = XMM_LOADFP1(opnd0);
        IR2_OPND src  = XMM_LOADFP1(opnd1);

        latxs_append_ir2_opnd3(LISA_VPACKEV_D, &dest, &src, &dest);
        latxs_append_ir2_opnd2i(LISA_VSSRANI_B_H, &dest, &dest, 0);
    }

    return true;
}

bool latxs_translate_packssdw(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    if (ir1_opnd_is_xmm(opnd0)) {
        IR2_OPND dest = XMM_LOAD128(opnd0);
        IR2_OPND src  = XMM_LOAD128(opnd1);
        if (ir1_opnd_is_xmm(opnd1) &&
                (XMM_REG(opnd0) == XMM_REG(opnd1))) {
            latxs_append_ir2_opnd2i(LISA_VSSRANI_H_W, &dest, &dest, 0);
        } else {
            latxs_append_ir2_opnd2i(LISA_VSSRANI_H_W, &dest, &dest, 0);
            IR2_OPND temp = latxs_ra_alloc_ftemp();
            latxs_append_ir2_opnd2i(LISA_VSSRANI_H_W, &temp, &src, 0);
            latxs_append_ir2_opnd2i(LISA_VEXTRINS_D, &dest, &temp,
                    VEXTRINS_IMM_4_0(1, 0));
        }
    } else {
        IR2_OPND dest = XMM_LOADFP1(opnd0);
        IR2_OPND src  = XMM_LOADFP1(opnd1);

        latxs_append_ir2_opnd3(LISA_VPACKEV_D, &dest, &src, &dest);
        latxs_append_ir2_opnd2i(LISA_VSSRANI_H_W, &dest, &dest, 0);
    }

    return true;
}

bool latxs_translate_paddb(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    if (ir1_opnd_is_xmm(opnd0)) {
        IR2_OPND dest = XMM_LOAD128(opnd0);
        IR2_OPND src  = XMM_LOAD128(opnd1);
        latxs_append_ir2_opnd3(LISA_VADD_B, &dest, &dest, &src);
        return true;
    }

    IR2_OPND dest_lo = XMM_LOADFP1(opnd0);
    IR2_OPND src_lo  = XMM_LOADFP1(opnd1);
    latxs_append_ir2_opnd3(LISA_VADD_B, &dest_lo, &dest_lo, &src_lo);

    return true;
}

bool latxs_translate_paddw(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    if (ir1_opnd_is_xmm(opnd0)) {
        IR2_OPND dest = XMM_LOAD128(opnd0);
        IR2_OPND src  = XMM_LOAD128(opnd1);
        latxs_append_ir2_opnd3(LISA_VADD_H, &dest, &dest, &src);
        return true;
    }

    IR2_OPND dest_lo = XMM_LOADFP1(opnd0);
    IR2_OPND src_lo  = XMM_LOADFP1(opnd1);
    latxs_append_ir2_opnd3(LISA_VADD_H, &dest_lo, &dest_lo, &src_lo);
    return true;
}

bool latxs_translate_paddd(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    if (ir1_opnd_is_xmm(opnd0)) {
        IR2_OPND dest = XMM_LOAD128(opnd0);
        IR2_OPND src  = XMM_LOAD128(opnd1);
        latxs_append_ir2_opnd3(LISA_VADD_W, &dest, &dest, &src);
        return true;
    }

    IR2_OPND dest_lo = XMM_LOADFP1(opnd0);
    IR2_OPND src_lo  = XMM_LOADFP1(opnd1);
    latxs_append_ir2_opnd3(LISA_VADD_W, &dest_lo, &dest_lo, &src_lo);
    return true;
}

bool latxs_translate_paddusb(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    if (ir1_opnd_is_xmm(opnd0)) {
        IR2_OPND dest = XMM_LOAD128(opnd0);
        IR2_OPND src  = XMM_LOAD128(opnd1);
        latxs_append_ir2_opnd3(LISA_VSADD_BU, &dest, &dest, &src);
        return true;
    }

    IR2_OPND dest_lo = XMM_LOADFP1(opnd0);
    IR2_OPND src_lo  = XMM_LOADFP1(opnd1);
    latxs_append_ir2_opnd3(LISA_VSADD_BU, &dest_lo, &dest_lo, &src_lo);

    return true;
}

bool latxs_translate_paddusw(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    if (ir1_opnd_is_xmm(opnd0)) {
        IR2_OPND dest = XMM_LOAD128(opnd0);
        IR2_OPND src  = XMM_LOAD128(opnd1);
        latxs_append_ir2_opnd3(LISA_VSADD_HU, &dest, &dest, &src);
        return true;
    }

    IR2_OPND dest_lo = XMM_LOADFP1(opnd0);
    IR2_OPND src_lo  = XMM_LOADFP1(opnd1);
    latxs_append_ir2_opnd3(LISA_VSADD_HU, &dest_lo, &dest_lo, &src_lo);

    return true;
}

bool latxs_translate_paddsw(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    if (ir1_opnd_is_xmm(opnd0)) {
        IR2_OPND dest = XMM_LOAD128(opnd0);
        IR2_OPND src  = XMM_LOAD128(opnd1);
        latxs_append_ir2_opnd3(LISA_VSADD_H, &dest, &dest, &src);
        return true;
    }

    IR2_OPND dest_lo = XMM_LOADFP1(opnd0);
    IR2_OPND src_lo  = XMM_LOADFP1(opnd1);
    latxs_append_ir2_opnd3(LISA_VSADD_H, &dest_lo, &dest_lo, &src_lo);

    return true;
}

bool latxs_translate_paddsb(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    if (ir1_opnd_is_xmm(opnd0)) {
        IR2_OPND dest = XMM_LOAD128(opnd0);
        IR2_OPND src  = XMM_LOAD128(opnd1);
        latxs_append_ir2_opnd3(LISA_VSADD_B, &dest, &dest, &src);
        return true;
    }

    IR2_OPND dest_lo = XMM_LOADFP1(opnd0);
    IR2_OPND src_lo  = XMM_LOADFP1(opnd1);
    latxs_append_ir2_opnd3(LISA_VSADD_B, &dest_lo, &dest_lo, &src_lo);

    return true;
}

bool latxs_translate_psubsb(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    IR2_OPND dest = latxs_load_freg_from_ir1_1(opnd0, false);
    IR2_OPND src = latxs_load_freg_from_ir1_1(opnd1, false);

    latxs_append_ir2_opnd3(LISA_VSSUB_B, &dest, &dest, &src);

    return true;
}

bool latxs_translate_psubsw(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    IR2_OPND dest = latxs_load_freg_from_ir1_1(opnd0, false);
    IR2_OPND src = latxs_load_freg_from_ir1_1(opnd1, false);

    latxs_append_ir2_opnd3(LISA_VSSUB_H, &dest, &dest, &src);

    return true;
}

bool latxs_translate_psubusb(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    IR2_OPND dest = latxs_load_freg_from_ir1_1(opnd0, false);
    IR2_OPND src = latxs_load_freg_from_ir1_1(opnd1, false);

    latxs_append_ir2_opnd3(LISA_VSSUB_BU, &dest, &dest, &src);

    return true;
}

bool latxs_translate_psubusw(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    IR2_OPND dest = latxs_load_freg_from_ir1_1(opnd0, false);
    IR2_OPND src = latxs_load_freg_from_ir1_1(opnd1, false);

    latxs_append_ir2_opnd3(LISA_VSSUB_HU, &dest, &dest, &src);

    return true;
}

bool latxs_translate_pand(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    if (ir1_opnd_is_xmm(opnd0)) {
        IR2_OPND dest = XMM_LOAD128(opnd0);
        IR2_OPND src  = XMM_LOAD128(opnd1);
        latxs_append_ir2_opnd3(LISA_VAND_V, &dest, &dest, &src);
        return true;
    }

    IR2_OPND dest_lo = XMM_LOADFP1(opnd0);
    IR2_OPND src_lo  = XMM_LOADFP1(opnd1);
    latxs_append_ir2_opnd3(LISA_VAND_V, &dest_lo, &dest_lo, &src_lo);
    return true;
}

bool latxs_translate_pandn(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    if (ir1_opnd_is_xmm(opnd0)) {
        IR2_OPND dest = XMM_LOAD128(opnd0);
        IR2_OPND src  = XMM_LOAD128(opnd1);
        latxs_append_ir2_opnd3(LISA_VANDN_V, &dest, &dest, &src);
        return true;
    }

    IR2_OPND dest_lo = XMM_LOADFP1(opnd0);
    IR2_OPND src_lo  = XMM_LOADFP1(opnd1);
    latxs_append_ir2_opnd3(LISA_VANDN_V, &dest_lo, &dest_lo, &src_lo);
    return true;
}

bool latxs_translate_pcmpeqb(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    if (ir1_opnd_is_xmm(opnd0)) {
        IR2_OPND dest = XMM_LOAD128(opnd0);
        IR2_OPND src  = XMM_LOAD128(opnd1);
        latxs_append_ir2_opnd3(LISA_VSEQ_B, &dest, &dest, &src);
    } else {
        IR2_OPND dest = XMM_LOADFP1(opnd0);
        IR2_OPND src  = XMM_LOADFP1(opnd1);
        latxs_append_ir2_opnd3(LISA_VSEQ_B, &dest, &dest, &src);
    }

    return true;
}

bool latxs_translate_pcmpeqw(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    if (ir1_opnd_is_xmm(opnd0)) {
        IR2_OPND dest = XMM_LOAD128(opnd0);
        IR2_OPND src  = XMM_LOAD128(opnd1);
        latxs_append_ir2_opnd3(LISA_VSEQ_H, &dest, &dest, &src);
    } else {
        IR2_OPND dest = XMM_LOADFP1(opnd0);
        IR2_OPND src  = XMM_LOADFP1(opnd1);
        latxs_append_ir2_opnd3(LISA_VSEQ_H, &dest, &dest, &src);
    }

    return true;
}

bool latxs_translate_pcmpeqd(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    if (ir1_opnd_is_xmm(opnd0)) {
        IR2_OPND dest = XMM_LOAD128(opnd0);
        IR2_OPND src  = XMM_LOAD128(opnd1);
        latxs_append_ir2_opnd3(LISA_VSEQ_W, &dest, &dest, &src);
    } else {
        IR2_OPND dest = XMM_LOADFP1(opnd0);
        IR2_OPND src  = XMM_LOADFP1(opnd1);
        latxs_append_ir2_opnd3(LISA_VSEQ_W, &dest, &dest, &src);
    }

    return true;
}

bool latxs_translate_pcmpgtb(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    if (ir1_opnd_is_xmm(opnd0)) {
        IR2_OPND dest = XMM_LOAD128(opnd0);
        IR2_OPND src  = XMM_LOAD128(opnd1);
        latxs_append_ir2_opnd3(LISA_VSLT_B, &dest, &src, &dest);
    } else {
        IR2_OPND dest = XMM_LOADFP1(opnd0);
        IR2_OPND src  = XMM_LOADFP1(opnd1);
        latxs_append_ir2_opnd3(LISA_VSLT_B, &dest, &src, &dest);
    }

    return true;
}

bool latxs_translate_pcmpgtw(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    if (ir1_opnd_is_xmm(opnd0)) {
        IR2_OPND dest = XMM_LOAD128(opnd0);
        IR2_OPND src  = XMM_LOAD128(opnd1);
        latxs_append_ir2_opnd3(LISA_VSLT_H, &dest, &src, &dest);
    } else {
        IR2_OPND dest = XMM_LOADFP1(opnd0);
        IR2_OPND src  = XMM_LOADFP1(opnd1);
        latxs_append_ir2_opnd3(LISA_VSLT_H, &dest, &src, &dest);
    }

    return true;
}

bool latxs_translate_pcmpgtd(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    if (ir1_opnd_is_xmm(opnd0)) {
        IR2_OPND dest = XMM_LOAD128(opnd0);
        IR2_OPND src  = XMM_LOAD128(opnd1);
        latxs_append_ir2_opnd3(LISA_VSLT_W, &dest, &src, &dest);
    } else {
        IR2_OPND dest = XMM_LOADFP1(opnd0);
        IR2_OPND src  = XMM_LOADFP1(opnd1);
        latxs_append_ir2_opnd3(LISA_VSLT_W, &dest, &src, &dest);
    }

    return true;
}

bool latxs_translate_pmullw(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    if (ir1_opnd_is_xmm(opnd0)) {
        IR2_OPND dest = XMM_LOAD128(opnd0);
        IR2_OPND src  = XMM_LOAD128(opnd1);
        latxs_append_ir2_opnd3(LISA_VMUL_H, &dest, &dest, &src);
    } else {
        IR2_OPND dest = XMM_LOADFP1(opnd0);
        IR2_OPND src  = XMM_LOADFP1(opnd1);
        latxs_append_ir2_opnd3(LISA_VMUL_H, &dest, &dest, &src);
    }

    return true;
}

bool latxs_translate_psllw(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    IR2_OPND dest = latxs_load_freg_from_ir1_1(opnd0, false);
    if (ir1_opnd_is_mmx(opnd1) || ir1_opnd_is_xmm(opnd1)
                               || ir1_opnd_is_mem(opnd1)) {
        IR2_OPND src = latxs_load_freg_from_ir1_1(opnd1, false);
        IR2_OPND temp1 = latxs_ra_alloc_ftemp();
        IR2_OPND temp2 = latxs_ra_alloc_ftemp();

        latxs_append_ir2_opnd2i(LISA_VREPLVEI_D, &temp1, &src, 0);
        latxs_append_ir2_opnd2i(LISA_VSLEI_DU, &temp2, &temp1, 15);
        latxs_append_ir2_opnd2i(LISA_VREPLVEI_H, &temp1, &src, 0);
        latxs_append_ir2_opnd3(LISA_VSLL_H, &dest, &dest, &temp1);
        latxs_append_ir2_opnd3(LISA_VAND_V, &dest, &dest, &temp2);

        latxs_ra_free_temp(&temp1);
        latxs_ra_free_temp(&temp2);
    } else if (ir1_opnd_is_imm(opnd1)) {
        uint8_t imm = ir1_opnd_uimm(opnd1);
        if (imm > 15) {
            latxs_append_ir2_opnd3(LISA_VXOR_V, &dest, &dest, &dest);
        } else {
            latxs_append_ir2_opnd2i(LISA_VSLLI_H, &dest, &dest, imm);
        }
    } else {
        lsassert(0);
    }
    return true;
}

bool latxs_translate_pslld(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    IR2_OPND dest = latxs_load_freg_from_ir1_1(opnd0, false);
    if (ir1_opnd_is_mmx(opnd1) || ir1_opnd_is_xmm(opnd1)
                               || ir1_opnd_is_mem(opnd1)) {
        IR2_OPND src = latxs_load_freg_from_ir1_1(opnd1, false);
        IR2_OPND temp1 = latxs_ra_alloc_ftemp();
        IR2_OPND temp2 = latxs_ra_alloc_ftemp();

        latxs_append_ir2_opnd2i(LISA_VREPLVEI_D, &temp1, &src, 0);
        latxs_append_ir2_opnd2i(LISA_VSLEI_DU, &temp2, &temp1, 31);
        latxs_append_ir2_opnd2i(LISA_VREPLVEI_W, &temp1, &src, 0);
        latxs_append_ir2_opnd3(LISA_VSLL_W, &dest, &dest, &temp1);
        latxs_append_ir2_opnd3(LISA_VAND_V, &dest, &dest, &temp2);

        latxs_ra_free_temp(&temp1);
        latxs_ra_free_temp(&temp2);
    } else if (ir1_opnd_is_imm(opnd1)) {
        uint8_t imm = ir1_opnd_uimm(opnd1);
        if (imm > 31) {
            latxs_append_ir2_opnd3(LISA_VXOR_V, &dest, &dest, &dest);
        } else {
            latxs_append_ir2_opnd2i(LISA_VSLLI_W, &dest, &dest, imm);
        }
    } else {
        lsassert(0);
    }
    return true;
}

bool latxs_translate_psllq(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    IR2_OPND dest = latxs_load_freg_from_ir1_1(opnd0, false);
    if (ir1_opnd_is_mmx(opnd1) || ir1_opnd_is_xmm(opnd1)
                               || ir1_opnd_is_mem(opnd1)) {
        IR2_OPND src = latxs_load_freg_from_ir1_1(opnd1, false);
        IR2_OPND temp1 = latxs_ra_alloc_ftemp();
        IR2_OPND temp2 = latxs_ra_alloc_ftemp();
        IR2_OPND temp3 = latxs_ra_alloc_ftemp();

        latxs_append_ir2_opnd2i(LISA_VREPLVEI_D, &temp1, &src, 0);
        latxs_append_ir2_opnd1i(LISA_VLDI, &temp3, VLDI_IMM_TYPE0(3, 63));
        latxs_append_ir2_opnd3(LISA_VSLE_DU, &temp2, &temp1, &temp3);
        latxs_append_ir2_opnd2i(LISA_VREPLVEI_D, &temp1, &src, 0);
        latxs_append_ir2_opnd3(LISA_VSLL_D, &dest, &dest, &temp1);
        latxs_append_ir2_opnd3(LISA_VAND_V, &dest, &dest, &temp2);

        latxs_ra_free_temp(&temp1);
        latxs_ra_free_temp(&temp2);
        latxs_ra_free_temp(&temp3);
    } else if (ir1_opnd_is_imm(opnd1)) {
        uint8_t imm = ir1_opnd_uimm(opnd1);
        if (imm > 63) {
            latxs_append_ir2_opnd3(LISA_VXOR_V, &dest, &dest, &dest);
        } else {
            latxs_append_ir2_opnd2i(LISA_VSLLI_D, &dest, &dest, imm);
        }
    } else {
        lsassert(0);
    }
    return true;
}

bool latxs_translate_psrlw(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    IR2_OPND dest = latxs_load_freg_from_ir1_1(opnd0, false);
    if (ir1_opnd_is_mmx(opnd1) || ir1_opnd_is_xmm(opnd1)
                               || ir1_opnd_is_mem(opnd1)) {
        IR2_OPND src = latxs_load_freg_from_ir1_1(opnd1, false);
        IR2_OPND temp1 = latxs_ra_alloc_ftemp();
        IR2_OPND temp2 = latxs_ra_alloc_ftemp();

        latxs_append_ir2_opnd2i(LISA_VREPLVEI_D, &temp1, &src, 0);
        latxs_append_ir2_opnd2i(LISA_VSLEI_DU, &temp2, &temp1, 15);
        latxs_append_ir2_opnd2i(LISA_VREPLVEI_H, &temp1, &src, 0);
        latxs_append_ir2_opnd3(LISA_VSRL_H, &dest, &dest, &temp1);
        latxs_append_ir2_opnd3(LISA_VAND_V, &dest, &dest, &temp2);

        latxs_ra_free_temp(&temp1);
        latxs_ra_free_temp(&temp2);
    } else if (ir1_opnd_is_imm(opnd1)) {
        uint8_t imm = ir1_opnd_uimm(opnd1);
        if (imm > 15) {
            latxs_append_ir2_opnd3(LISA_VXOR_V, &dest, &dest, &dest);
        } else {
            latxs_append_ir2_opnd2i(LISA_VSRLI_H, &dest, &dest, imm);
        }
    } else {
        lsassert(0);
    }
    return true;
}

bool latxs_translate_psrld(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    IR2_OPND dest = latxs_load_freg_from_ir1_1(opnd0, false);
    if (ir1_opnd_is_mmx(opnd1) || ir1_opnd_is_xmm(opnd1)
                               || ir1_opnd_is_mem(opnd1)) {
        IR2_OPND src = latxs_load_freg_from_ir1_1(opnd1, false);
        IR2_OPND temp1 = latxs_ra_alloc_ftemp();
        IR2_OPND temp2 = latxs_ra_alloc_ftemp();

        latxs_append_ir2_opnd2i(LISA_VREPLVEI_D, &temp1, &src, 0);
        latxs_append_ir2_opnd2i(LISA_VSLEI_DU, &temp2, &temp1, 31);
        latxs_append_ir2_opnd2i(LISA_VREPLVEI_W, &temp1, &src, 0);
        latxs_append_ir2_opnd3(LISA_VSRL_W, &dest, &dest, &temp1);
        latxs_append_ir2_opnd3(LISA_VAND_V, &dest, &dest, &temp2);

        latxs_ra_free_temp(&temp1);
        latxs_ra_free_temp(&temp2);
    } else if (ir1_opnd_is_imm(opnd1)) {
        uint8_t imm = ir1_opnd_uimm(opnd1);
        if (imm > 31) {
            latxs_append_ir2_opnd3(LISA_VXOR_V, &dest, &dest, &dest);
        } else {
            latxs_append_ir2_opnd2i(LISA_VSRLI_W, &dest, &dest, imm);
        }
    } else {
        lsassert(0);
    }
    return true;
}

bool latxs_translate_psrlq(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    IR2_OPND dest = latxs_load_freg_from_ir1_1(opnd0, false);
    if (ir1_opnd_is_mmx(opnd1) || ir1_opnd_is_xmm(opnd1)
                               || ir1_opnd_is_mem(opnd1)) {
        IR2_OPND src = latxs_load_freg_from_ir1_1(opnd1, false);
        IR2_OPND temp1 = latxs_ra_alloc_ftemp();
        IR2_OPND temp2 = latxs_ra_alloc_ftemp();
        IR2_OPND temp3 = latxs_ra_alloc_ftemp();

        latxs_append_ir2_opnd2i(LISA_VREPLVEI_D, &temp1, &src, 0);
        latxs_append_ir2_opnd1i(LISA_VLDI, &temp3, VLDI_IMM_TYPE0(3, 63));
        latxs_append_ir2_opnd3(LISA_VSLE_DU, &temp2, &temp1, &temp3);
        latxs_append_ir2_opnd2i(LISA_VREPLVEI_D, &temp1, &src, 0);
        latxs_append_ir2_opnd3(LISA_VSRL_D, &dest, &dest, &temp1);
        latxs_append_ir2_opnd3(LISA_VAND_V, &dest, &dest, &temp2);

        latxs_ra_free_temp(&temp1);
        latxs_ra_free_temp(&temp2);
        latxs_ra_free_temp(&temp3);
    } else if (ir1_opnd_is_imm(opnd1)) {
        uint8_t imm = ir1_opnd_uimm(opnd1);
        if (imm > 63) {
            latxs_append_ir2_opnd3(LISA_VXOR_V, &dest, &dest, &dest);
        } else {
            latxs_append_ir2_opnd2i(LISA_VSRLI_D, &dest, &dest, imm);
        }
    } else {
        lsassert(0);
    }
    return true;
}

bool latxs_translate_psraw(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    IR2_OPND dest = latxs_load_freg_from_ir1_1(opnd0, false);
    if (ir1_opnd_is_mmx(opnd1) || ir1_opnd_is_xmm(opnd1)
                               || ir1_opnd_is_mem(opnd1)) {
        IR2_OPND src = latxs_load_freg_from_ir1_1(opnd1, false);
        IR2_OPND temp1 = latxs_ra_alloc_ftemp();
        IR2_OPND temp2 = latxs_ra_alloc_ftemp();
        IR2_OPND temp3 = latxs_ra_alloc_ftemp();
        IR2_OPND temp4 = latxs_ra_alloc_ftemp();
        latxs_append_ir2_opnd2i(LISA_VREPLVEI_D, &temp1, &src, 0);
        latxs_append_ir2_opnd2i(LISA_VSLTI_DU, &temp2, &temp1, 15);
        latxs_append_ir2_opnd2i(LISA_VSRAI_H, &temp4, &dest, 15);
        latxs_append_ir2_opnd2i(LISA_VREPLVEI_H, &temp3, &src, 0);
        latxs_append_ir2_opnd3(LISA_VSRA_H, &dest, &dest, &temp3);
        latxs_append_ir2_opnd4(LISA_VBITSEL_V, &dest, &temp4, &dest, &temp2);
        latxs_ra_free_temp(&temp1);
        latxs_ra_free_temp(&temp2);
        latxs_ra_free_temp(&temp3);
        latxs_ra_free_temp(&temp4);
    } else if (ir1_opnd_is_imm(opnd1)) {
        uint8_t imm = ir1_opnd_uimm(opnd1);
        if (imm > 15) {
            imm = 15;
        }
        latxs_append_ir2_opnd2i(LISA_VSRAI_H, &dest, &dest, imm);
    } else {
        lsassert(0);
    }
    return true;
}

bool latxs_translate_psrad(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    IR2_OPND dest = latxs_load_freg_from_ir1_1(opnd0, false);
    if (ir1_opnd_is_mmx(opnd1) || ir1_opnd_is_xmm(opnd1)
                               || ir1_opnd_is_mem(opnd1)) {
        IR2_OPND src = latxs_load_freg_from_ir1_1(opnd1, false);
        IR2_OPND temp1 = latxs_ra_alloc_ftemp();
        IR2_OPND temp2 = latxs_ra_alloc_ftemp();
        IR2_OPND temp3 = latxs_ra_alloc_ftemp();
        IR2_OPND temp4 = latxs_ra_alloc_ftemp();
        latxs_append_ir2_opnd2i(LISA_VREPLVEI_D, &temp1, &src, 0);
        latxs_append_ir2_opnd2i(LISA_VSLTI_DU, &temp2, &temp1, 31);
        latxs_append_ir2_opnd2i(LISA_VSRAI_W, &temp4, &dest, 31);
        latxs_append_ir2_opnd2i(LISA_VREPLVEI_W, &temp3, &src, 0);
        latxs_append_ir2_opnd3(LISA_VSRA_W, &dest, &dest, &temp3);
        latxs_append_ir2_opnd4(LISA_VBITSEL_V, &dest, &temp4, &dest, &temp2);
        latxs_ra_free_temp(&temp1);
        latxs_ra_free_temp(&temp2);
        latxs_ra_free_temp(&temp3);
        latxs_ra_free_temp(&temp4);
    } else if (ir1_opnd_is_imm(opnd1)) {
        uint8_t imm = ir1_opnd_uimm(opnd1);
        if (imm > 31) {
            imm = 31;
        }
        latxs_append_ir2_opnd2i(LISA_VSRAI_W, &dest, &dest, imm);
    } else {
        lsassert(0);
    }
    return true;
}


bool latxs_translate_psubb(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    if (ir1_opnd_is_xmm(opnd0)) {
        IR2_OPND dest = XMM_LOAD128(opnd0);
        IR2_OPND src = XMM_LOAD128(opnd1);
        latxs_append_ir2_opnd3(LISA_VSUB_B, &dest, &dest, &src);
    } else { /* mmx */
        IR2_OPND dest = XMM_LOADFP1(opnd0);
        IR2_OPND src = XMM_LOADFP1(opnd1);
        latxs_append_ir2_opnd3(LISA_VSUB_B, &dest, &dest, &src);
    }
    return true;
}

bool latxs_translate_psubw(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    if (ir1_opnd_is_xmm(opnd0)) {
        IR2_OPND dest = XMM_LOAD128(opnd0);
        IR2_OPND src = XMM_LOAD128(opnd1);
        latxs_append_ir2_opnd3(LISA_VSUB_H, &dest, &dest, &src);
    } else { /* mmx */
        IR2_OPND dest = XMM_LOADFP1(opnd0);
        IR2_OPND src = XMM_LOADFP1(opnd1);
        latxs_append_ir2_opnd3(LISA_VSUB_H, &dest, &dest, &src);
    }
    return true;
}

bool latxs_translate_psubd(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    if (ir1_opnd_is_xmm(opnd0)) {
        IR2_OPND dest = XMM_LOAD128(opnd0);
        IR2_OPND src = XMM_LOAD128(opnd1);
        latxs_append_ir2_opnd3(LISA_VSUB_W, &dest, &dest, &src);
    } else { /* mmx */
        IR2_OPND dest = XMM_LOADFP1(opnd0);
        IR2_OPND src = XMM_LOADFP1(opnd1);
        latxs_append_ir2_opnd3(LISA_VSUB_W, &dest, &dest, &src);
    }
    return true;
}

bool latxs_translate_punpckhbw(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    if (ir1_opnd_is_xmm(opnd0)) {
        /* PUNPCKHBW xmm1, xmm2/m128 */
        IR2_OPND dest = XMM_LOAD128(opnd0);
        IR2_OPND src  = XMM_LOAD128(opnd1);
        latxs_append_ir2_opnd3(LISA_VILVH_B, &dest, &src, &dest);
    } else {
        /* PUNPCKHBW mm, mm/m64 */
        IR2_OPND dest = XMM_LOADFP1(opnd0);
        IR2_OPND src  = XMM_LOADFP1(opnd1);
        latxs_append_ir2_opnd3(LISA_VILVL_B, &dest, &src, &dest);
        latxs_append_ir2_opnd2i(LISA_VEXTRINS_D, &dest, &dest,
                                     VEXTRINS_IMM_4_0(0, 1));
    }

    return true;
}

bool latxs_translate_punpckhwd(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    if (ir1_opnd_is_xmm(opnd0)) {
        /* PUNPCKHWD xmm1, xmm2/m128 */
        IR2_OPND dest = XMM_LOAD128(opnd0);
        IR2_OPND src  = XMM_LOAD128(opnd1);
        latxs_append_ir2_opnd3(LISA_VILVH_H, &dest, &src, &dest);
    } else {
        /* PUNPCKHWD mm, mm/m64 */
        IR2_OPND dest = XMM_LOADFP1(opnd0);
        IR2_OPND src  = XMM_LOADFP1(opnd1);
        latxs_append_ir2_opnd3(LISA_VILVL_H, &dest, &src, &dest);
        latxs_append_ir2_opnd2i(LISA_VEXTRINS_D, &dest, &dest,
                                     VEXTRINS_IMM_4_0(0, 1));
    }

    return true;
}

bool latxs_translate_punpckhdq(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    if (ir1_opnd_is_xmm(opnd0)) {
        /* PUNPCKHDQ xmm1, xmm2/m128 */
        IR2_OPND dest = XMM_LOAD128(opnd0);
        IR2_OPND src  = XMM_LOAD128(opnd1);
        latxs_append_ir2_opnd3(LISA_VILVH_W, &dest, &src, &dest);
    } else {
        /* PUNPCKHDQ mm, mm/m64 */
        IR2_OPND dest = XMM_LOADFP1(opnd0);
        IR2_OPND src  = XMM_LOADFP1(opnd1);
        latxs_append_ir2_opnd3(LISA_VILVL_W, &dest, &src, &dest);
        latxs_append_ir2_opnd2i(LISA_VEXTRINS_D, &dest, &dest,
                                     VEXTRINS_IMM_4_0(0, 1));
    }

    return true;
}

bool latxs_translate_punpcklbw(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    if (ir1_opnd_is_xmm(opnd0)) {
        /* PUNPCKLBW xmm1, xmm2/m128 */
        IR2_OPND dest = XMM_LOAD128(opnd0);
        IR2_OPND src  = XMM_LOAD128(opnd1);
        latxs_append_ir2_opnd3(LISA_VILVL_B, &dest, &src, &dest);
    } else {
        /* PUNPCKLBW mm, mm/m32 */
        IR2_OPND dest = XMM_LOADFP1(opnd0);
        IR2_OPND src  = latxs_load_freg_from_ir1_1(opnd1, false);
        latxs_append_ir2_opnd3(LISA_VILVL_B, &dest, &src, &dest);
    }

    return true;
}

bool latxs_translate_punpcklwd(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    if (ir1_opnd_is_xmm(opnd0)) {
        /* PUNPCKLWD xmm1, xmm2/m128 */
        IR2_OPND dest = XMM_LOAD128(opnd0);
        IR2_OPND src  = XMM_LOAD128(opnd1);
        latxs_append_ir2_opnd3(LISA_VILVL_H, &dest, &src, &dest);
    } else {
        /* PUNPCKLWD mm, mm/m32 */
        IR2_OPND dest = XMM_LOADFP1(opnd0);
        IR2_OPND src  = latxs_load_freg_from_ir1_1(opnd1, false);
        latxs_append_ir2_opnd3(LISA_VILVL_H, &dest, &src, &dest);
    }

    return true;
}

bool latxs_translate_punpckldq(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    if (ir1_opnd_is_xmm(opnd0)) {
        /* PUNPCKLQDQ xmm1, xmm2/m128 */
        IR2_OPND dest = XMM_LOAD128(opnd0);
        IR2_OPND src  = XMM_LOAD128(opnd1);
        latxs_append_ir2_opnd3(LISA_VILVL_W, &dest, &src, &dest);
    } else {
        /*PUNPCKLDQ mm, mm/m32  */
        IR2_OPND dest = XMM_LOADFP1(opnd0);
        IR2_OPND src  = latxs_load_freg_from_ir1_1(opnd1, false);
        latxs_append_ir2_opnd3(LISA_VILVL_W, &dest, &src, &dest);
    }

    return true;
}

bool latxs_translate_addsd(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));
    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);
    bool dead_high = SHBR_ACTIVE_64(pir1);
    if (option_risk_simd_ignore_upper64 ||
        (dead_high && !SIMD_FPE_CHECK)) {
        latxs_append_ir2_opnd3(LISA_FADD_D, &dest, &dest, &src);
    } else {
        IR2_OPND temp = latxs_ra_alloc_ftemp();
        latxs_append_ir2_opnd3(LISA_FADD_D, &temp, &dest, &src);
        latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
        if (dead_high) {
            latxs_append_ir2_opnd2(LISA_FMOV_D, &dest, &temp);
        } else if (option_enable_lasx) {
            latxs_append_ir2_opnd2i(LISA_XVINSVE0_D, &dest, &temp, 0);
        } else {
            latxs_append_ir2_opnd2i(LISA_VEXTRINS_D, &dest, &temp, 0);
        }
    }
    return true;
}

bool latxs_translate_addss(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));
    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);
    bool dead_high = SHBR_ACTIVE_32(pir1);
    if (option_risk_simd_ignore_upper64 ||
        (dead_high && !SIMD_FPE_CHECK)) {
        latxs_append_ir2_opnd3(LISA_FADD_S, &dest, &dest, &src);
    } else {
        IR2_OPND temp = latxs_ra_alloc_ftemp();
        latxs_append_ir2_opnd3(LISA_FADD_S, &temp, &dest, &src);
        latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
        if (dead_high) {
            latxs_append_ir2_opnd2(LISA_FMOV_S, &dest, &temp);
        } else if (option_enable_lasx) {
            latxs_append_ir2_opnd2i(LISA_XVINSVE0_W, &dest, &temp, 0);
        } else {
            latxs_append_ir2_opnd2i(LISA_VEXTRINS_W, &dest, &temp, 0);
        }
    }
    return true;
}

bool latxs_translate_andps(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));
    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);
    latxs_append_ir2_opnd3(LISA_VAND_V, &dest, &dest, &src);
    return true;
}

bool latxs_translate_cmpeqpd(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));
    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);

    IR2_OPND dest_t;
    if (SIMD_FPE_CHECK) {
        dest_t = latxs_ra_alloc_ftemp();
        if(option_enable_lasx) {
            latxs_append_ir2_opnd3(LISA_XVOR_V, &dest_t, &dest, &dest);
        } else {
            latxs_append_ir2_opnd3(LISA_VOR_V, &dest_t, &dest, &dest);
        }
    }

    latxs_append_ir2_opnd3i(LISA_VFCMP_COND_D, &dest, &dest, &src,
                                  FCMP_COND_CEQ);

    latxs_tr_gen_simd_fpe_check_t(pir1, &dest_t);
    return true;
}

bool latxs_translate_cmpltpd(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));
    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);

    IR2_OPND dest_t;
    if (SIMD_FPE_CHECK) {
        dest_t = latxs_ra_alloc_ftemp();
        if(option_enable_lasx) {
            latxs_append_ir2_opnd3(LISA_XVOR_V, &dest_t, &dest, &dest);
        } else {
            latxs_append_ir2_opnd3(LISA_VOR_V, &dest_t, &dest, &dest);
        }
    }

    latxs_append_ir2_opnd3i(LISA_VFCMP_COND_D, &dest, &dest, &src,
                                  FCMP_COND_SLT);
    latxs_tr_gen_simd_fpe_check_t(pir1, &dest_t);
    return true;
}

bool latxs_translate_cmplepd(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));
    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);

    IR2_OPND dest_t;
    if (SIMD_FPE_CHECK) {
        dest_t = latxs_ra_alloc_ftemp();
        if(option_enable_lasx) {
            latxs_append_ir2_opnd3(LISA_XVOR_V, &dest_t, &dest, &dest);
        } else {
            latxs_append_ir2_opnd3(LISA_VOR_V, &dest_t, &dest, &dest);
        }
    }

    latxs_append_ir2_opnd3i(LISA_VFCMP_COND_D, &dest, &dest, &src,
                                  FCMP_COND_SLE);
    latxs_tr_gen_simd_fpe_check_t(pir1, &dest_t);
    return true;
}

bool latxs_translate_cmpunordpd(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));
    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);

    IR2_OPND dest_t;
    if (SIMD_FPE_CHECK) {
        dest_t = latxs_ra_alloc_ftemp();
        if(option_enable_lasx) {
            latxs_append_ir2_opnd3(LISA_XVOR_V, &dest_t, &dest, &dest);
        } else {
            latxs_append_ir2_opnd3(LISA_VOR_V, &dest_t, &dest, &dest);
        }
    }

    latxs_append_ir2_opnd3i(LISA_VFCMP_COND_D, &dest, &dest, &src,
                                  FCMP_COND_CUN);
    latxs_tr_gen_simd_fpe_check_t(pir1, &dest_t);
    return true;
}

bool latxs_translate_cmpneqpd(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));
    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);

    IR2_OPND dest_t;
    if (SIMD_FPE_CHECK) {
        dest_t = latxs_ra_alloc_ftemp();
        if(option_enable_lasx) {
            latxs_append_ir2_opnd3(LISA_XVOR_V, &dest_t, &dest, &dest);
        } else {
            latxs_append_ir2_opnd3(LISA_VOR_V, &dest_t, &dest, &dest);
        }
    }

    latxs_append_ir2_opnd3i(LISA_VFCMP_COND_D, &dest, &dest, &src,
                                  FCMP_COND_CUNE);
    latxs_tr_gen_simd_fpe_check_t(pir1, &dest_t);
    return true;
}

bool latxs_translate_cmpnltpd(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));
    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);

    IR2_OPND dest_t;
    if (SIMD_FPE_CHECK) {
        dest_t = latxs_ra_alloc_ftemp();
        if(option_enable_lasx) {
            latxs_append_ir2_opnd3(LISA_XVOR_V, &dest_t, &dest, &dest);
        } else {
            latxs_append_ir2_opnd3(LISA_VOR_V, &dest_t, &dest, &dest);
        }
    }

    latxs_append_ir2_opnd3i(LISA_VFCMP_COND_D, &dest, &src, &dest,
                                  FCMP_COND_SULE);
    latxs_tr_gen_simd_fpe_check_t(pir1, &dest_t);
    return true;
}

bool latxs_translate_cmpnlepd(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));
    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);

    IR2_OPND dest_t;
    if (SIMD_FPE_CHECK) {
        dest_t = latxs_ra_alloc_ftemp();
        if(option_enable_lasx) {
            latxs_append_ir2_opnd3(LISA_XVOR_V, &dest_t, &dest, &dest);
        } else {
            latxs_append_ir2_opnd3(LISA_VOR_V, &dest_t, &dest, &dest);
        }
    }

    latxs_append_ir2_opnd3i(LISA_VFCMP_COND_D, &dest, &src, &dest,
                                  FCMP_COND_SULT);
    latxs_tr_gen_simd_fpe_check_t(pir1, &dest_t);
    return true;
}

bool latxs_translate_cmpordpd(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));
    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);

    IR2_OPND dest_t;
    if (SIMD_FPE_CHECK) {
        dest_t = latxs_ra_alloc_ftemp();
        if(option_enable_lasx) {
            latxs_append_ir2_opnd3(LISA_XVOR_V, &dest_t, &dest, &dest);
        } else {
            latxs_append_ir2_opnd3(LISA_VOR_V, &dest_t, &dest, &dest);
        }
    }

    latxs_append_ir2_opnd3i(LISA_VFCMP_COND_D, &dest, &dest, &src,
                                  FCMP_COND_COR);
    latxs_tr_gen_simd_fpe_check_t(pir1, &dest_t);
    return true;
}

bool latxs_translate_cmpeqps(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));
    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);

    IR2_OPND dest_t;
    if (SIMD_FPE_CHECK) {
        dest_t = latxs_ra_alloc_ftemp();
        if(option_enable_lasx) {
            latxs_append_ir2_opnd3(LISA_XVOR_V, &dest_t, &dest, &dest);
        } else {
            latxs_append_ir2_opnd3(LISA_VOR_V, &dest_t, &dest, &dest);
        }
    }

    latxs_append_ir2_opnd3i(LISA_VFCMP_COND_S, &dest, &dest, &src,
                                  FCMP_COND_CEQ);
    latxs_tr_gen_simd_fpe_check_t(pir1, &dest_t);
    return true;
}

bool latxs_translate_cmpltps(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));
    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);

    IR2_OPND dest_t;
    if (SIMD_FPE_CHECK) {
        dest_t = latxs_ra_alloc_ftemp();
        if(option_enable_lasx) {
            latxs_append_ir2_opnd3(LISA_XVOR_V, &dest_t, &dest, &dest);
        } else {
            latxs_append_ir2_opnd3(LISA_VOR_V, &dest_t, &dest, &dest);
        }
    }

    latxs_append_ir2_opnd3i(LISA_VFCMP_COND_S, &dest, &dest, &src,
                                  FCMP_COND_SLT);
    latxs_tr_gen_simd_fpe_check_t(pir1, &dest_t);
    return true;
}

bool latxs_translate_cmpleps(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));
    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);

    IR2_OPND dest_t;
    if (SIMD_FPE_CHECK) {
        dest_t = latxs_ra_alloc_ftemp();
        if(option_enable_lasx) {
            latxs_append_ir2_opnd3(LISA_XVOR_V, &dest_t, &dest, &dest);
        } else {
            latxs_append_ir2_opnd3(LISA_VOR_V, &dest_t, &dest, &dest);
        }
    }

    latxs_append_ir2_opnd3i(LISA_VFCMP_COND_S, &dest, &dest, &src,
                                  FCMP_COND_SLE);
    latxs_tr_gen_simd_fpe_check_t(pir1, &dest_t);
    return true;
}

bool latxs_translate_cmpunordps(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));
    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);

    IR2_OPND dest_t;
    if (SIMD_FPE_CHECK) {
        dest_t = latxs_ra_alloc_ftemp();
        if(option_enable_lasx) {
            latxs_append_ir2_opnd3(LISA_XVOR_V, &dest_t, &dest, &dest);
        } else {
            latxs_append_ir2_opnd3(LISA_VOR_V, &dest_t, &dest, &dest);
        }
    }

    latxs_append_ir2_opnd3i(LISA_VFCMP_COND_S, &dest, &dest, &src,
                                  FCMP_COND_CUN);
    latxs_tr_gen_simd_fpe_check_t(pir1, &dest_t);
    return true;
}

bool latxs_translate_cmpneqps(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));
    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src = XMM_LOAD128(opnd1);

    IR2_OPND dest_t;
    if (SIMD_FPE_CHECK) {
        dest_t = latxs_ra_alloc_ftemp();
        if(option_enable_lasx) {
            latxs_append_ir2_opnd3(LISA_XVOR_V, &dest_t, &dest, &dest);
        } else {
            latxs_append_ir2_opnd3(LISA_VOR_V, &dest_t, &dest, &dest);
        }
    }

    latxs_append_ir2_opnd3i(LISA_VFCMP_COND_S, &dest, &dest, &src,
                                  FCMP_COND_CUNE);
    latxs_tr_gen_simd_fpe_check_t(pir1, &dest_t);
    return true;
}

bool latxs_translate_cmpnltps(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));
    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);

    IR2_OPND dest_t;
    if (SIMD_FPE_CHECK) {
        dest_t = latxs_ra_alloc_ftemp();
        if(option_enable_lasx) {
            latxs_append_ir2_opnd3(LISA_XVOR_V, &dest_t, &dest, &dest);
        } else {
            latxs_append_ir2_opnd3(LISA_VOR_V, &dest_t, &dest, &dest);
        }
    }

    latxs_append_ir2_opnd3i(LISA_VFCMP_COND_S, &dest, &src, &dest,
                                  FCMP_COND_SULE);
    latxs_tr_gen_simd_fpe_check_t(pir1, &dest_t);
    return true;
}

bool latxs_translate_cmpnleps(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));
    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);

    IR2_OPND dest_t;
    if (SIMD_FPE_CHECK) {
        dest_t = latxs_ra_alloc_ftemp();
        if(option_enable_lasx) {
            latxs_append_ir2_opnd3(LISA_XVOR_V, &dest_t, &dest, &dest);
        } else {
            latxs_append_ir2_opnd3(LISA_VOR_V, &dest_t, &dest, &dest);
        }
    }

    latxs_append_ir2_opnd3i(LISA_VFCMP_COND_S, &dest, &src, &dest,
                                  FCMP_COND_SULT);
    latxs_tr_gen_simd_fpe_check_t(pir1, &dest_t);
    return true;
}

bool latxs_translate_cmpordps(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));
    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);

    IR2_OPND dest_t;
    if (SIMD_FPE_CHECK) {
        dest_t = latxs_ra_alloc_ftemp();
        if(option_enable_lasx) {
            latxs_append_ir2_opnd3(LISA_XVOR_V, &dest_t, &dest, &dest);
        } else {
            latxs_append_ir2_opnd3(LISA_VOR_V, &dest_t, &dest, &dest);
        }
    }

    latxs_append_ir2_opnd3i(LISA_VFCMP_COND_S, &dest, &dest, &src,
                                  FCMP_COND_COR);
    latxs_tr_gen_simd_fpe_check_t(pir1, &dest_t);
    return true;
}

bool latxs_translate_cmpeqsd(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));
    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);

    IR2_OPND dest_transfer = latxs_ra_alloc_ftemp();
    IR2_OPND src_temp = latxs_ra_alloc_ftemp();
    IR2_OPND dest_temp = latxs_ra_alloc_ftemp();

    latxs_append_ir2_opnd3(LISA_VREPLVE_D, &src_temp, &src, &latxs_zero_ir2_opnd);
    latxs_append_ir2_opnd3(LISA_VREPLVE_D, &dest_temp, &dest, &latxs_zero_ir2_opnd);
    latxs_append_ir2_opnd3i(LISA_VFCMP_COND_D, &dest_transfer, &dest_temp, &src_temp, FCMP_COND_CEQ);
    latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
    shbr_write_scalar64(pir1, &dest, &dest_transfer);
    return true;
}

bool latxs_translate_cmpltsd(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));
    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);

    IR2_OPND dest_transfer = latxs_ra_alloc_ftemp();
    IR2_OPND src_temp = latxs_ra_alloc_ftemp();
    IR2_OPND dest_temp = latxs_ra_alloc_ftemp();

    latxs_append_ir2_opnd3(LISA_VREPLVE_D, &src_temp, &src, &latxs_zero_ir2_opnd);
    latxs_append_ir2_opnd3(LISA_VREPLVE_D, &dest_temp, &dest, &latxs_zero_ir2_opnd);
    latxs_append_ir2_opnd3i(LISA_VFCMP_COND_D, &dest_transfer, &dest_temp, &src_temp, FCMP_COND_SLT);
    latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
    shbr_write_scalar64(pir1, &dest, &dest_transfer);
    return true;
}

bool latxs_translate_cmplesd(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));
    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);

    IR2_OPND dest_transfer = latxs_ra_alloc_ftemp();
    IR2_OPND src_temp = latxs_ra_alloc_ftemp();
    IR2_OPND dest_temp = latxs_ra_alloc_ftemp();

    latxs_append_ir2_opnd3(LISA_VREPLVE_D, &src_temp, &src, &latxs_zero_ir2_opnd);
    latxs_append_ir2_opnd3(LISA_VREPLVE_D, &dest_temp, &dest, &latxs_zero_ir2_opnd);
    latxs_append_ir2_opnd3i(LISA_VFCMP_COND_D, &dest_transfer, &dest_temp, &src_temp, FCMP_COND_SLE);
    latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
    shbr_write_scalar64(pir1, &dest, &dest_transfer);
    return true;
}

bool latxs_translate_cmpunordsd(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));
    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);

    IR2_OPND dest_transfer = latxs_ra_alloc_ftemp();
    IR2_OPND src_temp = latxs_ra_alloc_ftemp();
    IR2_OPND dest_temp = latxs_ra_alloc_ftemp();

    latxs_append_ir2_opnd3(LISA_VREPLVE_D, &src_temp, &src, &latxs_zero_ir2_opnd);
    latxs_append_ir2_opnd3(LISA_VREPLVE_D, &dest_temp, &dest, &latxs_zero_ir2_opnd);
    latxs_append_ir2_opnd3i(LISA_VFCMP_COND_D, &dest_transfer, &dest_temp, &src_temp, FCMP_COND_CUN);
    latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
    shbr_write_scalar64(pir1, &dest, &dest_transfer);
    return true;
}

bool latxs_translate_cmpneqsd(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));
    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);

    IR2_OPND dest_transfer = latxs_ra_alloc_ftemp();
    IR2_OPND src_temp = latxs_ra_alloc_ftemp();
    IR2_OPND dest_temp = latxs_ra_alloc_ftemp();

    latxs_append_ir2_opnd3(LISA_VREPLVE_D, &src_temp, &src, &latxs_zero_ir2_opnd);
    latxs_append_ir2_opnd3(LISA_VREPLVE_D, &dest_temp, &dest, &latxs_zero_ir2_opnd);
    latxs_append_ir2_opnd3i(LISA_VFCMP_COND_D, &dest_transfer, &dest_temp, &src_temp, FCMP_COND_CUNE);
    latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
    shbr_write_scalar64(pir1, &dest, &dest_transfer);
    return true;
}

bool latxs_translate_cmpnltsd(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));
    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);

    IR2_OPND dest_transfer = latxs_ra_alloc_ftemp();
    IR2_OPND src_temp = latxs_ra_alloc_ftemp();
    IR2_OPND dest_temp = latxs_ra_alloc_ftemp();

    latxs_append_ir2_opnd3(LISA_VREPLVE_D, &src_temp, &src, &latxs_zero_ir2_opnd);
    latxs_append_ir2_opnd3(LISA_VREPLVE_D, &dest_temp, &dest, &latxs_zero_ir2_opnd);
    latxs_append_ir2_opnd3i(LISA_VFCMP_COND_D, &dest_transfer, &src_temp, &dest_temp, FCMP_COND_SULE);
    latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
    shbr_write_scalar64(pir1, &dest, &dest_transfer);
    return true;
}

bool latxs_translate_cmpnlesd(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));
    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);

    IR2_OPND dest_transfer = latxs_ra_alloc_ftemp();
    IR2_OPND src_temp = latxs_ra_alloc_ftemp();
    IR2_OPND dest_temp = latxs_ra_alloc_ftemp();

    latxs_append_ir2_opnd3(LISA_VREPLVE_D, &src_temp, &src, &latxs_zero_ir2_opnd);
    latxs_append_ir2_opnd3(LISA_VREPLVE_D, &dest_temp, &dest, &latxs_zero_ir2_opnd);
    latxs_append_ir2_opnd3i(LISA_VFCMP_COND_D, &dest_transfer, &src_temp, &dest_temp, FCMP_COND_SULT);
    latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
    shbr_write_scalar64(pir1, &dest, &dest_transfer);
    return true;
}

bool latxs_translate_cmpordsd(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));
    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);

    IR2_OPND dest_transfer = latxs_ra_alloc_ftemp();
    IR2_OPND src_temp = latxs_ra_alloc_ftemp();
    IR2_OPND dest_temp = latxs_ra_alloc_ftemp();

    latxs_append_ir2_opnd3(LISA_VREPLVE_D, &src_temp, &src, &latxs_zero_ir2_opnd);
    latxs_append_ir2_opnd3(LISA_VREPLVE_D, &dest_temp, &dest, &latxs_zero_ir2_opnd);
    latxs_append_ir2_opnd3i(LISA_VFCMP_COND_D, &dest_transfer, &dest_temp, &src_temp, FCMP_COND_COR);
    latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
    shbr_write_scalar64(pir1, &dest, &dest_transfer);
    return true;
}

bool latxs_translate_cmpeqss(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));
    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);

    IR2_OPND dest_transfer = latxs_ra_alloc_ftemp();
    IR2_OPND src_temp = latxs_ra_alloc_ftemp();
    IR2_OPND dest_temp = latxs_ra_alloc_ftemp();

    latxs_append_ir2_opnd3(LISA_VREPLVE_W, &src_temp, &src, &latxs_zero_ir2_opnd);
    latxs_append_ir2_opnd3(LISA_VREPLVE_W, &dest_temp, &dest, &latxs_zero_ir2_opnd);
    latxs_append_ir2_opnd3i(LISA_VFCMP_COND_S, &dest_transfer, &dest_temp, &src_temp, FCMP_COND_CEQ);
    latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
    shbr_write_scalar32(pir1, &dest, &dest_transfer);
    return true;
}

bool latxs_translate_cmpltss(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));
    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);

    IR2_OPND dest_transfer = latxs_ra_alloc_ftemp();
    IR2_OPND src_temp = latxs_ra_alloc_ftemp();
    IR2_OPND dest_temp = latxs_ra_alloc_ftemp();

    latxs_append_ir2_opnd3(LISA_VREPLVE_W, &src_temp, &src, &latxs_zero_ir2_opnd);
    latxs_append_ir2_opnd3(LISA_VREPLVE_W, &dest_temp, &dest, &latxs_zero_ir2_opnd);
    latxs_append_ir2_opnd3i(LISA_VFCMP_COND_S, &dest_transfer, &dest_temp, &src_temp, FCMP_COND_SLT);
    latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
    shbr_write_scalar32(pir1, &dest, &dest_transfer);
    return true;
}

bool latxs_translate_cmpless(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));
    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);

    IR2_OPND dest_transfer = latxs_ra_alloc_ftemp();
    IR2_OPND src_temp = latxs_ra_alloc_ftemp();
    IR2_OPND dest_temp = latxs_ra_alloc_ftemp();

    latxs_append_ir2_opnd3(LISA_VREPLVE_W, &src_temp, &src, &latxs_zero_ir2_opnd);
    latxs_append_ir2_opnd3(LISA_VREPLVE_W, &dest_temp, &dest, &latxs_zero_ir2_opnd);
    latxs_append_ir2_opnd3i(LISA_VFCMP_COND_S, &dest_transfer, &dest_temp, &src_temp, FCMP_COND_SLE);
    latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
    shbr_write_scalar32(pir1, &dest, &dest_transfer);
    return true;
}

bool latxs_translate_cmpunordss(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));
    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);

    IR2_OPND dest_transfer = latxs_ra_alloc_ftemp();
    IR2_OPND src_temp = latxs_ra_alloc_ftemp();
    IR2_OPND dest_temp = latxs_ra_alloc_ftemp();

    latxs_append_ir2_opnd3(LISA_VREPLVE_W, &src_temp, &src, &latxs_zero_ir2_opnd);
    latxs_append_ir2_opnd3(LISA_VREPLVE_W, &dest_temp, &dest, &latxs_zero_ir2_opnd);
    latxs_append_ir2_opnd3i(LISA_VFCMP_COND_S, &dest_transfer, &dest_temp, &src_temp, FCMP_COND_CUN);
    latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
    shbr_write_scalar32(pir1, &dest, &dest_transfer);
    return true;
}

bool latxs_translate_cmpneqss(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));
    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);

    IR2_OPND dest_transfer = latxs_ra_alloc_ftemp();
    IR2_OPND src_temp = latxs_ra_alloc_ftemp();
    IR2_OPND dest_temp = latxs_ra_alloc_ftemp();

    latxs_append_ir2_opnd3(LISA_VREPLVE_W, &src_temp, &src, &latxs_zero_ir2_opnd);
    latxs_append_ir2_opnd3(LISA_VREPLVE_W, &dest_temp, &dest, &latxs_zero_ir2_opnd);
    latxs_append_ir2_opnd3i(LISA_VFCMP_COND_S, &dest_transfer, &dest_temp, &src_temp, FCMP_COND_CUNE);
    latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
    shbr_write_scalar32(pir1, &dest, &dest_transfer);
    return true;
}

bool latxs_translate_cmpnltss(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));
    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);

    IR2_OPND dest_transfer = latxs_ra_alloc_ftemp();
    IR2_OPND src_temp = latxs_ra_alloc_ftemp();
    IR2_OPND dest_temp = latxs_ra_alloc_ftemp();

    latxs_append_ir2_opnd3(LISA_VREPLVE_W, &src_temp, &src, &latxs_zero_ir2_opnd);
    latxs_append_ir2_opnd3(LISA_VREPLVE_W, &dest_temp, &dest, &latxs_zero_ir2_opnd);
    latxs_append_ir2_opnd3i(LISA_VFCMP_COND_S, &dest_transfer, &src_temp, &dest_temp, FCMP_COND_SULE);
    latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
    shbr_write_scalar32(pir1, &dest, &dest_transfer);
    return true;
}

bool latxs_translate_cmpnless(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));
    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);

    IR2_OPND dest_transfer = latxs_ra_alloc_ftemp();
    IR2_OPND src_temp = latxs_ra_alloc_ftemp();
    IR2_OPND dest_temp = latxs_ra_alloc_ftemp();

    latxs_append_ir2_opnd3(LISA_VREPLVE_W, &src_temp, &src, &latxs_zero_ir2_opnd);
    latxs_append_ir2_opnd3(LISA_VREPLVE_W, &dest_temp, &dest, &latxs_zero_ir2_opnd);
    latxs_append_ir2_opnd3i(LISA_VFCMP_COND_S, &dest_transfer, &src_temp, &dest_temp, FCMP_COND_SULT);
    latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
    shbr_write_scalar32(pir1, &dest, &dest_transfer);
    return true;
}

bool latxs_translate_cmpordss(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));
    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);

    IR2_OPND dest_transfer = latxs_ra_alloc_ftemp();
    IR2_OPND src_temp = latxs_ra_alloc_ftemp();
    IR2_OPND dest_temp = latxs_ra_alloc_ftemp();

    latxs_append_ir2_opnd3(LISA_VREPLVE_W, &src_temp, &src, &latxs_zero_ir2_opnd);
    latxs_append_ir2_opnd3(LISA_VREPLVE_W, &dest_temp, &dest, &latxs_zero_ir2_opnd);
    latxs_append_ir2_opnd3i(LISA_VFCMP_COND_S, &dest_transfer, &dest_temp, &src_temp, FCMP_COND_COR);
    latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
    shbr_write_scalar32(pir1, &dest, &dest_transfer);
    return true;
}

bool latxs_translate_comisd(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    /**
     * (bit 6)ZF = 1 if EQ || UOR
     * (bit 2)PF = 1 if UOR (= ZF & CF)
     * (bit 0)CF = 1 if LT || UOR
     */
    lsassert(ir1_opnd_num(pir1) == 2);
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);
    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src = XMM_LOAD128(opnd1);
    /* 0. set flag = 0 */
    IR2_OPND flag_zf = latxs_ra_alloc_itemp();
    IR2_OPND flag_pf = latxs_ra_alloc_itemp();
    IR2_OPND flag = latxs_ra_alloc_itemp();
    latxs_append_ir2_opnd2_(lisa_mov, &flag, &latxs_zero_ir2_opnd);

    /* 1. check ZF, are they equal & unordered? */
    latxs_append_ir2_opnd3i(LISA_FCMP_COND_D, &latxs_fcc0_ir2_opnd, &dest, &src,
                            FCMP_COND_CUEQ + FCMP_COND_SAF);
    latxs_append_ir2_opnd2(LISA_MOVCF2GR, &flag_zf, &latxs_fcc0_ir2_opnd);

    /* 2. check CF, are they less & unordered? */
    latxs_append_ir2_opnd3i(LISA_FCMP_COND_D, &latxs_fcc2_ir2_opnd, &dest, &src,
                            FCMP_COND_CULT + FCMP_COND_SAF);
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

    return true;
}

bool latxs_translate_comiss(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    /**
     * (bit 6)ZF = 1 if EQ || UOR
     * (bit 2)PF = 1 if UOR (= ZF & CF)
     * (bit 0)CF = 1 if LT || UOR
     */
    lsassert(ir1_opnd_num(pir1) == 2);
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);
    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src = XMM_LOAD128(opnd1);
    /* 0. set flag = 0 */
    IR2_OPND flag_zf = latxs_ra_alloc_itemp();
    IR2_OPND flag_pf = latxs_ra_alloc_itemp();
    IR2_OPND flag = latxs_ra_alloc_itemp();
    latxs_append_ir2_opnd2_(lisa_mov, &flag, &latxs_zero_ir2_opnd);

    /* 1. check ZF, are they equal & unordered? */
    latxs_append_ir2_opnd3i(LISA_FCMP_COND_S, &latxs_fcc0_ir2_opnd, &dest, &src,
                            FCMP_COND_CUEQ + FCMP_COND_SAF);
    latxs_append_ir2_opnd2(LISA_MOVCF2GR, &flag_zf, &latxs_fcc0_ir2_opnd);

    /* 2. check CF, are they less & unordered? */
    latxs_append_ir2_opnd3i(LISA_FCMP_COND_S, &latxs_fcc2_ir2_opnd, &dest, &src,
                            FCMP_COND_CULT + FCMP_COND_SAF);
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

    return true;
}

bool latxs_translate_cvtpi2ps(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));

    IR2_OPND dest = latxs_load_freg128_from_ir1(opnd0);
    IR2_OPND src = latxs_load_freg_from_ir1_1(opnd1, false);
    IR2_OPND temp = latxs_ra_alloc_ftemp();
    latxs_append_ir2_opnd3(LISA_VREPLVE_D, &temp, &src, &latxs_zero_ir2_opnd);
    latxs_append_ir2_opnd2(LISA_VFFINT_S_W, &temp, &temp);
    latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
    latxs_append_ir2_opnd2i(LISA_VEXTRINS_D, &dest, &temp,
                        VEXTRINS_IMM_4_0(0, 0));

    latxs_ra_free_temp(&temp);
    return true;
}

bool latxs_translate_cvtps2pi(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR2_OPND dest = latxs_ra_alloc_mmx(ir1_opnd_base_reg_num(ir1_get_opnd(pir1, 0)));
    IR2_OPND src_lo = latxs_load_freg128_from_ir1(ir1_get_opnd(pir1, 1));
    IR2_OPND ftemp_src_temp = latxs_ra_alloc_ftemp();
    IR2_OPND ftemp_final = latxs_ra_alloc_ftemp();
    IR2_OPND label_for_flow1 = latxs_ir2_opnd_new_label();
    IR2_OPND label_for_flow2 = latxs_ir2_opnd_new_label();
    IR2_OPND label_over = latxs_ir2_opnd_new_label();
    IR2_OPND label_high = latxs_ir2_opnd_new_label();

    /*
     * verify the first single scalar operand is unorder or overflow
     * or under flow?
     */
    IR2_OPND ftemp_src1 = latxs_ra_alloc_ftemp();

    /* convertion has to be done first, or INVALID exception may be missed */
    latxs_append_ir2_opnd3(LISA_VREPLVE_W, &ftemp_src_temp, &src_lo, &latxs_zero_ir2_opnd);
    latxs_append_ir2_opnd2(LISA_VFTINT_W_S, &ftemp_src_temp, &ftemp_src_temp);
    latxs_tr_gen_simd_fpe_check_t(pir1, NULL);

    IR2_OPND temp_int = latxs_ra_alloc_itemp();
    IR2_OPND temp_fcsr = latxs_ra_alloc_itemp();
    if(!option_fast_masked_v) {
        /* check if INVALID bit in fcsr is set */
        latxs_append_ir2_opnd2(LISA_MOVFCSR2GR, &temp_fcsr, &latxs_fcsr_ir2_opnd);
        latxs_append_ir2_opnd2ii(LISA_BSTRPICK_W, &temp_fcsr, &temp_fcsr, FCSR_OFF_CAUSE_V, FCSR_OFF_CAUSE_V);
        latxs_append_ir2_opnd2(LISA_BNEZ, &temp_fcsr, &label_for_flow1);

        /* ##if## no INVALID exception happend, save the lower bits result */
        latxs_append_ir2_opnd2(LISA_FMOV_S, &ftemp_src1, &ftemp_src_temp);
        latxs_append_ir2_opnd1(LISA_B, &label_high);

        /* ##else## INVALID exception did happen, load 0x80000000 manually*/
        latxs_append_ir2_opnd1(LISA_LABEL, &label_for_flow1);
        latxs_load_imm32_to_ir2(&temp_int, 0x80000000, EXMode_N);
        latxs_append_ir2_opnd2(LISA_MOVGR2FR_D, &ftemp_src1, &temp_int);
        latxs_append_ir2_opnd1(LISA_LABEL, &label_high);
    } else {
        latxs_append_ir2_opnd2(LISA_FMOV_S, &ftemp_src1, &ftemp_src_temp);
    }

    /*
     * verify the second single scalar operand is unorder or overflow
     * or under flow?
     */
    IR2_OPND ftemp_src2 = latxs_ra_alloc_ftemp();
    latxs_append_ir2_opnd2i(LISA_VEXTRINS_W, &ftemp_src2, &src_lo, VEXTRINS_IMM_4_0(0, 1));

    /* convertion has to be done first, or INVALID exception may be missed */
    latxs_append_ir2_opnd3(LISA_VREPLVE_W, &ftemp_src_temp, &ftemp_src2, &latxs_zero_ir2_opnd);
    latxs_append_ir2_opnd2(LISA_VFTINT_W_S, &ftemp_src_temp, &ftemp_src_temp);

    /* check if INVALID bit in fcsr is set */
    if(!option_fast_masked_v) {
        latxs_append_ir2_opnd2(LISA_MOVFCSR2GR, &temp_fcsr, &latxs_fcsr_ir2_opnd);
        latxs_append_ir2_opnd2ii(LISA_BSTRPICK_W, &temp_fcsr, &temp_fcsr, FCSR_OFF_CAUSE_V, FCSR_OFF_CAUSE_V);
        latxs_append_ir2_opnd2(LISA_BNEZ, &temp_fcsr, &label_for_flow2);

        /* ##if## no INVALID exception happend, save the lower bits result */
        latxs_append_ir2_opnd2(LISA_FMOV_S, &ftemp_src2, &ftemp_src_temp);
        latxs_append_ir2_opnd1(LISA_B, &label_over);

        /* ##else## INVALID exception did happen, load 0x80000000 manually*/
        latxs_append_ir2_opnd1(LISA_LABEL, &label_for_flow2);
        latxs_load_imm32_to_ir2(&temp_int, 0x80000000, EXMode_N);
        latxs_append_ir2_opnd2(LISA_MOVGR2FR_D, &ftemp_src2, &temp_int);
        latxs_append_ir2_opnd1(LISA_LABEL, &label_over);
    } else {
        latxs_append_ir2_opnd2(LISA_FMOV_S, &ftemp_src2, &ftemp_src_temp);
    }

    latxs_ra_free_temp(&ftemp_src_temp);
    latxs_ra_free_temp(&temp_int);
    latxs_ra_free_temp(&temp_fcsr);
    latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
    /**
     * merge, use ftemp_final as transfer register to prevent damaging the
     * 64 bits of the dest.
     */
    latxs_append_ir2_opnd3(LISA_VILVL_W, &ftemp_final, &ftemp_src2, &ftemp_src1);
    latxs_append_ir2_opnd2i(LISA_VEXTRINS_D, &dest, &ftemp_final, VEXTRINS_IMM_4_0(0, 0));

    latxs_ra_free_temp(&ftemp_src1);
    latxs_ra_free_temp(&ftemp_src2);
    latxs_ra_free_temp(&ftemp_final);

    return true;
}

bool latxs_translate_cvtsi2ss(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));

    IR2_OPND dest = latxs_load_freg128_from_ir1(opnd0);
    IR2_OPND src = latxs_ra_alloc_itemp();
    latxs_load_ir1_to_ir2(&src, opnd1, EXMode_Z);
    IR2_OPND temp = latxs_ra_alloc_ftemp();
    latxs_append_ir2_opnd2(LISA_MOVGR2FR_D, &temp, &src);
    if (ir1_opnd_size(opnd1) == 64) {
        latxs_append_ir2_opnd2(LISA_FFINT_S_L, &temp, &temp);
    } else {
        latxs_append_ir2_opnd2(LISA_FFINT_S_W, &temp, &temp);
    }
    latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
    latxs_append_ir2_opnd2i(LISA_VEXTRINS_W, &dest, &temp,
                        VEXTRINS_IMM_4_0(0, 0));

    latxs_ra_free_temp(&src);
    latxs_ra_free_temp(&temp);
    return true;
}

bool latxs_translate_cvtss2si(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR2_OPND src_lo = latxs_load_freg128_from_ir1(ir1_get_opnd(pir1, 1));
    IR2_OPND temp1 = latxs_ra_alloc_itemp();
    int opnd0_size = ir1_opnd_size(ir1_get_opnd(pir1, 0));
    IR2_OPCODE tr_inst = (opnd0_size == 64) ? LISA_FTINT_L_S : LISA_FTINT_W_S;
    IR2_OPND dest = latxs_ra_alloc_ftemp();
    latxs_append_ir2_opnd2(tr_inst, &dest, &src_lo);

    /* check fcsr to get overflow etc. */
    IR2_OPND fcsr = latxs_ra_alloc_itemp();
    IR2_OPND overflow = latxs_ra_alloc_itemp();
    if(!option_fast_masked_v) {
        latxs_append_ir2_opnd2(LISA_MOVFCSR2GR, &fcsr, &latxs_fcsr_ir2_opnd);
        latxs_append_ir2_opnd2ii(LISA_BSTRPICK_W, &fcsr, &fcsr, FCSR_OFF_CAUSE_V, FCSR_OFF_CAUSE_V);
        latxs_append_ir2_opnd2i(LISA_SLLI_D, &overflow, &fcsr, opnd0_size - 1);
        /* mov the result */
        latxs_append_ir2_opnd2(LISA_MOVFR2GR_D, &temp1, &dest);

        latxs_append_ir2_opnd3(LISA_MASKNEZ, &fcsr, &temp1, &fcsr);
        latxs_append_ir2_opnd3(LISA_OR, &temp1, &overflow, &fcsr);
    } else {
        latxs_append_ir2_opnd2(LISA_MOVFR2GR_D, &temp1, &dest);
    }

    latxs_ra_free_temp(&fcsr);
    latxs_ra_free_temp(&overflow);
    latxs_ra_free_temp(&dest);
    latxs_tr_gen_simd_fpe_check_t(pir1, NULL);

    latxs_store_ir2_to_ir1(&temp1, ir1_get_opnd(pir1, 0));

    latxs_ra_free_temp(&temp1);

    return true;
}

bool latxs_translate_cvttps2pi(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR2_OPND dest = latxs_ra_alloc_mmx(ir1_opnd_base_reg_num(ir1_get_opnd(pir1, 0)));
    IR2_OPND src_lo = latxs_load_freg128_from_ir1(ir1_get_opnd(pir1, 1));
    IR2_OPND ftemp_src_temp = latxs_ra_alloc_ftemp();
    IR2_OPND ftemp_final = latxs_ra_alloc_ftemp();
    IR2_OPND label_for_flow1 = latxs_ir2_opnd_new_label();
    IR2_OPND label_for_flow2 = latxs_ir2_opnd_new_label();
    IR2_OPND label_over = latxs_ir2_opnd_new_label();
    IR2_OPND label_high = latxs_ir2_opnd_new_label();

    /*
     * verify the first single scalar operand is unorder or overflow
     * or under flow?
     */
    IR2_OPND ftemp_src1 = latxs_ra_alloc_ftemp();

    /* convertion has to be done first, or INVALID exception may be missed */
    latxs_append_ir2_opnd3(LISA_VREPLVE_W, &ftemp_src_temp, &src_lo, &latxs_zero_ir2_opnd);
    latxs_append_ir2_opnd2(LISA_VFTINTRZ_W_S, &ftemp_src_temp, &ftemp_src_temp);
    latxs_tr_gen_simd_fpe_check_t(pir1, NULL);

    IR2_OPND temp_int = latxs_ra_alloc_itemp();
    IR2_OPND temp_fcsr = latxs_ra_alloc_itemp();
    /* check if INVALID bit in fcsr is set */
    if(!option_fast_masked_v) {
        latxs_append_ir2_opnd2(LISA_MOVFCSR2GR, &temp_fcsr, &latxs_fcsr_ir2_opnd);
        latxs_append_ir2_opnd2ii(LISA_BSTRPICK_W, &temp_fcsr, &temp_fcsr, FCSR_OFF_CAUSE_V, FCSR_OFF_CAUSE_V);
        latxs_append_ir2_opnd2(LISA_BNEZ, &temp_fcsr, &label_for_flow1);

        /* ##if## no INVALID exception happend, save the lower bits result */
        latxs_append_ir2_opnd2(LISA_FMOV_S, &ftemp_src1, &ftemp_src_temp);
        latxs_append_ir2_opnd1(LISA_B, &label_high);

        /* ##else## INVALID exception did happen, load 0x80000000 manually*/
        latxs_append_ir2_opnd1(LISA_LABEL, &label_for_flow1);
        latxs_load_imm32_to_ir2(&temp_int, 0x80000000, EXMode_N);
        latxs_append_ir2_opnd2(LISA_MOVGR2FR_D, &ftemp_src1, &temp_int);
        latxs_append_ir2_opnd1(LISA_LABEL, &label_high);
    } else {
        latxs_append_ir2_opnd2(LISA_FMOV_S, &ftemp_src1, &ftemp_src_temp);
    }

    /*
     * verify the second single scalar operand is unorder or overflow
     * or under flow?
     */
    IR2_OPND ftemp_src2 = latxs_ra_alloc_ftemp();
    latxs_append_ir2_opnd2i(LISA_VEXTRINS_W, &ftemp_src2, &src_lo, VEXTRINS_IMM_4_0(0, 1));

    /* convertion has to be done first, or INVALID exception may be missed */
    latxs_append_ir2_opnd3(LISA_VREPLVE_W, &ftemp_src_temp, &ftemp_src2, &latxs_zero_ir2_opnd);
    latxs_append_ir2_opnd2(LISA_VFTINTRZ_W_S, &ftemp_src_temp, &ftemp_src_temp);

    /* check if INVALID bit in fcsr is set */
    if(!option_fast_masked_v) {
        latxs_append_ir2_opnd2(LISA_MOVFCSR2GR, &temp_fcsr, &latxs_fcsr_ir2_opnd);
        latxs_append_ir2_opnd2ii(LISA_BSTRPICK_W, &temp_fcsr, &temp_fcsr, FCSR_OFF_CAUSE_V, FCSR_OFF_CAUSE_V);
        latxs_append_ir2_opnd2(LISA_BNEZ, &temp_fcsr, &label_for_flow2);

        /* ##if## no INVALID exception happend, save the lower bits result */
        latxs_append_ir2_opnd2(LISA_FMOV_S, &ftemp_src2, &ftemp_src_temp);
        latxs_append_ir2_opnd1(LISA_B, &label_over);

        /* ##else## INVALID exception did happen, load 0x80000000 manually*/
        latxs_append_ir2_opnd1(LISA_LABEL, &label_for_flow2);
        latxs_load_imm32_to_ir2(&temp_int, 0x80000000, EXMode_N);
        latxs_append_ir2_opnd2(LISA_MOVGR2FR_D, &ftemp_src2, &temp_int);
        latxs_append_ir2_opnd1(LISA_LABEL, &label_over);
    } else {
        latxs_append_ir2_opnd2(LISA_FMOV_S, &ftemp_src2, &ftemp_src_temp);
    }

    latxs_ra_free_temp(&ftemp_src_temp);
    latxs_ra_free_temp(&temp_int);
    latxs_ra_free_temp(&temp_fcsr);
    latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
    /**
     * merge, use ftemp_final as transfer register to prevent damaging the
     * 64 bits of the dest.
     */
    latxs_append_ir2_opnd3(LISA_VILVL_W, &ftemp_final, &ftemp_src2, &ftemp_src1);
    latxs_append_ir2_opnd2i(LISA_VEXTRINS_D, &dest, &ftemp_final, VEXTRINS_IMM_4_0(0, 0));

    latxs_ra_free_temp(&ftemp_src1);
    latxs_ra_free_temp(&ftemp_src2);
    latxs_ra_free_temp(&ftemp_final);

    return true;
}

bool latxs_translate_cvtpi2pd(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));
    /* TODO:simply */
    IR2_OPND dest = XMM_LOAD128(opnd0);

    IR2_OPND src = latxs_load_freg_from_ir1_1(opnd1, false);

    IR2_OPND temp_src = latxs_ra_alloc_ftemp();

    latxs_append_ir2_opnd2i(LISA_VEXTRINS_W, &temp_src, &src,
                                 VEXTRINS_IMM_4_0(0, 0));
    latxs_append_ir2_opnd2(LISA_FFINT_D_W, &temp_src, &temp_src);
    latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
    latxs_append_ir2_opnd2i(LISA_VEXTRINS_D, &dest, &temp_src,
                                 VEXTRINS_IMM_4_0(0, 0));

    latxs_append_ir2_opnd2i(LISA_VEXTRINS_W, &temp_src, &src,
                                 VEXTRINS_IMM_4_0(0, 1));
    latxs_append_ir2_opnd2(LISA_FFINT_D_W, &temp_src, &temp_src);
    latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
    latxs_append_ir2_opnd2i(LISA_VEXTRINS_D, &dest, &temp_src,
                                 VEXTRINS_IMM_4_0(1, 0));

    return true;
}

bool latxs_translate_cvtpd2pi(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR2_OPND dest = latxs_ra_alloc_mmx(ir1_opnd_base_reg_num(ir1_get_opnd(pir1, 0)));
    IR2_OPND src_lo = latxs_load_freg128_from_ir1(ir1_get_opnd(pir1, 1));
    IR2_OPND src_hi = latxs_ra_alloc_ftemp();
    latxs_append_ir2_opnd2i(LISA_VEXTRINS_D, &src_hi, &src_lo, VEXTRINS_IMM_4_0(0, 1));

    IR2_OPND ftemp_src1 = latxs_ra_alloc_ftemp();
    IR2_OPND ftemp_src2 = latxs_ra_alloc_ftemp();
    IR2_OPND ftemp_final = latxs_ra_alloc_ftemp();
    IR2_OPND label_for_flow1 = latxs_ir2_opnd_new_label();
    IR2_OPND label_for_flow2 = latxs_ir2_opnd_new_label();
    IR2_OPND label_over = latxs_ir2_opnd_new_label();
    IR2_OPND label_high = latxs_ir2_opnd_new_label();

    /*
     * verify the first double scalar operand is unorder or overflow
     * or under flow?
     */
    IR2_OPND ftemp_src_temp1 = latxs_ra_alloc_ftemp();
    IR2_OPND ftemp_src_temp2 = latxs_ra_alloc_ftemp();

    /* convertion has to be done first, or INVALID exception may be missed */
    latxs_append_ir2_opnd3(LISA_VREPLVE_D, &ftemp_src_temp1, &src_lo, &latxs_zero_ir2_opnd);
    latxs_append_ir2_opnd3(LISA_VFTINT_W_D, &ftemp_src_temp2, &ftemp_src_temp1, &ftemp_src_temp1);
    latxs_tr_gen_simd_fpe_check_t(pir1, NULL);

    IR2_OPND temp_int = latxs_ra_alloc_itemp();
    IR2_OPND temp_fcsr = latxs_ra_alloc_itemp();
    if(!option_fast_masked_v) {
        /* check if INVALID bit in fcsr is set */
        latxs_append_ir2_opnd2(LISA_MOVFCSR2GR, &temp_fcsr, &latxs_fcsr_ir2_opnd);
        latxs_append_ir2_opnd2ii(LISA_BSTRPICK_W, &temp_fcsr, &temp_fcsr, FCSR_OFF_CAUSE_V, FCSR_OFF_CAUSE_V);
        latxs_append_ir2_opnd2(LISA_BNEZ, &temp_fcsr, &label_for_flow1);

        /* ##if## no INVALID exception happend, save the lower bits result */
        latxs_append_ir2_opnd2(LISA_FMOV_D, &ftemp_src1, &ftemp_src_temp2);
        latxs_append_ir2_opnd1(LISA_B, &label_high);

        /* ##else## INVALID exception did happen, load 0x80000000 manually*/
        latxs_append_ir2_opnd1(LISA_LABEL, &label_for_flow1);
        latxs_load_imm32_to_ir2(&temp_int, 0x80000000, EXMode_N);
        latxs_append_ir2_opnd2(LISA_MOVGR2FR_D, &ftemp_src1, &temp_int);
        latxs_append_ir2_opnd1(LISA_LABEL, &label_high);
    } else {
        latxs_append_ir2_opnd2(LISA_FMOV_D, &ftemp_src1, &ftemp_src_temp2);
    }

    /*
     * verify the second double scalar operand is unorder or overflow
     * or under flow?
     */
    latxs_append_ir2_opnd3(LISA_VREPLVE_D, &ftemp_src_temp1, &src_hi, &latxs_zero_ir2_opnd);
    latxs_append_ir2_opnd3(LISA_VFTINT_W_D, &ftemp_src_temp2, &ftemp_src_temp1, &ftemp_src_temp1);
    latxs_ra_free_temp(&ftemp_src_temp1);

    if(!option_fast_masked_v) {
        /* check if INVALID bit in fcsr is set */
        latxs_append_ir2_opnd2(LISA_MOVFCSR2GR, &temp_fcsr, &latxs_fcsr_ir2_opnd);
        latxs_append_ir2_opnd2ii(LISA_BSTRPICK_W, &temp_fcsr, &temp_fcsr, FCSR_OFF_CAUSE_V, FCSR_OFF_CAUSE_V);
        latxs_append_ir2_opnd2(LISA_BNEZ, &temp_fcsr, &label_for_flow2);

        /* ##if## no INVALID exception happend, save the lower bits result */
        latxs_append_ir2_opnd2(LISA_FMOV_D, &ftemp_src2, &ftemp_src_temp2);
        latxs_append_ir2_opnd1(LISA_B, &label_over);

        /* ##else## INVALID exception did happen, load 0x80000000 manually*/
        latxs_append_ir2_opnd1(LISA_LABEL, &label_for_flow2);
        latxs_load_imm32_to_ir2(&temp_int, 0x80000000, EXMode_N);
        latxs_append_ir2_opnd2(LISA_MOVGR2FR_D, &ftemp_src2, &temp_int);
        latxs_append_ir2_opnd1(LISA_LABEL, &label_over);
    } else {
        latxs_append_ir2_opnd2(LISA_FMOV_D, &ftemp_src2, &ftemp_src_temp2);
    }

    latxs_ra_free_temp(&ftemp_src_temp2);
    latxs_ra_free_temp(&temp_int);
    latxs_ra_free_temp(&temp_fcsr);
    latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
    /**
     * merge, use ftemp_final as transfer register to prevent damaging the
     * 64 bits of the dest.
     */
    latxs_append_ir2_opnd3(LISA_VILVL_W, &ftemp_final, &ftemp_src2, &ftemp_src1);
    latxs_append_ir2_opnd2i(LISA_VEXTRINS_D, &dest, &ftemp_final, VEXTRINS_IMM_4_0(0, 0));

    latxs_ra_free_temp(&ftemp_src1);
    latxs_ra_free_temp(&ftemp_src2);
    latxs_ra_free_temp(&ftemp_final);

    return true;
}

bool latxs_translate_cvtsd2si(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR2_OPND src_lo = latxs_load_freg128_from_ir1(ir1_get_opnd(pir1, 1));
    IR2_OPND temp1 = latxs_ra_alloc_itemp();
    int opnd0_size = ir1_opnd_size(ir1_get_opnd(pir1, 0));
    IR2_OPCODE tr_inst = (opnd0_size == 64) ? LISA_FTINT_L_D : LISA_FTINT_W_D;
    IR2_OPND dest = latxs_ra_alloc_ftemp();
    latxs_append_ir2_opnd2(tr_inst, &dest, &src_lo);

    /* check fcsr to get overflow etc. */
    IR2_OPND fcsr = latxs_ra_alloc_itemp();
    IR2_OPND overflow = latxs_ra_alloc_itemp();
    if(!option_fast_masked_v) {
        latxs_append_ir2_opnd2(LISA_MOVFCSR2GR, &fcsr, &latxs_fcsr_ir2_opnd);
        latxs_append_ir2_opnd2ii(LISA_BSTRPICK_W, &fcsr, &fcsr, FCSR_OFF_CAUSE_V, FCSR_OFF_CAUSE_V);
        latxs_append_ir2_opnd2i(LISA_SLLI_D, &overflow, &fcsr, opnd0_size - 1);
        /* mov the result */
        latxs_append_ir2_opnd2(LISA_MOVFR2GR_D, &temp1, &dest);

        latxs_append_ir2_opnd3(LISA_MASKNEZ, &fcsr, &temp1, &fcsr);
        latxs_append_ir2_opnd3(LISA_OR, &temp1, &overflow, &fcsr);
    } else {
        latxs_append_ir2_opnd2(LISA_MOVFR2GR_D, &temp1, &dest);
    }

    latxs_ra_free_temp(&fcsr);
    latxs_ra_free_temp(&overflow);
    latxs_ra_free_temp(&dest);
    latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
    latxs_store_ir2_to_ir1(&temp1, ir1_get_opnd(pir1, 0));

    latxs_ra_free_temp(&temp1);

    return true;
}

bool latxs_translate_cvttpd2pi(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR2_OPND dest = latxs_ra_alloc_mmx(ir1_opnd_base_reg_num(ir1_get_opnd(pir1, 0)));
    IR2_OPND src_lo = latxs_load_freg128_from_ir1(ir1_get_opnd(pir1, 1));
    IR2_OPND src_hi = latxs_ra_alloc_ftemp();
    latxs_append_ir2_opnd2i(LISA_VEXTRINS_D, &src_hi, &src_lo, VEXTRINS_IMM_4_0(0, 1));

    IR2_OPND ftemp_src1 = latxs_ra_alloc_ftemp();
    IR2_OPND ftemp_src2 = latxs_ra_alloc_ftemp();
    IR2_OPND ftemp_final = latxs_ra_alloc_ftemp();
    IR2_OPND label_for_flow1 = latxs_ir2_opnd_new_label();
    IR2_OPND label_for_flow2 = latxs_ir2_opnd_new_label();
    IR2_OPND label_over = latxs_ir2_opnd_new_label();
    IR2_OPND label_high = latxs_ir2_opnd_new_label();

    /*
     * verify the first double scalar operand is unorder or overflow
     * or under flow?
     */
    IR2_OPND ftemp_src_temp1 = latxs_ra_alloc_ftemp();
    IR2_OPND ftemp_src_temp2 = latxs_ra_alloc_ftemp();

    /* convertion has to be done first, or INVALID exception may be missed */
    latxs_append_ir2_opnd3(LISA_VREPLVE_D, &ftemp_src_temp1, &src_lo, &latxs_zero_ir2_opnd);
    latxs_append_ir2_opnd3(LISA_VFTINTRZ_W_D, &ftemp_src_temp2, &ftemp_src_temp1, &ftemp_src_temp1);
    latxs_tr_gen_simd_fpe_check_t(pir1, NULL);

    IR2_OPND temp_int = latxs_ra_alloc_itemp();
    IR2_OPND temp_fcsr = latxs_ra_alloc_itemp();
    if(!option_fast_masked_v) {
        /* check if INVALID bit in fcsr is set */
        latxs_append_ir2_opnd2(LISA_MOVFCSR2GR, &temp_fcsr, &latxs_fcsr_ir2_opnd);
        latxs_append_ir2_opnd2ii(LISA_BSTRPICK_W, &temp_fcsr, &temp_fcsr, FCSR_OFF_CAUSE_V, FCSR_OFF_CAUSE_V);
        latxs_append_ir2_opnd2(LISA_BNEZ, &temp_fcsr, &label_for_flow1);

        /* ##if## no INVALID exception happend, save the lower bits result */
        latxs_append_ir2_opnd2(LISA_FMOV_D, &ftemp_src1, &ftemp_src_temp2);
        latxs_append_ir2_opnd1(LISA_B, &label_high);

        /* ##else## INVALID exception did happen, load 0x80000000 manually*/
        latxs_append_ir2_opnd1(LISA_LABEL, &label_for_flow1);
        latxs_load_imm32_to_ir2(&temp_int, 0x80000000, EXMode_N);
        latxs_append_ir2_opnd2(LISA_MOVGR2FR_D, &ftemp_src1, &temp_int);
        latxs_append_ir2_opnd1(LISA_LABEL, &label_high);
    } else {
        latxs_append_ir2_opnd2(LISA_FMOV_D, &ftemp_src1, &ftemp_src_temp2);
    }

    /*
     * verify the second double scalar operand is unorder or overflow
     * or under flow?
     */
    latxs_append_ir2_opnd3(LISA_VREPLVE_D, &ftemp_src_temp1, &src_hi, &latxs_zero_ir2_opnd);
    latxs_append_ir2_opnd3(LISA_VFTINTRZ_W_D, &ftemp_src_temp2, &ftemp_src_temp1, &ftemp_src_temp1);

    if(!option_fast_masked_v) {
        /* check if INVALID bit in fcsr is set */
        latxs_append_ir2_opnd2(LISA_MOVFCSR2GR, &temp_fcsr, &latxs_fcsr_ir2_opnd);
        latxs_append_ir2_opnd2ii(LISA_BSTRPICK_W, &temp_fcsr, &temp_fcsr, FCSR_OFF_CAUSE_V, FCSR_OFF_CAUSE_V);
        latxs_append_ir2_opnd2(LISA_BNEZ, &temp_fcsr, &label_for_flow2);

        /* ##if## no INVALID exception happend, save the lower bits result */
        latxs_append_ir2_opnd2(LISA_FMOV_D, &ftemp_src2, &ftemp_src_temp2);
        latxs_append_ir2_opnd1(LISA_B, &label_over);

        /* ##else## INVALID exception did happen, load 0x80000000 manually*/
        latxs_append_ir2_opnd1(LISA_LABEL, &label_for_flow2);
        latxs_load_imm32_to_ir2(&temp_int, 0x80000000, EXMode_N);
        latxs_append_ir2_opnd2(LISA_MOVGR2FR_D, &ftemp_src2, &temp_int);
        latxs_append_ir2_opnd1(LISA_LABEL, &label_over);
    } else {
        latxs_append_ir2_opnd2(LISA_FMOV_D, &ftemp_src2, &ftemp_src_temp2);
    }

    latxs_ra_free_temp(&ftemp_src_temp1);
    latxs_ra_free_temp(&ftemp_src_temp2);
    latxs_ra_free_temp(&temp_int);
    latxs_ra_free_temp(&temp_fcsr);
    latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
    /**
     * merge, use ftemp_final as transfer register to prevent damaging the
     * 64 bits of the dest.
     */
    latxs_append_ir2_opnd3(LISA_VILVL_W, &ftemp_final, &ftemp_src2, &ftemp_src1);
    latxs_append_ir2_opnd2i(LISA_VEXTRINS_D, &dest, &ftemp_final, VEXTRINS_IMM_4_0(0, 0));

    latxs_ra_free_temp(&ftemp_src1);
    latxs_ra_free_temp(&ftemp_src2);
    latxs_ra_free_temp(&ftemp_final);

    return true;
}

bool latxs_translate_cvtpd2dq(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR2_OPND dest = latxs_load_freg128_from_ir1(ir1_get_opnd(pir1, 0));
    IR2_OPND src_lo = latxs_load_freg128_from_ir1(ir1_get_opnd(pir1, 1));
    IR2_OPND src_hi = latxs_ra_alloc_ftemp();
    latxs_append_ir2_opnd2i(LISA_VEXTRINS_D, &src_hi, &src_lo, VEXTRINS_IMM_4_0(0, 1));

    IR2_OPND ftemp_src1 = latxs_ra_alloc_ftemp();
    IR2_OPND ftemp_src2 = latxs_ra_alloc_ftemp();
    IR2_OPND ftemp_final = latxs_ra_alloc_ftemp();
    IR2_OPND label_for_flow1 = latxs_ir2_opnd_new_label();
    IR2_OPND label_for_flow2 = latxs_ir2_opnd_new_label();
    IR2_OPND label_over = latxs_ir2_opnd_new_label();
    IR2_OPND label_high = latxs_ir2_opnd_new_label();

    /*
     * verify the first double scalar operand is unorder or overflow
     * or under flow?
     */
    IR2_OPND ftemp_src_temp1 = latxs_ra_alloc_ftemp();
    IR2_OPND ftemp_src_temp2 = latxs_ra_alloc_ftemp();

    /* convertion has to be done first, or INVALID exception may be missed */
    latxs_append_ir2_opnd3(LISA_VREPLVE_D, &ftemp_src_temp1, &src_lo, &latxs_zero_ir2_opnd);
    latxs_append_ir2_opnd3(LISA_VFTINT_W_D, &ftemp_src_temp2, &ftemp_src_temp1, &ftemp_src_temp1);
    latxs_tr_gen_simd_fpe_check_t(pir1, NULL);

    IR2_OPND temp_int = latxs_ra_alloc_itemp();
    IR2_OPND temp_fcsr = latxs_ra_alloc_itemp();
    if(!option_fast_masked_v) {
        /* check if INVALID bit in fcsr is set */
        latxs_append_ir2_opnd2(LISA_MOVFCSR2GR, &temp_fcsr, &latxs_fcsr_ir2_opnd);
        latxs_append_ir2_opnd2ii(LISA_BSTRPICK_W, &temp_fcsr, &temp_fcsr, FCSR_OFF_CAUSE_V, FCSR_OFF_CAUSE_V);
        latxs_append_ir2_opnd2(LISA_BNEZ, &temp_fcsr, &label_for_flow1);

        /* ##if## no INVALID exception happend, save the lower bits result */
        latxs_append_ir2_opnd2(LISA_FMOV_D, &ftemp_src1, &ftemp_src_temp2);
        latxs_append_ir2_opnd1(LISA_B, &label_high);

        /* ##else## INVALID exception did happen, load 0x80000000 manually*/
        latxs_append_ir2_opnd1(LISA_LABEL, &label_for_flow1);
        latxs_load_imm32_to_ir2(&temp_int, 0x80000000, EXMode_N);
        latxs_append_ir2_opnd2(LISA_MOVGR2FR_D, &ftemp_src1, &temp_int);
        latxs_append_ir2_opnd1(LISA_LABEL, &label_high);
    } else {
        latxs_append_ir2_opnd2(LISA_FMOV_D, &ftemp_src1, &ftemp_src_temp2);
    }

    /*
     * verify the second double scalar operand is unorder or overflow
     * or under flow?
     */
    latxs_append_ir2_opnd3(LISA_VREPLVE_D, &ftemp_src_temp1, &src_hi, &latxs_zero_ir2_opnd);
    latxs_append_ir2_opnd3(LISA_VFTINT_W_D, &ftemp_src_temp2, &ftemp_src_temp1, &ftemp_src_temp1);

    if(!option_fast_masked_v) {
        /* check if INVALID bit in fcsr is set */
        latxs_append_ir2_opnd2(LISA_MOVFCSR2GR, &temp_fcsr, &latxs_fcsr_ir2_opnd);
        latxs_append_ir2_opnd2ii(LISA_BSTRPICK_W, &temp_fcsr, &temp_fcsr, FCSR_OFF_CAUSE_V, FCSR_OFF_CAUSE_V);
        latxs_append_ir2_opnd2(LISA_BNEZ, &temp_fcsr, &label_for_flow2);

        /* ##if## no INVALID exception happend, save the lower bits result */
        latxs_append_ir2_opnd2(LISA_FMOV_D, &ftemp_src2, &ftemp_src_temp2);
        latxs_append_ir2_opnd1(LISA_B, &label_over);

        /* ##else## INVALID exception did happen, load 0x80000000 manually*/
        latxs_append_ir2_opnd1(LISA_LABEL, &label_for_flow2);
        latxs_load_imm32_to_ir2(&temp_int, 0x80000000, EXMode_N);
        latxs_append_ir2_opnd2(LISA_MOVGR2FR_D, &ftemp_src2, &temp_int);
        latxs_append_ir2_opnd1(LISA_LABEL, &label_over);
    } else {
        latxs_append_ir2_opnd2(LISA_FMOV_D, &ftemp_src2, &ftemp_src_temp2);
    }

    latxs_ra_free_temp(&ftemp_src_temp1);
    latxs_ra_free_temp(&ftemp_src_temp2);
    latxs_ra_free_temp(&temp_int);
    latxs_ra_free_temp(&temp_fcsr);
    latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
    /**
     * merge, use ftemp_final as transfer register to prevent damaging the
     * 64 bits of the dest.
     */
    latxs_append_ir2_opnd3(LISA_VILVL_W, &ftemp_final, &ftemp_src2, &ftemp_src1);
    latxs_append_ir2_opnd2i(LISA_VANDI_B, &dest, &dest, 0);
    latxs_append_ir2_opnd2i(LISA_VEXTRINS_D, &dest, &ftemp_final, VEXTRINS_IMM_4_0(0, 0));

    latxs_ra_free_temp(&ftemp_src1);
    latxs_ra_free_temp(&ftemp_src2);
    latxs_ra_free_temp(&ftemp_final);

    return true;
}

bool latxs_translate_cvtps2dq(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR2_OPND temp_fcsr = latxs_ra_alloc_itemp();
    IR2_OPND label_over = latxs_ir2_opnd_new_label();
    IR2_OPND label_second_operand = latxs_ir2_opnd_new_label();
    IR2_OPND label_third_operand = latxs_ir2_opnd_new_label();
    IR2_OPND label_fourth_operand = latxs_ir2_opnd_new_label();
    IR2_OPND dest = latxs_load_freg128_from_ir1(ir1_get_opnd(pir1, 0));
    IR2_OPND src = latxs_load_freg128_from_ir1(ir1_get_opnd(pir1, 1));

    IR2_OPND dest_t;
    if (SIMD_FPE_CHECK) {
        dest_t = latxs_ra_alloc_ftemp();
        if(option_enable_lasx) {
            latxs_append_ir2_opnd3(LISA_XVOR_V, &dest_t, &dest, &dest);
        } else {
            latxs_append_ir2_opnd3(LISA_VOR_V, &dest_t, &dest, &dest);
        }
    }

    latxs_append_ir2_opnd2(LISA_VFTINT_W_S, &dest, &src);
    latxs_tr_gen_simd_fpe_check_t(pir1, &dest_t);

    if(!option_fast_masked_v) {
        /* check if INVALID bit in fcsr is set */
        latxs_append_ir2_opnd2(LISA_MOVFCSR2GR, &temp_fcsr, &latxs_fcsr_ir2_opnd);
        latxs_append_ir2_opnd2ii(LISA_BSTRPICK_W, &temp_fcsr, &temp_fcsr, FCSR_OFF_CAUSE_V, FCSR_OFF_CAUSE_V);

        /* if no INVALID exception happend, convertion done */
        latxs_append_ir2_opnd2(LISA_BEQZ, &temp_fcsr, &label_over);
    }

    if(!option_fast_masked_v) {
        IR2_OPND temp_int = latxs_ra_alloc_itemp();
        IR2_OPND temp_operand_count = latxs_ra_alloc_itemp();
        IR2_OPND ftemp_src_temp1 = latxs_ra_alloc_ftemp();
        IR2_OPND ftemp_src_temp2 = latxs_ra_alloc_ftemp();

        /* if INVALID exception did happen, check the four operands separately */
        latxs_load_imm32_to_ir2(&temp_int, 0x80000000, EXMode_Z);

        /* check the first single operand */
        latxs_append_ir2_opnd3(LISA_VREPLVE_W, &ftemp_src_temp1, &src, &latxs_zero_ir2_opnd);
        latxs_append_ir2_opnd2(LISA_VFTINT_W_S, &ftemp_src_temp2, &ftemp_src_temp1);
        latxs_append_ir2_opnd2(LISA_MOVFCSR2GR, &temp_fcsr, &latxs_fcsr_ir2_opnd);
        latxs_append_ir2_opnd2ii(LISA_BSTRPICK_W, &temp_fcsr, &temp_fcsr, FCSR_OFF_CAUSE_V, FCSR_OFF_CAUSE_V);
        latxs_append_ir2_opnd2(LISA_BEQZ, &temp_fcsr, &label_second_operand);
        latxs_append_ir2_opnd2i(LISA_VINSGR2VR_W, &dest, &temp_int, 0);

        /* check the second single operand */
        latxs_append_ir2_opnd1(LISA_LABEL, &label_second_operand);
        latxs_load_imm32_to_ir2(&temp_operand_count, 0x1, EXMode_Z);
        latxs_append_ir2_opnd3(LISA_VREPLVE_W, &ftemp_src_temp1, &src, &temp_operand_count);
        latxs_append_ir2_opnd2(LISA_VFTINT_W_S, &ftemp_src_temp2, &ftemp_src_temp1);
        latxs_append_ir2_opnd2(LISA_MOVFCSR2GR, &temp_fcsr, &latxs_fcsr_ir2_opnd);
        latxs_append_ir2_opnd2ii(LISA_BSTRPICK_W, &temp_fcsr, &temp_fcsr, FCSR_OFF_CAUSE_V, FCSR_OFF_CAUSE_V);
        latxs_append_ir2_opnd2(LISA_BEQZ, &temp_fcsr, &label_third_operand);
        latxs_append_ir2_opnd2i(LISA_VINSGR2VR_W, &dest, &temp_int, 1);

        /* check the third single operand */
        latxs_append_ir2_opnd1(LISA_LABEL, &label_third_operand);
        latxs_load_imm32_to_ir2(&temp_operand_count, 0x2, EXMode_Z);
        latxs_append_ir2_opnd3(LISA_VREPLVE_W, &ftemp_src_temp1, &src, &temp_operand_count);
        latxs_append_ir2_opnd2(LISA_VFTINT_W_S, &ftemp_src_temp2, &ftemp_src_temp1);
        latxs_append_ir2_opnd2(LISA_MOVFCSR2GR, &temp_fcsr, &latxs_fcsr_ir2_opnd);
        latxs_append_ir2_opnd2ii(LISA_BSTRPICK_W, &temp_fcsr, &temp_fcsr, FCSR_OFF_CAUSE_V, FCSR_OFF_CAUSE_V);
        latxs_append_ir2_opnd2(LISA_BEQZ, &temp_fcsr, &label_fourth_operand);
        latxs_append_ir2_opnd2i(LISA_VINSGR2VR_W, &dest, &temp_int, 2);

        /* check the fourth single operand */
        latxs_append_ir2_opnd1(LISA_LABEL, &label_fourth_operand);
        latxs_load_imm32_to_ir2(&temp_operand_count, 0x3, EXMode_Z);
        latxs_append_ir2_opnd3(LISA_VREPLVE_W, &ftemp_src_temp1, &src, &temp_operand_count);
        latxs_append_ir2_opnd2(LISA_VFTINT_W_S, &ftemp_src_temp2, &ftemp_src_temp1);
        latxs_append_ir2_opnd2(LISA_MOVFCSR2GR, &temp_fcsr, &latxs_fcsr_ir2_opnd);
        latxs_append_ir2_opnd2ii(LISA_BSTRPICK_W, &temp_fcsr, &temp_fcsr, FCSR_OFF_CAUSE_V, FCSR_OFF_CAUSE_V);
        latxs_append_ir2_opnd2(LISA_BEQZ, &temp_fcsr, &label_over);
        latxs_append_ir2_opnd2i(LISA_VINSGR2VR_W, &dest, &temp_int, 3);

        latxs_ra_free_temp(&temp_int);
        latxs_ra_free_temp(&temp_operand_count);
        latxs_ra_free_temp(&ftemp_src_temp1);
        latxs_ra_free_temp(&ftemp_src_temp2);
    }

    latxs_append_ir2_opnd1(LISA_LABEL, &label_over);

    latxs_ra_free_temp(&temp_fcsr);

    return true;
}

static bool __latxs_translate_cvttpx2dq(IR1_INST *pir1)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);
    IR1_OPCODE op = ir1_opcode(pir1);
    IR2_OPCODE split_inst = LISA_INVALID;
    IR2_OPCODE cvt_inst = LISA_INVALID;
    switch (op) {
    case X86_INS_CVTTPS2DQ:
        split_inst = LISA_VEXTRINS_W;
        cvt_inst = LISA_FTINTRZ_W_S;
        break;
    case X86_INS_CVTTPD2DQ:
        split_inst = LISA_VEXTRINS_D;
        cvt_inst = LISA_FTINTRZ_W_D;
        break;
    default:
        ir1_dump(pir1);
        lsassertm(0, "ir1 is not supported.\n");
        break;
    }

    IR2_OPND src = XMM_LOAD128(opnd1);
    IR2_OPND src_hi = latxs_ra_alloc_ftemp();
    IR2_OPND src_lo = latxs_ra_alloc_ftemp();
    latxs_append_ir2_opnd2i(split_inst, &src_hi, &src, VEXTRINS_IMM_4_0(0, 1));

    /* low 32/64 bit */
    IR2_OPND ftemp_src_lo = latxs_ra_alloc_ftemp();
    latxs_append_ir2_opnd2(cvt_inst, &ftemp_src_lo, &src);
    latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
    /* load fcsr */
    IR2_OPND fcsr_lo = latxs_ra_alloc_itemp();
    IR2_OPND vtemps = latxs_ra_alloc_ftemp();
    if(!option_fast_masked_v) {
        latxs_append_ir2_opnd2(LISA_MOVFCSR2GR, &fcsr_lo, &latxs_fcsr_ir2_opnd);
        latxs_append_ir2_opnd2ii(LISA_BSTRPICK_W, &fcsr_lo, &fcsr_lo, FCSR_OFF_CAUSE_V, FCSR_OFF_CAUSE_V);
        latxs_append_ir2_opnd2i(LISA_VINSGR2VR_W, &vtemps, &fcsr_lo, 0);
    }

    /* high 32/64 bit */
    IR2_OPND ftemp_src_hi = latxs_ra_alloc_ftemp();
    latxs_append_ir2_opnd2(cvt_inst, &ftemp_src_hi, &src_hi);
    latxs_ra_free_temp(&src_hi);
    latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
    IR2_OPND fcsr_hi = latxs_ra_alloc_itemp();
    if(!option_fast_masked_v) {
        latxs_append_ir2_opnd2(LISA_MOVFCSR2GR, &fcsr_hi, &latxs_fcsr_ir2_opnd);
        latxs_append_ir2_opnd2ii(LISA_BSTRPICK_W, &fcsr_hi, &fcsr_hi, FCSR_OFF_CAUSE_V, FCSR_OFF_CAUSE_V);
        latxs_append_ir2_opnd2i(LISA_VINSGR2VR_W, &vtemps, &fcsr_hi, 1);
    }

    /* shuf data */
    IR2_OPND vtemp1 = latxs_ra_alloc_ftemp();
    latxs_append_ir2_opnd3(LISA_VILVL_W, &vtemp1, &ftemp_src_hi, &ftemp_src_lo);

    if (op == X86_INS_CVTTPS2DQ) {
        /* CVTPS2DQ will use 4 part */
        latxs_append_ir2_opnd2i(split_inst, &src_lo, &src, VEXTRINS_IMM_4_0(0, 2));
        latxs_append_ir2_opnd2(cvt_inst, &ftemp_src_lo, &src_lo);
        latxs_ra_free_temp(&src_lo);
        latxs_tr_gen_simd_fpe_check_t(pir1, NULL);

        src_hi = latxs_ra_alloc_ftemp();
        latxs_append_ir2_opnd2i(split_inst, &src_hi, &src, VEXTRINS_IMM_4_0(0, 3));

        if(!option_fast_masked_v) {
            /* load fcsr */
            latxs_append_ir2_opnd2(LISA_MOVFCSR2GR, &fcsr_lo, &latxs_fcsr_ir2_opnd);
            latxs_append_ir2_opnd2ii(LISA_BSTRPICK_W, &fcsr_lo, &fcsr_lo, FCSR_OFF_CAUSE_V, FCSR_OFF_CAUSE_V);
            latxs_append_ir2_opnd2i(LISA_VINSGR2VR_W, &vtemps, &fcsr_lo, 2);
        }

        /* high 32/64 bit */
        latxs_append_ir2_opnd2(cvt_inst, &ftemp_src_hi, &src_hi);
        latxs_ra_free_temp(&src_hi);
        latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
        if(!option_fast_masked_v) {
            latxs_append_ir2_opnd2(LISA_MOVFCSR2GR, &fcsr_hi, &latxs_fcsr_ir2_opnd);
            latxs_append_ir2_opnd2ii(LISA_BSTRPICK_W, &fcsr_hi, &fcsr_hi, FCSR_OFF_CAUSE_V, FCSR_OFF_CAUSE_V);
            latxs_append_ir2_opnd2i(LISA_VINSGR2VR_W, &vtemps, &fcsr_hi, 3);
        }

        /* shuf data */
        latxs_append_ir2_opnd2i(LISA_VEXTRINS_W, &vtemp1, &ftemp_src_lo, VEXTRINS_IMM_4_0(2, 0));
        latxs_append_ir2_opnd2i(LISA_VEXTRINS_W, &vtemp1, &ftemp_src_hi, VEXTRINS_IMM_4_0(3, 0));
    }
    latxs_ra_free_temp(&ftemp_src_hi);
    latxs_ra_free_temp(&ftemp_src_lo);
    latxs_ra_free_temp(&fcsr_lo);
    latxs_ra_free_temp(&fcsr_hi);

    /* set vtemps */
    if(!option_fast_masked_v) {
        latxs_append_ir2_opnd2i(LISA_VSEQI_W, &vtemps, &vtemps, 1);
    } else {
        latxs_append_ir2_opnd2i(LISA_VINSGR2VR_D, &vtemps, &latxs_zero_ir2_opnd, 0);
        latxs_append_ir2_opnd2i(LISA_VINSGR2VR_D, &vtemps, &latxs_zero_ir2_opnd, 1);
    }

    /* add 0x80..0 */
    IR2_OPND vtemp2 = latxs_ra_alloc_ftemp();
    /* ui13: 13'b1 0011 [1000 0000] */
    latxs_append_ir2_opnd1i(LISA_VLDI, &vtemp2, 0x1380);

    /* calc result */
    IR2_OPND dest = latxs_load_freg128_from_ir1(opnd0);

    latxs_append_ir2_opnd4(LISA_VBITSEL_V, &dest, &vtemp1, &vtemp2, &vtemps);

    if (op == X86_INS_CVTTPD2DQ) {
        latxs_append_ir2_opnd2i(LISA_VINSGR2VR_D, &dest, &latxs_zero_ir2_opnd, 1);
    }

    latxs_ra_free_temp(&vtemp1);
    latxs_ra_free_temp(&vtemp2);
    latxs_ra_free_temp(&vtemps);

    return true;
}

bool latxs_translate_cvttps2dq(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    return __latxs_translate_cvttpx2dq(pir1);
}

bool latxs_translate_cvttpd2dq(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    return __latxs_translate_cvttpx2dq(pir1);
}


bool latxs_translate_cvtdq2pd(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));

    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);

    IR2_OPND dest_t;
    if (SIMD_FPE_CHECK) {
        dest_t = latxs_ra_alloc_ftemp();
        if(option_enable_lasx) {
            latxs_append_ir2_opnd3(LISA_XVOR_V, &dest_t, &dest, &dest);
        } else {
            latxs_append_ir2_opnd3(LISA_VOR_V, &dest_t, &dest, &dest);
        }
    }

    latxs_append_ir2_opnd2(LISA_VFFINTL_D_W, &dest, &src);
    latxs_tr_gen_simd_fpe_check_t(pir1, &dest_t);

    return true;
}

bool latxs_translate_cvtdq2ps(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));
    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);

    IR2_OPND dest_t;
    if (SIMD_FPE_CHECK) {
        dest_t = latxs_ra_alloc_ftemp();
        if(option_enable_lasx) {
            latxs_append_ir2_opnd3(LISA_XVOR_V, &dest_t, &dest, &dest);
        } else {
            latxs_append_ir2_opnd3(LISA_VOR_V, &dest_t, &dest, &dest);
        }
    }

    latxs_append_ir2_opnd2(LISA_VFFINT_S_W, &dest, &src);
    latxs_tr_gen_simd_fpe_check_t(pir1, &dest_t);

    return true;
}

bool latxs_translate_cvtpd2ps(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));
    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);

    IR2_OPND dest_t;
    if (SIMD_FPE_CHECK) {
        dest_t = latxs_ra_alloc_ftemp();
        if(option_enable_lasx) {
            latxs_append_ir2_opnd3(LISA_XVOR_V, &dest_t, &dest, &dest);
        } else {
            latxs_append_ir2_opnd3(LISA_VOR_V, &dest_t, &dest, &dest);
        }
    }

    latxs_append_ir2_opnd3(LISA_VFCVT_S_D, &dest, &src, &src);
    latxs_tr_gen_simd_fpe_check_t(pir1, &dest_t);
    if(option_enable_lasx) {
        latxs_append_ir2_opnd2i(LISA_XVPICKVE_D, &dest, &dest, 0);
    } else {
        latxs_append_ir2_opnd2i(LISA_VINSGR2VR_D, &dest, &latxs_zero_ir2_opnd, 1);
    }
    return true;
}

bool latxs_translate_cvtsd2ss(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));
    /* TODO:simply */
    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);
    bool dead_high = SHBR_ACTIVE_32(pir1);

    if (dead_high && !SIMD_FPE_CHECK) {
        latxs_append_ir2_opnd2(LISA_FCVT_S_D, &dest, &src);
    } else {
        IR2_OPND temp = latxs_ra_alloc_ftemp();
        latxs_append_ir2_opnd2(LISA_FCVT_S_D, &temp, &src);
        latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
        if (dead_high) {
            latxs_append_ir2_opnd2(LISA_FMOV_S, &dest, &temp);
        } else if (option_enable_lasx) {
            latxs_append_ir2_opnd2i(LISA_XVINSVE0_W, &dest, &temp, 0);
        } else {
            latxs_append_ir2_opnd2i(LISA_VEXTRINS_W, &dest, &temp, 0);
        }
    }

    return true;
}

bool latxs_translate_cvtsi2sd(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));
    /* TODO:simply */
    IR2_OPND dest = XMM_LOAD128(opnd0);

    IR2_OPND src = latxs_ra_alloc_itemp();
    latxs_load_ir1_to_ir2(&src, opnd1, EXMode_S);

    IR2_OPND temp_src = latxs_ra_alloc_ftemp();
    latxs_append_ir2_opnd2(LISA_MOVGR2FR_D, &temp_src, &src);

    if (ir1_opnd_size(opnd0 + 1) == 64) {
        latxs_append_ir2_opnd2(LISA_FFINT_D_L, &temp_src, &temp_src);
    } else {
        latxs_append_ir2_opnd2(LISA_FFINT_D_W, &temp_src, &temp_src);
    }
    latxs_tr_gen_simd_fpe_check_t(pir1, NULL);

    latxs_append_ir2_opnd2i(LISA_VEXTRINS_D, &dest, &temp_src,
                                 VEXTRINS_IMM_4_0(0, 0));
    return true;
}

bool latxs_translate_cvtss2sd(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));

    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);
    bool dead_high = SHBR_ACTIVE_64(pir1);

    if (dead_high && !SIMD_FPE_CHECK) {
        latxs_append_ir2_opnd2(LISA_FCVT_D_S, &dest, &src);
    } else {
        IR2_OPND temp = latxs_ra_alloc_ftemp();
        latxs_append_ir2_opnd2(LISA_FCVT_D_S, &temp, &src);
        latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
        if (dead_high) {
            latxs_append_ir2_opnd2(LISA_FMOV_D, &dest, &temp);
        } else if (option_enable_lasx) {
            latxs_append_ir2_opnd2i(LISA_XVINSVE0_D, &dest, &temp, 0);
        } else {
            latxs_append_ir2_opnd2i(LISA_VEXTRINS_D, &dest, &temp, 0);
        }
    }

    return true;
}

bool latxs_translate_divpd(IR1_INST *pir1)
{
    lsassert(ir1_opnd_is_xmm(ir1_get_opnd(pir1, 0)));
    IR2_OPND dest = XMM_LOAD128(ir1_get_opnd(pir1, 0));
    IR2_OPND src  = XMM_LOAD128(ir1_get_opnd(pir1, 1));

    IR2_OPND dest_t;
    if (SIMD_FPE_CHECK) {
        dest_t = latxs_ra_alloc_ftemp();
        if(option_enable_lasx) {
            latxs_append_ir2_opnd3(LISA_XVOR_V, &dest_t, &dest, &dest);
        } else {
            latxs_append_ir2_opnd3(LISA_VOR_V, &dest_t, &dest, &dest);
        }
    }

    latxs_append_ir2_opnd3(LISA_VFDIV_D, &dest, &dest, &src);
    latxs_tr_gen_simd_fpe_check_t(pir1, &dest_t);
    return true;
}

bool latxs_translate_divps(IR1_INST *pir1)
{
    lsassert(ir1_opnd_is_xmm(ir1_get_opnd(pir1, 0)));
    IR2_OPND dest = XMM_LOAD128(ir1_get_opnd(pir1, 0));
    IR2_OPND src  = XMM_LOAD128(ir1_get_opnd(pir1, 1));

    IR2_OPND dest_t;
    if (SIMD_FPE_CHECK) {
        dest_t = latxs_ra_alloc_ftemp();
        if(option_enable_lasx) {
            latxs_append_ir2_opnd3(LISA_XVOR_V, &dest_t, &dest, &dest);
        } else {
            latxs_append_ir2_opnd3(LISA_VOR_V, &dest_t, &dest, &dest);
        }
    }

    latxs_append_ir2_opnd3(LISA_VFDIV_S, &dest, &dest, &src);

    latxs_tr_gen_simd_fpe_check_t(pir1, &dest_t);
    return true;
}

bool latxs_translate_divsd(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);
    bool dead_high = SHBR_ACTIVE_64(pir1);
    if (option_risk_simd_ignore_upper64 ||
        (dead_high && !SIMD_FPE_CHECK)) {
        latxs_append_ir2_opnd3(LISA_FDIV_D, &dest, &dest, &src);
    } else {
        IR2_OPND temp = latxs_ra_alloc_ftemp();
        latxs_append_ir2_opnd3(LISA_FDIV_D, &temp, &dest, &src);
        latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
        if (dead_high) {
            latxs_append_ir2_opnd2(LISA_FMOV_D, &dest, &temp);
        } else if (option_enable_lasx) {
            latxs_append_ir2_opnd2i(LISA_XVINSVE0_D, &dest, &temp, 0);
        } else {
            latxs_append_ir2_opnd2i(LISA_VEXTRINS_D, &dest, &temp, 0);
        }
    }

    return true;
}

bool latxs_translate_divss(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));
    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);
    bool dead_high = SHBR_ACTIVE_32(pir1);

    if (option_risk_simd_ignore_upper64 ||
        (dead_high && !SIMD_FPE_CHECK)) {
        latxs_append_ir2_opnd3(LISA_FDIV_S, &dest, &dest, &src);
    } else {
        IR2_OPND temp = latxs_ra_alloc_ftemp();
        latxs_append_ir2_opnd3(LISA_FDIV_S, &temp, &dest, &src);
        latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
        if (dead_high) {
            latxs_append_ir2_opnd2(LISA_FMOV_S, &dest, &temp);
        } else if (option_enable_lasx) {
            latxs_append_ir2_opnd2i(LISA_XVINSVE0_W, &dest, &temp, 0);
        } else {
            latxs_append_ir2_opnd2i(LISA_VEXTRINS_W, &dest, &temp, 0);
        }
    }

    return true;
}

bool latxs_translate_maxpd(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));
    if (ir1_opnd_is_xmm(opnd1)) {
        if (ir1_opnd_base_reg_num(opnd0) == ir1_opnd_base_reg_num(opnd1)) {
            return true;
        }
    }

    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);
    IR2_OPND mask = latxs_ra_alloc_ftemp();
    /* 1.if dest[i] > src[i]  (0 <= i < 4)
     *     mask[i] = 0xffffffffffffffff
     * else
     *     mask[i] = 0 */
    latxs_append_ir2_opnd3i(LISA_VFCMP_COND_D, &mask, &src, &dest, 0x3);
    latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
    /* 2. when dest[i] > src[i] is true, dest[i] = dest[i] */
    latxs_append_ir2_opnd3(LISA_VAND_V, &dest, &dest, &mask);
    /* 3. mask = ~mask */
    latxs_append_ir2_opnd3(LISA_VNOR_V, &mask, &mask, &mask);
    /* 4. when dest[i] > src[i] is false, dest[i] = src[i] */
    latxs_append_ir2_opnd3(LISA_VAND_V, &mask, &src, &mask);
    /* 5. get final result */
    latxs_append_ir2_opnd3(LISA_VOR_V, &dest, &dest, &mask);

    return true;
}

bool latxs_translate_maxps(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));
    if (ir1_opnd_is_xmm(opnd1)) {
        if (ir1_opnd_base_reg_num(opnd0) == ir1_opnd_base_reg_num(opnd1)) {
            return true;
        }
    }

    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);
    IR2_OPND mask = latxs_ra_alloc_ftemp();
    /* 1.if dest[i] > src[i]  (0 <= i < 4)
     *     mask[i] = 0xffffffffffffffff
     * else
     *     mask[i] = 0 */
    latxs_append_ir2_opnd3i(LISA_VFCMP_COND_S, &mask, &src, &dest, 0x3);
    latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
    /* 2. when dest[i] > src[i] is true, dest[i] = dest[i] */
    latxs_append_ir2_opnd3(LISA_VAND_V, &dest, &dest, &mask);
    /* 3. mask = ~mask */
    latxs_append_ir2_opnd3(LISA_VNOR_V, &mask, &mask, &mask);
    /* 4. when dest[i] > src[i] is false, dest[i] = src[i] */
    latxs_append_ir2_opnd3(LISA_VAND_V, &mask, &src, &mask);
    /* 5. get final result */
    latxs_append_ir2_opnd3(LISA_VOR_V, &dest, &dest, &mask);

    return true;
}

bool latxs_translate_maxsd(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);
    lsassert(ir1_opnd_is_xmm(opnd0));
    if (ir1_opnd_is_xmm(opnd1)) {
        if (ir1_opnd_base_reg_num(opnd0) == ir1_opnd_base_reg_num(opnd1)) {
            return true;
        }
    }

    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);
    latxs_append_ir2_opnd3i(LISA_FCMP_COND_D, &latxs_fcc0_ir2_opnd, &src, &dest, 0x3);
    latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
    if (option_risk_simd_ignore_upper64 || SHBR_ACTIVE_64(pir1)) {
        latxs_append_ir2_opnd4(LISA_FSEL, &dest, &src, &dest, &latxs_fcc0_ir2_opnd);
    } else {
        IR2_OPND temp = latxs_ra_alloc_ftemp();
        latxs_append_ir2_opnd4(LISA_FSEL, &temp, &src, &dest, &latxs_fcc0_ir2_opnd);
        if(option_enable_lasx) {
            latxs_append_ir2_opnd2i(LISA_XVINSVE0_D, &dest, &temp, 0);
        } else {
            latxs_append_ir2_opnd2i(LISA_VEXTRINS_D, &dest, &temp, 0);
        }
    }

    return true;
}

bool latxs_translate_maxss(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);
    lsassert(ir1_opnd_is_xmm(opnd0));
    if (ir1_opnd_is_xmm(opnd1)) {
        if (ir1_opnd_base_reg_num(opnd0) == ir1_opnd_base_reg_num(opnd1)) {
            return true;
        }
    }

    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);
    latxs_append_ir2_opnd3i(LISA_FCMP_COND_S, &latxs_fcc0_ir2_opnd, &src, &dest, 0x3);
    latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
    if (option_risk_simd_ignore_upper64 || SHBR_ACTIVE_32(pir1)) {
        latxs_append_ir2_opnd4(LISA_FSEL, &dest, &src, &dest, &latxs_fcc0_ir2_opnd);
    } else {
        IR2_OPND temp = latxs_ra_alloc_ftemp();
        latxs_append_ir2_opnd4(LISA_FSEL, &temp, &src, &dest, &latxs_fcc0_ir2_opnd);
        if(option_enable_lasx) {
            latxs_append_ir2_opnd2i(LISA_XVINSVE0_W, &dest, &temp, 0);
        } else {
            latxs_append_ir2_opnd2i(LISA_VEXTRINS_W, &dest, &temp, 0);
        }
    }

    return true;
}

bool latxs_translate_minpd(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));
    if (ir1_opnd_is_xmm(opnd1)) {
        if (ir1_opnd_base_reg_num(opnd0) == ir1_opnd_base_reg_num(opnd1)) {
            return true;
        }
    }

    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);
    IR2_OPND mask = latxs_ra_alloc_ftemp();
    /* 1.if dest[i] > src[i]  (0 <= i < 4)
     *     mask[i] = 0xffffffffffffffff
     * else
     *     mask[i] = 0 */
    latxs_append_ir2_opnd3i(LISA_VFCMP_COND_D, &mask, &dest, &src, 0x3);
    latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
    /* 2. when dest[i] > src[i] is true, dest[i] = dest[i] */
    latxs_append_ir2_opnd3(LISA_VAND_V, &dest, &dest, &mask);
    /* 3. mask = ~mask */
    latxs_append_ir2_opnd3(LISA_VNOR_V, &mask, &mask, &mask);
    /* 4. when dest[i] > src[i] is false, dest[i] = src[i] */
    latxs_append_ir2_opnd3(LISA_VAND_V, &mask, &src, &mask);
    /* 5. get final result */
    latxs_append_ir2_opnd3(LISA_VOR_V, &dest, &dest, &mask);

    return true;
}

bool latxs_translate_minps(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));
    if (ir1_opnd_is_xmm(opnd1)) {
        if (ir1_opnd_base_reg_num(opnd0) == ir1_opnd_base_reg_num(opnd1)) {
            return true;
        }
    }

    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);
    IR2_OPND mask = latxs_ra_alloc_ftemp();
    /* 1.if dest[i] > src[i]  (0 <= i < 4)
     *     mask[i] = 0xffffffffffffffff
     * else
     *     mask[i] = 0 */
    latxs_append_ir2_opnd3i(LISA_VFCMP_COND_S, &mask, &dest, &src, 0x3);
    latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
    /* 2. when dest[i] > src[i] is true, dest[i] = dest[i] */
    latxs_append_ir2_opnd3(LISA_VAND_V, &dest, &dest, &mask);
    /* 3. mask = ~mask */
    latxs_append_ir2_opnd3(LISA_VNOR_V, &mask, &mask, &mask);
    /* 4. when dest[i] > src[i] is false, dest[i] = src[i] */
    latxs_append_ir2_opnd3(LISA_VAND_V, &mask, &src, &mask);
    /* 5. get final result */
    latxs_append_ir2_opnd3(LISA_VOR_V, &dest, &dest, &mask);

    return true;
}

bool latxs_translate_minsd(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);
    lsassert(ir1_opnd_is_xmm(opnd0));
    if (ir1_opnd_is_xmm(opnd1)) {
        if (ir1_opnd_base_reg_num(opnd0) == ir1_opnd_base_reg_num(opnd1)) {
            return true;
        }
    }

    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);
    latxs_append_ir2_opnd3i(LISA_FCMP_COND_D, &latxs_fcc0_ir2_opnd, &dest, &src, 0x3);
    latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
    if (option_risk_simd_ignore_upper64 || SHBR_ACTIVE_64(pir1)) {
        latxs_append_ir2_opnd4(LISA_FSEL, &dest, &src, &dest, &latxs_fcc0_ir2_opnd);
    } else {
        IR2_OPND temp = latxs_ra_alloc_ftemp();
        latxs_append_ir2_opnd4(LISA_FSEL, &temp, &src, &dest, &latxs_fcc0_ir2_opnd);
        if(option_enable_lasx) {
            latxs_append_ir2_opnd2i(LISA_XVINSVE0_D, &dest, &temp, 0);
        } else {
            latxs_append_ir2_opnd2i(LISA_VEXTRINS_D, &dest, &temp, 0);
        }
    }

    return true;
}

bool latxs_translate_minss(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);
    lsassert(ir1_opnd_is_xmm(opnd0));
    if (ir1_opnd_is_xmm(opnd1)) {
        if (ir1_opnd_base_reg_num(opnd0) == ir1_opnd_base_reg_num(opnd1)) {
            return true;
        }
    }

    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);
    latxs_append_ir2_opnd3i(LISA_FCMP_COND_S, &latxs_fcc0_ir2_opnd, &dest, &src, 0x3);
    latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
    if (option_risk_simd_ignore_upper64 || SHBR_ACTIVE_32(pir1)) {
        latxs_append_ir2_opnd4(LISA_FSEL, &dest, &src, &dest, &latxs_fcc0_ir2_opnd);
    } else {
        IR2_OPND temp = latxs_ra_alloc_ftemp();
        latxs_append_ir2_opnd4(LISA_FSEL, &temp, &src, &dest, &latxs_fcc0_ir2_opnd);
        if(option_enable_lasx) {
            latxs_append_ir2_opnd2i(LISA_XVINSVE0_W, &dest, &temp, 0);
        } else {
            latxs_append_ir2_opnd2i(LISA_VEXTRINS_W, &dest, &temp, 0);
        }
    }

    return true;
}

bool latxs_translate_movmskpd(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *src = ir1_get_opnd(pir1, 1);
    IR1_OPND *dest = ir1_get_opnd(pir1, 0);
    lsassert(ir1_opnd_is_xmm(src));
    IR2_OPND temp = latxs_ra_alloc_ftemp();
    IR2_OPND src_ir2 = latxs_ra_alloc_xmm(XMM_REG(src));
    IR2_OPND dest_ir2 = latxs_ra_alloc_gpr(
        ir1_opnd_base_reg_num(dest));
    latxs_append_ir2_opnd2(LISA_VMSKLTZ_D, &temp, &src_ir2);
    latxs_append_ir2_opnd2(LISA_MOVFR2GR_D, &dest_ir2, &temp);
    return true;
}

bool latxs_translate_movmskps(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *src = ir1_get_opnd(pir1, 1);
    IR1_OPND *dest = ir1_get_opnd(pir1, 0);
    lsassert(ir1_opnd_is_xmm(src));
    IR2_OPND temp = latxs_ra_alloc_ftemp();
    IR2_OPND src_ir2 = latxs_ra_alloc_xmm(XMM_REG(src));
    IR2_OPND dest_ir2 = latxs_ra_alloc_gpr(
        ir1_opnd_base_reg_num(dest));
    latxs_append_ir2_opnd2(LISA_VMSKLTZ_W, &temp, &src_ir2);
    latxs_append_ir2_opnd2(LISA_MOVFR2GR_D, &dest_ir2, &temp);
    return true;
}

bool latxs_translate_movnti(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    if (option_by_hand_64 && latxs_translate_mov_byhand64(pir1)) {
        return true;
    }

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    IR2_OPND src = latxs_load_ireg_from_ir1(opnd1, EXMode_N);
    latxs_store_ir2_to_ir1(&src, opnd0);

    return true;
}

bool latxs_translate_movntdq(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd1));

    IR2_OPND src_ir2 = latxs_ra_alloc_xmm(XMM_REG(opnd1));
    latxs_store_freg128_to_ir1_mem(&src_ir2, opnd0);
    return true;
}

bool latxs_translate_movntpd(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd1));

    IR2_OPND src_ir2 = latxs_ra_alloc_xmm(XMM_REG(opnd1));
    latxs_store_freg128_to_ir1_mem(&src_ir2, opnd0);
    return true;
}

bool latxs_translate_movntps(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd1));

    IR2_OPND src_ir2 = latxs_ra_alloc_xmm(XMM_REG(opnd1));
    latxs_store_freg128_to_ir1_mem(&src_ir2, opnd0);
    return true;
}

bool latxs_translate_movntq(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_mmx(opnd1));

    IR2_OPND src_ir2 = XMM_LOADFP1(opnd1);
    latxs_store_freg_to_ir1(&src_ir2, opnd0, false);

    return true;
}

bool latxs_translate_mulpd(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));
    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);

    IR2_OPND dest_t;
    if (SIMD_FPE_CHECK) {
        dest_t = latxs_ra_alloc_ftemp();
        if(option_enable_lasx) {
            latxs_append_ir2_opnd3(LISA_XVOR_V, &dest_t, &dest, &dest);
        } else {
            latxs_append_ir2_opnd3(LISA_VOR_V, &dest_t, &dest, &dest);
        }
    }

    latxs_append_ir2_opnd3(LISA_VFMUL_D, &dest, &dest, &src);
    latxs_tr_gen_simd_fpe_check_t(pir1, &dest_t);

    return true;
}

bool latxs_translate_mulps(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));
    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);

    IR2_OPND dest_t;
    if (SIMD_FPE_CHECK) {
        dest_t = latxs_ra_alloc_ftemp();
        if(option_enable_lasx) {
            latxs_append_ir2_opnd3(LISA_XVOR_V, &dest_t, &dest, &dest);
        } else {
            latxs_append_ir2_opnd3(LISA_VOR_V, &dest_t, &dest, &dest);
        }
    }

    latxs_append_ir2_opnd3(LISA_VFMUL_S, &dest, &dest, &src);

    latxs_tr_gen_simd_fpe_check_t(pir1, &dest_t);

    return true;
}

bool latxs_translate_mulsd(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));

    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);
    bool dead_high = SHBR_ACTIVE_64(pir1);

    if (option_risk_simd_ignore_upper64 ||
        (dead_high && !SIMD_FPE_CHECK)) {
        latxs_append_ir2_opnd3(LISA_FMUL_D, &dest, &dest, &src);
    } else {
        IR2_OPND temp = latxs_ra_alloc_ftemp();
        latxs_append_ir2_opnd3(LISA_FMUL_D, &temp, &dest, &src);
        latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
        if (dead_high) {
            latxs_append_ir2_opnd2(LISA_FMOV_D, &dest, &temp);
        } else if (option_enable_lasx) {
            latxs_append_ir2_opnd2i(LISA_XVINSVE0_D, &dest, &temp, 0);
        } else {
            latxs_append_ir2_opnd2i(LISA_VEXTRINS_D, &dest, &temp, 0);
        }
    }

    return true;
}

bool latxs_translate_mulss(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));
    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);
    bool dead_high = SHBR_ACTIVE_32(pir1);
    if (option_risk_simd_ignore_upper64 ||
        (dead_high && !SIMD_FPE_CHECK)) {
        latxs_append_ir2_opnd3(LISA_FMUL_S, &dest, &dest, &src);
    } else {
        IR2_OPND temp = latxs_ra_alloc_ftemp();
        latxs_append_ir2_opnd3(LISA_FMUL_S, &temp, &dest, &src);
        latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
        if (dead_high) {
            latxs_append_ir2_opnd2(LISA_FMOV_S, &dest, &temp);
        } else if (option_enable_lasx) {
            latxs_append_ir2_opnd2i(LISA_XVINSVE0_W, &dest, &temp, 0);
        } else {
            latxs_append_ir2_opnd2i(LISA_VEXTRINS_W, &dest, &temp, 0);
        }
    }

    return true;
}

bool latxs_translate_orpd(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));
    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);
    latxs_append_ir2_opnd3(LISA_VOR_V, &dest, &dest, &src);
    return true;
}

bool latxs_translate_orps(IR1_INST *pir1)
{
    latxs_translate_orpd(pir1);
    return true;
}

bool latxs_translate_paddq(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    if (ir1_opnd_is_xmm(opnd0)) {
        IR2_OPND dest = XMM_LOAD128(opnd0);
        IR2_OPND src  = XMM_LOAD128(opnd1);
        latxs_append_ir2_opnd3(LISA_VADD_D, &dest, &dest, &src);
        return true;
    }

    IR2_OPND dest_lo = XMM_LOADFP1(opnd0);
    IR2_OPND src_lo  = XMM_LOADFP1(opnd1);

    latxs_append_ir2_opnd3(LISA_VADD_D, &dest_lo, &dest_lo, &src_lo);

    return true;
}

bool latxs_translate_pavgb(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    IR2_OPND dest = latxs_load_freg_from_ir1_1(opnd0, false);
    IR2_OPND src = latxs_load_freg_from_ir1_1(opnd1, false);

    latxs_append_ir2_opnd3(LISA_VAVGR_BU, &dest, &dest, &src);

    return true;
}

bool latxs_translate_pavgw(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    IR2_OPND dest = latxs_load_freg_from_ir1_1(opnd0, false);
    IR2_OPND src = latxs_load_freg_from_ir1_1(opnd1, false);

    latxs_append_ir2_opnd3(LISA_VAVGR_HU, &dest, &dest, &src);

    return true;
}

bool latxs_translate_pextrw(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);
    IR1_OPND *opnd2 = ir1_get_opnd(pir1, 2);

    uint8_t imm = ir1_opnd_uimm(opnd2);
    IR2_OPND src_ir2;
    IR2_OPND dest;
    if (ir1_opnd_is_xmm(opnd1)) {
        imm &= 7;
        if(ir1_opnd_is_mem(opnd0)) {
            dest = latxs_ra_alloc_itemp();
        } else {
            dest = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd0));
        }
        src_ir2 = latxs_ra_alloc_xmm(XMM_REG(opnd1));
        latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_HU, &dest, &src_ir2, imm);
        if(ir1_opnd_is_mem(opnd0)) {
            latxs_store_ir2_to_ir1(&dest, ir1_get_opnd(pir1, 0));
            latxs_ra_free_temp(&dest);
        }
        return true;
    } else if (ir1_opnd_is_mmx(opnd1)) {
        imm &= 3;
        dest = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd0));
        src_ir2 = latxs_ra_alloc_mmx(XMM_REG(opnd1));

        latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_HU, &dest, &src_ir2, imm);

        return true;
    } else {
        lsassert(0);
    }

    return false;
}

bool latxs_translate_pinsrw(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    IR2_OPND temp = latxs_load_ireg_from_ir1(opnd1, EXMode_N);

    IR1_OPND *opnd2 = ir1_get_opnd(pir1, 2);
    uint8_t imm = ir1_opnd_uimm(opnd2);
    IR2_OPND dest_ir2;
    if (ir1_opnd_is_xmm(opnd0)) {
        imm &= 7;
        dest_ir2 = latxs_ra_alloc_xmm(XMM_REG(opnd0));
    } else if (ir1_opnd_is_mmx(opnd0)) {
        imm &= 3;
        dest_ir2 = latxs_ra_alloc_mmx(XMM_REG(opnd0));
    } else {
        lsassert(0);
    }

    latxs_append_ir2_opnd2i(LISA_VINSGR2VR_H, &dest_ir2, &temp, imm);

    return true;
}

bool latxs_translate_pmaxsw(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    if (ir1_opnd_is_xmm(opnd0)) {
        /* PMAXSW xmm1, xmm2/m128 */
        IR2_OPND dest = XMM_LOAD128(opnd0);
        IR2_OPND src = XMM_LOAD128(opnd1);
        latxs_append_ir2_opnd3(LISA_VMAX_H, &dest, &dest, &src);
        return true;
    }

    /* PMAXSW mm1, mm2/m64 */
    IR2_OPND dest = XMM_LOADFP1(opnd0);
    IR2_OPND src = XMM_LOADFP1(opnd1);
    latxs_append_ir2_opnd3(LISA_VMAX_H, &dest, &dest, &src);

    return true;
}

bool latxs_translate_pmaxub(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    if (ir1_opnd_is_xmm(opnd0)) {
        /* PMAXUB xmm1, xmm2/m128 */
        IR2_OPND dest = XMM_LOAD128(opnd0);
        IR2_OPND src  = XMM_LOAD128(opnd1);
        latxs_append_ir2_opnd3(LISA_VMAX_BU, &dest, &dest, &src);
        return true;
    }

    /* PMAXUB mm1, mm2/m64 */
    IR2_OPND dest = XMM_LOADFP1(opnd0);
    IR2_OPND src  = XMM_LOADFP1(opnd1);
    latxs_append_ir2_opnd3(LISA_VMAX_BU, &dest, &dest, &src);

    return true;
}

bool latxs_translate_pminsw(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    if (ir1_opnd_is_xmm(opnd0)) {
        /* PMINSW xmm1, xmm2/m128 */
        IR2_OPND dest = XMM_LOAD128(opnd0);
        IR2_OPND src  = XMM_LOAD128(opnd1);
        latxs_append_ir2_opnd3(LISA_VMIN_H, &dest, &dest, &src);
        return true;
    }

    /* PMINSW mm1, mm2/m64 */
    IR2_OPND dest = XMM_LOADFP1(opnd0);
    IR2_OPND src  = XMM_LOADFP1(opnd1);
    latxs_append_ir2_opnd3(LISA_VMIN_H, &dest, &dest, &src);

    return true;
}

bool latxs_translate_pminub(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    if (ir1_opnd_is_xmm(opnd0)) {
        /* PMINUB xmm1, xmm2/m128 */
        IR2_OPND dest = XMM_LOAD128(opnd0);
        IR2_OPND src  = XMM_LOAD128(opnd1);
        latxs_append_ir2_opnd3(LISA_VMIN_BU, &dest, &dest, &src);
        return true;
    }

    /* PMINUB mm1, mm2/m64 */
    IR2_OPND dest = XMM_LOADFP1(opnd0);
    IR2_OPND src  = XMM_LOADFP1(opnd1);
    latxs_append_ir2_opnd3(LISA_VMIN_BU, &dest, &dest, &src);

    return true;
}

void latxs_gen_psadbw(IR2_OPND *dst, IR2_OPND *a, IR2_OPND *b)
{
    latxs_append_ir2_opnd3(LISA_VABSD_BU, dst, a, b);
    latxs_append_ir2_opnd3(LISA_VHADDW_HU_BU, dst, dst, dst);
    latxs_append_ir2_opnd3(LISA_VHADDW_WU_HU, dst, dst, dst);
    latxs_append_ir2_opnd3(LISA_VHADDW_DU_WU, dst, dst, dst);
}

bool latxs_translate_psadbw(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    if (ir1_opnd_is_xmm(opnd0)) {
        IR2_OPND d = XMM_LOAD128(opnd0);
        IR2_OPND s = XMM_LOAD128(opnd1);
        latxs_gen_psadbw(&d, &d, &s);
        return true;
    }

    IR2_OPND dest = latxs_load_freg_from_ir1_1(opnd0, false);
    IR2_OPND src = latxs_load_freg_from_ir1_1(opnd1, false);

    latxs_gen_psadbw(&dest, &dest, &src);

    return true;
}

bool latxs_translate_maskmovdqu(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    /* data */
    IR2_OPND dest = latxs_load_freg_from_ir1_1(opnd0, false);
    /* mask */
    IR2_OPND src = latxs_load_freg_from_ir1_1(opnd1, false);

    IR2_OPND mask = latxs_ra_alloc_ftemp();

    IR2_OPND one_msb = latxs_ra_alloc_itemp();
    IR2_OPND one_byte = latxs_ra_alloc_itemp();

    int addr_size = latxs_ir1_addr_size(pir1);

    if (lsenv->tr_data->sys.code64) {
        lsassert(addr_size == 4 || addr_size == 8);
    } else {
        lsassert(addr_size == 2 || addr_size == 4);
    }

    IR1_OPND mem_ir1_opnd;

    latxs_append_ir2_opnd2i(LISA_VANDI_B, &mask, &src, 0x80);

    int byte_num = 16;

    for (size_t i = 0; i < byte_num; i++) {
        latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_BU, &one_msb, &mask, i);
        latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_BU, &one_byte, &dest, i);
        IR2_OPND skip = latxs_ir2_opnd_new_label();
        latxs_append_ir2_opnd3(LISA_BEQ, &one_msb, &latxs_zero_ir2_opnd, &skip);

        ir1_opnd_build_full_mem(&mem_ir1_opnd, 8, X86_REG_DS, X86_REG_EDI,
                                      i, 0, 0);
        latxs_store_ir2_to_ir1_mem(&one_byte, &mem_ir1_opnd, addr_size);

        latxs_append_ir2_opnd1(LISA_LABEL, &skip);
    }

    latxs_ra_free_temp(&mask);
    latxs_ra_free_temp(&one_msb);
    latxs_ra_free_temp(&one_byte);

    return true;
}

bool latxs_translate_maskmovq(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    /* data */
    IR2_OPND dest = latxs_load_freg_from_ir1_1(opnd0, false);
    /* mask */
    IR2_OPND src = latxs_load_freg_from_ir1_1(opnd1, false);

    IR2_OPND mask = latxs_ra_alloc_ftemp();

    IR2_OPND one_msb = latxs_ra_alloc_itemp();
    IR2_OPND one_byte = latxs_ra_alloc_itemp();

    int addr_size = latxs_ir1_addr_size(pir1);

    if (lsenv->tr_data->sys.code64) {
        lsassert(addr_size == 4 || addr_size == 8);
    } else {
        lsassert(addr_size == 2 || addr_size == 4);
    }

    IR1_OPND mem_ir1_opnd;

    latxs_append_ir2_opnd2i(LISA_VANDI_B, &mask, &src, 0x80);

    int byte_num = 8;

    for (size_t i = 0; i < byte_num; i++) {
        latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_BU, &one_msb, &mask, i);
        latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_BU, &one_byte, &dest, i);
        IR2_OPND skip = latxs_ir2_opnd_new_label();
        latxs_append_ir2_opnd3(LISA_BEQ, &one_msb, &latxs_zero_ir2_opnd, &skip);

        ir1_opnd_build_full_mem(&mem_ir1_opnd, 8, X86_REG_DS, X86_REG_EDI,
                                      i, 0, 0);
        latxs_store_ir2_to_ir1_mem(&one_byte, &mem_ir1_opnd, addr_size);

        latxs_append_ir2_opnd1(LISA_LABEL, &skip);
    }

    latxs_ra_free_temp(&mask);
    latxs_ra_free_temp(&one_msb);
    latxs_ra_free_temp(&one_byte);

    return true;
}

bool latxs_translate_pmovmskb(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    IR1_OPND *dest = opnd0;
    IR1_OPND *src  = opnd1;

    if (ir1_opnd_is_xmm(src)) {
        IR2_OPND temp = latxs_ra_alloc_ftemp();
        IR2_OPND xmm_src  = latxs_ra_alloc_xmm(XMM_REG(src));
        IR2_OPND gpr_dest = latxs_ra_alloc_gpr(XMM_REG(dest));

        latxs_append_ir2_opnd2(LISA_VMSKLTZ_B,  &temp, &xmm_src);
        latxs_append_ir2_opnd2(LISA_MOVFR2GR_D, &gpr_dest, &temp);
        return true;
    }

    IR2_OPND ftemp = latxs_ra_alloc_ftemp();
    IR2_OPND itemp = latxs_ra_alloc_itemp();
    IR2_OPND src_opnd = latxs_ra_alloc_mmx(XMM_REG(src));

    latxs_append_ir2_opnd2(LISA_VMSKLTZ_B, &ftemp, &src_opnd);

    latxs_append_ir2_opnd2(LISA_MOVFR2GR_D, &itemp, &ftemp);
    latxs_append_ir2_opnd2i(LISA_ANDI, &itemp, &itemp, 0xff);
    latxs_store_ir2_to_ir1(&itemp, dest);

    latxs_ra_free_temp(&itemp);
    latxs_ra_free_temp(&ftemp);
    return true;
}

bool latxs_translate_pmulhw(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    IR2_OPND dest = latxs_load_freg_from_ir1_1(opnd0, false);
    IR2_OPND src = latxs_load_freg_from_ir1_1(opnd1, false);

    latxs_append_ir2_opnd3(LISA_VMUH_H, &dest, &dest, &src);

    return true;
}

bool latxs_translate_pmulhuw(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    IR2_OPND dest = latxs_load_freg_from_ir1_1(opnd0, false);
    IR2_OPND src = latxs_load_freg_from_ir1_1(opnd1, false);

    latxs_append_ir2_opnd3(LISA_VMUH_HU, &dest, &dest, &src);

    return true;
}

bool latxs_translate_pmaddwd(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    IR2_OPND dest = latxs_load_freg_from_ir1_1(opnd0, false);
    IR2_OPND src = latxs_load_freg_from_ir1_1(opnd1, false);

    IR2_OPND temp = latxs_ra_alloc_ftemp();
    latxs_append_ir2_opnd3(LISA_VXOR_V, &temp, &temp, &temp);
    latxs_append_ir2_opnd3(LISA_VMADDWEV_W_H, &temp, &dest, &src);
    latxs_append_ir2_opnd3(LISA_VMADDWOD_W_H, &temp, &dest, &src);
    latxs_append_ir2_opnd2i(LISA_VBSLL_V, &dest, &temp, 0);

    latxs_ra_free_temp(&temp);
    return true;
}

bool latxs_translate_pmuludq(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    if (ir1_opnd_is_xmm(opnd0)) {
        IR2_OPND dest = XMM_LOAD128(opnd0);
        IR2_OPND src  = XMM_LOAD128(opnd1);
        latxs_append_ir2_opnd3(LISA_VMULWEV_D_WU, &dest, &dest, &src);
        return true;
    }

    IR2_OPND dest_lo = XMM_LOADFP1(opnd0);
    IR2_OPND src_lo  = XMM_LOADFP1(opnd1);

    latxs_append_ir2_opnd3(LISA_VMULWEV_D_WU, &dest_lo,
                                              &dest_lo, &src_lo);
    return true;
}

bool latxs_translate_pshufd(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);
    IR1_OPND *opnd2 = ir1_get_opnd(pir1, 2);

    lsassert(ir1_opnd_is_xmm(opnd0));
    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);
    uint64_t imm8 = ir1_opnd_uimm(opnd2);

    latxs_append_ir2_opnd2i(LISA_VSHUF4I_W, &dest, &src, imm8);

    return true;
}

bool latxs_translate_pshufw(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    IR2_OPND dest = XMM_LOADFP1(opnd0);
    IR2_OPND src = XMM_LOADFP1(opnd1);

    IR1_OPND *imm8_reg = ir1_get_opnd(pir1, 2);
    uint64_t imm8 = ir1_opnd_uimm(imm8_reg);
    if (ir1_opnd_is_mem(opnd1) ||
        (XMM_REG(opnd0) != XMM_REG(opnd1))) {
        latxs_append_ir2_opnd2i(LISA_VORI_B, &dest, &src, 0);
    }
    latxs_append_ir2_opnd2i(LISA_VSHUF4I_H, &dest, &dest, imm8);

    return true;
}

bool latxs_translate_pshuflw(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));
    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);

    IR2_OPND temp = latxs_ra_alloc_ftemp();

    uint64_t imm8 = ir1_opnd_uimm(ir1_get_opnd(pir1, 2));
    if (ir1_opnd_is_mem(opnd1) ||
        (XMM_REG(opnd0) != XMM_REG(opnd1))) {
        latxs_append_ir2_opnd2i(LISA_VORI_B, &dest, &src, 0);
    }

    latxs_append_ir2_opnd2i(LISA_VORI_B, &temp, &src, 0);
    latxs_append_ir2_opnd2i(LISA_VBSRL_V, &temp, &temp, 8);
    latxs_append_ir2_opnd2i(LISA_VSHUF4I_H, &dest, &dest, imm8);
    if(option_enable_lasx) {
        latxs_append_ir2_opnd2i(LISA_XVINSVE0_D, &dest, &temp, 1);
    } else {
        latxs_append_ir2_opnd2i(LISA_VEXTRINS_D, &dest, &temp, 0x1 << 4);
    }
    return true;
}

bool latxs_translate_pshufhw(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));
    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);

    IR2_OPND temp = latxs_ra_alloc_ftemp();

    uint64_t imm8 = ir1_opnd_uimm(ir1_get_opnd(pir1, 2));

    latxs_append_ir2_opnd2i(LISA_VORI_B, &temp, &src, 0);
    latxs_append_ir2_opnd2i(LISA_VSHUF4I_H, &dest, &src, imm8);
    if(option_enable_lasx) {
        latxs_append_ir2_opnd2i(LISA_XVINSVE0_D, &dest, &temp, 0);
    } else {
        latxs_append_ir2_opnd2i(LISA_VEXTRINS_D, &dest, &temp, 0);
    }
    return true;
}

bool latxs_translate_pslldq(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));
    IR2_OPND dest = XMM_LOAD128(opnd0);
    uint8_t imm8 = ir1_opnd_uimm(opnd1);
    if (imm8 > 15) {
        latxs_append_ir2_opnd3(LISA_VXOR_V, &dest, &dest, &dest);
    } else if (imm8 == 0) {
        return true;
    } else {
        latxs_append_ir2_opnd2i(LISA_VBSLL_V, &dest, &dest, imm8);
    }
    return true;
}

bool latxs_translate_psrldq(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));
    IR2_OPND dest = XMM_LOAD128(opnd0);
    uint8_t imm8 = ir1_opnd_uimm(opnd1);
    if (imm8 > 15) {
        latxs_append_ir2_opnd3(LISA_VXOR_V, &dest, &dest, &dest);
    } else if (imm8 == 0) {
        return true;
    } else {
        latxs_append_ir2_opnd2i(LISA_VBSRL_V, &dest, &dest, imm8);
    }
    return true;
}

bool latxs_translate_psubq(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    if (ir1_opnd_is_xmm(opnd0)) {
        IR2_OPND dest = XMM_LOAD128(opnd0);
        IR2_OPND src = XMM_LOAD128(opnd1);
        latxs_append_ir2_opnd3(LISA_VSUB_D, &dest, &dest, &src);
    } else { /* mmx */
        IR2_OPND dest = XMM_LOADFP1(opnd0);
        IR2_OPND src = XMM_LOADFP1(opnd1);
        latxs_append_ir2_opnd3(LISA_VSUB_D, &dest, &dest, &src);
    }
    return true;
}

bool latxs_translate_ucomisd(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    /**
     * (bit 6)ZF = 1 if EQ || UOR
     * (bit 2)PF = 1 if UOR (= ZF & CF)
     * (bit 0)CF = 1 if LT || UOR
     */
    lsassert(ir1_opnd_num(pir1) == 2);
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);
    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src = XMM_LOAD128(opnd1);
    /* 0. set flag = 0 */
    IR2_OPND flag_zf = latxs_ra_alloc_itemp();
    IR2_OPND flag_pf = latxs_ra_alloc_itemp();
    IR2_OPND flag = latxs_ra_alloc_itemp();
    latxs_append_ir2_opnd2_(lisa_mov, &flag, &latxs_zero_ir2_opnd);

    /* 1. check ZF, are they equal & unordered? */
    latxs_append_ir2_opnd3i(LISA_FCMP_COND_D, &latxs_fcc0_ir2_opnd, &dest, &src,
                            FCMP_COND_CUEQ);
    latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
    latxs_append_ir2_opnd2(LISA_MOVCF2GR, &flag_zf, &latxs_fcc0_ir2_opnd);

    /* 2. check CF, are they less & unordered? */
    latxs_append_ir2_opnd3i(LISA_FCMP_COND_D, &latxs_fcc2_ir2_opnd, &dest, &src,
                            FCMP_COND_CULT);
    latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
    latxs_append_ir2_opnd2(LISA_MOVCF2GR, &flag, &latxs_fcc2_ir2_opnd);

    /* 3. check PF, are they unordered? (= ZF & CF) */
    latxs_append_ir2_opnd3(LISA_AND, &flag_pf, &flag, &flag_zf);

    latxs_append_ir2_opnd2ii(LISA_BSTRINS_W, &flag, &flag_zf, ZF_BIT_INDEX,
                             ZF_BIT_INDEX);
    latxs_append_ir2_opnd2ii(LISA_BSTRINS_W, &flag, &flag_pf, PF_BIT_INDEX,
                             PF_BIT_INDEX);

    /* 4. mov flag to EFLAGS */
    latxs_append_ir2_opnd1i(LISA_X86MTFLAG, &flag, 0x3f);

    latxs_ra_free_temp(&flag_pf);
    latxs_ra_free_temp(&flag_zf);
    latxs_ra_free_temp(&flag);

    return true;
}

bool latxs_translate_ucomiss(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    /**
     * (bit 6)ZF = 1 if EQ || UOR
     * (bit 2)PF = 1 if UOR (= ZF & CF)
     * (bit 0)CF = 1 if LT || UOR
     */
    lsassert(ir1_opnd_num(pir1) == 2);
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);
    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src = XMM_LOAD128(opnd1);
    /* 0. set flag = 0 */
    IR2_OPND flag_zf = latxs_ra_alloc_itemp();
    IR2_OPND flag_pf = latxs_ra_alloc_itemp();
    IR2_OPND flag = latxs_ra_alloc_itemp();
    latxs_append_ir2_opnd2_(lisa_mov, &flag, &latxs_zero_ir2_opnd);

    /* 1. check ZF, are they equal & unordered? */
    latxs_append_ir2_opnd3i(LISA_FCMP_COND_S, &latxs_fcc0_ir2_opnd, &dest, &src,
                            FCMP_COND_CUEQ);
    latxs_append_ir2_opnd2(LISA_MOVCF2GR, &flag_zf, &latxs_fcc0_ir2_opnd);

    /* 2. check CF, are they less & unordered? */
    latxs_append_ir2_opnd3i(LISA_FCMP_COND_S, &latxs_fcc2_ir2_opnd, &dest, &src,
                            FCMP_COND_CULT);
    latxs_append_ir2_opnd2(LISA_MOVCF2GR, &flag, &latxs_fcc2_ir2_opnd);

    /* 3. check PF, are they unordered? (= ZF & CF) */
    latxs_append_ir2_opnd3(LISA_AND, &flag_pf, &flag, &flag_zf);

    latxs_append_ir2_opnd2ii(LISA_BSTRINS_W, &flag, &flag_zf, ZF_BIT_INDEX,
                             ZF_BIT_INDEX);
    latxs_append_ir2_opnd2ii(LISA_BSTRINS_W, &flag, &flag_pf, PF_BIT_INDEX,
                             PF_BIT_INDEX);

    latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
    /* 4. mov flag to EFLAGS */
    latxs_append_ir2_opnd1i(LISA_X86MTFLAG, &flag, 0x3f);

    latxs_ra_free_temp(&flag_pf);
    latxs_ra_free_temp(&flag_zf);
    latxs_ra_free_temp(&flag);

    return true;
}

bool latxs_translate_movupd(IR1_INST *pir1)
{
    latxs_translate_movaps(pir1);
    return true;
}

bool latxs_translate_movdqa(IR1_INST *pir1)
{
    latxs_translate_movaps(pir1);
    return true;
}

bool latxs_translate_movdqu(IR1_INST *pir1)
{
    latxs_translate_movaps(pir1);
    return true;
}

bool latxs_translate_movups(IR1_INST *pir1)
{
    latxs_translate_movaps(pir1);
    return true;
}

bool latxs_translate_movapd(IR1_INST *pir1)
{
    latxs_translate_movaps(pir1);
    return true;
}

bool latxs_translate_movaps(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *dest = ir1_get_opnd(pir1, 0);
    IR1_OPND *src  = ir1_get_opnd(pir1, 1);

    if (ir1_opnd_is_xmm(dest) && ir1_opnd_is_mem(src)) {
        IR2_OPND xmm_opnd = latxs_ra_alloc_xmm(XMM_REG(dest));
        latxs_load_freg128_from_ir1_mem(&xmm_opnd, src);
    } else if (ir1_opnd_is_mem(dest) && ir1_opnd_is_xmm(src)) {
        IR2_OPND xmm_opnd = latxs_ra_alloc_xmm(XMM_REG(src));
        latxs_store_freg128_to_ir1_mem(&xmm_opnd, dest);
    } else if (ir1_opnd_is_xmm(dest) && ir1_opnd_is_xmm(src)) {
        IR2_OPND xmm_dest = latxs_ra_alloc_xmm(XMM_REG(dest));
        IR2_OPND xmm_src  = latxs_ra_alloc_xmm(XMM_REG(src));
        latxs_append_ir2_opnd2i(LISA_VORI_B, &xmm_dest, &xmm_src, 0);
    } else {
        lsassert(0);
    }

    return true;
}

bool latxs_translate_movbe(IR1_INST *pir1)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);
    bool is_lock = latxs_ir1_has_prefix_lock(pir1) && ir1_opnd_is_mem(opnd0);

    if (is_lock && (lsenv->tr_data->sys.cflags & CF_PARALLEL)) {
        lsassert(0);
    }

    if(!(lsenv->tr_data->sys.cpuid_ext_features & CPUID_EXT_MOVBE)) {
        lsassert(0);    /* illegal */
    }

    int opnd0_size;
    opnd0_size = ir1_opnd_size(opnd0);

    IR2_OPND dest, mem_opnd; int mem_offset;
    IR2_OPND src = latxs_load_ireg_from_ir1(opnd1, EXMode_N);
    IR2_OPCODE load  = LISA_INVALID;
    IR2_OPCODE store = LISA_INVALID;

    if(ir1_opnd_is_gpr(opnd0)) {
        int gpr_num = ir1_opnd_base_reg_num(opnd0);
        if (!ir1_opnd_is_8h(opnd1)) {
            dest = latxs_ra_alloc_gpr(gpr_num);
        } else {
            // dest can only be 16/32/64 according to x86 manual
            lsassert(0);
        }
    } else {
        dest = latxs_ra_alloc_itemp();
        latxs_convert_mem_opnd_opt(&mem_opnd, &mem_offset, opnd0, -1);
        switch (opnd0_size) {
            case 64: load = LISA_LD_D;  store = LISA_ST_D; break;
            case 32: load = LISA_LD_WU; store = LISA_ST_W; break;
            case 16: load = LISA_LD_HU; store = LISA_ST_H; break;
            case  8: load = LISA_LD_BU; store = LISA_ST_B; break;
            default: lsassert(0); break;
        }
        gen_ldst_softmmu_helper(load, &dest, &mem_opnd, mem_offset, 1);
    }

    /* set eflag */
    latxs_generate_eflag_calculation(&dest, &dest, &src, pir1, true);

    /* calculate */
    IR2_OPND temp = latxs_ra_alloc_itemp();
    if (opnd0_size == 16) {
        latxs_append_ir2_opnd2(LISA_REVB_2H, &temp, &src);
    } else if (opnd0_size == 32) {
        latxs_append_ir2_opnd2(LISA_REVB_2W, &temp, &src);
    } else if (opnd0_size == 64) {
        latxs_append_ir2_opnd2(LISA_REVB_D, &temp, &src);
    } else {
        lsassert(0);
    }

    /* write back */
    if (ir1_opnd_is_gpr(opnd0)) {
        if (opnd0_size == 32) {
            latxs_append_ir2_opnd2ii(LISA_BSTRPICK_D, &dest, &temp, opnd0_size - 1, 0);
        } else {
            latxs_append_ir2_opnd2ii(LISA_BSTRINS_D, &dest, &temp, opnd0_size - 1, 0);
        }
    } else {
        gen_ldst_softmmu_helper(store, &temp, &mem_opnd, mem_offset, 1);
    }

    return true;
}

bool latxs_translate_movlhps(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));
    lsassert(ir1_opnd_is_xmm(opnd1));

    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);
    latxs_append_ir2_opnd3(LISA_VILVL_D, &dest, &src, &dest);
    return true;
}

bool latxs_translate_movhlps(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    lsassert(ir1_opnd_is_xmm(opnd0));
    lsassert(ir1_opnd_is_xmm(opnd1));

    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);
    latxs_append_ir2_opnd3(LISA_VILVH_D, &dest, &dest, &src);
    return true;
}

bool latxs_translate_movsd(IR1_INST *pir1)
{
    if (pir1->info->detail->x86.opcode[0] == 0xa5) {
        return latxs_translate_movs(pir1);
    }

    XMM_EXCP(pir1);

    IR1_OPND *dest = ir1_get_opnd(pir1, 0);
    IR1_OPND *src  = ir1_get_opnd(pir1, 1);

    IR2_OPND *zero = &latxs_zero_ir2_opnd;
    if (ir1_opnd_is_xmm(dest) && ir1_opnd_is_mem(src)) {
        /*
         * MOVSD (128-bit Legacy SSE version: MOVSD XMM1, m64)
         * DEST[63:0] <= SRC[63:0]
         * DEST[127:64] <= 0
         * DEST[MAXVL-1:128] (Unmodified)
         */
        if (SHBR_ACTIVE_64(pir1)) {
            IR2_OPND xmm_dest = latxs_ra_alloc_xmm(XMM_REG(dest));
            latxs_load_freg_from_ir1_2(&xmm_dest, src, false);
        } else if (option_risk_simd_ignore_upper64) {
            IR2_OPND xmm_dest = latxs_ra_alloc_xmm(XMM_REG(dest));

            IR2_OPND mem_opnd;
            int mem_offset;
            int addr_size = latxs_ir1_addr_size(pir1);
            latxs_convert_mem_opnd_opt(&mem_opnd, &mem_offset, src, addr_size);
            gen_ldst_c1_softmmu_helper(LISA_FLD_D, &xmm_dest, &mem_opnd, mem_offset, 1);

            latxs_append_ir2_opnd2i(LISA_VINSGR2VR_D, &xmm_dest, zero, 1);

            /*latxs_load_freg_from_ir1_2(&xmm_dest, src, false);*/
            /*latxs_append_ir2_opnd2i(LISA_VINSGR2VR_D, &xmm_dest, zero, 1);*/
        } else {
            IR2_OPND temp = latxs_load_freg_from_ir1_1(src, false);
            IR2_OPND xmm_dest = latxs_ra_alloc_xmm(XMM_REG(dest));
            if(option_enable_lasx) {
                latxs_append_ir2_opnd2i(LISA_XVPICKVE_D, &xmm_dest, &temp, 0);
            } else {
                latxs_append_ir2_opnd2i(LISA_VANDI_B, &xmm_dest, &xmm_dest, 0);
                latxs_append_ir2_opnd2i(LISA_VEXTRINS_D, &xmm_dest, &temp, 0);
            }
        }
    } else if (ir1_opnd_is_mem(dest) && ir1_opnd_is_xmm(src)) {
        /*
         * MOVSD/VMOVSD
         * (128-bit versions: MOVSD m64, xmm1 or VMOVSD m64, xmm1)
         * DEST[63:0] <= SRC[63:0]
         */
        IR2_OPND xmm_src = latxs_ra_alloc_xmm(XMM_REG(src));
        latxs_store_freg_to_ir1(&xmm_src, dest, false);
    } else if (ir1_opnd_is_xmm(dest) && ir1_opnd_is_xmm(src)) {
        /*
         * MOVSD/VMOVSD
         * (128-bit versions: MOVSD m64, xmm1 or VMOVSD m64, xmm1)
         * DEST[63:0] <= SRC[63:0]
         */
        IR2_OPND xmm_dest = latxs_ra_alloc_xmm(XMM_REG(dest));
        IR2_OPND xmm_src  = latxs_ra_alloc_xmm(XMM_REG(src));
        if (SHBR_ACTIVE_64(pir1)) {
            latxs_append_ir2_opnd2(LISA_FMOV_D, &xmm_dest, &xmm_src);
        } else if (option_enable_lasx) {
            latxs_append_ir2_opnd2i(LISA_XVINSVE0_D, &xmm_dest, &xmm_src, 0);
        } else {
            latxs_append_ir2_opnd2i(LISA_VEXTRINS_D, &xmm_dest, &xmm_src, 0);
        }
    }

    return true;
}

bool latxs_translate_movss(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *dest = ir1_get_opnd(pir1, 0);
    IR1_OPND *src  = ir1_get_opnd(pir1, 1);

    if (ir1_opnd_is_xmm(dest) && ir1_opnd_is_mem(src)) {
        /*
         * MOVSS
         * (Legacy SSE version when the source operand is memory
         * and the destination is an XMM register)
         * DEST[31:0] <= SRC[31:0]
         * DEST[127:32] <= 0
         * DEST[MAXVL-1:128] (Unmodified)
         */
        IR2_OPND xmm_dest = latxs_ra_alloc_xmm(XMM_REG(dest));
        if (SHBR_ACTIVE_32(pir1)) {
            latxs_load_freg_from_ir1_2(&xmm_dest, src, false);
        } else {
            IR2_OPND temp = latxs_load_freg_from_ir1_1(src, false);
            if (option_enable_lasx) {
                latxs_append_ir2_opnd2i(LISA_XVPICKVE_W, &xmm_dest, &temp, 0);
            } else {
                latxs_append_ir2_opnd2i(LISA_VANDI_B, &xmm_dest, &xmm_dest, 0);
                latxs_append_ir2_opnd2i(LISA_VEXTRINS_W, &xmm_dest, &temp, 0);
            }
        }
        return true;
    } else if (ir1_opnd_is_mem(dest) && ir1_opnd_is_xmm(src)) {
        /*
         * MOVSS/VMOVSS
         * (when the source operand is an XMM register
         * and the destination is memory)
         * DEST[31:0] <= SRC[31:0]
         */
        IR2_OPND xmm_src = latxs_ra_alloc_xmm(XMM_REG(src));
        latxs_store_freg_to_ir1(&xmm_src, dest, false);
        return true;
    } else if (ir1_opnd_is_xmm(dest) && ir1_opnd_is_xmm(src)) {
        /*
         * MOVSS
         * (Legacy SSE version when the source and
         * destination operands are both XMM registers)
         * DEST[31:0] <= SRC[31:0]
         * DEST[MAXVL-1:32] (Unmodified)
         */
        IR2_OPND xmm_dest = latxs_ra_alloc_xmm(XMM_REG(dest));
        IR2_OPND xmm_src  = latxs_ra_alloc_xmm(XMM_REG(src));
        if (SHBR_ACTIVE_32(pir1)) {
            latxs_append_ir2_opnd2(LISA_FMOV_S, &xmm_dest, &xmm_src);
        } else if (option_enable_lasx) {
            latxs_append_ir2_opnd2i(LISA_XVINSVE0_W, &xmm_dest, &xmm_src, 0);
        } else {
            latxs_append_ir2_opnd2i(LISA_VEXTRINS_W, &xmm_dest, &xmm_src, 0);
        }
        return true;
    }

    return true;
}

bool latxs_translate_movhpd(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *dest = ir1_get_opnd(pir1, 0);
    IR1_OPND *src  = ir1_get_opnd(pir1, 1);

    if (ir1_opnd_is_mem(src) && ir1_opnd_is_xmm(dest)) {
        IR2_OPND temp = latxs_ra_alloc_itemp();
        latxs_load_ir1_to_ir2(&temp, src, EXMode_Z);
        IR2_OPND xmm_dest = latxs_ra_alloc_xmm(XMM_REG(dest));
        latxs_append_ir2_opnd2i(LISA_VINSGR2VR_D, &xmm_dest, &temp, 1);
        return true;
    } else if (ir1_opnd_is_mem(dest) && ir1_opnd_is_xmm(src)) {
        IR2_OPND xmm_src = latxs_ra_alloc_xmm(XMM_REG(src));
        latxs_store_freg128_to_ir1_mem_idx(&xmm_src, dest, 1);

        return true;
    }

    lsassertm(0, "movhpd not implemented situation.\n");

    return true;
}

bool latxs_translate_movhps(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    latxs_translate_movhpd(pir1);
    return true;
}

bool latxs_translate_movlpd(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    IR1_OPND *dest = ir1_get_opnd(pir1, 0);
    IR1_OPND *src  = ir1_get_opnd(pir1, 1);

    if (ir1_opnd_is_mem(src) && ir1_opnd_is_xmm(dest)) {
        IR2_OPND temp = latxs_ra_alloc_itemp();
        latxs_load_ir1_to_ir2(&temp, src, EXMode_Z);
        IR2_OPND xmm_dest = latxs_ra_alloc_xmm(XMM_REG(dest));
        latxs_append_ir2_opnd2i(LISA_VINSGR2VR_D, &xmm_dest, &temp, 0);
        return true;
    } else if (ir1_opnd_is_mem(dest) && ir1_opnd_is_xmm(src)) {
        IR2_OPND xmm_src = latxs_ra_alloc_xmm(XMM_REG(src));
        latxs_store_freg128_to_ir1_mem_idx(&xmm_src, dest, 0);
        return true;
    }

    lsassertm(0, "movlpd not implemented situation.\n");

    return true;
}

bool latxs_translate_movlps(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    latxs_translate_movlpd(pir1);
    return true;
}

bool latxs_translate_movq2dq(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    IR2_OPND dest = latxs_load_freg_from_ir1_1(opnd0, false);
    IR2_OPND src = latxs_load_freg_from_ir1_1(opnd1, false);

    if(option_enable_lasx) {
        latxs_append_ir2_opnd2i(LISA_XVPICKVE_D, &dest, &src, 0);
    } else {
        latxs_append_ir2_opnd2i(LISA_VANDI_B, &dest, &dest, 0);
        latxs_append_ir2_opnd2i(LISA_VEXTRINS_D, &dest, &src, 0);
    }

    return true;
}

bool latxs_translate_movdq2q(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_enter_mmx(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    IR2_OPND dest = latxs_load_freg_from_ir1_1(opnd0, false);
    IR2_OPND src = latxs_load_freg_from_ir1_1(opnd1, false);

    if(option_enable_lasx) {
        latxs_append_ir2_opnd2i(LISA_XVPICKVE_D, &dest, &src, 0);
    } else {
        latxs_append_ir2_opnd2i(LISA_VANDI_B, &dest, &dest, 0);
        latxs_append_ir2_opnd2i(LISA_VEXTRINS_D, &dest, &src, 0);
    }

    return true;
}

bool latxs_translate_addps(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    lsassert(ir1_opnd_is_xmm(ir1_get_opnd(pir1, 0)));
    IR2_OPND dest = XMM_LOAD128(ir1_get_opnd(pir1, 0));
    IR2_OPND src  = XMM_LOAD128(ir1_get_opnd(pir1, 1));

    IR2_OPND dest_t;
    if (SIMD_FPE_CHECK) {
        dest_t = latxs_ra_alloc_ftemp();
        if(option_enable_lasx) {
            latxs_append_ir2_opnd3(LISA_XVOR_V, &dest_t, &dest, &dest);
        } else {
            latxs_append_ir2_opnd3(LISA_VOR_V, &dest_t, &dest, &dest);
        }
    }

    latxs_append_ir2_opnd3(LISA_VFADD_S, &dest, &dest, &src);
    latxs_tr_gen_simd_fpe_check_t(pir1, &dest_t);

    return true;
}

bool latxs_translate_addpd(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    lsassert(ir1_opnd_is_xmm(ir1_get_opnd(pir1, 0)));
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);
    IR2_OPND dest = XMM_LOAD128(opnd0);
    IR2_OPND src  = XMM_LOAD128(opnd1);

    IR2_OPND dest_t;
    if (SIMD_FPE_CHECK) {
        dest_t = latxs_ra_alloc_ftemp();
        if(option_enable_lasx) {
            latxs_append_ir2_opnd3(LISA_XVOR_V, &dest_t, &dest, &dest);
        } else {
            latxs_append_ir2_opnd3(LISA_VOR_V, &dest_t, &dest, &dest);
        }
    }

    latxs_append_ir2_opnd3(LISA_VFADD_D, &dest, &dest, &src);
    latxs_tr_gen_simd_fpe_check_t(pir1, &dest_t);

    return true;
}

bool latxs_translate_andpd(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    lsassert(ir1_opnd_is_xmm(ir1_get_opnd(pir1, 0)));
    IR2_OPND dest = XMM_LOAD128(ir1_get_opnd(pir1, 0));
    IR2_OPND src  = XMM_LOAD128(ir1_get_opnd(pir1, 1));
    latxs_append_ir2_opnd3(LISA_VAND_V, &dest, &dest, &src);
    return true;
}

bool latxs_translate_andnpd(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    lsassert(ir1_opnd_is_xmm(ir1_get_opnd(pir1, 0)));
    IR2_OPND dest = XMM_LOAD128(ir1_get_opnd(pir1, 0));
    IR2_OPND src  = XMM_LOAD128(ir1_get_opnd(pir1, 1));
    latxs_append_ir2_opnd3(LISA_VANDN_V, &dest, &dest, &src);
    return true;
}

bool latxs_translate_andnps(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    lsassert(ir1_opnd_is_xmm(ir1_get_opnd(pir1, 0)));
    IR2_OPND dest = XMM_LOAD128(ir1_get_opnd(pir1, 0));
    IR2_OPND src  = XMM_LOAD128(ir1_get_opnd(pir1, 1));
    latxs_append_ir2_opnd3(LISA_VANDN_V, &dest, &dest, &src);
    return true;
}

bool latxs_translate_unpcklpd(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    lsassert(ir1_opnd_is_xmm(ir1_get_opnd(pir1, 0)));
    IR2_OPND dest = XMM_LOAD128(ir1_get_opnd(pir1, 0));
    IR2_OPND src  = XMM_LOAD128(ir1_get_opnd(pir1, 1));
    latxs_append_ir2_opnd2i(LISA_VSHUF4I_D, &dest, &src, 0x8);
    return true;
}

bool latxs_translate_unpckhpd(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    lsassert(ir1_opnd_is_xmm(ir1_get_opnd(pir1, 0)));
    IR2_OPND dest = XMM_LOAD128(ir1_get_opnd(pir1, 0));
    IR2_OPND src  = XMM_LOAD128(ir1_get_opnd(pir1, 1));
    latxs_append_ir2_opnd2i(LISA_VSHUF4I_D, &dest, &src, 0xd);
    return true;
}

bool latxs_translate_unpcklps(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    lsassert(ir1_opnd_is_xmm(ir1_get_opnd(pir1, 0)));
    IR2_OPND dest = XMM_LOAD128(ir1_get_opnd(pir1, 0));
    IR2_OPND src  = XMM_LOAD128(ir1_get_opnd(pir1, 1));
    latxs_append_ir2_opnd3(LISA_VILVL_W, &dest, &src, &dest);
    return true;
}

bool latxs_translate_unpckhps(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    lsassert(ir1_opnd_is_xmm(ir1_get_opnd(pir1, 0)));
    IR2_OPND dest = XMM_LOAD128(ir1_get_opnd(pir1, 0));
    IR2_OPND src  = XMM_LOAD128(ir1_get_opnd(pir1, 1));
    latxs_append_ir2_opnd3(LISA_VILVH_W, &dest, &src, &dest);
    return true;
}

bool latxs_translate_punpcklqdq(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    lsassert(ir1_opnd_is_xmm(ir1_get_opnd(pir1, 0)));
    IR2_OPND dest = XMM_LOAD128(ir1_get_opnd(pir1, 0));
    IR2_OPND src  = XMM_LOAD128(ir1_get_opnd(pir1, 1));
    latxs_append_ir2_opnd3(LISA_VILVL_D, &dest, &src, &dest);
    return true;
}

bool latxs_translate_punpckhqdq(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    lsassert(ir1_opnd_is_xmm(ir1_get_opnd(pir1, 0)));
    IR2_OPND dest = XMM_LOAD128(ir1_get_opnd(pir1, 0));
    IR2_OPND src  = XMM_LOAD128(ir1_get_opnd(pir1, 1));
    latxs_append_ir2_opnd3(LISA_VILVH_D, &dest, &src, &dest);
    return true;
}

bool latxs_translate_shufps(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    lsassert(ir1_opnd_is_xmm(ir1_get_opnd(pir1, 0)));
    IR2_OPND dest = XMM_LOAD128(ir1_get_opnd(pir1, 0));
    IR2_OPND src  = XMM_LOAD128(ir1_get_opnd(pir1, 1));
    uint64_t imm8 = ir1_opnd_uimm(ir1_get_opnd(pir1, 2));
    if (imm8 == 0 && ir1_opnd_is_xmm(ir1_get_opnd(pir1, 1)) &&
        (ir1_opnd_base_reg_num(ir1_get_opnd(pir1, 0)) ==
        ir1_opnd_base_reg_num(ir1_get_opnd(pir1, 1))) &&
        option_enable_lasx) {
        latxs_append_ir2_opnd2(LISA_XVREPLVE0_W, &dest, &src);
        return true;
    }
    IR2_OPND temp1 = latxs_ra_alloc_ftemp();
    IR2_OPND temp2 = latxs_ra_alloc_ftemp();
    latxs_append_ir2_opnd2i(LISA_VSHUF4I_W, &temp1, &dest, imm8);
    latxs_append_ir2_opnd2i(LISA_VSHUF4I_W, &temp2, &src, imm8 >> 4);
    latxs_append_ir2_opnd3(LISA_VPICKEV_D, &dest, &temp2, &temp1);
    return true;
}

bool latxs_translate_shufpd(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    lsassert(ir1_opnd_is_xmm(ir1_get_opnd(pir1, 0)));
    IR2_OPND dest = XMM_LOAD128(ir1_get_opnd(pir1, 0));
    IR2_OPND src  = XMM_LOAD128(ir1_get_opnd(pir1, 1));
    uint8_t imm8 = ir1_opnd_uimm(ir1_get_opnd(pir1, 2));
    imm8 &= 3;
    uint8_t shfd_imm8 = 8 | (imm8 & 1) | ((imm8 & 2) << 1);
    latxs_append_ir2_opnd2i(LISA_VSHUF4I_D, &dest, &src, shfd_imm8);
    return true;
}

bool latxs_translate_xorps(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    lsassert(ir1_opnd_is_xmm(ir1_get_opnd(pir1, 0)));
    IR2_OPND dest = XMM_LOAD128(ir1_get_opnd(pir1, 0));
    IR2_OPND src  = XMM_LOAD128(ir1_get_opnd(pir1, 1));
    latxs_append_ir2_opnd3(LISA_VXOR_V, &dest, &dest, &src);
    return true;

}

bool latxs_translate_xorpd(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    latxs_translate_xorps(pir1);
    return true;
}

bool latxs_translate_subss(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    lsassert(ir1_opnd_is_xmm(ir1_get_opnd(pir1, 0)));
    IR2_OPND dest = XMM_LOAD128(ir1_get_opnd(pir1, 0));
    IR2_OPND src  = XMM_LOAD128(ir1_get_opnd(pir1, 1));
    bool dead_high = SHBR_ACTIVE_32(pir1);

    if (option_risk_simd_ignore_upper64 ||
        (dead_high && !SIMD_FPE_CHECK)) {
        latxs_append_ir2_opnd3(LISA_FSUB_S, &dest, &dest, &src);
    } else {
        IR2_OPND temp = latxs_ra_alloc_ftemp();
        latxs_append_ir2_opnd3(LISA_FSUB_S, &temp, &dest, &src);
        latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
        if (dead_high) {
            latxs_append_ir2_opnd2(LISA_FMOV_S, &dest, &temp);
        } else if (option_enable_lasx) {
            latxs_append_ir2_opnd2i(LISA_XVINSVE0_W, &dest, &temp, 0);
        } else {
            latxs_append_ir2_opnd2i(LISA_VEXTRINS_W, &dest, &temp, 0);
        }
    }

    return true;
}

bool latxs_translate_subsd(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    lsassert(ir1_opnd_is_xmm(ir1_get_opnd(pir1, 0)));
    IR2_OPND dest = XMM_LOAD128(ir1_get_opnd(pir1, 0));
    IR2_OPND src  = XMM_LOAD128(ir1_get_opnd(pir1, 1));
    bool dead_high = SHBR_ACTIVE_64(pir1);

    if (option_risk_simd_ignore_upper64 ||
        (dead_high && !SIMD_FPE_CHECK)) {
        latxs_append_ir2_opnd3(LISA_FSUB_D, &dest, &dest, &src);
    } else {
        IR2_OPND temp = latxs_ra_alloc_ftemp();
        latxs_append_ir2_opnd3(LISA_FSUB_D, &temp, &dest, &src);
        latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
        if (dead_high) {
            latxs_append_ir2_opnd2(LISA_FMOV_D, &dest, &temp);
        } else if (option_enable_lasx) {
            latxs_append_ir2_opnd2i(LISA_XVINSVE0_D, &dest, &temp, 0);
        } else {
            latxs_append_ir2_opnd2i(LISA_VEXTRINS_D, &dest, &temp, 0);
        }
    }

    return true;
}

bool latxs_translate_subps(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    lsassert(ir1_opnd_is_xmm(ir1_get_opnd(pir1, 0)));
    IR2_OPND dest = XMM_LOAD128(ir1_get_opnd(pir1, 0));
    IR2_OPND src  = XMM_LOAD128(ir1_get_opnd(pir1, 1));

    IR2_OPND dest_t;
    if (SIMD_FPE_CHECK) {
        dest_t = latxs_ra_alloc_ftemp();
        if(option_enable_lasx) {
            latxs_append_ir2_opnd3(LISA_XVOR_V, &dest_t, &dest, &dest);
        } else {
            latxs_append_ir2_opnd3(LISA_VOR_V, &dest_t, &dest, &dest);
        }
    }

    latxs_append_ir2_opnd3(LISA_VFSUB_S, &dest, &dest, &src);
    latxs_tr_gen_simd_fpe_check_t(pir1, &dest_t);

    return true;
}

bool latxs_translate_subpd(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    lsassert(ir1_opnd_is_xmm(ir1_get_opnd(pir1, 0)));
    IR2_OPND dest = XMM_LOAD128(ir1_get_opnd(pir1, 0));
    IR2_OPND src  = XMM_LOAD128(ir1_get_opnd(pir1, 1));

    IR2_OPND dest_t;
    if (SIMD_FPE_CHECK) {
        dest_t = latxs_ra_alloc_ftemp();
        if(option_enable_lasx) {
            latxs_append_ir2_opnd3(LISA_XVOR_V, &dest_t, &dest, &dest);
        } else {
            latxs_append_ir2_opnd3(LISA_VOR_V, &dest_t, &dest, &dest);
        }
    }

    latxs_append_ir2_opnd3(LISA_VFSUB_D, &dest, &dest, &src);
    latxs_tr_gen_simd_fpe_check_t(pir1, &dest_t);

    return true;
}

bool latxs_translate_rcpps(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    lsassert(ir1_opnd_is_xmm(ir1_get_opnd(pir1, 0)));
    IR2_OPND dest = XMM_LOAD128(ir1_get_opnd(pir1, 0));
    IR2_OPND src  = XMM_LOAD128(ir1_get_opnd(pir1, 1));
    latxs_append_ir2_opnd2(LISA_VFRECIP_S, &dest, &src);
    return true;
}

bool latxs_translate_rcpss(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    lsassert(ir1_opnd_is_xmm(ir1_get_opnd(pir1, 0)));
    IR2_OPND dest = XMM_LOAD128(ir1_get_opnd(pir1, 0));
    IR2_OPND src  = XMM_LOAD128(ir1_get_opnd(pir1, 1));
    if (SHBR_ACTIVE_32(pir1)) {
        latxs_append_ir2_opnd2(LISA_VFRECIP_S, &dest, &src);
    } else {
        IR2_OPND temp = latxs_ra_alloc_ftemp();
        latxs_append_ir2_opnd2(LISA_VFRECIP_S, &temp, &src);
        if (option_enable_lasx) {
            latxs_append_ir2_opnd2i(LISA_XVINSVE0_W, &dest, &temp, 0);
        } else {
            latxs_append_ir2_opnd2i(LISA_VEXTRINS_W, &dest, &temp, 0);
        }
    }
    return true;
}

bool latxs_translate_sqrtpd(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    lsassert(ir1_opnd_is_xmm(ir1_get_opnd(pir1, 0)));
    IR2_OPND dest = XMM_LOAD128(ir1_get_opnd(pir1, 0));
    IR2_OPND src  = XMM_LOAD128(ir1_get_opnd(pir1, 1));

    IR2_OPND dest_t;
    if (SIMD_FPE_CHECK) {
        dest_t = latxs_ra_alloc_ftemp();
        if(option_enable_lasx) {
            latxs_append_ir2_opnd3(LISA_XVOR_V, &dest_t, &dest, &dest);
        } else {
            latxs_append_ir2_opnd3(LISA_VOR_V, &dest_t, &dest, &dest);
        }
    }

    latxs_append_ir2_opnd2(LISA_VFSQRT_D, &dest, &src);

    latxs_tr_gen_simd_fpe_check_t(pir1, &dest_t);

    return true;
}

bool latxs_translate_sqrtps(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    lsassert(ir1_opnd_is_xmm(ir1_get_opnd(pir1, 0)));
    IR2_OPND dest = XMM_LOAD128(ir1_get_opnd(pir1, 0));
    IR2_OPND src  = XMM_LOAD128(ir1_get_opnd(pir1, 1));

    IR2_OPND dest_t;
    if (SIMD_FPE_CHECK) {
        dest_t = latxs_ra_alloc_ftemp();
        if(option_enable_lasx) {
            latxs_append_ir2_opnd3(LISA_XVOR_V, &dest_t, &dest, &dest);
        } else {
            latxs_append_ir2_opnd3(LISA_VOR_V, &dest_t, &dest, &dest);
        }
    }

    latxs_append_ir2_opnd2(LISA_VFSQRT_S, &dest, &src);

    latxs_tr_gen_simd_fpe_check_t(pir1, &dest_t);

    return true;
}

bool latxs_translate_sqrtsd(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    lsassert(ir1_opnd_is_xmm(ir1_get_opnd(pir1, 0)));
    IR2_OPND dest = XMM_LOAD128(ir1_get_opnd(pir1, 0));
    IR2_OPND src  = XMM_LOAD128(ir1_get_opnd(pir1, 1));
    bool dead_high = SHBR_ACTIVE_64(pir1);

    if (option_risk_simd_ignore_upper64 ||
        (dead_high && !SIMD_FPE_CHECK)) {
        latxs_append_ir2_opnd2(LISA_FSQRT_D, &dest, &src);
    } else {
        IR2_OPND temp = latxs_ra_alloc_ftemp();
        latxs_append_ir2_opnd2(LISA_FSQRT_D, &temp, &src);
        latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
        if (dead_high) {
            latxs_append_ir2_opnd2(LISA_FMOV_D, &dest, &temp);
        } else if (option_enable_lasx) {
            latxs_append_ir2_opnd2i(LISA_XVINSVE0_D, &dest, &temp, 0);
        } else {
            latxs_append_ir2_opnd2i(LISA_VEXTRINS_D, &dest, &temp, 0);
        }
    }

    return true;
}

bool latxs_translate_sqrtss(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    lsassert(ir1_opnd_is_xmm(ir1_get_opnd(pir1, 0)));
    IR2_OPND dest = XMM_LOAD128(ir1_get_opnd(pir1, 0));
    IR2_OPND src  = XMM_LOAD128(ir1_get_opnd(pir1, 1));
    bool dead_high = SHBR_ACTIVE_32(pir1);

    if (option_risk_simd_ignore_upper64 ||
        (dead_high && !SIMD_FPE_CHECK)) {
        latxs_append_ir2_opnd2(LISA_FSQRT_S, &dest, &src);
    } else {
        IR2_OPND temp = latxs_ra_alloc_ftemp();
        latxs_append_ir2_opnd2(LISA_FSQRT_S, &temp, &src);
        latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
        if (dead_high) {
            latxs_append_ir2_opnd2(LISA_FMOV_S, &dest, &temp);
        } else if (option_enable_lasx) {
            latxs_append_ir2_opnd2i(LISA_XVINSVE0_W, &dest, &temp, 0);
        } else {
            latxs_append_ir2_opnd2i(LISA_VEXTRINS_W, &dest, &temp, 0);
        }
    }

    return true;
}

bool latxs_translate_rsqrtps(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    lsassert(ir1_opnd_is_xmm(ir1_get_opnd(pir1, 0)));
    IR2_OPND dest = XMM_LOAD128(ir1_get_opnd(pir1, 0));
    IR2_OPND src  = XMM_LOAD128(ir1_get_opnd(pir1, 1));
    latxs_append_ir2_opnd2(LISA_VFRSQRT_S, &dest, &src);
    return true;
}

bool latxs_translate_rsqrtss(IR1_INST *pir1)
{
    XMM_EXCP(pir1);
    lsassert(ir1_opnd_is_xmm(ir1_get_opnd(pir1, 0)));
    IR2_OPND dest = XMM_LOAD128(ir1_get_opnd(pir1, 0));
    IR2_OPND src  = XMM_LOAD128(ir1_get_opnd(pir1, 1));
    if (SHBR_ACTIVE_32(pir1)) {
        latxs_append_ir2_opnd2(LISA_FRSQRT_S, &dest, &src);
    } else {
        IR2_OPND temp = latxs_ra_alloc_ftemp();
        latxs_append_ir2_opnd2(LISA_FRSQRT_S, &temp, &src);
        if (option_enable_lasx) {
            latxs_append_ir2_opnd2i(LISA_XVINSVE0_W, &dest, &temp, 0);
        } else {
            latxs_append_ir2_opnd2i(LISA_VEXTRINS_W, &dest, &temp, 0);
        }
    }
    return true;
}

bool latxs_translate_cvtps2pd(IR1_INST *pir1)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    XMM_EXCP(pir1);

    lsassert(ir1_opnd_is_xmm(ir1_get_opnd(pir1, 0)));
    /* TODO:simply */

    IR2_OPND dest = XMM_LOAD128(ir1_get_opnd(pir1, 0));
    IR2_OPND src = XMM_LOAD128(ir1_get_opnd(pir1, 1));

    if(option_no_reserve_256 && option_sigfpe && td->sys.mxcsr_on) {
        IR2_OPND temp = latxs_ra_alloc_ftemp();
        latxs_append_ir2_opnd2(LISA_VFCVTL_D_S, &temp, &src);
        latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
        latxs_append_ir2_opnd3(LISA_VOR_V, &dest, &temp, &temp);
    } else if (option_no_reserve_256) {
        latxs_append_ir2_opnd2(LISA_VFCVTL_D_S, &dest, &src);
        latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
    } else {
        IR2_OPND temp = latxs_ra_alloc_ftemp();
        latxs_append_ir2_opnd2(LISA_VFCVTL_D_S, &temp, &src);
        latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
        if (option_enable_lasx) {
            latxs_append_ir2_opnd2i(LISA_XVINSVE0_D, &dest, &temp, 0);
            latxs_append_ir2_opnd2i(LISA_XVINSVE0_D, &dest, &temp, 1);
        } else {
            latxs_append_ir2_opnd2i(LISA_VEXTRINS_D, &dest, &temp, 0);
            latxs_append_ir2_opnd2i(LISA_VEXTRINS_D, &dest, &temp, 1 << 4);
        }
    }

    return true;
}

static bool __latxs_translate_cvttsx2si(IR1_INST *pir1)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    IR2_OPND src_lo = XMM_LOAD128(opnd1);
    int os = ir1_opnd_size(opnd0);

    IR2_OPCODE la_cvt = LISA_INVALID;
    switch (ir1_opcode(pir1)) {
    case X86_INS_CVTTSD2SI:
        la_cvt = (os == 64) ? LISA_FTINTRZ_L_D : LISA_FTINTRZ_W_D;
        break;
    case X86_INS_CVTTSS2SI:
        la_cvt = (os == 64) ? LISA_FTINTRZ_L_S : LISA_FTINTRZ_W_S;
        break;
    default:
        ir1_dump(pir1);
        lsassertm(0, "ir1 is not supported.\n");
        break;
    }

    IR2_OPND dest = latxs_ra_alloc_ftemp();
    latxs_append_ir2_opnd2(la_cvt, &dest, &src_lo);

    IR2_OPND overflow = latxs_ra_alloc_itemp();
    IR2_OPND fcsr = latxs_ra_alloc_itemp();
    IR2_OPND tmp = latxs_ra_alloc_itemp();

    if(!option_fast_masked_v) {
        latxs_append_ir2_opnd2(LISA_MOVFCSR2GR, &fcsr, &latxs_fcsr_ir2_opnd);
        latxs_append_ir2_opnd2ii(LISA_BSTRPICK_W, &fcsr, &fcsr,
                FCSR_OFF_CAUSE_V, FCSR_OFF_CAUSE_V);
        latxs_append_ir2_opnd2i(LISA_SLLI_D, &overflow, &fcsr, os - 1);
        latxs_append_ir2_opnd2(LISA_MOVFR2GR_D, &tmp, &dest);
        latxs_append_ir2_opnd3(LISA_MASKNEZ, &fcsr
                , &tmp, &fcsr);
        latxs_append_ir2_opnd3(LISA_OR, &tmp, &overflow, &fcsr);
    } else {
        latxs_append_ir2_opnd2(LISA_MOVFR2GR_D, &tmp, &dest);
    }

    latxs_ra_free_temp(&overflow);
    latxs_ra_free_temp(&fcsr);
    latxs_ra_free_temp(&dest);

    latxs_tr_gen_simd_fpe_check_t(pir1, NULL);
    latxs_store_ir2_to_ir1(&tmp, opnd0);

    return true;
}

bool latxs_translate_cvttsd2si(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    return __latxs_translate_cvttsx2si(pir1);
}

bool latxs_translate_cvttss2si(IR1_INST *pir1)
{
    XMM_EXCP(pir1);

    return __latxs_translate_cvttsx2si(pir1);
}

bool latxs_translate_lddqu(IR1_INST *pir1)
{
    return latxs_translate_movaps(pir1);
}

#define AES_ISH(X)  (((X) * 13) & 15)
#define alsl_imm 1
static inline void cal_aesdec(IR2_OPND r, IR2_OPND st, IR2_OPND rk)
{
    ADDR Td0 =(ADDR)AES_Td0;
    ADDR Td1 =(ADDR)AES_Td1;
    ADDR Td2 =(ADDR)AES_Td2;
    ADDR Td3 =(ADDR)AES_Td3;

    IR2_OPND w = latxs_ra_alloc_itemp();
    IR2_OPND temp0 = latxs_ra_alloc_itemp();
    IR2_OPND AES_Td_0 = latxs_ra_alloc_itemp();
    IR2_OPND AES_Td_1 = latxs_ra_alloc_itemp();
    IR2_OPND AES_Td_2 = latxs_ra_alloc_itemp();
    IR2_OPND AES_Td_3 = latxs_ra_alloc_itemp();
    IR2_OPND w_all = latxs_ra_alloc_ftemp();

    latxs_load_addr_to_ir2(&AES_Td_0, Td0, LOAD_AES_Td0);
    latxs_load_addr_to_ir2(&AES_Td_1, Td1, LOAD_AES_Td1);
    latxs_load_addr_to_ir2(&AES_Td_2, Td2, LOAD_AES_Td2);
    latxs_load_addr_to_ir2(&AES_Td_3, Td3, LOAD_AES_Td3);

    if (hamt_enabled()) latxs_tr_dmw_in_fix_mem(&AES_Td_0);
    if (hamt_enabled()) latxs_tr_dmw_in_fix_mem(&AES_Td_1);
    if (hamt_enabled()) latxs_tr_dmw_in_fix_mem(&AES_Td_2);
    if (hamt_enabled()) latxs_tr_dmw_in_fix_mem(&AES_Td_3);

    /* w0 */
    latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_BU, &la_a0, &st, AES_ISH(0x0));
    latxs_append_ir2_opnd3i(LISA_ALSL_D, &temp0, &la_a0, &AES_Td_0, alsl_imm);
    latxs_append_ir2_opnd2i(LISA_LD_W, &w, &temp0, 0);

    latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_BU, &la_a0, &st, AES_ISH(0x1));
    latxs_append_ir2_opnd3i(LISA_ALSL_D, &temp0, &la_a0, &AES_Td_1, alsl_imm);
    latxs_append_ir2_opnd2i(LISA_LD_W, &temp0, &temp0, 0);
    latxs_append_ir2_opnd3(LISA_XOR, &w, &w, &temp0);

    latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_BU, &la_a0, &st, AES_ISH(0x2));
    latxs_append_ir2_opnd3i(LISA_ALSL_D, &temp0, &la_a0, &AES_Td_2, alsl_imm);
    latxs_append_ir2_opnd2i(LISA_LD_W, &temp0, &temp0, 0);
    latxs_append_ir2_opnd3(LISA_XOR, &w, &w, &temp0);

    latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_BU, &la_a0, &st, AES_ISH(0x3));
    latxs_append_ir2_opnd3i(LISA_ALSL_D, &temp0, &la_a0, &AES_Td_3, alsl_imm);
    latxs_append_ir2_opnd2i(LISA_LD_W, &temp0, &temp0, 0);
    latxs_append_ir2_opnd3(LISA_XOR, &w, &w, &temp0);

    latxs_append_ir2_opnd2(LISA_REVB_2H, &w, &w);
    latxs_append_ir2_opnd2i(LISA_ROTRI_W, &w, &w, 0x10);
    latxs_append_ir2_opnd2i(LISA_VINSGR2VR_W, &w_all, &w, 0);

    /* w1 */
    latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_BU, &la_a0, &st, AES_ISH(0x4));
    latxs_append_ir2_opnd3i(LISA_ALSL_D, &temp0, &la_a0, &AES_Td_0, alsl_imm);
    latxs_append_ir2_opnd2i(LISA_LD_W, &w, &temp0, 0);

    latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_BU, &la_a0, &st, AES_ISH(0x5));
    latxs_append_ir2_opnd3i(LISA_ALSL_D, &temp0, &la_a0, &AES_Td_1, alsl_imm);
    latxs_append_ir2_opnd2i(LISA_LD_W, &temp0, &temp0, 0);
    latxs_append_ir2_opnd3(LISA_XOR, &w, &w, &temp0);

    latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_BU, &la_a0, &st, AES_ISH(0x6));
    latxs_append_ir2_opnd3i(LISA_ALSL_D, &temp0, &la_a0, &AES_Td_2, alsl_imm);
    latxs_append_ir2_opnd2i(LISA_LD_W, &temp0, &temp0, 0);
    latxs_append_ir2_opnd3(LISA_XOR, &w, &w, &temp0);

    latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_BU, &la_a0, &st, AES_ISH(0x7));
    latxs_append_ir2_opnd3i(LISA_ALSL_D, &temp0, &la_a0, &AES_Td_3, alsl_imm);
    latxs_append_ir2_opnd2i(LISA_LD_W, &temp0, &temp0, 0);
    latxs_append_ir2_opnd3(LISA_XOR, &w, &w, &temp0);

    latxs_append_ir2_opnd2(LISA_REVB_2H, &w, &w);
    latxs_append_ir2_opnd2i(LISA_ROTRI_W, &w, &w, 0x10);
    latxs_append_ir2_opnd2i(LISA_VINSGR2VR_W, &w_all, &w, 1);

    /* w2 */
    latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_BU, &la_a0, &st, AES_ISH(0x8));
    latxs_append_ir2_opnd3i(LISA_ALSL_D, &temp0, &la_a0, &AES_Td_0, alsl_imm);
    latxs_append_ir2_opnd2i(LISA_LD_W, &w, &temp0, 0);

    latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_BU, &la_a0, &st, AES_ISH(0x9));
    latxs_append_ir2_opnd3i(LISA_ALSL_D, &temp0, &la_a0, &AES_Td_1, alsl_imm);
    latxs_append_ir2_opnd2i(LISA_LD_W, &temp0, &temp0, 0);
    latxs_append_ir2_opnd3(LISA_XOR, &w, &w, &temp0);

    latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_BU, &la_a0, &st, AES_ISH(0xa));
    latxs_append_ir2_opnd3i(LISA_ALSL_D, &temp0, &la_a0, &AES_Td_2, alsl_imm);
    latxs_append_ir2_opnd2i(LISA_LD_W, &temp0, &temp0, 0);
    latxs_append_ir2_opnd3(LISA_XOR, &w, &w, &temp0);

    latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_BU, &la_a0, &st, AES_ISH(0xb));
    latxs_append_ir2_opnd3i(LISA_ALSL_D, &temp0, &la_a0, &AES_Td_3, alsl_imm);
    latxs_append_ir2_opnd2i(LISA_LD_W, &temp0, &temp0, 0);
    latxs_append_ir2_opnd3(LISA_XOR, &w, &w, &temp0);

    latxs_append_ir2_opnd2(LISA_REVB_2H, &w, &w);
    latxs_append_ir2_opnd2i(LISA_ROTRI_W, &w, &w, 0x10);
    latxs_append_ir2_opnd2i(LISA_VINSGR2VR_W, &w_all, &w, 2);

    /* w3 */
    latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_BU, &la_a0, &st, AES_ISH(0xc));
    latxs_append_ir2_opnd3i(LISA_ALSL_D, &temp0, &la_a0, &AES_Td_0, alsl_imm);
    latxs_append_ir2_opnd2i(LISA_LD_W, &w, &temp0, 0);

    latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_BU, &la_a0, &st, AES_ISH(0xd));
    latxs_append_ir2_opnd3i(LISA_ALSL_D, &temp0, &la_a0, &AES_Td_1, alsl_imm);
    latxs_append_ir2_opnd2i(LISA_LD_W, &temp0, &temp0, 0);
    latxs_append_ir2_opnd3(LISA_XOR, &w, &w, &temp0);

    latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_BU, &la_a0, &st, AES_ISH(0xe));
    latxs_append_ir2_opnd3i(LISA_ALSL_D, &temp0, &la_a0, &AES_Td_2, alsl_imm);
    latxs_append_ir2_opnd2i(LISA_LD_W, &temp0, &temp0, 0);
    latxs_append_ir2_opnd3(LISA_XOR, &w, &w, &temp0);

    latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_BU, &la_a0, &st, AES_ISH(0xf));
    latxs_append_ir2_opnd3i(LISA_ALSL_D, &temp0, &la_a0, &AES_Td_3, alsl_imm);
    latxs_append_ir2_opnd2i(LISA_LD_W, &temp0, &temp0, 0);
    latxs_append_ir2_opnd3(LISA_XOR, &w, &w, &temp0);

    latxs_append_ir2_opnd2(LISA_REVB_2H, &w, &w);
    latxs_append_ir2_opnd2i(LISA_ROTRI_W, &w, &w, 0x10);
    latxs_append_ir2_opnd2i(LISA_VINSGR2VR_W, &w_all, &w, 3);

    latxs_append_ir2_opnd3(LISA_VXOR_V, &r, &rk, &w_all);
}

bool latxs_translate_aesdec(IR1_INST *pir1)
{
    if (latxs_tr_gen_excp_check(pir1)) return true; // CPUID_EXT_AES

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);
    int d = ir1_opnd_base_reg_num(opnd0);

    if (!ir1_opnd_is_mem(opnd1)) {
        int s1 = ir1_opnd_base_reg_num(opnd1);
        if (option_aes_opt & 0x1) {
            IR2_OPND dest = latxs_ra_alloc_xmm(d);
            IR2_OPND src = latxs_ra_alloc_xmm(s1);
            cal_aesdec(dest, dest, src);
        } else {
            latxs_tr_gen_call_to_helper_aes((ADDR)helper_aesdec_xmm, d, d, s1,
                    HELPER_AESDEC_XMM);
        }
    } else {
        IR2_OPND temp = latxs_ra_alloc_ftemp();
        assert(ir1_opnd_size(opnd1) == 128);

        if (option_aes_opt & 0x1) {
            IR2_OPND dest = latxs_ra_alloc_xmm(d);
            latxs_load_freg128_from_ir1_mem(&temp, opnd1);
            cal_aesdec(dest, dest, temp);
        } else {
            int s1 = (d + 1) & 7;
            IR2_OPND src = latxs_ra_alloc_xmm(s1);
            if (option_enable_lasx) {
                latxs_append_ir2_opnd3(LISA_XVOR_V, &temp, &src, &src);
            } else {
                latxs_append_ir2_opnd3(LISA_VOR_V, &temp, &src, &src);
            }
            latxs_load_freg128_from_ir1_mem(&src, opnd1);
            latxs_tr_gen_call_to_helper_aes((ADDR)helper_aesdec_xmm, d, d, s1,
                    HELPER_AESDEC_XMM);
            if (option_enable_lasx) {
                latxs_append_ir2_opnd3(LISA_XVOR_V, &src, &temp, &temp);
            } else {
                latxs_append_ir2_opnd3(LISA_VOR_V, &src, &temp, &temp);
            }
        }
    }
    return true;
}

static inline void cal_aesdeclast(IR2_OPND ret, IR2_OPND st, IR2_OPND rk)
{
    ADDR isbox =(ADDR)AES_isbox;

    IR2_OPND b = latxs_ra_alloc_itemp();
    IR2_OPND temp0 = latxs_ra_alloc_itemp();
    IR2_OPND AES_isbox_opnd = latxs_ra_alloc_itemp();
    IR2_OPND b_all = latxs_ra_alloc_ftemp();

    latxs_load_addr_to_ir2(&AES_isbox_opnd, isbox, LOAD_AES_isbox);
    if (hamt_enabled()) latxs_tr_dmw_in_fix_mem(&AES_isbox_opnd);

    for(int i = 0; i <=15 ;i++) {
        latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_BU, &temp0, &st, AES_ISH(i));
        latxs_append_ir2_opnd3(LISA_LDX_B, &b, &AES_isbox_opnd, &temp0);
        latxs_append_ir2_opnd2i(LISA_VINSGR2VR_B, &b_all, &b, i);
    }
    latxs_append_ir2_opnd3(LISA_VXOR_V, &ret, &rk, &b_all);
}

bool latxs_translate_aesdeclast(IR1_INST *pir1)
{
    if (latxs_tr_gen_excp_check(pir1)) return true; // CPUID_EXT_AES

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);
    int d = ir1_opnd_base_reg_num(opnd0);

    if (!ir1_opnd_is_mem(opnd1)) {
        int s1 = ir1_opnd_base_reg_num(opnd1);
        if (option_aes_opt & 0x2) {
            IR2_OPND dest = latxs_ra_alloc_xmm(d);
            IR2_OPND src = latxs_ra_alloc_xmm(s1);
            cal_aesdeclast(dest, dest, src);
        } else {
            latxs_tr_gen_call_to_helper_aes((ADDR)helper_aesdeclast_xmm, d, d, s1,
                    HELPER_AESDECLAST_XMM);
        }
    } else {
        IR2_OPND temp = latxs_ra_alloc_ftemp();
        assert(ir1_opnd_size(opnd1) == 128);

        if (option_aes_opt & 0x2) {
            IR2_OPND dest = latxs_ra_alloc_xmm(d);
            latxs_load_freg128_from_ir1_mem(&temp, opnd1);
            cal_aesdeclast(dest, dest, temp);
        } else {
            int s1 = (d + 1) & 7;
            IR2_OPND src = latxs_ra_alloc_xmm(s1);
            if (option_enable_lasx) {
                latxs_append_ir2_opnd3(LISA_XVOR_V, &temp, &src, &src);
            } else {
                latxs_append_ir2_opnd3(LISA_VOR_V, &temp, &src, &src);
            }
            latxs_load_freg128_from_ir1_mem(&src, opnd1);
            latxs_tr_gen_call_to_helper_aes((ADDR)helper_aesdeclast_xmm, d, d, s1,
                    HELPER_AESDECLAST_XMM);
            if (option_enable_lasx) {
                latxs_append_ir2_opnd3(LISA_XVOR_V, &src, &temp, &temp);
            } else {
                latxs_append_ir2_opnd3(LISA_VOR_V, &src, &temp, &temp);
            }
        }
    }
    /* TODO: need to check */
    return true;
}

#define AES_SH(X)   (((X) * 5) & 15)
static inline void cal_aesenc(IR2_OPND r, IR2_OPND st, IR2_OPND rk)
{
    ADDR Te0 =(ADDR)AES_Te0;
    ADDR Te1 =(ADDR)AES_Te1;
    ADDR Te2 =(ADDR)AES_Te2;
    ADDR Te3 =(ADDR)AES_Te3;

    IR2_OPND w = latxs_ra_alloc_itemp();
    IR2_OPND temp0 = latxs_ra_alloc_itemp();
    IR2_OPND AES_Te_0 = latxs_ra_alloc_itemp();
    IR2_OPND AES_Te_1 = latxs_ra_alloc_itemp();
    IR2_OPND AES_Te_2 = latxs_ra_alloc_itemp();
    IR2_OPND AES_Te_3 = latxs_ra_alloc_itemp();
    IR2_OPND w_all = latxs_ra_alloc_ftemp();

    latxs_load_addr_to_ir2(&AES_Te_0, Te0, LOAD_AES_Te0);
    latxs_load_addr_to_ir2(&AES_Te_1, Te1, LOAD_AES_Te1);
    latxs_load_addr_to_ir2(&AES_Te_2, Te2, LOAD_AES_Te2);
    latxs_load_addr_to_ir2(&AES_Te_3, Te3, LOAD_AES_Te3);

    if (hamt_enabled()) latxs_tr_dmw_in_fix_mem(&AES_Te_0);
    if (hamt_enabled()) latxs_tr_dmw_in_fix_mem(&AES_Te_1);
    if (hamt_enabled()) latxs_tr_dmw_in_fix_mem(&AES_Te_2);
    if (hamt_enabled()) latxs_tr_dmw_in_fix_mem(&AES_Te_3);

    /* w0 */
    latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_BU, &la_a0, &st, AES_SH(0x0));
    latxs_append_ir2_opnd3i(LISA_ALSL_D, &temp0, &la_a0, &AES_Te_0, alsl_imm);
    latxs_append_ir2_opnd2i(LISA_LD_W, &w, &temp0, 0);

    latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_BU, &la_a0, &st, AES_SH(0x1));
    latxs_append_ir2_opnd3i(LISA_ALSL_D, &temp0, &la_a0, &AES_Te_1, alsl_imm);
    latxs_append_ir2_opnd2i(LISA_LD_W, &temp0, &temp0, 0);
    latxs_append_ir2_opnd3(LISA_XOR, &w, &w, &temp0);

    latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_BU, &la_a0, &st, AES_SH(0x2));
    latxs_append_ir2_opnd3i(LISA_ALSL_D, &temp0, &la_a0, &AES_Te_2, alsl_imm);
    latxs_append_ir2_opnd2i(LISA_LD_W, &temp0, &temp0, 0);
    latxs_append_ir2_opnd3(LISA_XOR, &w, &w, &temp0);

    latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_BU, &la_a0, &st, AES_SH(0x3));
    latxs_append_ir2_opnd3i(LISA_ALSL_D, &temp0, &la_a0, &AES_Te_3, alsl_imm);
    latxs_append_ir2_opnd2i(LISA_LD_W, &temp0, &temp0, 0);
    latxs_append_ir2_opnd3(LISA_XOR, &w, &w, &temp0);

    latxs_append_ir2_opnd2(LISA_REVB_2H, &w, &w);
    latxs_append_ir2_opnd2i(LISA_ROTRI_W, &w, &w, 0x10);
    latxs_append_ir2_opnd2i(LISA_VINSGR2VR_W, &w_all, &w, 0);

    /* w1 */
    latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_BU, &la_a0, &st, AES_SH(0x4));
    latxs_append_ir2_opnd3i(LISA_ALSL_D, &temp0, &la_a0, &AES_Te_0, alsl_imm);
    latxs_append_ir2_opnd2i(LISA_LD_W, &w, &temp0, 0);

    latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_BU, &la_a0, &st, AES_SH(0x5));
    latxs_append_ir2_opnd3i(LISA_ALSL_D, &temp0, &la_a0, &AES_Te_1, alsl_imm);
    latxs_append_ir2_opnd2i(LISA_LD_W, &temp0, &temp0, 0);
    latxs_append_ir2_opnd3(LISA_XOR, &w, &w, &temp0);

    latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_BU, &la_a0, &st, AES_SH(0x6));
    latxs_append_ir2_opnd3i(LISA_ALSL_D, &temp0, &la_a0, &AES_Te_2, alsl_imm);
    latxs_append_ir2_opnd2i(LISA_LD_W, &temp0, &temp0, 0);
    latxs_append_ir2_opnd3(LISA_XOR, &w, &w, &temp0);

    latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_BU, &la_a0, &st, AES_SH(0x7));
    latxs_append_ir2_opnd3i(LISA_ALSL_D, &temp0, &la_a0, &AES_Te_3, alsl_imm);
    latxs_append_ir2_opnd2i(LISA_LD_W, &temp0, &temp0, 0);
    latxs_append_ir2_opnd3(LISA_XOR, &w, &w, &temp0);

    latxs_append_ir2_opnd2(LISA_REVB_2H, &w, &w);
    latxs_append_ir2_opnd2i(LISA_ROTRI_W, &w, &w, 0x10);
    latxs_append_ir2_opnd2i(LISA_VINSGR2VR_W, &w_all, &w, 1);

    /* w2 */
    latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_BU, &la_a0, &st, AES_SH(0x8));
    latxs_append_ir2_opnd3i(LISA_ALSL_D, &temp0, &la_a0, &AES_Te_0, alsl_imm);
    latxs_append_ir2_opnd2i(LISA_LD_W, &w, &temp0, 0);

    latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_BU, &la_a0, &st, AES_SH(0x9));
    latxs_append_ir2_opnd3i(LISA_ALSL_D, &temp0, &la_a0, &AES_Te_1, alsl_imm);
    latxs_append_ir2_opnd2i(LISA_LD_W, &temp0, &temp0, 0);
    latxs_append_ir2_opnd3(LISA_XOR, &w, &w, &temp0);

    latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_BU, &la_a0, &st, AES_SH(0xa));
    latxs_append_ir2_opnd3i(LISA_ALSL_D, &temp0, &la_a0, &AES_Te_2, alsl_imm);
    latxs_append_ir2_opnd2i(LISA_LD_W, &temp0, &temp0, 0);
    latxs_append_ir2_opnd3(LISA_XOR, &w, &w, &temp0);

    latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_BU, &la_a0, &st, AES_SH(0xb));
    latxs_append_ir2_opnd3i(LISA_ALSL_D, &temp0, &la_a0, &AES_Te_3, alsl_imm);
    latxs_append_ir2_opnd2i(LISA_LD_W, &temp0, &temp0, 0);
    latxs_append_ir2_opnd3(LISA_XOR, &w, &w, &temp0);

    latxs_append_ir2_opnd2(LISA_REVB_2H, &w, &w);
    latxs_append_ir2_opnd2i(LISA_ROTRI_W, &w, &w, 0x10);
    latxs_append_ir2_opnd2i(LISA_VINSGR2VR_W, &w_all, &w, 2);

    /* w3 */
    latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_BU, &la_a0, &st, AES_SH(0xc));
    latxs_append_ir2_opnd3i(LISA_ALSL_D, &temp0, &la_a0, &AES_Te_0, alsl_imm);
    latxs_append_ir2_opnd2i(LISA_LD_W, &w, &temp0, 0);

    latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_BU, &la_a0, &st, AES_SH(0xd));
    latxs_append_ir2_opnd3i(LISA_ALSL_D, &temp0, &la_a0, &AES_Te_1, alsl_imm);
    latxs_append_ir2_opnd2i(LISA_LD_W, &temp0, &temp0, 0);
    latxs_append_ir2_opnd3(LISA_XOR, &w, &w, &temp0);

    latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_BU, &la_a0, &st, AES_SH(0xe));
    latxs_append_ir2_opnd3i(LISA_ALSL_D, &temp0, &la_a0, &AES_Te_2, alsl_imm);
    latxs_append_ir2_opnd2i(LISA_LD_W, &temp0, &temp0, 0);
    latxs_append_ir2_opnd3(LISA_XOR, &w, &w, &temp0);

    latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_BU, &la_a0, &st, AES_SH(0xf));
    latxs_append_ir2_opnd3i(LISA_ALSL_D, &temp0, &la_a0, &AES_Te_3, alsl_imm);
    latxs_append_ir2_opnd2i(LISA_LD_W, &temp0, &temp0, 0);
    latxs_append_ir2_opnd3(LISA_XOR, &w, &w, &temp0);

    latxs_append_ir2_opnd2(LISA_REVB_2H, &w, &w);
    latxs_append_ir2_opnd2i(LISA_ROTRI_W, &w, &w, 0x10);
    latxs_append_ir2_opnd2i(LISA_VINSGR2VR_W, &w_all, &w, 3);

    latxs_append_ir2_opnd3(LISA_VXOR_V, &r, &rk, &w_all);
}

bool latxs_translate_aesenc(IR1_INST *pir1)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);
    int d = ir1_opnd_base_reg_num(opnd0);

    if (!ir1_opnd_is_mem(opnd1)) {
        int s1 = ir1_opnd_base_reg_num(opnd1);
        if (option_aes_opt & 0x4) {
            IR2_OPND dest = latxs_ra_alloc_xmm(d);
            IR2_OPND src = latxs_ra_alloc_xmm(s1);
            cal_aesenc(dest, dest, src);
        } else {
            latxs_tr_gen_call_to_helper_aes((ADDR)helper_aesenc_xmm, d, d, s1,
                    HELPER_AESENC_XMM);
        }
    } else {
        IR2_OPND temp = latxs_ra_alloc_ftemp();
        assert(ir1_opnd_size(opnd1) == 128);

        if (option_aes_opt & 0x4) {
            IR2_OPND dest = latxs_ra_alloc_xmm(d);
            latxs_load_freg128_from_ir1_mem(&temp, opnd1);
            cal_aesenc(dest, dest, temp);
        } else {
            int s1 = (d + 1) & 7;
            IR2_OPND src = latxs_ra_alloc_xmm(s1);
            if (option_enable_lasx) {
                latxs_append_ir2_opnd3(LISA_XVOR_V, &temp, &src, &src);
            } else {
                latxs_append_ir2_opnd3(LISA_VOR_V, &temp, &src, &src);
            }
            latxs_load_freg128_from_ir1_mem(&src, opnd1);
            latxs_tr_gen_call_to_helper_aes((ADDR)helper_aesenc_xmm, d, d, s1,
                    HELPER_AESENC_XMM);
            if (option_enable_lasx) {
                latxs_append_ir2_opnd3(LISA_XVOR_V, &src, &temp, &temp);
            } else {
                latxs_append_ir2_opnd3(LISA_VOR_V, &src, &temp, &temp);
            }
        }
    }
    /* TODO: need to check */
    return true;
}

static inline void cal_aesenclast(IR2_OPND ret, IR2_OPND st, IR2_OPND rk)
{
    ADDR sbox =(ADDR)AES_sbox;

    IR2_OPND b = latxs_ra_alloc_itemp();
    IR2_OPND temp0 = latxs_ra_alloc_itemp();
    IR2_OPND AES_sbox_opnd = latxs_ra_alloc_itemp();
    IR2_OPND b_all = latxs_ra_alloc_ftemp();

    latxs_load_addr_to_ir2(&AES_sbox_opnd, sbox, LOAD_AES_sbox);
    if (hamt_enabled()) latxs_tr_dmw_in_fix_mem(&AES_sbox_opnd);

    for(int i = 0; i <=15 ;i++) {
        latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_BU, &temp0, &st, AES_SH(i));
        latxs_append_ir2_opnd3(LISA_LDX_B, &b, &AES_sbox_opnd, &temp0);
        latxs_append_ir2_opnd2i(LISA_VINSGR2VR_B, &b_all, &b, i);
    }
    latxs_append_ir2_opnd3(LISA_VXOR_V, &ret, &rk, &b_all);
}

bool latxs_translate_aesenclast(IR1_INST *pir1)
{
    if (latxs_tr_gen_excp_check(pir1)) return true; // CPUID_EXT_AES

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);
    int d = ir1_opnd_base_reg_num(opnd0);

    if (!ir1_opnd_is_mem(opnd1)) {
        int s1 = ir1_opnd_base_reg_num(opnd1);
        if (option_aes_opt & 0x8) {
            IR2_OPND dest = latxs_ra_alloc_xmm(d);
            IR2_OPND src = latxs_ra_alloc_xmm(s1);
            cal_aesenclast(dest, dest, src);
        } else {
            latxs_tr_gen_call_to_helper_aes((ADDR)helper_aesenclast_xmm, d, d, s1,
                    HELPER_AESENCLAST_XMM);
        }
    } else {
        IR2_OPND temp = latxs_ra_alloc_ftemp();
        assert(ir1_opnd_size(opnd1) == 128);

        if (option_aes_opt & 0x8) {
            IR2_OPND dest = latxs_ra_alloc_xmm(d);
            latxs_load_freg128_from_ir1_mem(&temp, opnd1);
            cal_aesenclast(dest, dest, temp);
        } else {
            int s1 = (d + 1) & 7;
            IR2_OPND src = latxs_ra_alloc_xmm(s1);
            if (option_enable_lasx) {
                latxs_append_ir2_opnd3(LISA_XVOR_V, &temp, &src, &src);
            } else {
                latxs_append_ir2_opnd3(LISA_VOR_V, &temp, &src, &src);
            }
            latxs_load_freg128_from_ir1_mem(&src, opnd1);
            latxs_tr_gen_call_to_helper_aes((ADDR)helper_aesenclast_xmm, d, d, s1,
                    HELPER_AESENCLAST_XMM);
            if (option_enable_lasx) {
                latxs_append_ir2_opnd3(LISA_XVOR_V, &src, &temp, &temp);
            } else {
                latxs_append_ir2_opnd3(LISA_VOR_V, &src, &temp, &temp);
            }
        }
    }
    /* TODO: need to check */
    return true;
}

static inline void cal_aesimc(IR2_OPND r,IR2_OPND st)
{
    ADDR AES_IMC_ROT_ADDR = (ADDR)AES_imc_rot;

    IR2_OPND AES_IMC = latxs_ra_alloc_itemp();
    IR2_OPND w = latxs_ra_alloc_itemp();
    IR2_OPND st_b = latxs_ra_alloc_itemp();
    IR2_OPND temp0 = latxs_ra_alloc_itemp();

    latxs_load_addr_to_ir2(&AES_IMC, AES_IMC_ROT_ADDR, LOAD_AES_imc_rot);
    if (hamt_enabled()) latxs_tr_dmw_in_fix_mem(&AES_IMC);

    /* w0 */
    latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_BU, &st_b, &st, 0x0);
    latxs_append_ir2_opnd3i(LISA_ALSL_D, &w, &st_b, &AES_IMC, alsl_imm);
    latxs_append_ir2_opnd2i(LISA_LD_W, &w, &w, 0);

    latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_BU, &st_b, &st, 0x1);
    latxs_append_ir2_opnd3i(LISA_ALSL_D, &temp0, &st_b, &AES_IMC, alsl_imm);
    latxs_append_ir2_opnd2i(LISA_LD_W, &temp0, &temp0, 0);
    latxs_append_ir2_opnd2i(LISA_ROTRI_W, &temp0, &temp0, 24);
    latxs_append_ir2_opnd3(LISA_XOR, &w, &w, &temp0);

    latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_BU, &st_b, &st, 0x2);
    latxs_append_ir2_opnd3i(LISA_ALSL_D, &temp0, &st_b, &AES_IMC, alsl_imm);
    latxs_append_ir2_opnd2i(LISA_LD_W, &temp0, &temp0, 0);
    latxs_append_ir2_opnd2i(LISA_ROTRI_W, &temp0, &temp0, 16);
    latxs_append_ir2_opnd3(LISA_XOR, &w, &w, &temp0);

    latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_BU, &st_b, &st, 0x3);
    latxs_append_ir2_opnd3i(LISA_ALSL_D, &temp0, &st_b, &AES_IMC, alsl_imm);
    latxs_append_ir2_opnd2i(LISA_LD_W, &temp0, &temp0, 0);
    latxs_append_ir2_opnd2i(LISA_ROTRI_W, &temp0, &temp0, 8);
    latxs_append_ir2_opnd3(LISA_XOR, &w, &w, &temp0);

    latxs_append_ir2_opnd2i(LISA_VINSGR2VR_W, &r, &w, 0);

    /* w1 */
    latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_BU, &st_b, &st, 0x4);
    latxs_append_ir2_opnd3i(LISA_ALSL_D, &w, &st_b, &AES_IMC, alsl_imm);
    latxs_append_ir2_opnd2i(LISA_LD_W, &w, &w, 0);

    latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_BU, &st_b, &st, 0x5);
    latxs_append_ir2_opnd3i(LISA_ALSL_D, &temp0, &st_b, &AES_IMC, alsl_imm);
    latxs_append_ir2_opnd2i(LISA_LD_W, &temp0, &temp0, 0);
    latxs_append_ir2_opnd2i(LISA_ROTRI_W, &temp0, &temp0, 24);
    latxs_append_ir2_opnd3(LISA_XOR, &w, &w, &temp0);

    latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_BU, &st_b, &st, 0x6);
    latxs_append_ir2_opnd3i(LISA_ALSL_D, &temp0, &st_b, &AES_IMC, alsl_imm);
    latxs_append_ir2_opnd2i(LISA_LD_W, &temp0, &temp0, 0);
    latxs_append_ir2_opnd2i(LISA_ROTRI_W, &temp0, &temp0, 16);
    latxs_append_ir2_opnd3(LISA_XOR, &w, &w, &temp0);

    latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_BU, &st_b, &st, 0x7);
    latxs_append_ir2_opnd3i(LISA_ALSL_D, &temp0, &st_b, &AES_IMC, alsl_imm);
    latxs_append_ir2_opnd2i(LISA_LD_W, &temp0, &temp0, 0);
    latxs_append_ir2_opnd2i(LISA_ROTRI_W, &temp0, &temp0, 8);
    latxs_append_ir2_opnd3(LISA_XOR, &w, &w, &temp0);

    latxs_append_ir2_opnd2i(LISA_VINSGR2VR_W, &r, &w, 1);

    /* w2 */
    latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_BU, &st_b, &st, 0x8);
    latxs_append_ir2_opnd3i(LISA_ALSL_D, &w, &st_b, &AES_IMC, alsl_imm);
    latxs_append_ir2_opnd2i(LISA_LD_W, &w, &w, 0);

    latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_BU, &st_b, &st, 0x9);
    latxs_append_ir2_opnd3i(LISA_ALSL_D, &temp0, &st_b, &AES_IMC, alsl_imm);
    latxs_append_ir2_opnd2i(LISA_LD_W, &temp0, &temp0, 0);
    latxs_append_ir2_opnd2i(LISA_ROTRI_W, &temp0, &temp0, 24);
    latxs_append_ir2_opnd3(LISA_XOR, &w, &w, &temp0);

    latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_BU, &st_b, &st, 0xa);
    latxs_append_ir2_opnd3i(LISA_ALSL_D, &temp0, &st_b, &AES_IMC, alsl_imm);
    latxs_append_ir2_opnd2i(LISA_LD_W, &temp0, &temp0, 0);
    latxs_append_ir2_opnd2i(LISA_ROTRI_W, &temp0, &temp0, 16);
    latxs_append_ir2_opnd3(LISA_XOR, &w, &w, &temp0);

    latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_BU, &st_b, &st, 0xb);
    latxs_append_ir2_opnd3i(LISA_ALSL_D, &temp0, &st_b, &AES_IMC, alsl_imm);
    latxs_append_ir2_opnd2i(LISA_LD_W, &temp0, &temp0, 0);
    latxs_append_ir2_opnd2i(LISA_ROTRI_W, &temp0, &temp0, 8);
    latxs_append_ir2_opnd3(LISA_XOR, &w, &w, &temp0);

    latxs_append_ir2_opnd2i(LISA_VINSGR2VR_W, &r, &w, 2);

    /* w3 */
    latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_BU, &st_b, &st, 0xc);
    latxs_append_ir2_opnd3i(LISA_ALSL_D, &w, &st_b, &AES_IMC, alsl_imm);
    latxs_append_ir2_opnd2i(LISA_LD_W, &w, &w, 0);

    latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_BU, &st_b, &st, 0xd);
    latxs_append_ir2_opnd3i(LISA_ALSL_D, &temp0, &st_b, &AES_IMC, alsl_imm);
    latxs_append_ir2_opnd2i(LISA_LD_W, &temp0, &temp0, 0);
    latxs_append_ir2_opnd2i(LISA_ROTRI_W, &temp0, &temp0, 24);
    latxs_append_ir2_opnd3(LISA_XOR, &w, &w, &temp0);

    latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_BU, &st_b, &st, 0xe);
    latxs_append_ir2_opnd3i(LISA_ALSL_D, &temp0, &st_b, &AES_IMC, alsl_imm);
    latxs_append_ir2_opnd2i(LISA_LD_W, &temp0, &temp0, 0);
    latxs_append_ir2_opnd2i(LISA_ROTRI_W, &temp0, &temp0, 16);
    latxs_append_ir2_opnd3(LISA_XOR, &w, &w, &temp0);

    latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_BU, &st_b, &st, 0xf);
    latxs_append_ir2_opnd3i(LISA_ALSL_D, &temp0, &st_b, &AES_IMC, alsl_imm);
    latxs_append_ir2_opnd2i(LISA_LD_W, &temp0, &temp0, 0);
    latxs_append_ir2_opnd2i(LISA_ROTRI_W, &temp0, &temp0, 8);
    latxs_append_ir2_opnd3(LISA_XOR, &w, &w, &temp0);

    latxs_append_ir2_opnd2i(LISA_VINSGR2VR_W, &r, &w, 3);

}

bool latxs_translate_aesimc(IR1_INST *pir1)
{
    if (latxs_tr_gen_excp_check(pir1)) return true; // CPUID_EXT_AES

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);
    int d = ir1_opnd_base_reg_num(opnd0);

    if (!ir1_opnd_is_mem(opnd1)) {
        int s1 = ir1_opnd_base_reg_num(opnd1);
        if (option_aes_opt & 0x10) {
            IR2_OPND dest = latxs_ra_alloc_xmm(d);
            IR2_OPND src = latxs_ra_alloc_xmm(s1);
            cal_aesimc(dest, src);
        } else {
            latxs_tr_gen_call_to_helper_pcmpxstrx((ADDR)helper_aesimc_xmm, d, s1, 0,
                    HELPER_AESIMC_XMM);
        }
    } else {
        IR2_OPND temp = latxs_ra_alloc_ftemp();
        assert(ir1_opnd_size(opnd1) == 128);

        if (option_aes_opt & 0x10) {
            IR2_OPND dest = latxs_ra_alloc_xmm(d);
            latxs_load_freg128_from_ir1_mem(&temp, opnd1);
            cal_aesimc(dest, temp);
        } else {
            int s1 = (d + 1) & 7;
            IR2_OPND src = latxs_ra_alloc_xmm(s1);
            if (option_enable_lasx) {
                latxs_append_ir2_opnd3(LISA_XVOR_V, &temp, &src, &src);
            } else {
                latxs_append_ir2_opnd3(LISA_VOR_V, &temp, &src, &src);
            }
            latxs_load_freg128_from_ir1_mem(&src, opnd1);
            latxs_tr_gen_call_to_helper_pcmpxstrx((ADDR)helper_aesimc_xmm, d, s1, 0,
                    HELPER_AESIMC_XMM);
            if (option_enable_lasx) {
                latxs_append_ir2_opnd3(LISA_XVOR_V, &src, &temp, &temp);
            } else {
                latxs_append_ir2_opnd3(LISA_VOR_V, &src, &temp, &temp);
            }
        }
    }
    /* TODO: need to check */
    return true;
}

static inline void cal_aeskeygenassist(IR2_OPND d,IR2_OPND s, int ctrl)
{
    ADDR sbox =(ADDR)AES_sbox;
    ctrl = ctrl & 0xff;

    IR2_OPND AES_sbox_opnd = latxs_ra_alloc_itemp();
    IR2_OPND temp0 = latxs_ra_alloc_itemp();

    latxs_load_addr_to_ir2(&AES_sbox_opnd, sbox, LOAD_AES_sbox);
    if (hamt_enabled()) latxs_tr_dmw_in_fix_mem(&AES_sbox_opnd);

    for(int i = 0; i < 4; i++) {
        latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_BU, &temp0, &s, i + 4);
        latxs_append_ir2_opnd3(LISA_LDX_B, &temp0, &AES_sbox_opnd, &temp0);
        latxs_append_ir2_opnd2i(LISA_VINSGR2VR_B, &d, &temp0, i);

        latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_BU, &temp0, &s, i + 12);
        latxs_append_ir2_opnd3(LISA_LDX_B, &temp0, &AES_sbox_opnd, &temp0);
        latxs_append_ir2_opnd2i(LISA_VINSGR2VR_B, &d, &temp0, i + 8);
    }
    latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_WU, &temp0, &d, 0);
    latxs_append_ir2_opnd2i(LISA_ROTRI_W, &temp0, &temp0, 8);
    latxs_append_ir2_opnd2i(LISA_XORI, &temp0, &temp0, ctrl);
    latxs_append_ir2_opnd2i(LISA_VINSGR2VR_W, &d, &temp0, 1);

    latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_WU, &temp0, &d, 2);
    latxs_append_ir2_opnd2i(LISA_ROTRI_W, &temp0, &temp0, 8);
    latxs_append_ir2_opnd2i(LISA_XORI, &temp0, &temp0, ctrl);
    latxs_append_ir2_opnd2i(LISA_VINSGR2VR_W, &d, &temp0, 3);
}

bool latxs_translate_aeskeygenassist(IR1_INST *pir1)
{
    if (latxs_tr_gen_excp_check(pir1)) return true; // CPUID_EXT_AES

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);
    IR1_OPND *opnd2 = ir1_get_opnd(pir1, 2);
    int d = ir1_opnd_base_reg_num(opnd0);
    int imm = ir1_opnd_uimm(opnd2);

    if (ir1_opnd_is_xmm(opnd1)) {
        int s1 = ir1_opnd_base_reg_num(opnd1);
        if (option_aes_opt & 0x20) {
            IR2_OPND dest = latxs_ra_alloc_xmm(d);
            IR2_OPND src = latxs_ra_alloc_xmm(s1);
            cal_aeskeygenassist(dest, src, imm);
        } else {
            latxs_tr_gen_call_to_helper_pcmpxstrx((ADDR)helper_aeskeygenassist_xmm, d, s1, imm,
                    HELPER_AESKEYGENASSIST_XMM);
        }
    } else {
        IR2_OPND temp = latxs_ra_alloc_ftemp();

        if (option_aes_opt & 0x20) {
            IR2_OPND dest = latxs_ra_alloc_xmm(d);
            latxs_load_freg128_from_ir1_mem(&temp, opnd1);
            cal_aeskeygenassist(dest, temp, imm);
        } else {
            int s1 = (d + 1) & 7;
            IR2_OPND src = latxs_ra_alloc_xmm(s1);
            if (option_enable_lasx) {
                latxs_append_ir2_opnd3(LISA_XVOR_V, &temp, &src, &src);
            } else {
                latxs_append_ir2_opnd3(LISA_VOR_V, &temp, &src, &src);
            }
            latxs_load_freg128_from_ir1_mem(&src, opnd1);
            latxs_tr_gen_call_to_helper_pcmpxstrx((ADDR)helper_aeskeygenassist_xmm, d, s1, imm,
                    HELPER_AESKEYGENASSIST_XMM);
            if (option_enable_lasx) {
                latxs_append_ir2_opnd3(LISA_XVOR_V, &src, &temp, &temp);
            } else {
                latxs_append_ir2_opnd3(LISA_VOR_V, &src, &temp, &temp);
            }
        }
    }
    /* TODO: need to check */
    return true;
}

bool latxs_translate_andn(IR1_INST *pir1)
{
    IR2_OPND dest = latxs_load_ireg_from_ir1(ir1_get_opnd(pir1, 0), EXMode_N);
    IR2_OPND src0 = latxs_load_ireg_from_ir1(ir1_get_opnd(pir1, 1), EXMode_N);
    IR2_OPND src1 = latxs_load_ireg_from_ir1(ir1_get_opnd(pir1, 2), EXMode_N);
    int opnd0_size = ir1_opnd_size(ir1_get_opnd(pir1, 0));
    IR2_OPND temp = latxs_ra_alloc_itemp();

    latxs_append_ir2_opnd3(LISA_ORN, &temp, &latxs_zero_ir2_opnd, &src0);
    latxs_generate_eflag_calculation(&dest, &temp, &src1, pir1, false);
    latxs_append_ir2_opnd3(LISA_ANDN, &temp, &src1, &src0);

    if (opnd0_size == 32) {
        latxs_append_ir2_opnd2ii(LISA_BSTRPICK_D, &dest, &temp, 31, 0);
    } else {
        latxs_append_ir2_opnd2i(LISA_ORI, &dest, &temp, 0);
    }
    latxs_store_ir2_to_ir1(&dest, ir1_get_opnd(pir1, 0));

    return true;
}

void latxs_gen_avx_aes(IR1_OPCODE opcode, IR2_OPND dest, IR2_OPND src,
                       IR2_OPND key, uint8_t imm)
{
    switch (opcode) {
    case X86_INS_VAESDEC:
        cal_aesdec(dest, src, key);
        break;
    case X86_INS_VAESDECLAST:
        cal_aesdeclast(dest, src, key);
        break;
    case X86_INS_VAESENC:
        cal_aesenc(dest, src, key);
        break;
    case X86_INS_VAESENCLAST:
        cal_aesenclast(dest, src, key);
        break;
    case X86_INS_VAESIMC:
        cal_aesimc(dest, src);
        break;
    case X86_INS_VAESKEYGENASSIST:
        cal_aeskeygenassist(dest, src, imm);
        break;
    default:
        g_assert_not_reached();
    }
}

void latxs_gen_pclmulqdq(IR2_OPND dest, IR2_OPND left, IR2_OPND right,
                         uint8_t ctrl)
{
    IR2_OPND a = latxs_ra_alloc_itemp();
    IR2_OPND b = latxs_ra_alloc_itemp();
    IR2_OPND low = latxs_ra_alloc_itemp();
    IR2_OPND high = latxs_ra_alloc_itemp();
    IR2_OPND a_half = la_a2;
    IR2_OPND shift = la_a0;
    IR2_OPND part = la_a1;
    IR2_OPND loop = latxs_ir2_opnd_new_label();
    IR2_OPND done = latxs_ir2_opnd_new_label();

    latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_D, &a, &left, ctrl & 1);
    latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_D, &b, &right, (ctrl >> 4) & 1);
    latxs_append_ir2_opnd2i(LISA_SRLI_D, &a_half, &a, 1);
    latxs_append_ir2_opnd2i(LISA_ORI, &low, &la_zero, 0);
    latxs_append_ir2_opnd2i(LISA_ORI, &high, &la_zero, 0);
    latxs_append_ir2_opnd2(LISA_BEQZ, &b, &done);

    /* Each set bit adds one shifted copy of a over GF(2). */
    latxs_append_ir2_opnd1(LISA_LABEL, &loop);
    latxs_append_ir2_opnd2(LISA_CTZ_D, &shift, &b);
    latxs_append_ir2_opnd3(LISA_SLL_D, &part, &a, &shift);
    latxs_append_ir2_opnd3(LISA_XOR, &low, &low, &part);
    /* (a >> 1) >> (63 - shift) is also valid for shift == 0. */
    latxs_append_ir2_opnd2i(LISA_XORI, &shift, &shift, 63);
    latxs_append_ir2_opnd3(LISA_SRL_D, &part, &a_half, &shift);
    latxs_append_ir2_opnd3(LISA_XOR, &high, &high, &part);
    latxs_append_ir2_opnd2i(LISA_ADDI_D, &part, &b, -1);
    latxs_append_ir2_opnd3(LISA_AND, &b, &b, &part);
    latxs_append_ir2_opnd2(LISA_BNEZ, &b, &loop);

    latxs_append_ir2_opnd1(LISA_LABEL, &done);
    latxs_append_ir2_opnd2i(LISA_VINSGR2VR_D, &dest, &low, 0);
    latxs_append_ir2_opnd2i(LISA_VINSGR2VR_D, &dest, &high, 1);
}

bool latxs_translate_pclmulqdq(IR1_INST * pir1)
{
    if (latxs_tr_gen_excp_check(pir1)) return true; // CPUID_EXT_PCLMULQDQ

    IR1_OPND * opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND * opnd1 = ir1_get_opnd(pir1, 1);
    IR1_OPND * opnd2 = ir1_get_opnd(pir1, 2);

    int s0 = ir1_opnd_base_reg_num(opnd0);
    uint8_t ctrl = ir1_opnd_uimm(opnd2);
    if (!ir1_opnd_is_mem(opnd1)) {
        int s1 = ir1_opnd_base_reg_num(opnd1);
        if(option_aes_opt & 0x40) {
            IR2_OPND dest = latxs_ra_alloc_xmm(s0);
            IR2_OPND src = latxs_ra_alloc_xmm(s1);
            latxs_gen_pclmulqdq(dest, dest, src, ctrl);
        } else {
            latxs_tr_gen_call_to_helper_pclmulqdq((ADDR)helper_pclmulqdq_xmm, s0, s0, s1, ctrl,
                    HELPER_PCLMULQDQ_XMM);
        }
    } else {
        if(option_aes_opt & 0x40) {
            IR2_OPND dest = latxs_ra_alloc_xmm(s0);
            IR2_OPND src = latxs_ra_alloc_ftemp();
            latxs_load_freg128_from_ir1_mem(&src, opnd1);
            latxs_gen_pclmulqdq(dest, dest, src, ctrl);
        } else {
            int s1 = 0;
            while (s1 < 8) {
                if (s1 != s0) {
                    break;
                }
                s1++;
            }
            IR2_OPND temp_mem = latxs_ra_alloc_ftemp();
            IR2_OPND src = latxs_ra_alloc_xmm(s1);

            if (option_enable_lasx) {
                latxs_append_ir2_opnd3(LISA_XVOR_V, &temp_mem, &src, &src);
            } else {
                latxs_append_ir2_opnd3(LISA_VOR_V, &temp_mem, &src, &src);
            }
            latxs_load_freg128_from_ir1_mem(&src, opnd1);
            latxs_tr_gen_call_to_helper_pclmulqdq((ADDR)helper_pclmulqdq_xmm, s0, s0, s1, ctrl,
                    HELPER_PCLMULQDQ_XMM);

            if (option_enable_lasx) {
                latxs_append_ir2_opnd3(LISA_XVOR_V, &src, &temp_mem, &temp_mem);
            } else {
                latxs_append_ir2_opnd3(LISA_VOR_V, &src, &temp_mem, &temp_mem);
            }
        }
    }
    return true;
}

bool latxs_translate_sha1msg1(IR1_INST *pir1)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);
    int d = ir1_opnd_base_reg_num(opnd0);

    if (!ir1_opnd_is_mem(opnd1)) {
        int s1 = ir1_opnd_base_reg_num(opnd1);
        latxs_tr_gen_call_to_helper_sha((ADDR)helper_sha1msg1, d, d, s1, HELPER_SHA1MSG1);
    } else {
        int s1 = (d + 1) & 7;
        IR2_OPND temp = latxs_ra_alloc_ftemp();
        IR2_OPND src = latxs_ra_alloc_xmm(s1);

        if (option_enable_lasx) {
            latxs_append_ir2_opnd3(LISA_XVOR_V, &temp, &src, &src);
        } else {
            latxs_append_ir2_opnd3(LISA_VOR_V, &temp, &src, &src);
        }
        assert(ir1_opnd_size(opnd1) == 128);
        latxs_load_freg128_from_ir1_mem(&src, opnd1);

        latxs_tr_gen_call_to_helper_sha((ADDR)helper_sha1msg1, d, d, s1, HELPER_SHA1MSG1);

        if (option_enable_lasx) {
            latxs_append_ir2_opnd3(LISA_XVOR_V, &src, &temp, &temp);
        } else {
            latxs_append_ir2_opnd3(LISA_VOR_V, &src, &temp, &temp);
        }
    }
    /* TODO: need to check */
    return true;
}

bool latxs_translate_sha1msg2(IR1_INST *pir1)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);
    int d = ir1_opnd_base_reg_num(opnd0);

    if (!ir1_opnd_is_mem(opnd1)) {
        int s1 = ir1_opnd_base_reg_num(opnd1);
        latxs_tr_gen_call_to_helper_sha((ADDR)helper_sha1msg2, d, d, s1, HELPER_SHA1MSG2);
    } else {
        int s1 = (d + 1) & 7;
        IR2_OPND temp = latxs_ra_alloc_ftemp();
        IR2_OPND src = latxs_ra_alloc_xmm(s1);

        if (option_enable_lasx) {
            latxs_append_ir2_opnd3(LISA_XVOR_V, &temp, &src, &src);
        } else {
            latxs_append_ir2_opnd3(LISA_VOR_V, &temp, &src, &src);
        }
        assert(ir1_opnd_size(opnd1) == 128);
        latxs_load_freg128_from_ir1_mem(&src, opnd1);

        latxs_tr_gen_call_to_helper_sha((ADDR)helper_sha1msg2, d, d, s1, HELPER_SHA1MSG2);

        if (option_enable_lasx) {
            latxs_append_ir2_opnd3(LISA_XVOR_V, &src, &temp, &temp);
        } else {
            latxs_append_ir2_opnd3(LISA_VOR_V, &src, &temp, &temp);
        }
    }
    /* TODO: need to check */
    return true;
}

bool latxs_translate_sha1nexte(IR1_INST *pir1)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);
    int d = ir1_opnd_base_reg_num(opnd0);
    if (!ir1_opnd_is_mem(opnd1)) {
        int s1 = ir1_opnd_base_reg_num(opnd1);
        latxs_tr_gen_call_to_helper_sha((ADDR)helper_sha1nexte, d, d, s1, HELPER_SHA1NEXTE);
    } else {
        int s1 = (d + 1) & 7;
        IR2_OPND temp = latxs_ra_alloc_ftemp();
        IR2_OPND src = latxs_ra_alloc_xmm(s1);

        if (option_enable_lasx) {
            latxs_append_ir2_opnd3(LISA_XVOR_V, &temp, &src, &src);
        } else {
            latxs_append_ir2_opnd3(LISA_VOR_V, &temp, &src, &src);
        }
        assert(ir1_opnd_size(opnd1) == 128);
        latxs_load_freg128_from_ir1_mem(&src, opnd1);

        latxs_tr_gen_call_to_helper_sha((ADDR)helper_sha1nexte, d, d, s1, HELPER_SHA1NEXTE);

        if (option_enable_lasx) {
            latxs_append_ir2_opnd3(LISA_XVOR_V, &src, &temp, &temp);
        } else {
            latxs_append_ir2_opnd3(LISA_VOR_V, &src, &temp, &temp);
        }
    }
    /* TODO: need to check */
    return true;
}

bool latxs_translate_sha1rnds4(IR1_INST *pir1)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);
    IR1_OPND *opnd2 = ir1_get_opnd(pir1, 2);
    int imm = ir1_opnd_uimm(opnd2);
    int d = ir1_opnd_base_reg_num(opnd0);
    ADDR helper_func;
    int helper_kind;

    switch (imm) {
        case 0:
            helper_func = (ADDR)helper_sha1rnds4_f0;
            helper_kind = HELPER_SHA1RNDS4_F0;
            break;
        case 1:
            helper_func = (ADDR)helper_sha1rnds4_f1;
            helper_kind = HELPER_SHA1RNDS4_F1;
            break;
        case 2:
            helper_func = (ADDR)helper_sha1rnds4_f2;
            helper_kind = HELPER_SHA1RNDS4_F2;
            break;
        case 3:
            helper_func = (ADDR)helper_sha1rnds4_f3;
            helper_kind = HELPER_SHA1RNDS4_F3;
            break;
        default:
            helper_func = 0xdeadbeaf;
            helper_kind = DEADBEAF;
            lsassert(0);
            break;
    }

    if (!ir1_opnd_is_mem(opnd1)) {
        int s1 = ir1_opnd_base_reg_num(opnd1);
        latxs_tr_gen_call_to_helper_sha((ADDR)helper_func, d, d, s1, helper_kind);
    } else {
        int s1 = (d + 1) & 7;
		/* DO NOT use XMM0 because this insns use it implicitly */
        if (s1 == 0)
            s1++;
        IR2_OPND temp = latxs_ra_alloc_ftemp();
        IR2_OPND src = latxs_ra_alloc_xmm(s1);

        if (option_enable_lasx) {
            latxs_append_ir2_opnd3(LISA_XVOR_V, &temp, &src, &src);
        } else {
            latxs_append_ir2_opnd3(LISA_VOR_V, &temp, &src, &src);
        }
        assert(ir1_opnd_size(opnd1) == 128);
        latxs_load_freg128_from_ir1_mem(&src, opnd1);

        latxs_tr_gen_call_to_helper_sha((ADDR)helper_func, d, d, s1, helper_kind);

        if (option_enable_lasx) {
            latxs_append_ir2_opnd3(LISA_XVOR_V, &src, &temp, &temp);
        } else {
            latxs_append_ir2_opnd3(LISA_VOR_V, &src, &temp, &temp);
        }
    }
    /* TODO: need to check */
    return true;
}

bool latxs_translate_sha256msg1(IR1_INST *pir1)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);
    int d = ir1_opnd_base_reg_num(opnd0);
    if (!ir1_opnd_is_mem(opnd1)) {
        int s1 = ir1_opnd_base_reg_num(opnd1);
        latxs_tr_gen_call_to_helper_sha((ADDR)helper_sha256msg1, d, d, s1, HELPER_SHA256MSG1);
    } else {
        int s1 = (d + 1) & 7;
        IR2_OPND temp = latxs_ra_alloc_ftemp();
        IR2_OPND src = latxs_ra_alloc_xmm(s1);

        if (option_enable_lasx) {
            latxs_append_ir2_opnd3(LISA_XVOR_V, &temp, &src, &src);
        } else {
            latxs_append_ir2_opnd3(LISA_VOR_V, &temp, &src, &src);
        }
        assert(ir1_opnd_size(opnd1) == 128);
        latxs_load_freg128_from_ir1_mem(&src, opnd1);

        latxs_tr_gen_call_to_helper_sha((ADDR)helper_sha256msg1, d, d, s1, HELPER_SHA256MSG1);

        if (option_enable_lasx) {
            latxs_append_ir2_opnd3(LISA_XVOR_V, &src, &temp, &temp);
        } else {
            latxs_append_ir2_opnd3(LISA_VOR_V, &src, &temp, &temp);
        }
    }
    /* TODO: need to check */
    return true;
}

bool latxs_translate_sha256msg2(IR1_INST *pir1)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);
    int d = ir1_opnd_base_reg_num(opnd0);
    if (!ir1_opnd_is_mem(opnd1)) {
        int s1 = ir1_opnd_base_reg_num(opnd1);
        latxs_tr_gen_call_to_helper_sha((ADDR)helper_sha256msg2, d, d, s1, HELPER_SHA256MSG2);
    } else {
        int s1 = (d + 1) & 7;
        IR2_OPND temp = latxs_ra_alloc_ftemp();
        IR2_OPND src = latxs_ra_alloc_xmm(s1);

        if (option_enable_lasx) {
            latxs_append_ir2_opnd3(LISA_XVOR_V, &temp, &src, &src);
        } else {
            latxs_append_ir2_opnd3(LISA_VOR_V, &temp, &src, &src);
        }
        assert(ir1_opnd_size(opnd1) == 128);
        latxs_load_freg128_from_ir1_mem(&src, opnd1);

        latxs_tr_gen_call_to_helper_sha((ADDR)helper_sha256msg2, d, d, s1, HELPER_SHA256MSG2);

        if (option_enable_lasx) {
            latxs_append_ir2_opnd3(LISA_XVOR_V, &src, &temp, &temp);
        } else {
            latxs_append_ir2_opnd3(LISA_VOR_V, &src, &temp, &temp);
        }
    }
    /* TODO: need to check */
    return true;
}

bool latxs_translate_sha256rnds2(IR1_INST *pir1)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);
    int d = ir1_opnd_base_reg_num(opnd0);
    if (!ir1_opnd_is_mem(opnd1)) {
        int s1 = ir1_opnd_base_reg_num(opnd1);
        latxs_tr_gen_call_to_helper_sha256rnds2((ADDR)helper_sha256rnds2, d, d, s1,
                HELPER_SHA256RNDS2);
    } else {
        int s1 = (d + 1) & 7;
		/* DO NOT use XMM0 because this insns use it implicitly */
        if (s1 == 0)
            s1++;
        IR2_OPND temp = latxs_ra_alloc_ftemp();
        IR2_OPND src = latxs_ra_alloc_xmm(s1);

        if (option_enable_lasx) {
            latxs_append_ir2_opnd3(LISA_XVOR_V, &temp, &src, &src);
        } else {
            latxs_append_ir2_opnd3(LISA_VOR_V, &temp, &src, &src);
        }
        assert(ir1_opnd_size(opnd1) == 128);
        latxs_load_freg128_from_ir1_mem(&src, opnd1);

        latxs_tr_gen_call_to_helper_sha256rnds2((ADDR)helper_sha256rnds2, d, d, s1,
                HELPER_SHA256RNDS2);

        if (option_enable_lasx) {
            latxs_append_ir2_opnd3(LISA_XVOR_V, &src, &temp, &temp);
        } else {
            latxs_append_ir2_opnd3(LISA_VOR_V, &src, &temp, &temp);
        }
    }
    /* TODO: need to check */
    return true;
}
