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
#include "latx-intb-sys.h"
#include "latx-config.h"
#include "latx-misc.h"
#include "latx-sys-inst-ptn.h"
#include <string.h>

/*
 * Execute after every ir1_disasm to fix something
 *
 * 1> A little bug of capstone
 *    Under the following situction :
 *    - address size is 2
 *    - modrm is used and
 *      - mod is 0
 *      - rm is 6
 *    The disp should be considered as unsigned 16-bit value.
 *
 * 2. Set mem opnd's default segment
 *
 * 3. opnd size for rep string operation
 *
 */
void latxs_fix_up_ir1(IR1_INST *ir1)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    int i = 0;

    /* The XSAVE helpers share the same state representation for REX.W. */
    switch (ir1_opcode(ir1)) {
    case X86_INS_XSAVE64:
        ir1->info->id = X86_INS_XSAVE;
        break;
    case X86_INS_XSAVEOPT64:
        ir1->info->id = X86_INS_XSAVEOPT;
        break;
    case X86_INS_XRSTOR64:
        ir1->info->id = X86_INS_XRSTOR;
        break;
    default:
        break;
    }

    /* 1. A little bug of capstone */
    if (latxs_ir1_addr_size(ir1) == 2) {
        uint8 modrm = ir1->info->detail->x86.modrm;
        uint8 mod = (modrm >> 6) & 0x3;
        uint8 rm  = (modrm >> 0) & 0x7;
        if (mod == 0 && rm == 6) {
            /* disp should be unsigend */
            int64 disp_old = ir1->info->detail->x86.disp;
            int64 disp_new = disp_old & 0xffff;
            ir1->info->detail->x86.disp = disp_new;
            for (i = 0; i < ir1->info->detail->x86.op_count; ++i) {
                IR1_OPND *opnd = ir1_get_opnd(ir1, i);
                if (ir1_opnd_is_mem(opnd)) {
                    opnd->mem.disp = disp_new;
                    goto fix_up_2;
                }
            }
        }
    }

fix_up_2:
    /*
     * 2. set mem opnd's default segment
     * --------------------------------------------
     * seg |    situction       | who deals with it
     * --------------------------------------------
     *  CS | instruction fetch  | QEMU
     * --------------------------------------------
     *  SS | stack push pop     | translate_pop/push
     *     | base = EBP or ESP  | here !!!
     * --------------------------------------------
     *  DS | All data reference | here !!!
     * --------------------------------------------
     *  ES | strint operation's | translate_ins/outs
     *     | destination        | translate_movs
     * --------------------------------------------
     */
    if (td->sys.addseg && !latxs_ir1_is_lea(ir1)) {
        for (i = 0; i < ir1->info->detail->x86.op_count; ++i) {
            IR1_OPND *opnd = ir1_get_opnd(ir1, i);
            if (ir1_opnd_is_mem(opnd) && !ir1_opnd_has_seg(opnd)) {
                int base = ir1_opnd_base_reg_num(opnd);
                if (base == esp_index || base == ebp_index) {
                    opnd->mem.segment = X86_REG_SS;
                } else {
                    opnd->mem.segment = X86_REG_DS;
                }
            }
        }
    }

    /*
     * 3. opnd size for 'rep movs/outs/ins... string operation'
     *
     * When inst is prefixed with '0x66 0xf3', only the '0xf3' (rep)
     * will be disassembled. But if it is prefixed with '0xf3 0x66',
     * the '0x66' (opnd size) will also be disassembled.
     *
     * This bug only exists in 32-bit mode between 16 and 32.
     *
     * -------------------------------------
     *   opc |      default opnd size
     * -------------------------------------
     *  inst | 8-bits |  16-bits | 32-bits
     * -------------------------------------
     *   ins |  0x6c  |   0x6d   |  0x6d
     *  outs |  0x6e  |   0x6f   |  0x6f
     *  movs |  0xa4  |   0xa5   |  0xa5
     *  cmps |  0xa6  |   0xa7   |  0xa7
     *  stos |  0xaa  |   0xab   |  0xab
     *  lods |  0xac  |   0xad   |  0xad
     *  scas |  0xae  |   0xaf   |  0xaf
     * -------------------------------------
     */
    if (latxs_ir1_is_string_op(ir1)) {
        uint8_t *opbytes = latxs_ir1_inst_opbytes(ir1);
        uint8_t b = opbytes[0];
        int ds = latxs_ir1_data_size(ir1) >> 3;
        IR1_OPND *opnd  = NULL;
        IR1_OPND *opnd2 = NULL;
        switch (opbytes[0]) {
        case 0x6c: case 0x6d: /* ins  */
            opnd = ir1_get_opnd(ir1, 0);
            /*return b & 1 ? (ot == MO_16 ? MO_16 : MO_32) : MO_8;*/
            ds = (b & 1) ? (ds == 2 ? 2 : 4) : 1;
            break;
        case 0x6e: case 0x6f: /* outs */
            opnd = ir1_get_opnd(ir1, 1);
            /*return b & 1 ? (ot == MO_16 ? MO_16 : MO_32) : MO_8;*/
            ds = (b & 1) ? (ds == 2 ? 2 : 4) : 1;
            break;
        case 0xa4: case 0xa5: /* movs */
        case 0xa6: case 0xa7: /* cmps */
        case 0xaa: case 0xab: /* stos */
        case 0xac: case 0xad: /* lods */
        case 0xae: case 0xaf: /* scas */
            opnd  = ir1_get_opnd(ir1, 0);
            opnd2 = ir1_get_opnd(ir1, 1);
            /*return b & 1 ? ot : MO_8;*/
            ds = (b & 1) ? ds : 1;
            break;
        default:
            lsassert(0);
            break;
        }
        if (opnd)  opnd->size  = ds;
        if (opnd2) opnd2->size = ds;
    }

    if (lsenv->tr_data->sys.code64) {
        if (ir1_opcode(ir1) == X86_INS_MOVD) {
            IR1_OPND *opnd0 = ir1_get_opnd(ir1, 0);
            IR1_OPND *opnd1 = ir1_get_opnd(ir1, 1);
            if (ir1->info->bytes[0] == 0xf3 && ir1->info->bytes[1] == 0x48 &&
                ir1->info->bytes[2] == 0x0f && ir1->info->bytes[3] == 0x7e) {
                ir1->info->id = X86_INS_MOVQ;
                IR1_OPND temp = *opnd0;
                *opnd0 = *opnd1;
                *opnd1 = temp;
                opnd0->reg +=  X86_REG_XMM0 - X86_REG_MM0;
                ir1_dump(ir1);
            }
            if ((ir1_opnd_is_gpr(opnd0) && ir1_opnd_size(opnd0) == 64) ||
                (ir1_opnd_is_gpr(opnd1) && ir1_opnd_size(opnd1) == 64)) {
                ir1->info->id = X86_INS_MOVQ;
                ir1_dump(ir1);
                return;
            }
            if ((ir1_opnd_is_mem(opnd0) && ir1_opnd_size(opnd0) == 64) ||
                (ir1_opnd_is_mem(opnd1) && ir1_opnd_size(opnd1) == 64)) {
                ir1->info->id = X86_INS_MOVQ;
                ir1_dump(ir1);
                return;
            }
        }
    }

    if (ir1_opcode(ir1) == X86_INS_BSF || ir1_opcode(ir1) == X86_INS_BSR) {
        unsigned repeat = 0;
        for (unsigned k = 0; k < ir1->info->size; ++k) {
            unsigned byte = ir1->info->bytes[k];
            if (byte == 0xf2 || byte == 0xf3) {
                repeat = byte;
            } else if (byte != 0x66 && byte != 0x67 && byte != 0xf0 &&
                       byte != 0x26 && byte != 0x2e && byte != 0x36 &&
                       byte != 0x3e && byte != 0x64 && byte != 0x65 &&
                       !(td->sys.code64 && byte >= 0x40 && byte <= 0x4f)) {
                break;
            }
        }
        if (repeat == 0xf3) {
            ir1->info->id = ir1_opcode(ir1) == X86_INS_BSF ? X86_INS_TZCNT
                                                           : X86_INS_LZCNT;
            ir1_dump(ir1);
        }
    }
}

int latxs_ir1_data_size(IR1_INST *ir1)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    int prefix_opsize = latxs_ir1_has_prefix_opsize(ir1);

    if (td->sys.code64) {
        /*
         * In 64-bit mode, the default data size is 32-bit.  Select 64-bit
         *  data with rex_w, and 16-bit data with 0x66; rex_w takes precedence
         * over 0x66 if both are present.
         */
        return (ir1->info->detail->x86.rex & 0x8) ? 64
               : prefix_opsize                    ? 16
                                                  : 32;
    }

    if (td->sys.code32 ^ prefix_opsize) {
        /*  code32 && !prefix_opsize  */
        /* !code32 &&  prefix_opsize  */
        return 4 << 3;
    } else {
        /*  code32 &&  prefix_opsize  */
        /* !code32 && !prefix_opsize  */
        return 2 << 3;
    }
}

void latxs_ir1_make_ins_ILLEGAL(IR1_INST *ir1,
        ADDRX addr, int size, int flags)
{
    lsassert(ir1->info != NULL);
    cs_insn *info = ir1->info;

    TRANSLATION_DATA *td = lsenv->tr_data;
    if (td->sys.code32) {
        info->detail->x86.addr_size = 4;
    } else if (td->sys.code64) {
        info->detail->x86.addr_size = 8;
    }
#ifdef CONFIG_SOFTMMU
    else {
        info->detail->x86.addr_size = 2;
    }
#endif

    info->id = X86_INS_INVALID;
    info->address = addr;
    info->mnemonic[0] = 'i';
    info->mnemonic[1] = 'l';
    info->mnemonic[2] = 'l';
    info->mnemonic[3] = 'e';
    info->mnemonic[4] = 'g';
    info->mnemonic[5] = 'a';
    info->mnemonic[6] = 'l';
    info->mnemonic[7] = 0;
    info->size = size;
    info->op_str[0] = 0;
    info->detail->x86.op_count = 0;

    ir1->flags = flags;
    ir1->_eflag_def = 0;
    ir1->_eflag_use = 0;
    ir1->instptn.opc = INSTPTN_OPC_NONE;
    ir1->instptn.next = -1;
}

void latxs_tr_data_reset(TranslationBlock *tb)
{
    /*Support TU :  It should be reset before each translation */
    TRANSLATION_DATA *td = lsenv->tr_data;

    td->in_gen_slow_path = 0;
    td->sys.f_st = 0;
    td->sys.popl_esp_hack = 0;
    td->sys.bp_hit = 0;
    td->inhibit_irq_in_hflags = 0;
    td->recheck_tf  = 0;
    td->ignore_top_update = 0;
    td->in_gen_excp_dynamic_check = 0;
    td->end_with_exception = 0;
    td->dec_icount_inst_id = 0;
    td->cc_pro_dycheck_filter = 0;
#ifdef CONFIG_SOFTMMU
    td->need_save_currtb_for_int = 0;
#endif

    td->curr_top_save = -1;
    td->ignore_rcd_curr_top = 0;
    td->force_curr_top_save = 0;
    td->force_curr_top_save_bak = 0;
#ifdef CONFIG_LATX_OPT_NOFP
    td->mmx_flag = 0;
#endif

    if (option_opt_mmio) {
        td->opt_mmio = check_misc_flags(tb, MF_MMIO_OPT) ? 1 : 0;
        td->opt_mmio_type = tb->opt_mmio_type;
        td->inst_do_opt_mmio = 0; /* set by each inst */
    } else {
        td->opt_mmio = 0;
        td->opt_mmio_type = 0;
        td->inst_do_opt_mmio = 0;
    }
#ifdef LATX_SYS_DEBUG_OPT_MMIO
    if (td->opt_mmio) {
        printf("%s:%d TB %p opt mmio type %d\n", __func__, __LINE__,
                tb, tb->opt_mmio_type);
    }
#endif
    return;

}

void latxs_tr_sys_init(TranslationBlock *tb,
        int max_insns, void *code_highwater, void *host_pc)
{
    if (!tb) {
        return;
    }

    CPUX86State *env = lsenv->cpu_state;
    TRANSLATION_DATA *td = lsenv->tr_data;

    td->curr_tb = tb;
    td->ir1_nr = 0;

    td->mtcc_id = get_mask_from_miscflags(tb, MF_MTCC_ID_MASK, MF_MTCC_ID_SHIFT);

    lsassert(max_insns >= 1);
    int max_nr = latxs_tb_max_insns();
    td->max_insns = max_insns < max_nr ? max_insns : max_nr;
    td->code_highwater = code_highwater;

    uint32_t flags  = tb->flags;
    uint32_t cflags = tb->cflags;

#if defined(CONFIG_TB_FLAGS_NO_EFLAGS) && defined(CONFIG_LATX_PRINT)
    if (flags & (IOPL_MASK | TF_MASK | RF_MASK | VM_MASK | AC_MASK)) {
        printf("%s:%d TB FLAGS_NO_EFLAGS %x\n",
                __func__, __LINE__, flags);
    }
#endif

    td->slow_path_rcd_nr = 0;

    td->sys.pe = (flags >> HF_PE_SHIFT) & 1;
    td->sys.code32 = (flags >> HF_CS32_SHIFT) & 1;
    td->sys.ss32 = (flags >> HF_SS32_SHIFT) & 1;
    td->sys.addseg = (flags >> HF_ADDSEG_SHIFT) & 1;
    td->sys.vm86 = (flags >> VM_SHIFT) & 1;
    td->sys.cpl = (flags >> HF_CPL_SHIFT) & 3;
    td->sys.iopl = (flags >> IOPL_SHIFT) & 3;
    td->sys.tf = (flags >> TF_SHIFT) & 1;
    td->sys.cs_base = tb->cs_base;
    td->sys.mxcsr_on = (flags >> HF_SIGFPE_ON) &1;

    td->sys.mem_index = cpu_mmu_index(env, false);

    td->sys.cpuid_features = env->features[FEAT_1_EDX];
    td->sys.cpuid_ext_features = env->features[FEAT_1_ECX];
    td->sys.cpuid_ext2_features = env->features[FEAT_8000_0001_EDX];
    td->sys.cpuid_ext3_features = env->features[FEAT_8000_0001_ECX];
    td->sys.cpuid_7_0_ebx_features = env->features[FEAT_7_0_EBX];
    td->sys.cpuid_xsave_features = env->features[FEAT_XSAVE];

    td->sys.lma = (flags >> HF_LMA_SHIFT) & 1;
    td->sys.code64 = (flags >> HF_CS64_SHIFT) & 1;

    td->sys.flags = flags;
    td->sys.cflags = cflags;

    td->sys.pc = get_eip_cpu_state(tb);
    td->curr_page = td->sys.pc & TARGET_PAGE_MASK;

    if (option_dump && qemu_log_in_addr_range(tb->pc)) {
        fprintf(stderr, "SYS.FLAGS  = %x\n", td->sys.flags);
        fprintf(stderr, "SYS.PE     = %d\n", td->sys.pe);
        fprintf(stderr, "SYS.code32 = %d\n", td->sys.code32);
        fprintf(stderr, "SYS.lma    = %d\n", td->sys.lma);
        fprintf(stderr, "SYS.code64 = %d\n", td->sys.code64);
        fprintf(stderr, "SYS.ss32   = %d\n", td->sys.ss32);
        fprintf(stderr, "SYS.addseg = %d\n", td->sys.addseg);
        fprintf(stderr, "SYS.vm86   = %d\n", td->sys.vm86);
        fprintf(stderr, "SYS.cpl    = %d\n", td->sys.cpl);
        fprintf(stderr, "SYS.iopl   = %d\n", td->sys.iopl);
        fprintf(stderr, "SYS.tf     = %d\n", td->sys.tf);
        fprintf(stderr, "SYS.CSBase = %p\n", (void *)(ADDR)td->sys.cs_base);
        fprintf(stderr, "SYS.MMUIdx = %d\n", td->sys.mem_index);
    }

    latxs_tr_data_reset(tb);
    td->need_eob    = 1; /* always generate eob */
    td->ignore_eip_update = 0;

    td->sys_eob_can_link = 0;
    td->sys_eob_is_indir = 0;
    td->sys_eob_use_fsht = 0;

    /* option can link */
    td->opt_can_link       = option_tb_link && !(td->sys.cflags & CF_NO_GOTO_TB);
    td->opt_can_indir_link = intb_link_enable() && !(td->sys.cflags & CF_NO_GOTO_PTR);
    /* Rare branches in an interrupt shadow return to the dispatcher. The
     * usual non-branch shadow instruction keeps its existing checked exit. */
    if (td->sys.flags & HF_INHIBIT_IRQ_MASK) {
        td->opt_can_link = 0;
        td->opt_can_indir_link = 0;
    }
    /* can link (not for indirect branch) */
    td->can_link       = td->opt_can_link;
    td->can_indir_link = td->opt_can_indir_link;
    if (td->sys.tf) {
        td->can_link = 0;
        td->can_indir_link = 0;
    }


    td->host_addr[0] = host_pc;
    td->host_addr[1] = NULL;
    td->deadlock_avoidance = 0;
    td->disable_cross_page_tb = 0;
}
