/*
 * SPDX-FileCopyrightText: 2024-2026 LATS Project Authors
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef _IR2_H_
#define _IR2_H_

#include "common.h"
#include "la-ir2.h"
#include "ir2-inst.h"
#include "tcg/tcg.h"

int ir2_to_string(IR2_INST *, char *);

uint32_t ir2_assemble(IR2_INST *);
uint32_t ir2_assemble_new(IR2_INST *);

/* CSR registers */
extern IR2_OPND la_csr_crmd;
extern IR2_OPND la_csr_prmd;
extern IR2_OPND la_csr_euen;
extern IR2_OPND la_csr_misc;
extern IR2_OPND la_csr_ecfg;
extern IR2_OPND la_csr_estat;
extern IR2_OPND la_csr_era;
extern IR2_OPND la_csr_badv;
extern IR2_OPND la_csr_badi;
extern IR2_OPND la_csr_eentry;
extern IR2_OPND la_csr_rvacfg;
extern IR2_OPND la_csr_cpuid;
extern IR2_OPND la_csr_prcfg1;
extern IR2_OPND la_csr_prcfg2;
extern IR2_OPND la_csr_prcfg3;

extern IR2_OPND la_csr_tlbidx;
extern IR2_OPND la_csr_tlbehi;
extern IR2_OPND la_csr_tlbelo0;
extern IR2_OPND la_csr_tlbelo1;
extern IR2_OPND la_csr_asid;
extern IR2_OPND la_csr_stlbps;

extern IR2_OPND la_csr_ks0;
extern IR2_OPND la_csr_ks1;
extern IR2_OPND la_csr_ks2;
extern IR2_OPND la_csr_ks3;
extern IR2_OPND la_csr_ks4;
extern IR2_OPND la_csr_ks5;
extern IR2_OPND la_csr_ks6;
extern IR2_OPND la_csr_ks7;

extern IR2_OPND la_csr_tcfg;
extern IR2_OPND la_csr_tintclear;

extern IR2_OPND la_csr_impctl1;
extern IR2_OPND la_csr_impctl2;

extern IR2_OPND la_csr_tlbr_entry;
extern IR2_OPND la_csr_tlbr_badv;
extern IR2_OPND la_csr_tlbr_era;
extern IR2_OPND la_csr_tlbr_save;
extern IR2_OPND la_csr_tlbr_elo0;
extern IR2_OPND la_csr_tlbr_elo1;
extern IR2_OPND la_csr_tlbr_ehi;
extern IR2_OPND la_csr_tlbr_prmd;

/* Global variables */
extern IR2_OPND latxs_zero_ir2_opnd;
extern IR2_OPND latxs_sp_ir2_opnd;
extern IR2_OPND latxs_fp_ir2_opnd;
extern IR2_OPND latxs_env_ir2_opnd;

extern IR2_OPND latxs_f32_ir2_opnd;
extern IR2_OPND latxs_fcsr_ir2_opnd;
extern IR2_OPND latxs_fcsr1_ir2_opnd;
extern IR2_OPND latxs_fcsr2_ir2_opnd;
extern IR2_OPND latxs_fcsr3_ir2_opnd;
extern IR2_OPND latxs_fcc0_ir2_opnd;
extern IR2_OPND latxs_fcc1_ir2_opnd;
extern IR2_OPND latxs_fcc2_ir2_opnd;
extern IR2_OPND latxs_fcc3_ir2_opnd;
extern IR2_OPND latxs_fcc4_ir2_opnd;
extern IR2_OPND latxs_fcc5_ir2_opnd;
extern IR2_OPND latxs_fcc6_ir2_opnd;
extern IR2_OPND latxs_fcc7_ir2_opnd;

/* host helper  latxs_arguments */
extern IR2_OPND latxs_arg0_ir2_opnd;
extern IR2_OPND latxs_arg1_ir2_opnd;
extern IR2_OPND latxs_arg2_ir2_opnd;
extern IR2_OPND latxs_arg3_ir2_opnd;
extern IR2_OPND latxs_arg4_ir2_opnd;
extern IR2_OPND latxs_arg5_ir2_opnd;
extern IR2_OPND latxs_arg6_ir2_opnd;
extern IR2_OPND latxs_arg7_ir2_opnd;
extern IR2_OPND latxs_ret0_ir2_opnd;
extern IR2_OPND latxs_ret1_ir2_opnd;
extern IR2_OPND latxs_ra_ir2_opnd;

extern IR2_OPND latxs_tmp0_ir2_opnd;
extern IR2_OPND latxs_tmp1_ir2_opnd;
extern IR2_OPND latxs_tmp2_ir2_opnd;
extern IR2_OPND latxs_tmp3_ir2_opnd;
extern IR2_OPND latxs_tmp4_ir2_opnd;
extern IR2_OPND latxs_tmp5_ir2_opnd;
extern IR2_OPND latxs_tmp6_ir2_opnd;
extern IR2_OPND latxs_tmp7_ir2_opnd;

extern IR2_OPND latxs_s0_ir2_opnd;
extern IR2_OPND latxs_s1_ir2_opnd;
extern IR2_OPND latxs_s2_ir2_opnd;
extern IR2_OPND latxs_s3_ir2_opnd;
extern IR2_OPND latxs_s4_ir2_opnd;
extern IR2_OPND latxs_s5_ir2_opnd;
extern IR2_OPND latxs_s6_ir2_opnd;
extern IR2_OPND latxs_s7_ir2_opnd;
extern IR2_OPND latxs_s8_ir2_opnd;

/* static temp  latxs_registers */
extern IR2_OPND latxs_stmp1_ir2_opnd;
extern IR2_OPND latxs_stmp2_ir2_opnd;

extern IR2_OPND latxs_invalid_ir2_opnd;

extern IR2_OPND latxs_scr0_ir2_opnd;
extern IR2_OPND latxs_scr1_ir2_opnd;
extern IR2_OPND latxs_scr2_ir2_opnd;
extern IR2_OPND latxs_scr3_ir2_opnd;

/* Functions to build IR2_OPND */
inline __attribute__ ((always_inline))
void latxs_ir2_opnd_build(IR2_OPND *opnd, IR2_OPND_TYPE t, int value)
{
    opnd->_type = t;
    opnd->val   = value;
}

inline __attribute__ ((always_inline))
IR2_OPND latxs_ir2_opnd_new(IR2_OPND_TYPE type, int value)
{
    IR2_OPND opnd;
    latxs_ir2_opnd_build(&opnd, type, value);
    return opnd;
}

inline __attribute__ ((always_inline))
bool latxs_ir2_opcode_is_branch(IR2_OPCODE opcode)
{
    return (opcode >= LISA_BEQZ && opcode <= LISA_BNEZ) ||
           (opcode >= LISA_BCEQZ && opcode <= LISA_BCNEZ) ||
           (opcode >= LISA_B && opcode <= LISA_BGEU);
}

/*
 * Instructions whose last operand may be an IR2 label that the label
 * disposal pass rewrites into a PC-relative immediate:
 *
 *   - control-flow branches:  offs16 = (label_pos - imm_pos) instructions
 *   - LISA_PCADDI:            si20   = (label_pos - imm_pos) instructions,
 *                             rd = PC + sign_extend(si20 << 2) at run time.
 *
 * LISA_PCADDI is intentionally not reported by latxs_ir2_opcode_is_branch():
 * it is not control flow and must not end a basic block.  It only shares the
 * label-resolution machinery because LoongArch encodes its PC-relative
 * displacement with the same "label - pc, in instruction units" convention
 * that branches use for offs16.
 */
inline __attribute__ ((always_inline))
bool latxs_ir2_opcode_has_pcrel_label(IR2_OPCODE opcode)
{
    return latxs_ir2_opcode_is_branch(opcode) || opcode == LISA_PCADDI;
}

inline __attribute__ ((always_inline))
IR2_OPND latxs_ir2_opnd_new_inv(void)
{
    return latxs_invalid_ir2_opnd;
}


/* Functions to access IR2_OPND's fields */
inline __attribute__ ((always_inline))
IR2_OPND_TYPE latxs_ir2_opnd_type(IR2_OPND *opnd) { return opnd->_type; }
inline __attribute__ ((always_inline))
int latxs_ir2_opnd_reg(IR2_OPND *opnd) { return opnd->val; }
inline __attribute__ ((always_inline))
int latxs_ir2_opnd_imm(IR2_OPND *opnd) { return opnd->val; }
inline __attribute__ ((always_inline))
int latxs_ir2_opnd_label_id(IR2_OPND *opnd) { return opnd->val; }
inline __attribute__ ((always_inline))
int latxs_ir2_opnd_addr(IR2_OPND *opnd) { return opnd->val; }
int           latxs_ir2_opnd_offset(IR2_OPND *opnd);
/* Compare IR2 OPND */
inline __attribute__ ((always_inline))
int latxs_ir2_opnd_cmp(IR2_OPND *opnd1, IR2_OPND *opnd2)
{
    return opnd1->_type == opnd2->_type &&
           opnd1->val   == opnd2->val;
}

/* Functions to identify IR2_OPND's type/attribute */
inline __attribute__ ((always_inline))
int latxs_ir2_opnd_is_inv(IR2_OPND *opnd) { return opnd->_type == IR2_OPND_INV; }
inline __attribute__ ((always_inline))
int latxs_ir2_opnd_is_gpr(IR2_OPND *opnd) { return opnd->_type == IR2_OPND_GPR; }
inline __attribute__ ((always_inline))
int latxs_ir2_opnd_is_fpr(IR2_OPND *opnd) { return opnd->_type == IR2_OPND_FPR; }
inline __attribute__ ((always_inline))
int latxs_ir2_opnd_is_scr(IR2_OPND *opnd) { return opnd->_type == IR2_OPND_SCR; }
inline __attribute__ ((always_inline))
int latxs_ir2_opnd_is_fcsr(IR2_OPND *opnd) { return opnd->_type == IR2_OPND_FCSR; }
inline __attribute__ ((always_inline))
int latxs_ir2_opnd_is_cc(IR2_OPND *opnd) { return opnd->_type == IR2_OPND_CC; }
inline __attribute__ ((always_inline))
int latxs_ir2_opnd_is_label(IR2_OPND *opnd) { return opnd->_type == IR2_OPND_LABEL; }
inline __attribute__ ((always_inline))
int latxs_ir2_opnd_is_immd(IR2_OPND *opnd) { return opnd->_type == IR2_OPND_IMMD; }
inline __attribute__ ((always_inline))
int latxs_ir2_opnd_is_immh(IR2_OPND *opnd) { return opnd->_type == IR2_OPND_IMMH; }
inline __attribute__ ((always_inline))
int latxs_ir2_opnd_is_imm(IR2_OPND *opnd) { return opnd->_type == IR2_OPND_IMMD || opnd->_type == IR2_OPND_IMMH; }

/* Functions to convert IR2_INST to string */
int latxs_ir2_dump(IR2_INST *);
int latxs_ir2_to_string(IR2_INST *, char *);
int latxs_ir2_opnd_to_string(IR2_OPND *, char *, uint32_t);
const char *latxs_ir2_name(int value);

/* Function used during label disposing */
inline __attribute__ ((always_inline))
void latxs_ir2_opnd_convert_label_to_imm(IR2_OPND *opnd, int imm)
{
    lsassert(latxs_ir2_opnd_is_label(opnd));
    opnd->_type = IR2_OPND_IMMH;
    opnd->val = imm;
}

/*
 * Rewrite a PC-relative label operand into the immediate that LATS encodes.
 *
 * Control-flow branches store offs16 = (label_pc - branch_pc) >> 2.
 * LISA_PCADDI stores si20 = (label_pc - pcaddi_pc) >> 2 and computes
 * rd = PC + SignExtend(si20 << 2) at run time (LoongArch PCADDI semantics),
 * so the very same instruction-unit displacement is encoded; it just has a
 * wider field.  Reject out-of-range labels here (last point where the label
 * position is known) instead of silently wrapping si20 during encoding.
 *
 * This is a translator invariant, not a guest-visible condition, so fail hard
 * with a message rather than generating wrong code.
 */
inline __attribute__ ((always_inline))
void latxs_ir2_resolve_pcrel_label(IR2_INST *pir2, IR2_OPND *label_opnd,
        int disp)
{
    if (ir2_opcode(pir2) == LISA_PCADDI && !int32_in_int20(disp)) {
        fprintf(stderr,
                "IR2: LISA_PCADDI label displacement %d instructions does not "
                "fit its si20 field (+/-524287 instructions, +/2MiB)\n", disp);
        abort();
    }
    latxs_ir2_opnd_convert_label_to_imm(label_opnd, disp);
}

/* Functions to manage IR2 extension mode */
inline __attribute__ ((always_inline))
int latxs_ir2_opnd_is_mapping(IR2_OPND *opnd2, int *x86_gpr_num)
{
    lsassert(latxs_ir2_opnd_is_gpr(opnd2));
    int reg = latxs_ir2_opnd_reg(opnd2);
    if (24 <= reg && reg <= 31) {
        *x86_gpr_num = reg - 24;
        return true;
    }
    return false;
}

/* ------------------ IR2_INST ------------------ */

/* Fucntion to build IR2_INST */
inline __attribute__ ((always_inline))
void latxs_ir2_build(
        IR2_INST *ir2, IR2_OPCODE opcode,
        IR2_OPND *opnd0, IR2_OPND *opnd1,
        IR2_OPND *opnd2, IR2_OPND *opnd3)
{
    ir2->_opcode  = opcode;
    ir2->_opnd[0] = opnd0 ? *opnd0 : latxs_invalid_ir2_opnd;
    ir2->_opnd[1] = opnd1 ? *opnd1 : latxs_invalid_ir2_opnd;
    ir2->_opnd[2] = opnd2 ? *opnd2 : latxs_invalid_ir2_opnd;
    ir2->_opnd[3] = opnd3 ? *opnd3 : latxs_invalid_ir2_opnd;
}
inline __attribute__ ((always_inline))
void latxs_ir2_build0(IR2_INST *pir2, IR2_OPCODE opcode)
{
    latxs_ir2_build(pir2, opcode, NULL, NULL, NULL, NULL);
}
inline __attribute__ ((always_inline))
void latxs_ir2_build1(IR2_INST *pir2, IR2_OPCODE opcode, IR2_OPND *opnd0)
{
    latxs_ir2_build(pir2, opcode, opnd0, NULL, NULL, NULL);
}
inline __attribute__ ((always_inline))
void latxs_ir2_build2(IR2_INST *pir2, IR2_OPCODE opcode, IR2_OPND *opnd0, IR2_OPND *opnd1)
{
    latxs_ir2_build(pir2, opcode, opnd0, opnd1, NULL, NULL);
}
inline __attribute__ ((always_inline))
void latxs_ir2_build3(IR2_INST *pir2, IR2_OPCODE opcode, IR2_OPND *opnd0, IR2_OPND *opnd1, IR2_OPND *opnd2)
{
    latxs_ir2_build(pir2, opcode, opnd0, opnd1, opnd2, NULL);
}
inline __attribute__ ((always_inline))
void latxs_ir2_build4(IR2_INST *pir2, IR2_OPCODE opcode, IR2_OPND *opnd0, IR2_OPND *opnd1,
                IR2_OPND *opnd2, IR2_OPND *opnd3)
{
    latxs_ir2_build(pir2, opcode, opnd0, opnd1, opnd2, opnd3);
}

/* Fucntions ot access IR2_INST's fields */
inline __attribute__ ((always_inline))
int latxs_ir2_branch_label_index(IR2_INST *pir2)
{
    IR2_OPCODE opc = ir2_opcode(pir2);
    if (opc == LISA_PCADDI) {
        /* LISA_PCADDI rd, si20: the PC-relative label is operand 1 */
        return 1;
    }
    lsassert(latxs_ir2_opcode_is_branch(opc));
    switch (opc) {
    case LISA_BEQZ:
    case LISA_BNEZ: return 1; break;
    case LISA_B:
    case LISA_BL:   return 0; break;
    case LISA_BEQ:
    case LISA_BNE:
    case LISA_BLT:
    case LISA_BGE:
    case LISA_BLTU:
    case LISA_BGEU: return 2; break;
    case LISA_BCEQZ:
    case LISA_BCNEZ: return 1; break;
    default: return -1; break;
    }
    return -1;
}

IR2_OPND *latxs_ir2_branch_get_label(IR2_INST *pir2);

IR2_OPND *latxs_ir2_interrupt_get_label(TranslationBlock *tb, int succ_id);
void latxs_ir2_interrupt_set_label(TranslationBlock *tb, int succ_id);
/*
 *  jmp_offset : instruction number
 *  link : whether save pc + c at ra
 */
#define latxs_append_ir2_jmp_far(jmp_offset, link)                             \
    do {                                                                       \
        if (option_large_code_cache && !int32_in_int26(jmp_offset)) {          \
            int64_t upper, lower;                                              \
            upper = (((int64_t)(jmp_offset) + (1 << 15)) >> 16) << 44 >> 44;   \
            lower = ((int64_t)jmp_offset) << 48 >> 48;                         \
            IR2_OPND tmp = latxs_ra_alloc_itemp();                             \
            latxs_append_ir2_opnd1i(LISA_PCADDU18I, &tmp, upper);              \
            if (link) {                                                        \
                latxs_append_ir2_opnd2i(LISA_JIRL, &latxs_ra_ir2_opnd, &tmp,   \
                                        lower);                                \
            } else {                                                           \
                latxs_append_ir2_opnd2i(LISA_JIRL, &latxs_zero_ir2_opnd, &tmp, \
                                        lower);                                \
            }                                                                  \
            latxs_ra_free_temp(&tmp);                                          \
        } else {                                                               \
            if (link) {                                                        \
                latxs_append_ir2_opnda(LISA_BL, (int32_t)jmp_offset);          \
            } else {                                                           \
                latxs_append_ir2_opnda(LISA_B, (int32_t)jmp_offset);           \
            }                                                                  \
        }                                                                      \
    } while (0)

#define aot_append_ir2_jmp_far(jmp_offset, link, tmp_opnd)                     \
    do {                                                                       \
        if (option_large_code_cache && !int32_in_int26(jmp_offset - 1)) {      \
            int64_t upper, lower;                                              \
            upper = (((int64_t)(jmp_offset) + (1 << 15)) >> 16) << 44 >> 44;   \
            lower = ((int64_t)jmp_offset) << 48 >> 48;                         \
            latxs_append_ir2_opnd1i(LISA_PCADDU18I, &tmp_opnd, upper);         \
            if (link) {                                                        \
                latxs_append_ir2_opnd2i(LISA_JIRL, &latxs_ra_ir2_opnd,         \
                        &tmp_opnd, lower);                                     \
            } else {                                                           \
                latxs_append_ir2_opnd2i(LISA_JIRL, &latxs_zero_ir2_opnd,       \
                        &tmp_opnd, lower);                                     \
            }                                                                  \
        } else {                                                               \
            if (option_large_code_cache) {                                     \
                jmp_offset--;                                                  \
                latxs_append_ir2_opnd0_(lisa_nop);                             \
            }                                                                  \
            if (link) {                                                        \
                latxs_append_ir2_opnda(LISA_BL, (int32_t)jmp_offset);          \
            } else {                                                           \
                latxs_append_ir2_opnda(LISA_B, (int32_t)jmp_offset);           \
            }                                                                  \
        }                                                                      \
    } while (0)

#define aot_append_ir2_jmp_near(jmp_offset, link)                              \
    do {                                                                       \
        if (option_large_code_cache && !int32_in_int26(jmp_offset)) {          \
            fprintf(stderr, "aot_append_ir2_jmp_near() cannot jmmp too far "); \
            fprintf(stderr, "jmp_offset %dMB\n", jmp_offset / 1024 / 256);    \
            exit(-1);                                                          \
        } else {                                                               \
            if (link) {                                                        \
                latxs_append_ir2_opnda(LISA_BL, (int32_t)jmp_offset);          \
            } else {                                                           \
                latxs_append_ir2_opnda(LISA_B, (int32_t)jmp_offset);           \
            }                                                                  \
        }                                                                      \
    } while (0)

#define tb_sc_table_jmp(tb, rid, name, curr_ir2_size, link, aot_rel_kind)                                   \
    do {                                                                                                    \
        int ins_offset = (GET_SC_TABLE(rid, name) - ((ADDR)tb->tc.ptr + (curr_ir2_size))) >> 2;             \
        if (option_aot_save) {                                                                              \
            assert(tb);                                                                                     \
            IR2_OPND tmp_opnd = latxs_ra_alloc_itemp();                                                     \
            add_curr_rel_entry(tb, aot_rel_kind, curr_ir2_size, 2, -1,                                      \
                    link | tmp_opnd.val << 16);                                                             \
            aot_append_ir2_jmp_far(ins_offset, link, tmp_opnd);                                             \
            latxs_ra_free_temp(&tmp_opnd);                                                                  \
        } else {                                                                                            \
            latxs_append_ir2_jmp_far(ins_offset, link);                                                     \
        }                                                                                                   \
    } while (0)                                                                                             \

#define tb_sc_table_offset_jmp(tb, rid, name, tcg_region_offset, curr_ir2_size, link, aot_rel_kind)         \
    do {                                                                                                    \
        int ins_offset = (GET_SC_TABLE_OFFSET(rid, name, tcg_region_offset)                                 \
                - ((ADDR)tb->tc.ptr + (curr_ir2_size))) >> 2;                                               \
        if (option_aot_save) {                                                                              \
            assert(tb);                                                                                     \
            add_curr_rel_entry(tb, aot_rel_kind, curr_ir2_size, 1, -1, link);                               \
            if (option_large_code_cache && !int32_in_int26(ins_offset)) {                                   \
                ins_offset = reset_region_offset(ins_offset, rid);                                          \
            }                                                                                               \
            aot_append_ir2_jmp_near(ins_offset, link);                                                      \
        } else {                                                                                            \
            latxs_append_ir2_jmp_far(ins_offset, link);                                                     \
        }                                                                                                   \
    } while (0)

/* Functions to identify IR2_INST's type */
inline __attribute__ ((always_inline))
bool latxs_ir2_opcode_is_load(IR2_OPCODE opcode)
{
    if (opcode >= LISA_LD_B && opcode <=  LISA_LD_D) {
    return true;
    }
    if (opcode >=  LISA_LD_BU && opcode <=  LISA_LD_WU) {
        return true;
    }
    if (opcode == LISA_LL_W || opcode == LISA_LL_D) {
        return true;
    }
    if (opcode == LISA_LDPTR_W || opcode == LISA_LDPTR_D) {
        return true;
    }
    if (opcode == LISA_FLD_S || opcode == LISA_FLD_D) {
        return true;
    }
    if (opcode == LISA_VLD || opcode == LISA_XVLD){
        return true;
    }
    if (opcode >= LISA_LDL_W && opcode <= LISA_LDR_D){
        return true;
    }
    if (opcode >= LISA_VLDREPL_D && opcode <= LISA_VLDREPL_B){
        return true;
    }
    if (opcode >= LISA_XVLDREPL_D && opcode <= LISA_XVLDREPL_B){
        return true;
    }
    if (opcode == LISA_PRELD) {
        return true;
    }
    return false;
}
inline __attribute__ ((always_inline))
bool latxs_ir2_opcode_is_store(IR2_OPCODE opcode)
{
    if (opcode >= LISA_ST_B && opcode <= LISA_ST_D) {
        return true;
    }
    if (opcode >= LISA_STL_W && opcode <= LISA_STR_D) {
        return true;
    }
    if (opcode == LISA_SC_D || opcode == LISA_SC_W) {
        return true;
    }
    if (opcode == LISA_STPTR_W || opcode == LISA_STPTR_D) {
        return true;
    }
    if (opcode == LISA_FST_S || opcode == LISA_FST_D) {
        return true;
    }
    if (opcode == LISA_XVST || opcode == LISA_VST){
        return true;
    }
    return false;
}
inline __attribute__ ((always_inline))
bool latxs_ir2_opcode_is_convert(IR2_OPCODE opcode)
{
    switch (opcode) {
    case LISA_FCVT_D_S:
    case LISA_FCVT_S_D:
    case LISA_FFINT_D_W:
    case LISA_FFINT_D_L:
    case LISA_FFINT_S_W:
    case LISA_FFINT_S_L:
    case LISA_FTINT_L_D:
    case LISA_FTINT_L_S:
    case LISA_FTINT_W_D:
    case LISA_FTINT_W_S:
        return true;
    default:
        return false;
    }
}
inline __attribute__ ((always_inline))
bool latxs_ir2_opcode_is_fcmp(IR2_OPCODE opcode)
{
    return (opcode == LISA_FCMP_COND_S || opcode == LISA_FCMP_COND_D);
}

bool latxs_ir2_op_check(IR2_INST *pir2);

#include "ir2-out.h"

IR2_INST *latxs_append_ir2_opnd3i(IR2_OPCODE opcode,
        IR2_OPND *opnd0, IR2_OPND *opnd1,
        IR2_OPND *opnd2, int imm);
IR2_INST *latxs_append_ir2_opnd1ii(IR2_OPCODE opcode,
        IR2_OPND *opnd0, int imm1, int imm2);
IR2_INST *latxs_append_ir2_opnd2ii(IR2_OPCODE opcode,
        IR2_OPND *opnd0, IR2_OPND *opnd1,
        int imm1, int imm2);
IR2_INST *latxs_append_ir2_opnd3(IR2_OPCODE opcode,
        IR2_OPND  *opnd0, IR2_OPND *opnd1, IR2_OPND *opnd2);
IR2_INST *latxs_append_ir2_opnd4(IR2_OPCODE opcode,
    IR2_OPND  *opnd0, IR2_OPND *opnd1, IR2_OPND *opnd2, IR2_OPND *opnd3);
IR2_INST *latxs_append_ir2_opnd2i(IR2_OPCODE opcode,
        IR2_OPND  *opnd0, IR2_OPND *opnd1, int32_t imm);
IR2_INST *latxs_append_ir2_opnd2(IR2_OPCODE opcode,
        IR2_OPND  *opnd0, IR2_OPND *opnd1);
IR2_INST *latxs_append_ir2_opnd1i(IR2_OPCODE opcode,
        IR2_OPND  *opnd0, int32 imm);
IR2_INST *latxs_append_ir2_opnd1(IR2_OPCODE opcode,
        IR2_OPND  *opnd0);
IR2_INST *latxs_append_ir2_opndi(IR2_OPCODE opcode,
        int32_t imm);
IR2_INST *latxs_append_ir2_opnda(IR2_OPCODE opcode,
        ADDR addr);
IR2_INST *latxs_append_ir2_opnd0(
        IR2_OPCODE opcode);
int latxs_ir2_opnd_is_reg_temp(IR2_OPND *opnd);
int latxs_ir2_itemp_index(IR2_OPND *opnd);
IR2_INST *latxs_append_ir2_opnd2_(IR2_OPCODE opcode,
        IR2_OPND  *opnd0, IR2_OPND *opnd1);
IR2_INST *latxs_append_ir2_opnd1_(IR2_OPCODE opcode,
        IR2_OPND  *opnd0);
IR2_INST *latxs_append_ir2_opnd0_(IR2_OPCODE opcode);

IR2_INST *latxs_append_label(IR2_OPND *label);
IR2_INST *latxs_append_label_with_name(IR2_OPND *label, const char *name);

#ifdef CONFIG_LATX_PROFILER

void per_tb_count(void *area, int inc);
void latxs_profile_address(IR2_OPND *dest, TranslationBlock *tb, void *area);

#endif

#endif
