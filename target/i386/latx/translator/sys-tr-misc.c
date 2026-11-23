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
#include "flag-lbt.h"
#include "latx-ras.h"
#include "latx-sys-hamt-dmw.h"
#include "latx-misc.h"
#include "hamt-data.h"
#ifdef CONFIG_LATX_TU
#include "latx-tu.h"
#endif
#include "tcg/helper-tcg.h"
#include "latx-sys-push-pop.h"
#include "latx-static-codes.h"

void latxs_sys_misc_register_ir1(void)
{
    latxs_register_ir1(X86_INS_INVALID);
    latxs_register_ir1(X86_INS_LJMP);
    latxs_register_ir1(X86_INS_CALL);
    latxs_register_ir1(X86_INS_JMP);
    latxs_register_ir1(X86_INS_CLI);
    latxs_register_ir1(X86_INS_STI);
    latxs_register_ir1(X86_INS_LIDT);
    latxs_register_ir1(X86_INS_SIDT);
    latxs_register_ir1(X86_INS_LGDT);
    latxs_register_ir1(X86_INS_SGDT);
    latxs_register_ir1(X86_INS_LLDT);
    latxs_register_ir1(X86_INS_SLDT);
    latxs_register_ir1(X86_INS_LTR);
    latxs_register_ir1(X86_INS_STR);
    latxs_register_ir1(X86_INS_PUSH);
    latxs_register_ir1(X86_INS_POP);
    latxs_register_ir1(X86_INS_RET);
    latxs_register_ir1(X86_INS_CPUID);
    latxs_register_ir1(X86_INS_XCHG);
    latxs_register_ir1(X86_INS_CMPXCHG);
    latxs_register_ir1(X86_INS_CMPXCHG8B);
    latxs_register_ir1(X86_INS_CMPXCHG16B);
    latxs_register_ir1(X86_INS_RSM);
    latxs_register_ir1(X86_INS_INVD);
    latxs_register_ir1(X86_INS_WBINVD);
    latxs_register_ir1(X86_INS_NOP);
    latxs_register_ir1(X86_INS_PAUSE);
    latxs_register_ir1(X86_INS_IRET);
    latxs_register_ir1(X86_INS_IRETD);
    latxs_register_ir1(X86_INS_IRETQ);
    latxs_register_ir1(X86_INS_INT);
    latxs_register_ir1(X86_INS_INT1);
    latxs_register_ir1(X86_INS_INT3);
    latxs_register_ir1(X86_INS_INTO);
    latxs_register_ir1(X86_INS_RETF);
    latxs_register_ir1(X86_INS_RETFQ);
    latxs_register_ir1(X86_INS_CWD);
    latxs_register_ir1(X86_INS_CDQ);
    latxs_register_ir1(X86_INS_CQO);
    latxs_register_ir1(X86_INS_CWDE);
    latxs_register_ir1(X86_INS_CDQE);
    latxs_register_ir1(X86_INS_ARPL);
    latxs_register_ir1(X86_INS_CBW);
    latxs_register_ir1(X86_INS_LCALL);
    latxs_register_ir1(X86_INS_POPAW);
    latxs_register_ir1(X86_INS_POPAL);
    latxs_register_ir1(X86_INS_PUSHAW);
    latxs_register_ir1(X86_INS_PUSHAL);
    latxs_register_ir1(X86_INS_LDS);
    latxs_register_ir1(X86_INS_LES);
    latxs_register_ir1(X86_INS_LFS);
    latxs_register_ir1(X86_INS_LGS);
    latxs_register_ir1(X86_INS_LSS);
    latxs_register_ir1(X86_INS_ENTER);
    latxs_register_ir1(X86_INS_LEAVE);
    latxs_register_ir1(X86_INS_RDTSC);
    latxs_register_ir1(X86_INS_RDTSCP);
    latxs_register_ir1(X86_INS_RDPMC);
    latxs_register_ir1(X86_INS_RDRAND);
    latxs_register_ir1(X86_INS_RDSEED);
    latxs_register_ir1(X86_INS_POPCNT);
    latxs_register_ir1(X86_INS_HLT);
    latxs_register_ir1(X86_INS_RDMSR);
    latxs_register_ir1(X86_INS_WRMSR);
    latxs_register_ir1(X86_INS_INVPCID);
    latxs_register_ir1(X86_INS_INVLPG);
    latxs_register_ir1(X86_INS_INVLPGA);
    latxs_register_ir1(X86_INS_LFENCE);
    latxs_register_ir1(X86_INS_MFENCE);
    latxs_register_ir1(X86_INS_SFENCE);
    latxs_register_ir1(X86_INS_PREFETCHNTA);
    latxs_register_ir1(X86_INS_SYSENTER);
    latxs_register_ir1(X86_INS_SYSEXIT);
    latxs_register_ir1(X86_INS_SYSCALL);
    latxs_register_ir1(X86_INS_SYSRET);
    latxs_register_ir1(X86_INS_PREFETCHT0);
    latxs_register_ir1(X86_INS_PREFETCHT1);
    latxs_register_ir1(X86_INS_PREFETCHT2);
    latxs_register_ir1(X86_INS_PREFETCHW);
    latxs_register_ir1(X86_INS_PREFETCH);
    latxs_register_ir1(X86_INS_TZCNT);

    latxs_register_ir1(X86_INS_UD2);
    latxs_register_ir1(X86_INS_ENDBR32);
    latxs_register_ir1(X86_INS_ENDBR64);
    latxs_register_ir1(X86_INS_SWAPGS);

    latxs_register_ir1(X86_INS_LMSW);
    latxs_register_ir1(X86_INS_SMSW);

    latxs_register_ir1(X86_INS_VERR);
    latxs_register_ir1(X86_INS_VERW);

    latxs_register_ir1(X86_INS_LSL);
    latxs_register_ir1(X86_INS_LAR);

    latxs_register_ir1(X86_INS_CLFLUSH);
}

int latxs_get_sys_stack_addr_size(void)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    /* in 64-bit mode, the size of the stack pointer is always 64 bits. */
    if (td->sys.code64) {
        return 8;
    }
    if (td->sys.ss32) {
        return 4;
    } else {
        return 2;
    }
}

static void translate_jmp_far_pe_imm(IR1_INST *pir1, IR1_OPND *opnd0)
{
    /*
     * void helper_ljmp_protected(
     *     CPUX86State     *env,
     *     int             new_cs,
     *     target_ulong    new_eip,
     *     target_ulong    next_eip)
     */
    IR2_OPND *env = &latxs_env_ir2_opnd;
    IR2_OPND *arg0 = &latxs_arg0_ir2_opnd;
    IR2_OPND *arg1 = &latxs_arg1_ir2_opnd;
    IR2_OPND *arg2 = &latxs_arg2_ir2_opnd;
    IR2_OPND *arg3 = &latxs_arg3_ir2_opnd;

    int   new_cs   = 0;
    ADDRX new_eip  = 0;
    ADDRX next_eip = ir1_addr_next(pir1);

    /*
     *                  ptr16:16 ptr16:32
     * opnd[0]:selector    16       16
     * opnd[1]:offset      16       32
     */
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);
    int opnd1_size  = ir1_opnd_size(opnd1);

    new_cs  = ir1_opnd_simm(opnd0);
    new_eip = ir1_opnd_uimm(opnd1);

    /* 1. save native context */
    latxs_tr_gen_call_to_helper_prologue_cfg(default_helper_cfg);

    /* 2. parameters */

    /* 2.1 arg0: env */
    latxs_append_ir2_opnd2_(lisa_mov, arg0, env);
    if (hamt_enabled()) {
        latxs_tr_dmw_out_fix_mem(arg0);
    }

    /* 2.2 arg1: new_cs, 16-bits */
    if (option_aot_save) {
        aot_load_guest_addr_to_ir2(arg1, new_cs & 0xffff, LOAD_GUEST_ADDR);
    } else {
        latxs_load_imm32_to_ir2(arg1, new_cs & 0xffff, EXMode_Z);
    }

    /* 2.3 arg2: new_eip, 16/32-bits */
    if (option_aot_save) {
        /* if (opnd1_size == 16) { */
        /*     aot_load_guest_addr_to_ir2(arg2, new_eip & 0xffffffff, LOAD_GUEST_ADDR); */
        /* } else if (opnd1_size == 32) { */
        /*     aot_load_guest_addr_to_ir2(arg2, new_eip & 0xffffffff, LOAD_GUEST_ADDR); */
        /* } else { */
        /*     aot_load_guest_addr_to_ir2(arg2, new_eip, LOAD_GUEST_ADDR); */
        /* } */
        aot_load_guest_addr_to_ir2(arg2, new_eip, LOAD_GUEST_ADDR);
    }else if (opnd1_size == 16) {
        latxs_load_imm32_to_ir2(arg2, new_eip & 0xffff, EXMode_Z);
    } else if (opnd1_size == 32) { /* opnd1_size == 32 */
        latxs_load_imm32_to_ir2(arg2, new_eip & 0xffffffff, EXMode_Z);
    } else {
        latxs_load_imm64_to_ir2(arg2, new_eip);
    }

    /* 2.4 arg3: next_eip, 32-bits */
    if (option_aot_save) {
        aot_load_guest_addr_to_ir2(arg3, next_eip, LOAD_GUEST_ADDR);
    } else {
        latxs_load_imm32_to_ir2(arg3, next_eip, EXMode_Z);
    }

    /* 3. call helper */
    latxs_tr_gen_call_to_helper((ADDR)helper_ljmp_protected, HELPER_LJMP_PROTECTED);

    /* 4. restore native context */
    latxs_tr_gen_call_to_helper_epilogue_cfg(default_helper_cfg);

    /* 5. context switch */
    lsenv->tr_data->ignore_eip_update = 1;
}

static void translate_jmp_far_pe_mem(IR1_INST *pir1, IR1_OPND *opnd0)
{
    /*
     * void helper_ljmp_protected(
     *     CPUX86State     *env,
     *     int             new_cs,
     *     target_ulong    new_eip,
     *     target_ulong    next_eip)
     */

    /*
     *           m16:16     m16:32
     * offset      16         32     MEM(addr)
     * selector    16         16     MEM(addr+2/4)
     */
    IR2_OPND mem_base;
    int mem_offset;
    latxs_convert_mem_opnd(&mem_base, &mem_offset, opnd0, -1);

    /* 1. load offset and selector */
    IR2_OPND offset = latxs_ra_alloc_itemp();
    IR2_OPND selector = latxs_ra_alloc_itemp();

    int opnd_size = ir1_opnd_size(opnd0);
    int data_size = latxs_ir1_data_size(pir1);

    opnd_size = data_size + 16;

    switch (opnd_size) {
    case 32: /* m16:16 */
        gen_ldst_softmmu_helper(LISA_LD_WU, &offset, &mem_base, mem_offset, 0);
        latxs_append_ir2_opnd2i(LISA_SRAI_D, &selector, &offset, 0x10);
        latxs_append_ir2_opnd2_(lisa_mov16z, &offset, &offset);
        break;
    case 48: /* m16:32 */
        /* load 32-bit offset */
        gen_ldst_softmmu_helper(LISA_LD_WU, &offset, &mem_base, mem_offset, 1);
        /* load 16-bit selector */
        gen_ldst_softmmu_helper(LISA_LD_HU, &selector, &mem_base, mem_offset + 4, 1);
        break;
    case 80: /* m16:64 */
        /* load 64-bit offset */
        gen_ldst_softmmu_helper(LISA_LD_D, &offset, &mem_base, mem_offset, 1);
        /* memory address += 8 */
        /* load 16-bit selector */
        gen_ldst_softmmu_helper(LISA_LD_HU, &selector, &mem_base, mem_offset + 8, 1);
        break;
    default:
        ir1_dump(pir1);
#ifdef CONFIG_SOFTMMU
        lsassertm_illop(ir1_addr(pir1), 0,
                "unsupported opnd size %d in ljmp mem.\n", opnd_size);
#else
        lsassertm(0,
                "unsupported opnd size %d in ljmp mem.\n", opnd_size);
#endif
        break;
    }

    latxs_ra_free_temp(&mem_base);

    /* 2. save native context here */
    latxs_tr_gen_call_to_helper_prologue_cfg(default_helper_cfg);

    /* 3. parameters */
    /* 3.1 arg1: new_cs, 16-bits */
    /* 3.2 arg2: new_eip, 16/32-bits */
    latxs_append_ir2_opnd2_(lisa_mov, &latxs_arg1_ir2_opnd, &selector);
    latxs_append_ir2_opnd2_(lisa_mov, &latxs_arg2_ir2_opnd, &offset);
    /* 3.4 arg3: next_eip, 32-bits */
    ADDRX next_eip = ir1_addr_next(pir1);
    if (option_aot_save) {
        aot_load_guest_addr_to_ir2(&latxs_arg3_ir2_opnd, next_eip, LOAD_GUEST_ADDR);
    } else {
        latxs_load_imm32_to_ir2(&latxs_arg3_ir2_opnd, next_eip, EXMode_Z);
    }

    /* 3.5 arg0: env */
    latxs_append_ir2_opnd2_(lisa_mov, &latxs_arg0_ir2_opnd,
                                      &latxs_env_ir2_opnd);
    if (hamt_enabled()) {
        latxs_tr_dmw_out_fix_mem(&latxs_arg0_ir2_opnd);
    }

    /* 4. call helper */
    latxs_tr_gen_call_to_helper((ADDR)helper_ljmp_protected, HELPER_LJMP_PROTECTED);

    /* 5. restore native context */
    latxs_tr_gen_call_to_helper_epilogue_cfg(default_helper_cfg);

    /* 6. context switch */
    lsenv->tr_data->ignore_eip_update = 1;
}

static void translate_jmp_far_real_imm(IR1_INST *pir1, IR1_OPND *opnd0)
{
    uint32_t selector;
    ADDRX base;

    IR2_OPND tmp0 = latxs_ra_alloc_itemp();
    IR2_OPND tmp1 = latxs_ra_alloc_itemp();

    /*                  ptr16:16 ptr16:32 */
    /* opnd[0]:selector    16       16    */
    /* opnd[1]:offset      16       32    */
    selector = ir1_opnd_uimm(opnd0);
    base     = selector << 0x4;

    latxs_load_imm32_to_ir2(&tmp0, selector, EXMode_Z);
    latxs_load_addrx_to_ir2(&tmp1, base);

    latxs_append_ir2_opnd2i(LISA_ST_W, &tmp0, &latxs_env_ir2_opnd,
            lsenv_offset_of_seg_selector(lsenv, R_CS));
    latxs_append_ir2_opnd2i(LISA_ST_W, &tmp1, &latxs_env_ir2_opnd,
            lsenv_offset_of_seg_base(lsenv, R_CS));

    IR1_OPND *imm = ir1_get_opnd(pir1, 1);
    uint64_t next_eip = ir1_opnd_uimm(imm);
    switch (ir1_opnd_size(imm)) {
    case 32: la_imm(&tmp0, next_eip & ((1ULL << 32) - 1)); break;
    case 16: la_imm(&tmp0, next_eip & ((1ULL << 16) - 1)); break;
    case 64: la_imm(&tmp0, next_eip); break;
    default: lsassert(0); break;
    }

    latxs_tr_gen_store_to_curr_eip(&tmp0);

    lsenv->tr_data->ignore_eip_update = 1;
}

static void translate_jmp_far_real_mem(IR1_INST *pir1, IR1_OPND *opnd0)
{
    /* m16:16/m16:32/m16:64 */
    IR2_OPND mem_base;
    int mem_offset;
    latxs_convert_mem_opnd(&mem_base, &mem_offset, opnd0, -1);

    IR2_OPND dest = latxs_ra_alloc_itemp();
    IR2_OPND next_eip = latxs_ra_alloc_itemp();

    /* m16:16 */
    if (ir1_opnd_size(opnd0) == 32) {
        IR2_OPND tmp0 = latxs_ra_alloc_itemp();

        gen_ldst_softmmu_helper(LISA_LD_W, &dest, &mem_base, mem_offset, 1);

        latxs_append_ir2_opnd2_(lisa_mov16z, &next_eip, &dest);

        latxs_append_ir2_opnd2i(LISA_SRLI_W, &tmp0, &dest, 0x10);
        latxs_append_ir2_opnd2_(lisa_mov16z, &tmp0, &tmp0);
        latxs_append_ir2_opnd2i(LISA_ST_W, &tmp0, &latxs_env_ir2_opnd,
                lsenv_offset_of_seg_selector(lsenv, R_CS));

        latxs_append_ir2_opnd2i(LISA_SLLI_W, &tmp0, &tmp0, 0x4);
        latxs_append_ir2_opnd2i(LISA_ST_W, &tmp0, &latxs_env_ir2_opnd,
                lsenv_offset_of_seg_base(lsenv, R_CS));

        latxs_ra_free_temp(&tmp0);
    } else if (ir1_opnd_size(opnd0) == 48) { /* m16:32 */
        /* load 32-bit offset */
        gen_ldst_softmmu_helper(LISA_LD_WU, &dest, &mem_base, mem_offset, 1);
        latxs_append_ir2_opnd2_(lisa_mov, &next_eip, &dest);

        /* load 16-bit selector */
        gen_ldst_softmmu_helper(LISA_LD_HU, &dest, &mem_base, mem_offset + 4, 1);
        latxs_append_ir2_opnd2i(LISA_ST_W, &dest, &latxs_env_ir2_opnd,
                lsenv_offset_of_seg_selector(lsenv, R_CS));

        latxs_append_ir2_opnd2i(LISA_SLLI_D, &dest, &dest, 0x4);
        latxs_append_ir2_opnd2i(LISA_ST_W, &dest, &latxs_env_ir2_opnd,
                lsenv_offset_of_seg_base(lsenv, R_CS));
    } else { /* m16:64 */
#ifdef CONFIG_SOFTMMU
        lsassertm_illop(ir1_addr(pir1), 0,
            "unsupported opnd size %d in ljmp mem.\n", ir1_opnd_size(opnd0));
#endif
    }

    latxs_ra_free_temp(&dest);

    latxs_tr_gen_store_to_curr_eip(&next_eip);

    lsenv->tr_data->ignore_eip_update = 1;
}

bool latxs_translate_jmp_far(IR1_INST *pir1)
{
    TRANSLATION_DATA *td = lsenv->tr_data;

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);

    /* protected mode && not vm86 mode */
    if (td->sys.pe && !td->sys.vm86) {
        if (ir1_opnd_is_imm(opnd0)) {
            translate_jmp_far_pe_imm(pir1, opnd0);
        } else if (ir1_opnd_is_mem(opnd0)) {
            translate_jmp_far_pe_mem(pir1, opnd0);
        } else {
            lsassert(0);
        }
        return true;
    }

    /* Real-Address mode || vm86 mode */
    if (ir1_opnd_is_imm(opnd0)) {
        translate_jmp_far_real_imm(pir1, opnd0);
    } else if (ir1_opnd_is_mem(opnd0)) {
        translate_jmp_far_real_mem(pir1, opnd0);
    }

    return true;
}

void latxs_update_stack_pointer(int64_t val, int size)
{
    IR2_OPND esp = latxs_ra_alloc_gpr(esp_index);

    switch (size) {
    case 8: { /* code64 */
        if (val != sextract64(val, 0, 12)) {
            IR2_OPND imm = latxs_ra_alloc_itemp();
            latxs_load_imm64_to_ir2(&imm, val);
            latxs_append_ir2_opnd3(LISA_ADD_D, &esp, &esp, &imm);
            latxs_ra_free_temp(&imm);
        } else {
            latxs_append_ir2_opnd2i(LISA_ADDI_D, &esp, &esp, val);
        }
        break;
    }
    case 4: { /* ss32 */
        if (val != sextract64(val, 0, 12)) {
            IR2_OPND imm = latxs_ra_alloc_itemp();
            latxs_load_imm64_to_ir2(&imm, val);
            latxs_append_ir2_opnd3(LISA_ADD_W, &esp, &esp, &imm);
            latxs_ra_free_temp(&imm);
        } else {
            latxs_append_ir2_opnd2i(LISA_ADDI_W, &esp, &esp, val);
        }
        latxs_append_ir2_opnd2_(lisa_mov32z, &esp, &esp);
        break;
    }
    case 2: {
        IR2_OPND tmp = latxs_ra_alloc_itemp();
        if (val != sextract64(val, 0, 12)) {
            latxs_load_imm64_to_ir2(&tmp, val);
            latxs_append_ir2_opnd3(LISA_ADD_D, &tmp, &esp, &tmp);
        } else {
            latxs_append_ir2_opnd2i(LISA_ADDI_D, &tmp, &esp, val);
        }
        latxs_store_ir2_to_ir1_gpr(&tmp, &sp_ir1_opnd);
        latxs_ra_free_temp(&tmp);
        break;
    }
    default: lsassert(0); break;
    }
}


bool latxs_translate_call(IR1_INST *pir1)
{
    if (ir1_is_indirect_call(pir1)) {
        return latxs_translate_callin(pir1);
    }

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    lsassertm(ir1_opnd_is_imm(opnd0), "opnd is not imm");
    int data_size = latxs_ir1_data_size(pir1);
    int opnd_size = ir1_opnd_size(opnd0);

    uint64_t ta = ir1_target_addr(pir1); (void)ta;
    if (lsenv->tr_data->sys.code64) {
        data_size = (data_size == 16) ? 16 : 64;
        if (data_size == 16) {
#ifdef CONFIG_LATX_DEBUG
            lsassertm(!(ta >> 16), "target addr not 16 : %lx", ta);
            if (ta >> 16) ir1_dump(pir1);
#endif
            opnd0->imm &= 0xffff;
        }
    } else {
        lsassertm(opnd_size == 16 || opnd_size == 32, "opnd size not 16 or 32");
        lsassertm(data_size == opnd_size, "data size opnd size not equal");
        /* make sure target addr size is correct */
        if (opnd_size == 16) {
#ifdef CONFIG_LATX_DEBUG
            lsassertm(!(ta >> 16), "target addr not 16 : %lx", ta);
            if (ta >> 16) ir1_dump(pir1);
#endif
            opnd0->imm &= 0xffff;
        } else {
#ifdef CONFIG_LATX_DEBUG
            lsassertm(!(ta >> 32), "target addr not 32 : %lx", ta);
            if (ta >> 32) ir1_dump(pir1);
#endif
            opnd0->imm &= 0xffffffff;
        }
    }

    int ss_addr_size = latxs_get_sys_stack_addr_size();

    int esp_dec = 0 - (data_size >> 3);

    /* 1. get return address */
    IR2_OPND return_addr_opnd = latxs_ra_alloc_itemp();
    /* pcrel, we should avoid directly storing next_eip */
    TRANSLATION_DATA *td = lsenv->tr_data;
    ADDRX next_eip_offset = 0;
    if (option_tb_pcrel)
        next_eip_offset = td->pc_next - td->pc_save;
    else if (option_page_pcrel)
        next_eip_offset = td->pc_next - td->curr_page;
    latxs_tr_gen_add_curr_eip_to_ir2(&return_addr_opnd, next_eip_offset, ir1_addr_next(pir1), true);

    /* 2. save return address at MEM(SS:ESP - 2/4)*/
    IR1_OPND mem_ir1_opnd;
    ir1_opnd_build_full_mem(&mem_ir1_opnd, data_size,
            X86_REG_SS, X86_REG_ESP, esp_dec, 0, 0);
    latxs_store_ir2_to_ir1_mem(&return_addr_opnd,
            &mem_ir1_opnd, ss_addr_size);

    /* 3. update ESP */
    latxs_update_stack_pointer(esp_dec, ss_addr_size);

    if (!option_rastoptp) latxs_ra_free_temp(&return_addr_opnd);

    if (option_soft_ras) {
        if (!latxs_translate_ras_push(pir1, option_rastoptp ? &return_addr_opnd : NULL)) {
            return false;
        }
    }

    if (option_rastoptp) latxs_ra_free_temp(&return_addr_opnd);

    /* 4. exit TB */
    latxs_tr_generate_exit_tb(pir1, 0);

    return true;
}

bool latxs_translate_callin(IR1_INST *pir1)
{
    TRANSLATION_DATA *td = lsenv->tr_data;

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    int data_size = latxs_ir1_data_size(pir1);
    if (lsenv->tr_data->sys.code64) {
        data_size = (data_size == 16) ? 16 : 64;
    }
    int opnd_size = ir1_opnd_size(opnd0);
    (void)opnd_size;
    if (lsenv->tr_data->sys.code64) {
        lsassert(opnd_size == 16 || opnd_size == 64);
    } else {
        lsassert(opnd_size == 16 || opnd_size == 32);
    }
    lsassert(data_size == opnd_size);
    (void)data_size; /* to avoid compile warning */

    int ss_addr_size = latxs_get_sys_stack_addr_size();

    int esp_dec = 0 - (data_size >> 3);

    /* 1. prepare successor x86 address */
#ifdef CONFIG_SOFTMMU
    bool direct_gpc = hamt_enabled() && can_do_hamt(lsenv->cpu_state);
#else
    bool direct_gpc = true;
#endif
    IR2_OPND next_eip_opnd = direct_gpc ? latxs_arg1_ir2_opnd :
                                           latxs_ra_alloc_itemp();
    latxs_load_ir1_to_ir2(&next_eip_opnd, opnd0, EXMode_Z);
    if (data_size == 16) {
        latxs_append_ir2_opnd2_(lisa_mov16z, &next_eip_opnd,
                                             &next_eip_opnd);
    }

    /* 2. get return address */
    IR2_OPND return_addr_opnd = latxs_ra_alloc_itemp();
    /* pcrel, we should avoid directly storing next_eip */
    ADDRX next_eip_offset = 0;
    if (option_tb_pcrel)
        next_eip_offset = td->pc_next - td->pc_save;
    else if (option_page_pcrel)
        next_eip_offset = td->pc_next - td->curr_page;
    latxs_tr_gen_add_curr_eip_to_ir2(&return_addr_opnd, next_eip_offset, ir1_addr_next(pir1), true);

    /* 3. save return address at MEM(SS:ESP - 2/4)*/
    IR1_OPND mem_ir1_opnd;
    ir1_opnd_build_full_mem(&mem_ir1_opnd, data_size, /* 16 or 32 */
            X86_REG_SS, X86_REG_ESP, esp_dec, 0, 0);
    latxs_store_ir2_to_ir1_mem(&return_addr_opnd,
            &mem_ir1_opnd, ss_addr_size);
    bool reuse_return_addr = option_soft_ras && option_rastoptp;
    if (!reuse_return_addr) {
        latxs_ra_free_temp(&return_addr_opnd);
    }

    /* 4. update ESP */
    latxs_update_stack_pointer(esp_dec, ss_addr_size);

    /* 5. go to next TB */
    if (!direct_gpc) {
        latxs_append_ir2_opnd2_(lisa_mov, &latxs_arg1_ir2_opnd, &next_eip_opnd);
        latxs_ra_free_temp(&next_eip_opnd);
    }

    if (option_soft_ras) {
        bool pushed = latxs_translate_ras_push(pir1,
                reuse_return_addr ? &return_addr_opnd : NULL);
        if (reuse_return_addr) {
            latxs_ra_free_temp(&return_addr_opnd);
        }
        if (!pushed) {
            return false;
        }
    }

    latxs_tr_generate_exit_tb(pir1, 0);

    return true;
}

#ifdef CONFIG_LATX_TU
static void latxs_tu_backedge_irq_exit(IR1_INST *pir1, TranslationBlock *tb,
                                      TRANSLATION_DATA *td, IR2_OPND *tmp)
{
    IR2_OPND tb_ptr_opnd = latxs_ra_alloc_dbt_arg1();
    latxs_tr_gen_exit_tb_load_tb_addr(&tb_ptr_opnd, (ADDR)tb);
    ADDRX eip = ir1_target_addr(pir1);
    if (option_aot_save) {
        aot_load_guest_addr_to_ir2(tmp, eip, LOAD_GUEST_ADDR);
    } else {
        latxs_load_imm64_to_ir2(tmp, eip);
    }
    la_out2i(LISA_ST_D, tmp, &la_env,
            lsenv_offset_of_eip(lsenv));
    int offset = td->ir2_asm_nr << 2;
    tb_sc_table_offset_jmp(tb, td->mtcc_id,
            cs_native_to_bt_ret_0,
            td->tcg_region_offset, offset, 0,
            CS_NATIVE_TO_BT_RET_0);
}

typedef enum TUBackedgeIRQPath {
    TU_IRQ_INLINE_EXIT,
    TU_IRQ_AOT_BACKEDGE,
    TU_IRQ_COLD_SHORT,
    TU_IRQ_COLD_LONG,
} TUBackedgeIRQPath;

static TUBackedgeIRQPath latxs_tu_backedge_irq_path(TranslationBlock *tb,
        TranslationBlock *target_tb, const TRANSLATION_DATA *td)
{
    bool layout = option_irq_backedge && (tb->flags & HF_CS64_MASK) &&
                  !tb->cs_base && (tb->flags & HF_CPL_MASK) == HF_CPL_MASK;

    if (layout && aot_tu_enabled() && in_aot_translate && option_aot_save &&
        !tb_is_adjacent(tb, target_tb)) {
        return TU_IRQ_AOT_BACKEDGE;
    }
    if (tb->s_data->tu_cold_exit_offset[TU_TB_INDEX_TARGET] !=
        TU_COLD_EXIT_DIRECT_PENDING) {
        return TU_IRQ_INLINE_EXIT;
    }
    return layout && option_tu_hras == 2 && !td->hras_long_checks &&
           !in_aot_translate && !option_aot_save ?
           TU_IRQ_COLD_SHORT : TU_IRQ_COLD_LONG;
}

static bool latxs_tu_translate_jmp(IR1_INST *pir1)
{
    TranslationBlock *tb = lsenv->tr_data->curr_tb;
    TranslationBlock *target_tb = tb->s_data->next_tb[TU_TB_INDEX_TARGET];
    TRANSLATION_DATA *td = lsenv->tr_data;
    bool ret = false;

    /*
     * A direct TU edge skips the validation emitted by
     * latxs_tr_generate_exit_tb().  Keep the legacy same-page restriction
     * unless tucpfast enables direct TU-internal jumps; that mode relies on
     * validation when entering the TU.
     */
    if (target_tb && tb_is_in_tb_list(target_tb) &&
         (tucp_fast_tu_direct() ||
           (tb_page_addr1(tb) == -1 &&
             (target_tb->pc & TARGET_PAGE_MASK) ==
             (ir1_addr(pir1) & TARGET_PAGE_MASK)
           )
         )
        ) {
#ifdef CONFIG_LATX_PROFILER
        add_tu_profiler(tb);
#endif

        if (latxs_check_interrupt_at_block_end() && target_tb->pc <= tb->pc) {
            IR2_OPND tmp = la_new_itmp();
            TUBackedgeIRQPath path = latxs_tu_backedge_irq_path(tb, target_tb, td);

            switch (path) {
            case TU_IRQ_AOT_BACKEDGE: {
                IR2_OPND irq = la_new_label();

                latxs_load_intcheck_to_reg(&tmp);
                la_out3(LISA_BLT, &tmp, &la_zero, &irq);
                tb->s_data->tu_jmp[TU_TB_INDEX_TARGET] =
                    get_current_inst_addr(tb) - (uintptr_t)tb->tc.ptr;
                unsigned jump_start = td->ir2_asm_nr;
                tu_jcc_nop_gen(tb, 1, true);
                g_assert(td->ir2_asm_nr == jump_start + 1);
                la_label(irq);
                latxs_tu_backedge_irq_exit(pir1, tb, td, &tmp);
                la_tmp_free(tmp);
                return true;
            }
            case TU_IRQ_COLD_SHORT:
            case TU_IRQ_COLD_LONG: {
                latxs_load_intcheck_to_reg(&tmp);
                if (path == TU_IRQ_COLD_SHORT) {
                    /* A near cold exit lets the common path fall through
                     * to the loop jump. Pre-publication range validation
                     * retries with the original two-word form if needed. */
                    la_out3(LISA_BLT, &tmp, &la_zero,
                            latxs_ir2_interrupt_get_label(tb, TU_TB_INDEX_TARGET));
                    td->irq_backedge_short = true;
                } else {
                    IR2_OPND tb_exec_label = la_new_label();
                    la_out3(LISA_BGE, &tmp, &la_zero, &tb_exec_label);
                    la_out1(LISA_B, latxs_ir2_interrupt_get_label(
                                tb, TU_TB_INDEX_TARGET));
                    la_out1(LISA_LABEL, &tb_exec_label);
                }
                tb->s_data->tu_cold_exit_offset[TU_TB_INDEX_TARGET] =
                    TU_COLD_EXIT_JMP_INTERRUPT_DEFERRED;
                break;
            }
            case TU_IRQ_INLINE_EXIT: {
                IR2_OPND tb_exec_label = la_new_label();

                latxs_load_intcheck_to_reg(&tmp);
                la_out3(LISA_BGE, &tmp, &la_zero, &tb_exec_label);
                latxs_tu_backedge_irq_exit(pir1, tb, td, &tmp);
                la_out1(LISA_LABEL, &tb_exec_label);
                break;
            }
            }
            la_tmp_free(tmp);
        }

        /* calculate eflag */
        if (tb_is_adjacent(tb, target_tb)) {
            tb->s_data->tu_jmp[TU_TB_INDEX_TARGET] = TB_JMP_RESET_OFFSET_INVALID;
            latxs_append_ir2_opnd2i(LISA_ORI, &latxs_zero_ir2_opnd, &latxs_zero_ir2_opnd, 0);
        } else {
            tb->s_data->tu_jmp[TU_TB_INDEX_TARGET] =
                get_current_inst_addr(tb) - (uintptr_t)tb->tc.ptr;
            tu_jcc_nop_gen(tb, 1, true);
        }
        ret = true;
    } else {
        tb->s_data->tu_jmp[TU_TB_INDEX_TARGET] = TB_JMP_RESET_OFFSET_INVALID;
    }

    return ret;
}
#endif

bool latxs_translate_jmp(IR1_INST *pir1)
{
    if (ir1_is_indirect_jmp(pir1)) {
        return latxs_translate_jmpin(pir1);
    }

#ifdef CONFIG_LATX_TU
    if (latxs_tu_translate_jmp(pir1)) return true;
#endif

    latxs_tr_generate_exit_tb(pir1, 1);
    return true;
}

bool latxs_translate_jmpin(IR1_INST *pir1)
{
    /* 1. set successor x86 address */
#ifdef CONFIG_SOFTMMU
    bool direct_gpc = hamt_enabled() && can_do_hamt(lsenv->cpu_state);
#else
    bool direct_gpc = true;
#endif
    IR2_OPND next_eip = direct_gpc ? latxs_arg1_ir2_opnd :
                                      latxs_ra_alloc_itemp();
    latxs_load_ir1_to_ir2(&next_eip,
            ir1_get_opnd(pir1, 0), EXMode_Z);

    if (ir1_opnd_size(ir1_get_opnd(pir1, 0)) == 16) {
        latxs_append_ir2_opnd2_(lisa_mov16z, &next_eip, &next_eip);
    }

    if (!direct_gpc) {
        latxs_append_ir2_opnd2_(lisa_mov, &latxs_arg1_ir2_opnd, &next_eip);
        latxs_ra_free_temp(&next_eip);
    }
    latxs_tr_generate_exit_tb(pir1, 1);
    return true;
}

bool latxs_translate_cli(IR1_INST *pir1)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    CHECK_EXCP_CLI(pir1);

    IR2_OPND *zero = &latxs_zero_ir2_opnd;

    IR2_OPND eflags = latxs_ra_alloc_itemp();
    latxs_append_ir2_opnd2i(LISA_LD_WU, &eflags, &latxs_env_ir2_opnd,
            lsenv_offset_of_eflags(lsenv));

    IR2_OPND mask = latxs_ra_alloc_itemp();

    /* helper_cli */
    latxs_append_ir2_opnd2i(LISA_ORI, &mask, zero, IF_BIT);
    latxs_append_ir2_opnd3(LISA_NOR, &mask, zero, &mask);
    latxs_append_ir2_opnd3(LISA_AND, &eflags, &eflags, &mask);
    latxs_append_ir2_opnd2i(LISA_ST_W, &eflags, &latxs_env_ir2_opnd,
            lsenv_offset_of_eflags(lsenv));

    latxs_ra_free_temp(&eflags);
    latxs_ra_free_temp(&mask);

    return true;
}

/* End of TB in system-mode */
bool latxs_translate_sti(IR1_INST *pir1)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    CHECK_EXCP_CLI(pir1);

    IR2_OPND eflags = latxs_ra_alloc_itemp();
    latxs_append_ir2_opnd2i(LISA_LD_WU, &eflags, &latxs_env_ir2_opnd,
            lsenv_offset_of_eflags(lsenv));
    IR2_OPND *zero = &latxs_zero_ir2_opnd;

    IR2_OPND mask = latxs_ra_alloc_itemp();

    /* helper_sti */
    latxs_append_ir2_opnd2i(LISA_ORI, &mask, zero, IF_BIT);
    latxs_append_ir2_opnd3(LISA_OR, &eflags, &eflags, &mask);
    latxs_append_ir2_opnd2i(LISA_ST_W, &eflags, &latxs_env_ir2_opnd,
            lsenv_offset_of_eflags(lsenv));

    latxs_ra_free_temp(&eflags);
    latxs_ra_free_temp(&mask);

    if (inhibit_irq_hflags()) {
        /* sti is EOB in system-mode */
        lsenv->tr_data->inhibit_irq_in_hflags = 1;
    }

    return true;
}

bool latxs_translate_lidt(IR1_INST *pir1)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    CHECK_EXCP_LIDT(pir1);

    IR1_OPND *opnd = ir1_get_opnd(pir1, 0);
#ifdef CONFIG_SOFTMMU
    lsassertm_illop(ir1_addr(pir1),
            ir1_opnd_is_mem(opnd),
            "not a valid LIDT insn: pir1 = %p\n", (void *)pir1);
#endif

    /*
     * In i386: always load 6 bytes
     *  >  ir1_opnd_size(opnd) == 6, not a regular load operation
     *
     * 2 bytes for limit, and limit is always 16-bits long
     *
     * 4 bytes for base address, and base assress is 24-bits long
     * in real-address mode and vm86 mode, 32-bits long in PE mode.
     */
    IR2_OPND mem_base;
    int mem_offset;
    latxs_convert_mem_opnd(&mem_base, &mem_offset, opnd, -1);

    IR2_OPND limit = latxs_ra_alloc_itemp();
    IR2_OPND base  = latxs_ra_alloc_itemp();

    int save_temp = 1;

    /* 1. load 2 bytes for limit at MEM(addr)*/
    gen_ldst_softmmu_helper(LISA_LD_HU, &limit, &mem_base, mem_offset, save_temp);

    /* 2. load 4 bytes for base address at MEM(addr + 2)*/
    if (lsenv->tr_data->sys.code64) {
        gen_ldst_softmmu_helper(LISA_LD_D, &base, &mem_base, mem_offset + 2, save_temp);
    } else {
        gen_ldst_softmmu_helper(LISA_LD_WU, &base, &mem_base, mem_offset + 2, save_temp);
    }

    /* 3. 24-bits long base address in Real-Address mode and vm86 mode */
    if (latxs_ir1_data_size(pir1) == 16) {
        IR2_OPND tmp = latxs_ra_alloc_itemp();
        IR2_OPND tmp1 = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd2i(LISA_SRAI_D, &tmp, &base, 0x10);
        latxs_append_ir2_opnd2i(LISA_ANDI, &tmp, &tmp, 0xff);
        latxs_append_ir2_opnd2i(LISA_SLLI_D, &tmp, &tmp, 0x10);
        latxs_append_ir2_opnd2_(lisa_mov16z, &tmp1, &base);
        latxs_append_ir2_opnd3(LISA_OR, &tmp, &tmp, &tmp1);
        base = tmp;
    }

    /* 4. store limit/base into IDTR */
    latxs_append_ir2_opnd2i(LISA_ST_D, &base, &latxs_env_ir2_opnd,
            lsenv_offset_of_idtr_base(lsenv));

    latxs_append_ir2_opnd2i(LISA_ST_W, &limit, &latxs_env_ir2_opnd,
            lsenv_offset_of_idtr_limit(lsenv));

    return true;
}

bool latxs_translate_sidt(IR1_INST *pir1)
{
    IR1_OPND *opnd = ir1_get_opnd(pir1, 0);
#ifdef CONFIG_SOFTMMU
    lsassertm_illop(ir1_addr(pir1),
            ir1_opnd_is_mem(opnd),
            "not a valid SIDT insn: pir1 = %p\n", (void *)pir1);
#endif

    IR2_OPND mem_base;
    int mem_offset;
    latxs_convert_mem_opnd(&mem_base, &mem_offset, opnd, -1);

    /* 1. load limit/base into temp register */
    IR2_OPND base = latxs_ra_alloc_itemp();
    IR2_OPND limit = latxs_ra_alloc_itemp();

    latxs_append_ir2_opnd2i(LISA_LD_D, &base, &latxs_env_ir2_opnd,
            lsenv_offset_of_idtr_base(lsenv));
    // tcg_gen_ld32u_tl(s->T0, cpu_env, offsetof(CPUX86State, idt.limit));
    latxs_append_ir2_opnd2i(LISA_LD_WU, &limit, &latxs_env_ir2_opnd,
            lsenv_offset_of_idtr_limit(lsenv));

    int save_temp = 1;
    /* 2. store 2 bytes limit at MEM(addr) */
    gen_ldst_softmmu_helper(LISA_ST_H, &limit, &mem_base, mem_offset, save_temp);

    /* 3. store 4 bytes base at MEM(addr + 2) */
    if (latxs_ir1_addr_size(pir1) == 2) {
        latxs_append_ir2_opnd2_(lisa_mov24z, &base, &base);
    }
    if (lsenv->tr_data->sys.code64) {
        gen_ldst_softmmu_helper(LISA_ST_D, &base, &mem_base, mem_offset + 2, save_temp);
    } else {
        gen_ldst_softmmu_helper(LISA_ST_W, &base, &mem_base, mem_offset + 2, save_temp);
    }

    latxs_ra_free_temp(&base);
    latxs_ra_free_temp(&limit);
    return true;
}

bool latxs_translate_lgdt(IR1_INST *pir1)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    CHECK_EXCP_LGDT(pir1);

    IR1_OPND *opnd = ir1_get_opnd(pir1, 0);
#ifdef CONFIG_SOFTMMU
    lsassertm_illop(ir1_addr(pir1),
            ir1_opnd_is_mem(opnd),
            "not a valid LGDT insn: pir1 = %p\n", (void *)pir1);
#endif

    IR2_OPND mem_base;
    int mem_offset;
    latxs_convert_mem_opnd(&mem_base, &mem_offset, opnd, -1);

    IR2_OPND limit = latxs_ra_alloc_itemp();
    IR2_OPND base = latxs_ra_alloc_itemp();

    int save_temp = 1;

    /* 1. load 2 bytes for limit at MEM(addr)*/
    gen_ldst_softmmu_helper(LISA_LD_HU, &limit, &mem_base, mem_offset, save_temp);

    /* 2. load 4 bytes for base address at MEM(addr + 2)*/
    if (lsenv->tr_data->sys.code64) {
        gen_ldst_softmmu_helper(LISA_LD_D, &base, &mem_base, mem_offset + 2, save_temp);
    } else {
        gen_ldst_softmmu_helper(LISA_LD_WU, &base, &mem_base, mem_offset + 2, save_temp);
    }

    /* 3. 24-bits long base address in Real-Address mode and vm86 mode */
    if (latxs_ir1_data_size(pir1) == 16) {
        IR2_OPND tmp = latxs_ra_alloc_itemp();
        IR2_OPND tmp1 = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd2i(LISA_SRAI_D, &tmp, &base, 0x10);
        latxs_append_ir2_opnd2i(LISA_ANDI, &tmp, &tmp, 0xff);
        latxs_append_ir2_opnd2i(LISA_SLLI_D, &tmp, &tmp, 0x10);
        latxs_append_ir2_opnd2_(lisa_mov16z, &tmp1, &base);
        latxs_append_ir2_opnd3(LISA_OR, &tmp, &tmp, &tmp1);
        base = tmp;
    }

    /* 4. store limit/base into GDTR */
    latxs_append_ir2_opnd2i(LISA_ST_D, &base, &latxs_env_ir2_opnd,
            lsenv_offset_of_gdtr_base(lsenv));
    latxs_append_ir2_opnd2i(LISA_ST_W, &limit, &latxs_env_ir2_opnd,
            lsenv_offset_of_gdtr_limit(lsenv));

    return true;
}

bool latxs_translate_sgdt(IR1_INST *pir1)
{
    IR1_OPND *opnd = ir1_get_opnd(pir1, 0);
#ifdef CONFIG_SOFTMMU
    lsassertm_illop(ir1_addr(pir1),
            ir1_opnd_is_mem(opnd),
            "not a valid SGDT insn: pir1 = %p\n", (void *)pir1);
#endif

    IR2_OPND mem_base;
    int mem_offset;
    latxs_convert_mem_opnd(&mem_base, &mem_offset, opnd, -1);

    /* 0. mem_opnd might be temp register */
    int save_temp = 1;

    /* 1. load gdtr.limit from env */
    IR2_OPND limit = latxs_ra_alloc_itemp();
    latxs_append_ir2_opnd2i(LISA_LD_W, &limit, &latxs_env_ir2_opnd,
            lsenv_offset_of_gdtr_limit(lsenv));
    /* 2. store 16-bits limit at MEM(addr) */
    gen_ldst_softmmu_helper(LISA_ST_H, &limit, &mem_base, mem_offset, save_temp);
    latxs_ra_free_temp(&limit);

    /* 3. load gdtr.base  from env */
    IR2_OPND base = latxs_ra_alloc_itemp();
    latxs_append_ir2_opnd2i(LISA_LD_D, &base, &latxs_env_ir2_opnd,
            lsenv_offset_of_gdtr_base(lsenv));

    /* 4. store 32-bit base at MEM(addr + 2) */
    if (lsenv->tr_data->sys.code64) {
        gen_ldst_softmmu_helper(LISA_ST_D, &base, &mem_base, mem_offset + 2, save_temp);
    } else {
        gen_ldst_softmmu_helper(LISA_ST_W, &base, &mem_base, mem_offset + 2, save_temp);
    }

    latxs_ra_free_temp(&base);

    return true;
}

static void latxs_translate_lldt_gpr(IR1_INST *pir1, IR1_OPND *opnd0)
{
    /* helper might cause exception, save complete CPUX86State */
    latxs_tr_gen_call_to_helper_prologue_cfg(default_helper_cfg);

    /* void helper_lldt(CPUX86State  *env, int selector) */

    /* 1. get selector into arg1 (a1) */
    latxs_load_ir1_gpr_to_ir2(&latxs_arg1_ir2_opnd, opnd0, EXMode_Z);

    /* 2. call helper */
    latxs_append_ir2_opnd3(LISA_OR, &latxs_arg0_ir2_opnd,
            &latxs_env_ir2_opnd, &latxs_zero_ir2_opnd);
    if (hamt_enabled()) {
        latxs_tr_dmw_out_fix_mem(&latxs_arg0_ir2_opnd);
    }
    latxs_tr_gen_call_to_helper((ADDR)helper_lldt, HELPER_LLDT);

    latxs_tr_gen_call_to_helper_epilogue_cfg(default_helper_cfg);
}

static void latxs_translate_lldt_mem(IR1_INST *pir1, IR1_OPND *opnd0)
{
    /* 0. load selector value */
    IR2_OPND selector = latxs_ra_alloc_itemp();
    latxs_load_ir1_mem_to_ir2(&selector, opnd0, EXMode_Z, -1);

    /* helper might cause exception, save complete CPUX86State */
    latxs_tr_gen_call_to_helper_prologue_cfg(default_helper_cfg);

    /* void helper_lldt(CPUX86State  *env, int selector) */

    /* 1. get selector into arg1 (a1) */
    latxs_append_ir2_opnd3(LISA_OR, &latxs_arg1_ir2_opnd,
            &selector, &latxs_zero_ir2_opnd);

    /* 2. call helper */
    latxs_append_ir2_opnd3(LISA_OR, &latxs_arg0_ir2_opnd,
            &latxs_env_ir2_opnd, &latxs_zero_ir2_opnd);
    if (hamt_enabled()) {
        latxs_tr_dmw_out_fix_mem(&latxs_arg0_ir2_opnd);
    }
    latxs_tr_gen_call_to_helper((ADDR)helper_lldt, HELPER_LLDT);

    latxs_tr_gen_call_to_helper_epilogue_cfg(default_helper_cfg);
}

bool latxs_translate_lldt(IR1_INST *pir1)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    CHECK_EXCP_LLDT(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0); /* GPR or MEM */

    if (ir1_opnd_is_gpr(opnd0)) {
        latxs_translate_lldt_gpr(pir1, opnd0);
    } else {
#ifdef CONFIG_SOFTMMU
        lsassertm_illop(ir1_addr(pir1),
                ir1_opnd_is_mem(opnd0),
                "not a valid LLDT insn: pir1 = %p\n", (void *)pir1);
#endif

        latxs_translate_lldt_mem(pir1, opnd0);
    }

    return true;
}

bool latxs_translate_sldt(IR1_INST *pir1)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    CHECK_EXCP_SLDT(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0); /* GPR or MEM */

    /* load ldtr.selector from env */
    IR2_OPND selector = latxs_ra_alloc_itemp();
    latxs_append_ir2_opnd2i(LISA_LD_W, &selector, &latxs_env_ir2_opnd,
            lsenv_offset_of_ldtr_selector(lsenv));

    if (ir1_opnd_is_mem(opnd0)) {
        lsassert(ir1_opnd_size(opnd0) == 16);
    }

    latxs_store_ir2_to_ir1(&selector, opnd0);

    return true;
}

static void latxs_translate_ltr_gpr(IR1_INST *pir1, IR1_OPND *opnd0)
{
    /* helper might cause exception, save complete CPUX86State */
    latxs_tr_gen_call_to_helper_prologue_cfg(default_helper_cfg);

    /* void helper_ltr(CPUX86State  *env, int selector) */

    /* 1. get selector into arg1 (a1) */
    latxs_load_ir1_gpr_to_ir2(&latxs_arg1_ir2_opnd, opnd0, EXMode_Z);

    /* 2. call helper */
    latxs_append_ir2_opnd3(LISA_OR, &latxs_arg0_ir2_opnd,
            &latxs_env_ir2_opnd, &latxs_zero_ir2_opnd);
    if (hamt_enabled()) {
        latxs_tr_dmw_out_fix_mem(&latxs_arg0_ir2_opnd);
    }
    latxs_tr_gen_call_to_helper((ADDR)helper_ltr, HELPER_LTR);

    latxs_tr_gen_call_to_helper_epilogue_cfg(default_helper_cfg);
}

static void latxs_translate_ltr_mem(IR1_INST *pir1, IR1_OPND *opnd0)
{
    IR2_OPND selector = latxs_ra_alloc_itemp();
    latxs_load_ir1_mem_to_ir2(&selector, opnd0, EXMode_Z, -1);

    /* helper might cause exception, save complete CPUX86State */
    latxs_tr_gen_call_to_helper_prologue_cfg(default_helper_cfg);

    /* void helper_ltr(CPUX86State  *env, int selector) */

    /* 1. get selector into arg1 (a1) */
    latxs_append_ir2_opnd3(LISA_OR, &latxs_arg1_ir2_opnd,
            &selector, &latxs_zero_ir2_opnd);

    /* 2. call helper */
    latxs_append_ir2_opnd3(LISA_OR, &latxs_arg0_ir2_opnd,
            &latxs_env_ir2_opnd, &latxs_zero_ir2_opnd);
    if (hamt_enabled()) {
        latxs_tr_dmw_out_fix_mem(&latxs_arg0_ir2_opnd);
    }
    latxs_tr_gen_call_to_helper((ADDR)helper_ltr, HELPER_LTR);

    latxs_tr_gen_call_to_helper_epilogue_cfg(default_helper_cfg);
}

bool latxs_translate_ltr(IR1_INST *pir1)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    CHECK_EXCP_LTR(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0); /* GPR or MEM */

    if (ir1_opnd_is_gpr(opnd0)) {
        latxs_translate_ltr_gpr(pir1, opnd0);
    } else {
#ifdef CONFIG_SOFTMMU
        lsassertm_illop(ir1_addr(pir1),
                ir1_opnd_is_mem(opnd0),
                "not a valid LTR insn: pir1 = %p\n", (void *)pir1);
#endif
        latxs_translate_ltr_mem(pir1, opnd0);
    }

    return true;
}

bool latxs_translate_str(IR1_INST *pir1)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    CHECK_EXCP_STR(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);

    /* load tr.selector from env */
    IR2_OPND selector = latxs_ra_alloc_itemp();
    latxs_append_ir2_opnd2i(LISA_LD_W, &selector, &latxs_env_ir2_opnd,
            lsenv_offset_of_tr_selector(lsenv));

    latxs_store_ir2_to_ir1(&selector, opnd0);

    return true;
}

/*
 * push/pop
 *
 * In system-mode, for precise exception, the effect of instruction
 * should be executed at the end of this instruction's translated code.
 *
 * For pop, these two things need to be done at the end:
 *  1> save the read value into destination
 *  2> update esp register
 *
 * For push, since the write is done by softmmu, and the exception can
 * only be generated by the softmmu helper, only one thing need to be
 * done at the end:
 *  1> update esp register
 */

/* End of TB in system-mode : pop es/ss/ds */
bool latxs_translate_pop(IR1_INST *pir1)
{
    TranslationBlock *tb = lsenv->tr_data->curr_tb;
    /*
     * pop
     * ----------------------
     * >  dest <= MEM(SS:ESP)
     * >  ESP  <= ESP + 2/4
     * ----------------------
     * 1. tmp  <= MEM(SS:ESP) : softmmu
     * 2. ESP  <= ESP + 2/4
     * 3. tmp  => dest : gpr/mem/seg
     */
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);

    TRANSLATION_DATA *td = lsenv->tr_data;
    int ss_addr_size = latxs_get_sys_stack_addr_size();
    bool pushpop = get_pushpop_option(td);

    bool is_gpr_esp = 0;
    bool is_mem_esp = 0;

    if (ir1_opnd_is_gpr(opnd0)) {
        if (ir1_opnd_base_reg_num(opnd0) == esp_index) {
            is_gpr_esp = 1;
            if (td->sp_patch) {
                latxs_update_stack_pointer(td->sp_patch, ss_addr_size);
                td->sp_patch = 0;
                int *opt = tb_opt_ptr(tb);
                g_assert(opt);
                opt[td->curr_ir1_nr] = 0;
            }
            pushpop = false;
        }
    } else if (ir1_opnd_is_mem(opnd0)) {
        if (!ir1_opnd_is_pc_relative(opnd0))
        if (ir1_opnd_base_reg_num(opnd0) == esp_index) {
            is_mem_esp = 1;
        }
    }

    int data_size = latxs_ir1_data_size(pir1);
    if (lsenv->tr_data->sys.code64) {
        data_size = (data_size == 16) ? 16 : 64;
    }

    int opnd_size = ir1_opnd_size(opnd0);
    if (lsenv->tr_data->sys.code64) {
        lsassert(opnd_size == 16 || opnd_size == 64);
    } else {
        lsassert(opnd_size == 16 || opnd_size == 32);
    }
    if (!ir1_opnd_is_seg(opnd0)) {
        lsassert(data_size == opnd_size);
    }

    lsassert(data_size >= opnd_size);
    (void)data_size; /* to avoid compile warning */
    (void)opnd_size; /* to avoid compile warning */

    /* 1.1 build MEM(SS:ESP) */
    IR1_OPND mem_ir1_opnd;
    if (pushpop) {
        ir1_opnd_build_full_mem(&mem_ir1_opnd, data_size,
            X86_REG_SS, X86_REG_ESP, td->sp_patch, 0, 0);
    } else {
        ir1_opnd_build_full_mem(&mem_ir1_opnd, data_size,
                X86_REG_SS, X86_REG_ESP, 0, 0, 0);
    }

    /* 1.2 read data from stack   : might generate exception */
    int pop_to_gpr_directly = 0;
    IR2_OPND tmp;

    if (option_by_hand_64 && ir1_opnd_is_gpr(opnd0)
        && !is_gpr_esp
        && ss_addr_size == 8
        && data_size == 64) {
        pop_to_gpr_directly = 1;
        tmp = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd0));
    } else {
        tmp = latxs_ra_alloc_itemp();
    }
    latxs_load_ir1_mem_to_ir2(&tmp, &mem_ir1_opnd, EXMode_Z,
             ss_addr_size);

    /* 2. update ESP */
    int esp_inc = data_size >> 3;
    bool update_esp = true;
    if (pushpop) {
        update_esp = false;
        td->sp_patch += esp_inc;
    }
    if (update_esp && is_gpr_esp) {
        latxs_update_stack_pointer(esp_inc, ss_addr_size);
    }

    /* 3. write into destination  : might generate exception */
    if (is_mem_esp) {
        td->sys.popl_esp_hack = data_size >> 3;
        latxs_store_ir2_to_ir1_mem(&tmp, opnd0, ss_addr_size);
        td->sys.popl_esp_hack = 0;
    } else {
        if (!pop_to_gpr_directly) {
            latxs_store_ir2_to_ir1(&tmp, opnd0);
        }
    }

    /*
     * Order is important for pop %esp
     * The %esp is increased first. Then the pop value is loaded into %esp.
     * This executing order is the same as that in TCG.
     */
    if (update_esp && !is_gpr_esp) {
        latxs_update_stack_pointer(esp_inc, ss_addr_size);
    }
    if (!update_esp) {
#ifdef CONFIG_LATX_PROFILER
        per_tb_count((void *)&((tb->profile).pushpop_sub), 1);
#endif
    }
    return true;
}

bool latxs_translate_push(IR1_INST *pir1)
{
#ifdef CONFIG_LATX_PROFILER
    TranslationBlock *tb = lsenv->tr_data->curr_tb;
#endif
    /*
     * push
     * ----------------------
     * >  ESP  <= ESP - 2/4
     * >  src  => MEM(SS:ESP)
     * ----------------------
     * 1. tmp  <= source : gpr/mem/seg
     * 2. tmp  => MEM(SS:ESP - 2/4) : softmmu
     * 3. ESP  <= ESP - 2/4
     */
    TRANSLATION_DATA *td = lsenv->tr_data;
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    int ss_addr_size = latxs_get_sys_stack_addr_size();

    bool pushpop = get_pushpop_option(td);
    if (pushpop && td->sp_patch && !ir1_opnd_is_imm(opnd0) &&
        (ir1_opnd_base_reg_num(opnd0) == esp_index)) {
#ifdef CONFIG_LATX_PROFILER
        per_tb_count((void *)&((tb->profile).pushpop_add), 1);
#endif
        latxs_update_stack_pointer(td->sp_patch, ss_addr_size);
        td->sp_patch = 0;
        pushpop = false;
    }

    int data_size = latxs_ir1_data_size(pir1);

    if (lsenv->tr_data->sys.code64) {
        data_size = (data_size == 16) ? 16 : 64;
    }

    int opnd_size = ir1_opnd_size(opnd0);
    if (lsenv->tr_data->sys.code64) {
        if (!ir1_opnd_is_imm(opnd0)) {
            lsassert(opnd_size == 16 || opnd_size == 64);
        }
    } else if (!ir1_opnd_is_imm(opnd0)) {
        lsassert(opnd_size == 16 || opnd_size == 32);
    }
    if (!ir1_opnd_is_seg(opnd0) && !ir1_opnd_is_imm(opnd0)) {
        lsassert(data_size == opnd_size);
    }
    (void)opnd_size; /* to avoid compile warning */

    /* 1. load source data   : might generate exception */
    IR2_OPND tmp;
    if (option_by_hand_64 && ir1_opnd_is_gpr(opnd0)) {
        tmp = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd0));
    } else {
        tmp = latxs_load_ireg_from_ir1(opnd0, EXMode_N);
    }

    /* 2.1 build MEM(SS:ESP - 2/4)*/
    int esp_dec = 0 - (data_size >> 3);
    IR1_OPND mem_ir1_opnd;

    /* 3. update ESP */
    if (pushpop) {
        td->sp_patch += esp_dec;
        ir1_opnd_build_full_mem(&mem_ir1_opnd, data_size,
                X86_REG_SS, X86_REG_ESP, td->sp_patch, 0, 0);
    } else {
        /* 2.1 build MEM(SS:ESP - 2/4)*/
        ir1_opnd_build_full_mem(&mem_ir1_opnd, data_size,
                X86_REG_SS, X86_REG_ESP, esp_dec, 0, 0);
    }

    /* 2.2 write data into stack   : might generate exception */
    latxs_store_ir2_to_ir1_mem(&tmp, &mem_ir1_opnd,
             ss_addr_size);

    if (!pushpop) {
        latxs_update_stack_pointer(esp_dec, ss_addr_size);
    } else {
#ifdef CONFIG_LATX_PROFILER
        per_tb_count((void *)&((tb->profile).pushpop_sub), 1);
#endif
    }
    return true;
}

bool latxs_translate_ret(IR1_INST *pir1)
{
    /*
     * ESP update   according to dflag(opnd size)
     * Value size   according to dflag(opnd size)
     * Address size according to aflag(addr size) : load ret addr
     *
     * code32 || (code16 && prefix_data) : dflag = 4
     * code16 || (code32 && prefix_data) : dflag = 2
     *
     * aflag = ir1_addr_size(pir1) : provided by capstone
     * code32 || (code16 && prefix_addr) : aflag = 4
     * code16 || (code32 && prefix_addr) : aflag = 2
     *
     * Usually the dflag(opnd size) stores in inst's operand.
     * But 'ret' is allowed to have zero operand. And if it has
     * one operand, it must be 'imm16' which must has 16-bit opnd size.
     * So there is no way to get the dflag from pir1 itself.
     *
     * In normal situation, the capstone will translate zero operand with
     * different opnd size into serival instruction, such as 'ins'.
     * But near 'ret' has only one instruction in capstone......
     */

    int data_size = latxs_ir1_data_size(pir1);
    if (lsenv->tr_data->sys.code64) {
        data_size = (data_size == 16) ? 16 : 64;
    }
    int data_size_max = 32;
    if (lsenv->tr_data->sys.code64) data_size_max = 64;
    lsassert(data_size == 16 || data_size == data_size_max);
    (void)data_size_max;

    TRANSLATION_DATA *td = lsenv->tr_data;
    int pending = latxs_ret_can_consume_sp(pir1) ? td->sp_patch : 0;
    /* The pre-translate hook uses the same predicate; a rejected delta has
     * already been committed. Read the real guest return address first. */
    /* 1. load ret_addr into $25 from MEM(SS:ESP) */
    IR1_OPND mem_ir1_opnd;
    ir1_opnd_build_full_mem(&mem_ir1_opnd, data_size,
            X86_REG_SS, X86_REG_ESP, pending, 0, 0);
#ifdef CONFIG_SOFTMMU
    bool direct_gpc = hamt_enabled() && can_do_hamt(lsenv->cpu_state);
#else
    bool direct_gpc = true;
#endif
    IR2_OPND return_addr_opnd = direct_gpc ? latxs_arg1_ir2_opnd :
                                              latxs_ra_alloc_itemp();
    int ss_addr_size = latxs_get_sys_stack_addr_size();
    latxs_load_ir1_mem_to_ir2(&return_addr_opnd,
            &mem_ir1_opnd, EXMode_Z, ss_addr_size);

    /* 2. update ESP */
    int esp_inc = (data_size >> 3);
    if (ir1_opnd_num(pir1) && ir1_opnd_is_imm(ir1_get_opnd(pir1, 0))) {
        /* ret imm */
        esp_inc = ir1_opnd_uimm(ir1_get_opnd(pir1, 0)) + (data_size >> 3);
    }
    latxs_update_stack_pointer(pending + esp_inc, ss_addr_size);
    td->sp_patch = 0;

    /* SoftMMU uses a1 as scratch; publish the target after memory access. */
    if (!direct_gpc) {
        latxs_append_ir2_opnd2_(lisa_mov, &latxs_arg1_ir2_opnd, &return_addr_opnd);
        latxs_ra_free_temp(&return_addr_opnd);
    }
    /* A shadow RET uses the ordinary exit instead of a cached direct jump. */
    if (option_soft_ras &&
        !(lsenv->tr_data->sys.flags & HF_INHIBIT_IRQ_MASK)) {
        latxs_translate_ras_pop(&latxs_arg1_ir2_opnd);
    }
    latxs_tr_generate_exit_tb(pir1, 0);

    return true;
}

bool latxs_translate_cpuid(IR1_INST *pir1)
{
    /* 0. save next instruciton's EIP to env */
    latxs_tr_gen_save_curr_eip();

    /*
     * 1. call helper_cpuid
     *
     * void helper_cpuid(
     *      CPUX86State *env)
     * >> load new EAX/ECX/EDX/EBX
     */
    latxs_tr_gen_call_to_helper1_cfg((ADDR)helper_cpuid, nofp_helper_cfg, HELPER_CPUID);

    return true;
}

bool latxs_translate_xchg(IR1_INST *pir1)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    if ((lsenv->tr_data->sys.cflags & CF_PARALLEL) &&
        (ir1_opnd_is_mem(opnd0) || ir1_opnd_is_mem(opnd1))) {
        return latxs_translate_lock_xchg(pir1);
    }

    /* if two src is the same reg, do nothing */
    if (ir1_opnd_is_gpr(opnd0) && ir1_opnd_is_gpr(opnd1)) {
        if ((ir1_opnd_size(opnd0) ==
             ir1_opnd_size(opnd1)) &&
            (ir1_opnd_base_reg_num(opnd0) ==
             ir1_opnd_base_reg_num(opnd1)) &&
            (ir1_opnd_base_reg_bits_start(opnd0) ==
             ir1_opnd_base_reg_bits_start(opnd1))) {
            if (ir1_opnd_size(opnd0) == 32) {
                IR2_OPND reg_opnd =
                    latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd0));
                latxs_append_ir2_opnd2_(lisa_mov32z, &reg_opnd, &reg_opnd);
            }
            return true;
        }
    }

    IR2_OPND src_opnd_0, src_opnd_1;
    /* Do memory access first for precise exception */
    if (ir1_opnd_is_gpr(opnd0)) {
        src_opnd_0 = latxs_load_ireg_from_ir1(opnd0, EXMode_N);
        src_opnd_1 = latxs_ra_alloc_itemp();
        latxs_load_ir1_to_ir2(&src_opnd_1, opnd1, EXMode_N);
        latxs_store_ir2_to_ir1(&src_opnd_0, opnd1);
        latxs_store_ir2_to_ir1(&src_opnd_1, opnd0);
    } else {
        src_opnd_0 = latxs_ra_alloc_itemp();
        latxs_load_ir1_to_ir2(&src_opnd_0, opnd0, EXMode_N);
        src_opnd_1 = latxs_load_ireg_from_ir1(opnd1, EXMode_N);
        latxs_store_ir2_to_ir1(&src_opnd_1, opnd0);
        latxs_store_ir2_to_ir1(&src_opnd_0, opnd1);
    }

    return true;
}

bool latxs_translate_cmpxchg(IR1_INST *pir1)
{
    if (latxs_ir1_has_prefix_lock(pir1) &&
        (lsenv->tr_data->sys.cflags & CF_PARALLEL)) {
        return latxs_translate_lock_cmpxchg(pir1);
    }

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    IR1_OPND *reg_ir1 = NULL;

    int opnd_size = ir1_opnd_size(opnd0);
    switch (opnd_size) {
    case 8:
        reg_ir1 = &al_ir1_opnd;
        break;
    case 16:
        reg_ir1 = &ax_ir1_opnd;
        break;
    case 32:
        reg_ir1 = &eax_ir1_opnd;
        break;
    case 64:
        lsassert(lsenv->tr_data->sys.code64);
        reg_ir1 = &rax_ir1_opnd;
        break;
    default:
        lsassert(0);
        break;
    }

    IR2_OPND src_opnd_0 = latxs_ra_alloc_itemp();
    IR2_OPND src_opnd_1 = latxs_ra_alloc_itemp();
    IR2_OPND eax_opnd = latxs_ra_alloc_itemp();

    latxs_load_ir1_to_ir2(&src_opnd_0, opnd0, EXMode_S);
    latxs_load_ir1_to_ir2(&src_opnd_1, opnd1, EXMode_S);
    latxs_load_ir1_to_ir2(&eax_opnd, reg_ir1, EXMode_S);

    IR2_OPND label_unequal = latxs_ir2_opnd_new_label();
    latxs_append_ir2_opnd3(LISA_BNE,
            &src_opnd_0, &eax_opnd, &label_unequal);

    /* equal */
    latxs_store_ir2_to_ir1(&src_opnd_1, opnd0);
    latxs_ra_free_temp(&src_opnd_1);

    IR2_OPND label_exit = latxs_ir2_opnd_new_label();
    latxs_append_ir2_opnd1(LISA_B, &label_exit);

    /* unequal */
    latxs_append_ir2_opnd1(LISA_LABEL, &label_unequal);
    latxs_store_ir2_to_ir1_gpr(&src_opnd_0, reg_ir1);
    latxs_append_ir2_opnd1(LISA_LABEL, &label_exit);

    /*
     * calculate elfags after compare and exchange(store)
     *
     * dest = eax_opnd - src_opnd_0 (LISA_SUB_D)
     * with LBT, dest is not necessary
     */
    latxs_generate_eflag_calculation(NULL,
            &eax_opnd, &src_opnd_0, pir1, true);

    return true;
}

bool latxs_translate_cmpxchg8b(IR1_INST *pir1)
{
    if (latxs_ir1_has_prefix_lock(pir1) &&
        (lsenv->tr_data->sys.cflags & CF_PARALLEL)) {
        return latxs_translate_lock_cmpxchg8b(pir1);
    }

    TRANSLATION_DATA *td = lsenv->tr_data;
    CHECK_EXCP_ARPL(pir1);

    /* 1. check illegal operation exception */
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
#ifdef CONFIG_SOFTMMU
    lsassert_illop(ir1_addr(pir1), ir1_opnd_is_mem(opnd0));
#endif

    /* 2. get memory address */
    IR2_OPND mem_base;
    int mem_offset;
    latxs_convert_mem_opnd(&mem_base, &mem_offset, opnd0, -1);
    if (mem_offset != 0) {
        latxs_append_ir2_opnd2i(LISA_ADDI_D, &mem_base, &mem_base, mem_offset);
#ifdef LATXS_IMM_CACHE_ENABLE
        imm_cache_addi_update(mem_offset);
#endif
    }

    /* 3. select helper function */
    ADDR helper_addr = (ADDR)helper_latx_cmpxchg8b_unlocked;

    /* 4. call that helper */
    /* 4.1 save context */
    latxs_tr_gen_call_to_helper_prologue_cfg(nofp_helper_cfg);
    /* 4.2 arg1: address */
    latxs_append_ir2_opnd2_(lisa_mov, &latxs_arg1_ir2_opnd, &mem_base);
    /* 4.3 arg0: env */
    latxs_append_ir2_opnd2_(lisa_mov, &latxs_arg0_ir2_opnd,
                                      &latxs_env_ir2_opnd);
    /* 4.4 call helper */
    latxs_tr_gen_call_to_helper(helper_addr, HELPER_LATX_CMPXCHG8B_UNLOCKED);
    /* 4.5 restore context */
    latxs_tr_gen_call_to_helper_epilogue_cfg(nofp_helper_cfg);

    return true;
}

bool latxs_translate_cmpxchg16b(IR1_INST *pir1)
{
    if (latxs_ir1_has_prefix_lock(pir1) &&
        (lsenv->tr_data->sys.cflags & CF_PARALLEL)) {
        return latxs_translate_lock_cmpxchg16b(pir1);
    }

    TRANSLATION_DATA *td = lsenv->tr_data;
    CHECK_EXCP_ARPL(pir1);

    /* 1. check illegal operation exception */
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
#ifdef CONFIG_SOFTMMU
    lsassert_illop(ir1_addr(pir1), ir1_opnd_is_mem(opnd0));
#endif

    /* 2. get memory address */
    IR2_OPND mem_base;
    int mem_offset;
    latxs_convert_mem_opnd(&mem_base, &mem_offset, opnd0, -1);
    if (mem_offset != 0) {
        latxs_append_ir2_opnd2i(LISA_ADDI_D, &mem_base, &mem_base, mem_offset);
#ifdef LATXS_IMM_CACHE_ENABLE
        imm_cache_addi_update(mem_offset);
#endif
    }

    /* 3. select helper function */
    ADDR helper_addr = (ADDR)helper_latx_cmpxchg16b_unlocked;

    /* 4. call that helper */
    /* 4.1 save context */
    latxs_tr_gen_call_to_helper_prologue_cfg(nofp_helper_cfg);
    /* 4.2 arg1: address */
    latxs_append_ir2_opnd2_(lisa_mov, &latxs_arg1_ir2_opnd, &mem_base);
    /* 4.3 arg0: env */
    latxs_append_ir2_opnd2_(lisa_mov, &latxs_arg0_ir2_opnd,
                                      &latxs_env_ir2_opnd);
    /* 4.4 call helper */
    latxs_tr_gen_call_to_helper(helper_addr, HELPER_LATX_CMPXCHG16B_UNLOCKED);
    /* 4.5 restore context */
    latxs_tr_gen_call_to_helper_epilogue_cfg(nofp_helper_cfg);

    return true;
}

/* End of TB in system-mode */
bool latxs_translate_rsm(IR1_INST *pir1)
{
#ifdef CONFIG_SOFTMMU
    TRANSLATION_DATA *td = lsenv->tr_data;
    CHECK_EXCP_RSM(pir1);

    /* 1. save next instruciton's EIP to env */
    latxs_tr_gen_save_next_eip();

    /*
     * 2. helper_rsm
     *
     * target/i386/smm_helper.c
     * void helper_rsm(
     *      CPUX86State *env)
     */
    latxs_tr_gen_call_to_helper1_cfg((ADDR)helper_rsm, nofp_helper_cfg, HELPER_RSM);

    /* the eip is already updated in helper_rsm */
    lsenv->tr_data->ignore_eip_update = 1;

    return true;
#else
    return false;
#endif
}

bool latxs_translate_invd(IR1_INST *pir1)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    CHECK_EXCP_INVD(pir1);
    /* nothing to do */
    return true;
}

bool latxs_translate_wbinvd(IR1_INST *pir1)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    CHECK_EXCP_WBINVD(pir1);
    /* nothing to do */
    return true;
}

/* End of TB in system-mode : reps nop */
bool latxs_translate_nop(IR1_INST *pir1)
{
    if (option_pause) {
        return true;
    }
    if (latxs_ir1_is_repz_nop(pir1)) {
        /* 0. save current instruciton's EIP to env */
        latxs_tr_gen_save_curr_eip();

        /* 1. save full context */
        helper_cfg_t cfg = default_helper_cfg;
        latxs_tr_gen_call_to_helper_prologue_cfg(cfg);

        /*
         * 2. call helper_pause
         *
         * target/i386/misc_helper.c
         * void helper_pause(
         *      CPUX86State *env,
         *      int next_eip_addend)
         */

        /* 2.1 arg0: env */
        latxs_append_ir2_opnd3(LISA_OR, &latxs_arg0_ir2_opnd,
                &latxs_env_ir2_opnd, &latxs_zero_ir2_opnd);
        if (hamt_enabled()) {
            latxs_tr_dmw_out_fix_mem(&latxs_arg0_ir2_opnd);
        }
        /* 2.2 arg1: this inst's size, 12-bit is enough */
        int size = latxs_ir1_inst_size(pir1);
        latxs_append_ir2_opnd2i(LISA_ORI, &latxs_arg1_ir2_opnd,
                &latxs_zero_ir2_opnd, size & 0xfff);
        /* 2.3 call helper pause */
        latxs_tr_gen_call_to_helper((ADDR)helper_pause, HELPER_PAUSE);

        /* 3. This helper never return */
        latxs_tr_gen_infinite_loop();

    } else {
        if (pir1->info->size == 4 && pir1->info->bytes[0] == 0x0f &&
            pir1->info->bytes[1] == 0x1f && pir1->info->bytes[2] == 0x40) {
            int func = pir1->info->bytes[3];
            if (func >= 0xa0) {
                do_func(ir1_addr(pir1), func);
            }
        }
#ifdef CONFIG_LATX_TU
        latxs_append_ir2_opnd2i(LISA_ORI, &latxs_zero_ir2_opnd, &latxs_zero_ir2_opnd, 0);
#endif
    }

    return true;
}

/* End of TB in system-mode */
bool latxs_translate_pause(IR1_INST *pir1)
{
    if (option_pause) {
        return true;
    }
    /* 0. save current instruciton's EIP to env */
    latxs_tr_gen_save_curr_eip();

    /* 1. save full context */
    helper_cfg_t cfg = default_helper_cfg;
    latxs_tr_gen_call_to_helper_prologue_cfg(cfg);

    /*
     * 2. call helper_pause
     *
     * target/i386/misc_helper.c
     * void helper_pause(
     *      CPUX86State *env,
     *      int next_eip_addend)
     */

    /* 2.1 arg0: env */
    latxs_append_ir2_opnd3(LISA_OR, &latxs_arg0_ir2_opnd,
            &latxs_env_ir2_opnd, &latxs_zero_ir2_opnd);
    if (hamt_enabled()) {
        latxs_tr_dmw_out_fix_mem(&latxs_arg0_ir2_opnd);
    }
    /* 2.2 arg1: this inst's size, 16-bit is enough */
    int size = latxs_ir1_inst_size(pir1);
    latxs_append_ir2_opnd2i(LISA_ORI, &latxs_arg1_ir2_opnd,
            &latxs_zero_ir2_opnd, size & 0xfff);
    /* 2.3 call helper pause */
    latxs_tr_gen_call_to_helper((ADDR)helper_pause, HELPER_PAUSE);

    /* 3. This helper never return */
    latxs_tr_gen_infinite_loop();

    return true;
}

static inline void fast_load_segment_ra(IR2_OPND *e1_ptr,
                                IR2_OPND *e2_ptr,
                                IR2_OPND *selector,
                                IR2_OPND *label_helper)
{
    IR2_OPND tmp = latxs_ra_alloc_itemp();
    IR2_OPND tmp0 = latxs_arg0_ir2_opnd;

    IR2_OPND end_dt = latxs_ir2_opnd_new_label();
    IR2_OPND label_gdt = latxs_ir2_opnd_new_label();

    la_out2i(LISA_ANDI, &tmp, selector, 0x4);
    la_out2(LISA_BEQZ, &tmp, &label_gdt);

    la_out2i(LISA_LD_WU, e1_ptr, &latxs_env_ir2_opnd,
        lsenv_offset_of_ldt_limit(lsenv));
    la_out2i(LISA_LD_D, &tmp, &latxs_env_ir2_opnd,
        lsenv_offset_of_ldt_base(lsenv));
    la_out1(LISA_B, &end_dt);

    la_out1(LISA_LABEL, &label_gdt);
    la_out2i(LISA_LD_WU, e1_ptr, &latxs_env_ir2_opnd,
        lsenv_offset_of_gdt_limit(lsenv));
    la_out2i(LISA_LD_D, &tmp, &latxs_env_ir2_opnd,
        lsenv_offset_of_gdt_base(lsenv));

    la_out1(LISA_LABEL, &end_dt);

    /* index = selector & ~7 */
    /* (index + 7) > dt->limit, goto helper */
    latxs_load_imm64(e2_ptr, 0x7);
    la_out3(LISA_ANDN, e2_ptr, selector, e2_ptr);
    la_out2i(LISA_ADDI_D, &tmp0, e2_ptr, 0x7);

    la_out3(LISA_BLTU, e1_ptr, &tmp0, label_helper);

    /* ptr = dt->base + index */
    la_out3(LISA_ADD_D, &tmp, &tmp, e2_ptr);

    gen_ldst_softmmu_helper(LISA_LD_WU, e1_ptr, &tmp, 0, 1);
    gen_ldst_softmmu_helper(LISA_LD_WU, e2_ptr, &tmp, 4, 1);

    latxs_ra_free_temp(&tmp);
}

static inline void fast_get_seg_limit(IR2_OPND *e1, IR2_OPND *e2, IR2_OPND *limit)
{
    IR2_OPND tmp0 = latxs_arg0_ir2_opnd;
    IR2_OPND label_end = latxs_ir2_opnd_new_label();

    /* limit = (e1 & 0xffff) | (e2 & 0x000f0000); */
    latxs_load_imm64(&tmp0, 0xffff);
    la_out3(LISA_AND, limit, e1, &tmp0);

    latxs_load_imm64(&tmp0, 0xf0000);
    la_out3(LISA_AND, &tmp0, e2, &tmp0);

    la_out3(LISA_OR, limit, limit, &tmp0);

    /* if (e2 & DESC_G_MASK) */
    latxs_load_imm64(&tmp0, DESC_G_MASK);
    la_out3(LISA_AND, &tmp0, e2, &tmp0);
    la_out2(LISA_BEQZ, &tmp0, &label_end);

    /* limit = (limit << 12) | 0xfff */
    la_out2i(LISA_SLLI_D, limit, limit, 12);
    la_out2i(LISA_ORI, limit, limit, 0xfff);

    la_out1(LISA_LABEL, &label_end);
}

static inline void fast_get_seg_base(IR2_OPND *e1, IR2_OPND *e2, IR2_OPND *base)
{
    IR2_OPND tmp0 = latxs_arg0_ir2_opnd;

    /* (e1 >> 16) | ((e2 & 0xff) << 16) | (e2 & 0xff000000) */
    la_out2i(LISA_SRLI_D, base, e1, 16);
    la_out2i(LISA_ANDI, &tmp0, e2, 0xff);
    la_out2i(LISA_SLLI_D, &tmp0, &tmp0, 16);

    la_out3(LISA_OR, base, base, &tmp0);

    latxs_load_imm64(&tmp0, 0xff000000);
    la_out3(LISA_AND, &tmp0, e2, &tmp0);

    la_out3(LISA_OR, base, base, &tmp0);
}

/*
 * copy from cpu_sync_bndcs_hflags(CPUX86State *env),
 *
 */
static inline void latx_cpu_sync_bndcs_hflags(
                IR2_OPND *hflags, IR2_OPND *hflags2)
{
    /*
     * hflags &= ~HF_MPX_EN_MASK;
     * env->hflags = hflags;
     */
    latxs_load_imm64(hflags2, HF_MPX_EN_MASK);
    la_out3(LISA_ANDN, hflags, hflags, hflags2);
    latxs_append_ir2_opnd2i(LISA_ST_W, hflags, &latxs_env_ir2_opnd,
        lsenv_offset_of_hflags(lsenv));

    /*
     * hflags2 &= ~HF2_MPX_PR_MASK;
     * env->hflags2 = hflags2;
     */
    latxs_append_ir2_opnd2i(LISA_LD_WU, hflags2, &latxs_env_ir2_opnd,
        lsenv_offset_of_hflags2(lsenv));
    latxs_load_imm64(hflags, HF2_MPX_PR_MASK);
    la_out3(LISA_ANDN, hflags2, hflags2, hflags);
    latxs_append_ir2_opnd2i(LISA_ST_W, hflags2, &latxs_env_ir2_opnd,
        lsenv_offset_of_hflags2(lsenv));
}

static inline void fast_cpu_x86_load_seg_cache(
                X86Seg seg_reg, IR2_OPND *selector,
                IR2_OPND *base, IR2_OPND *limit,
                IR2_OPND *flags)
{
    IR2_OPND tmp = latxs_arg0_ir2_opnd;
    IR2_OPND tmp1 = latxs_arg1_ir2_opnd;
    IR2_OPND tmp2 = latxs_arg2_ir2_opnd;

    /* sc->selector = selector */
    //latxs_load_imm64(&tmp, selector);
    la_out2i(LISA_ST_W, selector, &latxs_env_ir2_opnd,
        lsenv_offset_of_seg_selector(lsenv, seg_reg));

    /* sc->base = base */
    //latxs_load_imm64(&tmp, base);
    la_out2i(LISA_ST_D, base, &latxs_env_ir2_opnd,
        lsenv_offset_of_seg_base(lsenv, seg_reg));

    /* sc->limit = limit */
    //latxs_load_imm64(&tmp, limit);
    la_out2i(LISA_ST_W, limit, &latxs_env_ir2_opnd,
        lsenv_offset_of_seg_limit(lsenv, seg_reg));

    /* sc->flags = flags */
    //latxs_load_imm64(&tmp, flags);
    la_out2i(LISA_ST_W, flags, &latxs_env_ir2_opnd,
        lsenv_offset_of_seg_flags(lsenv, seg_reg));

    if (seg_reg == R_CS) {
        /* env->hflags |= HF_CS32_MASK | HF_SS32_MASK | HF_CS64_MASK; */
        latxs_load_imm64(&tmp, HF_CS32_MASK | HF_SS32_MASK | HF_CS64_MASK);
        la_out2i(LISA_LD_WU, &tmp1, &latxs_env_ir2_opnd,
            lsenv_offset_of_hflags(lsenv));
        la_out3(LISA_OR, &tmp1, &tmp1, &tmp);

        /* env->hflags &= ~(HF_ADDSEG_MASK); */
        latxs_load_imm64(&tmp, HF_ADDSEG_MASK);
        la_out3(LISA_ANDN, &tmp1, &tmp1, &tmp);
        la_out2i(LISA_ST_W, &tmp1, &latxs_env_ir2_opnd,
            lsenv_offset_of_hflags(lsenv));
    }

    if (seg_reg == R_SS) {
        /* env->hflags = (env->hflags & ~HF_CPL_MASK) | cpl; */
        la_out2i(LISA_LD_WU, &tmp, &latxs_env_ir2_opnd,
            lsenv_offset_of_hflags(lsenv));
        latxs_load_imm64(&tmp1, HF_CPL_MASK);
        la_out3(LISA_ANDN, &tmp, &tmp, &tmp1);

        /* cpl = (flags >> DESC_DPL_SHIFT) & 3; */
        la_out2i(LISA_SRLI_D, &tmp1, flags, DESC_DPL_SHIFT);
        la_out2i(LISA_ANDI, &tmp1, &tmp1, 3);
        la_out3(LISA_OR, &tmp, &tmp1, &tmp);

        la_out2i(LISA_ST_W, &tmp, &latxs_env_ir2_opnd,
            lsenv_offset_of_hflags(lsenv));

        latx_cpu_sync_bndcs_hflags(&tmp, &tmp1);
    }

    /*
     *  new_hflags = (env->segs[R_SS].flags & DESC_B_MASK)
     *      >> (DESC_B_SHIFT - HF_SS32_SHIFT);
     */
    la_out2i(LISA_LD_WU, &tmp, &latxs_env_ir2_opnd,
        lsenv_offset_of_seg_flags(lsenv, R_SS));

    latxs_load_imm64(&tmp1, DESC_B_MASK);
    la_out3(LISA_AND, &tmp, &tmp, &tmp1);
    la_out2i(LISA_SRLI_D, &tmp, &tmp, DESC_B_SHIFT - HF_SS32_SHIFT);

    /* env->hflags = (env->hflags &
     *                 ~(HF_SS32_MASK | HF_ADDSEG_MASK)) | new_hflags;
     */
    la_out2i(LISA_LD_WU, &tmp1, &latxs_env_ir2_opnd,
        lsenv_offset_of_hflags(lsenv));

    latxs_load_imm64(&tmp2, HF_SS32_MASK | HF_ADDSEG_MASK);
    la_out3(LISA_ANDN, &tmp1, &tmp1, &tmp2);
    la_out3(LISA_OR, &tmp, &tmp, &tmp1);

    la_out2i(LISA_ST_W, &tmp, &latxs_env_ir2_opnd,
        lsenv_offset_of_hflags(lsenv));
}

static inline void fast_validate_seg(X86Seg seg_reg, IR2_OPND *cpl)
{
    IR2_OPND dpl = latxs_arg0_ir2_opnd;
    IR2_OPND tmp0 = latxs_arg1_ir2_opnd;
    IR2_OPND tmp1 = latxs_arg2_ir2_opnd;

    IR2_OPND end = latxs_ir2_opnd_new_label();
    IR2_OPND label_normal = latxs_ir2_opnd_new_label();

    if (seg_reg != R_FS && seg_reg != R_GS) {
        la_out1(LISA_B, &label_normal);
    }

    /* if ((env->segs[seg_reg].selector & 0xfffc) == 0), return */
    la_out2i(LISA_LD_WU, &tmp0, &latxs_env_ir2_opnd,
        lsenv_offset_of_seg_selector(lsenv, seg_reg));
    latxs_load_imm64(&tmp1, 0xfffc);
    la_out3(LISA_AND, &tmp0, &tmp0, &tmp1);
    la_out2(LISA_BEQZ, &tmp0, &end);

    la_out1(LISA_LABEL, &label_normal);

    IR2_OPND base = latxs_ra_alloc_itemp();
    IR2_OPND limit = latxs_ra_alloc_itemp();
    IR2_OPND flags = latxs_ra_alloc_itemp();

    la_out2i(LISA_LD_WU, &flags, &latxs_env_ir2_opnd,
        lsenv_offset_of_seg_flags(lsenv, seg_reg));

    la_out2i(LISA_SRLI_D, &dpl, &flags, DESC_DPL_SHIFT);
    la_out2i(LISA_ANDI, &dpl, &dpl, 3);

    /* if ((e2 & DESC_CS_MASK) && (e2 & DESC_C_MASK)), goto end */
    la_out2i(LISA_ANDI, &tmp0, &flags, DESC_C_MASK | DESC_CS_MASK);
    latxs_load_imm64(&tmp1, DESC_C_MASK | DESC_CS_MASK);
    la_out3(LISA_BEQ, &tmp0, &tmp1, &end);

    /* if (dpl >= cpl), goto end */
    la_out3(LISA_BGE, &dpl, cpl, &end);

    /*
     * cpu_x86_load_seg_cache_latx(env, seg_reg, 0,
     *                        env->segs[seg_reg].base,
     *                        env->segs[seg_reg].limit,
     *                        env->segs[seg_reg].flags & ~DESC_P_MASK);
     */
    la_out2i(LISA_LD_D, &base, &latxs_env_ir2_opnd,
        lsenv_offset_of_seg_base(lsenv, seg_reg));
    la_out2i(LISA_LD_WU, &limit, &latxs_env_ir2_opnd,
        lsenv_offset_of_seg_limit(lsenv, seg_reg));
    la_out2i(LISA_LD_WU, &flags, &latxs_env_ir2_opnd,
        lsenv_offset_of_seg_flags(lsenv, seg_reg));

    latxs_load_imm64(&tmp0, DESC_P_MASK);
    la_out3(LISA_ANDN, &flags, &flags, &tmp0);

    fast_cpu_x86_load_seg_cache(seg_reg,
                                &la_zero, &base, &limit, &flags);

    la_out1(LISA_LABEL, &end);

    latxs_ra_free_temp(&flags);
    latxs_ra_free_temp(&base);
    latxs_ra_free_temp(&limit);
}

static void latxs_iret_save_native_xmms(void)
{
#if defined(CONFIG_LATX_HACK_MEM) && defined(CONFIG_SOFTMMU)
    if (hamt_enabled() && hamt_mode_dmw()) {
        IR2_OPND active = latxs_ra_alloc_itemp();
        IR2_OPND done = latxs_ir2_opnd_new_label();
        size_t offset = offsetof(CPUX86State, aot_native_fault_active);

        g_assert(!(offset & 3) && offset <= 32764);
        la_out2i(LISA_LDPTR_D, &active, &latxs_env_ir2_opnd, offset >> 2);
        la_out2(LISA_BEQZ, &active, &done);
        /* The static NOFP prologue omits SIMD stores. Resume validation
         * needs the live guest vectors before entering any C helper. */
        latxs_tr_save_xmms_to_env(0xffffffff);
        latxs_append_ir2_opnd1(LISA_LABEL, &done);
        latxs_ra_free_temp(&active);
    }
#endif
}

static bool latxs_do_fast_iret(IR1_INST *pir1, int size)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    CHECK_EXCP_IRET(pir1);

    ADDRX next_eip = ir1_addr_next(pir1);
    ADDRX next_eip_offset = 0;
    /* pcrel, we should avoid directly storing next_eip */
    if (option_tb_pcrel)
        next_eip_offset = td->pc_next - td->pc_save;
    else if (option_page_pcrel)
        next_eip_offset = td->pc_next - td->curr_page;

    IR2_OPND label_helper = latxs_ir2_opnd_new_label();
    IR2_OPND label_end = latxs_ir2_opnd_new_label();

    IR2_OPND new_cs = latxs_ra_alloc_itemp();
    //IR2_OPND new_ss = latxs_ra_alloc_itemp();

    /* arg0 - arg3 should not be across mem access */
    IR2_OPND tmp0 = latxs_arg0_ir2_opnd;
    IR2_OPND tmp1 = latxs_arg1_ir2_opnd;
    IR2_OPND tmp2 = latxs_arg2_ir2_opnd;
    IR2_OPND tmp3 = latxs_arg3_ir2_opnd;

#if defined(CONFIG_LATX_HACK_MEM) && defined(CONFIG_SOFTMMU)
    if (hamt_enabled() && hamt_mode_dmw()) {
        /* Only an active demand-page context needs the full IRET helper. */
        size_t offset = offsetof(CPUX86State, aot_native_fault_active);
        g_assert(!(offset & 3) && offset <= 32764);
        la_out2i(LISA_LDPTR_D, &tmp0, &latxs_env_ir2_opnd, offset >> 2);
        la_out2(LISA_BNEZ, &tmp0, &label_helper);
    }
#endif

    /* sp = env->regs[R_ESP] */
    IR2_OPND ir2_esp = latxs_ra_alloc_gpr(rsp_index);

    if (!td->sys.pe || td->sys.vm86) {
        latxs_append_ir2_opnd1(LISA_B, &label_helper);
    }

    /* !(env->hflags & HF_LMA_MASK) */
    latxs_append_ir2_opnd2i(LISA_LD_WU, &tmp0, &latxs_env_ir2_opnd,
       lsenv_offset_of_hflags(lsenv));
    latxs_load_imm64(&new_cs, HF_LMA_MASK);
    la_out3(LISA_AND, &new_cs, &tmp0, &new_cs);

    la_out2(LISA_BEQZ, &new_cs, &label_helper);

    /* !(env->hflags & HF_CS64_MASK) */
    latxs_append_ir2_opnd2i(LISA_LD_WU, &tmp0, &latxs_env_ir2_opnd,
       lsenv_offset_of_hflags(lsenv));
    latxs_load_imm64(&new_cs, HF_CS64_MASK);
    la_out3(LISA_AND, &new_cs, &tmp0, &new_cs);

    la_out2(LISA_BEQZ, &new_cs, &label_helper);

    /* ssp = env->segs[R_SS].base; */
    //la_out2i(LISA_LD_D, &tmp0, &latxs_env_ir2_opnd,
    //    lsenv_offset_of_seg_base(lsenv, R_SS));

    /* POPQ_RA(sp, new_eip, retaddr) */
    //gen_ldst_softmmu_helper(LISA_LD_D, &tmp0, &ir2_esp, 0, 1);
    //la_out2i(LISA_ADDI_D, &ir2_esp, &ir2_esp, 8);

    /* POPQ_RA(sp, new_cs, retaddr) */
    gen_ldst_softmmu_helper(LISA_LD_WU, &new_cs, &ir2_esp, 8, 1);
    //la_out2i(LISA_ADDI_D, &ir2_esp, &ir2_esp, 8);

    la_out2i(LISA_ST_W, &new_cs, &latxs_env_ir2_opnd,
            lsenv_offset_of_new_cs(lsenv));

    /* new_cs &= 0xffff */
    latxs_load_imm64_to_ir2(&tmp2, 0xffff);
    la_out3(LISA_AND, &new_cs, &new_cs, &tmp2);

    /* POPQ_RA(sp, new_eflags, retaddr); */
    //gen_ldst_softmmu_helper(LISA_LD_D, &tmp2, &ir2_esp, 16, 1);
    //la_out2i(LISA_ADDI_D, &ir2_esp, &ir2_esp, 8);

    /* if (new_cs & 0xfffc) == 0, goto helper */
    latxs_load_imm64_to_ir2(&tmp3, 0xfffc);
    la_out3(LISA_AND, &tmp3, &new_cs, &tmp3);
    la_out2(LISA_BEQZ, &tmp3, &label_helper);

    IR2_OPND cpl = latxs_ra_alloc_itemp();
    /* cpl = env->hflags & HF_CPL_MASK */
    la_out2i(LISA_LD_WU, &tmp0, &latxs_env_ir2_opnd,
            lsenv_offset_of_hflags(lsenv));
    la_out2i(LISA_ANDI, &cpl, &tmp0, HF_CPL_MASK);

    /* store cpl to env for future use */
    la_out2i(LISA_ST_W, &cpl, &latxs_env_ir2_opnd,
            lsenv_offset_of_old_cpl(lsenv));

    /* rpl = new_cs & 3 */
    la_out2i(LISA_ANDI, &tmp2, &new_cs, 3);

    /* if (rpl < cpl), goto helper */
    la_out3(LISA_BLTU, &tmp2, &cpl, &label_helper);
    latxs_ra_free_temp(&cpl);

    IR2_OPND e1 = latxs_ra_alloc_itemp();
    IR2_OPND e2 = latxs_ra_alloc_itemp();

    /* load_segment_ra(e1_ptr, e2_ptr, selector, lebel_helper) */
    /* The fast_load_segment_ra function contains x86 registers,
     * which will use the a0-a3 registers, so we can't use the
     * a0-a3 registers.
     */

    fast_load_segment_ra(&e1, &e2, &new_cs,
                           &label_helper);
    /* save e1 & e2 for future use */
    la_out2i(LISA_ST_W, &e1, &latxs_env_ir2_opnd,
            lsenv_offset_of_cs_e1(lsenv));
    la_out2i(LISA_ST_W, &e2, &latxs_env_ir2_opnd,
            lsenv_offset_of_cs_e2(lsenv));

    /* if (!(e2 & DESC_S_MASK) || !(e2 & DESC_CS_MASK)), goto helper */
    latxs_load_imm64_to_ir2(&tmp3, DESC_S_MASK);
    la_out3(LISA_AND, &tmp3, &e2, &tmp3);
    la_out2(LISA_BEQZ, &tmp3, &label_helper);

    la_out2i(LISA_ANDI, &tmp3, &e2, DESC_CS_MASK);
    la_out2(LISA_BEQZ, &tmp3, &label_helper);

    /* if (!(e2 & DESC_P_MASK)), goto helper  */
    latxs_load_imm64_to_ir2(&tmp3, DESC_P_MASK);
    la_out3(LISA_AND, &tmp3, &e2, &tmp3);
    la_out2(LISA_BEQZ, &tmp3, &label_helper);

    IR2_OPND dpl = latxs_ra_alloc_itemp();
    IR2_OPND not_eq = latxs_ir2_opnd_new_label();
    IR2_OPND end_dpl_rpl = latxs_ir2_opnd_new_label();

    /* dpl = (e2 >> DESC_DPL_SHIFT) & 3; */
    la_out2i(LISA_SRLI_D, &dpl, &e2, DESC_DPL_SHIFT);
    la_out2i(LISA_ANDI, &dpl, &dpl, 3);

    /* if (e2 & DESC_C_MASK) */
    la_out2i(LISA_AND, &tmp3, &e2, DESC_C_MASK);
    la_out2(LISA_BNEZ, &tmp3, &not_eq);

    /* rpl = new_cs & 3 */
    la_out2i(LISA_ANDI, &tmp3, &new_cs, 3);
    /* if (dpl > rpl) */
    la_out3(LISA_BLT, &tmp3, &dpl, &label_helper);
    la_out1(LISA_B, &end_dpl_rpl);

    /* if (dpl != rpl) */
    la_out1(LISA_LABEL, &not_eq);
    /* rpl = new_cs & 3 */
    la_out2i(LISA_ANDI, &tmp3, &new_cs, 3);
    la_out3(LISA_BNE, &dpl, &tmp3, &label_helper);

    la_out1(LISA_LABEL, &end_dpl_rpl);

    latxs_ra_free_temp(&dpl);

    /* if (!(e2 & DESC_P_MASK)), goto helper */
    latxs_load_imm64_to_ir2(&tmp3, DESC_P_MASK);
    la_out3(LISA_AND, &tmp3, &e2, &tmp3);
    la_out2(LISA_BEQZ, &tmp3, &label_helper);

    latxs_ra_free_temp(&e1);
    latxs_ra_free_temp(&e2);

    IR2_OPND new_ss = latxs_ra_alloc_itemp();
    /* POPQ_RA(sp, new_esp, retaddr); */
    //gen_ldst_softmmu_helper(LISA_LD_D, &new_esp, &ir2_esp, 24, 1);

    /* POPQ_RA(sp, new_ss, retaddr); */
    gen_ldst_softmmu_helper(LISA_LD_WU, &new_ss, &ir2_esp, 32, 1);

    latxs_load_imm64_to_ir2(&tmp0, 0xffff);
    la_out3(LISA_AND, &new_ss, &new_ss, &tmp0);

    IR2_OPND diff_plv = latxs_ir2_opnd_new_label();
    IR2_OPND newss_end = latxs_ir2_opnd_new_label();

    /* if ((new_ss & 0xfffc) == 0) */
    latxs_load_imm64_to_ir2(&tmp0, 0xfffc);
    la_out3(LISA_AND, &tmp1, &new_ss, &tmp0);
    la_out2(LISA_BNEZ, &tmp1, &diff_plv);

    /* if ((env->hflags & HF_LMA_MASK) && rpl != 3) */
    latxs_load_imm64_to_ir2(&tmp0, 3);
    /* rpl = new_cs & 3 */
    la_out2i(LISA_ANDI, &tmp1, &new_cs, 3);
    la_out3(LISA_BEQ, &tmp0, &tmp1, &label_helper);

    IR2_OPND ss1_limit = latxs_ra_alloc_itemp();
    IR2_OPND ss1_flags = latxs_ra_alloc_itemp();
    /*
     * cpu_x86_load_seg_cache(env, R_SS, new_ss,
     *                        0, 0xffffffff,
     *                        DESC_G_MASK | DESC_B_MASK | DESC_P_MASK |
     *                        DESC_S_MASK | (rpl << DESC_DPL_SHIFT) |
     *                        DESC_W_MASK | DESC_A_MASK);
     */

    latxs_load_imm64_to_ir2(&ss1_limit, 0xffffffff);
    latxs_load_imm64_to_ir2(&ss1_flags, DESC_G_MASK | DESC_B_MASK | DESC_P_MASK |
                           DESC_S_MASK | DESC_W_MASK | DESC_A_MASK);
    la_out2i(LISA_SLLI_D, &tmp1, &tmp1, DESC_DPL_SHIFT);
    la_out3(LISA_OR, &ss1_flags, &ss1_flags, &tmp1);
    fast_cpu_x86_load_seg_cache(R_SS, &new_ss, &la_zero, &ss1_limit, &ss1_flags);

    latxs_ra_free_temp(&ss1_limit);
    latxs_ra_free_temp(&ss1_flags);

    /* ss_e2 = DESC_B_MASK, ss_e2 not used for (new_ss & 0xfffc) == 0 */
    la_out1(LISA_B, &newss_end);

    /* if ((new_ss & 3) != rpl), goto helper */
    la_out1(LISA_LABEL, &diff_plv);

    la_out2i(LISA_ANDI, &tmp0, &new_ss, 3);
    la_out2i(LISA_ANDI, &tmp1, &new_cs, 3);
    la_out3(LISA_BNE, &tmp0, &tmp1, &label_helper);

    IR2_OPND ss_e1 = latxs_ra_alloc_itemp();
    IR2_OPND ss_e2 = latxs_ra_alloc_itemp();
    /*IR2_OPND ss_normal = latxs_ir2_opnd_new_label();*/

    fast_load_segment_ra(&ss_e1, &ss_e2, &new_ss, &label_helper);

    /*
     * if (!(ss_e2 & DESC_S_MASK) ||
     *     (ss_e2 & DESC_CS_MASK) ||
     *     !(ss_e2 & DESC_W_MASK))
     * {
     *     goto helper
     * }
     */

    latxs_load_imm64_to_ir2(&tmp3, DESC_S_MASK);
    la_out3(LISA_AND, &tmp0, &ss_e2, &tmp3);
    la_out2(LISA_BEQZ, &tmp0, &label_helper);

    la_out2i(LISA_ANDI, &tmp0, &ss_e2, DESC_CS_MASK);
    la_out2(LISA_BNEZ, &tmp0, &label_helper);

    la_out2i(LISA_ANDI, &tmp0, &ss_e2, DESC_W_MASK);
    la_out2(LISA_BEQZ, &tmp0, &label_helper);

    /* dpl = (ss_e2 >> DESC_DPL_SHIFT) & 3 */
    la_out2i(LISA_SRLI_D, &tmp0, &ss_e2, DESC_DPL_SHIFT);
    la_out2i(LISA_ANDI, &tmp0, &tmp0, 3);

    /* if (dpl != rpl) */
    la_out2i(LISA_ANDI, &tmp1, &new_cs, 3);
    la_out3(LISA_BNE, &tmp0, &tmp1, &label_helper);

    /* if (!(ss_e2 & DESC_P_MASK)) */
    latxs_load_imm64(&tmp0, DESC_P_MASK);
    la_out3(LISA_AND, &tmp0, &ss_e2, &tmp0);
    la_out2(LISA_BEQZ, &tmp0, &label_helper);

    /*
     * cpu_x86_load_seg_cache(env, R_SS, new_ss,
     *                    get_seg_base(ss_e1, ss_e2),
     *                    get_seg_limit(ss_e1, ss_e2),
     *                    ss_e2);
     */

    IR2_OPND ss_base = latxs_ra_alloc_itemp();
    IR2_OPND ss2_limit = latxs_ra_alloc_itemp();

    fast_get_seg_base(&ss_e1, &ss_e2, &ss_base);
    fast_get_seg_limit(&ss_e1, &ss_e2, &ss2_limit);

    fast_cpu_x86_load_seg_cache(R_SS, &new_ss, &ss_base, &ss2_limit, &ss_e2);

    latxs_ra_free_temp(&ss_e1);
    latxs_ra_free_temp(&ss_e2);
    latxs_ra_free_temp(&ss_base);
    latxs_ra_free_temp(&ss2_limit);

    la_out1(LISA_LABEL, &newss_end);
    /*
     * cpu_x86_load_seg_cache(env, R_CS, new_cs,
     *                get_seg_base(e1, e2),
     *                get_seg_limit(e1, e2),
     *                e2);
     */

    IR2_OPND cs_e1 = latxs_ra_alloc_itemp();
    IR2_OPND cs_e2 = latxs_ra_alloc_itemp();
    IR2_OPND cs_base = latxs_ra_alloc_itemp();
    IR2_OPND cs_limit = latxs_ra_alloc_itemp();

    latxs_append_ir2_opnd2i(LISA_LD_WU, &cs_e1, &latxs_env_ir2_opnd,
       lsenv_offset_of_cs_e1(lsenv));
    latxs_append_ir2_opnd2i(LISA_LD_WU, &cs_e2, &latxs_env_ir2_opnd,
       lsenv_offset_of_cs_e2(lsenv));

    fast_get_seg_base(&cs_e1, &cs_e2, &cs_base);
    fast_get_seg_base(&cs_e1, &cs_e2, &cs_limit);

    fast_cpu_x86_load_seg_cache(R_CS, &new_cs, &cs_base, &cs_limit, &cs_e2);

    latxs_ra_free_temp(&cs_e1);
    latxs_ra_free_temp(&cs_e2);
    latxs_ra_free_temp(&ss_base);
    latxs_ra_free_temp(&cs_limit);

    latxs_ra_free_temp(&new_ss);

    /* rpl = new_cs & 3 */
    la_out2i(LISA_ANDI, &new_cs, &new_cs, 3);

    fast_validate_seg(R_ES, &new_cs);
    fast_validate_seg(R_DS, &new_cs);
    fast_validate_seg(R_FS, &new_cs);
    fast_validate_seg(R_GS, &new_cs);

    latxs_ra_free_temp(&new_cs);

    /* env->eip = new_eip */
    IR2_OPND new_eip = latxs_ra_alloc_itemp();
    gen_ldst_softmmu_helper(LISA_LD_D, &new_eip, &ir2_esp, 0, 1);
    latxs_append_ir2_opnd2i(LISA_ST_D, &new_eip, &latxs_env_ir2_opnd,
        lsenv_offset_of_eip(lsenv));
    latxs_ra_free_temp(&new_eip);

    /* POPQ_RA(sp, new_eflags, retaddr); */
    IR2_OPND new_eflags = latxs_ra_alloc_itemp();
    gen_ldst_softmmu_helper(LISA_LD_WU, &new_eflags, &ir2_esp, 16, 1);

    /* SET_ESP(sp, sp_mask) */
    IR2_OPND new_esp = latxs_ra_alloc_itemp();
    gen_ldst_softmmu_helper(LISA_LD_D, &new_esp, &ir2_esp, 24, 1);
    la_out3(LISA_OR, &ir2_esp, &new_esp, &la_zero);
    latxs_ra_free_temp(&new_esp);

    IR2_OPND eflags_mask = latxs_ra_alloc_itemp();
    IR2_OPND old_cpl = latxs_ra_alloc_itemp();

    latxs_load_imm64(&eflags_mask,
        TF_MASK | AC_MASK | ID_MASK | RF_MASK | NT_MASK);
    la_out2i(LISA_LD_W, &old_cpl, &latxs_env_ir2_opnd,
            lsenv_offset_of_old_cpl(lsenv));

    latxs_load_imm64(&tmp0, IOPL_MASK);
    la_out3(LISA_MASKNEZ, &tmp1, &tmp0, &old_cpl);
    la_out3(LISA_OR, &eflags_mask, &eflags_mask, &tmp1);

    /* iopl = (env->eflags >> IOPL_SHIFT) & 3; */
    la_out2i(LISA_LD_WU, &tmp0, &latxs_env_ir2_opnd,
        lsenv_offset_of_eflags(lsenv));
    la_out2i(LISA_SRLI_D, &tmp0, &tmp0, IOPL_SHIFT);
    la_out2i(LISA_ANDI, &tmp0, &tmp0, 3);

    /*
     * if (cpl <= iopl) {
     *     eflags_mask |= IF_MASK;
     * }
     */

    IR2_OPND label_cpl_lt_iopl = latxs_ir2_opnd_new_label();
    la_out3(LISA_BLT, &tmp0, &old_cpl, &label_cpl_lt_iopl);
    la_out2i(LISA_ORI, &eflags_mask, &eflags_mask, IF_MASK);
    latxs_append_ir2_opnd1(LISA_LABEL, &label_cpl_lt_iopl);

    /* cpu_load_eflags(env, new_eflags, eflags_mask); */
    latx_cpu_load_eflags(&new_eflags, &eflags_mask);

    /* env->hflags2 &= ~HF2_NMI_MASK; */
    latxs_load_imm64(&old_cpl, ~HF2_NMI_MASK);
    latxs_load_imm64(&new_eflags, lsenv_offset_of_hflags2(lsenv));
    la_out3(LISA_ADD_D, &new_eflags, &new_eflags, &latxs_env_ir2_opnd);
    la_out3(LISA_AMAND_W, &old_cpl, &new_eflags, &old_cpl);

    la_out1(LISA_B, &label_end);

    latxs_ra_free_temp(&old_cpl);
    latxs_ra_free_temp(&new_eflags);

    latxs_append_ir2_opnd1(LISA_LABEL, &label_helper);

    /* 1. save complete context */
    latxs_iret_save_native_xmms();
    latxs_tr_gen_call_to_helper_prologue_cfg(nofp_helper_cfg);

    if (td->sys.pe && !td->sys.vm86) {
        /*
         * 2. protected mode iret
         *
         * target/i386/seg_helper.c
         * void helper_iret_protected(
         *      CPUX86State *env,
         *      int shift,
         *      int next_eip)
         */

        latxs_tr_gen_add_curr_eip_to_ir2(&latxs_arg2_ir2_opnd, next_eip_offset, next_eip, false);

        /* 2.1 arg2: next eip */
        /* 2.2 arg1: size */
        latxs_append_ir2_opnd2i(LISA_ORI, &latxs_arg1_ir2_opnd,
                &latxs_zero_ir2_opnd, size);
        /* 2.3 arg0: env */
        latxs_append_ir2_opnd3(LISA_OR, &latxs_arg0_ir2_opnd,
                &latxs_env_ir2_opnd, &latxs_zero_ir2_opnd);
        if (hamt_enabled()) {
            latxs_tr_dmw_out_fix_mem(&latxs_arg0_ir2_opnd);
        }
        /* 2.4 call helper_iret_protected : might generate exception  */
        latxs_tr_gen_call_to_helper((ADDR)helper_iret_protected, HELPER_IRET_PROTECTED);
    } else {
        /*
         * 2. real and vm86 mode iret
         *
         * target/i386/seg_helper.c
         * void helper_iret_real(
         *      CPUX86State *env,
         *      int shift)
         */
        /* 2.1 arg0: env */
        latxs_append_ir2_opnd3(LISA_OR, &latxs_arg0_ir2_opnd,
                &latxs_env_ir2_opnd, &latxs_zero_ir2_opnd);
        if (hamt_enabled()) {
            latxs_tr_dmw_out_fix_mem(&latxs_arg0_ir2_opnd);
        }
        /* 2.2 arg1: size */
        latxs_append_ir2_opnd2i(LISA_ORI, &latxs_arg1_ir2_opnd,
                &latxs_zero_ir2_opnd, size);
        /* 2.3 call helper_iret_real : ESP is updated, */
        /* so we must save all gpr */
        latxs_tr_gen_call_to_helper((ADDR)helper_iret_real, HELPER_IRET_REAL);
    }

    /* 3. restore native context */
    latxs_tr_gen_call_to_helper_epilogue_cfg(nofp_helper_cfg);

    /* 5. disable eip update, since the helper modify eip */
    td->ignore_eip_update = 1;

    la_out1(LISA_LABEL, &label_end);

    return true;
}

static bool latxs_do_translate_iret(IR1_INST *pir1, int size)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    CHECK_EXCP_IRET(pir1);

    if (option_opt_iret && (size == 2)) {
        latxs_do_fast_iret(pir1, size);
        return true;
    }

    ADDRX next_eip = ir1_addr_next(pir1);
    ADDRX next_eip_offset = 0;
    /* pcrel, we should avoid directly storing next_eip */
    if (option_tb_pcrel)
        next_eip_offset = td->pc_next - td->pc_save;
    else if (option_page_pcrel)
        next_eip_offset = td->pc_next - td->curr_page;

    if (lsenv->tr_data->sys.code64) {
        lsassert(size == 0 || size == 1 || size == 2);
    } else {
        lsassert(size == 0 || size == 1);
    }

    /* 1. save complete context */
    latxs_iret_save_native_xmms();
    latxs_tr_gen_call_to_helper_prologue_cfg(nofp_helper_cfg);

    if (td->sys.pe && !td->sys.vm86) {
        /*
         * 2. protected mode iret
         *
         * target/i386/seg_helper.c
         * void helper_iret_protected(
         *      CPUX86State *env,
         *      int shift,
         *      int next_eip)
         */

        latxs_tr_gen_add_curr_eip_to_ir2(&latxs_arg2_ir2_opnd, next_eip_offset, next_eip, false);

        /* 2.1 arg2: next eip */
        /* 2.2 arg1: size */
        latxs_append_ir2_opnd2i(LISA_ORI, &latxs_arg1_ir2_opnd,
                &latxs_zero_ir2_opnd, size);
        /* 2.3 arg0: env */
        latxs_append_ir2_opnd3(LISA_OR, &latxs_arg0_ir2_opnd,
                &latxs_env_ir2_opnd, &latxs_zero_ir2_opnd);
        if (hamt_enabled()) {
            latxs_tr_dmw_out_fix_mem(&latxs_arg0_ir2_opnd);
        }
        /* 2.4 call helper_iret_protected : might generate exception  */
        latxs_tr_gen_call_to_helper((ADDR)helper_iret_protected, HELPER_IRET_PROTECTED);
    } else {
        /*
         * 2. real and vm86 mode iret
         *
         * target/i386/seg_helper.c
         * void helper_iret_real(
         *      CPUX86State *env,
         *      int shift)
         */
        /* 2.1 arg0: env */
        latxs_append_ir2_opnd3(LISA_OR, &latxs_arg0_ir2_opnd,
                &latxs_env_ir2_opnd, &latxs_zero_ir2_opnd);
        if (hamt_enabled()) {
            latxs_tr_dmw_out_fix_mem(&latxs_arg0_ir2_opnd);
        }
        /* 2.2 arg1: size */
        latxs_append_ir2_opnd2i(LISA_ORI, &latxs_arg1_ir2_opnd,
                &latxs_zero_ir2_opnd, size);
        /* 2.3 call helper_iret_real : ESP is updated, */
        /* so we must save all gpr */
        latxs_tr_gen_call_to_helper((ADDR)helper_iret_real, HELPER_IRET_REAL);
    }

    /* 3. restore native context */
    latxs_tr_gen_call_to_helper_epilogue_cfg(nofp_helper_cfg);

    /* 5. disable eip update, since the helper modify eip */
    td->ignore_eip_update = 1;

    return true;
}

/* End of TB in system-mode */
bool latxs_translate_iret(IR1_INST *pir1)
{
    IR1_OPCODE opc = ir1_opcode(pir1);

    if (opc == X86_INS_IRET) {
        /* 16-bit opnd size */
        return latxs_do_translate_iret(pir1, 0);
    } else if (opc == X86_INS_IRETD) {
        /* 32-bit opnd size */
        return latxs_do_translate_iret(pir1, 1);
    } else if (opc == X86_INS_IRETQ) {
        if (lsenv->tr_data->sys.code64) {
            return latxs_do_translate_iret(pir1, 2);
        } else {
            lsassert(0);
        }
    } else {
        lsassert(0);
    }

    return false;
}

bool latxs_translate_int(IR1_INST *pir1)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    CHECK_EXCP_INT(pir1);

    latx_restore_sp();

    /* 0. save current instruciton's EIP to env */
    latxs_tr_gen_save_curr_eip();

    /* 1. save complete context */
    helper_cfg_t cfg = default_helper_cfg;
    latxs_tr_gen_call_to_helper_prologue_cfg(cfg);

    /*
     * 2. raise interrupt
     *
     *  target/i386/excp_helper.c
     *  void helper_raise_interrupt(
     *      CPUX86State *env,
     *      int intno,
     *      int next_eip_addend)
     */

    /* 2.1 arg1: intno */
    IR1_OPND *opnd = ir1_get_opnd(pir1, 0);
    int intno = ir1_opnd_simm(opnd);
    latxs_load_imm32_to_ir2(&latxs_arg1_ir2_opnd, intno, EXMode_Z);
    /* 2.2 arg2 : next eip addend : instruction size */
    int size = latxs_ir1_inst_size(pir1);
    latxs_append_ir2_opnd2i(LISA_ORI, &latxs_arg2_ir2_opnd,
            &latxs_zero_ir2_opnd, size & 0xfff);
    /* 2.3 arg0 : env */
    latxs_append_ir2_opnd3(LISA_OR, &latxs_arg0_ir2_opnd,
            &latxs_env_ir2_opnd, &latxs_zero_ir2_opnd);
    if (hamt_enabled()) {
        latxs_tr_dmw_out_fix_mem(&latxs_arg0_ir2_opnd);
    }
    /* 2.4 call helper_raise_interrupt: generate interrupt */
    latxs_tr_gen_call_to_helper((ADDR)helper_raise_interrupt, HELPER_RAISE_INTERRUPT);

    /* 3. This helper never return */
    latxs_tr_gen_infinite_loop();

    return true;
}

bool latxs_translate_int_3(IR1_INST *pir1)
{
    latx_restore_sp();
    /* 0. save current instruciton's EIP to env */
    latxs_tr_gen_save_curr_eip();

    /* 1. save complete context */
    helper_cfg_t cfg = default_helper_cfg;
    latxs_tr_gen_call_to_helper_prologue_cfg(cfg);

    /*
     * 2. raise interrupt
     *
     *  target/i386/excp_helper.c
     *  void helper_raise_interrupt(
     *      CPUX86State *env,
     *      int intno,
     *      int next_eip_addend)
     */

    /* 2.1 arg1: intno = EXCP03_INT3 */
    latxs_append_ir2_opnd2i(LISA_ORI, &latxs_arg1_ir2_opnd,
            &latxs_zero_ir2_opnd, EXCP03_INT3);
    /* 2.2 arg2: next eip addend */
    int size = latxs_ir1_inst_size(pir1);
    latxs_append_ir2_opnd2i(LISA_ORI, &latxs_arg2_ir2_opnd,
            &latxs_zero_ir2_opnd, size & 0xfff);
    /* 2.3 arg0 : env */
    latxs_append_ir2_opnd3(LISA_OR, &latxs_arg0_ir2_opnd,
            &latxs_env_ir2_opnd, &latxs_zero_ir2_opnd);
    if (hamt_enabled()) {
        latxs_tr_dmw_out_fix_mem(&latxs_arg0_ir2_opnd);
    }
    /* 2.4 call helper_raise_interrupt: generate interrupt */
    latxs_tr_gen_call_to_helper((ADDR)helper_raise_interrupt, HELPER_RAISE_INTERRUPT);

    /* 3. This helper never return */
    latxs_tr_gen_infinite_loop();

    return true;
}

bool latxs_translate_into(IR1_INST *pir1)
{
    latx_restore_sp();
    /* 0. save current instruciton's EIP to env */
    latxs_tr_gen_save_curr_eip();

    /* 1. save complete context */
    helper_cfg_t cfg = default_helper_cfg;
    latxs_tr_gen_call_to_helper_prologue_cfg(cfg);

    /*
     * 2. helper_into
     *
     *  target/i386/misc_helper.c
     *  void helper_into(
     *      CPUX86State *env,
     *      int next_eip_addend)
     */

    /* 2.1 arg2: next eip addend */
    int size = latxs_ir1_inst_size(pir1);
    latxs_append_ir2_opnd2i(LISA_ORI, &latxs_arg1_ir2_opnd,
            &latxs_zero_ir2_opnd, size & 0xfff);
    /* 2.2 arg0 : env */
    latxs_append_ir2_opnd3(LISA_OR, &latxs_arg0_ir2_opnd,
            &latxs_env_ir2_opnd, &latxs_zero_ir2_opnd);
    if (hamt_enabled()) {
        latxs_tr_dmw_out_fix_mem(&latxs_arg0_ir2_opnd);
    }
    /* 2.3 call helper_raise_interrupt: generate interrupt */
    latxs_tr_gen_call_to_helper((ADDR)helper_into, HELPER_INTO);

    /* 3. restore context */
    latxs_tr_gen_call_to_helper_epilogue_cfg(cfg);

    return true;
}

bool latxs_translate_int1(IR1_INST *pir1)
{
#ifdef CONFIG_SOFTMMU
    lsassertm_illop(ir1_addr(pir1), 0,
        "unsupported int1.\n");
#endif
    return true;
}

static void latxs_do_translate_ret_far_pe(
        IR1_INST *pir1, int size, int esp_inc)
{
    /* 0. save current instruciton's EIP to env */
    latxs_tr_gen_save_curr_eip();

    /* 1. save complete context */
    latxs_tr_gen_call_to_helper_prologue_cfg(default_helper_cfg);

    /* 2. prepare arguments */

    /* arg0 : env */
    latxs_append_ir2_opnd3(LISA_OR, &latxs_arg0_ir2_opnd,
            &latxs_env_ir2_opnd, &latxs_zero_ir2_opnd);
    if (hamt_enabled()) {
        latxs_tr_dmw_out_fix_mem(&latxs_arg0_ir2_opnd);
    }
    /* arg1 : shift = opnd size (32-bit:1 ; 16-bit:0)*/
    latxs_append_ir2_opnd2i(LISA_ORI, &latxs_arg1_ir2_opnd,
            &latxs_zero_ir2_opnd, size);
    /* arg2 : addend to esp: esp inc */
    IR2_OPND imm_opnd = latxs_ra_alloc_itemp();
    latxs_load_imm64_to_ir2(&imm_opnd, esp_inc);
    latxs_append_ir2_opnd3(LISA_OR, &latxs_arg2_ir2_opnd, &latxs_zero_ir2_opnd, &imm_opnd);


    /*
     * 3. call helper_lret_protected
     *    > might genrate exception
     *    > eflags is updated in helper
     *    > EIP is updated in helper
     */
    latxs_tr_gen_call_to_helper((ADDR)helper_lret_protected, HELPER_LRET_PROTECTED);
    /*
     * > protected mode
     *
     * target/i386/seg_helper.c
     * void helper_lret_protected(
     *      CPUX86State *env,
     *      int shift,
     *      int addend)
     */

    /* 4. restore context */
    latxs_tr_gen_call_to_helper_epilogue_cfg(default_helper_cfg);

    /* 5. disable eip update in context switch */
    lsenv->tr_data->ignore_eip_update = 1;
}

static void latxs_do_translate_ret_far_real(
        IR1_INST *pir1, int opnd_size, int esp_inc)
{
    int ss_addr_size = latxs_get_sys_stack_addr_size();
    /* 1. build MEM(SS:ESP) */
    IR1_OPND mem_ir1_opnd;
    ir1_opnd_build_full_mem(&mem_ir1_opnd, opnd_size,
            X86_REG_SS, X86_REG_ESP, 0, 0, 0);

    /* 2. load new EIP from MEM(SS:ESP) */
    IR2_OPND tmp_new_eip = latxs_ra_alloc_itemp();
    latxs_load_ir1_mem_to_ir2(&tmp_new_eip,
            &mem_ir1_opnd, EXMode_Z, ss_addr_size);

    /* 3. load new CS from MEM(SS:ESP + 2/4) */
    ir1_opnd_build_full_mem(&mem_ir1_opnd, opnd_size,
            X86_REG_SS, X86_REG_ESP, (opnd_size >> 3), 0, 0);
    IR2_OPND tmp_new_cs = latxs_ra_alloc_itemp();
    latxs_load_ir1_mem_to_ir2(&tmp_new_cs,
            &mem_ir1_opnd, EXMode_Z, ss_addr_size);

    /* 4. update env->eip */
    latxs_tr_gen_store_to_curr_eip(&tmp_new_eip);

    /* 5. update env->cs.base, env->cs.selector */
    latxs_append_ir2_opnd2i(LISA_ST_W, &tmp_new_cs, &latxs_env_ir2_opnd,
            lsenv_offset_of_seg_selector(lsenv, cs_index));
    latxs_append_ir2_opnd2i(LISA_SLLI_D, &tmp_new_cs, &tmp_new_cs, 0x4);
    latxs_append_ir2_opnd2i(LISA_ST_W, &tmp_new_cs, &latxs_env_ir2_opnd,
            lsenv_offset_of_seg_base(lsenv, cs_index));

    /* 6. update esp */
    esp_inc += opnd_size >> 2; /* (opnd_size >> 3) << 1 */

    IR2_OPND offset_opnd = latxs_ra_alloc_itemp();
    IR2_OPND esp_opnd = latxs_ra_alloc_gpr(esp_index);
    if (lsenv->tr_data->sys.code64) {
        if (abs(esp_inc) > 0x7ff) {
            latxs_load_imm64_to_ir2(&offset_opnd, esp_inc);
            latxs_append_ir2_opnd3(LISA_ADD_D, &esp_opnd, &esp_opnd, &offset_opnd);
        } else {
            latxs_append_ir2_opnd2i(LISA_ADDI_D, &esp_opnd, &esp_opnd, esp_inc);
        }
    } else if (lsenv->tr_data->sys.ss32) {
        if (abs(esp_inc) > 0x7ff) {
            latxs_load_imm64_to_ir2(&offset_opnd, esp_inc);
            latxs_append_ir2_opnd3(LISA_ADD_D, &esp_opnd, &esp_opnd, &offset_opnd);
        } else {
            latxs_append_ir2_opnd2i(LISA_ADDI_D, &esp_opnd, &esp_opnd, esp_inc);
        }
        latxs_append_ir2_opnd2_(lisa_mov32z, &esp_opnd, &esp_opnd);
    } else {
        IR2_OPND tmp = latxs_ra_alloc_itemp();
        if (abs(esp_inc) > 0x7ff) {
            latxs_load_imm64_to_ir2(&offset_opnd, esp_inc);
            latxs_append_ir2_opnd3(LISA_ADD_D, &tmp, &esp_opnd, &offset_opnd);
        } else {
            latxs_append_ir2_opnd2i(LISA_ADDI_D, &tmp, &esp_opnd, esp_inc);
        }
        latxs_store_ir2_to_ir1_gpr(&tmp, &sp_ir1_opnd);
        latxs_ra_free_temp(&tmp);
    }
    latxs_ra_free_temp(&offset_opnd);

    /* 7. disable eip update in context switch */
    /*    Since we already update eip here */
    lsenv->tr_data->ignore_eip_update = 1;
}

static bool latxs_do_translate_ret_far(
        IR1_INST *pir1, int size, int opnd_size)
{
    TRANSLATION_DATA *td = lsenv->tr_data;

    int esp_inc = 0;
    if (ir1_opnd_num(pir1)) {
        IR1_OPND *opnd = ir1_get_opnd(pir1, 0);
        esp_inc = ir1_opnd_simm(opnd);
    }

    if (td->sys.pe && !td->sys.vm86) {
        latxs_do_translate_ret_far_pe(pir1, size, esp_inc);
    } else {
    /* > real mode or vm86 */
        latxs_do_translate_ret_far_real(pir1, opnd_size, esp_inc);
    }

    return true;
}

/* End of TB in system-mode */
bool latxs_translate_retf(IR1_INST *pir1)
{
    int data_size = latxs_ir1_data_size(pir1);
    if (data_size == 64) {
        return latxs_do_translate_ret_far(pir1, 2, 64);
    } else if (data_size == 16) {
        /* 16-bit opnd size */
        return latxs_do_translate_ret_far(pir1, 0, 16);
    } else {
        /* 32-bit opnd size */
        return latxs_do_translate_ret_far(pir1, 1, 32);
    }
}

bool latxs_translate_cwd(IR1_INST *pir1)
{
    IR2_OPND val = latxs_ra_alloc_itemp();
    latxs_load_ir1_to_ir2(&val, &ax_ir1_opnd, EXMode_S);
    latxs_append_ir2_opnd2i(LISA_SRAI_W, &val, &val, 15);
    latxs_store_ir2_to_ir1(&val, &dx_ir1_opnd);
    latxs_ra_free_temp(&val);
    return true;
}

bool latxs_translate_cdq(IR1_INST *pir1)
{
#ifdef CONFIG_SOFTMMU
    if (latxs_ir1_data_size(pir1) == 16) {
        return latxs_translate_cwd(pir1);
    }
#endif

    IR2_OPND eax = latxs_ra_alloc_itemp();
    latxs_load_ir1_to_ir2(&eax, &eax_ir1_opnd, EXMode_S);
    IR2_OPND edx = latxs_ra_alloc_itemp();
    latxs_append_ir2_opnd2_(lisa_mov32s, &edx, &eax);
    latxs_append_ir2_opnd2i(LISA_SRAI_W, &edx, &edx, 31);
    latxs_store_ir2_to_ir1(&edx, &edx_ir1_opnd);
    latxs_ra_free_temp(&eax);
    latxs_ra_free_temp(&edx);
    return true;
}

bool latxs_translate_cqo(IR1_INST *pir1)
{
    IR2_OPND rax = latxs_ra_alloc_gpr(eax_index);
    IR2_OPND rdx = latxs_ra_alloc_gpr(edx_index);
    latxs_append_ir2_opnd2i(LISA_SRAI_D, &rdx, &rax, 63);
    return true;
}

bool latxs_translate_cwde(IR1_INST *pir1)
{
    /*
     * In system-mode, since the capstone will always
     * generate CWDE ignoring the opnd size.
     * so we must do it by ourself.
     */
    int data_size = latxs_ir1_data_size(pir1);
    if (data_size == 16) {
        /* AX = signed extension(al) */
        IR2_OPND al_opnd = latxs_ra_alloc_itemp();
        latxs_load_ir1_gpr_to_ir2(&al_opnd, &al_ir1_opnd, EXMode_S);
        latxs_store_ir2_to_ir1(&al_opnd, &ax_ir1_opnd);
    } else if (data_size == 32) {
        /* EAX = signed extension(ax) */
        IR2_OPND eax_opnd = latxs_ra_alloc_gpr(eax_index);
        latxs_append_ir2_opnd2_(lisa_mov16s, &eax_opnd, &eax_opnd);
        latxs_append_ir2_opnd2_(lisa_mov32z, &eax_opnd, &eax_opnd);
    } else {
        lsassertm(0, "unknown data size of cwde.\n");
    }

    return true;
}

bool latxs_translate_arpl(IR1_INST *pir1)
{
    if (lsenv->tr_data->sys.code64) {
#ifdef CONFIG_SOFTMMU
        lsassertm_illop(ir1_addr(pir1), 0,
                "arpl not encodable in 64-bit mode\n");
        return true;
#else
        lsassertm(0, "arpl not encodable in 64-bit mode\n");
        return false;
#endif
    }

    if (latxs_tr_gen_excp_check(pir1)) {
        return true;
    }

    IR2_OPND label_exit = latxs_ir2_opnd_new_label();
    IR2_OPND label = latxs_ir2_opnd_new_label();
    IR2_OPND temp = latxs_ra_alloc_itemp();
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    IR2_OPND dest = latxs_ra_alloc_itemp();
    IR2_OPND src = latxs_ra_alloc_itemp();
    latxs_load_ir1_to_ir2(&dest, opnd0, EXMode_Z);
    latxs_load_ir1_to_ir2(&src, opnd1, EXMode_Z);

    latxs_append_ir2_opnd2i(LISA_ANDI, &temp, &dest, 0x3);
    latxs_append_ir2_opnd2i(LISA_ANDI, &src, &src, 0x3);
    latxs_append_ir2_opnd3(LISA_BLTU, &dest, &src, &label);
    if (ir1_is_zf_def(pir1)) {
        latxs_append_ir2_opnd1i(LISA_X86MTFLAG, &latxs_zero_ir2_opnd, 0x8);
    }
    latxs_append_ir2_opnd1(LISA_B, &label_exit);

    latxs_append_ir2_opnd1(LISA_LABEL, &label);
    if (ir1_is_zf_def(pir1)) {
        IR2_OPND tmp = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd2i(LISA_ORI, &tmp, &latxs_zero_ir2_opnd, 0xfff);
        latxs_append_ir2_opnd1i(LISA_X86MTFLAG, &tmp, 0x8);
        latxs_ra_free_temp(&tmp);
    }
    latxs_append_ir2_opnd2ii(LISA_BSTRINS_D, &dest, &la_zero, 1, 0);
    latxs_append_ir2_opnd3(LISA_OR, &dest, &dest, &src);
    latxs_store_ir2_to_ir1(&dest, opnd0);

    latxs_append_ir2_opnd1(LISA_LABEL, &label_exit);

    return true;
}

bool latxs_translate_cdqe(IR1_INST *pir1)
{
    IR2_OPND eax_opnd = latxs_ra_alloc_gpr(eax_index);
    latxs_append_ir2_opnd2_(lisa_mov32s, &eax_opnd, &eax_opnd);
    return true;
}

bool latxs_translate_cbw(IR1_INST *pir1)
{
    IR2_OPND val = latxs_ra_alloc_itemp();
    latxs_load_ir1_gpr_to_ir2(&val, &al_ir1_opnd, EXMode_S);
    latxs_store_ir2_to_ir1(&val, &ax_ir1_opnd);
    return true;
}

static void latxs_translate_call_far_imm(IR1_INST *pir1)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    /* 1. save native context */
    if (td->sys.pe && !td->sys.vm86) {
        latxs_tr_gen_call_to_helper_prologue_cfg(default_helper_cfg);
    } else {
        latxs_tr_gen_call_to_helper_prologue_cfg(nofp_helper_cfg);
    }

    /* 2. prepare the values for parameters */

    /* 2.1 prepare parameter new_cs and new_eip */
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);
    /*
     * -------------------------------------------
     *  opndtype |      opnd[0]    |     opnd[1]
     * -------------------------------------------
     *  ptr16:16 |  imm16 selector |  imm16 offset
     *  ptr16:32 |  imm16 selector |  imm32 offset
     * -------------------------------------------
     */
    int data_size = latxs_ir1_data_size(pir1);
    lsassert(data_size == 16 || data_size == 32);
    lsassert(ir1_opnd_size(opnd0) == 16);
    lsassert(ir1_opnd_size(opnd1) == data_size);

    uint32_t selector = ir1_opnd_uimm(opnd0); /* new_cs  : 16-bit    */
    uint32_t offset   = ir1_opnd_uimm(opnd1); /* new_eip : 16/32-bit */

    /* 2.2 prepare parameter next_eip */
    ADDRX next_eip = ir1_addr_next(pir1);

    /*
     * 2.3 prepare shift: data size
     *     16-bit: shift = 0
     *     32-bit: shift = 1
     */
    int shift = (data_size >> 4) - 1;

    /* 3. building the parameters: take care of temp register */
    /* 3.1 arg0 : env */
    latxs_append_ir2_opnd3(LISA_OR, &latxs_arg0_ir2_opnd,
            &latxs_env_ir2_opnd, &latxs_zero_ir2_opnd);
    if (hamt_enabled()) {
        latxs_tr_dmw_out_fix_mem(&latxs_arg0_ir2_opnd);
    }
    /* 3.2 arg1 : selector */
    /*     arg2 : offset   */
    latxs_load_imm32_to_ir2(&latxs_arg1_ir2_opnd,
            selector & 0xffff, EXMode_Z);
    if (data_size == 32) {
        latxs_load_imm32_to_ir2(&latxs_arg2_ir2_opnd, offset, EXMode_Z);
    } else {
        latxs_load_imm32_to_ir2(&latxs_arg2_ir2_opnd,
                offset & 0xffff, EXMode_Z);
    }
    /* 3.3 arg3 : shift according to data size */
    latxs_append_ir2_opnd2i(LISA_ORI, &latxs_arg3_ir2_opnd,
            &latxs_zero_ir2_opnd, shift);

    /* latxs_arg4_ir2_opnd is itemp in x86_64, please be careful */

    /* 3.4 arg4 : next eip */
    if (option_aot_save) {
        aot_load_guest_addr_to_ir2(&latxs_arg4_ir2_opnd, next_eip, LOAD_GUEST_ADDR);
    } else {
        latxs_load_imm32_to_ir2(&latxs_arg4_ir2_opnd,
                (uint32_t)next_eip, EXMode_Z);
    }


    /* 4. call the helper */
    if (td->sys.pe && !td->sys.vm86) {
        latxs_tr_gen_call_to_helper((ADDR)helper_lcall_protected, HELPER_LCALL_PROTECTED);
        latxs_tr_gen_call_to_helper_epilogue_cfg(default_helper_cfg);
    } else {
        latxs_tr_gen_call_to_helper((ADDR)helper_lcall_real, HELPER_LCALL_REAL);
        latxs_tr_gen_call_to_helper_epilogue_cfg(nofp_helper_cfg);
    }

    /*
     *  far call in protected mode    * far call in real-mode or vm86 mode
     *  > complete cpustate is used   *  > SS and ESP (mapping) is used
     *  > CS and EIP is updated       *  > CS and EIP is updated
     *  > might generate exception    *  > might generate exception
     *
     * target/i386/seg_helper.c       * target/i386/seg_helper.c
     * void helper_lcall_protected(   * void helper_lcall_real(
     *      CPUX86State     *env,     *      CPUX86State     *env,
     *      int             new_cs,   *      int             new_cs,
     *      target_ulong    new_eip,  *      target_ulong    new_eip,
     *      int             shift,    *      int             shift,
     *      target_ulong    next_eip) *      int             next_eip)
     */

    /* 5. restore native context */
    //latxs_tr_gen_call_to_helper_epilogue_cfg(default_helper_cfg);

    /* 6. disable EIP update in the later jmp */
    td->ignore_eip_update = 1;
}

static void latxs_translate_call_far_mem(IR1_INST *pir1)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    /* 1. prepare the values for parameters */

    /* 1.1 prepare parameter new_cs and new_eip */
    IR2_OPND selector_opnd = latxs_ra_alloc_itemp();
    IR2_OPND offset_opnd   = latxs_ra_alloc_itemp();
    /*
     * -------------------------------------------
     *  opndtype |      opnd[0]    |     opnd[1]
     * -------------------------------------------
     *    m16:16 |   mem  size = 4 |       --
     *    m16:32 |   mem  size = 6 |       --
     *    m16:64 |   mem  size = 10|       --
     * -------------------------------------------
     */
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR2_OPND base;
    int off;
    latxs_convert_mem_opnd(&base, &off, opnd0, -1);

    /* offset at MEM(address) : size = data_size */
    /* selector at MEM(address + 2,4,8 ) : size = 16-bits */
    int data_size = latxs_ir1_data_size(pir1);
    switch (data_size) {
    case 16:
        gen_ldst_softmmu_helper(LISA_LD_HU, &offset_opnd,   &base, off,     1);
        gen_ldst_softmmu_helper(LISA_LD_HU, &selector_opnd, &base, off + 2, 1);
        break;
    case 32:
        gen_ldst_softmmu_helper(LISA_LD_WU, &offset_opnd,   &base, off,     1);
        gen_ldst_softmmu_helper(LISA_LD_HU, &selector_opnd, &base, off + 4, 1);
        break;
    case 64:
        gen_ldst_softmmu_helper(LISA_LD_D,  &offset_opnd,   &base, off,     1);
        gen_ldst_softmmu_helper(LISA_LD_HU, &selector_opnd, &base, off + 8, 1);
        break;
    default:
        lsassertm(0, "invalid far call data size %d", data_size);
        break;
    }

    /* 1.2 prepare parameter next_eip */
    ADDRX next_eip = ir1_addr_next(pir1);

    /*
     * 1.3 prepare shift: data size
     *     16-bit: shift = 0
     *     32-bit: shift = 1
     */
    int shift = (data_size >> 4) - 1;

    /* 2. save native context */
    if (td->sys.pe && !td->sys.vm86) {
        latxs_tr_gen_call_to_helper_prologue_cfg(default_helper_cfg);
    } else {
        latxs_tr_gen_call_to_helper_prologue_cfg(nofp_helper_cfg);
    }

    /*
     *  far call in protected mode    * far call in real-mode or vm86 mode
     *  > complete cpustate is used   *  > SS and ESP (mapping) is used
     *  > CS and EIP is updated       *  > CS and EIP is updated
     *  > might generate exception    *  > might generate exception
     *
     * target/i386/seg_helper.c       * target/i386/seg_helper.c
     * void helper_lcall_protected(   * void helper_lcall_real(
     *      CPUX86State     *env,     *      CPUX86State     *env,
     *      int             new_cs,   *      int             new_cs,
     *      target_ulong    new_eip,  *      target_ulong    new_eip1,
     *      int             shift,    *      int             shift,
     *      target_ulong    next_eip) *      int             next_eip)
     */

    IR2_OPND *arg0 = &latxs_arg0_ir2_opnd;
    IR2_OPND *arg1 = &latxs_arg1_ir2_opnd;
    IR2_OPND *arg2 = &latxs_arg2_ir2_opnd;
    IR2_OPND *arg3 = &latxs_arg3_ir2_opnd;
    IR2_OPND *arg4 = &latxs_arg4_ir2_opnd;

    IR2_OPND *zero = &latxs_zero_ir2_opnd;

    /* 3.1 arg1 : selector */
    /* 3.2 arg2 : offset   */
    IR2_OPND tmp = latxs_ra_alloc_itemp();
    latxs_append_ir2_opnd3(LISA_OR, &tmp, &offset_opnd, zero);
    latxs_append_ir2_opnd3(LISA_OR, arg1, &selector_opnd, zero);
    latxs_append_ir2_opnd3(LISA_OR, arg2, &tmp, zero);
    /* 3.3 arg3 : shift according to data size */
    latxs_append_ir2_opnd2i(LISA_ORI, arg3, zero, shift);
    /* 3.4 arg4 : next eip */
    if (option_aot_save) {
        aot_load_guest_addr_to_ir2(arg4, next_eip, LOAD_GUEST_ADDR);
    } else {
        latxs_load_imm32_to_ir2(arg4, next_eip, EXMode_Z);
    }
    /* 3.5 arg0 : env */
    latxs_append_ir2_opnd3(LISA_OR, arg0, &latxs_env_ir2_opnd, zero);
    if (hamt_enabled()) {
        latxs_tr_dmw_out_fix_mem(arg0);
    }

    /* 4. call the helper */
    if (td->sys.pe && !td->sys.vm86) {
        latxs_tr_gen_call_to_helper((ADDR)helper_lcall_protected, HELPER_LCALL_PROTECTED);
        latxs_tr_gen_call_to_helper_epilogue_cfg(default_helper_cfg);
    } else {
        latxs_tr_gen_call_to_helper((ADDR)helper_lcall_real, HELPER_LCALL_REAL);
        latxs_tr_gen_call_to_helper_epilogue_cfg(nofp_helper_cfg);
    }

    /* 5. restore native context */
    //latxs_tr_gen_call_to_helper_epilogue_cfg(default_helper_cfg);

    /* 6. disable EIP update in the later jmp */
    td->ignore_eip_update = 1;
}

/* End of TB in system-mode */
bool latxs_translate_lcall(IR1_INST *pir1)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);

    if (ir1_opnd_is_imm(opnd0)) {
        latxs_translate_call_far_imm(pir1);
    } else {
#ifdef CONFIG_SOFTMMU
        lsassertm_illop(ir1_addr(pir1), ir1_opnd_is_mem(opnd0),
                "not a valid call far: pir1 = %p\n", (void *)pir1);
#endif
        latxs_translate_call_far_mem(pir1);
    }

    TRANSLATION_DATA *td = lsenv->tr_data;
    td->sys_eob_can_link = 1;
    td->sys_eob_is_indir = 1;

    return true;
}

bool latxs_translate_pushaw(IR1_INST *pir1)
{
    IR2_OPND esp_opnd = latxs_ra_alloc_gpr(esp_index);
    TRANSLATION_DATA *td = lsenv->tr_data;
    IR1_OPND mem_ir1_opnd;
    int esp_decrement = 2;

    latx_restore_sp();
    for (int i = 0; i < 8; i++) {
        ir1_opnd_build_full_mem(&mem_ir1_opnd, esp_decrement << 3,
                X86_REG_SS, X86_REG_ESP, 0-esp_decrement * (i + 1), 0, 0);
        IR2_OPND src_opnd = latxs_ra_alloc_gpr(i);
        latxs_store_ir2_to_ir1(&src_opnd, &mem_ir1_opnd);
    }
    bool pushpop = get_pushpop_option(td);
    if (pushpop) {
        td->sp_patch = -esp_decrement * 8;
#ifdef CONFIG_LATX_PROFILER
        TranslationBlock *tb = lsenv->tr_data->curr_tb;
        per_tb_count((void *)&((tb->profile).pushpop_sub), 1);
#endif
    } else {
        latxs_append_ir2_opnd2i(LISA_ADDI_D, &esp_opnd, &esp_opnd, -esp_decrement * 8);
    }
    return true;
}

bool latxs_translate_pushal(IR1_INST *pir1)
{
    IR2_OPND esp_opnd = latxs_ra_alloc_gpr(esp_index);
    int esp_decrement = 4;
    TRANSLATION_DATA *td = lsenv->tr_data;
    bool pushpop = get_pushpop_option(td);
    IR1_OPND mem_ir1_opnd;

    latx_restore_sp();
    for (int i = 0; i < 8; i++) {
        ir1_opnd_build_full_mem(&mem_ir1_opnd, esp_decrement << 3,
                X86_REG_SS, X86_REG_ESP, 0-esp_decrement * (i + 1), 0, 0);
        IR2_OPND src_opnd = latxs_ra_alloc_gpr(i);
        latxs_store_ir2_to_ir1(&src_opnd, &mem_ir1_opnd);
    }
    if (pushpop) {
        td->sp_patch += -esp_decrement * 8;
#ifdef CONFIG_LATX_PROFILER
        TranslationBlock *tb = lsenv->tr_data->curr_tb;
        per_tb_count((void *)&((tb->profile).pushpop_sub), 1);
#endif
    } else {
        latxs_append_ir2_opnd2i(LISA_ADDI_D, &esp_opnd, &esp_opnd, -esp_decrement * 8);
    }
    return true;
}

bool latxs_translate_popaw(IR1_INST *pir1)
{
    IR2_OPND esp_opnd = latxs_ra_alloc_gpr(esp_index);
    TRANSLATION_DATA *td = lsenv->tr_data;
    int esp_increment = 2;
    int esp_patch = 0;
    IR1_OPND mem_ir1_opnd;
    IR2_OPND dst_opnd;
    IR2_OPND tmp = latxs_ra_alloc_itemp();

    bool pushpop = get_pushpop_option(td);
    if (pushpop) {
        esp_patch = td->sp_patch;
    }
    for (int i = 7; i >= 0; i--) {
        if (i == esp_index) {
            // skip esp
            continue;
        }
        dst_opnd = latxs_ra_alloc_gpr(i);
        ir1_opnd_build_full_mem(&mem_ir1_opnd, esp_increment << 3,
                X86_REG_SS, X86_REG_ESP,
                esp_patch + esp_increment * (7 - i), 0, 0);
        latxs_load_ir1_to_ir2(&tmp, &mem_ir1_opnd, EXMode_N);
        // tmp => dst_opnd
        latxs_append_ir2_opnd2ii(LISA_BSTRINS_D, &dst_opnd, &tmp, 15, 0);
    }
    if (pushpop) {
        td->sp_patch += esp_increment * 8;
#ifdef CONFIG_LATX_PROFILER
        TranslationBlock *tb = lsenv->tr_data->curr_tb;
        per_tb_count((void *)&((tb->profile).pushpop_sub), 1);
#endif
    } else {
        latxs_append_ir2_opnd2i(LISA_ADDI_D, &esp_opnd, &esp_opnd, esp_increment * 8);
    }

    return true;
}

bool latxs_translate_popal(IR1_INST *pir1)
{
    IR2_OPND esp_opnd = latxs_ra_alloc_gpr(esp_index);
    int esp_patch = 0;
    int esp_increment = 4;
    IR1_OPND mem_ir1_opnd;
    IR2_OPND dst_opnd;
    IR2_OPND tmp = latxs_ra_alloc_itemp();
    TRANSLATION_DATA *td = lsenv->tr_data;

    bool pushpop = get_pushpop_option(td);
    if (pushpop) {
        esp_patch = td->sp_patch;
    }
    for (int i = 7; i >= 0; i--) {
        if (i == esp_index) {
            // skip esp
            continue;
        }
        dst_opnd = latxs_ra_alloc_gpr(i);
        ir1_opnd_build_full_mem(&mem_ir1_opnd, esp_increment << 3,
                X86_REG_SS, X86_REG_ESP,
                esp_patch + esp_increment * (7 - i), 0, 0);
        latxs_load_ir1_to_ir2(&tmp, &mem_ir1_opnd, EXMode_N);
        // tmp => dst_opnd
        latxs_append_ir2_opnd2ii(LISA_BSTRPICK_D, &dst_opnd, &tmp, 31, 0);
    }
    if (pushpop) {
        td->sp_patch += esp_increment * 8;
#ifdef CONFIG_LATX_PROFILER
        TranslationBlock *tb = lsenv->tr_data->curr_tb;
        per_tb_count((void *)&((tb->profile).pushpop_sub), 1);
#endif
    } else {
        latxs_append_ir2_opnd2i(LISA_ADDI_D, &esp_opnd, &esp_opnd, esp_increment * 8);
    }
    return true;
}

static bool latxs_do_translate_lxx(
        IR1_INST *pir1, IR1_OPND *seg_ir1_opnd)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0); /* dest: GPR */
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1); /* src : MEM */

    /*
     *      data size | 16 | 32 | mem opnd
     * -------------------------------------
     *    offset size | 16 | 32 |  m16:16
     *  selector size | 16 | 16 |  m16:32
     */

    int opnd_size = ir1_opnd_size(opnd0);
    int data_size = latxs_ir1_data_size(pir1);
    lsassert(data_size == 16 || data_size == 32);
    lsassert(opnd_size == data_size);
    (void)opnd_size; /* to avoid compile warning */

    IR2_OPND mem_base;
    int mem_offset;
    latxs_convert_mem_opnd(&mem_base, &mem_offset, opnd1, -1);
    int save_temp = 1;

    TRANSLATION_DATA *td = lsenv->tr_data;

    /* 1. load offset at MEM */
    IR2_OPND offset_opnd = latxs_ra_alloc_itemp();
    IR2_OPCODE opc = data_size == 16 ? LISA_LD_HU : LISA_LD_WU;
    gen_ldst_softmmu_helper(opc, &offset_opnd, &mem_base, mem_offset, save_temp);
    /* 1.1 save offset to not be ruined by store_ir2_to_ir1_seg */
    if (td->sys.pe && !td->sys.vm86) {
        latxs_append_ir2_opnd2i(LISA_ST_D, &offset_opnd,
                &latxs_env_ir2_opnd,
                offsetof(CPUX86State, tmparea[0]));
    }

    /* 2. load selector at MEM + 2 or 4 */
    IR2_OPND selector_opnd = latxs_ra_alloc_itemp();
    switch (data_size >> 3) {
    case 2:
        gen_ldst_softmmu_helper(LISA_LD_HU, &selector_opnd,
                &mem_base, mem_offset + 2, save_temp);
        break;
    case 4:
        gen_ldst_softmmu_helper(LISA_LD_HU, &selector_opnd,
                &mem_base, mem_offset + 4, save_temp);
        break;
    default:
        lsassert(0);
        break;
    }

    /* 3. store selector to segment register */
    latxs_store_ir2_to_ir1_seg(&selector_opnd, seg_ir1_opnd);
    latxs_ra_free_temp(&selector_opnd);

    /* 4.0 load offset if it is saved */
    if (td->sys.pe && !td->sys.vm86) {
        latxs_append_ir2_opnd2i(LISA_LD_D, &offset_opnd,
                &latxs_env_ir2_opnd,
                offsetof(CPUX86State, tmparea[0]));
    }

    /* 4. move offset into GPR.                     */
    /*    Do this at the end for precise exception. */
    latxs_store_ir2_to_ir1_gpr(&offset_opnd, opnd0);
    latxs_ra_free_temp(&offset_opnd);

    return true;
}

/* End of TB in system-mode : PE & !vm86 & code32 */
bool latxs_translate_lds(IR1_INST *pir1)
{
    IR1_OPND seg_ir1_opnd;
    ir1_opnd_build_reg(&seg_ir1_opnd, 16, X86_REG_DS);
    return latxs_do_translate_lxx(pir1, &seg_ir1_opnd);
}
/* End of TB in system-mode : PE & !vm86 & code32*/
bool latxs_translate_les(IR1_INST *pir1)
{
    IR1_OPND seg_ir1_opnd;
    ir1_opnd_build_reg(&seg_ir1_opnd, 16, X86_REG_ES);
    return latxs_do_translate_lxx(pir1, &seg_ir1_opnd);
}
bool latxs_translate_lfs(IR1_INST *pir1)
{
    IR1_OPND seg_ir1_opnd;
    ir1_opnd_build_reg(&seg_ir1_opnd, 16, X86_REG_FS);
    return latxs_do_translate_lxx(pir1, &seg_ir1_opnd);
}
bool latxs_translate_lgs(IR1_INST *pir1)
{
    IR1_OPND seg_ir1_opnd;
    ir1_opnd_build_reg(&seg_ir1_opnd, 16, X86_REG_GS);
    return latxs_do_translate_lxx(pir1, &seg_ir1_opnd);
}
/* End of TB in system-mode */
bool latxs_translate_lss(IR1_INST *pir1)
{
    IR1_OPND seg_ir1_opnd;
    ir1_opnd_build_reg(&seg_ir1_opnd, 16, X86_REG_SS);
    return latxs_do_translate_lxx(pir1, &seg_ir1_opnd);
}

bool latxs_translate_enter(IR1_INST *pir1)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0); /* esp_addend */
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1); /* level */
    int esp_addend  = ir1_opnd_uimm(opnd0);
    int level       = ir1_opnd_uimm(opnd1);

    TRANSLATION_DATA *td = lsenv->tr_data;

    int data_size = latxs_ir1_data_size(pir1);

    if (lsenv->tr_data->sys.code64) {
        data_size = (data_size == 16) ? 16 : 64;
    }

    IR2_OPND esp_opnd = latxs_ra_alloc_gpr(esp_index);
    IR2_OPND ebp_opnd = latxs_ra_alloc_gpr(ebp_index);

    IR2_OPND frametemp = latxs_ra_alloc_itemp();
    int esp_patch = 0;
    bool pushpop = get_pushpop_option(td);
    if (option_pushpop && td->sp_patch) {
        esp_patch = td->sp_patch;
    }
    int imm1 = esp_patch - (data_size >> 3);
    IR2_OPND imm_opnd1 = latxs_ra_alloc_itemp();
    latxs_load_imm64_to_ir2(&imm_opnd1, imm1);
    latxs_append_ir2_opnd3(LISA_ADD_D, &frametemp, &esp_opnd, &imm_opnd1);

    /* 1. push EBP into stack MEM(SS:ESP-d)*/
    IR1_OPND mem_ir1_opnd;
    /* 1.1 build MEM(SS:ESP-d) */
    ir1_opnd_build_full_mem(&mem_ir1_opnd, data_size,
            X86_REG_SS, X86_REG_RSP, esp_patch - (data_size >> 3), 0, 0);
    /* 1.2 store */
    int ss_addr_size = latxs_get_sys_stack_addr_size();
    latxs_store_ir2_to_ir1_mem(&ebp_opnd,
            &mem_ir1_opnd, ss_addr_size);

    level &= 31;
    if (level) {
        int i;
        /* copy level-1 pointers from the previous frame */
        for (i = 1; i < level; ++i) {
            ir1_opnd_build_full_mem(&mem_ir1_opnd, data_size,
                    X86_REG_SS, X86_REG_EBP, esp_patch - (data_size >> 3) * i, 0, 0);
            IR2_OPND value = latxs_ra_alloc_itemp();
            latxs_load_ir1_mem_to_ir2(&value,
                    &mem_ir1_opnd, EXMode_Z, ss_addr_size);

            ir1_opnd_build_full_mem(&mem_ir1_opnd, data_size,
                    X86_REG_SS, X86_REG_ESP,
                    esp_patch - (data_size >> 3) * (i + 1), 0, 0);
            latxs_store_ir2_to_ir1_mem(&value,
                    &mem_ir1_opnd, ss_addr_size);
            latxs_ra_free_temp(&value);
        }
        /* push current FrameTemp as the last level */
        ir1_opnd_build_full_mem(&mem_ir1_opnd, data_size,
                X86_REG_SS, X86_REG_ESP,
                esp_patch - (data_size >> 3) * (level + 1), 0, 0);
        latxs_store_ir2_to_ir1_mem(&frametemp,
                &mem_ir1_opnd, ss_addr_size);
    }

    /* copy FrameTemp vlaue to EBP */
    if (td->sys.code64) {
        latxs_store_ir2_to_ir1_gpr(&frametemp, &rbp_ir1_opnd);
    } else if (td->sys.ss32) {
        latxs_store_ir2_to_ir1_gpr(&frametemp, &ebp_ir1_opnd);
    } else {
        latxs_store_ir2_to_ir1_gpr(&frametemp, &bp_ir1_opnd);
    }

    int imm2 = -(esp_addend + (data_size >> 3) * level);
    if (pushpop) {
        /*
         * The push of EBP above is materialized to MEM(SS:RSP) but RSP itself
         * is only tracked through sp_patch, so the pushed slot must be
         * accounted here as well.  Without it the next push/pop in the new
         * frame aliases the saved EBP slot: the callee's first push overwrites
         * the saved frame pointer and its LEAVE then reloads EBP from it
         * (ring-3 only, because pushpop is disabled for CPL 0).
         */
        td->sp_patch += imm2 - (data_size >> 3);
#ifdef CONFIG_LATX_PROFILER
        TranslationBlock *tb = lsenv->tr_data->curr_tb;
        per_tb_count((void *)&((tb->profile).pushpop_sub), 2);
#endif
    } else {
    /* compute final value of ESP */
        IR2_OPND final_esp = latxs_ra_alloc_itemp();
        IR2_OPND imm_opnd2 = latxs_ra_alloc_itemp();
        latxs_load_imm64_to_ir2(&imm_opnd2, imm2);
        latxs_append_ir2_opnd3(LISA_ADD_D, &final_esp, &frametemp, &imm_opnd2);
#ifdef TARGET_X86_64
        if (td->sys.code64) {
            latxs_store_ir2_to_ir1_gpr(&final_esp, &rsp_ir1_opnd);
        } else
#endif
        if (td->sys.ss32) {
            latxs_store_ir2_to_ir1_gpr(&final_esp, &esp_ir1_opnd);
        } else {
            latxs_store_ir2_to_ir1_gpr(&final_esp, &sp_ir1_opnd);
        }
        latxs_ra_free_temp(&final_esp);
    }
    latxs_ra_free_temp(&frametemp);
    return true;
}

bool latxs_translate_leave(IR1_INST *pir1)
{
    IR2_OPND esp_opnd = latxs_ra_alloc_gpr(esp_index);
    IR2_OPND ebp_opnd = latxs_ra_alloc_gpr(ebp_index);

    int ss_addr_size = latxs_get_sys_stack_addr_size();
    int data_size = latxs_ir1_data_size(pir1);

    if (lsenv->tr_data->sys.code64) {
        data_size = (data_size == 16) ? 16 : 64;
    }

    /* 1. load value from MEM(SS:EBP) */
    IR1_OPND mem_ir1_opnd;
    TRANSLATION_DATA *td = lsenv->tr_data;
    if (option_pushpop && td->sp_patch) {
        td->sp_patch = 0;
    }
    ir1_opnd_build_full_mem(&mem_ir1_opnd, data_size,
            X86_REG_SS, X86_REG_RBP, 0, 0, 0);

    IR2_OPND new_ebp = latxs_ra_alloc_itemp();
    latxs_load_ir1_mem_to_ir2(&new_ebp, &mem_ir1_opnd,
            EXMode_Z, ss_addr_size);

    IR2_OPND new_esp = latxs_ra_alloc_itemp();

    if (lsenv->tr_data->sys.code64) {
        latxs_append_ir2_opnd2i(LISA_ADDI_D, &new_esp, &ebp_opnd,
                                data_size >> 3);
    } else {
        latxs_append_ir2_opnd2i(LISA_ADDI_W, &new_esp, &ebp_opnd,
                                data_size >> 3);
    }

    /* 2. update EBP according to data_size */
    if (data_size == 64) {
        latxs_append_ir2_opnd2_(lisa_mov, &ebp_opnd, &new_ebp);
    } else if (data_size == 32) {
        latxs_append_ir2_opnd2_(lisa_mov, &ebp_opnd, &new_ebp);
    } else if (data_size == 16) {
        latxs_store_ir2_to_ir1_gpr(&new_ebp, &bp_ir1_opnd);
    }

    /* 3. update ESP according to ss_addr_size */
    if (lsenv->tr_data->sys.code64) {
        latxs_append_ir2_opnd2_(lisa_mov, &esp_opnd, &new_esp);
    } else if (ss_addr_size == 4) {
        latxs_append_ir2_opnd2_(lisa_mov, &esp_opnd, &new_esp);
        latxs_append_ir2_opnd2_(lisa_mov32z, &esp_opnd, &esp_opnd);
    } else {
        latxs_store_ir2_to_ir1_gpr(&new_esp, &sp_ir1_opnd);
    }

    return true;
}

/* Use the same rate and per-vCPU offset as cpu_get_tsc() and the helpers. */
static void latxs_gen_read_tsc(IR2_OPND *value, IR2_OPND *tmp)
{
    latxs_append_ir2_opnd2(LISA_RDTIME_D, value, tmp);
#ifdef CONFIG_TSC_FREQ
    latxs_load_imm64(tmp, option_opt_rdtsc_freq);
    la_out3(LISA_MUL_D, value, value, tmp);
#endif
    /* LDPTR uses a signed 14-bit displacement in units of four bytes. */
    QEMU_BUILD_BUG_ON(offsetof(CPUX86State, tsc_offset) >= 32768);
    QEMU_BUILD_BUG_ON(offsetof(CPUX86State, tsc_base_offset) >= 32768);
    la_out2i(LISA_LDPTR_D, tmp, &latxs_env_ir2_opnd,
             offsetof(CPUX86State, tsc_base_offset) / 4);
    la_out3(LISA_ADD_D, value, value, tmp);
    la_out2i(LISA_LDPTR_D, tmp, &latxs_env_ir2_opnd,
             offsetof(CPUX86State, tsc_offset) / 4);
    la_out3(LISA_ADD_D, value, value, tmp);
}

bool latxs_translate_rdtsc(IR1_INST *pir1)
{
    if (option_opt_rdtsc_rdtscp) {
        IR2_OPND dest0 = latxs_ra_alloc_itemp();
        IR2_OPND dest1 = latxs_ra_alloc_itemp();

        IR2_OPND rax = latxs_ra_alloc_gpr(eax_index);
        IR2_OPND rdx = latxs_ra_alloc_gpr(edx_index);

        latxs_gen_read_tsc(&dest1, &dest0);
        latxs_append_ir2_opnd2ii(LISA_BSTRPICK_D, &rax, &dest1, 31, 0);
        latxs_append_ir2_opnd2ii(LISA_BSTRPICK_D, &rdx, &dest1, 63, 32);
     } else {
        TRANSLATION_DATA *td = lsenv->tr_data;

        /* 0. save next instruciton's EIP to env */
        latxs_tr_gen_save_curr_eip();

        if (td->sys.cflags & CF_USE_ICOUNT) {
            latxs_tr_gen_io_start();
        }

        /*
         * target/i386/tcg/misc_helper.c
         * void helper_rdtsc(CPUX86State *env)
         */
        helper_cfg_t cfg = default_helper_cfg;
        latxs_tr_gen_call_to_helper1_cfg((ADDR)helper_rdtsc, cfg, HELPER_RDTSC);

        if (td->sys.cflags & CF_USE_ICOUNT) {
            latxs_tr_gen_io_end();
        }
    }

    return true;
}

bool latxs_translate_rdtscp(IR1_INST *pir1)
{
    if (option_opt_rdtsc_rdtscp) {
        if (latxs_tr_gen_excp_check(pir1)) {
            return true;
        }

        IR2_OPND dest0 = latxs_ra_alloc_itemp();
        IR2_OPND dest1 = latxs_ra_alloc_itemp();

        IR2_OPND rax = latxs_ra_alloc_gpr(eax_index);
        IR2_OPND rdx = latxs_ra_alloc_gpr(edx_index);
        IR2_OPND rcx = latxs_ra_alloc_gpr(ecx_index);

        latxs_gen_read_tsc(&dest1, &dest0);
        latxs_append_ir2_opnd2ii(LISA_BSTRPICK_D, &rax, &dest1, 31, 0);
        latxs_append_ir2_opnd2ii(LISA_BSTRPICK_D, &rdx, &dest1, 63, 32);

        latxs_load_imm64(&dest0, offsetof(CPUX86State, tsc_aux));
        la_out3(LISA_LDX_WU, &rcx, &latxs_env_ir2_opnd, &dest0);
    } else {
        TRANSLATION_DATA *td = lsenv->tr_data;

        /* 0. save next instruciton's EIP to env */
        latxs_tr_gen_save_curr_eip();

        if (td->sys.cflags & CF_USE_ICOUNT) {
            latxs_tr_gen_io_start();
        }

        /*
         * target/i386/tcg/misc_helper.c
         * void helper_rdtsc(CPUX86State *env)
         * target_ulong HELPER(rdpid)(CPUX86State *env)
         */
        helper_cfg_t cfg = default_helper_cfg;
        latxs_tr_gen_call_to_helper1_cfg((ADDR)helper_rdtsc, cfg, HELPER_RDTSC);

        /*latxs_tr_gen_call_to_helper1_cfg((ADDR)helper_rdpid, cfg);*/
        IR2_OPND rcx = latxs_ra_alloc_gpr(ecx_index);
        IR2_OPND offset = latxs_ra_alloc_itemp();
        latxs_load_imm64(&offset, offsetof(CPUX86State, tsc_aux));
        la_out3(LISA_LDX_WU, &rcx, &latxs_env_ir2_opnd, &offset);

        if (td->sys.cflags & CF_USE_ICOUNT) {
            latxs_tr_gen_io_end();
        }
    }

    return true;
}

bool latxs_translate_rdpmc(IR1_INST *pir1)
{
    /* 0. save next instruciton's EIP to env */
    latxs_tr_gen_save_curr_eip();

    /*
     * target/i386/misc_helper.c
     * void helper_rdpmc(CPUX86State *env)
     * >> exception might be generated
     */
    helper_cfg_t cfg = default_helper_cfg;
    latxs_tr_gen_call_to_helper1_cfg((ADDR)helper_rdpmc, cfg, HELPER_RDPMC);

    return true;
}

bool latxs_translate_rdrand(IR1_INST *pir1)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    bool seed = ir1_opcode(pir1) == X86_INS_RDSEED;
    bool supported = seed ?
        (td->sys.cpuid_7_0_ebx_features & CPUID_7_0_EBX_RDSEED) :
        (td->sys.cpuid_ext_features & CPUID_EXT_RDRAND);

    if (pir1->info->detail->x86.prefix[0] || !supported ||
        !ir1_opnd_is_gpr(ir1_get_opnd(pir1, 0))) {
#ifdef CONFIG_SOFTMMU
        latxs_tr_gen_excp_illegal_op(pir1, 1);
        return true;
#else
        return false;
#endif
    }

    if (td->sys.cflags & CF_USE_ICOUNT) {
        latxs_tr_gen_io_start();
    }

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    /* save context */
    latxs_tr_gen_call_to_helper_prologue_cfg(nofp_helper_cfg);
    /*  arg0: env */
    latxs_append_ir2_opnd2_(lisa_mov, &latxs_arg0_ir2_opnd, &latxs_env_ir2_opnd);
    if (hamt_enabled()) {
        latxs_tr_dmw_out_fix_mem(&latxs_arg0_ir2_opnd);
    }
    /* call helper */
    latxs_tr_gen_call_to_helper((ADDR)helper_rdrand, HELPER_RDRAND);
    /* restore context */
    latxs_tr_gen_call_to_helper_epilogue_cfg(nofp_helper_cfg);
    /* store return val */
    latxs_store_ir2_to_ir1(&latxs_ret0_ir2_opnd, opnd0);
    /* The shared helper reports success in cc_src. */
    latxs_tr_load_eflags();

    if (td->sys.cflags & CF_USE_ICOUNT) {
        latxs_tr_gen_io_end();
    }

    return true;
}

bool latxs_translate_popcnt(IR1_INST *pir1)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    if (!(td->sys.cpuid_ext_features & CPUID_EXT_POPCNT)) {
        latxs_tr_gen_excp_illegal_op(pir1, 1);
        return true;
    }

    IR2_OPND src_opnd = latxs_ra_alloc_itemp();
    latxs_load_ir1_to_ir2(&src_opnd, ir1_get_opnd(pir1, 1), EXMode_Z);
    IR2_OPND count = latxs_ra_alloc_itemp();
    IR2_OPND temp = latxs_ra_alloc_itemp();
    IR2_OPND pop_count_ir2_opnd = latxs_ra_alloc_itemp();
    int pop_count = ir1_opnd_size(ir1_get_opnd(pir1, 0));
    IR2_OPND label_exit = latxs_ir2_opnd_new_label();
    IR2_OPND src_temp = latxs_ra_alloc_itemp();
    IR2_OPND label_zero = latxs_ir2_opnd_new_label();
    IR2_OPND label_start = latxs_ir2_opnd_new_label();

    /* set O S Z A C P = 0 */
    latxs_append_ir2_opnd1i(LISA_X86MTFLAG, &latxs_zero_ir2_opnd, 0x3f);
    latxs_append_ir2_opnd2i(LISA_ORI, &count, &latxs_zero_ir2_opnd, 0);

    latxs_append_ir2_opnd3(LISA_BEQ, &src_opnd, &latxs_zero_ir2_opnd, &label_zero);

    latxs_load_imm32_to_ir2(&pop_count_ir2_opnd, pop_count, EXMode_N);

    latxs_append_ir2_opnd3(LISA_OR, &src_temp, &latxs_zero_ir2_opnd, &src_opnd);
    /* label_start: */
    latxs_append_ir2_opnd1(LISA_LABEL, &label_start);
    latxs_append_ir2_opnd2i(LISA_ANDI, &temp, &src_temp, 1);
    latxs_append_ir2_opnd3(LISA_ADD_D, &count, &count, &temp);
    latxs_append_ir2_opnd2i(LISA_SRLI_D, &src_temp, &src_temp, 1);
    latxs_append_ir2_opnd2i(LISA_ADDI_D, &pop_count_ir2_opnd, &pop_count_ir2_opnd, -1);
    latxs_append_ir2_opnd3(LISA_BNE, &pop_count_ir2_opnd, &latxs_zero_ir2_opnd, &label_start);

    latxs_ra_free_temp(&temp);
    /* 5. finish all steps, jump to exit */
    latxs_append_ir2_opnd1(LISA_B, &label_exit);

    /* label_zero: */
    latxs_append_ir2_opnd1(LISA_LABEL, &label_zero);
    /* set zf = 1 */
    IR2_OPND n4095_opnd = latxs_ra_alloc_itemp();
    latxs_append_ir2_opnd2i(LISA_ORI, &n4095_opnd, &latxs_zero_ir2_opnd, 0xfff);
    latxs_append_ir2_opnd1i(LISA_X86MTFLAG, &n4095_opnd, 0x8);
    latxs_ra_free_temp(&n4095_opnd);

    /* label_exit: */
    latxs_append_ir2_opnd1(LISA_LABEL, &label_exit);
    latxs_store_ir2_to_ir1(&count, ir1_get_opnd(pir1, 0));
    latxs_ra_free_temp(&count);

    return true;
}

bool latxs_translate_hlt(IR1_INST *pir1)
{
#ifdef CONFIG_SOFTMMU
    TRANSLATION_DATA *td = lsenv->tr_data;
    CHECK_EXCP_HLT(pir1);

    /* 0. save next instruciton's EIP to env */
    latxs_tr_gen_save_curr_eip();

    /*
     * target/i386/misc_helper.c
     * void helper_hlt(
     *      CPUX86State  *env,
     *      int           next_eip_addend)
     */
    int next_eip_addend = latxs_ir1_inst_size(pir1);
    latxs_tr_gen_call_to_helper2_cfg((ADDR)helper_hlt,
            next_eip_addend, default_helper_cfg, HELPER_HLT);

    /* 2. should nevet reach here */
    latxs_tr_gen_infinite_loop();

#else
    latxs_append_ir2_opnd0_(lisa_nop);
#endif

    return true;
}

bool latxs_translate_wrmsr(IR1_INST *pir1)
{
#ifdef CONFIG_SOFTMMU
    TRANSLATION_DATA *td = lsenv->tr_data;
    CHECK_EXCP_WRMSR(pir1);
    latxs_tr_gen_save_curr_eip();

    /*
     * target/i386/misc_helper.c
     * void helper_wrmsr(CPUX86State *env)
     * >> EAX, ECX, EDX are used
     */
    latxs_tr_gen_call_to_helper1_cfg((ADDR)helper_wrmsr,
            default_helper_cfg, HELPER_WRMSR);

    return true;
#else
    return false;
#endif
}

bool latxs_translate_rdmsr(IR1_INST *pir1)
{
#ifdef CONFIG_SOFTMMU
    TRANSLATION_DATA *td = lsenv->tr_data;
    CHECK_EXCP_RDMSR(pir1);
    latxs_tr_gen_save_curr_eip();

    /*
     * target/i386/misc_helper.c
     * void helper_rdmsr(CPUX86State *env)
     * >> EAX, ECX, EDX are used
     */
    latxs_tr_gen_call_to_helper1_cfg((ADDR)helper_rdmsr,
            nofp_helper_cfg, HELPER_RDMSR);

    return true;
#else
    return false;
#endif
}

/* End of TB in system-mode */
bool latxs_translate_invpcid(IR1_INST *pir1)
{
#ifdef CONFIG_SOFTMMU
    IR1_OPND *type = ir1_get_opnd(pir1, 0);
    IR1_OPND *desc = ir1_get_opnd(pir1, 1);
    int seg = R_DS;
    IR2_OPND base;
    int offset;

    latxs_tr_gen_save_curr_eip();
    if (ir1_opnd_has_seg(desc)) {
        seg = ir1_opnd_get_seg_index(desc);
    } else {
        switch (desc->mem.base) {
        case X86_REG_BP: case X86_REG_EBP: case X86_REG_RBP:
        case X86_REG_SP: case X86_REG_ESP: case X86_REG_RSP:
            seg = R_SS;
            break;
        default:
            break;
        }
    }
    latxs_convert_mem_opnd(&base, &offset, desc, -1);
    latxs_tr_gen_call_to_helper_prologue_cfg(default_helper_cfg);
    latxs_append_ir2_opnd2i(LISA_ADDI_D, &latxs_arg1_ir2_opnd, &base, offset);
    latxs_load_imm32_to_ir2(&latxs_arg2_ir2_opnd,
                            ir1_opnd_base_reg_num(type), EXMode_Z);
    latxs_load_imm32_to_ir2(&latxs_arg3_ir2_opnd, seg, EXMode_Z);
    latxs_append_ir2_opnd2_(lisa_mov, &latxs_arg0_ir2_opnd,
                            &latxs_env_ir2_opnd);
    if (hamt_enabled()) {
        latxs_tr_dmw_out_fix_mem(&latxs_arg0_ir2_opnd);
    }
    latxs_tr_gen_call_to_helper((ADDR)helper_invpcid, HELPER_INVPCID);
    latxs_tr_gen_call_to_helper_epilogue_cfg(default_helper_cfg);
    latxs_ra_free_temp(&base);
    return true;
#else
    return false;
#endif
}

bool latxs_translate_invlpg(IR1_INST *pir1)
{
#ifdef CONFIG_SOFTMMU
    if (latxs_tr_gen_excp_check(pir1)) {
        return true;
    }

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    lsassert(ir1_opnd_is_mem(opnd0));

    /* 0. save current instruciton's EIP to env */
    latxs_tr_gen_save_curr_eip();

    /* 1. get mem address */
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

    /*
     * 2. helper_invlpg
     * target/i386/tcg/sysemu/misc_helper.c
     * void helper_flush_page(
     *      CPUX86State *env,
     *      target_ulong addr)
     */

    /* 2.0 save context */
    helper_cfg_t cfg = default_helper_cfg;
    latxs_tr_gen_call_to_helper_prologue_cfg(cfg);

    /* 2.1 arg1: address */
    /* latxs_append_ir2_opnd3(LISA_OR, &latxs_arg1_ir2_opnd, */
            /* &addr_opnd, &latxs_zero_ir2_opnd); */
    if (lsenv->tr_data->sys.code64) {
        latxs_append_ir2_opnd2_(lisa_mov, &latxs_arg1_ir2_opnd, &addr_opnd);
    } else {
        latxs_append_ir2_opnd2_(lisa_mov32z, &latxs_arg1_ir2_opnd, &addr_opnd);
    }

    latxs_ra_free_temp(&addr_opnd);
    /* 2.2 arg0: env */
    latxs_append_ir2_opnd3(LISA_OR, &latxs_arg0_ir2_opnd,
            &latxs_env_ir2_opnd, &latxs_zero_ir2_opnd);
    if (hamt_enabled()) {
        latxs_tr_dmw_out_fix_mem(&latxs_arg0_ir2_opnd);
    }

    /* 2.3 call helper_invlpg */
    latxs_tr_gen_call_to_helper((ADDR)helper_flush_page, HELPER_FLUSH_PAGE);

    /* 2.4 restore context */
    latxs_tr_gen_call_to_helper_epilogue_cfg(cfg);

    return true;
#else
    return false;
#endif
}

bool latxs_translate_invlpga(IR1_INST *pir1)
{
    return false;
}

bool latxs_translate_lfence(IR1_INST *pir1)
{
    if (latxs_tr_gen_excp_check(pir1)) {
        return true;
    }

    /* TRANSLATION_DATA *td = lsenv->tr_data; */
    /* if (td->sys.cflags & CF_PARALLEL) { */
        /* TODO: MTTCG */
    /* } */
    latxs_append_ir2_opndi(LISA_DBAR, 0x15);
    return true;
}
bool latxs_translate_mfence(IR1_INST *pir1)
{
    if (latxs_tr_gen_excp_check(pir1)) {
        return true;
    }

    /* TRANSLATION_DATA *td = lsenv->tr_data; */
    /* if (td->sys.cflags & CF_PARALLEL) { */
        /* TODO: MTTCG */
    /* } */
    latxs_append_ir2_opndi(LISA_DBAR, 0x10);

    return true;
}
bool latxs_translate_sfence(IR1_INST *pir1)
{
    if (latxs_tr_gen_excp_check(pir1)) {
        return true;
    }

    /* TRANSLATION_DATA *td = lsenv->tr_data; */
    /* if (td->sys.cflags & CF_PARALLEL) { */
        /* TODO: MTTCG */
    /* } */
    latxs_append_ir2_opndi(LISA_DBAR, 0x1a);

    return true;
}

/* End of TB in system-mode */
bool latxs_translate_sysenter(IR1_INST *pir1)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    CHECK_EXCP_SYSENTER(pir1);

    /*
     * target/i386/seg_helper.c
     * void helper_sysenter(CPUX86State *env)
     * >> EIP is updated
     */
    helper_cfg_t cfg = default_helper_cfg;
    latxs_tr_gen_call_to_helper1_cfg((ADDR)helper_sysenter, cfg, HELPER_SYSENTER);

    /* 2. disable EIP update in the added jmp */
    td->ignore_eip_update = 1;

    return true;
}

/* End of TB in system-mode */
bool latxs_translate_sysexit(IR1_INST *pir1)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    CHECK_EXCP_SYSEXIT(pir1);

    int data_size = latxs_ir1_data_size(pir1);
    lsassert(data_size == 16 || data_size == 32);

    /*
     * target/i386/seg_helper.c
     * void helper_sysexit(
     *      CPUX86State *env,
     *      int dflag)
     */
    helper_cfg_t cfg = default_helper_cfg;

    /* 1.1 dflag = 0 : 16-bit */
    /*     dflag = 1 : 32-bit */
    int dflag = (data_size >> 4) - 1;
    lsassert(!(dflag >> 1));

    /* 1.2 call helper_sysexit*/
    latxs_tr_gen_call_to_helper2_cfg((ADDR)helper_sysexit, dflag, cfg, HELPER_SYSEXIT);

    /* 2. disable EIP update in the added jmp */
    td->ignore_eip_update = 1;

    return true;
}

static bool latxs_translate_opt_syscall(IR1_INST *pir1)
{
    /* pcrel, we should avoid directly storing next_eip */
    TRANSLATION_DATA *td = lsenv->tr_data;
    TranslationBlock *tb = td->curr_tb;
    int next_eip_offset = 0;
    IR2_OPND label_helper = latxs_ir2_opnd_new_label();
    IR2_OPND label_end = latxs_ir2_opnd_new_label();

    IR2_OPND ir2_r11 = latxs_ra_alloc_gpr(r11_index);
    IR2_OPND ir2_rcx = latxs_ra_alloc_gpr(rcx_index);

    IR2_OPND tmp = latxs_ra_alloc_itemp();
    IR2_OPND tmp1 = latxs_ra_alloc_itemp();
    IR2_OPND tmp2 = latxs_ra_alloc_itemp();

    //latxs_append_ir2_opnd1(LISA_B, &label_helper);

    latxs_append_ir2_opnd2i(LISA_LD_D, &tmp, &latxs_env_ir2_opnd,
        lsenv_offset_of_efer(lsenv));

    /* !(env->efer & MSR_EFER_SCE)*/
    la_out2i(LISA_ANDI, &tmp, &tmp, MSR_EFER_SCE);
    la_out2(LISA_BEQZ, &tmp, &label_helper);

    /* if !(env->hflags & HF_LMA_MASK), call helper */
    latxs_append_ir2_opnd2i(LISA_LD_WU, &tmp, &latxs_env_ir2_opnd,
       lsenv_offset_of_hflags(lsenv));
    latxs_load_imm64(&tmp1, HF_LMA_MASK);
    la_out3(LISA_AND, &tmp1, &tmp, &tmp1);
    la_out2(LISA_BEQZ, &tmp1, &label_helper);

    /* if !(env->hflags & HF_CS64_MASK), call helper */
    latxs_load_imm64(&tmp1, HF_CS64_MASK);
    la_out3(LISA_AND, &tmp1, &tmp, &tmp1);
    la_out2(LISA_BEQZ, &tmp1, &label_helper);

    latxs_tr_gen_add_st_curr_eip(&tmp, next_eip_offset, ir1_addr(pir1));

    /* env->regs[R_ECX] = env->eip + next_eip_addend; */
    la_out2i(LISA_LD_D, &ir2_rcx, &latxs_env_ir2_opnd,
        lsenv_offset_of_eip(lsenv));
    la_out2i(LISA_ADDI_D, &ir2_rcx, &ir2_rcx, latxs_ir1_inst_size(pir1));

    /* env->regs[11] = cpu_compute_eflags(env) & ~RF_MASK; */
    la_out1i(LISA_X86MFFLAG, &ir2_r11, 0x3f);
    la_out2i(LISA_LD_WU, &tmp, &latxs_env_ir2_opnd,
        lsenv_offset_of_eflags(lsenv));

    latxs_load_imm64(&tmp1, (CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C));
    la_out3(LISA_ANDN, &tmp2, &tmp, &tmp1);
    la_out3(LISA_OR, &ir2_r11, &tmp2, &ir2_r11);

    /* eflags |= cpu_cc_compute_all(env, CC_OP) | (env->df & DF_MASK); */
    la_out2i(LISA_LD_W, &tmp, &latxs_env_ir2_opnd,
       lsenv_offset_of_df(lsenv));
    la_out2i(LISA_ANDI, &tmp, &tmp, DF_MASK);
    la_out3(LISA_OR, &ir2_r11, &tmp, &ir2_r11);

    latxs_load_imm64(&tmp1, RF_MASK);
    la_out3(LISA_ANDN, &ir2_r11, &ir2_r11, &tmp1);

    /* env->eflags &= ~(env->fmask | RF_MASK); */
    la_out2i(LISA_LD_WU, &tmp, &latxs_env_ir2_opnd,
        lsenv_offset_of_eflags(lsenv));
    la_out2i(LISA_LD_D, &tmp2, &latxs_env_ir2_opnd,
        lsenv_offset_of_fmask(lsenv));
    la_out3(LISA_OR, &tmp2, &tmp2, &tmp1);
    la_out3(LISA_ANDN, &tmp, &tmp, &tmp2);
    la_out2i(LISA_ST_W, &tmp, &latxs_env_ir2_opnd,
        lsenv_offset_of_eflags(lsenv));

    /* cpu_x86_load_seg_cache */
    latx_cpu_load_eflags(&tmp, &la_zero);

    /* selector = (env->star >> 32) & 0xffff */
    //la_out2i(LISA_LD_D, &tmp, &latxs_env_ir2_opnd,
    //    lsenv_offset_of_star(lsenv));
    latxs_load_imm64(&tmp1, lsenv_offset_of_star(lsenv));
    la_out3(LISA_LDX_D, &tmp, &latxs_env_ir2_opnd, &tmp1);

    la_out2i(LISA_SRLI_D, &tmp, &tmp, 32);
    latxs_load_imm64(&tmp1, 0xffff);
    la_out3(LISA_AND, &tmp2, &tmp, &tmp1);

    /* selector = selector & 0xfffc */
    latxs_load_imm64(&tmp1, 0xfffc);
    la_out3(LISA_AND, &tmp, &tmp2, &tmp1);

    /*
     * cpu_x86_load_seg_cache(env, R_CS, selector & 0xfffc,
     *                    0, 0xffffffff,
     *                        DESC_G_MASK | DESC_P_MASK |
     *                        DESC_S_MASK |
     *                        DESC_CS_MASK | DESC_R_MASK | DESC_A_MASK |
     *                        DESC_L_MASK);
     */

    IR2_OPND flags = latxs_ra_alloc_itemp();
    IR2_OPND limit = latxs_ra_alloc_itemp();

    latxs_load_imm64(&flags, DESC_G_MASK | DESC_P_MASK |
                           DESC_S_MASK |
                           DESC_CS_MASK | DESC_R_MASK | DESC_A_MASK |
                           DESC_L_MASK);

    latxs_load_imm64(&limit, 0xffffffff);
    fast_cpu_x86_load_seg_cache(R_CS, &tmp,
                       &la_zero, &limit, &flags);

    /* selector = (selector + 8) & 0xfffc */
    la_out2i(LISA_ADDI_D, &tmp2, &tmp2, 8);
    la_out3(LISA_AND, &tmp, &tmp2, &tmp1);

    /*
     *  cpu_x86_load_seg_cache(env, R_SS, (selector + 8) & 0xfffc,
     *                         0, 0xffffffff,
     *                         DESC_G_MASK | DESC_B_MASK | DESC_P_MASK |
     *                         DESC_S_MASK |
     *                         DESC_W_MASK | DESC_A_MASK);
     */
    latxs_load_imm64(&flags, DESC_G_MASK | DESC_B_MASK | DESC_P_MASK |
                           DESC_S_MASK |
                           DESC_W_MASK | DESC_A_MASK);

    latxs_load_imm64(&limit, 0xffffffff);
    fast_cpu_x86_load_seg_cache(R_SS, &tmp,
                           &la_zero, &limit, &flags);
    latxs_ra_free_temp(&flags);
    latxs_ra_free_temp(&limit);

    /* env->eip = env->lstar; */
     la_out2i(LISA_LD_D, &tmp, &latxs_env_ir2_opnd,
        lsenv_offset_of_lstar(lsenv));
     la_out2i(LISA_ST_D, &tmp, &latxs_env_ir2_opnd,
        lsenv_offset_of_eip(lsenv));

    latxs_append_ir2_opnd1(LISA_B, &label_end);

    latxs_ra_free_temp(&tmp);
    latxs_ra_free_temp(&tmp1);
    latxs_ra_free_temp(&tmp2);

    latxs_append_ir2_opnd1(LISA_LABEL, &label_helper);

    IR2_OPND cur_eip = latxs_ra_alloc_itemp();

    if (option_tb_pcrel)
        next_eip_offset = td->pc_curr - td->pc_save;
    else if (option_page_pcrel)
        next_eip_offset = td->pc_curr - td->curr_page;
    latxs_tr_gen_add_st_curr_eip(&cur_eip, next_eip_offset, ir1_addr(pir1));
    latxs_ra_free_temp(&cur_eip);

    latxs_tr_gen_call_to_helper2_cfg((ADDR)helper_syscall, latxs_ir1_inst_size(pir1),
            nofp_helper_cfg, HELPER_SYSCALL);
    lsenv->tr_data->ignore_eip_update = 1;

    latxs_append_ir2_opnd1(LISA_LABEL, &label_end);

    /* syscall tb link */
    add_misc_flags(tb, MF_END_SYSCALL);

    IR2_OPND syscall_tb = latxs_ra_alloc_itemp();
    IR2_OPND tb_lookup = latxs_ir2_opnd_new_label();

    if (option_opt_syscall == 1) {
        la_out2i(LISA_LD_D, &syscall_tb, &latxs_env_ir2_opnd,
                    lsenv_offset_of_syscall_tb(lsenv));
        la_out2(LISA_BEQZ, &syscall_tb, &tb_lookup);
        if (hamt_enabled()) latxs_tr_dmw_in_fix_mem(&syscall_tb);
        la_out2i(LISA_JIRL, &la_zero, &syscall_tb, 0);
    }

    if (option_opt_syscall == 2) {
        IR2_OPND pc = latxs_ra_alloc_itemp();
        IR2_OPND tcptr = latxs_ra_alloc_itemp();
        la_out2i(LISA_LD_D, &syscall_tb, &latxs_env_ir2_opnd,
                    lsenv_offset_of_syscall_tb(lsenv));
        la_out2(LISA_BEQZ, &syscall_tb, &tb_lookup);
        if (hamt_enabled()) latxs_tr_dmw_in_fix_mem(&syscall_tb);

        la_out2i(LISA_LD_D, &pc, &la_env, offsetof(CPUX86State, eip));
        la_out2i(LISA_LD_D, &tcptr, &syscall_tb, offsetof(TranslationBlock, pc));
        la_out3(LISA_BNE, &pc, &tcptr, &tb_lookup);

        la_out2i(LISA_LD_D, &tcptr, &syscall_tb, offsetof(TranslationBlock, tc.ptr));
        if (hamt_enabled()) latxs_tr_dmw_in_fix_mem(&tcptr);
        la_out2i(LISA_JIRL, &la_zero, &tcptr, 0);
    }

    latxs_append_ir2_opnd1(LISA_LABEL, &tb_lookup);

    latxs_ra_free_temp(&syscall_tb);

    return true;
}

bool latxs_translate_syscall(IR1_INST *pir1)
{
    /* pcrel, we should avoid directly storing next_eip */
    TRANSLATION_DATA *td = lsenv->tr_data;
    /*TranslationBlock *tb = td->curr_tb;*/
    int next_eip_offset = 0;

    IR2_OPND cur_eip = latxs_ra_alloc_itemp();

    if (option_opt_syscall && !td->sys.tf) {
        latxs_translate_opt_syscall(pir1);
        return true;
    }

    if (option_tb_pcrel)
        next_eip_offset = td->pc_curr - td->pc_save;
    else if (option_page_pcrel)
        next_eip_offset = td->pc_curr - td->curr_page;
    latxs_tr_gen_add_st_curr_eip(&cur_eip, next_eip_offset, ir1_addr(pir1));
    latxs_ra_free_temp(&cur_eip);

    latxs_tr_gen_call_to_helper2_cfg((ADDR)helper_syscall, latxs_ir1_inst_size(pir1),
            nofp_helper_cfg, HELPER_SYSCALL);
    lsenv->tr_data->ignore_eip_update = 1;

    return true;
}

static bool latxs_translate_opt_sysret(IR1_INST *pir1)
{
    if (!lsenv->tr_data->sys.pe) {
        /* TODO: should raise EXCP0D_GPF */
        lsassert(0);
    }

    IR2_OPND label_helper = latxs_ir2_opnd_new_label();
    IR2_OPND label_end = latxs_ir2_opnd_new_label();

    IR2_OPND ir2_r11 = latxs_ra_alloc_gpr(r11_index);

    IR2_OPND tmp = latxs_ra_alloc_itemp();
    IR2_OPND tmp1 = latxs_ra_alloc_itemp();
    IR2_OPND tmp2 = latxs_ra_alloc_itemp();

    /* !(env->efer & MSR_EFER_SCE), call helper*/
    la_out2i(LISA_ANDI, &tmp, &tmp, MSR_EFER_SCE);
    la_out2(LISA_BEQZ, &tmp, &label_helper);

    /* !(env->cr[0] & CR0_PE_MASK) || cpl != 0, call helper */
    latxs_append_ir2_opnd2i(LISA_LD_WU, &tmp, &latxs_env_ir2_opnd,
        lsenv_offset_of_hflags(lsenv));
    la_out2i(LISA_ANDI, &tmp, &tmp, HF_CPL_MASK);
    la_out2(LISA_BNEZ, &tmp, &label_helper);

    latxs_append_ir2_opnd2i(LISA_LD_WU, &tmp, &latxs_env_ir2_opnd,
        lsenv_offset_of_cr(lsenv, 0));
    la_out2i(LISA_ANDI, &tmp, &tmp, CR0_PE_MASK);
    la_out2(LISA_BNEZ, &tmp, &label_helper);

    /* if !(env->hflags & HF_LMA_MASK), call helper */
    latxs_append_ir2_opnd2i(LISA_LD_WU, &tmp, &latxs_env_ir2_opnd,
        lsenv_offset_of_hflags(lsenv));
    latxs_load_imm64(&tmp1, HF_LMA_MASK);
    la_out3(LISA_AND, &tmp1, &tmp, &tmp1);
    la_out2(LISA_BEQZ, &tmp1, &label_helper);

    /* cpu_load_eflags */
    latxs_load_imm64(&tmp2, TF_MASK | AC_MASK
                        | ID_MASK | IF_MASK | IOPL_MASK | VM_MASK | RF_MASK |
                        NT_MASK);
    latx_cpu_load_eflags(&ir2_r11, &tmp2);
    /* selector = (env->star >> 48) & 0xffff */
    latxs_load_imm64(&tmp1, lsenv_offset_of_star(lsenv));
    la_out3(LISA_LDX_D, &tmp, &latxs_env_ir2_opnd, &tmp1);

    la_out2i(LISA_SRLI_D, &tmp, &tmp, 48);
    latxs_load_imm64(&tmp1, 0xffff);
    la_out3(LISA_AND, &tmp2, &tmp, &tmp1);

    la_out2i(LISA_ADDI_D, &tmp, &tmp2, 16);
    la_out2i(LISA_ANDI, &tmp, &tmp, 3);

    IR2_OPND flags = latxs_ra_alloc_itemp();
    IR2_OPND limit = latxs_ra_alloc_itemp();
    /*
     * cpu_x86_load_seg_cache(env, R_CS, (selector + 16) | 3,
     *                        0, 0xffffffff,
     *                        DESC_G_MASK | DESC_P_MASK |
     *                        DESC_S_MASK | (3 << DESC_DPL_SHIFT) |
     *                        DESC_CS_MASK | DESC_R_MASK | DESC_A_MASK |
     *                        DESC_L_MASK);
     */

    latxs_load_imm64(&flags, DESC_G_MASK | DESC_P_MASK |
                           DESC_S_MASK | (3 << DESC_DPL_SHIFT) |
                           DESC_CS_MASK | DESC_R_MASK | DESC_A_MASK |
                           DESC_L_MASK);
    latxs_load_imm64(&limit, 0xffffffff);
    fast_cpu_x86_load_seg_cache(R_CS, &tmp,
                           &la_zero, &limit, &flags);

    la_out2i(LISA_ADDI_D, &tmp, &tmp2, 8);
    la_out2i(LISA_ANDI, &tmp, &tmp, 3);

    /*
     * cpu_x86_load_seg_cache(env, R_SS, (selector + 8) | 3,
     *                      0, 0xffffffff,
     *                      DESC_G_MASK | DESC_B_MASK | DESC_P_MASK |
     *                      DESC_S_MASK | (3 << DESC_DPL_SHIFT) |
     *                      DESC_W_MASK | DESC_A_MASK);
     */
    latxs_load_imm64(&flags, DESC_G_MASK | DESC_B_MASK | DESC_P_MASK |
                         DESC_S_MASK | (3 << DESC_DPL_SHIFT) |
                         DESC_W_MASK | DESC_A_MASK);
    fast_cpu_x86_load_seg_cache(R_SS, &tmp,
                         &la_zero, &limit, &flags);

    latxs_append_ir2_opnd1(LISA_B, &label_end);

    latxs_ra_free_temp(&flags);
    latxs_ra_free_temp(&limit);

    latxs_ra_free_temp(&tmp);
    latxs_ra_free_temp(&tmp1);
    latxs_ra_free_temp(&tmp2);

    latxs_append_ir2_opnd1(LISA_LABEL, &label_helper);

    int data_size = latxs_ir1_data_size(pir1);
    latxs_tr_gen_call_to_helper2_cfg((ADDR)helper_sysret, data_size / 32,
                                     default_helper_cfg, HELPER_SYSRET);
    lsenv->tr_data->ignore_eip_update = 1;

    latxs_append_ir2_opnd1(LISA_LABEL, &label_end);

    return true;
}

bool latxs_translate_sysret(IR1_INST *pir1)
{
    if (!lsenv->tr_data->sys.pe) {
        /* TODO: should raise EXCP0D_GPF */
        lsassert(0);
    }

    if (option_opt_sysret) {
        latxs_translate_opt_sysret(pir1);
        return true;
    }

    int data_size = latxs_ir1_data_size(pir1);
    latxs_tr_gen_call_to_helper2_cfg((ADDR)helper_sysret, data_size / 32,
                                     default_helper_cfg, HELPER_SYSRET);
    lsenv->tr_data->ignore_eip_update = 1;

    return true;
}

bool latxs_translate_lmsw(IR1_INST *pir1)
{
    return false;
}

bool latxs_translate_smsw(IR1_INST *pir1)
{
    //TRANSLATION_DATA *td = lsenv->tr_data;
    //CHECK_EXCP_SMSW(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    /* load cr0 from env */
    IR2_OPND cr0_low = latxs_ra_alloc_itemp();
    latxs_append_ir2_opnd2i(LISA_LD_W, &cr0_low, &latxs_env_ir2_opnd,
            lsenv_offset_of_cr(lsenv, 0));

    if (ir1_opnd_is_mem(opnd0)) {
        lsassert(ir1_opnd_size(opnd0) == 16);
    }
    latxs_store_ir2_to_ir1(&cr0_low, opnd0);

    return true;
}

bool latxs_translate_verr(IR1_INST *pir1)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    CHECK_EXCP_VERR(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);

    IR2_OPND src = latxs_load_ireg_from_ir1(opnd0, EXMode_N);

    latxs_tr_gen_call_to_helper_prologue_cfg(nofp_helper_cfg);

    latxs_append_ir2_opnd2_(lisa_mov, &latxs_arg0_ir2_opnd,
                            &latxs_env_ir2_opnd);
    if (hamt_enabled()) {
        latxs_tr_dmw_out_fix_mem(&latxs_arg0_ir2_opnd);
    }
    latxs_append_ir2_opnd2_(lisa_mov, &latxs_arg1_ir2_opnd, &src);

    latxs_tr_gen_call_to_helper((ADDR)helper_verr, HELPER_VERR);

    latxs_tr_gen_call_to_helper_epilogue_cfg(nofp_helper_cfg);

    return true;
}

bool latxs_translate_verw(IR1_INST *pir1)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    CHECK_EXCP_VERW(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);

    IR2_OPND src = latxs_load_ireg_from_ir1(opnd0, EXMode_N);

    latxs_tr_gen_call_to_helper_prologue_cfg(nofp_helper_cfg);

    latxs_append_ir2_opnd2_(lisa_mov, &latxs_arg0_ir2_opnd,
                            &latxs_env_ir2_opnd);
    if (hamt_enabled()) {
        latxs_tr_dmw_out_fix_mem(&latxs_arg0_ir2_opnd);
    }
    latxs_append_ir2_opnd2_(lisa_mov, &latxs_arg1_ir2_opnd, &src);

    latxs_tr_gen_call_to_helper((ADDR)helper_verw, HELPER_VERW);

    latxs_tr_gen_call_to_helper_epilogue_cfg(nofp_helper_cfg);

    return true;
}

bool latxs_translate_lar(IR1_INST *pir1)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    CHECK_EXCP_LSL(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    IR2_OPND seg_selector = latxs_ra_alloc_itemp();

    /* TODO: capstone mem opnd opsz should be 2 bytes */
    latxs_load_ir1_to_ir2(&seg_selector, opnd1, EXMode_N);
    latxs_append_ir2_opnd2_(lisa_mov16z, &seg_selector, &seg_selector);

    latxs_tr_gen_call_to_helper_prologue_cfg(nofp_helper_cfg);

    latxs_append_ir2_opnd2_(lisa_mov, &latxs_arg0_ir2_opnd,
                            &latxs_env_ir2_opnd);
    if (hamt_enabled()) {
        latxs_tr_dmw_out_fix_mem(&latxs_arg0_ir2_opnd);
    }
    latxs_append_ir2_opnd2_(lisa_mov, &latxs_arg1_ir2_opnd, &seg_selector);

    latxs_tr_gen_call_to_helper((ADDR)helper_lar, HELPER_LAR);

    latxs_tr_gen_call_to_helper_epilogue_cfg(nofp_helper_cfg);

    IR2_OPND zf = latxs_ra_alloc_itemp();
    latxs_get_eflag_condition(&zf, pir1);

    IR2_OPND exit = latxs_ir2_opnd_new_label();
    latxs_append_ir2_opnd3(LISA_BEQ, &zf, &latxs_zero_ir2_opnd, &exit);

    latxs_store_ir2_to_ir1_gpr(&latxs_ret0_ir2_opnd, opnd0);

    latxs_append_ir2_opnd1(LISA_LABEL, &exit);

    return true;
}

bool latxs_translate_lsl(IR1_INST *pir1)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    CHECK_EXCP_LSL(pir1);

    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    IR2_OPND seg_selector = latxs_ra_alloc_itemp();

    /* TODO: capstone mem opnd opsz should be 2 bytes */
    latxs_load_ir1_to_ir2(&seg_selector, opnd1, EXMode_N);
    latxs_append_ir2_opnd2_(lisa_mov16z, &seg_selector, &seg_selector);

    latxs_tr_gen_call_to_helper_prologue_cfg(nofp_helper_cfg);

    latxs_append_ir2_opnd2_(lisa_mov, &latxs_arg0_ir2_opnd,
                            &latxs_env_ir2_opnd);
    if (hamt_enabled()) {
        latxs_tr_dmw_out_fix_mem(&latxs_arg0_ir2_opnd);
    }
    latxs_append_ir2_opnd2_(lisa_mov, &latxs_arg1_ir2_opnd, &seg_selector);

    latxs_tr_gen_call_to_helper((ADDR)helper_lsl, HELPER_LSL);

    latxs_tr_gen_call_to_helper_epilogue_cfg(nofp_helper_cfg);

    IR2_OPND zf = latxs_ra_alloc_itemp();
    latxs_get_eflag_condition(&zf, pir1);

    IR2_OPND exit = latxs_ir2_opnd_new_label();
    latxs_append_ir2_opnd3(LISA_BEQ, &zf, &latxs_zero_ir2_opnd, &exit);

    latxs_store_ir2_to_ir1_gpr(&latxs_ret0_ir2_opnd, opnd0);

    latxs_append_ir2_opnd1(LISA_LABEL, &exit);

    return true;
}

#ifdef CONFIG_LATX_TU
bool latxs_translate_prefetchnta(IR1_INST *pir1)
{
    latxs_append_ir2_opnd2i(LISA_ORI, &latxs_zero_ir2_opnd, &latxs_zero_ir2_opnd, 0);
    return true;
}
bool latxs_translate_prefetcht0(IR1_INST *pir1)
{
    bool direct = true;
#ifndef CONFIG_USER_ONLY
    direct = can_do_hamt(lsenv->cpu_state) &&
             lsenv->tr_data->sys.code64 &&
             latxs_ir1_addr_size(pir1) == 8;
#endif
    if (direct) {
        IR2_OPND address;
        IR2_OPND hint = latxs_ir2_opnd_new(IR2_OPND_IMMH, 0);
        int offset;
        latxs_convert_mem_opnd_opt(&address, &offset,
                                   ir1_get_opnd(pir1, 0), -1);
        latxs_append_ir2_opnd2i(LISA_PRELD, &hint, &address, offset);
        return true;
    }
    latxs_append_ir2_opnd2i(LISA_ORI, &latxs_zero_ir2_opnd, &latxs_zero_ir2_opnd, 0);
    return true;
}
bool latxs_translate_prefetcht1(IR1_INST *pir1)
{
    latxs_append_ir2_opnd2i(LISA_ORI, &latxs_zero_ir2_opnd, &latxs_zero_ir2_opnd, 0);
    return true;
}
bool latxs_translate_prefetcht2(IR1_INST *pir1)
{
    latxs_append_ir2_opnd2i(LISA_ORI, &latxs_zero_ir2_opnd, &latxs_zero_ir2_opnd, 0);
    return true;
}
bool latxs_translate_prefetchw(IR1_INST *pir1)
{
    latxs_append_ir2_opnd2i(LISA_ORI, &latxs_zero_ir2_opnd, &latxs_zero_ir2_opnd, 0);
    return true;
}
bool latxs_translate_prefetch(IR1_INST *pir1)
{
    latxs_append_ir2_opnd2i(LISA_ORI, &latxs_zero_ir2_opnd, &latxs_zero_ir2_opnd, 0);
    return true;
}
#else
bool latxs_translate_prefetchnta(IR1_INST *pir1) { return true; }
bool latxs_translate_prefetcht0(IR1_INST *pir1) { return true; }
bool latxs_translate_prefetcht1(IR1_INST *pir1) { return true; }
bool latxs_translate_prefetcht2(IR1_INST *pir1) { return true; }
bool latxs_translate_prefetchw(IR1_INST *pir1) { return true; }
bool latxs_translate_prefetch(IR1_INST *pir1) { return true; }
#endif

bool latxs_translate_tzcnt(IR1_INST *pir1)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0);
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1);

    int opnd_size = ir1_opnd_size(opnd0);
    IR2_OPND label_exit = latxs_ir2_opnd_new_label();
    IR2_OPND label_ctz = latxs_ir2_opnd_new_label();
    IR2_OPND count;
    bool count_opnd_is_temp = false;

    /* 1. load source data */
    IR2_OPND src_opnd = latxs_ra_alloc_itemp();
    latxs_load_ir1_to_ir2(&src_opnd, opnd1, EXMode_Z);

    /* if the reg is 64 bits or is zero-extend from the high bits */
    lsassert(ir1_opnd_is_gpr(opnd0));

    if (opnd_size == 64) {
        count = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd0));
    } else {
        count = latxs_ra_alloc_itemp();
        count_opnd_is_temp = true;
    }

    if (ir1_need_calculate_any_flag(pir1)) {
        IR2_OPND eflags = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd2i(LISA_ORI, &eflags, &latxs_zero_ir2_opnd, 0x1);
        latxs_append_ir2_opnd3(LISA_MASKNEZ, &eflags, &eflags, &src_opnd);
        latxs_append_ir2_opnd2ii(LISA_BSTRINS_W, &eflags, &src_opnd, 6, 6);
        latxs_append_ir2_opnd1i(LISA_X86MTFLAG, &eflags, ZF_USEDEF_BIT | CF_USEDEF_BIT | AF_USEDEF_BIT | PF_USEDEF_BIT);
        latxs_ra_free_temp(&eflags);
    }

    /* case 1: if reg/mem is zero, it means dest should be opnd_size*/
    latxs_append_ir2_opnd3(LISA_BNE, &src_opnd, &latxs_zero_ir2_opnd, &label_ctz);
    latxs_load_imm32_to_ir2(&count, opnd_size, EXMode_Z);
    latxs_append_ir2_opnd1(LISA_B, &label_exit);

    /* case 2: reg/mem not zero */
    /* bit scan forward, looks like the `ctz.d` */
    latxs_append_ir2_opnd1(LISA_LABEL, &label_ctz);
    latxs_append_ir2_opnd2(LISA_CTZ_D, &count, &src_opnd);

    latxs_append_ir2_opnd1(LISA_LABEL, &label_exit);
    /* 3. store result */
    if (count_opnd_is_temp) {
        latxs_store_ir2_to_ir1(&count, opnd0);
        latxs_ra_free_temp(&count);
    }

    return true;
}

bool latxs_translate_invalid(IR1_INST *pir1)
{
    switch (pir1->flags) {
#ifdef CONFIG_SOFTMMU
    case LATXS_IR1_FLAGS_ILLOP:
        latxs_tr_gen_excp_illegal_op_addr(
                ir1_addr(pir1) - lsenv->tr_data->sys.cs_base, 1);
        break;
#endif
    case LATXS_IR1_FLAGS_GENNOP:
        /* Just do nothing */
        break;
    default:
        lsassertm(0, "unsupport invalid ir1 falgs %d\n", pir1->flags);
        break;
    }
    return true;
};

bool latxs_translate_ud0(IR1_INST *pir1)
{
#ifdef CONFIG_SOFTMMU
    latxs_tr_gen_excp_illegal_op_addr(
            ir1_addr(pir1) - lsenv->tr_data->sys.cs_base, /* EIP */
            1 /* translation end with exception */
    );
    return true;
#else
    return false;
#endif
}

bool latxs_translate_ud2(IR1_INST *pir1)
{
#ifdef CONFIG_SOFTMMU
    latxs_tr_gen_excp_illegal_op_addr(
            ir1_addr(pir1) - lsenv->tr_data->sys.cs_base, /* EIP */
            1 /* translation end with exception */
    );
    return true;
#else
    return false;
#endif
}

bool latxs_translate_swapgs(IR1_INST *pir1)
{
    if (!lsenv->tr_data->sys.code64) {
        /* TODO: should gen illegal op */
        lsassert(0);
    } else if (lsenv->tr_data->sys.cpl != 0) {
        /* TODO: should gen EXCP0D_GPF */
        lsassert(0);
    } else {
        IR2_OPND gs_base = latxs_ra_alloc_itemp();
        IR2_OPND kernel_gs_base = latxs_ra_alloc_itemp();

        latxs_append_ir2_opnd2i(LISA_LD_D, &gs_base, &latxs_env_ir2_opnd,
                                offsetof(CPUX86State, segs[gs_index].base));
        latxs_append_ir2_opnd2i(LISA_LD_D, &kernel_gs_base,
                                &latxs_env_ir2_opnd,
                                offsetof(CPUX86State, kernelgsbase));
        latxs_append_ir2_opnd2i(LISA_ST_D, &kernel_gs_base,
                                &latxs_env_ir2_opnd,
                                offsetof(CPUX86State, segs[gs_index].base));
        latxs_append_ir2_opnd2i(LISA_ST_D, &gs_base, &latxs_env_ir2_opnd,
                                offsetof(CPUX86State, kernelgsbase));
    }
    return true;
}

bool latxs_translate_clflush(IR1_INST *pir1)
{
    latxs_append_ir2_opndi(LISA_IBAR, 0);
    return true;
}

bool latxs_translate_clflushopt(IR1_INST *pir1)
{
    latxs_append_ir2_opndi(LISA_IBAR, 0);
    return true;
}
