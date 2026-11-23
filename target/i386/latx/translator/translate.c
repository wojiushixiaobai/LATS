/*
 * SPDX-FileCopyrightText: 2024-2026 LATS Project Authors
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "lsenv.h"
#include "reg-alloc.h"
#include "flag-lbt.h"
#include <string.h>
#include "latx-config.h"
#include "latx-options.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "latx-ras.h"
#include "sys-aot.h"
#include "fpu/softfloat.h"
#include "hamt-dmw.h"
#include "latx-native-recover.h"
#include "translate.h"
#include "latx-misc.h"
#include "latx-static-codes.h"
#include "latx-sys-inst-ptn.h"
#include "latx-sys-tr-sse41.h"
#include "latx-sys-tr-sse42.h"
#include "latx-sys-tr-ssse3.h"
#include "tcg/tcg-internal.h"
#include "trace.h"

bool ir1_need_calculate_cf(IR1_INST *ir1)
{
    return (ir1_is_cf_def(ir1) &&
           BITS_ARE_CLEAR(lsenv->tr_data->curr_ir1_skipped_eflags,
                          1 << CF_USEDEF_BIT_INDEX)) ||
                          ir1_opcode(ir1) == X86_INS_LZCNT;
}

bool ir1_need_calculate_pf(IR1_INST *ir1)
{
    return ir1_is_pf_def(ir1) &&
           BITS_ARE_CLEAR(lsenv->tr_data->curr_ir1_skipped_eflags,
                          1 << PF_USEDEF_BIT_INDEX);
}

bool ir1_need_calculate_af(IR1_INST *ir1)
{
    return ir1_is_af_def(ir1) &&
           BITS_ARE_CLEAR(lsenv->tr_data->curr_ir1_skipped_eflags,
                          1 << AF_USEDEF_BIT_INDEX);
}

bool ir1_need_calculate_zf(IR1_INST *ir1)
{
    return ir1_is_zf_def(ir1) &&
           BITS_ARE_CLEAR(lsenv->tr_data->curr_ir1_skipped_eflags,
                          1 << ZF_USEDEF_BIT_INDEX);
}

bool ir1_need_calculate_sf(IR1_INST *ir1)
{
    return ir1_is_sf_def(ir1) &&
           BITS_ARE_CLEAR(lsenv->tr_data->curr_ir1_skipped_eflags,
                          1 << SF_USEDEF_BIT_INDEX);
}

bool ir1_need_calculate_of(IR1_INST *ir1)
{
    return ir1_is_of_def(ir1) &&
           BITS_ARE_CLEAR(lsenv->tr_data->curr_ir1_skipped_eflags,
                          1 << OF_USEDEF_BIT_INDEX);
}

bool ir1_need_calculate_any_flag(IR1_INST *ir1)
{
    return (ir1_get_eflag_def(ir1) &
            ~(lsenv->tr_data->curr_ir1_skipped_eflags)) != 0;
}

static bool latxs_translate_to_null(IR1_INST *pir1)
{
    return true;
}

#define DEF_TR_FUNC(opcode, func) [X86_INS_##opcode] = latxs_translate_##func

int idx_start = __LINE__ + 5;
static bool (*translate_functions[])(IR1_INST *) = {
    DEF_TR_FUNC(INVALID,    invalid),

    DEF_TR_FUNC(AAA,    aaa),
    DEF_TR_FUNC(AAD,    aad),
    DEF_TR_FUNC(AAM,    aam),
    DEF_TR_FUNC(AAS,    aas),
    DEF_TR_FUNC(DAA,    daa),
    DEF_TR_FUNC(DAS,    das),

    DEF_TR_FUNC(ADC,    adc),
    DEF_TR_FUNC(SBB,    sbb),

    DEF_TR_FUNC(INC,    inc),
    DEF_TR_FUNC(DEC,    dec),

    DEF_TR_FUNC(ADD,    add),
    DEF_TR_FUNC(ADDPD,  addpd),
    DEF_TR_FUNC(ADDPS,  addps),
    DEF_TR_FUNC(ADDSD,  addsd),
    DEF_TR_FUNC(ADDSS,  addss),

    DEF_TR_FUNC(AND,    and),
    DEF_TR_FUNC(ANDNPD, andnpd),
    DEF_TR_FUNC(ANDNPS, andnps),
    DEF_TR_FUNC(ANDPD,  andpd),
    DEF_TR_FUNC(ANDPS,  andps),

    DEF_TR_FUNC(BSF,    bsf),
    DEF_TR_FUNC(BSR,    bsr),
    DEF_TR_FUNC(BSWAP,  bswap),
    DEF_TR_FUNC(BT,     btx),
    DEF_TR_FUNC(BTC,    btx),
    DEF_TR_FUNC(BTR,    btx),
    DEF_TR_FUNC(BTS,    btx),

    DEF_TR_FUNC(JMP,    jmp),
    DEF_TR_FUNC(CALL,   call),
    DEF_TR_FUNC(RET,    ret),
    DEF_TR_FUNC(LCALL,  lcall),
    DEF_TR_FUNC(LJMP,   jmp_far),
    DEF_TR_FUNC(RETF,   retf),
    DEF_TR_FUNC(RETFQ,  retf),

    DEF_TR_FUNC(CLAC,   clac),
    DEF_TR_FUNC(STAC,   stac),

    DEF_TR_FUNC(CLC,    clc),
    DEF_TR_FUNC(STC,    stc),
    DEF_TR_FUNC(CLD,    cld),
    DEF_TR_FUNC(STD,    std),

    DEF_TR_FUNC(STI,    sti),
    DEF_TR_FUNC(CLI,    cli),

    DEF_TR_FUNC(CMC,    cmc),
    DEF_TR_FUNC(CMOVA,  cmova),
    DEF_TR_FUNC(CMOVAE, cmovae),
    DEF_TR_FUNC(CMOVB,  cmovb),
    DEF_TR_FUNC(CMOVBE, cmovbe),
    DEF_TR_FUNC(CMOVE,  cmovz),
    DEF_TR_FUNC(FCMOVE, fcmovcc),
    DEF_TR_FUNC(CMOVG,  cmovg),
    DEF_TR_FUNC(CMOVGE, cmovge),
    DEF_TR_FUNC(CMOVL,  cmovl),
    DEF_TR_FUNC(CMOVLE, cmovle),
    DEF_TR_FUNC(CMOVNE, cmovnz),
    DEF_TR_FUNC(CMOVNO, cmovno),
    DEF_TR_FUNC(CMOVNP, cmovnp),
    DEF_TR_FUNC(CMOVNS, cmovns),
    DEF_TR_FUNC(CMOVO,  cmovo),
    DEF_TR_FUNC(CMOVP,  cmovp),
    DEF_TR_FUNC(FCMOVU, fcmovcc),
    DEF_TR_FUNC(CMOVS,  cmovs),

    DEF_TR_FUNC(FABS,   fabs),
    DEF_TR_FUNC(FADD,   fadd),
    DEF_TR_FUNC(FIADD,  fiadd),
    DEF_TR_FUNC(FADDP,  faddp),
    DEF_TR_FUNC(FCHS,   fchs),
    DEF_TR_FUNC(FCMOVBE,    fcmovcc),
    DEF_TR_FUNC(FCMOVB,     fcmovcc),
    DEF_TR_FUNC(FCMOVNBE,   fcmovcc),
    DEF_TR_FUNC(FCMOVNB,    fcmovcc),
    DEF_TR_FUNC(FCMOVNE,    fcmovcc),
    DEF_TR_FUNC(FCMOVNU,    fcmovcc),
    DEF_TR_FUNC(FCOMP,  fcomp),
    DEF_TR_FUNC(FCOMIP, fcomip),
    DEF_TR_FUNC(FCOMI,  fcomi),
    DEF_TR_FUNC(FCOM,   fcom),
    DEF_TR_FUNC(FCOS,   fcos),
    DEF_TR_FUNC(FSIN,   fsin),
    DEF_TR_FUNC(FLDZ,   fldz),
    DEF_TR_FUNC(FLD1,   fld1),
    DEF_TR_FUNC(FLD,    fld),
    DEF_TR_FUNC(F2XM1,      f2xm1),
    DEF_TR_FUNC(FBLD,       fbld),
    DEF_TR_FUNC(FBSTP,      fbstp),
    DEF_TR_FUNC(FCOMPP,     fcompp),
    DEF_TR_FUNC(FDECSTP,    fdecstp),
    DEF_TR_FUNC(FFREE,      ffree),
    DEF_TR_FUNC(FICOM,      ficom),
    DEF_TR_FUNC(FICOMP,     ficomp),
    DEF_TR_FUNC(FINCSTP,    fincstp),
    DEF_TR_FUNC(FLDCW,      fldcw),
    DEF_TR_FUNC(FLDENV,     fldenv),
    DEF_TR_FUNC(FLDL2E,     fldl2e),
    DEF_TR_FUNC(FLDL2T,     fldl2t),
    DEF_TR_FUNC(FLDLG2,     fldlg2),
    DEF_TR_FUNC(FLDLN2,     fldln2),
    DEF_TR_FUNC(FLDPI,      fldpi),
    DEF_TR_FUNC(FNCLEX,     fnclex),
    DEF_TR_FUNC(FNINIT,     fninit),
    DEF_TR_FUNC(FNOP,       fnop),
    DEF_TR_FUNC(FNSTCW,     fnstcw),
    DEF_TR_FUNC(FNSTSW,     fnstsw),
    DEF_TR_FUNC(FPATAN,     fpatan),
    DEF_TR_FUNC(FPREM,      fprem),
    DEF_TR_FUNC(FPREM1,     fprem1),
    DEF_TR_FUNC(FPTAN,      fptan),
    DEF_TR_FUNC(FRNDINT,    frndint),
    DEF_TR_FUNC(FRSTOR,     frstor),
    DEF_TR_FUNC(FNSAVE,     fnsave),
    DEF_TR_FUNC(FSCALE,     fscale),
    DEF_TR_FUNC(FSETPM,     fsetpm),
    DEF_TR_FUNC(FSINCOS,    fsincos),
    DEF_TR_FUNC(FNSTENV,    fnstenv),
    DEF_TR_FUNC(FXAM,       fxam),
    DEF_TR_FUNC(FXRSTOR,    fxrstor),
    DEF_TR_FUNC(FXRSTOR64,  fxrstor),
    DEF_TR_FUNC(FXSAVE,     fxsave),
    DEF_TR_FUNC(FXSAVE64,   fxsave),
    DEF_TR_FUNC(FXTRACT,    fxtract),
    DEF_TR_FUNC(FYL2X,      fyl2x),
    DEF_TR_FUNC(FYL2XP1,    fyl2xp1),
    DEF_TR_FUNC(FILD,       fild),
    DEF_TR_FUNC(FDIVR,      fdivr),
    DEF_TR_FUNC(FIDIVR,     fidivr),
    DEF_TR_FUNC(FDIVRP,     fdivrp),
    DEF_TR_FUNC(FDIV,       fdiv),
    DEF_TR_FUNC(FIDIV,      fidiv),
    DEF_TR_FUNC(FDIVP,      fdivp),
    DEF_TR_FUNC(FMUL,       fmul),
    DEF_TR_FUNC(FIMUL,      fimul),
    DEF_TR_FUNC(FMULP,      fmulp),
    DEF_TR_FUNC(FUCOMIP,    fucomip),
    DEF_TR_FUNC(FUCOMI,     fucomi),
    DEF_TR_FUNC(FUCOMPP,    fucompp),
    DEF_TR_FUNC(FUCOMP,     fucomp),
    DEF_TR_FUNC(FUCOM,      fucom),
    DEF_TR_FUNC(FSQRT,      fsqrt),
    DEF_TR_FUNC(FST,        fst),
    DEF_TR_FUNC(FSTP,       fstp),
    DEF_TR_FUNC(FSTPNCE,    fstp),
    DEF_TR_FUNC(FXCH,       fxch),
    DEF_TR_FUNC(FSUBR,      fsubr),
    DEF_TR_FUNC(FISUBR,     fisubr),
    DEF_TR_FUNC(FSUBRP,     fsubrp),
    DEF_TR_FUNC(FSUB,       fsub),
    DEF_TR_FUNC(FISUB,      fisub),
    DEF_TR_FUNC(FSUBP,      fsubp),
    DEF_TR_FUNC(FTST,       ftst),
    DEF_TR_FUNC(PAUSE,  pause),

    DEF_TR_FUNC(CMP,        cmp),
    DEF_TR_FUNC(COMISD,     comisd),
    DEF_TR_FUNC(COMISS,     comiss),
    DEF_TR_FUNC(CMPXCHG,    cmpxchg),
    DEF_TR_FUNC(CMPXCHG8B,  cmpxchg8b),
    DEF_TR_FUNC(CMPXCHG16B, cmpxchg16b),

    DEF_TR_FUNC(CPUID,  cpuid),

    DEF_TR_FUNC(CBW,    cbw),
    DEF_TR_FUNC(CDQ,    cdq),
    DEF_TR_FUNC(CDQE,   cdqe),
    DEF_TR_FUNC(CQO,    cqo),
    DEF_TR_FUNC(CWD,    cwd),
    DEF_TR_FUNC(CWDE,   cwde),

    DEF_TR_FUNC(CVTDQ2PD,   cvtdq2pd),
    DEF_TR_FUNC(CVTDQ2PS,   cvtdq2ps),
    DEF_TR_FUNC(CVTPD2DQ,   cvtpd2dq),
    DEF_TR_FUNC(CVTPD2PS,   cvtpd2ps),
    DEF_TR_FUNC(CVTPS2DQ,   cvtps2dq),
    DEF_TR_FUNC(CVTPS2PD,   cvtps2pd),
    DEF_TR_FUNC(CVTSD2SI,   cvtsd2si),
    DEF_TR_FUNC(CVTSD2SS,   cvtsd2ss),
    DEF_TR_FUNC(CVTSI2SD,   cvtsi2sd),
    DEF_TR_FUNC(CVTSI2SS,   cvtsi2ss),
    DEF_TR_FUNC(CVTSS2SD,   cvtss2sd),
    DEF_TR_FUNC(CVTSS2SI,   cvtss2si),
    DEF_TR_FUNC(CVTTPD2DQ,  cvttpd2dq),
    DEF_TR_FUNC(CVTTPS2DQ,  cvttps2dq),
    DEF_TR_FUNC(CVTTSD2SI,  cvttsd2si),
    DEF_TR_FUNC(CVTTSS2SI,  cvttss2si),
    DEF_TR_FUNC(CVTPD2PI,   cvtpd2pi),
    DEF_TR_FUNC(CVTPI2PD,   cvtpi2pd),
    DEF_TR_FUNC(CVTPI2PS,   cvtpi2ps),
    DEF_TR_FUNC(CVTPS2PI,   cvtps2pi),
    DEF_TR_FUNC(CVTTPD2PI,  cvttpd2pi),
    DEF_TR_FUNC(CVTTPS2PI,  cvttps2pi),

    DEF_TR_FUNC(ENTER,  enter),
    DEF_TR_FUNC(LEAVE,  leave),

    DEF_TR_FUNC(DIV,    div),
    DEF_TR_FUNC(DIVPD,  divpd),
    DEF_TR_FUNC(DIVPS,  divps),
    DEF_TR_FUNC(DIVSD,  divsd),
    DEF_TR_FUNC(DIVSS,  divss),
    DEF_TR_FUNC(IDIV,   idiv),
    DEF_TR_FUNC(MUL,    mul),
    DEF_TR_FUNC(MULPD,  mulpd),
    DEF_TR_FUNC(MULPS,  mulps),
    DEF_TR_FUNC(MULSD,  mulsd),
    DEF_TR_FUNC(MULSS,  mulss),
    DEF_TR_FUNC(IMUL,   imul),

    DEF_TR_FUNC(IRET,   iret),
    DEF_TR_FUNC(IRETD,  iret),
    DEF_TR_FUNC(IRETQ,  iret),

    DEF_TR_FUNC(IN,     in),
    DEF_TR_FUNC(INSB,   ins),
    DEF_TR_FUNC(INSD,   ins),
    DEF_TR_FUNC(INSW,   ins),
    DEF_TR_FUNC(OUT,    out),
    DEF_TR_FUNC(OUTSB,  outs),
    DEF_TR_FUNC(OUTSD,  outs),
    DEF_TR_FUNC(OUTSW,  outs),

    DEF_TR_FUNC(INT,    int),
    DEF_TR_FUNC(INT1,   int1),
    DEF_TR_FUNC(INT3,   int_3),
    DEF_TR_FUNC(INTO,   into),
    DEF_TR_FUNC(FISTTP, fisttp),
    DEF_TR_FUNC(FIST,   fist),
    DEF_TR_FUNC(FISTP,  fistp),
    DEF_TR_FUNC(UCOMISD,    ucomisd),
    DEF_TR_FUNC(UCOMISS,    ucomiss),

    DEF_TR_FUNC(JAE,    jae),
    DEF_TR_FUNC(JA,     ja),
    DEF_TR_FUNC(JBE,    jbe),
    DEF_TR_FUNC(JB,     jb),
    DEF_TR_FUNC(JCXZ,   jcxz),
    DEF_TR_FUNC(JECXZ,  jecxz),
    DEF_TR_FUNC(JE,     jz),
    DEF_TR_FUNC(JGE,    jge),
    DEF_TR_FUNC(JG,     jg),
    DEF_TR_FUNC(JLE,    jle),
    DEF_TR_FUNC(JL,     jl),
    DEF_TR_FUNC(JNE,    jnz),
    DEF_TR_FUNC(JNO,    jno),
    DEF_TR_FUNC(JNP,    jnp),
    DEF_TR_FUNC(JNS,    jns),
    DEF_TR_FUNC(JO,     jo),
    DEF_TR_FUNC(JP,     jp),
    DEF_TR_FUNC(JRCXZ,  jrcxz),
    DEF_TR_FUNC(JS,     js),
    DEF_TR_FUNC(LOOP,   loop),
    DEF_TR_FUNC(LOOPE,  loopz),
    DEF_TR_FUNC(LOOPNE, loopnz),

    DEF_TR_FUNC(LEA,    lea),

    DEF_TR_FUNC(LDMXCSR,    ldmxcsr),
    DEF_TR_FUNC(STMXCSR,    stmxcsr),

    DEF_TR_FUNC(LFENCE, lfence),
    DEF_TR_FUNC(SFENCE, sfence),
    DEF_TR_FUNC(MFENCE, mfence),

    DEF_TR_FUNC(LDS,    lds),
    DEF_TR_FUNC(LES,    les),
    DEF_TR_FUNC(LFS,    lfs),
    DEF_TR_FUNC(LGS,    lgs),
    DEF_TR_FUNC(LSS,    lss),

    DEF_TR_FUNC(LAHF,   lahf),
    DEF_TR_FUNC(SAHF,   sahf),

    DEF_TR_FUNC(EMMS,   emms),
    DEF_TR_FUNC(XADD,   xadd),

    DEF_TR_FUNC(OR,     or),
    DEF_TR_FUNC(ORPD,   orpd),
    DEF_TR_FUNC(ORPS,   orps),

    DEF_TR_FUNC(SUB,    sub),
    DEF_TR_FUNC(SUBSD,  subsd),
    DEF_TR_FUNC(SUBSS,  subss),
    DEF_TR_FUNC(SUBPD,  subpd),
    DEF_TR_FUNC(SUBPS,  subps),

    DEF_TR_FUNC(XOR,    xor),
    DEF_TR_FUNC(XORPD,  xorpd),
    DEF_TR_FUNC(XORPS,  xorps),

    DEF_TR_FUNC(MASKMOVDQU, maskmovdqu),
    DEF_TR_FUNC(MASKMOVQ,   maskmovq),

    DEF_TR_FUNC(MAXPD,  maxpd),
    DEF_TR_FUNC(MAXPS,  maxps),
    DEF_TR_FUNC(MAXSD,  maxsd),
    DEF_TR_FUNC(MAXSS,  maxss),
    DEF_TR_FUNC(MINPD,  minpd),
    DEF_TR_FUNC(MINPS,  minps),
    DEF_TR_FUNC(MINSD,  minsd),
    DEF_TR_FUNC(MINSS,  minss),

    DEF_TR_FUNC(HADDPD,   haddpd),
    DEF_TR_FUNC(HADDPS,   haddps),

    DEF_TR_FUNC(PACKSSDW,   packssdw),
    DEF_TR_FUNC(PACKSSWB,   packsswb),
    DEF_TR_FUNC(PACKUSWB,   packuswb),
    DEF_TR_FUNC(PADDB,      paddb),
    DEF_TR_FUNC(PADDD,      paddd),
    DEF_TR_FUNC(PADDQ,      paddq),
    DEF_TR_FUNC(PADDSB,     paddsb),
    DEF_TR_FUNC(PADDSW,     paddsw),
    DEF_TR_FUNC(PADDUSB,    paddusb),
    DEF_TR_FUNC(PADDUSW,    paddusw),
    DEF_TR_FUNC(PADDW,      paddw),
    DEF_TR_FUNC(PANDN,      pandn),
    DEF_TR_FUNC(PAND,       pand),
    DEF_TR_FUNC(PAVGB,      pavgb),
    DEF_TR_FUNC(PAVGW,      pavgw),
    DEF_TR_FUNC(PCMPEQB,    pcmpeqb),
    DEF_TR_FUNC(PCMPEQD,    pcmpeqd),
    DEF_TR_FUNC(PCMPEQW,    pcmpeqw),
    DEF_TR_FUNC(PCMPGTB,    pcmpgtb),
    DEF_TR_FUNC(PCMPGTD,    pcmpgtd),
    DEF_TR_FUNC(PCMPGTW,    pcmpgtw),
    DEF_TR_FUNC(PEXTRW,     pextrw),
    DEF_TR_FUNC(PINSRW,     pinsrw),
    DEF_TR_FUNC(PMADDWD,    pmaddwd),
    DEF_TR_FUNC(PMAXSW,     pmaxsw),
    DEF_TR_FUNC(PMAXUB,     pmaxub),
    DEF_TR_FUNC(PMINSW,     pminsw),
    DEF_TR_FUNC(PMINUB,     pminub),
    DEF_TR_FUNC(PMOVMSKB,   pmovmskb),
    DEF_TR_FUNC(PMULHUW,    pmulhuw),
    DEF_TR_FUNC(PMULHW,     pmulhw),
    DEF_TR_FUNC(PMULLW,     pmullw),
    DEF_TR_FUNC(PMULUDQ,    pmuludq),
    DEF_TR_FUNC(POR,        por),
    DEF_TR_FUNC(PSADBW,     psadbw),
    DEF_TR_FUNC(PSHUFW,     pshufw),
    DEF_TR_FUNC(PSLLD,      pslld),
    DEF_TR_FUNC(PSLLQ,      psllq),
    DEF_TR_FUNC(PSLLW,      psllw),
    DEF_TR_FUNC(PSRAD,      psrad),
    DEF_TR_FUNC(PSRAW,      psraw),
    DEF_TR_FUNC(PSRLD,      psrld),
    DEF_TR_FUNC(PSRLQ,      psrlq),
    DEF_TR_FUNC(PSRLW,      psrlw),
    DEF_TR_FUNC(PSUBB,      psubb),
    DEF_TR_FUNC(PSUBD,      psubd),
    DEF_TR_FUNC(PSUBQ,      psubq),
    DEF_TR_FUNC(PSUBSB,     psubsb),
    DEF_TR_FUNC(PSUBSW,     psubsw),
    DEF_TR_FUNC(PSUBUSB,    psubusb),
    DEF_TR_FUNC(PSUBUSW,    psubusw),
    DEF_TR_FUNC(PSUBW,      psubw),
    DEF_TR_FUNC(PUNPCKHBW,  punpckhbw),
    DEF_TR_FUNC(PUNPCKHDQ,  punpckhdq),
    DEF_TR_FUNC(PUNPCKHWD,  punpckhwd),
    DEF_TR_FUNC(PUNPCKLBW,  punpcklbw),
    DEF_TR_FUNC(PUNPCKLDQ,  punpckldq),
    DEF_TR_FUNC(PUNPCKLWD,  punpcklwd),
    DEF_TR_FUNC(PXOR,       pxor),
    DEF_TR_FUNC(PSHUFD,     pshufd),
    DEF_TR_FUNC(PSHUFHW,    pshufhw),
    DEF_TR_FUNC(PSHUFLW,    pshuflw),
    DEF_TR_FUNC(PSLLDQ,     pslldq),
    DEF_TR_FUNC(PSRLDQ,     psrldq),
    DEF_TR_FUNC(PUNPCKHQDQ, punpckhqdq),
    DEF_TR_FUNC(PUNPCKLQDQ, punpcklqdq),
    DEF_TR_FUNC(RCPPS,      rcpps),
    DEF_TR_FUNC(RCPSS,      rcpss),
    DEF_TR_FUNC(RSQRTPS,    rsqrtps),
    DEF_TR_FUNC(RSQRTSS,    rsqrtss),
    DEF_TR_FUNC(SHUFPD,     shufpd),
    DEF_TR_FUNC(SHUFPS,     shufps),
    DEF_TR_FUNC(SQRTPD,     sqrtpd),
    DEF_TR_FUNC(SQRTPS,     sqrtps),
    DEF_TR_FUNC(SQRTSD,     sqrtsd),
    DEF_TR_FUNC(SQRTSS,     sqrtss),
    DEF_TR_FUNC(UNPCKHPD,   unpckhpd),
    DEF_TR_FUNC(UNPCKHPS,   unpckhps),
    DEF_TR_FUNC(UNPCKLPD,   unpcklpd),
    DEF_TR_FUNC(UNPCKLPS,   unpcklps),
    DEF_TR_FUNC(CMPEQSS,    cmpeqss),
    DEF_TR_FUNC(CMPEQSD,    cmpeqsd),
    DEF_TR_FUNC(CMPEQPS,    cmpeqps),
    DEF_TR_FUNC(CMPEQPD,    cmpeqpd),
    DEF_TR_FUNC(CMPLTSS,    cmpltss),
    DEF_TR_FUNC(CMPLTSD,    cmpltsd),
    DEF_TR_FUNC(CMPLTPS,    cmpltps),
    DEF_TR_FUNC(CMPLTPD,    cmpltpd),
    DEF_TR_FUNC(CMPLESS,    cmpless),
    DEF_TR_FUNC(CMPLESD,    cmplesd),
    DEF_TR_FUNC(CMPLEPS,    cmpleps),
    DEF_TR_FUNC(CMPLEPD,    cmplepd),
    DEF_TR_FUNC(CMPNEQSS,   cmpneqss),
    DEF_TR_FUNC(CMPNEQSD,   cmpneqsd),
    DEF_TR_FUNC(CMPNEQPS,   cmpneqps),
    DEF_TR_FUNC(CMPNEQPD,   cmpneqpd),
    DEF_TR_FUNC(CMPNLTSS,   cmpnltss),
    DEF_TR_FUNC(CMPNLTSD,   cmpnltsd),
    DEF_TR_FUNC(CMPNLTPS,   cmpnltps),
    DEF_TR_FUNC(CMPNLTPD,   cmpnltpd),
    DEF_TR_FUNC(CMPNLESS,   cmpnless),
    DEF_TR_FUNC(CMPNLESD,   cmpnlesd),
    DEF_TR_FUNC(CMPNLEPS,   cmpnleps),
    DEF_TR_FUNC(CMPNLEPD,   cmpnlepd),
    DEF_TR_FUNC(CMPORDSS,   cmpordss),
    DEF_TR_FUNC(CMPORDSD,   cmpordsd),
    DEF_TR_FUNC(CMPORDPS,   cmpordps),
    DEF_TR_FUNC(CMPORDPD,   cmpordpd),
    DEF_TR_FUNC(CMPUNORDSS, cmpunordss),
    DEF_TR_FUNC(CMPUNORDSD, cmpunordsd),
    DEF_TR_FUNC(CMPUNORDPS, cmpunordps),
    DEF_TR_FUNC(CMPUNORDPD, cmpunordpd),

    DEF_TR_FUNC(MOV,        mov),
    DEF_TR_FUNC(MOVABS,     mov),
    DEF_TR_FUNC(MOVDQA,     movdqa),
    DEF_TR_FUNC(MOVDQU,     movdqu),
    DEF_TR_FUNC(MOVHPD,     movhpd),
    DEF_TR_FUNC(MOVHPS,     movhps),
    DEF_TR_FUNC(MOVLPD,     movlpd),
    DEF_TR_FUNC(MOVLPS,     movlps),
    DEF_TR_FUNC(MOVHLPS,    movhlps),
    DEF_TR_FUNC(MOVLHPS,    movlhps),
    DEF_TR_FUNC(MOVMSKPD,   movmskpd),
    DEF_TR_FUNC(MOVMSKPS,   movmskps),
    DEF_TR_FUNC(MOVNTDQ,    movntdq),
    DEF_TR_FUNC(MOVNTI,     movnti),
    DEF_TR_FUNC(MOVNTPD,    movntpd),
    DEF_TR_FUNC(MOVNTPS,    movntps),
    DEF_TR_FUNC(MOVSS,      movss),
    DEF_TR_FUNC(MOVSXD,     movsxd),
    DEF_TR_FUNC(MOVUPD,     movupd),
    DEF_TR_FUNC(MOVUPS,     movups),
    DEF_TR_FUNC(MOVZX,      movzx),
    DEF_TR_FUNC(MOVSX,      movsx),
    DEF_TR_FUNC(MOVD,       movd),
    DEF_TR_FUNC(MOVDQ2Q,    movdq2q),
    DEF_TR_FUNC(MOVNTQ,     movntq),
    DEF_TR_FUNC(MOVQ2DQ,    movq2dq),
    DEF_TR_FUNC(MOVQ,       movq),
    DEF_TR_FUNC(MOVAPD,     movapd),
    DEF_TR_FUNC(MOVAPS,     movaps),

    DEF_TR_FUNC(NEG,    neg),
    DEF_TR_FUNC(NOT,    not),
    DEF_TR_FUNC(NOP,    nop),

    DEF_TR_FUNC(POP,    pop),
    DEF_TR_FUNC(POPAW,  popaw),
    DEF_TR_FUNC(POPAL,  popal),
    DEF_TR_FUNC(POPF,   popf),
    DEF_TR_FUNC(POPFD,  popf),
    DEF_TR_FUNC(POPFQ,  popf),
    DEF_TR_FUNC(PUSH,   push),
    DEF_TR_FUNC(PUSHAW, pushaw),
    DEF_TR_FUNC(PUSHAL, pushal),
    DEF_TR_FUNC(PUSHF,  pushf),
    DEF_TR_FUNC(PUSHFD, pushf),
    DEF_TR_FUNC(PUSHFQ, pushf),

    DEF_TR_FUNC(PREFETCH,       prefetch),
    DEF_TR_FUNC(PREFETCHNTA,    prefetchnta),
    DEF_TR_FUNC(PREFETCHT0,     prefetcht0),
    DEF_TR_FUNC(PREFETCHT1,     prefetcht1),
    DEF_TR_FUNC(PREFETCHT2,     prefetcht2),
    DEF_TR_FUNC(PREFETCHW,      prefetchw),

    DEF_TR_FUNC(RCL,    rcl),
    DEF_TR_FUNC(RCR,    rcr),
    DEF_TR_FUNC(ROL,    rol),
    DEF_TR_FUNC(ROR,    ror),

    DEF_TR_FUNC(SAL,    sal),
    DEF_TR_FUNC(SAR,    sar),
    DEF_TR_FUNC(SARX,   sarx),
    DEF_TR_FUNC(SHL,    shl),
    DEF_TR_FUNC(SHLD,   shld),
    DEF_TR_FUNC(SHLX,   shlx),
    DEF_TR_FUNC(SHR,    shr),
    DEF_TR_FUNC(SHRD,   shrd),
    DEF_TR_FUNC(SHRX,   shrx),

    DEF_TR_FUNC(SCASB,  scas),
    DEF_TR_FUNC(SCASD,  scas),
    DEF_TR_FUNC(SCASQ,  scas),
    DEF_TR_FUNC(SCASW,  scas),
    DEF_TR_FUNC(STOSB,  stos),
    DEF_TR_FUNC(STOSD,  stos),
    DEF_TR_FUNC(STOSQ,  stos),
    DEF_TR_FUNC(STOSW,  stos),
    DEF_TR_FUNC(LODSB,  lods),
    DEF_TR_FUNC(LODSD,  lods),
    DEF_TR_FUNC(LODSQ,  lods),
    DEF_TR_FUNC(LODSW,  lods),
    DEF_TR_FUNC(MOVSB,  movs),
    DEF_TR_FUNC(MOVSQ,  movs),
    DEF_TR_FUNC(MOVSW,  movs),
    DEF_TR_FUNC(CMPSB,  cmps),
    DEF_TR_FUNC(CMPSD,  cmps),
    DEF_TR_FUNC(CMPSQ,  cmps),
    DEF_TR_FUNC(CMPSW,  cmps),

    DEF_TR_FUNC(SETAE,  setcc),
    DEF_TR_FUNC(SETA,   setcc),
    DEF_TR_FUNC(SETBE,  setcc),
    DEF_TR_FUNC(SETB,   setcc),
    DEF_TR_FUNC(SETE,   setcc),
    DEF_TR_FUNC(SETGE,  setcc),
    DEF_TR_FUNC(SETG,   setcc),
    DEF_TR_FUNC(SETLE,  setcc),
    DEF_TR_FUNC(SETL,   setcc),
    DEF_TR_FUNC(SETNE,  setcc),
    DEF_TR_FUNC(SETNO,  setcc),
    DEF_TR_FUNC(SETNP,  setcc),
    DEF_TR_FUNC(SETNS,  setcc),
    DEF_TR_FUNC(SETO,   setcc),
    DEF_TR_FUNC(SETP,   setcc),
    DEF_TR_FUNC(SETS,   setcc),

    DEF_TR_FUNC(SYSCALL,    syscall),
    DEF_TR_FUNC(SYSENTER,   sysenter),
    DEF_TR_FUNC(SYSEXIT,    sysexit),
    DEF_TR_FUNC(SYSRET,     sysret),

    DEF_TR_FUNC(TEST,   test),
    DEF_TR_FUNC(TZCNT,  bsf),
    DEF_TR_FUNC(WAIT,   wait),
    DEF_TR_FUNC(XCHG,   xchg),
    DEF_TR_FUNC(XLATB,   xlat),

    DEF_TR_FUNC(XSAVE,      xsave),
    DEF_TR_FUNC(XSAVEOPT,   xsaveopt),
    DEF_TR_FUNC(XRSTOR,     xrstor),

    DEF_TR_FUNC(SWAPGS, swapgs),

    DEF_TR_FUNC(UD2,    ud2),
    DEF_TR_FUNC(UD0,    ud0),

    DEF_TR_FUNC(ENDBR32,   to_null),
    DEF_TR_FUNC(ENDBR64,   to_null),

    /* privilege instructions */
    DEF_TR_FUNC(LLDT,   lldt),
    DEF_TR_FUNC(SLDT,   sldt),
    DEF_TR_FUNC(LGDT,   lgdt),
    DEF_TR_FUNC(SGDT,   sgdt),
    DEF_TR_FUNC(LTR,    ltr),
    DEF_TR_FUNC(STR,    str),
    DEF_TR_FUNC(LIDT,   lidt),
    DEF_TR_FUNC(SIDT,   sidt),
    DEF_TR_FUNC(LMSW,   lmsw),
    DEF_TR_FUNC(SMSW,   smsw),
    DEF_TR_FUNC(CLTS,   clts),
    DEF_TR_FUNC(ARPL, arpl),
    /* LAR */
    DEF_TR_FUNC(LSL,    lsl),
    DEF_TR_FUNC(LAR,    lar),
    DEF_TR_FUNC(VERR,   verr),
    DEF_TR_FUNC(VERW,   verw),
    DEF_TR_FUNC(INVD,   invd),
    DEF_TR_FUNC(WBINVD, wbinvd),
    DEF_TR_FUNC(INVPCID, invpcid),
    DEF_TR_FUNC(INVLPG, invlpg),
    DEF_TR_FUNC(INVLPGA,    invlpga),
    DEF_TR_FUNC(HLT,    hlt),
    DEF_TR_FUNC(RSM,    rsm),
    DEF_TR_FUNC(RDMSR,  rdmsr),
    DEF_TR_FUNC(WRMSR,  wrmsr),
    DEF_TR_FUNC(RDPMC,  rdpmc),
    DEF_TR_FUNC(RDTSC,  rdtsc),
    DEF_TR_FUNC(RDTSCP, rdtscp),

    DEF_TR_FUNC(XGETBV, xgetbv),
    DEF_TR_FUNC(XSETBV, xsetbv),

    DEF_TR_FUNC(EXTRACTPS, extractps),
    /*DEF_TR_FUNC(EXTRQ, extrq),*/
    /*DEF_TR_FUNC(FEMMS, femms),*/
    DEF_TR_FUNC(FFREEP, ffreep),
    /*DEF_TR_FUNC(VMOVAPD, vmovapd),*/
    /*DEF_TR_FUNC(VMOVAPS, vmovaps),*/
    /*DEF_TR_FUNC(GETSEC, getsec),*/
    DEF_TR_FUNC(HADDPD, haddpd),
    DEF_TR_FUNC(HADDPS, haddps),
    DEF_TR_FUNC(HSUBPD, hsubpd),
    DEF_TR_FUNC(HSUBPS, hsubps),
    DEF_TR_FUNC(INSERTPS, insertps),
    /*DEF_TR_FUNC(INSERTQ, insertq),*/
    /*DEF_TR_FUNC(INVEPT, invept),*/
    DEF_TR_FUNC(RDRAND, rdrand),
    DEF_TR_FUNC(RDSEED, rdrand),

    DEF_TR_FUNC(CLFLUSH, clflush),

    /* ssse3 */
    DEF_TR_FUNC(PSHUFB,  pshufb),
    DEF_TR_FUNC(PALIGNR, palignr),
    DEF_TR_FUNC(PSIGNB,  psignb),
    DEF_TR_FUNC(PSIGNW,  psignw),
    DEF_TR_FUNC(PSIGND,  psignd),
    DEF_TR_FUNC(PHADDD,   phaddd),
    DEF_TR_FUNC(PHADDW,   phaddw),
    DEF_TR_FUNC(PHADDSW,  phaddsw),
    DEF_TR_FUNC(PHSUBD,   phsubd),
    DEF_TR_FUNC(PHSUBW,   phsubw),
    DEF_TR_FUNC(PHSUBSW,  phsubsw),
    DEF_TR_FUNC(MOVSHDUP, movshdup),
    DEF_TR_FUNC(MOVSLDUP, movsldup),
    DEF_TR_FUNC(MOVDDUP,  movddup),
    DEF_TR_FUNC(PABSB      ,pabsb),
    DEF_TR_FUNC(PABSW      ,pabsw),
    DEF_TR_FUNC(PABSD      ,pabsd),
    DEF_TR_FUNC(PMADDUBSW  ,pmaddubsw),
    DEF_TR_FUNC(ADDSUBPS   ,addsubps),
    DEF_TR_FUNC(ADDSUBPD   ,addsubpd),
    DEF_TR_FUNC(PMULHRSW   ,pmulhrsw),


    /* sse41 */
    DEF_TR_FUNC(PTEST, ptest),
    DEF_TR_FUNC(PEXTRQ, pextrq),
    DEF_TR_FUNC(PEXTRD, pextrd),
    DEF_TR_FUNC(PEXTRB, pextrb),
    DEF_TR_FUNC(PINSRB, pinsrb),
    DEF_TR_FUNC(PINSRD, pinsrd),
    DEF_TR_FUNC(PINSRQ, pinsrq),
    DEF_TR_FUNC(PMOVZXBQ, pmovzxbq),
    DEF_TR_FUNC(PMOVZXDQ, pmovzxdq),
    DEF_TR_FUNC(PMOVZXBW, pmovzxbw),
    DEF_TR_FUNC(PMOVZXBD, pmovzxbd),
    DEF_TR_FUNC(PMOVZXWD, pmovzxwd),
    DEF_TR_FUNC(PMOVZXWQ, pmovzxwq),
    DEF_TR_FUNC(PMOVSXBW, pmovsxbw),
    DEF_TR_FUNC(PMOVSXDQ, pmovsxdq),
    DEF_TR_FUNC(PMOVSXBD, pmovsxbd),
    DEF_TR_FUNC(PMOVSXWD, pmovsxwd),
    DEF_TR_FUNC(PMOVSXBQ, pmovsxbq),
    DEF_TR_FUNC(PMOVSXWQ, pmovsxwq),
    DEF_TR_FUNC(BLENDVPD, blendvpd),
    DEF_TR_FUNC(BLENDVPS, blendvps),
    DEF_TR_FUNC(ROUNDSS, roundss),
    DEF_TR_FUNC(ROUNDPS, roundps),
    DEF_TR_FUNC(ROUNDSD, roundsd),
    DEF_TR_FUNC(ROUNDPD, roundpd),
    DEF_TR_FUNC(PCMPEQQ, pcmpeqq),
    DEF_TR_FUNC(PBLENDW, pblendw),
    DEF_TR_FUNC(PBLENDVB, pblendvb),
    DEF_TR_FUNC(INSERTPS, insertps),
    DEF_TR_FUNC(EXTRACTPS, extractps),
    DEF_TR_FUNC(PMINUD, pminud),
    DEF_TR_FUNC(PMINSD, pminsd),
    DEF_TR_FUNC(PMINUW, pminuw),
    DEF_TR_FUNC(PMINSB, pminsb),
    DEF_TR_FUNC(PMULLD, pmulld),
    DEF_TR_FUNC(PMULDQ, pmuldq),
    DEF_TR_FUNC(PMAXSD, pmaxsd),
    DEF_TR_FUNC(PMAXUD, pmaxud),
    DEF_TR_FUNC(PMAXSB, pmaxsb),
    DEF_TR_FUNC(PMAXUW, pmaxuw),
    DEF_TR_FUNC(PACKUSDW, packusdw),
    DEF_TR_FUNC(DPPD, dppd),
    DEF_TR_FUNC(DPPS, dpps),
    DEF_TR_FUNC(MOVNTDQA, movntdqa),
    DEF_TR_FUNC(BLENDPD, blendpd),
    DEF_TR_FUNC(BLENDPS, blendps),

    /* sse42 */
    DEF_TR_FUNC(PCMPGTQ, pcmpgtq),
    DEF_TR_FUNC(PCMPISTRI, pcmpistri),
    DEF_TR_FUNC(PCMPESTRI, pcmpestri),
    DEF_TR_FUNC(PCMPISTRM, pcmpistrm),
    DEF_TR_FUNC(PCMPESTRM, pcmpestrm),

    DEF_TR_FUNC(CRC32, crc32),
    DEF_TR_FUNC(POPCNT, popcnt),

    DEF_TR_FUNC(ADCX, adcx),
    DEF_TR_FUNC(ADOX, adox),
    DEF_TR_FUNC(AESDEC, aesdec),
    DEF_TR_FUNC(AESDECLAST, aesdeclast),
    DEF_TR_FUNC(AESENC, aesenc),
    DEF_TR_FUNC(AESENCLAST, aesenclast),
    DEF_TR_FUNC(AESIMC, aesimc),
    DEF_TR_FUNC(AESKEYGENASSIST, aeskeygenassist),
    DEF_TR_FUNC(ANDN, andn),
//    DEF_TR_FUNC(ARPL, nop),
    DEF_TR_FUNC(BEXTR, bextr),
    DEF_TR_FUNC(BZHI, bzhi),
    DEF_TR_FUNC(BLSI, blsi),
    DEF_TR_FUNC(BLSMSK, blsmsk),
    DEF_TR_FUNC(BLSR, blsr),
    DEF_TR_FUNC(BOUND, nop),
    DEF_TR_FUNC(CLFLUSHOPT, clflushopt),
    DEF_TR_FUNC(CMPPD, cmppd),
    DEF_TR_FUNC(CMPPS, cmpps),
    DEF_TR_FUNC(CMPSS, cmpss),
    DEF_TR_FUNC(LDDQU, lddqu),
    DEF_TR_FUNC(LZCNT, lzcnt),
    DEF_TR_FUNC(MPSADBW, mpsadbw),
    DEF_TR_FUNC(MULX, mulx),
    DEF_TR_FUNC(MOVBE, movbe),
    DEF_TR_FUNC(PCLMULQDQ, pclmulqdq),
    DEF_TR_FUNC(PDEP, pdep),
    DEF_TR_FUNC(PEXT, pext),
    DEF_TR_FUNC(PHMINPOSUW, phminposuw),
    DEF_TR_FUNC(RORX, rorx),
    DEF_TR_FUNC(SALC, salc),
    DEF_TR_FUNC(SHA1MSG1, sha1msg1),
    DEF_TR_FUNC(SHA1MSG2, sha1msg2),
    DEF_TR_FUNC(SHA1NEXTE, sha1nexte),
    DEF_TR_FUNC(SHA1RNDS4, sha1rnds4),
    DEF_TR_FUNC(SHA256MSG1, sha256msg1),
    DEF_TR_FUNC(SHA256MSG2, sha256msg2),
    DEF_TR_FUNC(SHA256RNDS2, sha256rnds2),

    /* X86_INS_ENDING */
};

bool ir1_translate(IR1_INST *ir1)
{
#ifdef LATXS_INSTPTN_ENABLE
    if (try_translate_instptn(ir1)) {
        return true;
    }
#endif

    /* 2. call translate_xx function */
    int tr_func_idx = ir1_opcode(ir1) - X86_INS_INVALID;

    bool translation_success = false;

    // MOVSD means movsd(movs) or movsd(sse2) , diff opcode
    if (ir1_opcode(ir1) == X86_INS_MOVSD) {
        if (ir1->info->detail->x86.opcode[0] == 0xa5) {
            translation_success = latxs_translate_movs(ir1);
        } else if (ir1->info->detail->x86.opcode[0] == 0x0f) {
            translation_success = latxs_translate_movsd(ir1);
        } else {
#ifdef CONFIG_LATX_PRINT
            fprintf(stderr, "%s %s %d error : this ins not implemented\n",
                    __FILE__, __func__, __LINE__);
            ir1_opcode_dump(ir1);
#endif
            exit(-1);
        }
    } else {
        if (translate_functions[tr_func_idx] == NULL || !latxs_is_ir1_ok(ir1)) {
            if (latxs_translate_avx(ir1)) {
                return true;
            }
#ifdef CONFIG_LATX_PRINT
            fprintf(stderr, "%s %s %d error : this ins %d not implemented\n",
                    __FILE__, __func__, __LINE__, tr_func_idx);
            ir1_opcode_dump(ir1);
#endif
#ifdef CONFIG_SOFTMMU
            if (trace_event_get_state_backends(TRACE_LATX_UNSUPPORTED)) {
                uint64_t bytes;
                uint64_t pc = ir1_addr(ir1) + lsenv->tr_data->sys.cs_base;

                memcpy(&bytes, ir1->info->bytes, sizeof(bytes));
                trace_latx_unsupported(pc, ir1_opcode(ir1),
                                       latxs_ir1_inst_size(ir1), bytes);
            }
            latxs_tr_gen_excp_illegal_op_addr(ir1_addr(ir1) - lsenv->tr_data->sys.cs_base, 1);
            translation_success = true;
#else
            translation_success = false;
#endif
        } else {
            translation_success = translate_functions[tr_func_idx](ir1); /* TODO */
        }
    }

    if (!translation_success) {
        ir1_dump(ir1);
    }

    lsassertm(translation_success,
            "tr_func_idx %d translate failed, the failed ins is at line %d",
            tr_func_idx,
            (tr_func_idx + idx_start));

    return translation_success;
}

#include "qemu/cacheflush.h"

#ifdef CONFIG_LATX_TU
#define LA_REG_A0 4
#define LA_LU12I_W_A0_ZERO (0x14000000 | LA_REG_A0)
#define LA_ORI_A0_A0_ZERO  (0x03800000 | (LA_REG_A0 << 5) | LA_REG_A0)

static uint32_t latxs_tu_generation_offset_lu12i(uint32_t offset)
{
    return 0x14000000 | ((offset >> 12) << 5) | LA_REG_A0;
}

static uint32_t latxs_tu_generation_offset_ori(uint32_t offset)
{
    return 0x03800000 | ((offset & 0xfff) << 10) |
           (LA_REG_A0 << 5) | LA_REG_A0;
}

static void latxs_tu_generation_offset_patch(TranslationBlock *tb, int n,
                                              uint32_t offset)
{
    uint16_t code_offset = tb->s_data->tu_generation_offset_patch[n];

    if (code_offset == TB_JMP_OFFSET_INVALID) {
        return;
    }

    uint32_t *patch = (uint32_t *)((uintptr_t)tb->tc.ptr + code_offset);

    g_assert(offset <= INT32_MAX);
    qatomic_set(patch, latxs_tu_generation_offset_lu12i(offset));
    qatomic_set(patch + 1, latxs_tu_generation_offset_ori(offset));
    flush_idcache_range((uintptr_t)patch, (uintptr_t)patch, 8);
}

static void latxs_tu_generation_offset_link(
        TranslationBlock *tb, int n, const TranslationBlock *next_tb)
{
    uint32_t offset = TB_TU_PAGES(next_tb) ? TB_TU_PAGES(next_tb)->id : 0;

    if (tucp_fast_link_check()) {
        TB_NEXT_TU_PAGES(tb, n) = TB_TU_PAGES(next_tb);
        TB_NEXT_TU_GENERATION_OFFSET(tb, n) =
            (uint64_t)offset * tucp_generation_entry_size();
    }

    if (tucp_generation_offset()) {
        latxs_tu_generation_offset_patch(tb, n, offset);
    }
}

static void latxs_tu_generation_offset_unlink(TranslationBlock *tb, int n)
{
    if (tucp_fast_link_check()) {
        TB_NEXT_TU_PAGES(tb, n) = NULL;
        TB_NEXT_TU_GENERATION_OFFSET(tb, n) = 0;
    }

    if (tucp_generation_offset() &&
        tb->s_data->tu_generation_offset_patch[n] != TB_JMP_OFFSET_INVALID) {
        uint16_t code_offset = tb->s_data->tu_generation_offset_patch[n];
        uint32_t *patch = (uint32_t *)((uintptr_t)tb->tc.ptr + code_offset);

        qatomic_set(patch, LA_LU12I_W_A0_ZERO);
        qatomic_set(patch + 1, LA_ORI_A0_A0_ZERO);
        flush_idcache_range((uintptr_t)patch, (uintptr_t)patch, 8);
    }
}

static bool latxs_tucp_pages_cover(const TranslationUnitPages *source,
                                   const TranslationUnitPages *target)
{
    for (int target_index = 0;
         target_index < TRANSLATION_UNIT_MAX_PAGES; target_index++) {
        uint64_t target_mix = target->pages[target_index].mix;
        uint64_t target_vp_phys_hi = target->pages[target_index].vp_phys_hi;
        bool found = false;

        if (target_vp_phys_hi == UINT64_MAX) {
            return true;
        }
        for (int source_index = 0;
             source_index < TRANSLATION_UNIT_MAX_PAGES; source_index++) {
            uint64_t source_mix = source->pages[source_index].mix;
            uint64_t source_vp_phys_hi = source->pages[source_index].vp_phys_hi;

            if (source_vp_phys_hi == UINT64_MAX) {
                break;
            }
            if (source_mix == target_mix &&
                source_vp_phys_hi == target_vp_phys_hi) {
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }

    /* A full descriptor stores its terminator in the extra slot. */
    return target->pages[TRANSLATION_UNIT_MAX_PAGES].vp_phys_hi == UINT64_MAX;
}

static bool latxs_tucp_link_can_bypass_check(const TranslationBlock *source,
                                              const TranslationBlock *target)
{
    return tucp_fast_direct_check() &&
           (source->flags & HF_CPL_MASK) == HF_CPL_MASK &&
           (target->flags & HF_CPL_MASK) == HF_CPL_MASK &&
           tb_is_tu(source) && tb_is_tu(target) &&
           tb_tu_identity(source) != tb_tu_identity(target) &&
           TB_TU_PAGES(source) && TB_TU_PAGES(target) &&
           latxs_tucp_pages_cover(TB_TU_PAGES(source), TB_TU_PAGES(target));
}

#ifdef CONFIG_SOFTMMU
static void __attribute__((noreturn)) latxs_hras_link_range_error(
        TranslationBlock *source, TranslationBlock *target,
        uintptr_t call_site, int64_t offset)
{
    error_report("HRAS CALL target exceeds the signed 36-bit range: "
                 "source=0x%llx target=0x%llx call_site=0x%llx offset=%lld",
                 (unsigned long long)source->pc,
                 (unsigned long long)target->pc,
                 (unsigned long long)call_site,
                 (long long)offset);
    abort();
}

static bool latxs_hras_call_target_info(TranslationBlock *source,
        TranslationBlock *target, uintptr_t call_site, bool checked,
        bool *needs_long)
{
    uintptr_t entry;
    int64_t short_offset, long_offset;

    if (!target || !target->tc.ptr ||
        source->flags != target->flags ||
        (tb_cflags(target) & (CF_INVALID | CF_NO_ACTIVE)) ||
        get_mask_from_miscflags(source, MF_MTCC_ID_MASK, MF_MTCC_ID_SHIFT) !=
        get_mask_from_miscflags(target, MF_MTCC_ID_MASK, MF_MTCC_ID_SHIFT)) {
        return false;
    }
    /* Ordinary JIT descriptors also list every instruction page. Keep this
     * proof local to HRAS; the general direct-link bypass stays unchanged. */
    if (!checked && !latxs_tucp_link_can_bypass_check(source, target) &&
        !(tucp_fast_direct_check() && tb_is_tu(source) && !tb_is_tu(target) &&
          !((source->bool_flags | target->bool_flags) & IS_AOT_TB) &&
          (source->flags & HF_CPL_MASK) == HF_CPL_MASK &&
          TB_TU_PAGES(source) && TB_TU_PAGES(target) &&
          !TB_TU_PAGES(target)->id &&
          latxs_tucp_pages_cover(TB_TU_PAGES(source), TB_TU_PAGES(target)))) {
        return false;
    }
    entry = (uintptr_t)target->tc.ptr;
    if (option_int_opt == IRQ_CHECK_BLOCK_START && target->pc > source->pc) {
        entry += target->label_pos_nocheck * 4;
    }
    short_offset = ((int64_t)entry - (int64_t)(call_site + 4)) >> 2;
    if (short_offset == sextract64(short_offset, 0, 26)) {
        *needs_long = false;
        return true;
    }
    long_offset = ((int64_t)entry - (int64_t)call_site) >> 2;
    if (long_offset == sextract64(long_offset, 0, 36)) {
        if ((source->bool_flags & IS_AOT_TB) || latxs_hras_aot_translate()) {
            int64_t upper = (long_offset + (1 << 15)) >> 16;
            if (upper != sextract64(upper, 0, 20)) {
                return false;
            }
        }
        *needs_long = true;
        return true;
    }
    if (source->bool_flags & IS_AOT_TB) {
        return false;
    }
    latxs_hras_link_range_error(source, target, call_site, long_offset);
}

bool latxs_hras_call_target_can_link(TranslationBlock *source,
        TranslationBlock *target, uintptr_t call_site, bool checked)
{
    bool needs_long;

    return latxs_hras_call_target_info(source, target, call_site, checked,
                                       &needs_long);
}

bool latxs_hras_call_target_needs_long(TranslationBlock *source,
        TranslationBlock *target, uintptr_t call_site, bool checked)
{
    bool needs_long = false;

    return latxs_hras_call_target_info(source, target, call_site, checked,
                                       &needs_long) && needs_long;
}

#endif
#endif

#if defined(CONFIG_LATX_TU) && defined(CONFIG_SOFTMMU)
static bool latxs_plt_near_jump(uintptr_t source, uintptr_t target)
{
    int64_t delta = (int64_t)target - (int64_t)source;
    return !(delta & 3) && (delta >> 2) == sextract64(delta >> 2, 0, 26);
}

/* Preserve the ordinary link's instruction count at its actual patch site. */
static bool latxs_plt_link_fits(TranslationBlock *source, int n,
                                TranslationBlock *target, const void *entry)
{
    if (source->jmp_insn_offset[n] == TB_JMP_OFFSET_INVALID) {
        return false;
    }
    uintptr_t slot = (uintptr_t)source->tc.ptr + source->jmp_insn_offset[n];
    if (n == 0 && (source->bool_flags & IS_HRAS_CALL)) {
        unsigned bytes = source->jmp_reset_offset[n] - source->jmp_insn_offset[n];
        int64_t short_off = ((int64_t)(uintptr_t)entry - (int64_t)(slot + 4)) >> 2;
        int64_t delta = (int64_t)(uintptr_t)entry - (int64_t)slot;
        int64_t off = delta >> 2, upper = (off + (1 << 15)) >> 16;
        return bytes == 8 && !(delta & 3) &&
               (short_off == sextract64(short_off, 0, 26) ||
                (off == sextract64(off, 0, 36) &&
                 upper == sextract64(upper, 0, 20)));
    }
    if ((source->jmp_cp_data[n] & TB_JMP_CP_DATA_VALID) &&
        latxs_tucp_link_can_bypass_check(source, target)) {
        slot -= (source->jmp_cp_data[n] & TB_JMP_CP_DATA_OFF) << 2;
    }
    return !latxs_plt_near_jump(slot, (uintptr_t)target->tc.ptr) ||
           latxs_plt_near_jump(slot, (uintptr_t)entry);
}

/* Keep the ordinary outgoing TU check and incoming-jump invalidation when
 * choosing a complete LLVM function as the destination of a guest CALL or
 * an n=1 ordinary jump continuation. */
void *latxs_aot_direct_call_entry(TranslationBlock *source, int n,
                                   TranslationBlock *target)
{
    bool allow_n1 = n == 1;
    bool aot_source = (source->bool_flags & IS_AOT_TB) && tb_is_tu(source) &&
                      TB_TU_PAGES(source);
    /* Preserve the existing caller's page proof. HRAS calls may have no
     * legacy RAS patch offset; their CALL marker identifies the ABI boundary.
     * A caller without an outgoing check must cover every target page. */
    bool jit_checked = !(source->bool_flags & IS_AOT_TB) &&
                       latxs_hras_jit_translate(source) &&
                       ((!tb_is_tu(source) && option_external_tu_stub) ||
                        (source->jmp_cp_data[n] & TB_JMP_CP_DATA_VALID) ||
                        (((n == 0 && (source->bool_flags & IS_HRAS_CALL)) ||
                          allow_n1) &&
                         TB_TU_PAGES(source) && TB_TU_PAGES(target) &&
                         latxs_tucp_pages_cover(TB_TU_PAGES(source),
                                               TB_TU_PAGES(target))));
    if ((n && !allow_n1) ||
        (!allow_n1 && !source->inst_ptr_offset &&
         !(source->bool_flags & IS_HRAS_CALL)) ||
        (allow_n1 && (source->jmp_insn_offset[1] == TB_JMP_OFFSET_INVALID ||
                      !((source->jmp_cp_data[1] & TB_JMP_CP_DATA_VALID) ||
                        (!tb_is_tu(source) && option_external_tu_stub) ||
                        (TB_TU_PAGES(source) && TB_TU_PAGES(target) &&
                         latxs_tucp_pages_cover(TB_TU_PAGES(source),
                                               TB_TU_PAGES(target)))))) ||
        (!aot_source && !jit_checked) ||
        !(target->bool_flags & IS_AOT_TB) ||
        !tb_is_tu(target) || !tucp_fast_direct_check() ||
        !TB_TU_PAGES(target) || !TB_TU_PAGES(target)->native ||
        !option_soft_ras || !option_rastoptp ||
        option_int_opt != IRQ_CHECK_ASYNC_NOTIFY ||
        source->flags != target->flags || source->cs_base != target->cs_base ||
        tb_cflags(source) != tb_cflags(target) ||
        (tb_cflags(target) & (CF_INVALID | CF_NO_ACTIVE | CF_SINGLE_STEP |
                              CF_USE_ICOUNT)) ||
        !(target->flags & HF_CS64_MASK) || target->cs_base ||
        (target->flags & HF_CPL_MASK) != HF_CPL_MASK ||
        get_mask_from_miscflags(source, MF_MTCC_ID_MASK, MF_MTCC_ID_SHIFT) !=
        get_mask_from_miscflags(target, MF_MTCC_ID_MASK, MF_MTCC_ID_SHIFT)) {
        return NULL;
    }

    const void *published = qatomic_load_acquire(&target->s_data->aot_native_call_entry);
    AOTNativeTUInfo *info = TB_TU_PAGES(target)->native;
    for (uint32_t i = 0; i < info->count; i++) {
        AOTNativeRecord *record = &info->records[i];
        const NativeRecoverHeader *recover = record->recover;
        if (record->tb == target && record->entry && record->entry == published &&
            record->core == record->entry && recover &&
            !recover->state_bytes) {
            if (((n == 0 && (source->bool_flags & IS_HRAS_CALL)) || n == 1) &&
                !latxs_plt_link_fits(source, n, target, record->entry)) {
                return NULL;
            }
            return record->entry;
        }
    }
    return NULL;
}
#endif

/*
 * Patch a successor link stub in place.
 *
 * HRAS CALLs use a fixed two-word slot: a short link is NOP + BL/B and a
 * long link is PCADDU18I + JIRL. Other successors retain the normal B/BL
 * short link or PCADDU18I + JIRL long link.
 */
static void __latxs_tb_set_jmp_target(TranslationBlock *tb,
        int n, void *target_ptr, bool use_cp_label)
{
    void *jmp_inst_ptr = (void *)((uint64_t)tb->tc.ptr +
                                  tb->jmp_insn_offset[n]);

    /* HRAS CALL slot: patch the fixed two-word B cold + NOP reservation. */
    if (n == 0 && (tb->bool_flags & IS_HRAS_CALL)) {
        uint32_t *slot = jmp_inst_ptr;
        unsigned bytes = tb->jmp_reset_offset[0] - tb->jmp_insn_offset[0];
        int64_t short_offset = ((int64_t)(uintptr_t)target_ptr -
                                (int64_t)(uintptr_t)(slot + 1)) >> 2;
        int64_t long_offset = ((int64_t)(uintptr_t)target_ptr -
                               (int64_t)(uintptr_t)slot) >> 2;
        uint32_t opcode = option_tu_hras == 2 ? 0x54000000 : 0x50000000;

        g_assert(bytes == 8);
        if (tb->link_opt[n] == 1) {
            tb->jmp_inst_hold[n] = qatomic_read((uint64_t *)slot);
            tb->link_opt[n] = 2;
        }
        /*
         * Short HRAS CALL link: NOP + BL/B target.
         * With tuhras=2, BL records PC+4, which is the private return TB
         * immediately after the slot and therefore matches the software RAS.
         */
        if (short_offset == sextract64(short_offset, 0, 26)) {
            uint32_t inst = opcode | ((short_offset >> 16) & 0x3ff) |
                            (((uint64_t)short_offset << 10) & 0x3fffc00);

            qatomic_set((uint64_t *)slot,
                        UINT64_C(0x03400000) | ((uint64_t)inst << 32));
        } else {
            /* Long HRAS CALL link: PCADDU18I $a0 + JIRL $ra/$zero, $a0. */
            uint32_t upper =
                ((long_offset + (1 << 15)) >> 16) & 0xfffff;
            uint32_t lower = long_offset & 0xffff;
            uint32_t first = 0x1e000004 | (upper << 5);
            uint32_t last = 0x4c000080 | (lower << 10) |
                            (option_tu_hras == 2 ? 1 : 0);

            if (long_offset != sextract64(long_offset, 0, 36)) {
                error_report("HRAS CALL slot exceeds signed 36-bit range: "
                             "pc=0x%llx offset=%lld",
                             (unsigned long long)tb->pc,
                             (long long)long_offset);
                abort();
            }
            qatomic_set((uint64_t *)slot, ((uint64_t)last << 32) | first);
        }
        flush_idcache_range((uintptr_t)slot, (uintptr_t)slot, bytes);
        return;
    }

    /*
     * Cross-page check stub: retain the original two-word B + NOP fallback
     * before replacing the check entry with the normal link sequence.
     */
    if (use_cp_label && (tb->jmp_cp_data[n] & TB_JMP_CP_DATA_VALID))
    {
        /*
         * +--------+  <--  jmp_cp_data.off
         * |        |
         * |        |  cp_tb_lookup() : save eip
         * |        |  cp_tb_check() : load tb ptr
         * |        |
         * +--------+  <--  jmp_insn_offset
         * |  b 2   |
         * |  nop   |
         * +--------+  <--  jmp_reset_offset
         */
        int jmp_cp_off = tb->jmp_cp_data[n] & TB_JMP_CP_DATA_OFF;
        jmp_inst_ptr = (void *)((uint64_t)tb->tc.ptr +
                (tb->jmp_insn_offset[n] - (jmp_cp_off << 2)));
        tb->jmp_cp_data[n] |= TB_JMP_CP_DATA_LINKED;
        tb->jmp_inst_hold[n] = qatomic_read((uint64_t*)jmp_inst_ptr);
    }
    if (tb->link_opt[n] == 1) {
        tb->link_opt[n] = 2;
        tb->jmp_inst_hold[n] = qatomic_read((uint64_t*)jmp_inst_ptr);
    }
    /* Default unlinked fallback: B over the following NOP. */
    if (tb->jmp_inst_hold[n] == 0) {
	uint64_t inst_b2  = 0x50000800;
	uint64_t inst_nop = 0x03400000;
	tb->jmp_inst_hold[n] = (inst_nop << 32) | inst_b2;
    }
    int64_t offset = target_ptr - jmp_inst_ptr;

#if 0
    printf("tb %p n %d target ptr %p\n", tb, n, target_ptr);
#endif

    uint32_t inst1, inst2;
    uint32_t *p = jmp_inst_ptr;
    bool hras_link = false;
#if defined(CONFIG_LATX_TU) && defined(CONFIG_SOFTMMU)
    /* A nonzero push site survives gate close/relink and also identifies
     * ordinary TB CALLs, whose last_ir1_type is not initialized.
     * Nonadjacent returns may miss locally, but must not lose a RAS push. */
    hras_link = option_tu_hras == 2 && option_rastoptp && option_soft_ras &&
        latxs_hras_jit_translate(tb) &&
        n == 0 && tb->inst_ptr_offset && !(tb->bool_flags & IS_AOT_TB) &&
        (tb->flags & HF_CS64_MASK) && !tb->cs_base &&
        (tb->flags & HF_CPL_MASK) == HF_CPL_MASK;
#endif
    /*
     * IF next TB is not a cross-page TB
     * BUT current TB generates extra codes for it
     *
     * +-------+            +-------+  <-- jmp_inst_ptr - (jmp_cp_off << 2)
     * | inst1 |    link    | link  |
     * | inst2 |   >>>>>>   | codes |  <== link here to override
     * |       |            |       |
     * +-------+            +-------+  <-- jmp_inst_ptr
     * | b 2   |   unlink   | b 2   |
     * | nop   |   <<<<<<   | nop   |
     * +-------+            +-------+
     *
     * The link codes could be one inst or two inst
     * depending on the jump distance.
     */

    offset >>= 2;

    if (offset == sextract64(offset, 0, 26)) {
        /* Near link (signed 26-bit), one word: B target or BL target. */
        /* 256MB */
        /*
         * 24   20   16   12    8    4    0
         *  |    |    |    |    |    |    |
         * ## #### #### #### #### #### ####
         * | to 0:9   |    to 10:25       |
         * 25         16                  0
         * |  0x3ff   |    0x3fffc00      |
         */
        inst1 = ((offset >> 16) & 0x3ff)     |
                ((offset << 10) & 0x3fffc00) |
                (hras_link ? 0x54000000 : 0x50000000);
        qatomic_set(p, inst1);
        flush_idcache_range((uintptr_t)p, (uintptr_t)p, 4);
    } else if (offset == sextract64(offset, 0, 36)) {
        /* Far link (signed 36-bit): PCADDU18I $a0 + JIRL $ra/$zero, $a0. */
        /* 256GB */
        /*
         *   32   28   24   20   16   12    8    4    0
         *    |    |    |    |    |    |    |    |    |
         * #### #### #### #### #### #### #### #### ####
         * |  pcaddu18i 5:24      |   jirl 0:15       |
         * 35                    16                   0
         */
        int64_t upper = ((offset + (1 << 15)) >> 16) & 0xfffff;
        int64_t lower = (offset & 0xffff);
        /* use $a0($4) */
        inst1 = 0x1e000000 | (upper <<  5) | (4);      /* pcaddu18i */
        inst2 = 0x4c000000 | (lower << 10) | (4 << 5) |
                (hras_link ? 1 : 0); /* jirl */
        qatomic_set(p + 0, inst1);
        qatomic_set(p + 1, inst2);
        flush_idcache_range((uintptr_t)p, (uintptr_t)p, 8);
    } else {
        assert(0);
    }
}

static void latxs_tb_set_jmp_target(TranslationBlock *tb,
        int n, TranslationBlock *next_tb)
{
#ifdef CONFIG_SOFTMMU
    bool is_cross = n ? check_misc_flags(tb, MF_NTB_CROSSPAGE_1) :
                        check_misc_flags(tb, MF_NTB_CROSSPAGE_0);
    bool is_cp_tb = (tb_page_addr1(next_tb) != -1) ? true : false;
#else
    /* no cross-page problem in user mode */
    bool is_cross = false;
    bool is_cp_tb = false;
#endif
    lsassert(next_tb != NULL);
    TBNextData *data = &tb->next_tb_data[n];

    data->pc = next_tb->pc;
    data->page_addr_ptw[0] = next_tb->page_addr[0] >> TARGET_PAGE_BITS;
    data->page_addr_ptw[1] = next_tb->page_addr[1] >> TARGET_PAGE_BITS;
    data->page_addr_cache[0] = next_tb->page_addr_mix;
    data->page_addr_cache[1] = next_tb->page_addr[1] == -1 ? 0 :
        tb_cal_page_addr_mix(next_tb->pc + TARGET_PAGE_SIZE,
                             next_tb->page_addr[1]);
#ifdef CONFIG_LATX_TU
    latxs_tu_generation_offset_link(tb, n, next_tb);
#endif

    uint64_t target_ptr = 0;

    if (option_int_opt == IRQ_CHECK_BLOCK_START && next_tb->pc > tb->pc) {
        target_ptr = (uint64_t)next_tb->tc.ptr +
                     (next_tb->label_pos_nocheck << 2);
    } else {
        target_ptr = (uint64_t)next_tb->tc.ptr;
    }

#if defined(CONFIG_LATX_TU) && defined(CONFIG_SOFTMMU)
    void *direct_entry = latxs_aot_direct_call_entry(tb, n, next_tb);
    if (direct_entry) {
        target_ptr = (uintptr_t)direct_entry;
    }
#endif

    if (target_ptr) {
#if defined(CONFIG_LATX_TU) && defined(CONFIG_SOFTMMU)
        if (option_external_tu_stub && !in_aot_translate &&
            !(tb->bool_flags & IS_AOT_TB) &&
            tucp_fast_direct_check() && !tb_is_tu(tb) &&
            (tb->flags & HF_CPL_MASK) == HF_CPL_MASK &&
            tb_is_tu(next_tb) && TB_TU_PAGES(next_tb)) {
            void *entry = latxs_external_tu_stub(tb, n, (void *)target_ptr);
            if (entry) {
                __latxs_tb_set_jmp_target(tb, n, entry, false);
            }
            return;
        }
#endif
        bool use_cp_label = !is_cross && !is_cp_tb;
#ifdef CONFIG_LATX_TU
        /*
         * Ordinary JIT TBs also carry an id-0 pages descriptor for page
         * tracking, but only real TU targets require the direct TU page check.
         * Keep ordinary targets on the normal direct-link path.
         */
        if (tucp_fast_direct_check() && tb_is_tu(next_tb)) {
            use_cp_label = false;
        }
        if (latxs_tucp_link_can_bypass_check(tb, next_tb)) {
            use_cp_label = true;
        }
#endif
        /* The TU check label follows the normal interrupt check.  When the
         * source TU already covers every target page, patching this label
         * removes only the redundant TUCHECK, not interrupt handling. */
        __latxs_tb_set_jmp_target(tb, n, (void *)target_ptr,
                use_cp_label);
    }
}

#if defined(CONFIG_LATX_TU) && defined(CONFIG_SOFTMMU)
static AOTNativeRecord *aot_native_record(TranslationBlock *tb, bool call)
{
    if (!(tb->bool_flags & IS_AOT_TB) || !TB_TU_PAGES(tb) ||
        !TB_TU_PAGES(tb)->native) {
        return NULL;
    }
    AOTNativeTUInfo *info = TB_TU_PAGES(tb)->native;
    for (uint32_t i = 0; i < info->count; i++) {
        AOTNativeRecord *record = &info->records[i];
        if (record->tb == tb && (call ? record->gate != NULL :
                                           record->entry && !record->core)) {
            return record;
        }
    }
    return NULL;
}

static bool aot_native_branch_fits(intptr_t from, intptr_t to)
{
    int64_t bytes = to - from;
    int64_t offset = bytes >> 2;
    return !(bytes & 3) && offset == sextract64(offset, 0, 26);
}

static uint32_t aot_native_branch(uint32_t opcode, intptr_t from, intptr_t to)
{
    int64_t offset = (to - from) >> 2;
    g_assert(offset == sextract64(offset, 0, 26));
    return opcode | ((offset >> 16) & 0x3ff) |
           (((uint64_t)offset << 10) & 0x3fffc00);
}

static bool aot_tu_pages_cached_valid(CPUX86State *env,
                                      const TranslationUnitPages *desc)
{
    uint64_t entry_size = tucp_generation_entry_size();
    if (!desc || !desc->id || !tucp_generation_enabled() ||
        !env->tu_generation_array || !entry_size ||
        (uint64_t)desc->id * entry_size + entry_size >
            option_tucp_generation_array_size) {
        return false;
    }
    void *base = (void *)(uintptr_t)DMW_OUT(env->tu_generation_array);
    if (tucp_generation_counter()) {
        return ((uint16_t *)base)[desc->id] == env->tu_generation;
    }
    return ((uint8_t *)base)[desc->id] != 0;
}

static const void *aot_native_linked_call_entry(TranslationBlock *target)
{
    if (!option_soft_ras || !option_rastoptp || !TB_TU_PAGES(target) ||
        !TB_TU_PAGES(target)->native) {
        return NULL;
    }
    const void *published_compat =
        qatomic_load_acquire(&target->s_data->aot_native_call_entry);
    const void *published_direct =
        qatomic_load_acquire(&target->s_data->aot_direct_native_entry);
    if (!published_compat || !published_direct) {
        return NULL;
    }
    AOTNativeTUInfo *info = TB_TU_PAGES(target)->native;
    for (uint32_t i = 0; i < info->count; i++) {
        AOTNativeRecord *record = &info->records[i];
        const NativeRecoverHeader *recover = record->recover;
        if (record->tb == target && record->entry &&
            record->entry == published_compat &&
            record->core == record->entry &&
            record->direct_native_entry &&
            record->direct_native_entry == published_direct &&
            recover && !recover->state_bytes) {
            return record->direct_native_entry;
        }
    }
    return NULL;
}

/* Patch one LLVM CALL once its source and target have been validated. */
static void aot_native_direct_call_patch(AOTNativeRecord *record,
                                         TranslationBlock *source,
                                         TranslationBlock *target,
                                         const void *entry,
                                         uint64_t generation_offset)
{
    uint32_t *slot = record->direct_call_slot;
    if (record->tb != source || !slot ||
        (!record->direct_call_linked && !record->direct_call_guards_emitted) ||
        record->callee_pc != target->pc) {
        return;
    }
    if (record->direct_call_linked) {
        const void *direct_entry = aot_native_linked_call_entry(target);
        if (!direct_entry ||
            !aot_native_branch_fits((intptr_t)slot, (intptr_t)direct_entry)) {
            return;
        }
        bool covered = TB_TU_PAGES(source) && TB_TU_PAGES(target) &&
            (TB_TU_PAGES(source) == TB_TU_PAGES(target) ||
             latxs_tucp_pages_cover(TB_TU_PAGES(source),
                                    TB_TU_PAGES(target)));
        if (!covered) {
            if (!current_cpu ||
                !aot_tu_pages_cached_valid(current_cpu->env_ptr,
                                           TB_TU_PAGES(target))) {
                return;
            }
            CPUX86State *env = current_cpu->env_ptr;
            if (!record->linked_call_open) {
                if (env->aot_open_linked_calls_count ==
                    env->aot_open_linked_calls_cap) {
                    uint32_t new_cap = env->aot_open_linked_calls_cap ?
                        env->aot_open_linked_calls_cap * 2 : 64;
                    env->aot_open_linked_calls = g_renew(
                        void *, env->aot_open_linked_calls, new_cap);
                    env->aot_open_linked_calls_cap = new_cap;
                }
                env->aot_open_linked_calls[env->aot_open_linked_calls_count++] =
                    record;
                record->linked_call_open = true;
            }
        }
        qatomic_set(slot, aot_native_branch(0x54000000,
                                            (intptr_t)slot,
                                            (intptr_t)direct_entry));
        flush_idcache_range((uintptr_t)slot, (uintptr_t)slot, 4);
        return;
    }
    if (!aot_native_branch_fits((intptr_t)slot, (intptr_t)entry)) {
        return;
    }
    uint32_t *guard = slot - 11;
    qatomic_set(&guard[0], 0x14000005U |
                (((generation_offset >> 12) & 0xfffff) << 5));
    qatomic_set(&guard[1], 0x038000a5U |
                ((generation_offset & 0xfff) << 10));
    flush_idcache_range((uintptr_t)guard, (uintptr_t)guard, 8);
    qatomic_set(slot, aot_native_branch(0x54000000,
                                        (intptr_t)slot, (intptr_t)entry));
    flush_idcache_range((uintptr_t)slot, (uintptr_t)slot, 4);
}

static void aot_native_direct_call_close(AOTNativeRecord *record,
                                         TranslationBlock *source)
{
    uint32_t *slot = record->direct_call_slot;
    if (record->tb != source || !slot ||
        qatomic_read(slot) == record->direct_call_closed) {
        return;
    }
    qatomic_set(slot, record->direct_call_closed);
    flush_idcache_range((uintptr_t)slot, (uintptr_t)slot, 4);
}

void latxs_aot_close_open_linked_calls(CPUArchState *env)
{
    if (!env || !env->aot_open_linked_calls_count) {
        return;
    }
    qemu_thread_jit_write();
    for (uint32_t i = 0; i < env->aot_open_linked_calls_count; i++) {
        AOTNativeRecord *record = env->aot_open_linked_calls[i];
        record->linked_call_open = false;
        if (record->tb) {
            aot_native_direct_call_close(record, record->tb);
        }
    }
    env->aot_open_linked_calls_count = 0;
}

/* Only continue inside the current frame after an ordinary edge really
 * became a direct jump. External TU stubs and retargeted HRAS layouts keep
 * the dispatcher fallback until their existing link path takes over. */
const void *latxs_aot_ordinary_jump_replay(TranslationBlock *source,
                                           TranslationBlock *target)
{
    const void *entry = NULL;
    qemu_spin_lock(&target->jmp_lock);
    if (!(tb_cflags(source) & CF_INVALID) &&
        !(tb_cflags(target) & CF_INVALID) &&
        qatomic_read(&source->jmp_dest[1]) == (uintptr_t)target &&
        source->jmp_insn_offset[1] != TB_JMP_OFFSET_INVALID) {
        uintptr_t slot = (uintptr_t)source->tc.ptr + source->jmp_insn_offset[1];
        if (source->jmp_cp_data[1] & TB_JMP_CP_DATA_LINKED) {
            slot -= (source->jmp_cp_data[1] & TB_JMP_CP_DATA_OFF) << 2;
        }
        uint32_t *code = (uint32_t *)slot;
        const void *want = latxs_aot_direct_call_entry(source, 1, target);
        if (!want) {
            want = target->tc.ptr;
        }
        intptr_t dest = (intptr_t)want;
        int64_t off = (dest - (intptr_t)slot) >> 2;
        if (aot_native_branch_fits(slot, dest)) {
            if (qatomic_read(code) == aot_native_branch(0x50000000, slot, dest)) {
                entry = want;
            }
        } else if (off == sextract64(off, 0, 36) &&
            qatomic_read(code) == (0x1e000004U |
                ((((off + (1 << 15)) >> 16) & 0xfffff) << 5)) &&
            qatomic_read(code + 1) == (0x4c000080U | ((off & 0xffff) << 10))) {
            entry = want;
        }
    }
    qemu_spin_unlock(&target->jmp_lock);
    return entry;
}

/* Use the same destination checks when linking or replaying an existing
 * ordinary edge. The caller holds the target TB's jump lock. */
static bool aot_native_direct_call_target(TranslationBlock *source, int n,
                                          TranslationBlock *target,
                                          const void **entry_out,
                                          uint64_t *generation_out)
{
    /* Every direct LLVM CALL has its own target generation guard. It does
     * not require the caller TU to cover the target TU's code pages. */
    if (n || !(source->bool_flags & IS_AOT_TB) || !tb_is_tu(source) ||
        !TB_TU_PAGES(source) || !tb_is_tu(target) || !TB_TU_PAGES(target) ||
        !tucp_fast_direct_check() ||
        (source->flags & HF_CPL_MASK) != HF_CPL_MASK ||
        (target->flags & HF_CPL_MASK) != HF_CPL_MASK ||
        !tucp_generation_offset() ||
        option_int_opt != IRQ_CHECK_ASYNC_NOTIFY ||
        source->flags != target->flags || source->cs_base != target->cs_base ||
        tb_cflags(source) != tb_cflags(target) ||
        (tb_cflags(target) & (CF_INVALID | CF_NO_ACTIVE | CF_SINGLE_STEP |
                              CF_USE_ICOUNT)) ||
        !(source->flags & HF_CS64_MASK) || source->cs_base ||
        get_mask_from_miscflags(source, MF_MTCC_ID_MASK, MF_MTCC_ID_SHIFT) !=
        get_mask_from_miscflags(target, MF_MTCC_ID_MASK, MF_MTCC_ID_SHIFT) ||
        !can_do_hamt(current_cpu->env_ptr) || !hamt_mode_dmw()) {
        return false;
    }
    CPUX86State *env = current_cpu->env_ptr;
    TranslationUnitPages *target_pages = TB_TU_PAGES(target);
    uint64_t generation_offset = target_pages->id;
    if (!env->tu_generation_array || !generation_offset ||
        generation_offset >= option_tucp_generation_array_size ||
        generation_offset > INT32_MAX) {
        return false;
    }
    const void *entry = latxs_aot_direct_call_entry(source, n, target);
    if (!entry) {
        entry = target->tc.ptr;
    }
    *entry_out = entry;
    *generation_out = generation_offset;
    return true;
}

/* LLVM CALL sites share the source TB's ordinary incoming edge. A source
 * guest CALL may be duplicated by LLVM, so update every matching machine
 * site while tb_add_jump holds the target lock. */
static void aot_native_direct_call_link(TranslationBlock *source, int n,
                                        TranslationBlock *target)
{
    if (!(source->bool_flags & IS_AOT_TB) || !TB_TU_PAGES(source) ||
        !TB_TU_PAGES(source)->native) {
        return;
    }
    const void *entry;
    uint64_t generation_offset;
    if (!aot_native_direct_call_target(source, n, target, &entry,
                                       &generation_offset)) {
        return;
    }
    AOTNativeTUInfo *info = TB_TU_PAGES(source)->native;
    for (uint32_t i = 0; i < info->count; i++) {
        aot_native_direct_call_patch(&info->records[i], source, target,
                                     entry, generation_offset);
    }
}

/* The target lock protects both the incoming edge and its invalidation. */
const void *latxs_aot_direct_call_replay(TranslationBlock *source,
                                         TranslationBlock *target,
                                         bool ordinary, uint32_t *exact_slot,
                                         uint32_t *generation_out)
{
    const void *entry = NULL;
    uint64_t generation;
    if (generation_out) *generation_out = 0;
    qemu_spin_lock(&target->jmp_lock);
    if (!(tb_cflags(source) & CF_INVALID) &&
        !(tb_cflags(target) & CF_INVALID) &&
        qatomic_read(&source->jmp_dest[0]) == (uintptr_t)target &&
        aot_native_direct_call_target(source, 0, target, &entry, &generation)) {
        aot_native_direct_call_link(source, 0, target);
        bool linked = ordinary && !exact_slot;
        AOTNativeTUInfo *info = TB_TU_PAGES(source)->native;
        for (uint32_t i = 0; info && i < info->count; i++) {
            AOTNativeRecord *record = &info->records[i];
            if (record->tb == source && !record->direct_call_linked &&
                record->callee_pc == target->pc &&
                record->direct_call_slot &&
                (!exact_slot || (record->direct_call_slot == exact_slot &&
                    record->direct_call_guards_emitted &&
                    qatomic_read(exact_slot - 11) == (0x14000005U |
                        (((generation >> 12) & 0xfffff) << 5)) &&
                    qatomic_read(exact_slot - 10) == (0x038000a5U |
                        ((generation & 0xfff) << 10)))) &&
                aot_native_branch_fits((intptr_t)record->direct_call_slot,
                                       (intptr_t)entry) &&
                qatomic_read(record->direct_call_slot) ==
                    aot_native_branch(0x54000000,
                        (intptr_t)record->direct_call_slot, (intptr_t)entry)) {
                linked = true;
            }
        }
        if (!linked) entry = NULL;
        if (entry && generation_out) *generation_out = generation;
    } else {
        entry = NULL;
    }
    qemu_spin_unlock(&target->jmp_lock);
    return entry;
}

const void *latxs_aot_linked_call_replay(TranslationBlock *source,
                                         TranslationBlock *target)
{
    const void *direct_entry = NULL;
    const void *compat_entry = NULL;
    uint64_t generation = 0;
    qemu_spin_lock(&target->jmp_lock);
    if (!(tb_cflags(source) & CF_INVALID) &&
        !(tb_cflags(target) & CF_INVALID) &&
        qatomic_read(&source->jmp_dest[0]) == (uintptr_t)target &&
        aot_native_direct_call_target(source, 0, target, &compat_entry,
                                      &generation)) {
        aot_native_direct_call_link(source, 0, target);
        direct_entry = aot_native_linked_call_entry(target);
        bool linked = false;
        AOTNativeTUInfo *info = TB_TU_PAGES(source)->native;
        for (uint32_t i = 0; direct_entry && info && i < info->count; i++) {
            AOTNativeRecord *record = &info->records[i];
            if (record->tb == source && record->direct_call_linked &&
                record->callee_pc == target->pc &&
                record->direct_call_slot &&
                aot_native_branch_fits((intptr_t)record->direct_call_slot,
                                       (intptr_t)direct_entry) &&
                qatomic_read(record->direct_call_slot) ==
                    aot_native_branch(0x54000000,
                        (intptr_t)record->direct_call_slot,
                        (intptr_t)direct_entry)) {
                linked = true;
            }
        }
        if (!linked) {
            direct_entry = NULL;
        }
    }
    qemu_spin_unlock(&target->jmp_lock);
    return direct_entry;
}

static void aot_native_direct_call_unlink(TranslationBlock *source, int n)
{
    if (n || !(source->bool_flags & IS_AOT_TB) || !TB_TU_PAGES(source) ||
        !TB_TU_PAGES(source)->native) {
        return;
    }
    AOTNativeTUInfo *info = TB_TU_PAGES(source)->native;
    for (uint32_t i = 0; i < info->count; i++) {
        aot_native_direct_call_close(&info->records[i], source);
    }
}

/* Complete CALLs retain the original source TB's ordinary incoming edge. */
static AOTNativeRecord *aot_native_complete_record(TranslationBlock *tb,
                                                 bool call)
{
    if (!(tb->bool_flags & IS_AOT_TB) || !TB_TU_PAGES(tb) ||
        !TB_TU_PAGES(tb)->native) {
        return NULL;
    }
    AOTNativeTUInfo *info = TB_TU_PAGES(tb)->native;
    for (uint32_t i = 0; i < info->count; i++) {
        AOTNativeRecord *record = &info->records[i];
        if (record->tb == tb &&
            (call ? record->binding_slot != NULL : record->core != NULL)) {
            return record;
        }
    }
    return NULL;
}

static void aot_native_complete_unlink(TranslationBlock *tb, int n)
{
    AOTNativeRecord *record = n == 0 ?
        aot_native_complete_record(tb, true) : NULL;
    if (record) {
        /* Old immutable bindings remain allocated until their region flush. */
        qatomic_store_release(&record->binding_slot[0], 0);
    }
}

static bool aot_native_complete_same_frame(AOTNativeRecord *call,
                                           AOTNativeRecord *callee)
{
    const NativeRecoverHeader *outer = NULL;
    AOTNativeTUInfo *info = TB_TU_PAGES(call->tb)->native;
    for (uint32_t i = 0; i < info->count; i++) {
        if (info->records[i].core) {
            outer = info->records[i].recover;
            break;
        }
    }
    const NativeRecoverHeader *target = callee->recover;
    return outer && target && outer->state_bytes && target->state_bytes &&
        outer->state_bytes == target->state_bytes &&
        outer->frame_bytes == target->frame_bytes &&
        outer->saved_xr_mask == target->saved_xr_mask &&
        !memcmp(outer->saved_xr_offsets, target->saved_xr_offsets,
                sizeof(outer->saved_xr_offsets));
}

static void aot_native_complete_link(TranslationBlock *tb, int n,
                                      TranslationBlock *target)
{
    AOTNativeRecord *call = n == 0 ?
        aot_native_complete_record(tb, true) : NULL;
    if (!call) {
        return;
    }
    AOTNativeRecord *callee = aot_native_complete_record(target, false);
    unsigned mtcc = get_mask_from_miscflags(tb, MF_MTCC_ID_MASK,
                                            MF_MTCC_ID_SHIFT);
    if (!callee || call->callee_pc != target->pc ||
        tb->flags != target->flags || tb->cs_base != target->cs_base ||
        tb_cflags(tb) != tb_cflags(target) ||
        (tb_cflags(target) & (CF_INVALID | CF_NO_ACTIVE | CF_SINGLE_STEP |
                              CF_USE_ICOUNT)) ||
        mtcc >= MTCC_NR || mtcc != get_mask_from_miscflags(target,
                                      MF_MTCC_ID_MASK, MF_MTCC_ID_SHIFT) ||
        !can_do_hamt(current_cpu->env_ptr) || !hamt_mode_dmw() ||
        !aot_native_complete_same_frame(call, callee)) {
        aot_native_complete_unlink(tb, n);
        return;
    }
    CPUX86State *env = current_cpu->env_ptr;
    TranslationUnitPages *pages = TB_TU_PAGES(target);
    uint64_t width = tucp_generation_entry_size();
    uint64_t offset = (uint64_t)pages->id * width;
    if (!tucp_fast_direct_check() || !env->tu_generation_array ||
        !(tucp_generation_memset() || tucp_generation_counter() ||
          tucp_generation_offset()) || !pages->id || !width ||
        offset > option_tucp_generation_array_size ||
        width > option_tucp_generation_array_size - offset) {
        aot_native_complete_unlink(tb, n);
        return;
    }
    uintptr_t old = qatomic_read(&call->binding_slot[0]);
    if (old) {
        const AOTNativeCoreBinding *binding = (void *)(uintptr_t)DMW_OUT(old);
        if (binding->core == (void *)(uintptr_t)DMW_IN(callee->core) &&
            binding->generation_offset == offset) {
            return;
        }
        aot_native_complete_unlink(tb, n);
    }
    TCGContext *ctx = tcg_ctx_mtcc[mtcc];
    AOTNativeCoreBinding *binding = tcg_region_alloc_data(ctx,
                                                       sizeof(*binding));
    if (!binding) {
        return;
    }
    binding->core = (void *)(uintptr_t)DMW_IN(callee->core);
    binding->generation_offset = offset;
    /* The core's slot and binding loads use the same DMW data alias. */
    qatomic_store_release(&call->binding_slot[0], DMW_IN(binding));
}

static void aot_native_unlink(TranslationBlock *tb, int n)
{
    aot_native_direct_call_unlink(tb, n);
    aot_native_complete_unlink(tb, n);
    AOTNativeRecord *record = n == 0 ? aot_native_record(tb, true) : NULL;
    if (record && record->saved_ras_offset) {
        /* The backup executes the complete, closed software-RAS CALL. */
        qatomic_set(record->gate, aot_native_branch(0x50000000,
                    (intptr_t)record->gate, (intptr_t)record->stub));
        flush_idcache_range((uintptr_t)record->gate,
                           (uintptr_t)record->gate, 4);
        memcpy(record->gate + 1, record->stub + 1, 7 * sizeof(uint32_t));
        record->gate[4] = record->stub[8];
        flush_idcache_range((uintptr_t)(record->gate + 1),
                           (uintptr_t)(record->gate + 1), 7 * sizeof(uint32_t));
        tb->inst_ptr_offset = record->saved_ras_offset;
        record->saved_ras_offset = 0;
    }
    if (record && qatomic_read(record->gate) != record->gate_insn) {
        qatomic_set(record->gate, record->gate_insn);
        flush_idcache_range((uintptr_t)record->gate,
                           (uintptr_t)record->gate, 4);
    }
}

/* Checked bodies start with ld.d a0, guest_rsp, -8 and return the final slot
 * in a1. Ordinary adapted bodies cannot use a0/a1. Keep their path unchanged. */
static bool aot_native_checks_return(AOTNativeRecord *leaf)
{
    return leaf->entry[0] == 0x28ffe384;
}

static bool aot_native_return_exit(AOTNativeRecord *call)
{
    unsigned mtcc_id = get_mask_from_miscflags(call->tb, MF_MTCC_ID_MASK,
                                              MF_MTCC_ID_SHIFT);
    uintptr_t target = GET_SC_TABLE(mtcc_id, cs_native_to_bt_ret_0);
    uint32_t *cold = call->stub + 13;
    int64_t offset = ((intptr_t)target - (intptr_t)(cold + 1)) >> 2;
    unsigned eip_offset = offsetof(CPUX86State, eip);
    if (eip_offset > 2047 || offset != sextract64(offset, 0, 36)) {
        return false;
    }
    /* RET has already consumed the hardware link and completed every guest
     * output. Dispatch the actual PC without repeating its memory effects. */
    cold[0] = 0x29c002e5 | (eip_offset << 10); /* st.d a1, env, eip */
    cold[1] = 0x1e000004 | ((((offset + (1 << 15)) >> 16) & 0xfffff) << 5);
    cold[2] = 0x4c000080 | ((offset & 0xffff) << 10); /* jirl zero, a0 */
    flush_idcache_range((uintptr_t)cold, (uintptr_t)cold, 12);
    return true;
}

static bool aot_native_inline(AOTNativeRecord *call, AOTNativeRecord *leaf,
                              TranslationBlock *target, unsigned generation)
{
    TranslationBlock *tb = call->tb;
    uintptr_t base = (uintptr_t)tb->tc.ptr - tb->tc.offset_in_tu;
    uintptr_t begin = (uintptr_t)call->gate;
    uintptr_t end = begin + 8 * sizeof(uint32_t);
    bool counter = tucp_generation_counter();
    unsigned guard = counter ? 4 : 3;
    uint32_t *slot = call->gate + guard;
    uint32_t *cold = (void *)(base + LATXS_RAS_TP_COLD_OFFSET(tb));
    int64_t miss = ((intptr_t)call->stub -
                    (intptr_t)(call->gate + guard - 1)) >> 2;
    int64_t offset = ((intptr_t)leaf->entry - (intptr_t)slot) >> 2;
    bool check_return = aot_native_checks_return(leaf);
    unsigned call_insns = offset == sextract64(offset, 0, 26) ? 1 : 2;
    uint32_t *after = slot + call_insns;
    int64_t changed = ((intptr_t)(call->stub + 13) - (intptr_t)after) >> 2;
    if (!option_rastoptp || !option_soft_ras || !tb_is_tu(tb) ||
        !tb->inst_ptr_offset || base + tb->inst_ptr_offset != begin + 16 ||
        end > (uintptr_t)tb->tc.ptr + tb->ir2_num * sizeof(uint32_t) ||
        qatomic_read(call->gate) != call->gate_insn ||
        /* The ordinary link caller already holds target->jmp_lock. */
        qatomic_read(&tb->ras_dest) == (uintptr_t)target ||
        miss != sextract64(miss, 0, counter ? 16 : 21) ||
        offset != sextract64(offset, 0, 36) ||
        !aot_native_branch_fits(begin, (intptr_t)call->stub) ||
        !aot_native_branch_fits((intptr_t)(call->stub + 4), (intptr_t)cold) ||
        !aot_native_branch_fits((intptr_t)(cold + LATX_RAS_TP_CALL_COLD_INSNS - 1), end) ||
        (check_return && changed != sextract64(changed, 0, 16)) ||
        !aot_native_branch_fits((intptr_t)(after + check_return),
                               (intptr_t)call->continuation)) {
        return false;
    }
    uint32_t *done = cold + LATX_RAS_TP_CALL_COLD_INSNS - 1;
    if (*done != aot_native_branch(0x50000000, (intptr_t)done, end)) {
        return false;
    }
    for (unsigned i = 0; i < 2; i++) {
        uintptr_t jump = (uintptr_t)tb->tc.ptr + tb->jmp_insn_offset[i];
        uintptr_t patch = (uintptr_t)tb->tc.ptr +
                          tb->s_data->tu_generation_offset_patch[i];
        if ((tb->jmp_insn_offset[i] != TB_JMP_OFFSET_INVALID &&
             jump < end && jump + 16 > begin) ||
            (tb->s_data->tu_generation_offset_patch[i] != TB_JMP_OFFSET_INVALID &&
             patch < end && patch + 8 > begin)) {
            return false;
        }
    }
    uint32_t code[8] = { 0 };
    unsigned insn = 0;
    code[insn++] = 0x28c002e5 |
                   (offsetof(CPUX86State, tu_generation_array) << 10);
    code[insn++] = (counter ? 0x2a4000a6 : 0x2a0000a6) | (generation << 10);
    if (counter) {
        code[insn++] = 0x2a4002e5 |
                       (offsetof(CPUX86State, tu_generation) << 10);
        code[insn++] = 0x5c0000a6 | (((uint32_t)miss & 0xffff) << 10);
    } else {
        code[insn++] = 0x400000c0 | (((uint32_t)miss & 0xffff) << 10) |
                       (((uint64_t)miss >> 16) & 0x1f);
    }
    if (offset == sextract64(offset, 0, 26)) {
        code[insn++] = aot_native_branch(0x54000000, (intptr_t)slot,
                                       (intptr_t)leaf->entry);
    } else {
        code[insn++] = 0x1e000008 | ((((offset + (1 << 15)) >> 16) & 0xfffff) << 5);
        code[insn++] = 0x4c000101 | ((offset & 0xffff) << 10);
    }
    if (check_return) {
        code[insn++] = 0x5c0000a4 | (((uint32_t)changed & 0xffff) << 10);
    }
    code[insn] = aot_native_branch(0x50000000, (intptr_t)(call->gate + insn),
                                  (intptr_t)call->continuation);
    insn++;
    while (insn < ARRAY_SIZE(code)) {
        code[insn++] = 0x03400000;
    }
    /* Stop all RAS patching before reusing its seven hot instructions.
     * The closed cold helper clears the software stack on a native miss. */
    if (!latxs_ras_close_relinkable(tb)) {
        return false;
    }
    memcpy(call->stub, call->gate, sizeof(code));
    call->stub[8] = call->stub[4];
    call->stub[4] = aot_native_branch(0x50000000,
                                    (intptr_t)(call->stub + 4), (intptr_t)cold);
    flush_idcache_range((uintptr_t)call->stub, (uintptr_t)call->stub, 36);
    call->saved_ras_offset = tb->inst_ptr_offset;
    tb->inst_ptr_offset = 0;
    qatomic_set(call->gate, aot_native_branch(0x50000000,
                (intptr_t)call->gate, (intptr_t)call->stub));
    flush_idcache_range((uintptr_t)call->gate, (uintptr_t)call->gate, 4);
    memcpy(call->gate + 1, code + 1, 7 * sizeof(uint32_t));
    flush_idcache_range((uintptr_t)(call->gate + 1),
                       (uintptr_t)(call->gate + 1), 7 * sizeof(uint32_t));
    qatomic_set(call->gate, code[0]);
    flush_idcache_range((uintptr_t)call->gate, (uintptr_t)call->gate, 4);
    return true;
}

static void aot_native_link(TranslationBlock *tb, int n,
                            TranslationBlock *target)
{
    AOTNativeRecord *call = n == 0 ? aot_native_record(tb, true) : NULL;
    AOTNativeRecord *leaf = call ? aot_native_record(target, false) : NULL;
    if (call && call->saved_ras_offset) {
        aot_native_unlink(tb, n);
    }
    if (!leaf || tb->flags != target->flags ||
        tb->cs_base != target->cs_base ||
        tb_cflags(tb) != tb_cflags(target) ||
        (tb_cflags(target) & (CF_INVALID | CF_NO_ACTIVE | CF_SINGLE_STEP |
                              CF_USE_ICOUNT)) ||
        get_mask_from_miscflags(tb, MF_MTCC_ID_MASK, MF_MTCC_ID_SHIFT) !=
        get_mask_from_miscflags(target, MF_MTCC_ID_MASK, MF_MTCC_ID_SHIFT) ||
        !can_do_hamt(current_cpu->env_ptr)) {
        return;
    }
    CPUX86State *env = current_cpu->env_ptr;
    TranslationUnitPages *pages = TB_TU_PAGES(target);
    uint64_t entry_size = tucp_generation_entry_size();
    uint64_t generation_offset = (uint64_t)pages->id * entry_size;
    unsigned array_offset = offsetof(CPUX86State, tu_generation_array);
    unsigned counter_offset = offsetof(CPUX86State, tu_generation);
    if (!tucp_fast_direct_check() || !env->tu_generation_array ||
        !(tucp_generation_memset() || tucp_generation_counter() ||
          tucp_generation_offset()) || !pages->id ||
        generation_offset + entry_size > option_tucp_generation_array_size ||
        generation_offset > INT32_MAX || array_offset > 2047 ||
        counter_offset > 2047) {
        return;
    }
    /* The body models CALL's RSP subtraction in its IR before the guest
     * instructions, so LLVM can cancel it with the proven RET adjustment.
     * The original return-address store precedes the gate and still executes. */
    bool counter = tucp_generation_counter();
    bool immediate = generation_offset <= 2047;
    bool check_return = aot_native_checks_return(leaf);
    if (check_return && !aot_native_return_exit(call)) {
        return;
    }
    if (immediate && aot_native_inline(call, leaf, target, generation_offset)) {
        return;
    }
    unsigned guard = (counter ? 4 : 3) + (immediate ? 0 : 2);
    uint32_t *slot = call->stub + guard;
    unsigned call_slots = check_return ? 4 : 3;
    uint32_t *fallback = slot + call_slots;
    int64_t offset = ((intptr_t)leaf->entry - (intptr_t)slot) >> 2;
    if (offset != sextract64(offset, 0, 36) ||
        !aot_native_branch_fits((intptr_t)call->gate,
                                (intptr_t)call->stub) ||
        !aot_native_branch_fits((intptr_t)(slot + 1),
                                (intptr_t)call->continuation) ||
        !aot_native_branch_fits((intptr_t)(slot + 2),
                                (intptr_t)call->continuation) ||
        !aot_native_branch_fits((intptr_t)(slot + call_slots - 1),
                                (intptr_t)call->continuation) ||
        !aot_native_branch_fits((intptr_t)(fallback + 1),
                                (intptr_t)(call->gate + 1))) {
        return;
    }
    /* TLB flushes expire this cache even when they keep the linked TB alive.
     * Check before changing RSP or the software RAS. Keep a4, which holds the
     * guest return PC needed by the complete original CALL on a cache miss. */
    unsigned insn = 0;
    if (!immediate) {
        call->stub[insn++] = 0x14000004 | ((generation_offset >> 12) << 5);
        call->stub[insn++] = 0x03800084 | ((generation_offset & 0xfff) << 10);
    }
    call->stub[insn++] = 0x28c002e5 | (array_offset << 10); /* ld.d a1, env */
    if (counter) {
        call->stub[insn++] = immediate ?
            0x2a4000a6 | (generation_offset << 10) : 0x382410a6;
        call->stub[insn++] = 0x2a4002e5 | (counter_offset << 10);
        call->stub[insn++] = 0x5c0000a6 | ((call_slots + 1) << 10);
    } else {
        call->stub[insn++] = immediate ?
            0x2a0000a6 | (generation_offset << 10) : 0x382010a6;
        call->stub[insn++] = 0x400000c0 | ((call_slots + 1) << 10);
    }
    g_assert(insn == guard);
    if (offset == sextract64(offset, 0, 26)) {
        slot[0] = aot_native_branch(0x54000000, (intptr_t)slot,
                                   (intptr_t)leaf->entry);
        if (check_return) {
            int64_t changed = (call->stub + 13) - (slot + 1);
            slot[1] = 0x5c0000a4 | (((uint32_t)changed & 0xffff) << 10);
        } else {
            slot[1] = aot_native_branch(0x50000000, (intptr_t)(slot + 1),
                                       (intptr_t)call->continuation);
        }
    } else {
        uint32_t upper = ((offset + (1 << 15)) >> 16) & 0xfffff;
        uint32_t lower = offset & 0xffff;
        slot[0] = 0x1e000008 | (upper << 5); /* pcaddu18i a4 */
        slot[1] = 0x4c000101 | (lower << 10); /* jirl ra, a4 */
    }
    slot[2] = aot_native_branch(0x50000000, (intptr_t)(slot + 2),
                               (intptr_t)call->continuation);
    if (check_return) {
        if (offset != sextract64(offset, 0, 26)) {
            int64_t changed = (call->stub + 13) - (slot + 2);
            slot[2] = 0x5c0000a4 | (((uint32_t)changed & 0xffff) << 10);
        }
        slot[3] = aot_native_branch(0x50000000, (intptr_t)(slot + 3),
                                   (intptr_t)call->continuation);
    }
    fallback[0] = call->gate_insn;
    fallback[1] = aot_native_branch(0x50000000, (intptr_t)(fallback + 1),
                                   (intptr_t)(call->gate + 1));
    flush_idcache_range((uintptr_t)call->stub, (uintptr_t)call->stub,
                       (fallback + 2 - call->stub) * sizeof(uint32_t));
    /* Publish last. Unlink restores one original instruction before code dies. */
    qatomic_set(call->gate, aot_native_branch(0x50000000,
                (intptr_t)call->gate, (intptr_t)call->stub));
    flush_idcache_range((uintptr_t)call->gate, (uintptr_t)call->gate, 4);
}
#endif

/* check fpu rotate and patch the native jump address */
void latx_tb_set_jmp_target(TranslationBlock *tb, int n,
        TranslationBlock *next_tb)
{
#ifdef CONFIG_LATX_DEBUG
    if (tb->inhibit_irq_type == 1) {
        /* type1 = set   the inhibitirq in env.hflags
         * type2 = clear the inhibitirq in env.hflags
         * type1 TB must be linked to type2 TB to make sure the
         * inhibitirq in env.hflags will be cleared.  */
        if (next_tb->inhibit_irq_type != 2) {
            fprintf(stderr, "%s:%d \033[31m unexpected tb link. \033[m " \
                    "TB %p -> %p PC %lx -> %lx " \
                    "inhibitirq %d -> %d\n", __func__, __LINE__,
                    tb, next_tb ,tb->pc, next_tb->pc,
                    tb->inhibit_irq_type, next_tb->inhibit_irq_type);
        }
    }
#endif
    latxs_tb_set_jmp_target(tb, n, next_tb);
#if defined(CONFIG_LATX_TU) && defined(CONFIG_SOFTMMU)
    aot_native_direct_call_link(tb, n, next_tb);
    aot_native_complete_link(tb, n, next_tb);
    aot_native_link(tb, n, next_tb);
#endif
}

/*
 * Restore the unlinked fallback sequence for a successor link stub.
 * Dynamic links can have replaced that sequence with B/BL,
 * PCADDU18I + JIRL, or an HRAS CALL slot.
 */
void latx_tb_reset_jmp_target(TranslationBlock *tb, int n)
{
#if defined(CONFIG_LATX_TU) && defined(CONFIG_SOFTMMU)
    aot_native_unlink(tb, n);
#endif
    uint64_t inst_b2  = 0x50000800;
    uint64_t inst_nop = 0x03400000;
    uint64_t inst = (inst_nop << 32) | inst_b2;
    TBNextData *data = &tb->next_tb_data[n];

    memset(data, 0, sizeof(*data));
    data->pc = tb->pc;

    void *jmp_inst_ptr = (void *)((uint64_t)tb->tc.ptr +
            tb->jmp_insn_offset[n]);
    /* HRAS CALL: restore the saved B cold + NOP two-word slot. */
    if (n == 0 && (tb->bool_flags & IS_HRAS_CALL)) {
        latxs_hras_close_check_gate(tb);
        if (tb->link_opt[n] == 2) {
            unsigned bytes = tb->jmp_reset_offset[0] - tb->jmp_insn_offset[0];
            g_assert(bytes == 8);
            tb->link_opt[n] = 1;
            qatomic_set((uint64_t *)jmp_inst_ptr, tb->jmp_inst_hold[n]);
            flush_idcache_range((uintptr_t)jmp_inst_ptr,
                               (uintptr_t)jmp_inst_ptr, bytes);
        }
#ifdef CONFIG_LATX_TU
        latxs_tu_generation_offset_unlink(tb, n);
#endif
        return;
    }
    /* Cross-page check stub: restore its saved B + NOP fallback. */
    if (tb->jmp_cp_data[n] & TB_JMP_CP_DATA_LINKED) {
        int jmp_cp_off = tb->jmp_cp_data[n] & TB_JMP_CP_DATA_OFF;
        jmp_inst_ptr = (void *)((uint64_t)tb->tc.ptr +
                (tb->jmp_insn_offset[n] - (jmp_cp_off << 2)));
        /*
         * +-------+            +-------+  <-- jmp_inst_ptr - (jmp_cp_off << 2)
         * | inst1 |    link    | link  |
         * | inst2 |   >>>>>>   | codes |
         * |       |            |       |
         * +-------+   unlink   +-------+  <-- jmp_inst_ptr
         * | b 2   |   <<<<<<   | b 2   |
         * | nop   |            | nop   |
         * +-------+            +-------+
         */
        tb->jmp_cp_data[n] &= ~TB_JMP_CP_DATA_LINKED;
        inst = tb->jmp_inst_hold[n];
    }
    /* The stub is already unlinked; only discard auxiliary link state. */
    if (tb->link_opt[n] == 1) {
#ifdef CONFIG_LATX_TU
        latxs_tu_generation_offset_unlink(tb, n);
#ifdef CONFIG_SOFTMMU
        latxs_external_tu_stub_unlink(tb, n);
#endif
#endif
        return;
    }
    /* A linked stub saved its original two-word fallback here. */
    if (tb->link_opt[n] == 2) {
        tb->link_opt[n] = 1;
        inst = tb->jmp_inst_hold[n];
    }

    /* Restore the saved fallback pair, or the default B over NOP pair. */
    qatomic_set((uint64_t*)jmp_inst_ptr, inst);
    flush_idcache_range((uintptr_t)jmp_inst_ptr, (uintptr_t)jmp_inst_ptr, 8);
#ifdef CONFIG_LATX_TU
    latxs_tu_generation_offset_unlink(tb, n);
#ifdef CONFIG_SOFTMMU
    latxs_external_tu_stub_unlink(tb, n);
#endif
#endif
}
