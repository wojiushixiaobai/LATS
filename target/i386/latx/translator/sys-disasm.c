/*
 * SPDX-FileCopyrightText: 2024-2026 LATS Project Authors
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ir1-decode.h"
#include "common.h"
#include "cpu.h"
#include "lsenv.h"
#include "ir1.h"
#include "ir2.h"
#include "reg-alloc.h"
#include "latx-options.h"
#include "latx-config.h"
#include "translate.h"
#include "hamt-ptw.h"
#include "latx-sys-inst-ptn.h"
#include "sys-aot.h"
#include "sys-aot-tu-merge.h"
#include "exec/cputlb.h"
#include "trace.h"

#ifdef CONFIG_LATX_TU
#include <latx-tu.h>
#include <latx-tu-page-lock.h>
static __thread char *latxs_insn_info;
#else
static __thread char latxs_insn_info[MAX_IR1_NUM_PER_TB * IR1_INST_SIZE] = {0};
#endif

#define IR1_BYTES_BUF_SIZE (MAX_IR1_NUM_PER_TB * 15)
static __thread uint8_t latxs_ir1_bytes_buf[IR1_BYTES_BUF_SIZE];

const uint8_t *latxs_ir1_bytes(void)
{
    return latxs_ir1_bytes_buf;
}

static QemuMutex cs_lock;

static void __attribute__((__constructor__)) latxs_cs_lock_init(void)
{
    qemu_mutex_init(&cs_lock);
}
static inline int ir1_is_incsspd(const uint8_t *inst_cache)
{
    if (inst_cache[0] == 0xf3 && inst_cache[1] == 0x0f &&
        inst_cache[2] == 0xae && ((inst_cache[3] & 0xf8) == 0xe8)) {
        return 4;
    } else {
        return 0;
    }
}

static inline int ir1_is_incssp_rex(const uint8_t *inst_cache)
{
    if (inst_cache[0] == 0xf3 && ((inst_cache[1] & 0xf0) == 0x40) &&
        inst_cache[2] == 0x0f && inst_cache[3] == 0xae &&
        (((inst_cache[4] & 0xf8)) == 0xe8)) {
        return 5;
    } else {
        return 0;
    }
}

inline ADDRX latxs_ir1_disasm(IR1_INST *ir1,
        uint8_t *addr, ADDRX t_pc, int *error, int ir1_num)
{
//    qemu_mutex_lock_1(&cs_lock);
    TRANSLATION_DATA *td = lsenv->tr_data;
    ADDRX t_eip = t_pc - td->sys.cs_base; /* EIP = PC - CS_BASE */
    int count = 0;
    cs_insn *info = NULL;

    void *pir1_base = latxs_insn_info;
    if (td->sys.code64) {
        count = latxs_decode_one(handle64, addr, t_eip, 64,
                          &info, ir1_num, pir1_base);
    } else if (td->sys.code32) {
        count = latxs_decode_one(handle, addr, t_eip, 32,
                          &info, ir1_num, pir1_base);
    }
#ifdef CONFIG_SOFTMMU
    else {
        count = latxs_decode_one(handle16, addr, t_eip, 16,
                          &info, ir1_num, pir1_base);
    }
#endif

    if (count == 1 && info->id == X86_INS_LFENCE &&
        ((td->sys.code64 && ir1_is_incssp_rex(addr)) ||
         ir1_is_incsspd(addr))) {
        count = 0;
        info = NULL;
    }

//    qemu_mutex_unlock_1(&cs_lock);
    ir1->info = info;
    ir1->shbr_flags = 0;
    ir1->ghbr_opt = 0;
    ir1->shbr_restore_zero64_before = 0;
    ir1->shbr_restore_zero64_after = 0;
    ir1->shbr_restore_zero64_exit[0] = 0;
    ir1->shbr_restore_zero64_exit[1] = 0;

    /* Disasm error */
    if (count != 1) {
        *error = 1;
        /* for error insn assign ir1->info needed by
           latxs_ir1_make_ins_ILLEGAL */
		lsassert(ir1->info == NULL);
        ir1->info = pir1_base + (ir1_num * IR1_INST_SIZE);
        return 0;
    }

    if (trace_event_get_state_backends(TRACE_LATX_UNSUPPORTED)) {
        memset(info->bytes, 0, sizeof(uint64_t));
        memcpy(info->bytes, addr, MIN(info->size, sizeof(uint64_t)));
    }

    ir1->flags = 0;
    ir1->_eflag_def = 0;
    ir1->_eflag_use = 0;
    ir1->_eflag_opt = IR1_EFOPT_INIT;
#ifdef LATXS_INSTPTN_ENABLE
    ir1->instptn.opc  = INSTPTN_OPC_NONE;
    ir1->instptn.next = -1;
#endif
    latxs_fix_up_ir1(ir1);
    t_pc = ir1_addr(ir1) + td->sys.cs_base;
    return (ADDRX)(t_pc + ir1->info->size);
}

/* from target/i386/translate.c */
#define LATXS_X86_MAX_INSN_LENGTH 15

#include "accel/tcg/internal.h"

static void latxs_before_inst_fetch(CPUX86State *env,
        TranslationBlock *tb, ADDRX pc)
{
    tb_page_addr_t phys_page1 = tb_page_addr0(tb);
    if (unlikely(phys_page1 == -1)) {
        return;
    }

    ADDRX base_page = tb->pc & TARGET_PAGE_MASK;
    ADDRX curr_page = pc     & TARGET_PAGE_MASK;

    if (likely(base_page == curr_page)) {
        return;
    }

    tb_page_addr_t phys_page2_old;
    tb_page_addr_t phys_page2_new;

    TRANSLATION_DATA *td = lsenv->tr_data;
    if (td->is_tu_mode) {
        if (!cp_tu_cptb() ||
            td->disable_cross_page_tb ||
            tu_data->disable_cross_page_for_run ||
            (cp_tu_pages() && tu_data->pages_num >= cp_tu_page_num()))
        {
            /* avoid cross-apge tb in tu */
            td->deadlock_avoidance = -4;
            return;
        }
    }
    
    void *host_pc2 = td->host_addr[1];
    if (host_pc2 == NULL) {
        /*
         * TU exploration deliberately treats a missing successor page as a
         * non-fatal fallback.  Keep the same contract while decoding the TB:
         * the faulting code-page lookup must not enter the guest exception
         * path before deadlock_avoidance can abort this translation.
         */
        if (td->is_tu_mode) {
            phys_page2_new = get_page_addr_code_hostp_noexit(
                    env, curr_page, &host_pc2);
        } else {
            phys_page2_new = get_page_addr_code_hostp(
                    env, curr_page, &host_pc2);
        }
        td->host_addr[1] = host_pc2;

        /* 2nd page is MMIO */
        if (phys_page2_new == -1) {
            if (td->is_tu_mode) {
                td->deadlock_avoidance = -4;
                return;
            }
            tb_unlock_ctx_pages(1);
            tb_set_page_addr0(tb, -1);
            return;
        }

        phys_page2_old = tb_page_addr1(tb);
        if (phys_page2_new != phys_page2_old) {
            if (td->is_tu_mode && cp_tu_pages()) {
                int lock_result = tu_lock_page(curr_page, phys_page2_new,
                                               NULL);

                if (lock_result == TU_LOCK_RESTART_LOCK_ORDER) {
                    td->deadlock_avoidance = -5;
                    return;
                }
                if (lock_result == TU_LOCK_NEED_PROTECT) {
                    td->deadlock_avoidance = TU_DECODE_NEED_PROTECT;
                    return;
                }
                if (lock_result == TU_LOCK_FALLBACK) {
                    td->deadlock_avoidance = -4;
                    return;
                }
            } else {
#ifdef CONFIG_SOFTMMU
                if (!tlb_code_page_is_protected(phys_page2_new)) {
                    do {
                        tb_unlock_ctx_pages(1);
                        tlb_protect_code(phys_page2_new);

                        tb_lock_page0(phys_page1);
                        tcg_ctx->pre_locked_page0 = phys_page1;
                        tb_lock_page1(phys_page1, phys_page2_new);
                        tcg_ctx->pre_locked_page1 = phys_page2_new;
                    } while (!tlb_code_page_is_protected(phys_page2_new));

                    tb_set_page_addr1(tb, phys_page2_new);
                    td->deadlock_avoidance = __GENCODE_ERR_PAGE_LOCK_RETRY;
                    return;
                }
#endif
                if (phys_page2_old != -1) {
                    tb_unlock_page1(phys_page1, phys_page2_old);
                    tcg_ctx->pre_locked_page1 = -1;
                }
            }
            tb_set_page_addr1(tb, phys_page2_new);
#ifdef CONFIG_SOFTMMU
            tlb_protect_code(phys_page2_new);
#endif
            if (!(td->is_tu_mode && cp_tu_pages())) {
                tb_lock_page1(phys_page1, phys_page2_new);
                tcg_ctx->pre_locked_page1 = phys_page2_new;
            }
        }
    }
}

/* Function inside X86toMIPS to read target's code */
uint8_t latxs_cpu_read_code_via_qemu(CPUX86State *env, ADDRX pc)
{
    latxs_before_inst_fetch(env, lsenv->tr_data->curr_tb, pc);
    if (lsenv->tr_data->is_tu_mode &&
            lsenv->tr_data->deadlock_avoidance) {
        return 0;
    }
    return cpu_ldub_code(env, (target_ulong)pc);
}

static inline int ir1_is_rdsspd(uint8_t *inst_cache)
{
    if (inst_cache[0] == 0xf3 && inst_cache[1] == 0x0f &&
        inst_cache[2] == 0x1e && ((inst_cache[3] & 0xf8) == 0xc8)) {
        return 4;
    } else {
        return 0;
    }
}

static inline int ir1_is_rdssp_rex(uint8_t *inst_cache)
{
    if (inst_cache[0] == 0xf3 && ((inst_cache[1] & 0xf0) == 0x40) &&
        inst_cache[2] == 0x0f && inst_cache[3] == 0x1e &&
        (((inst_cache[4] & 0xf8)) == 0xc8)) {
        return 5;
    } else {
        return 0;
    }
}

static int is_prefix(uint8_t b)
{
    switch (b) {
    case 0xf3: return 1; /* F3H - rep/repe/repz */
    case 0xf2: return 1; /* F2H - repne/repnz */
    case 0xf0: return 1; /* FOH - lock */
    case 0x26: return 1; /* 26H - ES segment override */
    case 0x2e: return 1; /* 2EH - CS segment override / branch not taken */
    case 0x36: return 1; /* 36H - SS segment override */
    case 0x3e: return 1; /* 3EH - DS segment override / branch taken */
    case 0x64: return 1; /* 64H - FS segment override */
    case 0x65: return 1; /* 65H - GS segment override */
    case 0x66: return 1; /* operand size override */
    case 0x67: return 1; /* address size override */
    case 0x40 ... 0x4f: return 1;
    default: break;
    }
    return 0;
}

static bool ir1_0fae_missing_modrm(const uint8_t *bytes, size_t available,
                                   bool code64)
{
    size_t i = 0;
    while (i < available && i < 14) {
        uint8_t b = bytes[i];
        if (is_prefix(b) && (code64 || b < 0x40 || b > 0x4f)) {
            i++;
            continue;
        }
        break;
    }
    /* Two-byte opcode 0F AE always requires a trailing ModRM byte; without it
     * Capstone may misdecode a truncated F3 0F AE prefix at 0xffd as LFENCE. */
    return i + 2 == available && bytes[i] == 0x0f && bytes[i + 1] == 0xae;
}

static int ir1_gennop_size(const uint8_t *inst_cache)
{
    if (lsenv->tr_data->sys.code64 &&
        (ir1_is_rdssp_rex((uint8_t *)inst_cache) ||
         ir1_is_incssp_rex(inst_cache))) {
        return 5;
    }
    if (ir1_is_rdsspd((uint8_t *)inst_cache) ||
        ir1_is_incsspd(inst_cache)) {
        return 4;
    }
    if (inst_cache[0] == 0xf3 && inst_cache[1] == 0x0f &&
        inst_cache[2] == 0x1e && inst_cache[3] == 0xfb) {
        /* endbr32 */
        return 4;
    }
    return 0;
}

static void __latxs_disasm_do_error_sys(IR1_INST *pir1,
        uint8_t *inst_cache, ADDRX pc, ADDRX *next_pc, int *end_disasm)
{
    if (lsenv->tr_data->sys.code64 &&
        (ir1_is_rdssp_rex(inst_cache) || ir1_is_incssp_rex(inst_cache))) {
        latxs_ir1_make_ins_ILLEGAL(pir1, pc, 5, LATXS_IR1_FLAGS_GENNOP);
        *next_pc = pc + 5;
    } else if (ir1_is_rdsspd(inst_cache) || ir1_is_incsspd(inst_cache)) {
        latxs_ir1_make_ins_ILLEGAL(pir1, pc, 4, LATXS_IR1_FLAGS_GENNOP);
        *next_pc = pc + 4;
    } else if (inst_cache[0] == 0xf3 && inst_cache[1] == 0x0f &&
            inst_cache[2] == 0x1e && inst_cache[3] == 0xfb) {
        /* endbr32 */
        latxs_ir1_make_ins_ILLEGAL(pir1, pc, 4, LATXS_IR1_FLAGS_GENNOP);
        *next_pc = pc + 4;
    } else {
        TRANSLATION_DATA *td = lsenv->tr_data;
        if (td->ir1_nr && !(td->sys.cflags & CF_USE_ICOUNT)) {
            *end_disasm = 1;
        } else {
            int is = 0;
            while (is < 14 && is_prefix(inst_cache[is])) {
                is += 1;
            }
            lsassert(is < 14);
            trace_latx_undecodable(pc, inst_cache[0], inst_cache[1],
                                   inst_cache[2], inst_cache[3], inst_cache[4]);
            /* next_pc has no meaning if illegal instruction is detected */
            latxs_ir1_make_ins_ILLEGAL(pir1, pc, is + 1, LATXS_IR1_FLAGS_ILLOP);
        }
    }
}

static ADDRX aot_get_hva(TranslationBlock *tb, ADDRX pc)
{
    /* fprintf(stderr, "tb pc %lx phys_pc %lx hva %lx\n", */
    /*         tb->pc, tb_page_addr0(tb), tb->hva); */
    /* fprintf(stderr, " curr_hva %lx\n", */
    /*     tb->hva + ((tb_page_addr0(tb) + (pc - tb->pc)) & ~TARGET_PAGE_MASK)); */

    return tb->hva + ((tb_page_addr0(tb) + (pc - tb->pc)) & ~TARGET_PAGE_MASK);
}

#ifdef CONFIG_SOFTMMU
/* Success, return 0 */
static int __latxs_disasm_ont_ir1_directly(IR1_INST *pir1,
        ADDRX pc, ADDRX *next_pc, int ir1_info_index,
        int *end_disasm, TranslationBlock *tb)
{
    TRANSLATION_DATA *td = lsenv->tr_data;
    ADDR hva = 0;
    int error = 0;

    if (in_aot_translate) {
        hva = aot_get_hva(tb, pc);
    } else {
        hva = ((ADDR)td->host_addr[0] & TARGET_PAGE_MASK) + (pc & (~TARGET_PAGE_MASK));
    }


    *next_pc = latxs_ir1_disasm(pir1, (void *)hva, pc, &error,
                                ir1_info_index);

    if (error) {
        __latxs_disasm_do_error_sys(pir1, (void *)hva, pc, next_pc, end_disasm);
    }

    return 0;
}
#endif
#define GET_PAGE(addr) ((addr) & TARGET_PAGE_MASK)

bool inst_may_cross_page(ADDRX pc)
{
    /* at most 15 bytes: 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 */
    ADDRX pc_st_page = GET_PAGE(pc);
    ADDRX pc_ed_page = GET_PAGE(pc + LATXS_X86_MAX_INSN_LENGTH - 1);
    return pc_st_page != pc_ed_page;
}

/*
 * Disassemble one instruction according to target' pc.
 * IR1_INST will be filled in with the disasm result.
 *
 * @pir1: IR1_INST to be filled
 * @pc: target pc = cs_base + eip
 * @ir1_info_index: slot in the shared Capstone information buffer
 * @decode_cross_page_first: whether a cross-page instruction may be decoded
 *                           as the first instruction of this TB
 *
 * #PF: read code across pages
 * #ILLOP: disasm fail
 */
static inline ADDRX __latxs_disasm_one_ir1(IR1_INST *pir1, ADDRX pc,
        int ir1_info_index, bool decode_cross_page_first, int *end_disasm,
        TranslationBlock *tb)
{
    lsassert(pir1 != NULL);

    /* File-backed translation must never fall through to guest page tables. */
    if (aot_file_code_view) {
        uint8_t bytes[LATXS_X86_MAX_INSN_LENGTH] = { 0 };
        size_t available = 0;
        int error = 0;
        ADDRX next;

        while (available < sizeof(bytes)) {
            const uint8_t *page = aot_file_code_page(pc + available);

            if (!page) {
                break;
            }
            bytes[available] = page[(pc + available) & ~TARGET_PAGE_MASK];
            available++;
        }
        next = latxs_ir1_disasm(pir1, bytes, pc, &error, ir1_info_index);
        if (ir1_0fae_missing_modrm(bytes, available,
                                   lsenv->tr_data->sys.code64)) {
            aot_file_code_view->failed = true;
            *end_disasm = 1;
            return 0;
        }
        if (error) {
            __latxs_disasm_do_error_sys(pir1, bytes, pc, &next, end_disasm);
        }
        if (*end_disasm || !available ||
            latxs_ir1_inst_size(pir1) > available ||
            (error && pir1->flags != LATXS_IR1_FLAGS_GENNOP)) {
            aot_file_code_view->failed = true;
            *end_disasm = 1;
            return 0;
        }
        if (GET_PAGE(next - 1) != GET_PAGE(pc)) {
            if (!decode_cross_page_first) {
                *end_disasm = 1;
                return 0;
            }
            /* A file-view page address is a private key, never a RAM offset. */
            tb_set_page_addr1(tb, GET_PAGE(next - 1));
        }
        return next;
    }

    CPUX86State *env = lsenv->cpu_state;
    uint8_t inst[64] = { 0 };
    int len = 0;

#define LATXS_INST_FETCH(pc) \
    do { \
        if (in_aot_translate) { \
            inst[len++] = *(uint8_t *)aot_get_hva(tb, pc); \
        } else \
        { \
            inst[len++] = latxs_cpu_read_code_via_qemu(env, pc); \
        } \
    } while(0);


    ADDRX pc_st_page = GET_PAGE(pc);
    bool may_cross_page = inst_may_cross_page(pc);
#if !defined(CONFIG_SOFTMMU)
    may_cross_page = false; 
#endif

    ADDRX pc_fetch;
    ADDRX pc_next; /* Next PC after current instruction */

    int i = 0;
    int error = 0;

    if (may_cross_page) {
        pc_fetch = pc;
        while (GET_PAGE(pc_fetch) == pc_st_page) {
            LATXS_INST_FETCH(pc_fetch);
            pc_fetch += 1;
        }
        /*
         * at here:
         *
         * pc            pc_fetch
         * |             |
         * 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14
         * -------------|---------------------
         *     page 1   |   page 2
         *
         * len = 7
         * pc_fetch = pc + 7 = pc + len
         */
        pc_next = latxs_ir1_disasm(pir1, inst, pc, &error, ir1_info_index);
        if (ir1_0fae_missing_modrm(inst, len, lsenv->tr_data->sys.code64)) {
            error = 1;
        } else if (error) {
            int nop_size = ir1_gennop_size(inst);
            if (nop_size && nop_size <= len) {
                latxs_ir1_make_ins_ILLEGAL(pir1, pc, nop_size,
                                           LATXS_IR1_FLAGS_GENNOP);
                pc_next = pc + nop_size;
                error = 0;
            }
        }
        if (error || latxs_ir1_inst_size(pir1) > len) {
            /* this is a cross-page instruction */
            if (decode_cross_page_first) {
                /* is the first inst of this TB, do disasm */
                while (len < LATXS_X86_MAX_INSN_LENGTH) {
                    LATXS_INST_FETCH(pc_fetch);
                    pc_fetch += 1;
                }
                error = 0;
                pc_next = latxs_ir1_disasm(pir1, inst, pc, &error,
                                           ir1_info_index);
                TRANSLATION_DATA *td = lsenv->tr_data;
                if (td->deadlock_avoidance) {
                    *end_disasm = 1;
                    return 0;
                }
            } else {
#ifdef CONFIG_SOFTMMU
                /* not the first inst, do not disasm now */
                *end_disasm = 1;
                return 0;
#endif
            }
        }
    } else {
#ifdef CONFIG_SOFTMMU
        /*
         * This instruction must be in the same page,
         * we can use HVA to read its bytes dirtectly
         */
        if (!__latxs_disasm_ont_ir1_directly(pir1, pc, &pc_next,
            ir1_info_index, end_disasm, tb)) {
            return pc_next;
        }
#endif
        pc_fetch = pc;
        for(i = 0; i < 15; ++i) {
            LATXS_INST_FETCH(pc_fetch);
            pc_fetch += 1;
        }
        pc_next = latxs_ir1_disasm(pir1, inst, pc, &error, ir1_info_index);
    }

    if (error) {
        __latxs_disasm_do_error_sys(pir1, inst, pc, &pc_next, end_disasm);
    }
    lsenv->tr_data->ir1_nr = ir1_info_index;

    return pc_next;
}

void latxs_tr_disasm(TranslationBlock *tb)
{
    ADDRX pc;
    if (in_aot_translate) {
        /* pc = tb->hva + (tb->pc & ~TARGET_PAGE_MASK); */
        pc = tb->pc;
    } else {
        pc = (ADDRX)get_eip_cpu_state(tb);
    }
    // ADDRX pc = (ADDRX)tb->pc;

    int ir1_num = 0;
    IR1_INST *ir1_list = latxs_get_ir1_list(tb, pc, &ir1_num);

    TRANSLATION_DATA *td = lsenv->tr_data;

#ifdef CONFIG_LATX_TU
    tb->s_data->ir1 = ir1_list;
#endif
    td->ir1_inst_array = ir1_list;
    td->ir1_nr = ir1_num;
    td->curr_ir1_inst = NULL;

    tb->icount = ir1_num;
    if (ir1_list && ir1_num) {
        tb->size = ir1_addr_next(&(ir1_list[ir1_num-1])) -
                   ir1_addr(&(ir1_list[0]));
    }

    if (option_tb_record) {
        int offset = 0;
        for (int i = 0; i < ir1_num; i++) {
            IR1_INST *ir1 = &ir1_list[i];
            int inst_size = latxs_ir1_inst_size(ir1);
            memcpy(latxs_ir1_bytes_buf + offset,
                   ir1->info->bytes, inst_size);
            offset += inst_size;
        }
    }

    if (option_dump && qemu_log_in_addr_range(tb->pc)) {
        fprintf(stderr, "TB.ICOUNT   = %d\n", tb->icount);
    }
}

#ifdef CONFIG_LATX_TU
static __thread IR1_INST *latxs_ir1_list_rel;
#else
static __thread IR1_INST latxs_ir1_list [MAX_IR1_NUM_PER_TB] = {0};
#endif
inline IR1_INST *latxs_get_ir1_list(TranslationBlock *tb, ADDRX pc, int *p_ir1_num)
{
    lsassert(lsenv->cpu_state != NULL);

    TRANSLATION_DATA *td = lsenv->tr_data;

#ifdef CONFIG_LATX_TU
    /* TODO */
    //IR1_INST *ir1_list = (IR1_INST *)mm_calloc(max_insns, sizeof(IR1_INST));
    uint32_t *ir1_num_in_tu = &(tu_data->ir1_num_in_tu);
    if (tb->s_data->tu_tb_mode == TB_GEN_CODE) {
        *ir1_num_in_tu = 0;
    }
    if (latxs_ir1_list_rel == NULL) {
        latxs_ir1_list_rel = mmap(NULL, MAX_IR1_IN_TU * sizeof(IR1_INST), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (latxs_ir1_list_rel == MAP_FAILED) {
            fprintf(stderr, "latxs_ir1_list_rel mmap failed\n");
        }
    }
    if (latxs_insn_info == NULL) {
        latxs_insn_info = mmap(NULL, MAX_IR1_IN_TU * IR1_INST_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (latxs_insn_info == MAP_FAILED) {
            fprintf(stderr, "latxs_insn_info mmap failed\n");
        }
    }
    IR1_INST *latxs_ir1_list = latxs_ir1_list_rel + (*ir1_num_in_tu);
#endif

    IR1_INST *pir1 = latxs_ir1_list;
    int ir1_num = 0;

    int end_disasm = 0;

    do {
        /* disasemble this instruction */
        pir1 = &latxs_ir1_list[ir1_num];
        end_disasm = 0;
#ifdef CONFIG_LATX_TU
        int ir1_info_index = *ir1_num_in_tu + ir1_num;
#else
        int ir1_info_index = ir1_num;
#endif
        bool decode_cross_page_first = ir1_num == 0;
#ifdef CONFIG_LATX_TU
        /*
         * With cross-page TBs disabled, only the TU entry may fall back to
         * decoding a first instruction across the page boundary.  A later
         * explore TB must stop at the boundary without discarding the TU.
         */
        if (td->is_tu_mode && ir1_info_index != 0 && !cp_tu_cptb()) {
            decode_cross_page_first = false;
        }
#endif
        pc = __latxs_disasm_one_ir1(pir1, pc, ir1_info_index,
                                    decode_cross_page_first, &end_disasm, tb);

        if (end_disasm) {
            if (td->is_tu_mode && td->deadlock_avoidance != 0) {
                return NULL;
            }
            if (ir1_num == 0) {
                return NULL;
            }
            pir1 = &latxs_ir1_list[ir1_num - 1];
            td->sys_eob_pir1 = pir1;
            lsenv->tr_data->sys_eob_can_link = 1;
            lsenv->tr_data->sys_eob_is_indir = 1;
            lsenv->tr_data->sys_eob_use_fsht = 1;
            break;
        }
        ir1_num++;

        if (latxs_ir1_is_illegal(pir1) &&
                pir1->flags != LATXS_IR1_FLAGS_GENNOP) {
#ifdef CONFIG_LATX_TU
            tb->s_data->tu_tb_mode = TU_TB_MODE_BROKEN;
            tb->s_data->x86_next_pc = tb->pc;
#endif
            break;
        }

#ifdef CONFIG_SOFTMMU
        /*
         * special end of block in system-mode
         * > special system instructions
         * > system eob: inhibit irq, icount, cross page
         * > breakpoint
         */
        if (latxs_ir1_is_eob_in_sys(pir1)) {
            /*
             * the cross-page instruction should format one TB.
             * 
             *           +-------+
             *  +------- | inst0 |
             *  |        |       |
             *  V        +-------+
             * TB1 <---- | inst1 |
             *           +-------+
             *           |       |
             * TB2 <---- | inst2 | <----- page boundary
             *           |       |
             *           +-------+
             * TB3 <---- | inst3 |
             *           +-------+
             */
            if (latxs_ir1_is_cross_page_eip(pir1, tb->cs_base)) {
                /*
                 *      when ir1_num = 2
                 * +---+---+---+---+
                 * | 0 | 1 | 2 |   |
                 * +---+---+---+---+
                 *       ^
                 *       pir1 points here
                 */
                if (ir1_num > 1) {
                    ir1_num -= 1;
                    pir1 = &latxs_ir1_list[ir1_num - 1];
                }
            }
            td->sys_eob_pir1 = pir1;
            lsenv->tr_data->sys_eob_can_link = 1;
            lsenv->tr_data->sys_eob_is_indir = 1;
            break;
        }
#endif

        /* check if TB is too large */
        if (ir1_num == lsenv->tr_data->max_insns &&
                !ir1_is_tb_ending(pir1)) {
            td->tb_too_large_pir1 = pir1;
            break;
        }

    } while (!ir1_is_tb_ending(pir1));

    *p_ir1_num = ir1_num;
#if defined(CONFIG_LATX_TU) || defined(CONFIG_LATX_AOT)
    if (td->is_tu_mode) {
        get_last_info(tb, &latxs_ir1_list[ir1_num - 1]);
        *ir1_num_in_tu += ir1_num;
    }
#endif
    return latxs_ir1_list;
}
