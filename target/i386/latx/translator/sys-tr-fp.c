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
#include "latx-sys-hamt-dmw.h"
#include "sys-aot.h"

void latxs_sys_fp_register_ir1(void)
{
    latxs_register_ir1(X86_INS_WAIT);

    latxs_register_ir1(X86_INS_FSETPM);

    latxs_register_ir1(X86_INS_FXSAVE);
    latxs_register_ir1(X86_INS_FXSAVE64);
    latxs_register_ir1(X86_INS_FXRSTOR);
    latxs_register_ir1(X86_INS_FXRSTOR64);
    latxs_register_ir1(X86_INS_FNSTENV);
    latxs_register_ir1(X86_INS_FLDENV);
    latxs_register_ir1(X86_INS_FNSAVE);
    latxs_register_ir1(X86_INS_FRSTOR);
    latxs_register_ir1(X86_INS_XSAVE);
    latxs_register_ir1(X86_INS_XRSTOR);
    latxs_register_ir1(X86_INS_XSAVEOPT);
    latxs_register_ir1(X86_INS_FNINIT);
    latxs_register_ir1(X86_INS_FFREE);
    latxs_register_ir1(X86_INS_FFREEP);
    latxs_register_ir1(X86_INS_FINCSTP);
    latxs_register_ir1(X86_INS_FDECSTP);

    latxs_register_ir1(X86_INS_XGETBV);
    latxs_register_ir1(X86_INS_XSETBV);
}

#if defined(LATX_SYS_FCSR_EXCP)
/* TODO: support fpu exception */
static void latxs_tr_gen_fwait(void)
{
    helper_cfg_t cfg = all_helper_cfg;
    latxs_tr_gen_call_to_helper1_cfg((ADDR)helper_fwait, cfg);
}
#endif

bool latxs_translate_wait(IR1_INST *pir1)
{
    lsenv->tr_data->sys_eob_can_link = 1;

    if (option_soft_fpu) {
        return latxs_translate_wait_softfpu(pir1);
    }
    if (latxs_tr_gen_excp_check(pir1)) {
        return true;
    }

#if defined(LATX_SYS_FCSR_EXCP)
    latxs_tr_gen_fwait();
#endif

    return true;
}

static IR2_OPND latxs_tr_get_real_curr_top(void)
{
    lsassert(!option_soft_fpu);
    IR2_OPND top = latxs_ra_alloc_itemp();

    lsassert(option_lsfpu);

    latxs_append_ir2_opnd1(LISA_X86MFTOP, &top);
    latxs_append_ir2_opnd2i(LISA_ANDI, &top, &top, 0x7);

    return top;
}

bool latxs_translate_fsetpm(IR1_INST *pir1)
{
    if (latxs_tr_gen_fp_common_excp_check(pir1)) {
        return true;
    }
    /* fsetpm (287 only, just do nop here) */
    return true;
}

bool latxs_translate_fxsave(IR1_INST *pir1)
{
    if (option_soft_fpu) {
        return latxs_translate_fxsave_softfpu(pir1);
    }
    if (latxs_tr_gen_excp_check(pir1)) {
        return true;
    }
#ifdef CONFIG_LATX_OPT_NOFP
    latxs_gen_nofp_sync_fprs_to_env();
#endif

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    lsassertm(ir1_opnd_is_mem(opnd0), "fxsave opnd0 is not mem.\n");

    /* 1. get memory address */
    IR2_OPND mem_base;
    int mem_offset;
    latxs_convert_mem_opnd(&mem_base, &mem_offset, opnd0, -1);
    if (mem_offset != 0) {
        latxs_append_ir2_opnd2i(LISA_ADDI_D, &mem_base, &mem_base, mem_offset);
#ifdef LATXS_IMM_CACHE_ENABLE
        imm_cache_addi_update(mem_offset);
#endif
    }
    IR2_OPND addr_opnd = mem_base;

    /* 2. save complete context */
    helper_cfg_t hp_cfg;
    hp_cfg.cfg.sv_allgpr = 1;
    hp_cfg.cfg.sv_eflags = 1;
    hp_cfg.cfg.cvt_fp80  = 1;
    latxs_tr_gen_call_to_helper_prologue_cfg(hp_cfg);
    /*
     * 3. call helper_fxsave
     *
     * target/i386/fpu_helper.c
     * void helper_fxsave(
     *      CPUX86State *env,
     *      target_ulong ptr)
     * >> All floating point registers will be used
     * >> 80-bit FP will be used
     */
    /* 3.1 arg1 : memory address */
    latxs_append_ir2_opnd2_(lisa_mov, &latxs_arg1_ir2_opnd,
                                      &addr_opnd);
    /* 3.2 arg0 : env */
    latxs_append_ir2_opnd2_(lisa_mov, &latxs_arg0_ir2_opnd,
                                      &latxs_env_ir2_opnd);
    if (hamt_enabled()) {
        latxs_tr_dmw_out_fix_mem(&latxs_arg0_ir2_opnd);
    }
    /* 3.3 call helper */
    latxs_tr_gen_call_to_helper((ADDR)helper_fxsave, HELPER_FXSAVE);
    /* 4. restore context */
    latxs_tr_gen_call_to_helper_epilogue_cfg(hp_cfg);
    return true;
}

/* End of TB in system-mode */
bool latxs_translate_fxrstor(IR1_INST *pir1)
{
    if (option_soft_fpu) {
        return latxs_translate_fxrstor_softfpu(pir1);
    }
    if (latxs_tr_gen_excp_check(pir1)) {
        return true;
    }

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    lsassertm(ir1_opnd_is_mem(opnd0), "fxrstor opnd0 is not mem.\n");

    /* 1. get memory address */
    IR2_OPND mem_base;
    int mem_offset;
    latxs_convert_mem_opnd(&mem_base, &mem_offset, opnd0, -1);
    if (mem_offset != 0) {
        latxs_append_ir2_opnd2i(LISA_ADDI_D, &mem_base, &mem_base, mem_offset);
#ifdef LATXS_IMM_CACHE_ENABLE
        imm_cache_addi_update(mem_offset);
#endif

    }
    IR2_OPND addr_opnd = mem_base;

    /* 2. save complete context */
    helper_cfg_t hp_cfg;
    hp_cfg.cfg.sv_allgpr = 1;
    hp_cfg.cfg.sv_eflags = 1;
    hp_cfg.cfg.cvt_fp80  = 1;
    latxs_tr_gen_call_to_helper_prologue_cfg(hp_cfg);
    /*
     * 3. call helper_fxrstor
     *
     * target/i386/fpu_helper.c
     * void helper_fxrstor(
     *      CPUX86State *env,
     *      target_ulong ptr)
     * >> All floating point registers will be used
     * >> 80-bit FP will be used
     */
    /* 3.1 arg1 : memory address */
    latxs_append_ir2_opnd2_(lisa_mov, &latxs_arg1_ir2_opnd,
                                      &addr_opnd);
    /* 3.2 arg0 : env */
    latxs_append_ir2_opnd2_(lisa_mov, &latxs_arg0_ir2_opnd,
                                      &latxs_env_ir2_opnd);
    if (hamt_enabled()) {
        latxs_tr_dmw_out_fix_mem(&latxs_arg0_ir2_opnd);
    }
    /* 3.3 call helper */
    latxs_tr_gen_call_to_helper((ADDR)helper_fxrstor, HELPER_FXRSTOR);
    /* 4. restore context */
    latxs_tr_gen_call_to_helper_epilogue_cfg(hp_cfg);
#ifdef CONFIG_LATX_OPT_NOFP
    latxs_gen_nofp_sync_fprs_from_env();
#endif

    return true;
}

bool latxs_translate_fnstenv(IR1_INST *pir1)
{
    if (option_soft_fpu) {
        return latxs_translate_fnstenv_softfpu(pir1);
    }
    if (latxs_tr_gen_fp_common_excp_check(pir1)) {
        return true;
    }

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
#ifdef CONFIG_SOFTMMU
    lsassertm_illop(ir1_addr(pir1), ir1_opnd_is_mem(opnd0),
            "fnstenv operand is not mem.\n");
#endif

    int data_size = latxs_ir1_data_size(pir1);
    int data32 = data_size == 32 ? 1 : 0;
    /* 1. get memory address */
    IR2_OPND mem_base;
    int mem_offset;
    latxs_convert_mem_opnd(&mem_base, &mem_offset, opnd0, -1);
    if (mem_offset != 0) {
        latxs_append_ir2_opnd2i(LISA_ADDI_D, &mem_base, &mem_base, mem_offset);
#ifdef LATXS_IMM_CACHE_ENABLE
        imm_cache_addi_update(mem_offset);
#endif
    }
    IR2_OPND address = mem_base;

    /* 2. save context */
    helper_cfg_t cfg = default_helper_cfg;
    latxs_tr_gen_call_to_helper_prologue_cfg(cfg);
    /*
     * 3. call helper_fstenv
     *
     * target/i386/fpu_helper.c
     * void helper_fstenv(
     *      CPUX86State *env,
     *      target_ulong ptr,
     *      int data32)
     * >> Control Word : env->fpuc        => 16-bit
     * >> Status  Word : env->fpus        => 16-bit
     * >> Tag     Word : env->fptags[0:7] => 16-bit tag word
     * >> FIP,FOP,.... : zero
     */
    /* 3.1 arg1: address */
    latxs_append_ir2_opnd2_(lisa_mov, &latxs_arg1_ir2_opnd,
                                      &address);
    /* 3.2 arg0: env */
    latxs_append_ir2_opnd2_(lisa_mov, &latxs_arg0_ir2_opnd,
                                      &latxs_env_ir2_opnd);
    if (hamt_enabled()) {
        latxs_tr_dmw_out_fix_mem(&latxs_arg0_ir2_opnd);
    }
    /* 3.3 arg2: data32 */
    latxs_append_ir2_opnd2i(LISA_ORI, &latxs_arg2_ir2_opnd,
                                      &latxs_zero_ir2_opnd, data32);
    /* 3.4 call helper_frstor */
    latxs_tr_gen_call_to_helper((ADDR)helper_fstenv, HELPER_FSTENV);
    /* 4. restore context */
    latxs_tr_gen_call_to_helper_epilogue_cfg(cfg);
    return true;
}

/* End of TB in system-mode */
bool latxs_translate_fldenv(IR1_INST *pir1)
{
    if (option_soft_fpu) {
        return latxs_translate_fldenv_softfpu(pir1);
    }
    if (latxs_tr_gen_fp_common_excp_check(pir1)) {
        return true;
    }

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
#ifdef CONFIG_SOFTMMU
    lsassertm_illop(ir1_addr(pir1), ir1_opnd_is_mem(opnd0),
            "fldtenv operand is not mem.\n");
#endif

    int data_size = latxs_ir1_data_size(pir1);
    int data32 = data_size == 32 ? 1 : 0;
    /* 1. get memory address */
    IR2_OPND mem_base;
    int mem_offset;
    latxs_convert_mem_opnd(&mem_base, &mem_offset, opnd0, -1);
    if (mem_offset != 0) {
        latxs_append_ir2_opnd2i(LISA_ADDI_D, &mem_base, &mem_base, mem_offset);
#ifdef LATXS_IMM_CACHE_ENABLE
        imm_cache_addi_update(mem_offset);
#endif
    }
    IR2_OPND address = mem_base;

    /* 2. save context */
    helper_cfg_t cfg = default_helper_cfg;
    latxs_tr_gen_call_to_helper_prologue_cfg(cfg);
    /*
     * 3. call helper_fldenv
     *
     * target/i386/fpu_helper.c
     * void helper_fldenv(
     *      CPUX86State *env,
     *      target_ulong ptr,
     *      int data32)
     * >> Control Word : env->fpuc        => 16-bit
     * >> Status  Word : env->fpus        => 16-bit
     * >> Tag     Word : env->fptags[0:7] => 16-bit tag word
     * >> FIP,FOP,.... : zero
     */
    /* 3.1 arg1: address */
    latxs_append_ir2_opnd2_(lisa_mov, &latxs_arg1_ir2_opnd,
                                      &address);
    /* 3.2 arg0: env */
    latxs_append_ir2_opnd2_(lisa_mov, &latxs_arg0_ir2_opnd,
                                      &latxs_env_ir2_opnd);
    if (hamt_enabled()) {
        latxs_tr_dmw_out_fix_mem(&latxs_arg0_ir2_opnd);
    }
    /* 3.3 arg2: data32 */
    latxs_append_ir2_opnd2i(LISA_ORI, &latxs_arg2_ir2_opnd,
                                      &latxs_zero_ir2_opnd, data32);
    /* 3.4 call helper_frstor */
    latxs_tr_gen_call_to_helper((ADDR)helper_fldenv, HELPER_FLDENV);
    /* 4. restore context */
    latxs_tr_gen_call_to_helper_epilogue_cfg(cfg);

    return true;
}

/* End of TB in system-mode */
bool latxs_translate_fnsave(IR1_INST *pir1)
{
    if (option_soft_fpu) {
        return latxs_translate_fnsave_softfpu(pir1);
    }
    if (latxs_tr_gen_fp_common_excp_check(pir1)) {
        return true;
    }
#ifdef CONFIG_LATX_OPT_NOFP
    latxs_gen_nofp_sync_fprs_to_env();
#endif

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
#ifdef CONFIG_SOFTMMU
    lsassertm_illop(ir1_addr(pir1), ir1_opnd_is_mem(opnd0),
            "fnsave operand is not mem.\n");
#endif

    int data_size = latxs_ir1_data_size(pir1);
    int data32 = data_size == 32 ? 1 : 0;
    /* 1. get memory address */
    IR2_OPND mem_base;
    int mem_offset;
    latxs_convert_mem_opnd(&mem_base, &mem_offset, opnd0, -1);
    if (mem_offset != 0) {
        latxs_append_ir2_opnd2i(LISA_ADDI_D, &mem_base, &mem_base, mem_offset);
#ifdef LATXS_IMM_CACHE_ENABLE
        imm_cache_addi_update(mem_offset);
#endif
    }
    IR2_OPND address = mem_base;

    /* 2. save context, convert to 80fp */
    helper_cfg_t cfg = all_helper_cfg;
    latxs_tr_gen_call_to_helper_prologue_cfg(cfg);
    /*
     * call helper_fsave
     *
     * void helper_fsave(
     *      CPUX86State *env,
     *      target_ulong ptr,
     *      int data32)
     * >> Control Word : env->fpuc        => 16-bit
     * >> Status  Word : env->fpus        => 16-bit
     * >> Tag     Word : env->fptags[0:7] => 16-bit tag word
     * >> FIP,FOP,.... : zero
     * >> 80-bit  FPRs : env->fpregs[0:7] => 80-bit * 8
     */
    /* 3.1 arg1: address */
    latxs_append_ir2_opnd2_(lisa_mov, &latxs_arg1_ir2_opnd,
                                      &address);
    /* 3.2 arg0: env */
    latxs_append_ir2_opnd2_(lisa_mov, &latxs_arg0_ir2_opnd,
                                      &latxs_env_ir2_opnd);
    if (hamt_enabled()) {
        latxs_tr_dmw_out_fix_mem(&latxs_arg0_ir2_opnd);
    }
    /* 3.3 arg2: data32 */
    latxs_append_ir2_opnd2i(LISA_ORI, &latxs_arg2_ir2_opnd,
                                      &latxs_zero_ir2_opnd, data32);
    /* 3.4 call helper_frstor */
    latxs_tr_gen_call_to_helper((ADDR)helper_fsave, HELPER_FSAVE);
    /* 4. restore context */
    latxs_tr_gen_call_to_helper_epilogue_cfg(cfg);

    return true;
}

/* End of TB in system-mode */
bool latxs_translate_frstor(IR1_INST *pir1)
{
    if (option_soft_fpu) {
        return latxs_translate_frstor_softfpu(pir1);
    }
    if (latxs_tr_gen_fp_common_excp_check(pir1)) {
        return true;
    }

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
#ifdef CONFIG_SOFTMMU
    lsassertm_illop(ir1_addr(pir1), ir1_opnd_is_mem(opnd0),
            "frstor operand is not mem.\n");
#endif

    int data_size = latxs_ir1_data_size(pir1);
    int data32 = data_size == 32 ? 1 : 0;
    /* 1. get memory address */
    IR2_OPND mem_base;
    int mem_offset;
    latxs_convert_mem_opnd(&mem_base, &mem_offset, opnd0, -1);
    if (mem_offset != 0) {
        latxs_append_ir2_opnd2i(LISA_ADDI_D, &mem_base, &mem_base, mem_offset);
#ifdef LATXS_IMM_CACHE_ENABLE
        imm_cache_addi_update(mem_offset);
#endif
    }
    IR2_OPND address = mem_base;

    /* 2. save complete context, along with fp80 convert */
    helper_cfg_t cfg = all_helper_cfg;
    latxs_tr_gen_call_to_helper_prologue_cfg(cfg);
    /*
     * 3. call helper_frstor
     *
     * target/i386/fpu_helper.c
     * void helper_frstor(
     *      CPUX86State *env,
     *      target_ulong ptr,
     *      int data32)
     * >> Control Word : env->fpuc        => 16-bit
     * >> Status  Word : env->fpus        => 16-bit
     * >> Tag     Word : env->fptags[0:7] => 16-bit tag word
     * >> FIP,FOP,.... : zero
     * >> 80-bit  FPRs : env->fpregs[0:7] => 80-bit * 8
     */
    /* 3.1 arg1: address */
    latxs_append_ir2_opnd2_(lisa_mov, &latxs_arg1_ir2_opnd,
                                      &address);
    /* 3.2 arg0: env */
    latxs_append_ir2_opnd2_(lisa_mov, &latxs_arg0_ir2_opnd,
                                      &latxs_env_ir2_opnd);
    if (hamt_enabled()) {
        latxs_tr_dmw_out_fix_mem(&latxs_arg0_ir2_opnd);
    }
    /* 3.3 arg2: data32 */
    latxs_append_ir2_opnd2i(LISA_ORI, &latxs_arg2_ir2_opnd,
                                      &latxs_zero_ir2_opnd, data32);
    /* 3.4 call helper_frstor */
    latxs_tr_gen_call_to_helper((ADDR)helper_frstor, HELPER_FRSTOR);
    /* 4. restore context */
    latxs_tr_gen_call_to_helper_epilogue_cfg(cfg);
#ifdef CONFIG_LATX_OPT_NOFP
    latxs_gen_nofp_sync_fprs_from_env();
#endif

    return true;
}

bool latxs_translate_xgetbv(IR1_INST *pir1)
{
    if (latxs_tr_gen_excp_check(pir1)) {
        return true;
    }

    IR2_OPND ecx_opnd = latxs_ra_alloc_gpr(ecx_index);
    /*
     * 1. call helper_xgetbv
     *
     * target/i386/fpu_helper.c
     * uint64_t helper_xgetbv(
     *      CPUX86State *env,
     *      uint32_t ecx)
     * > Exception might be generated
     */
    helper_cfg_t cfg = default_helper_cfg;
    /* 1.1 save context */
    latxs_tr_gen_call_to_helper_prologue_cfg(cfg);
    /* 1.2 arg1 : ecx */
    latxs_append_ir2_opnd2_(lisa_mov32z, &latxs_arg1_ir2_opnd,
                                         &ecx_opnd);
    /* 1.3 arg0 : env */
    latxs_append_ir2_opnd2_(lisa_mov, &latxs_arg0_ir2_opnd,
                                      &latxs_env_ir2_opnd);
    if (hamt_enabled()) {
        latxs_tr_dmw_out_fix_mem(&latxs_arg0_ir2_opnd);
    }
    /* 1.4 call helper */
    latxs_tr_gen_call_to_helper((ADDR)helper_xgetbv, HELPER_XGETBV);
    /* 1.5 restore context */
    latxs_tr_gen_call_to_helper_epilogue_cfg(cfg);
    /* 2. get return value EDX:EAX */
    IR2_OPND eax_value = latxs_ra_alloc_itemp();
    latxs_append_ir2_opnd2_(lisa_mov32z, &eax_value,
                                         &latxs_ret0_ir2_opnd);

    IR2_OPND edx_value = latxs_ra_alloc_itemp();
    latxs_append_ir2_opnd2i(LISA_SRLI_D, &latxs_ret0_ir2_opnd,
                                         &latxs_ret0_ir2_opnd, 32);
    latxs_append_ir2_opnd2_(lisa_mov32z, &edx_value,
                                         &latxs_ret0_ir2_opnd);
    /* 3. save into eax and edx */
    latxs_store_ir2_to_ir1(&eax_value, &eax_ir1_opnd);
    latxs_store_ir2_to_ir1(&edx_value, &edx_ir1_opnd);
    return true;
}

/* End of TB in system-mode */
bool latxs_translate_xsetbv(IR1_INST *pir1)
{
    if (latxs_tr_gen_excp_check(pir1)) {
        return true;
    }

    IR2_OPND ecx_opnd = latxs_ra_alloc_gpr(ecx_index);
    IR2_OPND eax_opnd = latxs_ra_alloc_gpr(eax_index);
    IR2_OPND edx_opnd = latxs_ra_alloc_gpr(edx_index);
    /* 1. get EDX:EAX */
    IR2_OPND mask = latxs_ra_alloc_itemp();
    IR2_OPND eax_opnd32 = latxs_ra_alloc_itemp();

    latxs_append_ir2_opnd2_(lisa_mov32z, &mask, &edx_opnd);
    latxs_append_ir2_opnd2i(LISA_SLLI_D, &mask, &mask, 32);
    latxs_append_ir2_opnd2_(lisa_mov32z, &eax_opnd32, &eax_opnd);
    latxs_append_ir2_opnd3(LISA_OR, &mask, &mask, &eax_opnd32);
    /*
     * 2. call helper_xsetbv
     *
     * target/i386/fpu_helper.c
     * void helper_xsetbv(
     *      CPUX86State *env,
     *      uint32_t ecx,
     *      uint64_t mask)
     * > hflags might be changed, so this is an EOB
     */
    helper_cfg_t cfg = default_helper_cfg;
    /* 2.1 save context */
    latxs_tr_gen_call_to_helper_prologue_cfg(cfg);
    /* 2.2 arg2: mask */
    latxs_append_ir2_opnd2_(lisa_mov, &latxs_arg2_ir2_opnd,
                                      &mask);
    /* 2.3 arg1: ecx */
    latxs_append_ir2_opnd2_(lisa_mov32z, &latxs_arg1_ir2_opnd,
                                         &ecx_opnd);
    /* 2.4 arg0 : env */
    latxs_append_ir2_opnd2_(lisa_mov, &latxs_arg0_ir2_opnd,
                                      &latxs_env_ir2_opnd);
    if (hamt_enabled()) {
        latxs_tr_dmw_out_fix_mem(&latxs_arg0_ir2_opnd);
    }
    /* 2.5 call helper */
    latxs_tr_gen_call_to_helper((ADDR)helper_xsetbv, HELPER_XSETBV);
    /* 2.6 restore context */
    latxs_tr_gen_call_to_helper_epilogue_cfg(cfg);
    return true;
}

bool latxs_translate_xsave(IR1_INST *pir1)
{
    if (latxs_tr_gen_excp_check(pir1)) {
        return true;
    }
#ifdef CONFIG_LATX_OPT_NOFP
    latxs_gen_nofp_sync_fprs_to_env();
#endif

    IR1_OPND *opnd = ir1_get_opnd(pir1, 0);
#ifdef CONFIG_SOFTMMU
    lsassertm_illop(ir1_addr(pir1), ir1_opnd_is_mem(opnd),
            "xsave operand is not MEM.\n");
#endif

    /* 1. get memory address from operand */
    IR2_OPND mem_base;
    int mem_offset;
    latxs_convert_mem_opnd(&mem_base, &mem_offset, opnd, -1);
    if (mem_offset != 0) {
        latxs_append_ir2_opnd2i(LISA_ADDI_D, &mem_base, &mem_base, mem_offset);
#ifdef LATXS_IMM_CACHE_ENABLE
        imm_cache_addi_update(mem_offset);
#endif
    }
    IR2_OPND mem_addr_opnd = mem_base;

    /* 2. get rfbm from eax and edx */
    IR2_OPND rfbm_opnd = latxs_ra_alloc_itemp();
    IR2_OPND eax_opnd = latxs_ra_alloc_gpr(eax_index);
    IR2_OPND edx_opnd = latxs_ra_alloc_gpr(edx_index);
    IR2_OPND eax_opnd32 = latxs_ra_alloc_itemp();
    /* eax might be sign extension */

    latxs_append_ir2_opnd2i(LISA_SLLI_D, &rfbm_opnd, &edx_opnd, 32);
    latxs_append_ir2_opnd2_(lisa_mov32z, &eax_opnd32, &eax_opnd);
    latxs_append_ir2_opnd3(LISA_OR, &rfbm_opnd, &eax_opnd32, &rfbm_opnd);
    /*
     * 3. call helper_xsave
     *
     * void helper_xsave(
     *      CPUX86State *env,
     *      target_ulong ptr,
     *      uint64_t rfbm)
     * >> FPU might be saved
     */
    helper_cfg_t cfg = all_helper_cfg;
    /* 3.1 save context */
    latxs_tr_gen_call_to_helper_prologue_cfg(cfg);
    /* 3.2 arg2 : rfbm */
    latxs_append_ir2_opnd2_(lisa_mov, &latxs_arg2_ir2_opnd,
                                      &rfbm_opnd);
    /* 3.3 arg0 : env */
    latxs_append_ir2_opnd2_(lisa_mov, &latxs_arg0_ir2_opnd,
                                      &latxs_env_ir2_opnd);
    if (hamt_enabled()) {
        latxs_tr_dmw_out_fix_mem(&latxs_arg0_ir2_opnd);
    }
    /* 3.4 arg1 : memory address */
    latxs_append_ir2_opnd2_(lisa_mov, &latxs_arg1_ir2_opnd,
                                      &mem_addr_opnd);
    /* 3.5 call helper xsave */
    latxs_tr_gen_call_to_helper((ADDR)helper_xsave, HELPER_XSAVE);
    /* 3.6 restore context */
    latxs_tr_gen_call_to_helper_epilogue_cfg(cfg);
    return true;
}

/* End of TB in system-mode */
bool latxs_translate_xrstor(IR1_INST *pir1)
{
    if (latxs_tr_gen_excp_check(pir1)) {
        return true;
    }

    IR1_OPND *opnd = ir1_get_opnd(pir1, 0);
#ifdef CONFIG_SOFTMMU
    lsassertm_illop(ir1_addr(pir1), ir1_opnd_is_mem(opnd),
            "xsave operand is not MEM.\n");
#endif

    /* 1. get memory address from operand */
    IR2_OPND mem_base;
    int mem_offset;
    latxs_convert_mem_opnd(&mem_base, &mem_offset, opnd, -1);
    if (mem_offset != 0) {
        latxs_append_ir2_opnd2i(LISA_ADDI_D, &mem_base, &mem_base, mem_offset);
#ifdef LATXS_IMM_CACHE_ENABLE
        imm_cache_addi_update(mem_offset);
#endif
    }
    IR2_OPND mem_addr_opnd = mem_base;

    /* 2. get rfbm from eax and edx */
    IR2_OPND rfbm_opnd = latxs_ra_alloc_itemp();
    IR2_OPND eax_opnd = latxs_ra_alloc_gpr(eax_index);
    IR2_OPND edx_opnd = latxs_ra_alloc_gpr(edx_index);
    IR2_OPND eax_opnd32 = latxs_ra_alloc_itemp();
    /* eax might be sign extension */

    latxs_append_ir2_opnd2i(LISA_SLLI_D, &rfbm_opnd, &edx_opnd, 32);
    latxs_append_ir2_opnd2_(lisa_mov32z, &eax_opnd32, &eax_opnd);
    latxs_append_ir2_opnd3(LISA_OR, &rfbm_opnd, &eax_opnd32, &rfbm_opnd);
    /*
     * 3. call helper_xrstor
     *
     * void helper_xrstor(
     *      CPUX86State *env,
     *      target_ulong ptr,
     *      uint64_t rfbm)
     * >> FPU might be restored
     * >> XRSTOR is how MPX is enabled, we need to end the TB
     */
    helper_cfg_t cfg = all_helper_cfg;
    /* 3.1 save context */
    latxs_tr_gen_call_to_helper_prologue_cfg(cfg);
    /* 3.2 arg2 : rfbm */
    latxs_append_ir2_opnd2_(lisa_mov, &latxs_arg2_ir2_opnd,
                                      &rfbm_opnd);
    /* 3.3 arg0 : env */
    latxs_append_ir2_opnd2_(lisa_mov, &latxs_arg0_ir2_opnd,
                                      &latxs_env_ir2_opnd);
    if (hamt_enabled()) {
        latxs_tr_dmw_out_fix_mem(&latxs_arg0_ir2_opnd);
    }
    /* 3.4 arg1 : memory address */
    latxs_append_ir2_opnd2_(lisa_mov, &latxs_arg1_ir2_opnd,
                                      &mem_addr_opnd);
    /* 3.5 call helper xsave */
    latxs_tr_gen_call_to_helper((ADDR)helper_xrstor, HELPER_XRSTOR);
    /* 3.6 restore context */
    latxs_tr_gen_call_to_helper_epilogue_cfg(cfg);

#ifdef CONFIG_LATX_OPT_NOFP
    latxs_gen_nofp_sync_fprs_from_env();
#endif

    return true;
}

bool latxs_translate_xsaveopt(IR1_INST *pir1)
{
    if (latxs_tr_gen_excp_check(pir1)) {
        return true;
    }
#ifdef CONFIG_LATX_OPT_NOFP
    latxs_gen_nofp_sync_fprs_to_env();
#endif

    IR1_OPND *opnd = ir1_get_opnd(pir1, 0);
#ifdef CONFIG_SOFTMMU
    lsassertm_illop(ir1_addr(pir1), ir1_opnd_is_mem(opnd),
            "xsaveopt operand is not memory.\n");
#endif

    /* 1. get memory address from operand */
    IR2_OPND mem_base;
    int mem_offset;
    latxs_convert_mem_opnd(&mem_base, &mem_offset, opnd, -1);
    if (mem_offset != 0) {
        latxs_append_ir2_opnd2i(LISA_ADDI_D, &mem_base, &mem_base, mem_offset);
#ifdef LATXS_IMM_CACHE_ENABLE
        imm_cache_addi_update(mem_offset);
#endif
    }
    IR2_OPND mem_addr_opnd = mem_base;

    /* 2. get rfbm from eax and edx */
    IR2_OPND rfbm_opnd = latxs_ra_alloc_itemp();
    IR2_OPND eax_opnd = latxs_ra_alloc_gpr(eax_index);
    IR2_OPND edx_opnd = latxs_ra_alloc_gpr(edx_index);
    IR2_OPND eax_opnd32 = latxs_ra_alloc_itemp();
    /* eax might be sign extension */

    latxs_append_ir2_opnd2i(LISA_SLLI_D, &rfbm_opnd, &edx_opnd, 32);
    latxs_append_ir2_opnd2_(lisa_mov32z, &eax_opnd32, &eax_opnd);
    latxs_append_ir2_opnd3(LISA_OR, &rfbm_opnd, &eax_opnd32, &rfbm_opnd);

    /*
     * 3. call helper_xsaveopt
     *
     * void helper_xsaveopt(
     *      CPUX86State *env,
     *      target_ulong ptr,
     *      uint64_t rfbm)
     */
    helper_cfg_t cfg = all_helper_cfg;
    /* 3.1 save context */
    latxs_tr_gen_call_to_helper_prologue_cfg(cfg);
    /* 3.2 arg2 : rfbm */
    latxs_append_ir2_opnd2_(lisa_mov, &latxs_arg2_ir2_opnd,
                                      &rfbm_opnd);
    /* 3.3 arg0 : env */
    latxs_append_ir2_opnd2_(lisa_mov, &latxs_arg0_ir2_opnd,
                                      &latxs_env_ir2_opnd);
    if (hamt_enabled()) {
        latxs_tr_dmw_out_fix_mem(&latxs_arg0_ir2_opnd);
    }
    /* 3.4 arg1 : memory address */
    latxs_append_ir2_opnd2_(lisa_mov, &latxs_arg1_ir2_opnd,
                                      &mem_addr_opnd);
    /* 3.5 call helper xsave */
    latxs_tr_gen_call_to_helper((ADDR)helper_xsaveopt, HELPER_XSAVEOPT);
    /* 3.6 restore context */
    latxs_tr_gen_call_to_helper_epilogue_cfg(cfg);
    return true;
}

/* End of TB in system-mode */
bool latxs_translate_fninit(IR1_INST *pir1)
{
    if (option_soft_fpu) {
        return latxs_translate_fninit_softfpu(pir1);
    }
    if (latxs_tr_gen_fp_common_excp_check(pir1)) {
        return true;
    }

    /* 1. reset control word = 0x37f */
    IR2_OPND cw = latxs_ra_alloc_itemp();
    latxs_append_ir2_opnd2i(LISA_ORI, &cw, &latxs_zero_ir2_opnd, 0x37f);
    latxs_append_ir2_opnd2i(LISA_ST_H, &cw,
            &latxs_env_ir2_opnd,
            lsenv_offset_of_control_word(lsenv));

    /* 2. reset status word = 0 */
    latxs_append_ir2_opnd2i(LISA_ST_H, &latxs_zero_ir2_opnd,
            &latxs_env_ir2_opnd,
            lsenv_offset_of_status_word(lsenv));
    if (option_lsfpu) {
        latxs_append_ir2_opndi(LISA_X86MTTOP, 0);
    }

    /* 3. reset top = 0 */
    latxs_append_ir2_opnd2i(LISA_ST_W, &latxs_zero_ir2_opnd,
            &latxs_env_ir2_opnd,
            lsenv_offset_of_top(lsenv));

    /* 4. reset fptags = 0b11 (but = 1 in helper_fninit )*/
    IR2_OPND tag = latxs_ra_alloc_itemp();
    latxs_load_imm64_to_ir2(&tag, 0x0101010101010101);
    latxs_append_ir2_opnd2i(LISA_ST_D, &tag,
            &latxs_env_ir2_opnd,
            lsenv_offset_of_tag_word(lsenv));

    latxs_tr_reset_fcsr_rm();

    return true;
}

bool latxs_translate_ffree(IR1_INST *pir1)
{
    if (option_soft_fpu) {
        return latxs_translate_ffree_softfpu(pir1);
    }
    if (latxs_tr_gen_fp_common_excp_check(pir1)) {
        return true;
    }

    /*
     * 1. get real current top
     *    The td->curr_top might be rotated along with the FPRs.
     *    But the tag word is not rotated.
     *    It should be fixed with top_bias.
     */
    IR2_OPND real_top = latxs_tr_get_real_curr_top();
    /* 2. tag_offset = (real_top + reg) & 0x7 */
    IR2_OPND tag_offset_opnd = latxs_ra_alloc_itemp();
    int reg = ir1_opnd_base_reg_num(ir1_get_opnd(pir1, 0));
    latxs_append_ir2_opnd2i(LISA_ADDI_D, &tag_offset_opnd,
                                         &real_top, reg);
    latxs_append_ir2_opnd2i(LISA_ANDI, &tag_offset_opnd,
                                       &tag_offset_opnd, 0x7);
    /* 3. address of tag = env + tag_offset. */
    IR2_OPND address = latxs_ra_alloc_itemp();
    latxs_append_ir2_opnd2i(LISA_ADDI_D, &address,
            &latxs_env_ir2_opnd,
            lsenv_offset_of_tag_word(lsenv));
    latxs_append_ir2_opnd3(LISA_ADD_D, &address,
                                       &address, &tag_offset_opnd);
    /* 4. set tag = 1 */
    IR2_OPND tag = latxs_ra_alloc_itemp();
    latxs_append_ir2_opnd2i(LISA_ORI,  &tag, &latxs_zero_ir2_opnd, 1);
    latxs_append_ir2_opnd2i(LISA_ST_B, &tag, &address,       0);
    return true;
}

bool latxs_translate_ffreep(IR1_INST *pir1)
{
    if (option_soft_fpu) {
        return latxs_translate_ffreep_softfpu(pir1);
    }
    latxs_translate_ffree(pir1);
    latxs_tr_fpu_pop();
    return true;
}

bool latxs_translate_fincstp(IR1_INST *pir1)
{
    if (latxs_tr_gen_fp_common_excp_check(pir1)) {
        return true;
    }
    if (option_soft_fpu) {
        return latxs_translate_fincstp_softfpu(pir1);
    }

    /* 1. increase the top-of-stack pointer : td->curr_top */
    latxs_tr_fpu_inc();
    /* 2. set C1 flag to 0 */
    IR2_OPND mask = latxs_ra_alloc_itemp();
    IR2_OPND value_status = latxs_ra_alloc_itemp();

    latxs_append_ir2_opnd2i(LISA_LD_H, &value_status,
            &latxs_env_ir2_opnd,
            lsenv_offset_of_status_word(lsenv));
    latxs_load_imm32_to_ir2(&mask, 0xfdffULL, EXMode_Z);
    latxs_append_ir2_opnd3(LISA_AND, &value_status,
                                     &value_status, &mask);
    latxs_append_ir2_opnd2i(LISA_ST_H, &value_status,
            &latxs_env_ir2_opnd,
            lsenv_offset_of_status_word(lsenv));
    return true;
}

bool latxs_translate_fdecstp(IR1_INST *pir1)
{
    if (option_soft_fpu) {
        return latxs_translate_fdecstp_softfpu(pir1);
    }
    if (latxs_tr_gen_fp_common_excp_check(pir1)) {
        return true;
    }

    /* 1. decrease the top-of-stack pointer : td->curr_top */
    latxs_tr_fpu_dec();
    /* 2. set C1 flag to 0 */
    IR2_OPND mask = latxs_ra_alloc_itemp();
    IR2_OPND value_status = latxs_ra_alloc_itemp();

    latxs_append_ir2_opnd2i(LISA_LD_H, &value_status,
            &latxs_env_ir2_opnd,
            lsenv_offset_of_status_word(lsenv));
    latxs_load_imm32_to_ir2(&mask, 0xfdffULL, EXMode_Z);
    latxs_append_ir2_opnd3(LISA_AND, &value_status,
                                     &value_status, &mask);
    latxs_append_ir2_opnd2i(LISA_ST_H, &value_status,
            &latxs_env_ir2_opnd,
            lsenv_offset_of_status_word(lsenv));

    return true;
}
