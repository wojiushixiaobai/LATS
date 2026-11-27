/*
 * SPDX-FileCopyrightText: 2024-2026 LATS Project Authors
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "common.h"
#include "qemu/osdep.h"
#include "qemu/cacheinfo.h"
#include "cpu.h"
#include "lsenv.h"
#include "reg-alloc.h"
#include "latx-options.h"
#include "latx-ras.h"
#include "translate.h"
#include "latx-static-codes.h"
#include "latx-sys-hamt-dmw.h"
#include "latx-config.h"
#include "latx-sys-smc-check.h"
#include "latx-sys-inst-ptn.h"
#include "hamt-data.h"
#include "tcg/tcg-internal.h"
#include "qemu/cacheflush.h"
#include "sys-aot.h"
#include "latx-sys-push-pop.h"
#include "latxs-imm-cache.h"
#include "latx-shbr.h"
#include "accel/tcg/internal.h"

/* Main Translation Process */

extern __thread TRANSLATION_DATA tr_data_real;
void latxs_tr_tcg_region_offset(uint64_t offset, int mtcc_id)
{
    TRANSLATION_DATA *td = &tr_data_real;
    if (option_region_prologue) {
        td->tcg_region_offset[mtcc_id] = offset;
    } else {
        td->tcg_region_offset[mtcc_id] = 0;
    }
}

uint64_t get_region_offset(int mtcc_id)
{
    TRANSLATION_DATA *td = &tr_data_real;
    return td->tcg_region_offset[mtcc_id];
}

void latxs_tr_sync_region_offset(int mtcc_id)
{
    latxs_tr_tcg_region_offset(
            tcg_region_get_offset(tcg_ctx_mtcc[mtcc_id]), mtcc_id);
}

void latxs_tr_init(TranslationBlock *tb)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    lsassert(td);

    if (tb) {
        int mtcc_id = curr_mtcc(lsenv->cpu_state);
        latxs_tr_sync_region_offset(mtcc_id);
    }

    /* 1. set current tb and ir1 */
    latxs_tr_tb_init(td, tb);

    /* 2. register allocation init */
    latxs_tr_ra_init(td);

    /* 3. reset all registers extension mode */
    /*latxs_tr_em_init(td);*/

    /* 4. reset ir2 array */
    latxs_tr_ir2_array_init(td);

    /* 5. set up lsfpu */
    /*latxs_tr_fpu_init(td, tb);*/

    if (tb) {
        td->x86_ins_nr = 0;
        td->exitreq_label = latxs_ir2_opnd_new_label();
        td->end_with_exception = 0;
#ifdef CONFIG_LATX_PROFILER
        td->tbprofile_rdtime_label = latxs_ir2_opnd_new_label();
#endif
    }

    /* 6. label for optimization */
    if (option_int_opt == IRQ_CHECK_BLOCK_START) {
        td->int_nocheck_label = latxs_ir2_opnd_new_label();
        td->int_docheck_label = latxs_ir2_opnd_new_label();
    }
    td->label_intexit[0] = latxs_ir2_opnd_new_label();
    td->label_intexit[1] = latxs_ir2_opnd_new_label();
}

void latxs_tr_tb_init(TRANSLATION_DATA *td, TranslationBlock *tb)
{
    td->curr_tb = tb;
    td->curr_ir1_inst = NULL;
    if (tb) {
        LATXS_RAS_TP_COLD_OFFSET(tb) = 0;
        LATXS_RAS_TP_COLD_INSNS(tb) = 0;
    }
}

void latxs_tr_ir2_array_init(TRANSLATION_DATA *td)
{
    if (td->ir2_array == NULL) {
        td->ir2_array = mm_calloc(512, sizeof(IR2_INST));
        td->ir2_max_nr = 512;
    }
    td->ir2_cur_nr = 0;
    td->ir2_asm_nr = 0;
    td->gpr_highbits_dynamic_tu_active = false;
    latxs_gpr_hb_state_reset(&td->gpr_highbits_state);
    if (option_aot_gpr_highbits || option_tu_gpr_highbits) {
        memset(td->gpr_highbits_forms, 0,
               sizeof(td->gpr_highbits_forms));
        td->gpr_highbits_tb = NULL;
    }

    td->label_num = 0;
    td->ras_tp_has_call = false;
    td->hras_indirect_call.valid = false;
}

void latxs_tr_fini(void)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    lsassert(td != NULL);

    td->curr_tb = NULL;
    td->curr_ir1_inst = NULL;

    td->ir2_cur_nr = 0;
    td->ir2_asm_nr = 0;

    td->label_num = 0;
}

void latxs_label_dispose(const void *code_buffer)
{
#ifdef CONFIG_LATX_PROFILER
    int profile_addr_diff = 0;
#endif
    TRANSLATION_DATA *td = lsenv->tr_data;
    int label_nr = td->label_num;

    /* 1. record the positions of label */
    int *label_pos = (int *)alloca(label_nr * 4 + 20);
    memset(label_pos , -1, label_nr * 4 + 20);
    int *label_pos_id = (int *)alloca(label_nr * 4 + 20);
    memset(label_pos_id , -1, label_nr * 4 + 20);

    int ir2_num = 0;

    int ir2_id = 0;
    int ir2_nr = td->ir2_cur_nr;
    IR2_INST *ir2_current = NULL;
    IR2_OPCODE ir2_opc;
    for (ir2_id = 0; ir2_id < ir2_nr; ++ir2_id) {

        ir2_current = ir2_get(ir2_id);
        ir2_opc = ir2_opcode(ir2_current);

        if (ir2_opc  == LISA_LABEL) {
            int label_id = latxs_ir2_opnd_label_id(&ir2_current->_opnd[0]);
            lsassertm(label_pos[label_id] == -1,
                      "label %d is in multiple positions\n", label_id);
            label_pos[label_id] = ir2_num;
            label_pos_id[label_id] = ir2_id;
        } else if (ir2_opc == LISA_X86_INST) {
            /* will not be assembled */
#ifdef CONFIG_LATX_PROFILER
        } else if (ir2_opc == LISA_PROFILE) {
            lsassert(label_pos[latxs_ir2_opnd_label_id(&ir2_current->_opnd[1])] != -1);
            if (ir2_current->_opnd[0].val == PROFILE_BEGIN) {
                profile_addr_diff -= label_pos[latxs_ir2_opnd_label_id(&ir2_current->_opnd[1])]<<2;
            } else {
                profile_addr_diff += label_pos[latxs_ir2_opnd_label_id(&ir2_current->_opnd[1])]<<2;
            }
#endif
        } else {
            ir2_num++;
        }
    }

    /*
     * 2. resolve the offset of successor linkage code
     * @jmp_target_arg recoed the jmp  inst position
     * @jmp_reset_offset record the successor inst of jmp(exclude delay slot)
     *                   when the tb is removed from buffer. the jmp inst use
     *                   this position to revert the original "fall through"
     */
    {
        TranslationBlock *tb = td->curr_tb;
        /* prologue/epilogue has no tb */
        if (tb) {
            int label_id_0 = tb->jmp_reset_offset[0];
            if (label_id_0 != TB_JMP_OFFSET_INVALID) {
                tb->jmp_reset_offset[0] =
                    (label_pos[label_id_0] << 2) + 8;
                tb->jmp_insn_offset[0] = (label_pos[label_id_0] << 2);
            }
            int label_id_1 = tb->jmp_reset_offset[1];
            if (label_id_1 != TB_JMP_OFFSET_INVALID) {
                tb->jmp_reset_offset[1] =
                    (label_pos[label_id_1] << 2) + 8;
                tb->jmp_insn_offset[1] = (label_pos[label_id_1] << 2);
            }
#ifdef CONFIG_LATX_TU
            for (int i = 0; i < 2; i++) {
                int label_id = tb->s_data->tu_generation_offset_patch[i];

                if (label_id != TB_JMP_OFFSET_INVALID) {
                    tb->s_data->tu_generation_offset_patch[i] =
                        label_pos[label_id] << 2;
                }
            }
#endif
            /*
             * +--------+    <--- label for jmp_cp_data
             * |        | \
             * |        |  | : cp offset
             * |        | /
             * +--------+    <--- label for jmp_insn_offset
             * |  b 2   |
             * |  nop   |
             * +--------+
             */
            int cp_off = 0;
            int label_id_cp0 = tb->jmp_cp_data[0];
            if (label_id_cp0 != 0) {
                cp_off = label_pos[label_id_0] - label_pos[label_id_cp0];
                g_assert((cp_off & ~TB_JMP_CP_DATA_OFF) == 0);
                tb->jmp_cp_data[0] = cp_off | TB_JMP_CP_DATA_VALID;
            }
            int label_id_cp1 = tb->jmp_cp_data[1];
            if (label_id_cp1 != 0) {
                cp_off = label_pos[label_id_1] - label_pos[label_id_cp1];
                g_assert((cp_off & ~TB_JMP_CP_DATA_OFF) == 0);
                tb->jmp_cp_data[1] = cp_off | TB_JMP_CP_DATA_VALID;
            }

            if (option_int_opt == IRQ_CHECK_BLOCK_START) {
                int nocheck = latxs_ir2_opnd_label_id(&td->int_nocheck_label);
                tb->label_pos_nocheck = label_pos[nocheck];
            }
        }
    }

    /* 3. resolve the branch instructions */
    ir2_num = 0;
    for (ir2_id = 0; ir2_id < ir2_nr; ++ir2_id) {

        ir2_current = ir2_get(ir2_id);
        ir2_opc = ir2_opcode(ir2_current);

        if (latxs_ir2_opcode_has_pcrel_label(ir2_opc)) {

            IR2_OPND *label_opnd = latxs_ir2_branch_get_label(ir2_current);

            /* LISA_B and LISA_BL could contain label or imm */
            if (label_opnd && latxs_ir2_opnd_is_label(label_opnd)) {
                int label_id = latxs_ir2_opnd_label_id(label_opnd);
                lsassert(label_id > 0 && label_id <= label_nr);

                ir2_current->_b_label_id = label_id;
                ir2_current->_b_ir2_id = label_pos_id[label_id];

                int label_pos_ir2_num = label_pos[label_id];

                lsassertm(label_pos_ir2_num != -1,
                        "label %d is not inserted\n",
                         label_id);

                latxs_ir2_resolve_pcrel_label(ir2_current, label_opnd,
                        label_pos_ir2_num - ir2_num);
            }
        }

        if (ir2_opc != LISA_LABEL && ir2_opc != LISA_PROFILE && ir2_opc != LISA_X86_INST) {
            ir2_num++;
        }
    }
#ifdef CONFIG_LATX_PROFILER
        TranslationBlock *tb = td->curr_tb;
    if (tb) {
        SET_TB_PROFILE(tb, nr_code, ir2_num - (profile_addr_diff >> 2));
    }
#endif
}

int latxs_tr_check_buffer_overflow(
        const void *code_start,
        TRANSLATION_DATA *td)
{
    uint64_t code_highwater = (uint64_t)td->code_highwater;
    uint64_t code_tb_end = (uint64_t)code_start;

    int code_nr = td->ir2_cur_nr;
    code_nr -= td->label_num; /* lisa_label    */
    code_nr -= td->ir1_nr;    /* lisa_x86_inst */

    code_tb_end += code_nr << 2;

    if (code_tb_end >= code_highwater) {
        return 1;
    }

    return 0;
}

static inline __attribute__((always_inline))
int latxs_tr_ir2_emit(const void *code_base, TRANSLATION_DATA *td,
        uint32_t (*assemble)(IR2_INST *), bool dump_host)
{
    IR2_INST *pir2 = NULL;
    void *code_ptr = (void *)(ADDR)code_base;
    int code_nr = 0;

    TranslationBlock *tb = td->curr_tb;

    /* Counter for encode search */
    int x86ins_idx  = 0;
    int x86ins_nr  = -1;
    int x86ins_hcnt = 0; /* host instruction count */
    int x86ins_size = 0;

    int ir2_id = 0;
    int ir2_nr = td->ir2_cur_nr;
    IR2_INST *ir2_base = td->ir2_array;
    for (ir2_id = 0; ir2_id < ir2_nr; ++ir2_id) {

        pir2 = &ir2_base[ir2_id];
        IR2_OPCODE ir2_opc = ir2_opcode(pir2);

#ifdef CONFIG_LATX_PROFILER
        if (ir2_opc == LISA_PROFILE) {
            continue;
        }
#endif
        if (ir2_opc == LISA_X86_INST) {

            if (dump_host && qemu_log_in_addr_range(tb->pc)) {
                fprintf(stderr, "IR2 at %p LISA X86Ins = 0x%x\n",
                        code_ptr, latxs_ir2_opnd_imm(&pir2->_opnd[0]));
            }

            if (x86ins_nr >= 0) {
                td->x86_ins_idx[x86ins_nr] = x86ins_idx;
                td->x86_ins_lisa_nr[x86ins_nr] = x86ins_hcnt;
            }
            x86ins_idx = ir2_get_id(pir2);
            x86ins_nr += 1;
            /* calculate TB size */
            IR1_INST *pir1 = &td->ir1_inst_array[x86ins_nr];
            x86ins_size += latxs_ir1_inst_size(pir1);

            continue;
        }

        if (ir2_opc == LISA_LABEL) {
            if (dump_host && (!tb || qemu_log_in_addr_range(tb->pc))) {
                fprintf(stderr, "IR2 at %p LISA LABEL  = ---------- [%03d] LABEL %d",
                        code_ptr, pir2->_id,
                        latxs_ir2_opnd_imm(&pir2->_opnd[0]));
                if (pir2->_b_label_name) {
                    fprintf(stderr, " ====== %s ====== ", pir2->_b_label_name);
                }
                fprintf(stderr, "\n");
            }
            continue;
        }

        if (ir2_opc != LISA_LABEL) {
            uint32_t ir2_binary = assemble(pir2);
            if (dump_host && (!tb || qemu_log_in_addr_range(tb->pc))) {
                fprintf(stderr, "IR2 at %p LISA Binary = 0x%08x ",
                        code_ptr, ir2_binary);
                latxs_ir2_dump(pir2);
            }

            *(uint32_t *)code_ptr = ir2_binary;
            code_ptr = code_ptr + 4;
            code_nr += 1;

            x86ins_hcnt += 1;
        }
    }

    if (td->curr_tb) {
        td->x86_ins_idx[x86ins_nr] = x86ins_idx;
        td->x86_ins_lisa_nr[x86ins_nr] = x86ins_hcnt;
        td->x86_ins_nr = x86ins_nr + 1;
        td->x86_ins_size = x86ins_size;

        lsassertm(td->x86_ins_nr == td->ir1_nr,
                "x86 inst %d not equal to IR1 %d.\n",
                td->x86_ins_nr, td->ir1_nr);

        tb->icount = td->x86_ins_nr;
        tb->size = x86ins_size;
    }

    return code_nr;
}

inline int latxs_tr_ir2_assemble(const void *code_base)
{
    latxs_label_dispose(code_base);
    TRANSLATION_DATA *td = lsenv->tr_data;
    /* Prologue/epilogue has no TB and does not check buffer overflow. */
    if (td->curr_tb && latxs_tr_check_buffer_overflow(code_base, td)) {
        return -1;
    }
    if (likely(option_new_assemble && !option_dump_host)) {
        return latxs_tr_ir2_emit(code_base, td, ir2_assemble_new, false);
    }
    return latxs_tr_ir2_emit(code_base, td,
            option_new_assemble ? ir2_assemble_new : ir2_assemble,
            option_dump_host);
}

int latxs_tr_translate_tb(TranslationBlock *tb, int *search_size)
{
    int code_nr = 0;
    int code_size = 0;
    int is_buffer_overflow = 0;

    latxs_tr_init(tb);
#ifdef LATXS_IMM_CACHE_ENABLE
    latxs_imm_init(tb);
#endif

    TRANSLATION_DATA *td = lsenv->tr_data;
    uint32_t mtcc = td->mtcc_id;
    if (option_pushpop) {
        if (!region_alloc_push_pop_data(tb, mtcc, td->ir1_nr)) {
            return __GENCODE_ERR_AUX_DATA_OVERFLOW;
        }
    }
    bool translation_done = latxs_tr_ir2_generate(tb);

    if (translation_done) {
        code_nr = latxs_tr_ir2_assemble(tb->tc.ptr);
        if (option_rastoptp && code_nr > 0 && tb->inst_ptr_offset) {
            uint32_t cold = LATXS_RAS_TP_COLD_OFFSET(tb);
            uint32_t size = LATXS_RAS_TP_COLD_SIZE(tb);
            g_assert(size && !(cold & 3) &&
                     cold <= (uint64_t)code_nr * 4 &&
                     size <= (uint64_t)code_nr * 4 - cold);
            g_assert(*(uint32_t *)((uintptr_t)tb->tc.ptr + tb->inst_ptr_offset) ==
                     latxs_ras_tp_closed_gate(tb));
        }
    }

    if (likely(code_nr > 0)) {
        code_size = code_nr * 4;
        is_buffer_overflow = 0;
    } else {
        /* Buffer Overflow */
        code_size = -1;
        is_buffer_overflow = 1;
    }

    if (tb && !is_buffer_overflow) {
        void *search_block;
        if (!option_split_search) {
            search_block = (void *)(ADDR)tb->tc.ptr + code_size;
            *search_size = latxs_tb_encode_search(tb, search_block);
        } else {
            search_block = region_alloc_search(tb, mtcc);
            if (!search_block) {
                return __GENCODE_ERR_AUX_DATA_OVERFLOW;
            }
            latxs_tb_encode_search(tb, search_block);
            *search_size = 0;
        }
    }

    latxs_tr_fini();

    if (tb) {
        /* 0. IR1 will not be used again, free it */
        //int i = 0;
        //for (i = 0; i < tb->icount; ++i) {
        //    latxs_ir1_free_info(tb->_ir1_instructions + i);
        //}
        /* 1. check buffer overflow */
        if (unlikely(is_buffer_overflow)) {
            return __GENCODE_ERR_BUFFER_OVERFLOW;
        }
        /* 2. TODO check TB overflow */
        /* if (code_nr >= 0x3fff) { */
            /* return __GENCODE_ERR_TB_TOO_LARGE; */
        /* } */
        /* 3. flush icache range */
        ADDR s = (ADDR)tb->tc.ptr;
        ADDR e = s + code_size;
        flush_idcache_range((uintptr_t)s, (uintptr_t)s, (uintptr_t)e);
    }

    return code_size;
}

__inline static void free_temp_reg(TRANSLATION_DATA *td)
{
    td->itemp_mask = 0;
    td->ftemp_mask = 0;
    td->itemp_mask_bk = 0;
}

void latxs_tr_init_translate_ir1(TranslationBlock *tb, int index)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    IR1_INST *ir1_list = td->ir1_inst_array;
    int ir1_nr = td->ir1_nr;

    (void)ir1_nr; /* to avoid warning */
    lsassert(index >= 0 && ir1_nr >= 0 && index < ir1_nr);

    /* 1. initialize global data */
    IR1_INST *pir1 = ir1_list + index;
    lsenv->tr_data->curr_ir1_inst = pir1;

    /* 2. Append mips_x86_inst into IR2 list to mark */
    /*    the start of each IR1 */
    if (pir1->instptn.opc != INSTPTN_OPC_NOP_FOR_EXP)
        latxs_append_ir2_opnda(LISA_X86_INST, ir1_addr(pir1));

    /* 3. clear temp register mapping */
    free_temp_reg(td);

    /* 4. reset mmio optimization stat */
    td->inst_do_opt_mmio = 0;
}

uint8_t latxs_traceir1_type[X86_INS_ENDING + 1] = {0};

static inline void latxs_option_traceir1(void *_pir1)
{
    IR1_INST *pir1 = (IR1_INST *)_pir1;

    IR1_OPCODE id = ir1_opcode(pir1);

    if (option_traceir1 == 1) {
        if (!latxs_traceir1_type[id]) {
            latxs_traceir1_type[id] = 1;
            ir1_dump(pir1);
        }
    }

    if (option_traceir1 == 2) {
        if (id == option_traceir1_data) {
            ir1_dump(pir1);
        }
    }
}

void latxs_tr_shbr_restore_zero64(uint16_t mask)
{
#ifdef CONFIG_LATX_TU
    if (!option_shbr || !lsenv->tr_data->is_tu_mode) {
        return;
    }
    while (mask) {
        int reg = ctz32(mask);
        IR2_OPND xmm = latxs_ra_alloc_xmm(reg);

        mask &= mask - 1;
        latxs_append_ir2_opnd2i(LISA_VINSGR2VR_D, &xmm,
                                &latxs_zero_ir2_opnd, 1);
    }
#else
    (void)mask;
#endif
}

inline bool latxs_tr_ir2_generate(TranslationBlock *tb)
{
#ifdef LATXS_IMM_CACHE_DEBUG
    if (option_imm_debug) {
        static long hit_count = 0;
        imm_log_tb_pc(tb->pc);
        // imm_cache_set_dynamic_option(false,"enter latxs_tr_ir2_generate");
        if (imm_skip_pc || imm_pc_mask) {
            imm_cache_set_dynamic_option(false, "debug enter");
            bool hit = false;
            // unsigned long long pc =  0xfffff880012670b1;
            // unsigned long long prefix = 0xfffff80001200000;
            // unsigned long long mask =   0xfffffffffff00000;
            if (imm_skip_pc && tb->pc == imm_skip_pc) {
                hit = true;
            } else if (imm_pc_mask && ((tb->pc & imm_pc_mask) == imm_pc_prefix)) {
                hit = true;
            }
            if (hit) {
                hit_count++;
                printf("----------------------\n");
                printf(
                    "[%d][%ld]hit debug_pc %lx\n", qemu_get_thread_id(), hit_count, imm_pc_prefix);
                printf("----------------------\n");
                imm_trace("dynamic_set_option true hit\n");
                imm_cache_set_dynamic_option(true, "debug hit");
            }
        }
    }
#endif

    TRANSLATION_DATA *td = lsenv->tr_data;
    int i = 0;

    IR1_INST *ir1_list = td->ir1_inst_array;
    int ir1_nr = td->ir1_nr;

    latxs_tr_gen_tb_start();

#ifdef CONFIG_LATX_PROFILER
    per_tb_count((void *)&((tb->profile).exec_times), 1);
    {
        IR2_OPND tmp = la_new_itmp();
        // env.rdtime = start
        la_out2(LISA_RDTIME_D, &tmp, &la_zero);
        la_out2i(LISA_ST_D, &tmp, &la_env, offsetof(CPUX86State, rdtime));
        la_tmp_free(tmp);
    }
    if (option_hamt)
    {
        IR2_OPND t0 = la_new_itmp();
        la_out2i(LISA_LD_D, &t0, &la_env, offsetof(CPUX86State, hamt_data_ptr));
        if (hamt_enabled()) latxs_tr_dmw_in_fix_mem(&t0);
        la_out2i(LISA_LD_D, &t0, &t0, HD_TLBR_RDTIME_ALL * 8);
        la_out2i(LISA_ST_D, &t0, &la_env, offsetof(CPUX86State, rdtime_tlbr));
        la_tmp_free(t0);
    }
#endif
    /* PCREL trans_init, use eip instead */
    // td->pc_save = tb->pc;
    td->pc_save = get_eip_cpu_state(tb);
    td->pc_curr = td->pc_save;
    td->pc_next = td->pc_save;

    IR1_INST *pir1 = NULL;

    td->sp_patch = 0;

    for (i = 0; i < ir1_nr; ++i) {
        td->curr_ir1_nr = i;
        pir1 = ir1_list + i;

        if (option_traceir1)
            latxs_option_traceir1(pir1);

#ifdef LATXS_IMM_CACHE_ENABLE
        if (can_do_imm()) {
            IMM_CACHE *imm_cache = lsenv->tr_data->imm_cache;
            imm_cache->curr_ir1_index = i;
            imm_cache->curr_ir2_index = lsenv->tr_data->ir2_cur_nr;
        }
#endif

        latxs_tr_init_translate_ir1(tb, i);
        if (option_shbr && td->is_tu_mode) {
            latxs_tr_shbr_restore_zero64(
                pir1->shbr_restore_zero64_before);
        }

        if ((td->max_insns == i + 1) && (td->sys.cflags & CF_LAST_IO)) {
            latxs_tr_gen_io_start();
        } 

        if (option_dump_ir1 && qemu_log_in_addr_range(tb->pc)) {
            fprintf(stderr, "%lx ir1 translate >>> ", ir1_addr(pir1) + td->sys.cs_base);
            ir1_dump(pir1);
        }

#ifndef CONFIG_LATX_OPT
        if (td->sys.code64) {
            lsassert(((CPUX86State *)lsenv->cpu_state)->segs[cs_index].base ==
                     0);
            lsassert(((CPUX86State *)lsenv->cpu_state)->segs[ds_index].base ==
                     0);
            lsassert(((CPUX86State *)lsenv->cpu_state)->segs[es_index].base ==
                     0);
            lsassert(((CPUX86State *)lsenv->cpu_state)->segs[ss_index].base ==
                     0);
            if (ir1_opcode(pir1) != X86_INS_CALL) {
                if (latxs_ir1_has_prefix_addrsize(pir1)) {
                    ir1_dump(pir1);
                }
            }
            int opnd_num = ir1_opnd_num(pir1);
            for (size_t i = 0; i < opnd_num; i++) {
                IR1_OPND *op = ir1_get_opnd(pir1, i);
                if (ir1_opnd_is_mem(op) && ir1_opnd_has_seg(op) &&
                    !latxs_ir1_is_nop(pir1)) {
                    int seg_num = ir1_opnd_get_seg_index(op);
                    if (seg_num == cs_index || seg_num == ds_index ||
                        seg_num == es_index || seg_num == ss_index) {
                        lsassert(!lsenv->tr_data->sys.addseg);
                    }
                }
            }
        }
#endif

        td->pc_next += latxs_ir1_inst_size(pir1);
        latxs_tr_gen_smc_inst_self_check(tb, pir1);
        bool clear_esp = latx_adjust_sp_before_translate(tb, pir1, i);
        bool translation_success = ir1_translate(pir1);
        if (option_shbr && td->is_tu_mode) {
            latxs_tr_shbr_restore_zero64(
                pir1->shbr_restore_zero64_after);
        }
        if (option_pushpop &&
            (td->sys.flags & HF_CPL_MASK) &&
            clear_esp) {
            td->sp_patch = 0;
        }
        td->pc_curr = td->pc_next;

        (void)translation_success; /* to avoid warning  */
        lsassertm(translation_success,
                "translate failed for inst %s\n",
                pir1->info->mnemonic);
#ifdef LATXS_IMM_CACHE_ENABLE
        if (can_do_imm()) {// imm_debug=0;
            IMM_CACHE *imm_cache = lsenv->tr_data->imm_cache;
            if (option_imm_pattern) {
                imm_cache_update_ir1_usage(pir1, i);
            }
            if (imm_cache->optimized_ir2) {
                imm_cache_print_ir1(pir1);
                imm_cache_print_tr_ir2();
                imm_log("-------------------------------------------------\n\n");
                imm_cache->optimized_ir2 = false;
            }
        }
#endif
        if (td->end_with_exception) {
            /* 1. get real number of IR1 */
            int real_ir1_nr = i + 1;
            td->ir1_nr = real_ir1_nr;
            /* 2. Reset related fields in ETB */
            for (++i; i < ir1_nr; ++i) {
                pir1 = ir1_list + i;
                latxs_ir1_free_info(pir1);
            }
            tb_shrink_icount(tb, real_ir1_nr,
                             option_split_search && option_pushpop);
            /* 3. ICOUNT: decrease real number of IR1 */
            if (tb->cflags & CF_USE_ICOUNT) { 
                IR2_INST *pir2 = ir2_get(td->dec_icount_inst_id); 
                pir2->_opnd[2] = latxs_ir2_opnd_new(IR2_OPND_IMMH, 
                        0 - real_ir1_nr); 
            } 
            break;
        }
    }

    free_temp_reg(td);

    if (option_pushpop &&
        (td->sys.flags & HF_CPL_MASK) &&
        td->sp_patch) {
        int ss_addr_size = latxs_get_sys_stack_addr_size();
        latxs_update_stack_pointer(td->sp_patch, ss_addr_size);
        td->sp_patch = 0;
    }

    if (!(td->end_with_exception)) {
        latxs_tr_gen_eob_if_tb_too_large(td->tb_too_large_pir1);
        latxs_tr_gen_sys_eob(td->sys_eob_pir1);
    }
    latxs_tr_gen_tb_end();
#ifdef CONFIG_LATX_TU
    if (td->hras_indirect_call.valid) {
        /* Keep ordinary NJC miss branches near their final CALL, before
         * potentially large memory slow paths. Never BL into an inline
         * FSHT refill: ra still contains the NJC slot there. */
        latxs_tr_gen_indirect_call_cold(td->hras_indirect_call);
    }
#endif
#ifdef CONFIG_SOFTMMU
    tr_gen_softmmu_slow_path();
#endif
    /* Keep the four-instruction unlinked CALL tail last for gate recovery. */
    latxs_ras_tp_emit_call_cold();

    return true;
}

void latxs_load_intcheck_to_reg(IR2_OPND  *intop)
{
    int intoff = (int)offsetof(X86CPU, neg.icount_decr.u32) -
                 (int)offsetof(X86CPU, env);
    latxs_append_ir2_opnd2i(LISA_LD_W, intop, &la_env, intoff);
}

void latxs_tr_gen_tb_start(void)
{
#ifndef CONFIG_SOFTMMU
    if (latxs_tb_config_usermode()) {
        return;
    }
#endif

    TRANSLATION_DATA *td = lsenv->tr_data;
    TranslationBlock *tb = td->curr_tb;
    IR2_OPND count = latxs_ra_alloc_itemp();
    uint32_t cflags = tb->cflags;

    lsassert(!(cflags & CF_USE_ICOUNT) || option_int_opt == IRQ_CHECK_NO_OPT);
    lsassert(!option_tb_pcrel && !option_page_pcrel);

    if ((cflags & CF_USE_ICOUNT)) {
        /* load icount */
        latxs_load_intcheck_to_reg(&count);
        /* decrease by ir1 nr */
        int ir1_nr = td->ir1_nr;
        IR2_INST *pir2 = latxs_append_ir2_opnd2i(LISA_ADDI_D,
                &count, &count, 0 - ir1_nr);
        td->dec_icount_inst_id = ir2_get_id(pir2);
        /* exit if < 0 */
        latxs_append_ir2_opnd3(LISA_BLT, &count, &latxs_zero_ir2_opnd,
                &(td->exitreq_label));
        /* update icount */
        latxs_append_ir2_opnd2i(LISA_ST_H, &count, &latxs_env_ir2_opnd,
            (int32_t)offsetof(X86CPU, neg.icount_decr.u16.low) -
            (int32_t)offsetof(X86CPU, env));
        latxs_tr_gen_io_end();
        goto gen_tb_start_done;
    }

    if (option_int_opt == IRQ_CHECK_NO_OPT) {
        IR2_OPND tb_exec_label = la_new_label();
        /* load interrupt request */
        latxs_load_intcheck_to_reg(&count);
        /* exec tb if no interrupt exist */
        la_out3(LISA_BGE, &count, &la_zero, &tb_exec_label);
        /* exit here for interrupt */
        {
            /* load tb address */
            IR2_OPND tb_ptr_opnd = latxs_ra_alloc_dbt_arg1();
            latxs_tr_gen_exit_tb_load_tb_addr(&tb_ptr_opnd, (ADDR)tb);
            /* save eip */
            ADDRX eip = get_eip_cpu_state(tb) - tb->cs_base;
            if (option_aot_save) {
                aot_load_guest_addr_to_ir2(&count, eip, LOAD_GUEST_ADDR);
            } else {
                latxs_load_imm64_to_ir2(&count, eip);
            }
            la_out2i(LISA_ST_D, &count, &la_env, lsenv_offset_of_eip(lsenv));
            /* jmp to context switch 3 */
            int offset = td->ir2_asm_nr << 2;
            tb_sc_table_offset_jmp(tb, td->mtcc_id, cs_native_to_bt_ret_n3,
                    td->tcg_region_offset, offset, 0, CS_NATIVE_TO_BT_RET_N3);
        }
        /* go on execute this tb */
        la_out1(LISA_LABEL, &tb_exec_label);
    } else if (option_int_opt == IRQ_CHECK_BLOCK_START) {
        /* docheck label */
        la_out1(LISA_LABEL, &td->int_docheck_label);
        /* load interrupt request */
        latxs_load_intcheck_to_reg(&count);
        /* exit if interrupt exist : exit stub at tb end */
        la_out3(LISA_BLT, &count, &latxs_zero_ir2_opnd, &(td->exitreq_label));
        /* nocheck label */
        la_out1(LISA_LABEL, &td->int_nocheck_label);
    }

    latxs_ra_free_temp(&count);

gen_tb_start_done:

#if defined(CONFIG_LATX_TB_EXEC_TRACE)
    latx_tr_gen_tb_exec_trace(tb);
#endif

    return;
}

void latxs_tr_gen_tb_end(void)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    TranslationBlock *tb = td->curr_tb;
    uint32_t cflags = tb->cflags;
    uint32_t mtcc_id = td->mtcc_id;

#ifdef CONFIG_LATX_PROFILER
    if(!td->is_tu_mode)
    {
        la_label_name(td->tbprofile_rdtime_label, "TB profile rdtime");

        latxs_tr_save_temp_register_mask(0xffff);
        td->itemp_mask = 0;

        IR2_OPND tmp0 = la_new_itmp();
        IR2_OPND tmp1 = la_new_itmp();
        IR2_OPND tmp2 = la_new_itmp();

        la_out2(LISA_RDTIME_D, &tmp1, &la_zero);
        la_out2i(LISA_LD_D, &tmp0, &la_env, offsetof(CPUX86State, rdtime));
        la_out2i(LISA_ST_D, &tmp1, &la_env, offsetof(CPUX86State, rdtime));

        // tmp0 = end - start
        la_out3 (LISA_SUB_D, &tmp0, &tmp1, &tmp0);

        // tmp1 = &(tbprofile.exec_rdtime)
        latxs_profile_address(&tmp1, tb, &tb->profile.exec_rdtime);

        // tbprofile.exec_rdtime += tmp1
        la_out3(LISA_AMADD_D, &la_zero, &tmp1, &tmp0);
    //    la_out2i(LISA_LD_D, &tmp2, &tmp1, 0);
    //    la_out3(LISA_ADD_D, &tmp2, &tmp2, &tmp0);
    //    la_out2i(LISA_ST_D, &tmp2, &tmp1, 0);

        if (option_hamt) {
            la_out2i(LISA_LD_D, &tmp1, &la_env, offsetof(CPUX86State, hamt_data_ptr));
            if (hamt_enabled()) latxs_tr_dmw_in_fix_mem(&tmp1);
            la_out2i(LISA_LD_D, &tmp1, &tmp1, HD_TLBR_RDTIME_ALL * 8);
            la_out2i(LISA_LD_D, &tmp0, &la_env, offsetof(CPUX86State, rdtime_tlbr));
            la_out3 (LISA_SUB_D, &tmp0, &tmp1, &tmp0);
            latxs_profile_address(&tmp1, tb, &tb->profile.exec_tlbr_rdtime);
            la_out3(LISA_AMADD_D, &la_zero, &tmp1, &tmp0);
        }

        la_tmp_free(tmp0);
        la_tmp_free(tmp1);
        la_tmp_free(tmp2);

        latxs_tr_restore_temp_register_mask(0xffff);

        la_out0_(lisa_return);
    }
#endif

    if (option_int_opt != 1 &&
        !(cflags & CF_USE_ICOUNT)) {
        return;
    }
#ifndef CONFIG_SOFTMMU
    if (latxs_tb_config_usermode()) {
        return;
    }
#endif

    latxs_append_ir2_opnd1(LISA_LABEL, &td->exitreq_label);

    IR2_OPND tb_ptr_opnd = latxs_ra_alloc_dbt_arg1();
    IR2_OPND eip_opnd = latxs_ra_alloc_itemp();

    ADDR tb_addr = (ADDR)tb;

    latxs_tr_gen_exit_tb_load_tb_addr(&tb_ptr_opnd, tb_addr);

    ADDRX eip = get_eip_cpu_state(tb) - tb->cs_base;

    if (!option_tb_pcrel && !option_page_pcrel) {
        if (option_aot_save) {
            aot_load_guest_addr_to_ir2(&eip_opnd, eip, LOAD_GUEST_ADDR);
        } else {
            latxs_load_imm64_to_ir2(&eip_opnd, eip);
        }
        latxs_append_ir2_opnd2i(LISA_ST_D, &eip_opnd, &latxs_env_ir2_opnd,
                lsenv_offset_of_eip(lsenv));
    }

    latxs_ra_free_temp(&eip_opnd);

    int offset = td->ir2_asm_nr << 2;
    tb_sc_table_offset_jmp(tb, mtcc_id, cs_native_to_bt_ret_n3,
            td->tcg_region_offset, offset, 0, CS_NATIVE_TO_BT_RET_N3);
}

/* translate functions */

void latxs_tr_save_registers_to_env(
        uint32_t gpr_to_save,
        uint8_t fpr_to_save, int save_top,
        uint32_t xmm_to_save,
        uint8_t vreg_to_save)
{
    /* 1. GPR */
    latxs_tr_save_gprs_to_env(gpr_to_save);

    /* 2. FPR (MMX) */
    latxs_tr_save_fprs_to_env(fpr_to_save, save_top);

    /* 3. XMM */
    latxs_tr_save_xmms_to_env(xmm_to_save);

    /* 4. virtual registers */
    latxs_tr_save_vreg_to_env(vreg_to_save);
}

void latxs_tr_load_registers_from_env(
        uint32_t gpr_to_load,
        uint8_t fpr_to_load, int load_top,
        uint32_t xmm_to_load,
        uint8_t vreg_to_load)
{
    /* 4. virtual registers */
    latxs_tr_load_vreg_from_env(vreg_to_load);

    /* 3. XMM */
    latxs_tr_load_xmms_from_env(xmm_to_load);

    /* 2. FPR (MMX) */
    latxs_tr_load_fprs_from_env(fpr_to_load, load_top);

    /* 1. GPR */
    latxs_tr_load_gprs_from_env(gpr_to_load);

}

void latxs_tr_save_gprs_to_env(uint32_t mask)
{
    int i = 0;
    for (i = 0; i < CPU_NB_REGS; ++i) {
        if (BITS_ARE_SET(mask, 1 << i)) {
            IR2_OPND gpr_opnd = latxs_ra_alloc_gpr(i);
            latxs_append_ir2_opnd2i(LISA_ST_D, &gpr_opnd, &latxs_env_ir2_opnd,
                                    lsenv_offset_of_gpr(lsenv, i));
        }
    }
}

void latxs_tr_save_fprs_to_env(uint8_t mask, int save_top)
{

    /* 1. save FPU top: curr_top is rotated */
    if (save_top) {
        lsassert(!option_soft_fpu);
        latxs_tr_gen_save_curr_top();
        latxs_tr_fpu_disable_top_mode();
    }
    /* 2. save FPRs: together with FPU top */

#ifndef CONFIG_LATX_OPT_NOFP
    int i = 0;
    IR2_OPND mode_fpu = latxs_arg2_ir2_opnd;
    IR2_OPND label_fpu = latxs_ir2_opnd_new_label();

    if (option_soft_fpu_fast) {
        latxs_append_ir2_opnd2i(LISA_LD_BU, &mode_fpu, &latxs_env_ir2_opnd, lsenv_offset_of_mode_fpu(lsenv));
        latxs_append_ir2_opnd3(LISA_BNE, &mode_fpu, &latxs_zero_ir2_opnd, &label_fpu);
    }

    for (i = 0; i < 8; i++) {
        if (BITS_ARE_SET(mask, 1 << i)) {
            IR2_OPND mmx_opnd = latxs_ra_alloc_mmx(i);
            latxs_append_ir2_opnd2i(LISA_FST_D, &mmx_opnd,
                    &latxs_env_ir2_opnd,
                    lsenv_offset_of_mmx(lsenv, i));
        }
    }
    if (option_soft_fpu_fast) {
        latxs_append_ir2_opnd1(LISA_LABEL, &label_fpu);
    }
#endif

}

void latxs_tr_save_xmms_to_env(uint32_t mask)
{
    int i = 0;
    for (i = 0; i < CPU_NB_REGS; i++) {
        if (BITS_ARE_SET(mask, 1 << i)) {
            IR2_OPND xmm_opnd = latxs_ra_alloc_xmm(i);
            latxs_append_ir2_opnd2i(LISA_VST, &xmm_opnd,
                    &latxs_env_ir2_opnd,
                    lsenv_offset_of_xmm(lsenv, i));
        }
    }
}

void latxs_tr_save_vreg_to_env(uint8_t mask)
{
    int i = 0;
    for (i = 0; i < 8; ++i) {
        if (BITS_ARE_SET(mask, 1 << i)) {
            IR2_OPND vreg_opnd = latxs_ra_alloc_vreg(i);
            latxs_append_ir2_opnd2i(LISA_ST_D, &vreg_opnd,
                    &latxs_env_ir2_opnd,
                    lsenv_offset_of_vreg(lsenv, i));
        }
    }
}

void latxs_tr_load_gprs_from_env(uint32_t mask)
{
    int i = 0;
    for (i = 0; i < CPU_NB_REGS; ++i) {
        if (BITS_ARE_SET(mask, 1 << i)) {
            IR2_OPND gpr_opnd = latxs_ra_alloc_gpr(i);
            latxs_append_ir2_opnd2i(LISA_LD_D, &gpr_opnd, &latxs_env_ir2_opnd,
                                    lsenv_offset_of_gpr(lsenv, i));
        }
    }
}

void latxs_tr_load_fprs_from_env(uint8_t mask, int load_top)
{

    /* make sure the FP registers is loaded without LSFPU */
    latxs_tr_fpu_disable_top_mode();

#ifndef CONFIG_LATX_OPT_NOFP
    int i = 0;
    IR2_OPND mode_fpu = latxs_arg2_ir2_opnd;
    IR2_OPND label_fpu = latxs_ir2_opnd_new_label();

    if (option_soft_fpu_fast) {
        latxs_append_ir2_opnd2i(LISA_LD_BU, &mode_fpu, &latxs_env_ir2_opnd, lsenv_offset_of_mode_fpu(lsenv));
        latxs_append_ir2_opnd3(LISA_BNE, &mode_fpu, &latxs_zero_ir2_opnd, &label_fpu);
    }

    for (i = 0; i < 8; i++) {
        if (BITS_ARE_SET(mask, 1 << i)) {
            IR2_OPND mmx_opnd = latxs_ra_alloc_mmx(i);
            latxs_append_ir2_opnd2i(LISA_FLD_D, &mmx_opnd,
                    &latxs_env_ir2_opnd,
                    lsenv_offset_of_mmx(lsenv, i));
        }
    }

    if (option_soft_fpu_fast) {
        latxs_append_ir2_opnd1(LISA_LABEL, &label_fpu);
    }
#endif

    if (load_top && option_lsfpu) {
        lsassert(!option_soft_fpu);
        latxs_tr_fpu_enable_top_mode();
        IR2_OPND top = latxs_ra_alloc_itemp();
        latxs_tr_load_lstop_from_env(&top);
        latxs_ra_free_temp(&top);
    }
}

void latxs_tr_load_xmms_from_env(uint32_t mask)
{
    int i = 0;
    for (i = 0; i < CPU_NB_REGS; i++) {
        if (BITS_ARE_SET(mask, 1 << i)) {
            IR2_OPND xmm_opnd = latxs_ra_alloc_xmm(i);
            latxs_append_ir2_opnd2i(LISA_VLD, &xmm_opnd,
                    &latxs_env_ir2_opnd,
                    lsenv_offset_of_xmm(lsenv, i));
        }
    }
}

void latxs_tr_load_vreg_from_env(uint8_t mask)
{
    int i = 0;
    for (i = 0; i < 8; ++i) {
        if (BITS_ARE_SET(mask, 1 << i)) {
            IR2_OPND vreg_opnd = latxs_ra_alloc_vreg(i);
            latxs_append_ir2_opnd2i(LISA_LD_D, &vreg_opnd,
                    &latxs_env_ir2_opnd,
                    lsenv_offset_of_vreg(lsenv, i));
        }
    }
}

void latxs_enter_mmx(IR1_INST *pir1)
{
#ifdef CONFIG_LATX_OPT_NOFP
    int i;
    int use_mmx = latxs_ir1_access_mmx(pir1);
    if (option_soft_fpu && use_mmx && !(lsenv->tr_data->mmx_flag)) {
        lsenv->tr_data->mmx_flag = 1;
        latxs_append_ir2_opnd2i(LISA_ST_W, &latxs_zero_ir2_opnd,
                                &latxs_env_ir2_opnd, lsenv_offset_of_top(lsenv));
        latxs_append_ir2_opnd2i(LISA_ST_D, &latxs_zero_ir2_opnd,
                                &latxs_env_ir2_opnd,
                                lsenv_offset_of_tag_word(lsenv));
        IR2_OPND nofp = latxs_ra_alloc_itemp();
        IR2_OPND label_fpu = latxs_ir2_opnd_new_label();
        latxs_append_ir2_opnd2i(LISA_LD_W, &nofp, &latxs_env_ir2_opnd, lsenv_offset_of_nofp(lsenv));
        latxs_append_ir2_opnd3(LISA_BNE, &nofp, &latxs_zero_ir2_opnd, &label_fpu);
        latxs_append_ir2_opnd2i(LISA_ST_W, &latxs_env_ir2_opnd, &latxs_env_ir2_opnd,
                                lsenv_offset_of_nofp(lsenv));
        for (i = 0; i < 8; i++) {
            IR2_OPND mmx_opnd = latxs_ra_alloc_mmx(i);
            latxs_append_ir2_opnd2i(LISA_FLD_D, &mmx_opnd, &latxs_env_ir2_opnd,
                    lsenv_offset_of_mmx(lsenv, i));
        }
        latxs_append_ir2_opnd1(LISA_LABEL, &label_fpu);
    }
#else
    /* same as helper_enter_mmx */
    latxs_append_ir2_opnd2i(LISA_ST_W, &latxs_zero_ir2_opnd,
                            &latxs_env_ir2_opnd, lsenv_offset_of_top(lsenv));
    latxs_append_ir2_opnd2i(LISA_ST_D, &latxs_zero_ir2_opnd,
                            &latxs_env_ir2_opnd,
                            lsenv_offset_of_tag_word(lsenv));
    if (option_soft_fpu_fast) {
        /* transfer_to_mmx_mode */
        latxs_transfer_to_mmx_mode();
    }
#endif
}

#ifdef CONFIG_LATX_OPT_NOFP
/* Commit native FPRs before an instruction reads the architectural env. */
static void gen_nofp_sync_fprs_to_env(IR2_OPND *nofp)
{
    IR2_OPND done = latxs_ir2_opnd_new_label();

    latxs_append_ir2_opnd2i(LISA_LD_W, nofp, &latxs_env_ir2_opnd,
                            lsenv_offset_of_nofp(lsenv));
    latxs_append_ir2_opnd3(LISA_BEQ, nofp, &latxs_zero_ir2_opnd, &done);
    for (int i = 0; i < 8; i++) {
        IR2_OPND fpr = latxs_ra_alloc_mmx(i);
        latxs_append_ir2_opnd2i(LISA_FST_D, &fpr, &latxs_env_ir2_opnd,
                                lsenv_offset_of_mmx(lsenv, i));
    }
    latxs_append_ir2_opnd2i(LISA_ST_W, &latxs_zero_ir2_opnd,
                            &latxs_env_ir2_opnd, lsenv_offset_of_nofp(lsenv));
    latxs_append_ir2_opnd1(LISA_LABEL, &done);
}

void latxs_gen_nofp_sync_fprs_to_env(void)
{
    IR2_OPND nofp = latxs_ra_alloc_itemp();
    gen_nofp_sync_fprs_to_env(&nofp);
    latxs_ra_free_temp(&nofp);
    lsenv->tr_data->mmx_flag = 0;
}

/* Shared helper prologues preserve live temporaries and remember whether
 * their caller owns physical MMX registers. The matching epilogue restores
 * those registers even when it resumes in the middle of a translated TB. */
void latxs_gen_nofp_sync_fprs_to_env_static(void)
{
    IR2_OPND nofp = latxs_stmp1_ir2_opnd;
    if (hamt_enabled()) latxs_tr_dmw_in_fix_mem(&la_sp);
    la_out2i(LISA_ADDI_D, &la_sp, &la_sp, -LATXS_SCS_MMX_FRAME_SIZE);
    la_out2i(LISA_ST_D, &nofp, &la_sp, 8);
    la_out2i(LISA_LD_W, &nofp, &la_env, lsenv_offset_of_nofp(lsenv));
    la_out2i(LISA_ST_W, &nofp, &la_sp, 0);
    gen_nofp_sync_fprs_to_env(&nofp);
    la_out2i(LISA_LD_D, &nofp, &la_sp, 8);
    if (hamt_enabled()) latxs_tr_dmw_out_fix_mem(&la_sp);
}

void latxs_gen_nofp_restore_fprs_static(void)
{
    IR2_OPND nofp = latxs_stmp1_ir2_opnd;
    IR2_OPND done = latxs_ir2_opnd_new_label();
    if (hamt_enabled()) latxs_tr_dmw_in_fix_mem(&la_sp);
    la_out2i(LISA_LD_W, &nofp, &la_sp, 0);
    la_out3(LISA_BEQ, &nofp, &la_zero, &done);
    for (int i = 0; i < 8; i++) {
        IR2_OPND fpr = latxs_ra_alloc_mmx(i);
        la_out2i(LISA_FLD_D, &fpr, &la_env, lsenv_offset_of_mmx(lsenv, i));
    }
    la_out2i(LISA_ST_W, &nofp, &la_env, lsenv_offset_of_nofp(lsenv));
    la_label(done);
    la_out2i(LISA_ADDI_D, &la_sp, &la_sp, LATXS_SCS_MMX_FRAME_SIZE);
    if (hamt_enabled()) latxs_tr_dmw_out_fix_mem(&la_sp);
}

/* FXRSTOR/XRSTOR make env authoritative for subsequent MMX instructions. */
void latxs_gen_nofp_sync_fprs_from_env(void)
{
    latxs_append_ir2_opnd2i(LISA_ST_W, &latxs_zero_ir2_opnd,
                            &latxs_env_ir2_opnd,
                            lsenv_offset_of_nofp(lsenv));
    lsenv->tr_data->mmx_flag = 0;
}
#endif

void do_func(ADDRX addr, int id)
{
    return;
}
