/*
 * SPDX-FileCopyrightText: 2024-2026 LATS Project Authors
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "env.h"
#include "lsenv.h"
#include "reg-alloc.h"
#include "latx-options.h"
#include "translate.h"
#include "flag-lbt.h"

bool latxs_generate_eflag_by_lbt(IR2_OPND *dest,
        IR2_OPND *src0, IR2_OPND *src1,
        IR1_INST *pir1, bool is_sx)
{
    switch (ir1_opcode(pir1 )) {
    case X86_INS_XADD:
    case X86_INS_ADD: {
        LATXS_GENERATE_EFLAG_IR2_2(LISA_X86ADD_B);
        break;
    }
    case X86_INS_ADC: {
        LATXS_GENERATE_EFLAG_IR2_2(LISA_X86ADC_B);
        break;
    }
    case X86_INS_INC: {
        LATXS_GENERATE_EFLAG_IR2_1(LISA_X86INC_B);
        break;
    }
    case X86_INS_DEC: {
        LATXS_GENERATE_EFLAG_IR2_1(LISA_X86DEC_B);
        break;
    }
    case X86_INS_CMPSB:
    case X86_INS_CMPSW:
    case X86_INS_CMPSD:
    case X86_INS_CMPSQ:
    case X86_INS_SCASB:
    case X86_INS_SCASW:
    case X86_INS_SCASD:
    case X86_INS_SCASQ:
    case X86_INS_CMPXCHG:
    case X86_INS_NEG:
    case X86_INS_CMP:
    case X86_INS_SUB: {
        LATXS_GENERATE_EFLAG_IR2_2(LISA_X86SUB_B);
        break;
    }
    case X86_INS_SBB: {
        LATXS_GENERATE_EFLAG_IR2_2(LISA_X86SBC_B);
        break;
    }
    case X86_INS_BLSMSK:
    case X86_INS_XOR: {
        LATXS_GENERATE_EFLAG_IR2_2(LISA_X86XOR_B);
        break;
    }
    case X86_INS_TEST:
    case X86_INS_ANDN:
    case X86_INS_BZHI:
    case X86_INS_BLSI:
    case X86_INS_BLSR:
    case X86_INS_AND: {
        LATXS_GENERATE_EFLAG_IR2_2(LISA_X86AND_B);
        break;
    }
    case X86_INS_OR: {
        LATXS_GENERATE_EFLAG_IR2_2(LISA_X86OR_B);
        break;
    }
    case X86_INS_SAL:
    case X86_INS_SHL: {
        LATXS_GENERATE_EFLAG_IR2_2(LISA_X86SLL_B);
        break;
    }
    case X86_INS_SHR: {
        LATXS_GENERATE_EFLAG_IR2_2(LISA_X86SRL_B);
        break;
    }
    case X86_INS_SAR: {
        LATXS_GENERATE_EFLAG_IR2_2(LISA_X86SRA_B);
        break;
    }
    case X86_INS_RCL: {
        LATXS_GENERATE_EFLAG_IR2_2(LISA_X86RCL_B);
        break;
    }
    case X86_INS_RCR: {
        LATXS_GENERATE_EFLAG_IR2_2(LISA_X86RCR_B);
        break;
    }
    case X86_INS_MUL: {
        LATXS_GENERATE_EFLAG_IR2_2(LISA_X86MUL_BU);
        break;
    }
    case X86_INS_IMUL: {
        LATXS_GENERATE_EFLAG_IR2_2(LISA_X86MUL_B);
        break;
    }
    case X86_INS_ROR:
    case X86_INS_ROL:
        break;
    case X86_INS_SHLD:
    case X86_INS_SHRD:
    case X86_INS_LZCNT:
         return false;
    default:
       lsassertm(0, "%s is not implemented in %s\n",
               pir1->info->mnemonic, __func__);
        return false;
    }

    return true;
}

void latxs_get_eflag_condition(
        IR2_OPND *cond,
        IR1_INST *pir1)
{
    switch (ir1_opcode(pir1)) {
    case X86_INS_RCL:
    case X86_INS_RCR:
        {
            latxs_append_ir2_opnd1i(LISA_X86MFFLAG, cond, 0x1);
            break;
        }
    case X86_INS_LSL:
    case X86_INS_LAR:
        {
            latxs_append_ir2_opnd1i(LISA_X86MFFLAG, cond, 0x8);
            break;
        }
    case X86_INS_LOOPE:
        {
            latxs_append_ir2_opnd1i(LISA_SETX86J, cond, COND_E);
            break;
        }
    case X86_INS_LOOPNE:
        {
            latxs_append_ir2_opnd1i(LISA_SETX86J, cond, COND_NE);
            break;
        }
    case X86_INS_CMOVAE:
    case X86_INS_FCMOVNB:
    case X86_INS_SETAE:
        {
            latxs_append_ir2_opnd1i(LISA_SETX86J, cond, COND_AE);
            break;
        }
    case X86_INS_CMOVB:
    case X86_INS_FCMOVB:
    case X86_INS_SETB:
        {
            latxs_append_ir2_opnd1i(LISA_SETX86J, cond, COND_B);
            break;
        }
    case X86_INS_CMOVNP:
    case X86_INS_FCMOVNU:
    case X86_INS_SETNP:
        {
            latxs_append_ir2_opnd1i(LISA_SETX86J, cond, COND_PO);
            break;
        }
    case X86_INS_CMOVP:
    case X86_INS_FCMOVU:
    case X86_INS_SETP:
        {
            latxs_append_ir2_opnd1i(LISA_SETX86J, cond, COND_PE);
            break;
        }
    case X86_INS_CMOVE:
    case X86_INS_FCMOVE:
    case X86_INS_SETE:
        {
            latxs_append_ir2_opnd1i(LISA_SETX86J, cond, COND_E);
            break;
        }
    case X86_INS_CMOVNE:
    case X86_INS_FCMOVNE:
    case X86_INS_SETNE:
        {
            latxs_append_ir2_opnd1i(LISA_SETX86J, cond, COND_NE);
            break;
        }
    case X86_INS_CMOVS:
    case X86_INS_SETS:
        {
            latxs_append_ir2_opnd1i(LISA_SETX86J, cond, COND_S);
            break;
        }
    case X86_INS_CMOVNS:
    case X86_INS_SETNS:
        {
            latxs_append_ir2_opnd1i(LISA_SETX86J, cond, COND_NS);
            break;
        }
    case X86_INS_CMOVO:
    case X86_INS_SETO:
        {
            latxs_append_ir2_opnd1i(LISA_SETX86J, cond, COND_O);
            break;
        }
    case X86_INS_CMOVNO:
    case X86_INS_SETNO:
        {
            latxs_append_ir2_opnd1i(LISA_SETX86J, cond, COND_NO);
            break;
        }
    case X86_INS_CMOVBE:
    case X86_INS_FCMOVBE:
    case X86_INS_SETBE:
        {
            latxs_append_ir2_opnd1i(LISA_SETX86J, cond, COND_BE);
            break;
        }
    case X86_INS_CMOVA:
    case X86_INS_FCMOVNBE:
    case X86_INS_SETA:
        {
            latxs_append_ir2_opnd1i(LISA_SETX86J, cond, COND_A);
            break;
        }
    case X86_INS_CMOVGE:
    case X86_INS_SETGE:
        {
            latxs_append_ir2_opnd1i(LISA_SETX86J, cond, COND_GE);
            break;
        }
    case X86_INS_CMOVL:
    case X86_INS_SETL:
        {
            latxs_append_ir2_opnd1i(LISA_SETX86J, cond, COND_L);
            break;
        }
    case X86_INS_CMOVG:
    case X86_INS_SETG:
        {
            latxs_append_ir2_opnd1i(LISA_SETX86J, cond, COND_G);
            break;
        }
    case X86_INS_CMOVLE:
    case X86_INS_SETLE:
        {
            latxs_append_ir2_opnd1i(LISA_SETX86J, cond, COND_LE);
            break;
        }
    default:
        lsassertm(0, "%s for %s is not implemented\n",
                __func__, ir1_name(ir1_opcode(pir1)));
        break;
    }
}
