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
#include "latx-sys-hamt-dmw.h"
#if defined(CONFIG_SOFTMMU) && defined(CONFIG_LATX_HACK_MEM)
#include "exec/cpu_ldst.h"
#include "exec/ram_addr.h"
#include "hamt-ptw.h"
#include "tcg/helper-tcg.h"
#endif

static int check_icount_mode(void)
{
    TranslationBlock *tb = lsenv->tr_data->curr_tb;
    uint32_t cflags = tb->cflags;
    if (cflags & CF_USE_ICOUNT) {
        return 1; /* need to be EOB in sys-eob.c */
    }

    if (option_split_strins) {
        return 1; /* need to be EOB in sys-eob.c */
    } else {
        return 0;
    }
}

#if defined(CONFIG_SOFTMMU) && defined(CONFIG_LATX_HACK_MEM)
static uintptr_t rep_ram_ptr(CPUX86State *env, int mmu_idx,
                            vaddr addr, MMUAccessType access)
{
    CPUTLBEntry *entry = tlb_entry(env, mmu_idx, addr);

    if (tlb_read_idx(entry, access) != (addr & TARGET_PAGE_MASK)) {
        return 0;
    }
    uintptr_t host = addr + entry->addend;
    if ((host & HACK_RAMBASE_MASK) != HACK_RAMBASE) {
        return 0;
    }
    ram_addr_t offset = host & ~HACK_RAMBASE;
    bool regular_ram = latx_hack_gpa_in_ram(offset) && !paddr_in_vram(offset);

    if (!regular_ram ||
        (access == MMU_DATA_STORE && hamt_ptwdata_get_page(offset))) {
        return 0;
    }
    return host;
}

/* Fill only the next byte the instruction would attempt. A failed walk
 * does not deliver an exception here, and special mappings stay slow. */
static bool rep_ram_fill_next(CPUState *cpu, CPUX86State *env, int mmu_idx,
                              vaddr addr, MMUAccessType access)
{
    CPUTLBEntry *entry = tlb_entry(env, mmu_idx, addr);
    if (!tlb_hit_page(tlb_read_idx(entry, access), addr & TARGET_PAGE_MASK)) {
        int saved_error = env->error_code;
        bool filled = x86_cpu_tlb_fill(cpu, addr, 1, access, mmu_idx, true, 0);
        env->error_code = saved_error;
        if (!filled) {
            return false;
        }
    }
    return rep_ram_ptr(env, mmu_idx, addr, access) != 0;
}

static void latxs_rep_ram(CPUX86State *env, int copying)
{
    CPUState *cpu = env_cpu(env);
    CPUTLB *tlb = env_tlb(env);
    int mmu_idx = cpu_mmu_index(env, false);
    uint64_t count = env->regs[R_ECX];
    vaddr dst = env->regs[R_EDI];
    vaddr src = env->regs[R_ESI];
    uint64_t advanced = 0;

    if ((env->hflags & (HF_CPL_MASK | HF_CS64_MASK)) != HF_CS64_MASK ||
        (env->hflags & (HF_GUEST_MASK | HF_SMM_MASK)) ||
        (env->cr[0] & CR0_CD_MASK) ||
        !(env->msr_ia32_misc_enable & UINT64_C(1)) || /* Fast-Strings Enable */
        env->df != 1 || (env->eflags & (TF_MASK | RF_MASK)) ||
        cpu_plugin_mem_cbs_enabled(cpu)) {
        return;
    }
    /* Each chunk is permission-checked and stays within its RAM pages.
     * Keep the TLB lock across it so a code-protection sweep cannot finish
     * while this vCPU is still writing through the old mapping. */
    while (count >= 256 && advanced < 16 * TARGET_PAGE_SIZE) {
        size_t chunk = MIN(count, TARGET_PAGE_SIZE - (dst & ~TARGET_PAGE_MASK));
        if (copying) {
            chunk = MIN(chunk, TARGET_PAGE_SIZE - (src & ~TARGET_PAGE_MASK));
        }
        /* Preserve the MOVS source-before-destination access order. */
        if ((copying && !rep_ram_fill_next(cpu, env, mmu_idx, src, MMU_DATA_LOAD)) ||
            !rep_ram_fill_next(cpu, env, mmu_idx, dst, MMU_DATA_STORE)) {
            break;
        }
        qemu_spin_lock(&tlb->c.lock);
        uintptr_t dst_host = rep_ram_ptr(env, mmu_idx, dst, MMU_DATA_STORE);
        uintptr_t src_host = copying ?
            rep_ram_ptr(env, mmu_idx, src, MMU_DATA_LOAD) : 0;
        bool overlap = copying &&
                       (dst_host - src_host < chunk || src_host - dst_host < chunk);
        if (!dst_host || (copying && !src_host) || overlap) {
            qemu_spin_unlock(&tlb->c.lock);
            break;
        }
        if (copying) {
            memcpy((void *)dst_host, (void *)src_host, chunk);
            src += chunk;
        } else {
            memset((void *)dst_host, (uint8_t)env->regs[R_EAX], chunk);
        }
        qemu_spin_unlock(&tlb->c.lock);
        dst += chunk;
        count -= chunk;
        advanced += chunk;
    }
    env->regs[R_ECX] = count;
    env->regs[R_EDI] = dst;
    env->regs[R_ESI] = src;
}

static void latxs_gen_rep_ram(IR1_INST *pir1, bool copying)
{
    const TranslationBlock *tb = lsenv->tr_data->curr_tb;
    if ((tb->flags & (HF_CPL_MASK | HF_CS64_MASK)) != HF_CS64_MASK ||
        (tb->cflags & (CF_SINGLE_STEP | CF_MEMI_ONLY)) ||
        !latxs_ir1_has_prefix_rep(pir1) || latxs_ir1_has_prefix_seg(pir1) ||
        latxs_ir1_addr_size(pir1) != 8 ||
        ir1_opnd_size(ir1_get_opnd(pir1, 0)) != 8 ||
        !can_do_hamt(lsenv->cpu_state) || check_icount_mode() ||
        option_sys_aot || option_aot_save || jit_tu_enabled()) {
        return;
    }

    IR2_OPND count = latxs_ra_alloc_gpr(ecx_index);
    IR2_OPND limit = latxs_ra_alloc_itemp();
    IR2_OPND slow = la_new_label();
    la_imm(&limit, 256);
    la_out3(LISA_BLTU, &count, &limit, &slow);
    latxs_ra_free_temp(&limit);
    latxs_tr_gen_call_to_helper2_cfg((ADDR)latxs_rep_ram, copying,
                                    default_helper_cfg, 0);
    la_out1(LISA_LABEL, &slow);
}
#endif

void latxs_sys_string_register_ir1(void)
{
    latxs_register_ir1(X86_INS_INSB);
    latxs_register_ir1(X86_INS_INSD);
    latxs_register_ir1(X86_INS_INSW);

    latxs_register_ir1(X86_INS_OUTSB);
    latxs_register_ir1(X86_INS_OUTSD);
    latxs_register_ir1(X86_INS_OUTSW);

    latxs_register_ir1(X86_INS_MOVSB);
    latxs_register_ir1(X86_INS_MOVSD);
    latxs_register_ir1(X86_INS_MOVSW);
    latxs_register_ir1(X86_INS_MOVSQ);

    latxs_register_ir1(X86_INS_SCASB);
    latxs_register_ir1(X86_INS_SCASD);
    latxs_register_ir1(X86_INS_SCASW);
    latxs_register_ir1(X86_INS_SCASQ);

    latxs_register_ir1(X86_INS_CMPSB);
    latxs_register_ir1(X86_INS_CMPSD);
    latxs_register_ir1(X86_INS_CMPSW);
    latxs_register_ir1(X86_INS_CMPSQ);

    latxs_register_ir1(X86_INS_LODSB);
    latxs_register_ir1(X86_INS_LODSD);
    latxs_register_ir1(X86_INS_LODSW);
    latxs_register_ir1(X86_INS_LODSQ);

    latxs_register_ir1(X86_INS_STOSB);
    latxs_register_ir1(X86_INS_STOSD);
    latxs_register_ir1(X86_INS_STOSW);
    latxs_register_ir1(X86_INS_STOSQ);
}

static void latxs_load_string_step_to_ir2(IR2_OPND *setp,
        IR1_INST *pir1, int opidx)
{
    IR2_OPND df_opnd = latxs_ra_alloc_itemp();
    /*
     * env->df is +1 when DF is clear and -1 when DF is set. Loading it
     * unsigned and masking bit 10 converts -1 to 0x400 while +1 becomes 0.
     */
    latxs_append_ir2_opnd2i(LISA_LD_WU, &df_opnd, &latxs_env_ir2_opnd,
            lsenv_offset_of_df(lsenv));
    latxs_append_ir2_opnd2i(LISA_ANDI, &df_opnd,
            &df_opnd, 0x400);

    lsassert(!(opidx >> 1)); /* opidx == 0 || opidx == 1 */
    IR1_OPND *opnd = ir1_get_opnd(pir1, opidx);
    int opnd_size  = ir1_opnd_size(opnd);

    int bytes = opnd_size >> 3;
    int bits  = 0;
    switch (opnd_size) {
    case 8:
        bits = 9;
        break;
    case 16:
        bits = 8;
        break;
    case 32:
        bits = 7;
        break;
    case 64:
        bits = 6;
        break;
    default:
        lsassert(0);
        break;
    }
    lsassert(bits != 0);

    IR2_OPND tmp_step = latxs_ra_alloc_itemp();
    latxs_append_ir2_opnd2i(LISA_SRAI_W, &tmp_step, &df_opnd, bits);
    latxs_append_ir2_opnd2i(LISA_ADDI_W, setp, &tmp_step, 0 - bytes);

    latxs_ra_free_temp(&df_opnd);
    latxs_ra_free_temp(&tmp_step);
}

static void latxs_tr_gen_string_loop_start(IR1_INST *pir1,
        IR2_OPND *loop, IR2_OPND *exit,
        int load_step, IR2_OPND *step)
{
    int is_icount = check_icount_mode();

    /* 1. exit when initial count is zero */
    IR2_OPND label_exit = latxs_ir2_opnd_new_label();
    if (latxs_ir1_has_prefix_rep(pir1) || latxs_ir1_has_prefix_repne(pir1)) {
        IR2_OPND ecx_opnd = latxs_ra_alloc_gpr(ecx_index);
        int addr_size = latxs_ir1_addr_size(pir1);
        if (addr_size == 8) {
            /* RCX */
            latxs_append_ir2_opnd3(LISA_BEQ,
                    &ecx_opnd, &latxs_zero_ir2_opnd, &label_exit);
        } else if (addr_size == 4) {
            /* ECX */
            latxs_append_ir2_opnd3(LISA_BEQ,
                    &ecx_opnd, &latxs_zero_ir2_opnd, &label_exit);
        } else {
            /* CX */
            IR2_OPND cond = latxs_ra_alloc_itemp();
            latxs_append_ir2_opnd2_(lisa_mov16z, &cond, &ecx_opnd);
            latxs_append_ir2_opnd3(LISA_BEQ,
                    &cond, &latxs_zero_ir2_opnd, &label_exit);
            latxs_ra_free_temp(&cond);
        }
    }
    *exit = label_exit;

    /* 2. preparations outside the loop */
    if (load_step) {
        IR2_OPND setp_opnd = latxs_ra_alloc_itemp();
        latxs_load_string_step_to_ir2(&setp_opnd, pir1, 0);
        *step = setp_opnd;
    }

    /* 3. loop starts */
    IR2_OPND label_loop = latxs_ir2_opnd_new_label();
    if (!is_icount) {
        latxs_append_ir2_opnd1(LISA_LABEL, &label_loop);
    }
    *loop = label_loop;
}

/*
 * mode = 1 : next eip = curr ir1
 * mode = 0 : next eip = next ir1
 */
static void tr_load_eip_icount(IR1_INST *pir1, int mode, int is64)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    IR2_OPND eip_opnd = latxs_ra_alloc_itemp();
    ADDRX next_eip = mode ? ir1_addr(pir1) : ir1_addr_next(pir1);
    ADDRX next_eip_offset = mode ? td->pc_curr - td->pc_save : td->pc_next - td->pc_save;

    if(option_tb_pcrel){
        lsassertm(0, "may have error here, haven't tested\n");
        latxs_tr_gen_add_st_curr_eip(&eip_opnd, next_eip_offset, next_eip);
    } else if (option_page_pcrel) {
        lsassertm(0, "may have error here, haven't tested\n");
        latxs_tr_gen_add_st_curr_eip(&eip_opnd, next_eip_offset, next_eip);
    } 
    if (is64) {
        latxs_load_imm64_to_ir2(&eip_opnd, next_eip);
        latxs_append_ir2_opnd2i(LISA_ST_D, &eip_opnd,
                &latxs_env_ir2_opnd, lsenv_offset_of_eip(lsenv));
    } else {
        if (option_aot_save) {
            aot_load_guest_addr_to_ir2(&eip_opnd, next_eip, LOAD_GUEST_ADDR);
        } else {
            latxs_load_imm32_to_ir2(&eip_opnd, next_eip, EXMode_Z);
        }
        latxs_append_ir2_opnd2i(LISA_ST_W, &eip_opnd,
                &latxs_env_ir2_opnd, lsenv_offset_of_eip(lsenv));
    }
    latxs_ra_free_temp(&eip_opnd);
}

/* string instruction is sys eob in icount mode */
static void latxs_tr_gen_icount_end(IR1_INST *pir1, IR2_OPND label_loop)
{
    if (!check_icount_mode()) {
        return;
    }

    TRANSLATION_DATA *td = lsenv->tr_data;
    TranslationBlock *tb = td->curr_tb;

    if (latxs_ir1_has_prefix_rep(pir1) ||
        latxs_ir1_has_prefix_repe(pir1) ||
        latxs_ir1_has_prefix_repne(pir1))
    {
        /* exit loop */
        if (option_lazypc) {
            tb->next_pc[1] = ir1_addr_next(pir1);
        } else {
            tr_load_eip_icount(pir1, 0, 1);
        }
        latxs_tr_gen_eob();
        if (td->can_link) {
            latxs_tr_gen_exit_tb_j_tb_link(tb, 1);
            IR2_OPND tbptr = latxs_ra_alloc_dbt_arg1(); /* a6($10) */
            latxs_tr_gen_exit_tb_load_tb_addr(&tbptr, (ADDR)tb);
            latxs_tr_gen_exit_tb_j_context_switch(&tbptr, 1, 1);
        } else {
            latxs_tr_gen_exit_tb_j_context_switch(NULL, 0, 0);
        }

        /* continue loop */
        latxs_append_ir2_opnd1(LISA_LABEL, &label_loop);
        if (option_lazypc) {
            tb->next_pc[0] = ir1_addr(pir1);
        } else {
            tr_load_eip_icount(pir1, 1, 1);
        }
        latxs_tr_gen_eob();
        if (td->can_link) {
            latxs_tr_gen_exit_tb_j_tb_link(tb, 0);
            IR2_OPND tbptr = latxs_ra_alloc_dbt_arg1(); /* a6($10) */
            latxs_tr_gen_exit_tb_load_tb_addr(&tbptr, (ADDR)tb);
            latxs_tr_gen_exit_tb_j_context_switch(&tbptr, 1, 0);
        } else {
            latxs_tr_gen_exit_tb_j_context_switch(NULL, 0, 0);
        }
    }
}

static void latxs_tr_gen_string_loop_end(IR1_INST *pir1,
        IR2_OPND label_loop, IR2_OPND label_exit,
        IR2_OPND *result)
{
    int is_icount = check_icount_mode();

    IR2_OPND *zero = &latxs_zero_ir2_opnd;

    /* 1. loop ends when .. */
    if (!result) {
        /* 1.1 ends when ecx == 0*/
        if (latxs_ir1_has_prefix_rep(pir1) || latxs_ir1_has_prefix_repne(pir1)) {
            IR2_OPND ecx_opnd = latxs_ra_alloc_gpr(ecx_index);
            int addr_size = latxs_ir1_addr_size(pir1);
            if (addr_size == 8) {
                /* RCX */
                latxs_append_ir2_opnd2i(LISA_ADDI_D, &ecx_opnd, &ecx_opnd, -1);
                if (is_icount) {
                    latxs_append_ir2_opnd1(LISA_B, &label_loop);
                } else {
                    latxs_append_ir2_opnd3(LISA_BNE, &ecx_opnd, zero, &label_loop);
                }
            } else if (addr_size == 4) {
                /* ECX */
                latxs_append_ir2_opnd2i(LISA_ADDI_W, &ecx_opnd, &ecx_opnd, -1);
                latxs_append_ir2_opnd2_(lisa_mov32z, &ecx_opnd, &ecx_opnd);
                if (is_icount) {
                    latxs_append_ir2_opnd1(LISA_B, &label_loop);
                } else {
                    latxs_append_ir2_opnd3(LISA_BNE, &ecx_opnd, zero, &label_loop);
                }
            } else {
                /* CX */
                IR2_OPND cx_temp = latxs_ra_alloc_itemp();
                latxs_append_ir2_opnd2i(LISA_ADDI_W, &cx_temp, &ecx_opnd, -1);
                latxs_store_ir2_to_ir1_gpr(&cx_temp, &cx_ir1_opnd);
                latxs_append_ir2_opnd2_(lisa_mov16z, &cx_temp, &cx_temp);
                if (is_icount) {
                    latxs_append_ir2_opnd1(LISA_B, &label_loop);
                } else {
                    latxs_append_ir2_opnd3(LISA_BNE, &cx_temp, zero, &label_loop);
                }
                latxs_ra_free_temp(&cx_temp);
            }
        }
    } else if (latxs_ir1_has_prefix_repe(pir1) ||
               latxs_ir1_has_prefix_repne(pir1)) {
        /*
         * 1.2 loop ends when one of the following condition is true
         *     1> result != 0 (repe)
         *     2> result == 0 (repne)
         *     3>    ecx == 0 (always)
         */
        IR2_OPND condition  = latxs_ra_alloc_itemp(); /* <3>       */

        /*     3>    ecx == 0            */
        IR2_OPND ecx_opnd = latxs_ra_alloc_gpr(ecx_index);
        int addr_size = latxs_ir1_addr_size(pir1);
        if (addr_size == 8) {
                /* RCX */
            latxs_append_ir2_opnd2i(LISA_ADDI_D, &ecx_opnd, &ecx_opnd, -1);
            latxs_append_ir2_opnd2i(LISA_SLTUI,  &condition, &ecx_opnd, 1);
        } else if (addr_size == 4) {
            /* ECX */
            latxs_append_ir2_opnd2i(LISA_ADDI_W, &ecx_opnd, &ecx_opnd, -1);
            latxs_append_ir2_opnd2_(lisa_mov32z, &ecx_opnd, &ecx_opnd);
            latxs_append_ir2_opnd2i(LISA_SLTUI,  &condition, &ecx_opnd, 1);
        } else {
            /* CX */
            IR2_OPND cx_temp = latxs_ra_alloc_itemp();
            latxs_append_ir2_opnd2_(lisa_mov16z, &cx_temp, &ecx_opnd);
            latxs_append_ir2_opnd2i(LISA_ADDI_D, &cx_temp, &cx_temp, -1);
            latxs_store_ir2_to_ir1_gpr(&cx_temp, &cx_ir1_opnd);
            latxs_append_ir2_opnd2i(LISA_SLTUI,  &condition, &cx_temp, 1);
            latxs_ra_free_temp(&cx_temp);
        }

        IR2_OPND condition2 = latxs_ra_alloc_itemp(); /* <1> & <2> */

        /*     1> result != 0 (repe)     */
        /*     2> result == 0 (repne)    */
        if (latxs_ir1_has_prefix_repe(pir1)) {
            /* set 1 when 0 < result, i.e., result!=0 */
            latxs_append_ir2_opnd3(LISA_SLTU,
                    &condition2, &latxs_zero_ir2_opnd, result);
        }
        if (latxs_ir1_has_prefix_repne(pir1)) {
            /* set 1 when result < 1, i.e., result==0 */
            latxs_append_ir2_opnd2i(LISA_SLTUI,
                    &condition2, result, 1);
        }

        /* 4.3 when two conditions are not satisfied, the loop continues*/
        if (is_icount) {
            /* when <1> <2> not satisfied, exit loop now */
            IR2_OPND label_exit_icount = latxs_ir2_opnd_new_label();
            latxs_append_ir2_opnd3(LISA_BNE, &condition2, zero, &label_exit_icount);
            /* no matter <3>, continue once more loop */
            latxs_append_ir2_opnd1(LISA_B, &label_loop);
        latxs_append_ir2_opnd1(LISA_LABEL, &label_exit_icount);
        } else {
            latxs_append_ir2_opnd3(LISA_OR, &condition, &condition, &condition2);
            latxs_append_ir2_opnd3(LISA_BEQ, &condition, zero, &label_loop);
        }

        latxs_ra_free_temp(&condition);
        latxs_ra_free_temp(&condition2);
    }

    /* 2. exit label */
    if (!result) {
        latxs_append_ir2_opnd1(LISA_LABEL, &label_exit);
    }
}

bool latxs_translate_ins(IR1_INST *pir1)
{
#ifdef CONFIG_SOFTMMU
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0); /* destination : MEM(ES:(E)DI) */
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1); /* I/O port : DX */

    /*return b & 1 ? (ot == MO_16 ? MO_16 : MO_32) : MO_8;*/
    uint8_t *b = latxs_ir1_inst_opbytes(pir1);
    int ds = latxs_ir1_data_size(pir1);
    int data_size = (b[0] & 1) ? (ds == 16 ? 16 : 32) : 8;

    /* Always check IO before any IO operation */
    latxs_tr_gen_io_check(pir1, opnd1, data_size);

    /* 1. loop starts */
    IR2_OPND label_loop = latxs_ir2_opnd_new_inv();
    IR2_OPND label_exit = latxs_ir2_opnd_new_inv();
    latxs_tr_gen_string_loop_start(pir1, &label_loop, &label_exit, 0, NULL);

    latxs_tr_gen_io_start();

    int addr_size = latxs_ir1_addr_size(pir1);

    /* 2. I/O read from I/O port DX : helper_inb/w/l */
    /* 3. write to MEM(ES:(E)DI)    : softmmu helper */
    /* 4. adjust EDI                : according to opnd size */

    /*
     * Note: we must do this dummy write first to be restartable in
     * case of page fault.
     */
    latxs_store_ir2_to_ir1_mem(&latxs_zero_ir2_opnd, opnd0, addr_size);
    /* 2.0 save native context */
    latxs_tr_gen_call_to_helper_prologue_cfg(default_helper_cfg);
    /* 2.1 prepare parameter for IO read helper */
    /*    > arg0 : CPUX86State *env */
    latxs_append_ir2_opnd2_(lisa_mov, &latxs_arg0_ir2_opnd,
                                      &latxs_env_ir2_opnd);
    if (hamt_enabled()) {
        latxs_tr_dmw_out_fix_mem(&latxs_arg0_ir2_opnd);
    }
    /*    > arg1 ; uint32_t    port */
    IR2_OPND port_reg = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd1));
    latxs_append_ir2_opnd2_(lisa_mov16z, &latxs_arg1_ir2_opnd, &port_reg);
    /* 2.2 call the helper_inb/inw/inl */
    switch (data_size) {
    /* target/i386/misc_helper.c */
    case 8:
        latxs_tr_gen_call_to_helper((ADDR)helper_inb, HELPER_INB);
        break;
    case 16:
        latxs_tr_gen_call_to_helper((ADDR)helper_inw, HELPER_INW);
        break;
    case 32:
        latxs_tr_gen_call_to_helper((ADDR)helper_inl, HELPER_INL);
        break;
    default:
        lsassertm_illop(ir1_addr(pir1), 0,
                "Unsupported X86_INS_INS data size %d.\n", data_size);
        break;
    }
    /* 2.3 resotre the naive context */
    latxs_tr_gen_call_to_helper_epilogue_cfg(default_helper_cfg);
    /* 2.4 get the read IO value at $v0 */
    IR2_OPND io_value_reg = latxs_ra_alloc_itemp();
    latxs_append_ir2_opnd2_(lisa_mov, &io_value_reg, &latxs_ret0_ir2_opnd);

    /* 3. write to mem */
    latxs_store_ir2_to_ir1_mem(&io_value_reg, opnd0, addr_size);
    latxs_ra_free_temp(&io_value_reg);

    latxs_tr_gen_io_end();

    /* 4. adjust (E)DI */
    IR2_OPND setp = latxs_ra_alloc_itemp();
    latxs_load_string_step_to_ir2(&setp, pir1, 0);
    if (addr_size == 8) {
        IR2_OPND edi = latxs_ra_alloc_gpr(edi_index);
        latxs_append_ir2_opnd3(LISA_SUB_D, &edi, &edi, &setp);
    } else if (addr_size == 4) {
        /* adjust EDI */
        IR2_OPND edi = latxs_ra_alloc_gpr(edi_index);
        latxs_append_ir2_opnd3(LISA_SUB_W, &edi, &edi, &setp);
        latxs_append_ir2_opnd1i(LISA_LU32I_D, &edi, 0);
    } else {
        /* adjust DI */
        IR2_OPND tmp = latxs_ra_alloc_itemp();
        IR2_OPND edi = latxs_ra_alloc_gpr(edi_index);
        latxs_append_ir2_opnd3(LISA_SUB_W, &tmp, &edi, &setp);
        latxs_store_ir2_to_ir1_gpr(&tmp, &di_ir1_opnd);
        latxs_ra_free_temp(&tmp);
    }

    /* 5. breakpoint IO */
    latxs_tr_gen_io_bpt(pir1, opnd1, data_size >> 3);

    /* 6. loop ends */
    latxs_tr_gen_string_loop_end(pir1, label_loop, label_exit, NULL);

    /* 7. icount ends */
    latxs_tr_gen_icount_end(pir1, label_loop);

    latxs_ra_free_temp(&setp);

    return true;
#else
    lsassertm(0, "ins in user mode.");
    return false;
#endif
}

bool latxs_translate_outs(IR1_INST *pir1)
{
#ifdef CONFIG_SOFTMMU
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0); /* I/O port : DX  */
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1); /* src data : MEM(ES:(E/R)SI) */

    /*return b & 1 ? (ot == MO_16 ? MO_16 : MO_32) : MO_8;*/
    uint8_t *b = latxs_ir1_inst_opbytes(pir1);
    int ds = latxs_ir1_data_size(pir1);
    int data_size = (b[0] & 1) ? (ds == 16 ? 16 : 32) : 8;

    /* Always check IO before any IO operation */
    latxs_tr_gen_io_check(pir1, opnd0, data_size);

    /* 1. loop starts */
    IR2_OPND label_loop = latxs_ir2_opnd_new_inv();
    IR2_OPND label_exit = latxs_ir2_opnd_new_inv();
    latxs_tr_gen_string_loop_start(pir1, &label_loop, &label_exit, 0, NULL);

    latxs_tr_gen_io_start();

    /* 2. read from MEM(ES:(E)DI)  : softmmu helper  */
    /* 3. I/O write to I/O port DX : helper_outb/w/l */
    /* 4. adjust EDI               : according to opnd size */

    /* 2. load value from mem */
    IR2_OPND io_value_reg = latxs_ra_alloc_itemp();
    latxs_load_ir1_to_ir2(&io_value_reg, opnd1, EXMode_N);

    /* 3.0 save native context */
    latxs_tr_gen_call_to_helper_prologue_cfg(default_helper_cfg);
    /* 3.1 prepare parameter for IO read helper */
    /*    > arg2 : uint32_t    data */
    latxs_append_ir2_opnd2_(lisa_mov, &latxs_arg2_ir2_opnd,
                                      &io_value_reg);
    /*    > arg0 : CPUX86State *env */
    latxs_append_ir2_opnd2_(lisa_mov, &latxs_arg0_ir2_opnd,
                                      &latxs_env_ir2_opnd);
    if (hamt_enabled()) {
        latxs_tr_dmw_out_fix_mem(&latxs_arg0_ir2_opnd);
    }
    /*    > arg1 ; uint32_t    port */
    IR2_OPND port_reg = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(opnd0));
    latxs_append_ir2_opnd2_(lisa_mov16z, &latxs_arg1_ir2_opnd, &port_reg);
    /* 3.2 call the helper_outb/inw/inl */
    switch (data_size) {
    /* target/i386/misc_helper.c */
    case 8:
        latxs_tr_gen_call_to_helper((ADDR)helper_outb, HELPER_OUTB);
        break;
    case 16:
        latxs_tr_gen_call_to_helper((ADDR)helper_outw, HELPER_OUTW);
        break;
    case 32:
        latxs_tr_gen_call_to_helper((ADDR)helper_outl, HELPER_OUTL);
        break;
    default:
        lsassertm_illop(ir1_addr(pir1), 0,
                "Unsupported X86_INS_OUTS data size %d.\n", data_size);
        break;
    }
    /* 3.3 resotre the naive context */
    latxs_tr_gen_call_to_helper_epilogue_cfg(default_helper_cfg);

    latxs_tr_gen_io_end();

    /* 4. adjust (E)SI */
    IR2_OPND setp = latxs_ra_alloc_itemp();
    latxs_load_string_step_to_ir2(&setp, pir1, 1);
    int addr_size = latxs_ir1_addr_size(pir1);
    if (addr_size == 8) {
        IR2_OPND esi = latxs_ra_alloc_gpr(esi_index);
        latxs_append_ir2_opnd3(LISA_SUB_D, &esi, &esi, &setp);
    } else if (addr_size == 4) {
        /* adjust ESI */
        IR2_OPND esi = latxs_ra_alloc_gpr(esi_index);
        latxs_append_ir2_opnd3(LISA_SUB_W, &esi, &esi, &setp);
        latxs_append_ir2_opnd1i(LISA_LU32I_D, &esi, 0);
    } else {
        /* adjust SI */
        IR2_OPND tmp = latxs_ra_alloc_itemp();
        IR2_OPND esi = latxs_ra_alloc_gpr(esi_index);
        latxs_append_ir2_opnd3(LISA_SUB_W, &tmp, &esi, &setp);
        latxs_store_ir2_to_ir1_gpr(&tmp, &si_ir1_opnd);
        latxs_ra_free_temp(&tmp);
    }

    /* 5. breakpoint IO */
    latxs_tr_gen_io_bpt(pir1, opnd1, data_size >> 3);

    /* 6. loop ends */
    latxs_tr_gen_string_loop_end(pir1, label_loop, label_exit, NULL);

    /* 7. icount ends */
    latxs_tr_gen_icount_end(pir1, label_loop);

    latxs_ra_free_temp(&setp);

    return true;
#else
    lsassertm(0, "outs in user mode.");
    return false;
#endif
}

bool latxs_translate_movs(IR1_INST *pir1)
{
#if defined(CONFIG_SOFTMMU) && defined(CONFIG_LATX_HACK_MEM)
    latxs_gen_rep_ram(pir1, true);
#endif
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0); /* dest: MEM(ES:(E/R)DI) */
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1); /* src : MEM(DS:(E/R)SI) */

    int addr_size = latxs_ir1_addr_size(pir1);

    /* 1. loop starts */
    IR2_OPND label_loop = latxs_ir2_opnd_new_inv();
    IR2_OPND label_exit = latxs_ir2_opnd_new_inv();
    IR2_OPND setp = latxs_ir2_opnd_new_inv();
    latxs_tr_gen_string_loop_start(pir1,
            &label_loop, &label_exit, 1, &setp);

    /* 2. load memory value at ESI, and store into memory at EDI */
    IR2_OPND src_value = latxs_ra_alloc_itemp();

    latxs_load_ir1_to_ir2(&src_value, opnd1, EXMode_S);
    latxs_store_ir2_to_ir1_mem(&src_value, opnd0, addr_size);

    /* 3. adjust (E)SI and (E)DI */
    if (addr_size == 8) {
        /* RSI and RDI */
        IR2_OPND esi = latxs_ra_alloc_gpr(esi_index);
        IR2_OPND edi = latxs_ra_alloc_gpr(edi_index);
        latxs_append_ir2_opnd3(LISA_SUB_D, &esi, &esi, &setp);
        latxs_append_ir2_opnd3(LISA_SUB_D, &edi, &edi, &setp);
    } else if (addr_size == 4) {
        /* ESI and EDI */
        IR2_OPND esi = latxs_ra_alloc_gpr(esi_index);
        IR2_OPND edi = latxs_ra_alloc_gpr(edi_index);
        latxs_append_ir2_opnd3(LISA_SUB_W, &esi, &esi, &setp);
        latxs_append_ir2_opnd3(LISA_SUB_W, &edi, &edi, &setp);
        latxs_append_ir2_opnd1i(LISA_LU32I_D, &esi, 0);
        latxs_append_ir2_opnd1i(LISA_LU32I_D, &edi, 0);
    } else {
        /* SI and DI */
        IR2_OPND si = latxs_ra_alloc_itemp();
        IR2_OPND di = latxs_ra_alloc_itemp();
        latxs_load_ir1_to_ir2(&si, &si_ir1_opnd, EXMode_Z);
        latxs_load_ir1_to_ir2(&di, &di_ir1_opnd, EXMode_Z);
        latxs_append_ir2_opnd3(LISA_SUB_W, &si, &si, &setp);
        latxs_append_ir2_opnd3(LISA_SUB_W, &di, &di, &setp);
        latxs_store_ir2_to_ir1_gpr(&si, &si_ir1_opnd);
        latxs_store_ir2_to_ir1_gpr(&di, &di_ir1_opnd);
        latxs_ra_free_temp(&si);
        latxs_ra_free_temp(&di);
    }

    /* 4. loop ends */
    latxs_tr_gen_string_loop_end(pir1, label_loop, label_exit, NULL);

    /* 5. icount ends */
    latxs_tr_gen_icount_end(pir1, label_loop);

    latxs_ra_free_temp(&setp);

    return true;
}

bool latxs_translate_scas(IR1_INST *pir1)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0); /* src : AL, AX, EAX     */
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1); /* src : MEM(ES:(E/R)DI) */

    int addr_size = latxs_ir1_addr_size(pir1);
    int is_icount = check_icount_mode();

    /* 0. preparations outside the loop */
    IR2_OPND eax = latxs_ra_alloc_itemp();
    latxs_load_ir1_gpr_to_ir2(&eax, opnd0, EXMode_S);

    /* 1. loop starts */
    IR2_OPND label_loop = latxs_ir2_opnd_new_inv();
    IR2_OPND label_exit = latxs_ir2_opnd_new_inv();
    IR2_OPND setp  = latxs_ir2_opnd_new_inv();
    latxs_tr_gen_string_loop_start(pir1,
            &label_loop, &label_exit, 1, &setp);

    /* 2.1 load memory value at MEM(ES:(E)DI) */
    IR2_OPND _edi_ = latxs_ra_alloc_itemp();
    latxs_load_ir1_mem_to_ir2(&_edi_,
            opnd1, EXMode_S, addr_size);

    /* 2.2 adjust EDI */
    if (addr_size == 8) {
        /* RDI */
        IR2_OPND edi = latxs_ra_alloc_gpr(edi_index);
        latxs_append_ir2_opnd3(LISA_SUB_D, &edi, &edi, &setp);
    } else if (addr_size == 4) {
        /* EDI */
        IR2_OPND edi = latxs_ra_alloc_gpr(edi_index);
        latxs_append_ir2_opnd3(LISA_SUB_W, &edi, &edi, &setp);
        latxs_append_ir2_opnd1i(LISA_LU32I_D, &edi, 0);
    } else {
        /* DI */
        IR2_OPND di = latxs_ra_alloc_itemp();
        latxs_load_ir1_gpr_to_ir2(&di, &di_ir1_opnd, EXMode_Z);
        latxs_append_ir2_opnd3(LISA_SUB_W, &di, &di, &setp);
        latxs_store_ir2_to_ir1_gpr(&di, &di_ir1_opnd);
        latxs_ra_free_temp(&di);
    }

    /* 3. compare */
    IR2_OPND cmp_result = latxs_ra_alloc_itemp();
    if (addr_size == 8) {
        latxs_append_ir2_opnd3(LISA_SUB_D, &cmp_result, &eax, &_edi_);
    } else {
        latxs_append_ir2_opnd3(LISA_SUB_W, &cmp_result, &eax, &_edi_);
    }

    if (is_icount) {
        latxs_generate_eflag_calculation(&cmp_result, &eax, &_edi_, pir1, true);
    }

    /* 4. loop ends */
    latxs_tr_gen_string_loop_end(pir1, label_loop, label_exit, &cmp_result);

    /* 5. calculate eflags */
    if (!is_icount) {
        latxs_generate_eflag_calculation(&cmp_result, &eax, &_edi_, pir1, true);
    }
    latxs_ra_free_temp(&cmp_result);

    /* 6. loop exits */
    latxs_append_ir2_opnd1(LISA_LABEL, &label_exit);

    /* 7. icount ends */
    latxs_tr_gen_icount_end(pir1, label_loop);

    latxs_ra_free_temp(&setp);

    return true;
}

bool latxs_translate_cmps(IR1_INST *pir1)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0); /* src : MEM(ES:(E/R)SI) */
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1); /* src : MEM(ES:(E/R)DI) */

    int addr_size = latxs_ir1_addr_size(pir1);
    int is_icount = check_icount_mode();

    /* 1. loop starts */
    IR2_OPND label_loop = latxs_ir2_opnd_new_inv();
    IR2_OPND label_exit = latxs_ir2_opnd_new_inv();
    IR2_OPND setp  = latxs_ir2_opnd_new_inv();
    latxs_tr_gen_string_loop_start(pir1,
            &label_loop, &label_exit, 1, &setp);

    /* 2.1 load memory value at ESI and EDI */
    IR2_OPND _esi_ = latxs_ra_alloc_itemp();
    IR2_OPND _edi_ = latxs_ra_alloc_itemp();

    latxs_load_ir1_mem_to_ir2(&_esi_, opnd0, EXMode_S, addr_size);
    latxs_load_ir1_mem_to_ir2(&_edi_, opnd1, EXMode_S, addr_size);

    /* 2.2 adjust ESI and EDI */
    if (addr_size == 8) {
        /* RSI and RDI */
        IR2_OPND esi = latxs_ra_alloc_gpr(esi_index);
        IR2_OPND edi = latxs_ra_alloc_gpr(edi_index);
        latxs_append_ir2_opnd3(LISA_SUB_D, &esi, &esi, &setp);
        latxs_append_ir2_opnd3(LISA_SUB_D, &edi, &edi, &setp);
    } else if (addr_size == 4) {
        /* ESI and EDI */
        IR2_OPND esi = latxs_ra_alloc_gpr(esi_index);
        IR2_OPND edi = latxs_ra_alloc_gpr(edi_index);
        latxs_append_ir2_opnd3(LISA_SUB_W, &esi, &esi, &setp);
        latxs_append_ir2_opnd3(LISA_SUB_W, &edi, &edi, &setp);
        latxs_append_ir2_opnd1i(LISA_LU32I_D, &esi, 0);
        latxs_append_ir2_opnd1i(LISA_LU32I_D, &edi, 0);
    } else {
        /* SI and DI */
        IR2_OPND si = latxs_ra_alloc_itemp();
        IR2_OPND di = latxs_ra_alloc_itemp();
        latxs_load_ir1_gpr_to_ir2(&si, &si_ir1_opnd, EXMode_Z);
        latxs_load_ir1_gpr_to_ir2(&di, &di_ir1_opnd, EXMode_Z);
        latxs_append_ir2_opnd3(LISA_SUB_W, &si, &si, &setp);
        latxs_append_ir2_opnd3(LISA_SUB_W, &di, &di, &setp);
        latxs_store_ir2_to_ir1_gpr(&si, &si_ir1_opnd);
        latxs_store_ir2_to_ir1_gpr(&di, &di_ir1_opnd);
        latxs_ra_free_temp(&si);
        latxs_ra_free_temp(&di);
    }

    /* 3. compare */
    IR2_OPND cmp_result = latxs_ra_alloc_itemp();
    if (addr_size == 8) {
        latxs_append_ir2_opnd3(LISA_SUB_D, &cmp_result, &_esi_, &_edi_);
    } else {
        latxs_append_ir2_opnd3(LISA_SUB_W, &cmp_result, &_esi_, &_edi_);
    }

    if (is_icount) {
        latxs_generate_eflag_calculation(&cmp_result,
                &_esi_, &_edi_, pir1, true);
    }

    /* 4. loop ends */
    latxs_tr_gen_string_loop_end(pir1, label_loop, label_exit, &cmp_result);

    /* 5. calculate eflags */
    if (!is_icount) {
        latxs_generate_eflag_calculation(&cmp_result,
                &_esi_, &_edi_, pir1, true);
    }
    latxs_ra_free_temp(&cmp_result);

    /* 6. loop exits */
    latxs_append_ir2_opnd1(LISA_LABEL, &label_exit);

    /* 7. icount ends */
    latxs_tr_gen_icount_end(pir1, label_loop);

    latxs_ra_free_temp(&setp);

    return true;
}

bool latxs_translate_lods(IR1_INST *pir1)
{
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0); /* dest : AL, AX, EAX    */
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1); /* src  : MEM(ES:(E)SI)  */

    int addr_size = latxs_ir1_addr_size(pir1);

    /* 1. loop starts */
    IR2_OPND label_loop = latxs_ir2_opnd_new_inv();
    IR2_OPND label_exit = latxs_ir2_opnd_new_inv();
    IR2_OPND step = latxs_ir2_opnd_new_inv();
    latxs_tr_gen_string_loop_start(pir1,
            &label_loop, &label_exit, 1, &step);

    /* 2. load memory value at MEM(ES:(E)SI) */
    EXMode em = EXMode_S;
    if (ir1_opnd_size(opnd0) < 32) {
        em = EXMode_Z;
    }

    IR2_OPND _esi_ = latxs_ra_alloc_itemp();
    latxs_load_ir1_mem_to_ir2(&_esi_, opnd1, em, addr_size);

    /* 3 adjust (E)SI */
    if (addr_size == 8) {
        /* RSI*/
        IR2_OPND esi = latxs_ra_alloc_gpr(esi_index);
        latxs_append_ir2_opnd3(LISA_SUB_D, &esi, &esi, &step);
    } else if (addr_size == 4) {
        /* ESI */
        IR2_OPND esi = latxs_ra_alloc_gpr(esi_index);
        latxs_append_ir2_opnd3(LISA_SUB_W, &esi, &esi, &step);
        latxs_append_ir2_opnd1i(LISA_LU32I_D, &esi, 0);
    } else {
        /* SI */
        IR2_OPND si = latxs_ra_alloc_itemp();
        latxs_load_ir1_gpr_to_ir2(&si, &si_ir1_opnd, EXMode_Z);
        latxs_append_ir2_opnd3(LISA_SUB_W, &si, &si, &step);
        latxs_store_ir2_to_ir1_gpr(&si, &si_ir1_opnd);
        latxs_ra_free_temp(&si);
    }

    /* if xcx = 0, just end */

    /* 5. store the load value to dest */
    latxs_store_ir2_to_ir1_gpr(&_esi_, opnd0);

    /* 4. loop ends */
    latxs_tr_gen_string_loop_end(pir1, label_loop, label_exit, NULL);

    /* 5. icount ends */
    latxs_tr_gen_icount_end(pir1, label_loop);

    latxs_ra_free_temp(&step);

    return true;
}

bool latxs_translate_stos(IR1_INST *pir1)
{
#if defined(CONFIG_SOFTMMU) && defined(CONFIG_LATX_HACK_MEM)
    latxs_gen_rep_ram(pir1, false);
#endif
    IR1_OPND *opnd0 = ir1_get_opnd(pir1, 0); /* dest: MEM(ES:(E/R)DI) */
    IR1_OPND *opnd1 = ir1_get_opnd(pir1, 1); /* src : AL, AX, EAX     */

    int addr_size = latxs_ir1_addr_size(pir1);

    /* 0. preparations outside the loop */
    IR2_OPND eax_value_opnd = latxs_ra_alloc_itemp();
    latxs_load_ir1_gpr_to_ir2(&eax_value_opnd, opnd1, EXMode_S);

    /* 1. loop starts */
    IR2_OPND label_loop = latxs_ir2_opnd_new_inv();
    IR2_OPND label_exit = latxs_ir2_opnd_new_inv();
    IR2_OPND step = latxs_ir2_opnd_new_inv();
    latxs_tr_gen_string_loop_start(pir1,
            &label_loop, &label_exit, 1, &step);

    /* 2. store EAX into memory at EDI */
    latxs_store_ir2_to_ir1_mem(&eax_value_opnd, opnd0, addr_size);

    /* 3. adjust (E)DI */
    if (addr_size == 8) {
        /* RDI*/
        IR2_OPND edi = latxs_ra_alloc_gpr(edi_index);
        latxs_append_ir2_opnd3(LISA_SUB_D, &edi, &edi, &step);
    } else if (addr_size == 4) {
        /* EDI */
        IR2_OPND edi = latxs_ra_alloc_gpr(edi_index);
        latxs_append_ir2_opnd3(LISA_SUB_W, &edi, &edi, &step);
        latxs_append_ir2_opnd1i(LISA_LU32I_D, &edi, 0);
    } else {
        /* DI */
        IR2_OPND di = latxs_ra_alloc_itemp();
        latxs_load_ir1_gpr_to_ir2(&di, &di_ir1_opnd, EXMode_Z);
        latxs_append_ir2_opnd3(LISA_SUB_D, &di, &di, &step);
        latxs_store_ir2_to_ir1_gpr(&di, &di_ir1_opnd);
        latxs_ra_free_temp(&di);
    }

    /* 4. loop ends */
    latxs_tr_gen_string_loop_end(pir1, label_loop, label_exit, NULL);

    /* 5. icount ends */
    latxs_tr_gen_icount_end(pir1, label_loop);

    latxs_ra_free_temp(&step);

    return true;
}
