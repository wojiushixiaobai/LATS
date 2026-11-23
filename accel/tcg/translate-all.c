/*
 *  Host code generation
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"

#include "trace.h"
#include "disas/disas.h"
#include "exec/exec-all.h"
#include "tcg/tcg.h"
#if defined(CONFIG_USER_ONLY)
#include "qemu.h"
#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
#include <sys/param.h>
#if __FreeBSD_version >= 700104
#define HAVE_KINFO_GETVMMAP
#define sigqueue sigqueue_freebsd  /* avoid redefinition */
#include <sys/proc.h>
#include <machine/profile.h>
#define _KERNEL
#include <sys/user.h>
#undef _KERNEL
#undef sigqueue
#include <libutil.h>
#endif
#endif
#else
#include "exec/ram_addr.h"
#endif

#include "exec/cputlb.h"
#include "exec/translate-all.h"
#include "exec/translator.h"
#include "exec/tb-flush.h"
#include "qemu/bitmap.h"
#include "qemu/qemu-print.h"
#include "qemu/main-loop.h"
#include "qemu/cacheinfo.h"
#include "qemu/timer.h"
#include "exec/log.h"
#include "sysemu/cpus.h"
#include "sysemu/cpu-timers.h"
#include "sysemu/tcg.h"
#include "qapi/error.h"
#include "hw/core/tcg-cpu-ops.h"
#include "tb-jmp-cache.h"
#include "tb-hash.h"
#include "tb-context.h"
#include "internal.h"
#include "perf.h"
#include "tcg/insn-start-words.h"

#include "latx/include/latx-config.h"
#include "latx/include/translate.h"
#include "latx/include/latx-options.h"
#include "latx/include/latx-shadow-htable.h"
#include "latx/include/latx-ras.h"
#include "latx/include/latx-misc.h"
#include "latx/include/lsenv.h"
#include "latx/include/latx-sys-mmio.h"
#include "latx/include/sys-aot.h"
#include "latx/tb-record.h"
#ifdef CONFIG_LATX_TU
#include "latx/include/latx-tu.h"
#endif
#include "hamt-va.h"
#include "hamt-ptw.h"
#include "latx-hack.h"
#include "latx-sigrt-sys.h"

TBContext tb_ctx;

#ifdef CONFIG_LATX_PROFILER
int latx_tb_profile_split_sort;
int latx_tb_profile_enabled;
void latx_tb_profile_print_and_clear(int tbbytime);
uint64_t latx_profile_cpu_halt_time[16];
uint64_t latx_profile_cpu_time[2]; // 0 = st, 1 = ed
#define CPU_PRO_TIME (latx_profile_cpu_time[1]-latx_profile_cpu_time[0])
#define CPU_HLT_TIME(n) (latx_profile_cpu_halt_time[n])
#define RESET_CPU_HLT_TIME()  do {    \
    memset(latx_profile_cpu_halt_time, 0, sizeof(uint64_t) * 16);   \
} while (0)
#endif

uint32_t fsht_num;

//   70626 0x40c090 : OXFXSR(22) CS64(15) LMA(14) PE(7)         CS32(4) CPL=0
//  113468 0x4040b3 : OXFXSR(22)          LMA(14) PE(7) SS32(5) CS32(4) CPL=3
//  336732 0x40c0b0 : OXFXSR(22) CS64(15) LMA(14) PE(7) SS32(5) CS32(4) CPL=0
// 2419019 0x40c0b3 : OXFXSR(22) CS64(15) LMA(14) PE(7) SS32(5) CS32(4) CPL=3
flaged_shadow_hash_table_t fsht[32];
uint64_t fsht_global_base;
uint8_t fsht_flag_ids[FSHT_FLAG_KEY_SIZE];

void flaged_shadow_htable_init(void)
{
    if (!option_flaged_sht) return;

    if (hamt_enabled())
        fsht_num = (!option_aot_save && !option_aot_load && !option_sys_aot)
            ? 15 : 14;
    else
        fsht_num = 7;
    g_assert(fsht_num < 16);

    fsht[0].flags = 0x40c0b3;
    fsht[1].flags = 0x40c0b0;
    fsht[2].flags = 0x4040b3;
    fsht[3].flags = 0x40c090;

    fsht[4].flags = 0x40c2b3;
    fsht[5].flags = 0x40c2b0;
    fsht[6].flags = 0x40c290;
    if (hamt_enabled()) {
        fsht[7].flags = 0x4040c0b3;
        fsht[8].flags = 0x4040c0b0;
        fsht[9].flags = 0x404040b3;
        fsht[10].flags = 0x4040c090;

        fsht[11].flags = 0x4040c2b3;
        fsht[12].flags = 0x4040c2b0;
        fsht[13].flags = 0x4040c290;
        /* CPL3, CS64, HAMT and the full AVX/SMAP restore workload flags. */
        fsht[14].flags = fsht[11].flags | HF_AVX_EN_MASK | HF_SMAP_MASK;
    }
    void *d = NULL;
//  fsht[0].base     = 0x9900000000ULL; // = base + (0 << 28)
//  fsht[1].base     = 0x9910000000ULL; // = base + (1 << 28)
//  fsht[2].base     = 0x9920000000ULL; // = base + (2 << 28)
//  fsht[3].base     = 0x9930000000ULL; // = base + (3 << 28)
//  fsht[4].base     = 0x9940000000ULL; // = base + (4 << 28)
//  fsht[5].base     = 0x9950000000ULL; // = base + (5 << 28)
//  fsht[6].base     = 0x9960000000ULL; // = base + (6 << 28)
    fsht_global_base = 0x9900000000ULL;

    for (uint64_t i = 0; i < fsht_num; ++i) {
        unsigned key = fsht_flags_key(fsht[i].flags);

        /* A new flag set must keep the key collision-free. */
        g_assert(fsht_flag_ids[key] == 0);
        fsht_flag_ids[key] = i + 1;
        fsht[i].base = fsht_global_base + ((i % 16) << 28);
#if defined(CONFIG_SOFTMMU)
        if (is_host_kernel_48_va()) {
            fsht[i].base = fsht[i].base | HAMT_7F;
        }
#endif
        d = mmap((void *)fsht[i].base, FSHT_SIZE,
                PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
        g_assert(d == (void *)fsht[i].base);

#if defined(CONFIG_LATX_PRINT)
        printf("%s:%d FLAGED_SHT at %p flags = %x\n", __func__, __LINE__,
                d, fsht[i].flags);
#endif
    }
}

/* Publishers on different pages can mark the same table concurrently.
 * Lookup reinsertions reuse published TBs; flush runs with vCPUs stopped. */
static void flaged_shadow_htable_clear(unsigned fid)
{
    if (!option_arch_opt || qatomic_read(&fsht[fid].dirty)) {
        memset((void *)fsht[fid].base, 0, FSHT_SIZE);
        qatomic_set(&fsht[fid].dirty, false);
    }
}

void flaged_shadow_htable_reset(void)
{
    if (!option_flaged_sht) return;

    for (int i = 0; i < fsht_num; ++i) {
        flaged_shadow_htable_clear(i);
    }
}

void flaged_shadow_htable_reset_mtcc(int n)
{
    if (!option_flaged_sht) return;

    int cpl = -1;

    for (int i = 0; i < fsht_num; ++i) {
        cpl = fsht[i].flags & HF_CPL_MASK;
        if ((n == 0 && cpl == 0) ||
            (n == 1 && cpl == 3)) {
            flaged_shadow_htable_clear(i);
        }
    }
}

void flaged_shadow_htable_insert(void *p)
{
    if (!option_flaged_sht) return;

    TranslationBlock *tb = p;

    /* FSHT only validates the first physical page on lookup. */
    if (tb_page_addr1(tb) != -1 ||
        (tb_cflags(tb) & (CF_NO_GOTO_TB | CF_NO_GOTO_PTR))) {
        return;
    }

    int fid = fsht_flags_to_id(tb->flags);
    if (fid < 0) {
        return;
    }

    uint64_t *fshtdata = (void *)(fsht[fid].base);
#ifdef CONFIG_SOFTMMU
    int fshtindex = ((tb->pc >> 32) ^ (tb->page_addr[0])) & FSHT_MASK;
#else
    int fshtindex = ((tb->pc >> 32) ^ (tb->pc)) & FSHT_MASK;
#endif

    if (option_arch_opt) {
        qatomic_set(&fsht[fid].dirty, true);
    }
    fshtdata[fshtindex] = (uint64_t)tb;
}

void flaged_shadow_htable_remove(void *p)
{
    if (!option_flaged_sht) return;

    TranslationBlock *tb = p;

    int fid = fsht_flags_to_id(tb->flags);
    if (fid < 0) {
        return;
    }

    uint64_t *fshtdata = (void *)(fsht[fid].base);
#ifdef CONFIG_SOFTMMU
    int fshtindex = ((tb->pc >> 32) ^ (tb->page_addr[0])) & FSHT_MASK;
#else
    int fshtindex = ((tb->pc >> 32) ^ (tb->pc)) & FSHT_MASK;
#endif

    /*fshtdata[fshtindex] = NULL;*/
//    if (fshtdata[fshtindex] == tb)
//        fshtdata[fshtindex] = NULL;
    qatomic_cmpxchg((uint64_t *)&(fshtdata[fshtindex]),
                    (uint64_t)tb, NULL);
}

/*
 * Encode VAL as a signed leb128 sequence at P.
 * Return P incremented past the encoded value.
 */
static uint8_t *encode_sleb128(uint8_t *p, int64_t val)
{
    int more, byte;

    do {
        byte = val & 0x7f;
        val >>= 7;
        more = !((val == 0 && (byte & 0x40) == 0)
                 || (val == -1 && (byte & 0x40) != 0));
        if (more) {
            byte |= 0x80;
        }
        *p++ = byte;
    } while (more);

    return p;
}

/*
 * Decode a signed leb128 sequence at *PP; increment *PP past the
 * decoded value.  Return the decoded value.
 */
static int64_t decode_sleb128(const uint8_t **pp)
{
    const uint8_t *p = *pp;
    int64_t val = 0;
    int byte, shift = 0;

    do {
        byte = *p++;
        val |= (int64_t)(byte & 0x7f) << shift;
        shift += 7;
    } while (byte & 0x80);
    if (shift < TARGET_LONG_BITS && (byte & 0x40)) {
        val |= -(int64_t)1 << shift;
    }

    *pp = p;
    return val;
}

/* Encode the data collected about the instructions while compiling TB.
   Place the data at BLOCK, and return the number of bytes consumed.

   The logical table consists of TARGET_INSN_START_WORDS target_ulong's,
   which come from the target's insn_start data, followed by a uintptr_t
   which comes from the host pc of the end of the code implementing the insn.

   Each line of the table is encoded as sleb128 deltas from the previous
   line.  The seed for the first line is { tb->pc, 0..., tb->tc.ptr }.
   That is, the first column is seeded with the guest pc, the last column
   with the host pc, and the middle columns with zeros.  */

static int encode_search(TranslationBlock *tb, uint8_t *block)
{
    uint8_t *highwater = tcg_ctx->code_gen_highwater;
    uint64_t *insn_data = tcg_ctx->gen_insn_data;
    uint16_t *insn_end_off = tcg_ctx->gen_insn_end_off;
    uint8_t *p = block;
    int i, j, n;

    for (i = 0, n = tb->icount; i < n; ++i) {
        uint64_t prev, curr;

        for (j = 0; j < TARGET_INSN_START_WORDS; ++j) {
            if (i == 0) {
                prev = (!(tb_cflags(tb) & CF_PCREL) && j == 0 ? tb->pc : 0);
            } else {
                prev = insn_data[(i - 1) * TARGET_INSN_START_WORDS + j];
            }
            curr = insn_data[i * TARGET_INSN_START_WORDS + j];
            p = encode_sleb128(p, curr - prev);
        }
        prev = (i == 0 ? 0 : insn_end_off[i - 1]);
        curr = insn_end_off[i];
        p = encode_sleb128(p, curr - prev);

        /* Test for (pending) buffer overflow.  The assumption is that any
           one row beginning below the high water mark cannot overrun
           the buffer completely.  Thus we can test for overflow after
           encoding a row without having to check during encoding.  */
        if (unlikely(p > highwater)) {
            return -1;
        }
    }

    return p - block;
}

static int cpu_unwind_data_from_tb(TranslationBlock *tb, uintptr_t host_pc,
                                   uint64_t *data)
{
    uintptr_t iter_pc = (uintptr_t)tb->tc.ptr;
    int i, j, num_insns = tb->icount;
#if defined(CONFIG_LATX_TCG_MODE)
    const uint8_t *p = tb->tc.ptr + tb->tc.size;
    if (qemu_tcg_mode_1_tcg()) {
        host_pc -= GETPC_ADJ;
    } else {
        host_pc += GETPC_ADJ;
    }
#else
    const uint8_t *p;
    if (option_split_search) {
        p = tb->search_ptr;
    } else {
        if (tb_is_tu(tb)) {
            p = tb->search_ptr;
        } else {
            p = tb->tc.ptr + tb->ir2_num * 4;
        }
    }
    host_pc += GETPC_ADJ;
#endif

    if (host_pc < iter_pc) {
        return -1;
    }

    memset(data, 0, sizeof(uint64_t) * TARGET_INSN_START_WORDS);
    if (!(tb_cflags(tb) & CF_PCREL)) {
        data[0] = tb->pc;
    }

    /*
     * Reconstruct the stored insn data while looking for the point
     * at which the end of the insn exceeds host_pc.
     */
    for (i = 0; i < num_insns; ++i) {
        for (j = 0; j < TARGET_INSN_START_WORDS; ++j) {
            data[j] += decode_sleb128(&p);
        }
        iter_pc += decode_sleb128(&p);
        if (iter_pc > host_pc) {
            return num_insns - i;
        }
    }
    return -1;
}

/*
 * The cpu state corresponding to 'host_pc' is restored in
 * preparation for exiting the TB.
 */
void cpu_restore_state_from_tb(CPUState *cpu, TranslationBlock *tb,
                               uintptr_t host_pc)
{
    uint64_t data[TARGET_INSN_START_WORDS];
    int insns_left = cpu_unwind_data_from_tb(tb, host_pc, data);

    if (insns_left < 0) {
        return;
    }

    if (tb_cflags(tb) & CF_USE_ICOUNT) {
        assert(icount_enabled());
        /*
         * Reset the cycle counter to the start of the block and
         * shift if to the number of actually executed instructions.
         */
        cpu_neg(cpu)->icount_decr.u16.low += insns_left;
    }

    CPUArchState *env = cpu->env_ptr;
    if (option_pushpop && (tb->flags & HF_CPL_MASK)) {
        int *opt = tb_opt_ptr(tb);
        int exec_nums = tb->icount - insns_left;

        if (opt && opt[exec_nums]) {
            env->regs[R_ESP] += opt[exec_nums];
        }
    }
    cpu->cc->tcg_ops->restore_state_to_opc(cpu, tb, data);
}

bool cpu_restore_state(CPUState *cpu, uintptr_t host_pc)
{
    /*
     * The host_pc has to be in the rx region of the code buffer.
     * If it is not we will not be able to resolve it here.
     * The two cases where host_pc will not be correct are:
     *
     *  - fault during translation (instruction fetch)
     *  - fault from helper (not using GETPC() macro)
     *
     * Either way we need return early as we can't resolve it here.
     */
    if (in_code_gen_buffer((const void *)(host_pc - tcg_splitwx_diff))) {
        TranslationBlock *tb = tcg_tb_lookup(host_pc);
        if (tb) {
            cpu_restore_state_from_tb(cpu, tb, host_pc);
            return true;
        }
    }
    return false;
}

bool cpu_unwind_state_data(CPUState *cpu, uintptr_t host_pc, uint64_t *data)
{
    if (in_code_gen_buffer((const void *)(host_pc - tcg_splitwx_diff))) {
        TranslationBlock *tb = tcg_tb_lookup(host_pc);
        if (tb) {
            return cpu_unwind_data_from_tb(tb, host_pc, data) >= 0;
        }
    }
    return false;
}

void page_init(void)
{
    page_size_init();
    page_table_config_init();
}

/*
 * Isolate the portion of code gen which can setjmp/longjmp.
 * Return the size of the generated code, or negative on error.
 */
static int setjmp_gen_code(CPUArchState *env, TranslationBlock *tb,
                           vaddr pc, void *host_pc,
                           int *max_insns, int64_t *ti)
{
    int ret = sigsetjmp(tcg_ctx->jmp_trans, 0);
    if (unlikely(ret != 0)) {
        return ret;
    }

    tcg_func_start(tcg_ctx);

    tcg_ctx->cpu = env_cpu(env);
    gen_intermediate_code(env_cpu(env), tb, max_insns, pc, host_pc);
    assert(tb->size != 0);
    tcg_ctx->cpu = NULL;
    *max_insns = tb->icount;

    return tcg_gen_code(tcg_ctx, tb, pc);
}

inline void latxs_tb_reset(TranslationBlock *tb)
{
    tb->jmp_reset_offset[0] = TB_JMP_OFFSET_INVALID;
    tb->jmp_reset_offset[1] = TB_JMP_OFFSET_INVALID;
    tb->jmp_insn_offset[0] = TB_JMP_OFFSET_INVALID;
    tb->jmp_insn_offset[1] = TB_JMP_OFFSET_INVALID;
    tb->jmp_cp_data[0] = 0;
    tb->jmp_cp_data[1] = 0;
#ifdef CONFIG_LATX_TU
    tb->s_data->tu_generation_offset_patch[0] = TB_JMP_OFFSET_INVALID;
    tb->s_data->tu_generation_offset_patch[1] = TB_JMP_OFFSET_INVALID;
    tb->jit_tucheck_external[0] = NULL;
    tb->jit_tucheck_external[1] = NULL;
#endif

    tb->next_pc[0] = 0;
    tb->next_pc[1] = 0;
    tb->link_opt[0] = 0;
    tb->link_opt[1] = 0;
    tb->int_opt[0] = 0;
    tb->int_opt[1] = 0;
    tb->signal_unlink[0] = 0;
    tb->signal_unlink[1] = 0;
    tb->jmp_inst_hold[0] = 0;
    tb->jmp_inst_hold[1] = 0;
#ifdef CONFIG_LATX_TU
    tb->s_data->tu_cold_exit_offset[0] = TU_COLD_EXIT_INVALID;
    tb->s_data->tu_cold_exit_offset[1] = TU_COLD_EXIT_INVALID;
#endif
    LATXS_RAS_TP_COLD_OFFSET(tb) = 0;
    LATXS_RAS_TP_COLD_INSNS(tb) = 0;
    tb->label_pos_nocheck = 0;
}

int latx_setjmp_gen_code(CPUState *cpu,
        TranslationBlock *tb, vaddr pc, void *host_pc,
        int max_insns, int *search_size)
{
    latxs_tb_reset(tb);
    TRANSLATION_DATA *td = lsenv->tr_data;
    td->tb_too_large_pir1 = NULL;
    td->sys_eob_pir1 = NULL;

    return target_latxs_host(cpu, tb, max_insns,
            tcg_ctx->code_gen_highwater,
            search_size, host_pc);
}

#if !defined(CONFIG_TCG_REGION_TREE_ENABLE)
inline void tbmini_set_pointer(uint64_t* tbm, uint64_t tb_addr)
{
    *tbm = (tb_addr & MAKE_64BIT_MASK(0, HOST_VIRT_ADDR_SPACE_BITS)) |
                (TB_MAGIC << HOST_VIRT_ADDR_SPACE_BITS);
}
#endif


int get_max_insns(int cflags)
{
    int max_insns;
    max_insns = cflags & CF_COUNT_MASK;
    if (max_insns == 0) {
        max_insns = TCG_MAX_INSNS;
    }
    return max_insns;
}

/*
 * Discard the code space of a TB that lost the race to an existing TB.
 *
 * tcg_tb_alloc() may switch the context to a fresh code region, so the
 * caller samples the start only after that call.  The assertion documents
 * that invariant: a start from the previous region must never be used to
 * rewind code_gen_ptr, or later allocations would cross the region
 * boundary and overwrite the static-code prefix of the new region.
 */
static void lat_tb_discard_code(TCGContext *s, void *start)
{
    uintptr_t base = (uintptr_t)s->code_gen_buffer;
    uintptr_t ptr = (uintptr_t)start;

    g_assert(ptr >= base && ptr < base + s->code_gen_buffer_size);
    qatomic_set(&s->code_gen_ptr, start);
}

static TranslationBlock *lat_tb_alloc(CPUState *cpu,
        vaddr pc, uint64_t cs_base, uint32_t flags, int cflags,
        tb_page_addr_t phys_pc, TCGContext *tcg_ctx)
{
    CPUArchState *env = cpu->env_ptr;
    TranslationBlock *tb = tcg_tb_alloc(tcg_ctx);
    if (unlikely(!tb)) {
        return NULL;
    }

    void *gen_code_buf = tcg_ctx->code_gen_ptr;
#if !defined(CONFIG_TCG_REGION_TREE_ENABLE)
    uint64_t *tbm = (uint64_t *)gen_code_buf;
    tbmini_set_pointer(tbm, (uint64_t)tb);
    gen_code_buf = (tcg_insn_unit *)((uintptr_t)gen_code_buf + sizeof(struct TBMini));
#endif

    tb_set_opt_ptr(tb, NULL);
    TB_REL_START(tb) = -1;
    TB_REL_END(tb) = -1;
    tb->tc.ptr = tcg_splitwx_to_rx(gen_code_buf);
    tb->pc = pc;
    tb->cs_base = cs_base;
    tb->flags = flags;
    tb->cflags = cflags;
    tb->int_opt[0] = 0;
    tb->int_opt[1] = 0;
    tb->signal_unlink[0] = 0;
    tb->signal_unlink[1] = 0;
    tb->ptn.eflag_type = PTN_EFLAG_TYPE_INVALID;
#ifdef CONFIG_LATX_DEBUG
    tb->inhibit_irq_type = 0;
#endif
    clear_misc_flags(tb);

#ifdef CONFIG_LATX_TU
    tb->s_data->_top_out = -1;
    tb->s_data->_top_in = -1;
    tu_reset_tb(tb);
    tb->s_data->tu_tb_mode = TB_GEN_CODE;
    tb->bool_flags &= ~IS_TU_TB;
    TB_TU_PAGES(tb) = NULL;
#endif

    tb_set_page_addr0(tb, phys_pc);
    tb_next_data_reset(tb);
#if defined(CONFIG_SOFTMMU)
    if (option_fast_itlb) {
        tb->hva = phys_pc & TARGET_PAGE_MASK;
    } else {
        int error = 0;
        tb->hva = latxs_tr_cvt_cpu_addr_code(env, pc, &error);
    }
#else
    tb->hva = -1ULL;
#endif
    tb_set_page_addr1(tb, -1);

    set_mask_to_miscflags(tb, curr_mtcc(env),
                          MF_MTCC_ID_MASK, MF_MTCC_ID_SHIFT);
    tb->preorder_tb_offset = 0;
    tb->preorder_next = NULL;
    tb->inst_ptr_offset = 0;

#ifdef CONFIG_SOFTMMU
    latx_mmio_tb_check(tb);
#endif
#ifdef CONFIG_LATX_PROFILER
    CLN_TB_PROFILE(tb);
#endif

#if defined(CONFIG_TB_FLAGS_NO_EFLAGS)
    if (tb->flags == cpu_get_tb_cpu_state_flags_no_eflags(env)) {
        add_misc_flags(tb, MF_FLAGS_NOEFLAGS);
    }
#endif
    return tb;
}

static TranslationBlock *lat_tb_gen_code_locked_depth(CPUState *cpu,
        vaddr pc, uint64_t cs_base, uint32_t flags, int cflags,
        tb_page_addr_t phys_pc, void *host_pc, int *error, unsigned ras_depth);

/* Both source and return start on the already locked page. Bounded recursion
 * avoids a second publication list and cannot loop backwards in guest PC. */
static void ras_tp_prelink_unpublished(CPUState *cpu, TranslationBlock *source,
                                      tb_page_addr_t phys_pc, unsigned depth)
{
    vaddr pc = source->pc + source->size;
    if (depth == 16 || !source->inst_ptr_offset ||
        tb_page_addr1(source) != -1 || pc <= source->pc ||
        (pc & TARGET_PAGE_MASK) != (source->pc & TARGET_PAGE_MASK) ||
        inst_may_cross_page(pc)) return;
    TranslationBlock *target = tb_lookup(cpu, pc, source->cs_base,
                                         source->flags, tb_cflags(source));
    if (!target) {
        void *host = NULL;
        tb_page_addr_t phys = get_page_addr_code_hostp(cpu->env_ptr, pc, &host);
        /* No additional page lock may be acquired by this pretranslation. */
        if ((phys & TARGET_PAGE_MASK) != (phys_pc & TARGET_PAGE_MASK)) return;
        int error = 0;
        target = lat_tb_gen_code_locked_depth(cpu, pc, source->cs_base,
            source->flags, tb_cflags(source), phys, host, &error, depth + 1);
        if (error && error != GENCODE_ERR_TB_EXIST) {
            if (error == GENCODE_ERR_BUF_OVERFLOW) {
                qatomic_set(&tcg_ctx->code_gen_ptr,
                             tcg_ctx->code_gen_highwater + 1);
            }
            return;
        }
    }
    if (target) latxs_ras_tp_link_initial(cpu, source, target);
}

static TranslationBlock *lat_tb_gen_code_locked_depth(CPUState *cpu,
        vaddr pc, uint64_t cs_base, uint32_t flags, int cflags,
        tb_page_addr_t phys_pc, void *host_pc, int *error, unsigned ras_depth)
{
    CPUArchState *env = cpu->env_ptr;
    TranslationBlock *tb, *existing_tb;
    tb_page_addr_t phys_p2;
    int gen_code_size = 0, search_size, max_insns;

    max_insns = get_max_insns(cflags);
    uint32_t mtcc_id = curr_mtcc(env);
    tcg_ctx = tcg_ctx_mtcc[mtcc_id];

    tb = lat_tb_alloc(cpu, pc, cs_base, flags, cflags, phys_pc, tcg_ctx);
    if (!tb) {
        *error = GENCODE_ERR_FLUSH1; /* do tb flush */
        return NULL;
    }

    /* Sample after allocation: it may have switched to a new region. */
    void *code_gen_start = tcg_ctx->code_gen_ptr;
    tcg_ctx->addr_type = TARGET_LONG_BITS == 32 ? TCG_TYPE_I32 : TCG_TYPE_I64;
#ifdef CONFIG_SOFTMMU
    tcg_ctx->page_bits = TARGET_PAGE_BITS;
    tcg_ctx->page_mask = TARGET_PAGE_MASK;
    tcg_ctx->tlb_dyn_max_bits = CPU_TLB_DYN_MAX_BITS;
    tcg_ctx->tlb_fast_offset =
        (int)offsetof(ArchCPU, neg.tlb.f) - (int)offsetof(ArchCPU, env);
#endif

restart_translate:
    trace_translate_block(tb, pc, tb->tc.ptr);
    curr_rel_entry_num = 0;
    gen_code_size = latx_setjmp_gen_code(cpu, tb, pc, host_pc, max_insns, &search_size);
    if (unlikely(gen_code_size < 0)) {
        switch (gen_code_size) {
        case __GENCODE_ERR_BUFFER_OVERFLOW:
            *error = GENCODE_ERR_BUF_OVERFLOW;
            return tb;
        case __GENCODE_ERR_TB_TOO_LARGE:
            max_insns /= 2;
            phys_p2 = tb_page_addr1(tb);
            if (unlikely(phys_p2 != -1)) {
                tb_unlock_page1(phys_pc, phys_p2);
                tcg_ctx->pre_locked_page1 = -1;
                tb_set_page_addr1(tb, -1);
            }
            goto restart_translate;
        case __GENCODE_ERR_PAGE_LOCK_RETRY:
            goto restart_translate;
        case __GENCODE_ERR_AUX_DATA_OVERFLOW:
            *error = GENCODE_ERR_FLUSH2;
            return tb;
        default:
            g_assert_not_reached();
        }
    }
    tb->ir2_num = gen_code_size >> 2;
#if defined(CONFIG_LATX_TCG_MODE) || defined(CONFIG_TCG_REGION_TREE_ENABLE)
    tb->tc.size = gen_code_size;
#endif
    uintptr_t sptr = (uintptr_t)(tb->tc.ptr) + gen_code_size;
    qatomic_set(&tcg_ctx->code_gen_ptr, (void *)
        ROUND_UP(sptr + search_size, CODE_GEN_ALIGN));

    /* init jump list */
    qemu_spin_init(&tb->jmp_lock);
    tb->jmp_list_head = (uintptr_t)NULL;
    tb->jmp_list_next[0] = (uintptr_t)NULL;
    tb->jmp_list_next[1] = (uintptr_t)NULL;
    tb->jmp_dest[0] = (uintptr_t)NULL;
    tb->jmp_dest[1] = (uintptr_t)NULL;

    /* init original jump addresses which have been set during tcg_gen_code() */
    if (tb->jmp_reset_offset[0] != TB_JMP_OFFSET_INVALID) {
        latx_tb_reset_jmp_target(tb, 0);
    }
    if (tb->jmp_reset_offset[1] != TB_JMP_OFFSET_INVALID) {
        latx_tb_reset_jmp_target(tb, 1);
    }

    if (!option_split_search) {
        tb->search_size = search_size;
    }

    if (tb_page_addr0(tb) == -1) {
        /* ont-shot TB, do not insert it */
        *error = GENCODE_ERR_TB_ONESHOT;
#if DEBUG_AOT
        one_ins_tb++;
#endif
        return tb;
    }

#ifdef CONFIG_LATX_TU
    if (tucp_fast_needs_pages_desc() &&
        (flags & HF_CPL_MASK) == HF_CPL_MASK &&
        !(flags & HF_FORCE_TB_MODE)) {
        TB_TU_PAGES(tb) = tb_build_pages_desc(tcg_ctx, tb);
        if (!TB_TU_PAGES(tb)) {
            *error = GENCODE_ERR_FLUSH1;
            return tb;
        }
    }
#endif

    if (option_rastoptp) {
        /* Child translation overwrites translation globals. Finish their
         * use before recursively preparing the unpublished source. */
        add_rel_entry(tb);
        ras_tp_prelink_unpublished(cpu, tb, phys_pc, ras_depth);
    }
    tcg_tb_insert(tb);
    existing_tb = tb_link_page(tb, false);
    if (existing_tb != tb) {
        *error = GENCODE_ERR_TB_EXIST;
        if (option_rastoptp) {
            /* Later return TBs may already be published in this buffer. */
            qatomic_or(&tb->cflags, CF_INVALID);
            latxs_ras_unlink(tb);
        } else {
            lat_tb_discard_code(tcg_ctx, code_gen_start);
        }
        tcg_tb_remove(tb);
        return existing_tb;
    }
    return tb;
}

TranslationBlock *lat_tb_gen_code_locked(CPUState *cpu,
        vaddr pc, uint64_t cs_base, uint32_t flags, int cflags,
        tb_page_addr_t phys_pc, void *host_pc, int *error)
{
    return lat_tb_gen_code_locked_depth(cpu, pc, cs_base, flags, cflags,
                                        phys_pc, host_pc, error, 0);
}

static TranslationBlock *lat_ras_pretranslate_one(TranslationBlock *curr,
        CPUState *cpu,
        uint64_t pcnext, uint64_t cs_base, uint32_t flags, uint32_t cflags)
{
    CPUX86State *env = cpu->env_ptr;
    TranslationBlock *next;
    Soft_RAS *ras = cpu->s_ras;
    tb_page_addr_t phys_pc;
    void *host = NULL;
    int error = 0;

    ras->patching_tb = curr;

    tcg_ctx->gen_tb = NULL;
    next = tb_lookup(cpu, pcnext, cs_base, flags, cflags);

    if (next == NULL) {
        phys_pc = get_page_addr_code_hostp(env, pcnext, &host);
        next = lat_tb_gen_code_locked(cpu,
                pcnext, cs_base, flags, cflags,
                phys_pc, host, &error);
        if (!error) {
            add_rel_entry(next);
        } else if (error != GENCODE_ERR_TB_EXIST) {
            next = NULL;
        }
    }

    if (next) {
        if (check_misc_flags(next, MF_FLAGS_NOEFLAGS) &&
            tu_jmp_cache_can_insert(cpu, next)) {
            tb_jmp_cache_insert(cpu, next->pc, false, next);
        }
        if (!latxs_ras_link(curr, next)) {
            latxs_ras_unlink(curr);
        }
    } else {
        latxs_ras_unlink(curr);
    }

    if (option_sys_aot) {
        curr->s_data->x86_next_pc = pcnext;
    }

    ras->patching_tb = NULL;
    return next;
}

static inline bool tb_ras_unhandled(TranslationBlock *tb)
{
    return tb && tb->inst_ptr_offset &&
        !check_misc_flags(tb, MF_RAS_CLEAR_B0) &&
        !check_misc_flags(tb, MF_RAS_LINKED);
}

static void lat_ras_pretranslate_locked(CPUState *cpu, TranslationBlock *tb,
        tb_page_addr_t phys_pc, void *host_pc)
{
    TranslationBlock *curr = tb;
    TranslationBlock *next = NULL;
    uint64_t pcnext = 0;
    uint64_t cs_base = tb->cs_base;
    uint32_t flags = tb->flags;
    uint32_t cflags = tb->cflags;

    if (!tb_ras_unhandled(tb)) return;

    /*
     * Record the locked tb pages before pretranslation
     * in case cpu_loop_exit() occurs during translation.
     * After cpu loop exit, the cpu_exec_longjmp_cleanup()
     * will unlock the tb pages and ras_unlink the curr tb.
     */
    tcg_ctx->pre_locked_page0 = phys_pc;
    if (tb_page_addr1(tb) != -1) {
        tcg_ctx->pre_locked_page1 = tb_page_addr1(tb);
    }

    while (tb_ras_unhandled(curr)) {
        pcnext = curr->pc + curr->size;
        if (!(pcnext & (~TARGET_PAGE_MASK))) {
            /* The next pc starts from the next page exactly.
             * Avoid accessing the next page in ahead. */
            latxs_ras_unlink(curr);
            break;
        }
        if (inst_may_cross_page(pcnext)) {
            /* The next pc is close to the page boundary.
             * The pretranslation might cross page. */
            latxs_ras_unlink(curr);
            break;
        }
        assert (cpu->cpu_index <= MF_RAS_CPU_MASK);
        set_ras_cpuindex(curr, cpu->cpu_index);
        /* translate next tb */
        next = lat_ras_pretranslate_one(curr, cpu,
                pcnext, cs_base, flags, cflags);
        curr = next;
    }
}

TranslationBlock *lat_tb_gen_code(CPUState *cpu, vaddr pc,
        uint64_t cs_base, uint32_t flags, int cflags)
{
    CPUArchState *env = cpu->env_ptr;
    uint32_t mtcc_id = curr_mtcc(env);
    TranslationBlock *tb;
    TranslationBlock *etb = NULL; (void)etb;
    tb_page_addr_t phys_pc;
    void *host_pc;
    int error = 0;
    bool lookup_after_lock = lal_level(3);
    lsenv->tr_data->is_tu_mode = 0;

#ifdef CONFIG_LATX_PRINT
    tb_trans_cnt[cpu->cpu_index + (flags&HF_CPL_MASK)?4:0]++;
#endif

    tcg_ctx = tcg_ctx_mtcc[mtcc_id];

    assert_memory_lock();
    qemu_thread_jit_write();

    phys_pc = get_page_addr_code_hostp(env, pc, &host_pc);

    if (phys_pc == -1) {
        cflags = (cflags & ~CF_COUNT_MASK) | CF_LAST_IO | 1;
    }

    if (lal_level(2) && tb_page_locked(phys_pc)) {
        lookup_after_lock = true;
    }

    QEMU_BUILD_BUG_ON(CF_COUNT_MASK + 1 != TCG_MAX_INSNS);

relock:
    tcg_ctx->pre_locked_page0 = -1;
    tcg_ctx->pre_locked_page1 = -1;
    error = 0;
    if (phys_pc != -1) {
        if (tb_trylock_page0(phys_pc)) {
            tb_lock_page0(phys_pc);
            lookup_after_lock = lal_level(1);
        }
#ifdef CONFIG_SOFTMMU
        /*
         * A published TB under the page lock means protection already
         * completed. For a new page, protect outside the page lock.
         */
        if (!tb_page_has_code_locked(phys_pc) ||
            !tlb_code_page_is_protected(phys_pc)) {
            tb_unlock_page0(phys_pc);
            tlb_protect_code(phys_pc);
            if (tb_trylock_page0(phys_pc)) {
                tb_lock_page0(phys_pc);
                lookup_after_lock = lal_level(1);
            }
        }
#endif
        tcg_ctx->pre_locked_page0 = phys_pc;
        /* if page is busy, lookup again after get the lock
         * to avoid useless translation */
        if (lookup_after_lock) {
            etb = tb_lookup(cpu, pc, cs_base, flags, cflags);
            if (etb) {
                tcg_ctx->pre_locked_page0 = -1;
                tb_unlock_page0(phys_pc);
                return etb;
            }
        }
    }

    tb = lat_tb_gen_code_locked(cpu, pc, cs_base, flags, cflags,
            phys_pc, host_pc, &error);

    switch (error) {
    case GENCODE_ERR_NONE: /* no error */
        assert(tb != NULL);
        break;
    case GENCODE_ERR_FLUSH1: /* do tb flush case 1 : no space to alloc tb */
    case GENCODE_ERR_FLUSH2: /* do tb flush case 2 : sleb overflow */
        tb_unlock_ctx_pages(1);
        tb_flush_mtcc(cpu, mtcc_id);
        mmap_unlock();
        cpu->exception_index = EXCP_INTERRUPT;
        cpu_loop_exit(cpu);
        break;
    case GENCODE_ERR_TB_EXIST: /* existing tb */
        tb_unlock_ctx_pages(1);
#if defined(CONFIG_SOFTMMU) && defined(CONFIG_LATX_TU)
        if (tb_is_tu(tb)) {
            /* The registration key covers this member. Check its whole TU. */
            if (tu_pages_valid(cpu, tb)) {
                return tb;
            }

            /* Reuse a cached single TB before taking the page lock again. */
            flags |= HF_FORCE_TB_MODE;
            env->hflags |= HF_FORCE_TB_MODE;
            etb = tb_lookup(cpu, pc, cs_base, flags, cflags);
            if (etb) {
                return etb;
            }
            goto relock;
        }
#endif
        return tb;
    case GENCODE_ERR_BUF_OVERFLOW: /* buffer overflow */
        /* set code_gen_ptr to force alloc a new region */
        qatomic_set(&tcg_ctx->code_gen_ptr, tcg_ctx->code_gen_highwater + 1);
        tb_unlock_ctx_pages(1);
        goto relock;
    case GENCODE_ERR_TB_ONESHOT: /* generate one-shot TB without caching it */
        return tb;
    default:
        g_assert_not_reached();
        break;
    }

    if (!option_rastoptp) {
        add_rel_entry(tb);
        if (option_soft_ras) {
            lat_ras_pretranslate_locked(cpu, tb, phys_pc, host_pc);
        }
    }

    tb_unlock_ctx_pages(1);
    assert_no_pages_locked();

    return tb;
}

/* Called with mmap_lock held for user mode emulation.  */
TranslationBlock *tb_gen_code(CPUState *cpu,
                              vaddr pc, uint64_t cs_base,
                              uint32_t flags, int cflags)
{
    CPUArchState *env = cpu->env_ptr;
    TranslationBlock *tb, *existing_tb;

    if (qemu_tcg_mode_2_lat()) {
        tb = lat_tb_gen_code(cpu, pc, cs_base, flags, cflags);
        if (option_tb_record) {
            const uint8_t *ir1_bytes = latxs_ir1_bytes();
            const uint32_t *ir2_ptr = (const uint32_t *)tb->tc.ptr;
            uint32_t ir2_len = tb->ir2_num * 4;

            tb_record_tb(tb, ir1_bytes, tb->size, ir2_ptr, ir2_len);
        }
        return tb;
    }

    tb_page_addr_t phys_pc, phys_p2;
    tcg_insn_unit *gen_code_buf;
    int gen_code_size = 0, search_size, max_insns;
    int64_t ti;
    void *host_pc;

    max_insns = get_max_insns(cflags);

    uint32_t mtcc_id = curr_mtcc(env);
    tcg_ctx = tcg_ctx_mtcc[mtcc_id];

    assert_memory_lock();
    qemu_thread_jit_write();

    phys_pc = get_page_addr_code_hostp(env, pc, &host_pc);

    if (phys_pc == -1) {
        /* Generate a one-shot TB with 1 insn in it */
        cflags = (cflags & ~CF_COUNT_MASK) | CF_LAST_IO | 1;
    }

    QEMU_BUILD_BUG_ON(CF_COUNT_MASK + 1 != TCG_MAX_INSNS);

 buffer_overflow:
    assert_no_pages_locked();
    tb = tcg_tb_alloc(tcg_ctx);
    if (unlikely(!tb)) {
        /* flush must be done */
        tb_flush_mtcc(cpu, mtcc_id);
        mmap_unlock();
        /* Make the execution loop process the flush as soon as possible.  */
        cpu->exception_index = EXCP_INTERRUPT;
        cpu_loop_exit(cpu);
    }

    gen_code_buf = tcg_ctx->code_gen_ptr;
    /* Sample after allocation: it may have switched to a new region. */
    void *code_gen_start = tcg_ctx->code_gen_ptr;
    tb_set_opt_ptr(tb, NULL);
    TB_REL_START(tb) = -1;
    TB_REL_END(tb) = -1;
    tb->tc.ptr = tcg_splitwx_to_rx(gen_code_buf);
    tb->pc = pc;
    tb->cs_base = cs_base;
    tb->flags = flags;
    tb->cflags = cflags;
    tb->int_opt[0] = 0;
    tb->int_opt[1] = 0;
    tb->signal_unlink[0] = 0;
    tb->signal_unlink[1] = 0;
    tb->ptn.eflag_type = PTN_EFLAG_TYPE_INVALID;
#ifdef CONFIG_LATX_DEBUG
    tb->inhibit_irq_type = 0;
#endif
    clear_misc_flags(tb);

    tb_set_page_addr0(tb, phys_pc);
    tb_next_data_reset(tb);
    tb->hva = -1ULL;
    tb_set_page_addr1(tb, -1);
    if (phys_pc != -1) {
#if defined(CONFIG_SOFTMMU)
        tlb_protect_code(phys_pc);
#endif
        tb_lock_page0(phys_pc);
    }
    set_mask_to_miscflags(tb, curr_mtcc(env),
                          MF_MTCC_ID_MASK, MF_MTCC_ID_SHIFT);
    tb->preorder_tb_offset = 0;
    tb->preorder_next = NULL;
#ifdef CONFIG_LATX_TU
    tb->ras_dest = (uintptr_t)NULL;
#endif
    tb->inst_ptr_offset = 0;

    tcg_ctx->gen_tb = tb;
    tcg_ctx->addr_type = TARGET_LONG_BITS == 32 ? TCG_TYPE_I32 : TCG_TYPE_I64;
#ifdef CONFIG_SOFTMMU
    tcg_ctx->page_bits = TARGET_PAGE_BITS;
    tcg_ctx->page_mask = TARGET_PAGE_MASK;
    tcg_ctx->tlb_dyn_max_bits = CPU_TLB_DYN_MAX_BITS;
    tcg_ctx->tlb_fast_offset =
        (int)offsetof(ArchCPU, neg.tlb.f) - (int)offsetof(ArchCPU, env);
#endif
    tcg_ctx->insn_start_words = TARGET_INSN_START_WORDS;
#ifdef TCG_GUEST_DEFAULT_MO
    tcg_ctx->guest_mo = TCG_GUEST_DEFAULT_MO;
#else
    tcg_ctx->guest_mo = TCG_MO_ALL;
#endif

 restart_translate:
    trace_translate_block(tb, pc, tb->tc.ptr);

    curr_rel_entry_num = 0;

    if (qemu_tcg_mode_1_tcg()) {
        gen_code_size = setjmp_gen_code(env, tb, pc, host_pc, &max_insns, &ti);
    } else {
        g_assert_not_reached();
    }

    if (unlikely(gen_code_size < 0)) {
        switch (gen_code_size) {
        case -1:
            /*
             * Overflow of code_gen_buffer, or the current slice of it.
             *
             * TODO: We don't need to re-do gen_intermediate_code, nor
             * should we re-do the tcg optimization currently hidden
             * inside tcg_gen_code.  All that should be required is to
             * flush the TBs, allocate a new TB, re-initialize it per
             * above, and re-do the actual code generation.
             */
            qemu_log_mask(CPU_LOG_TB_OP | CPU_LOG_TB_OP_OPT,
                          "Restarting code generation for "
                          "code_gen_buffer overflow\n");
            tb_unlock_pages(tb);
            tcg_ctx->gen_tb = NULL;
            qatomic_set(&tcg_ctx->code_gen_ptr, tcg_ctx->code_gen_highwater + 1);
            goto buffer_overflow;

        case -2:
            /*
             * The code generated for the TranslationBlock is too large.
             * The maximum size allowed by the unwind info is 64k.
             * There may be stricter constraints from relocations
             * in the tcg backend.
             *
             * Try again with half as many insns as we attempted this time.
             * If a single insn overflows, there's a bug somewhere...
             */
            assert(max_insns > 1);
            max_insns /= 2;
            qemu_log_mask(CPU_LOG_TB_OP | CPU_LOG_TB_OP_OPT,
                          "Restarting code generation with "
                          "smaller translation block (max %d insns)\n",
                          max_insns);

            /*
             * The half-sized TB may not cross pages.
             * TODO: Fix all targets that cross pages except with
             * the first insn, at which point this can't be reached.
             */
            phys_p2 = tb_page_addr1(tb);
            if (unlikely(phys_p2 != -1)) {
                tb_unlock_page1(phys_pc, phys_p2);
                tb_set_page_addr1(tb, -1);
            }
            goto restart_translate;

        case -3:
            /*
             * We had a page lock ordering problem.  In order to avoid
             * deadlock we had to drop the lock on page0, which means
             * that everything we translated so far is compromised.
             * Restart with locks held on both pages.
             */
            qemu_log_mask(CPU_LOG_TB_OP | CPU_LOG_TB_OP_OPT,
                          "Restarting code generation with re-locked pages");
            goto restart_translate;

        default:
            g_assert_not_reached();
        }
    }
    tcg_ctx->gen_tb = NULL;

    search_size = encode_search(tb, (void *)gen_code_buf + gen_code_size);
    if (unlikely(search_size < 0)) {
        tb_unlock_pages(tb);
        goto buffer_overflow;
    }

    tb->ir2_num = gen_code_size >> 2;
#if defined(CONFIG_LATX_TCG_MODE) || defined(CONFIG_TCG_REGION_TREE_ENABLE)
    tb->tc.size = gen_code_size;
#endif

    /*
     * For CF_PCREL, attribute all executions of the generated code
     * to its first mapping.
     */
    perf_report_code(pc, tb, tcg_splitwx_to_rx(gen_code_buf));

    if (qemu_loglevel_mask(CPU_LOG_TB_OUT_ASM) &&
        qemu_log_in_addr_range(pc)) {
        FILE *logfile = qemu_log_trylock();
        if (logfile) {
            int code_size, data_size;
            const tcg_target_ulong *rx_data_gen_ptr;
            size_t chunk_start;
            int insn = 0;

            if (tcg_ctx->data_gen_ptr) {
                rx_data_gen_ptr = tcg_splitwx_to_rx(tcg_ctx->data_gen_ptr);
                code_size = (const void *)rx_data_gen_ptr - tb->tc.ptr;
                data_size = gen_code_size - code_size;
            } else {
                rx_data_gen_ptr = 0;
                code_size = gen_code_size;
                data_size = 0;
            }

            /* Dump header and the first instruction */
            fprintf(logfile, "OUT: [size=%d]\n", gen_code_size);
            fprintf(logfile,
                    "  -- guest addr 0x%016" PRIx64 " + tb prologue\n",
                    tcg_ctx->gen_insn_data[insn * TARGET_INSN_START_WORDS]);
            chunk_start = tcg_ctx->gen_insn_end_off[insn];
            disas(logfile, tb->tc.ptr, chunk_start);

            /*
             * Dump each instruction chunk, wrapping up empty chunks into
             * the next instruction. The whole array is offset so the
             * first entry is the beginning of the 2nd instruction.
             */
            while (insn < tb->icount) {
                size_t chunk_end = tcg_ctx->gen_insn_end_off[insn];
                if (chunk_end > chunk_start) {
                    fprintf(logfile, "  -- guest addr 0x%016" PRIx64 "\n",
                            tcg_ctx->gen_insn_data[insn * TARGET_INSN_START_WORDS]);
                    disas(logfile, tb->tc.ptr + chunk_start,
                          chunk_end - chunk_start);
                    chunk_start = chunk_end;
                }
                insn++;
            }

            if (chunk_start < code_size) {
                fprintf(logfile, "  -- tb slow paths + alignment\n");
                disas(logfile, tb->tc.ptr + chunk_start,
                      code_size - chunk_start);
            }

            /* Finally dump any data we may have after the block */
            if (data_size) {
                int i;
                fprintf(logfile, "  data: [size=%d]\n", data_size);
                for (i = 0; i < data_size / sizeof(tcg_target_ulong); i++) {
                    if (sizeof(tcg_target_ulong) == 8) {
                        fprintf(logfile,
                                "0x%08" PRIxPTR ":  .quad  0x%016" TCG_PRIlx "\n",
                                (uintptr_t)&rx_data_gen_ptr[i], rx_data_gen_ptr[i]);
                    } else if (sizeof(tcg_target_ulong) == 4) {
                        fprintf(logfile,
                                "0x%08" PRIxPTR ":  .long  0x%08" TCG_PRIlx "\n",
                                (uintptr_t)&rx_data_gen_ptr[i], rx_data_gen_ptr[i]);
                    } else {
                        qemu_build_not_reached();
                    }
                }
            }
            fprintf(logfile, "\n");
            qemu_log_unlock(logfile);
        }
    }

    uintptr_t sptr = (uintptr_t)gen_code_buf + gen_code_size;
    qatomic_set(&tcg_ctx->code_gen_ptr, (void *)
        ROUND_UP(sptr + search_size, CODE_GEN_ALIGN));

    /* init jump list */
    qemu_spin_init(&tb->jmp_lock);
    tb->jmp_list_head = (uintptr_t)NULL;
    tb->jmp_list_next[0] = (uintptr_t)NULL;
    tb->jmp_list_next[1] = (uintptr_t)NULL;
    tb->jmp_dest[0] = (uintptr_t)NULL;
    tb->jmp_dest[1] = (uintptr_t)NULL;

    /* init original jump addresses which have been set during tcg_gen_code() */
    if (tb->jmp_reset_offset[0] != TB_JMP_OFFSET_INVALID) {
        tb_reset_jump(tb, 0);
    }
    if (tb->jmp_reset_offset[1] != TB_JMP_OFFSET_INVALID) {
        tb_reset_jump(tb, 1);
    }

    /*
     * If the TB is not associated with a physical RAM page then it must be
     * a temporary one-insn TB, and we have nothing left to do. Return early
     * before attempting to link to other TBs or add to the lookup table.
     */
    if (tb_page_addr0(tb) == -1) {
        assert_no_pages_locked();
        return tb;
    }

    /*
     * Insert TB into the corresponding region tree before publishing it
     * through QHT. Otherwise rewinding happened in the TB might fail to
     * lookup itself using host PC.
     */
    tcg_tb_insert(tb);

    /*
     * No explicit memory barrier is required -- tb_link_page() makes the
     * TB visible in a consistent state.
     */
    existing_tb = tb_link_page(tb, false);
    tb_unlock_pages(tb);
    assert_no_pages_locked();

    /* if the TB already exists, discard what we just translated */
    if (unlikely(existing_tb != tb)) {
        lat_tb_discard_code(tcg_ctx, code_gen_start);
        tcg_tb_remove(tb);
        return existing_tb;
    }

    return tb;
}

/* user-mode: call with mmap_lock held */
void tb_check_watchpoint(CPUState *cpu, uintptr_t retaddr)
{
    TranslationBlock *tb;

    assert_memory_lock();

    tb = tcg_tb_lookup(retaddr);
    if (tb) {
        /* We can use retranslation to find the PC.  */
        cpu_restore_state_from_tb(cpu, tb, retaddr);
        tb_phys_invalidate(tb, -1);
    } else {
        /* The exception probably happened in a helper.  The CPU state should
           have been saved before calling it. Fetch the PC from there.  */
        CPUArchState *env = cpu->env_ptr;
        vaddr pc;
        uint64_t cs_base;
        tb_page_addr_t addr;
        uint32_t flags;

        cpu_get_tb_cpu_state(env, &pc, &cs_base, &flags);
        addr = get_page_addr_code(env, pc);
        if (addr != -1) {
            tb_invalidate_phys_range(addr, addr);
        }
    }
}

#if defined(CONFIG_USER_ONLY) && defined(CONFIG_LATX_PROFILER)
struct tb_exec_stats {
    target_ulong pc;
    target_ulong end_pc;
    TBProfile profile;
    uint64_t ir1_num;
    double   exp_rate;
    uint64_t run_ir1_num;
    uint64_t run_ir2_num;
    double   run_exp_rate;
    uint64_t eflags_run;
};

static void tb_stats_copy(TranslationBlock *tb, void *ptr)
{
    struct tb_exec_stats *stats = (struct tb_exec_stats *)ptr;
    uint64_t exec_times = GET_TB_PROFILE(tb, exec_times);
    uint64_t ir2_num = GET_TB_PROFILE(tb, nr_code);
    uint64_t eflags_gen = GET_TB_PROFILE(tb, sta_generate);
    uint64_t eflags_eli = GET_TB_PROFILE(tb, sta_eliminate);
    memcpy(&(stats->profile), &(tb->profile), sizeof(TBProfile));

    stats->pc = tb->pc;
    stats->end_pc = tb->pc + tb->size;
    stats->ir1_num = tb->icount;
    stats->run_ir1_num = tb->icount * exec_times;
    stats->run_ir2_num = ir2_num * exec_times;
    stats->exp_rate = ir2_num / (double)tb->icount;
    stats->run_exp_rate = (stats->exp_rate - 1) * exec_times;
    stats->eflags_run = (eflags_gen - eflags_eli) * exec_times;
    SET_TB_PROFILE(tb, exec_times, 0);
    SET_TB_PROFILE(tb, llvm_exec_times, 0);
}
static int
tb_exec_cmp(const void *_a, const void *_b)
{
    struct tb_exec_stats *a = (struct tb_exec_stats *)_a;
    struct tb_exec_stats *b = (struct tb_exec_stats *)_b;

  //return b->profile.exec_times - a->profile.exec_times;
  return (b->run_ir2_num > a->run_ir2_num)? 1 : -1;
}
#endif

struct tb_tree_stats {
    size_t nb_tbs;
    size_t host_size;
    size_t target_size;
    size_t max_target_size;
    size_t direct_jmp_count;
    size_t direct_jmp2_count;
    size_t cross_page;
#ifdef CONFIG_LATX_PROFILER
    GArray *tb_execinfo;
    uint64_t tb_exec_num_all;
    uint64_t tb_exec_ir2_num_all;
    uint64_t tb_rdtime_all;
#endif
};

#ifndef CONFIG_USER_ONLY
/*
 * In deterministic execution mode, instructions doing device I/Os
 * must be at the end of the TB.
 *
 * Called by softmmu_template.h, with iothread mutex not held.
 */
void cpu_io_recompile(CPUState *cpu, uintptr_t retaddr)
{
    TranslationBlock *tb;
    CPUClass *cc;
    uint32_t n;

    tb = tcg_tb_lookup(retaddr);
    if (!tb) {
        cpu_abort(cpu, "cpu_io_recompile: could not find TB for pc=%p",
                  (void *)retaddr);
    }
    cpu_restore_state_from_tb(cpu, tb, retaddr);

    /*
     * Some guests must re-execute the branch when re-executing a delay
     * slot instruction.  When this is the case, adjust icount and N
     * to account for the re-execution of the branch.
     */
    n = 1;
    cc = CPU_GET_CLASS(cpu);
    if (cc->tcg_ops->io_recompile_replay_branch &&
        cc->tcg_ops->io_recompile_replay_branch(cpu, tb)) {
        cpu_neg(cpu)->icount_decr.u16.low++;
        n = 2;
    }

    /*
     * Exit the loop and potentially generate a new TB executing the
     * just the I/O insns. We also limit instrumentation to memory
     * operations only (which execute after completion) so we don't
     * double instrument the instruction.
     */
    cpu->cflags_next_tb = curr_cflags(cpu) | CF_MEMI_ONLY | CF_LAST_IO | n;

    if (qemu_loglevel_mask(CPU_LOG_EXEC)) {
        vaddr pc = log_pc(cpu, tb);
        if (qemu_log_in_addr_range(pc)) {
            qemu_log("cpu_io_recompile: rewound execution of TB to %016"
                     VADDR_PRIx "\n", pc);
        }
    }

    cpu_loop_exit_noexc(cpu);
}

static void print_qht_statistics(struct qht_stats hst, GString *buf)
{
    uint32_t hgram_opts;
    size_t hgram_bins;
    char *hgram;

    if (!hst.head_buckets) {
        return;
    }
    g_string_append_printf(buf, "TB hash buckets     %zu/%zu "
                           "(%0.2f%% head buckets used)\n",
                           hst.used_head_buckets, hst.head_buckets,
                           (double)hst.used_head_buckets /
                           hst.head_buckets * 100);

    hgram_opts =  QDIST_PR_BORDER | QDIST_PR_LABELS;
    hgram_opts |= QDIST_PR_100X   | QDIST_PR_PERCENT;
    if (qdist_xmax(&hst.occupancy) - qdist_xmin(&hst.occupancy) == 1) {
        hgram_opts |= QDIST_PR_NODECIMAL;
    }
    hgram = qdist_pr(&hst.occupancy, 10, hgram_opts);
    g_string_append_printf(buf, "TB hash occupancy   %0.2f%% avg chain occ. "
                           "Histogram: %s\n",
                           qdist_avg(&hst.occupancy) * 100, hgram);
    g_free(hgram);

    hgram_opts = QDIST_PR_BORDER | QDIST_PR_LABELS;
    hgram_bins = qdist_xmax(&hst.chain) - qdist_xmin(&hst.chain);
    if (hgram_bins > 10) {
        hgram_bins = 10;
    } else {
        hgram_bins = 0;
        hgram_opts |= QDIST_PR_NODECIMAL | QDIST_PR_NOBINRANGE;
    }
    hgram = qdist_pr(&hst.chain, hgram_bins, hgram_opts);
    g_string_append_printf(buf, "TB hash avg chain   %0.3f buckets. "
                           "Histogram: %s\n",
                           qdist_avg(&hst.chain), hgram);
    g_free(hgram);
}

#ifdef CONFIG_LATX_PROFILER
struct tb_exec_stats {
    target_ulong pc;
    target_ulong end_pc;
    TBProfile profile;
    uint64_t ir1_num;
    double   exp_rate;
    uint64_t run_ir1_num;
    uint64_t run_ir2_num;
    uint64_t run_rdtime;
    uint64_t run_rdtime_avg;
    uint64_t run_tlbr_rdtime;
    uint64_t run_tlbr_rdtime_p;
    uint64_t run_mmio_rdtime;
    uint64_t run_mmio_lock_rdtime;
    double   run_exp_rate;
    uint64_t eflags_run;
#ifdef CONFIG_LATX_TU
    target_ulong tupc;
#endif
    TranslationBlock *tb;
};

static void tb_stats_copy(TranslationBlock *tb, void *ptr)
{
    struct tb_exec_stats *stats = (struct tb_exec_stats *)ptr;
    uint64_t exec_times = GET_TB_PROFILE(tb, exec_times);
    uint64_t ir2_num = GET_TB_PROFILE(tb, nr_code);
    uint64_t eflags_gen = GET_TB_PROFILE(tb, sta_generate);
    uint64_t eflags_eli = GET_TB_PROFILE(tb, sta_eliminate);
    memcpy(&(stats->profile), &(tb->profile), sizeof(TBProfile));

    stats->pc = tb->pc;
    stats->end_pc = tb->pc + tb->size;
    stats->ir1_num = tb->icount;
    stats->run_ir1_num = tb->icount * exec_times;
    stats->run_ir2_num = ir2_num * exec_times;
    stats->run_rdtime = GET_TB_PROFILE(tb, exec_rdtime);
    stats->run_tlbr_rdtime = GET_TB_PROFILE(tb, exec_tlbr_rdtime);
    stats->run_mmio_rdtime = GET_TB_PROFILE(tb, exec_mmio_rdtime);
    stats->run_mmio_lock_rdtime = GET_TB_PROFILE(tb, exec_mmio_lock_rdtime);
    if (stats->run_rdtime != 0) {
        stats->run_tlbr_rdtime_p = (1000000*stats->run_tlbr_rdtime)/stats->run_rdtime;
    } else {
        stats->run_tlbr_rdtime_p = 0;
    }
    if (exec_times) {
        stats->run_rdtime_avg = stats->run_rdtime / exec_times;
    } else {
        stats->run_rdtime_avg = 0;
    }
    stats->exp_rate = ir2_num / (double)tb->icount;
    stats->run_exp_rate = (stats->exp_rate - 1) * exec_times;
    stats->eflags_run = (eflags_gen - eflags_eli) * exec_times;
#ifdef CONFIG_LATX_TU
    stats->tupc = tb_is_tu(tb) ? tb_tu_identity(tb) : 0;
#endif
    stats->tb = tb;
    SET_TB_PROFILE(tb, exec_times, 0);
    SET_TB_PROFILE(tb, llvm_exec_times, 0);
    SET_TB_PROFILE(tb, exec_rdtime, 0);
    SET_TB_PROFILE(tb, exec_tlbr_rdtime, 0);
    SET_TB_PROFILE(tb, exec_mmio_rdtime, 0);
    SET_TB_PROFILE(tb, exec_mmio_lock_rdtime, 0);
    SET_TB_PROFILE(tb, pushpop_add, 0);
    SET_TB_PROFILE(tb, pushpop_sub, 0);
    for (int i = 0; i < 8; ++i) {
        tb->profile.hamt_error[i] = 0;
    }
}
#endif

#ifdef CONFIG_LATX_PROFILER
static int
tb_exec_cmp(const void *_a, const void *_b)
{
    struct tb_exec_stats *a = (struct tb_exec_stats *)_a;
    struct tb_exec_stats *b = (struct tb_exec_stats *)_b;

  //return b->profile.exec_times - a->profile.exec_times;
  return (b->run_ir2_num > a->run_ir2_num)? 1 : -1;
}

static int
tb_exec_rdtime_cmp(const void *_a, const void *_b)
{
    struct tb_exec_stats *a = (struct tb_exec_stats *)_a;
    struct tb_exec_stats *b = (struct tb_exec_stats *)_b;

  //return b->profile.exec_times - a->profile.exec_times;
  return (b->run_rdtime > a->run_rdtime)? 1 : -1;
}

static int
tb_exec_rdtime_cmp_avg(const void *_a, const void *_b)
{
    struct tb_exec_stats *a = (struct tb_exec_stats *)_a;
    struct tb_exec_stats *b = (struct tb_exec_stats *)_b;

  //return b->profile.exec_times - a->profile.exec_times;
  return (b->run_rdtime_avg > a->run_rdtime_avg)? 1 : -1;
}

static int
tb_exec_tlbr_rdtime_cmp(const void *_a, const void *_b)
{
    struct tb_exec_stats *a = (struct tb_exec_stats *)_a;
    struct tb_exec_stats *b = (struct tb_exec_stats *)_b;

  //return b->profile.exec_times - a->profile.exec_times;
  return (b->run_tlbr_rdtime > a->run_tlbr_rdtime)? 1 : -1;
}
static int
tb_exec_tlbr_rdtime_p_cmp(const void *_a, const void *_b)
{
    struct tb_exec_stats *a = (struct tb_exec_stats *)_a;
    struct tb_exec_stats *b = (struct tb_exec_stats *)_b;

  //return b->profile.exec_times - a->profile.exec_times;
  return (b->run_tlbr_rdtime_p > a->run_tlbr_rdtime_p)? 1 : -1;
}
static GHashTable *tupc_sum_map;
static gint tb_in_tu_info_cmp(const void *_a, const void *_b)
{
    struct tb_exec_stats *a = (struct tb_exec_stats *)_a;
    struct tb_exec_stats *b = (struct tb_exec_stats *)_b;

    if (a->tupc == 0 && b->tupc != 0) {
        return 1;
    }
    if (a->tupc != 0 && b->tupc == 0) {
        return -1;
    }
    if (a->tupc == 0 && b->tupc == 0) {
        if (a->run_rdtime != b->run_rdtime) {
            return (a->run_rdtime > b->run_rdtime) ? -1 : 1;
        }
        return 0;
    }

    gpointer key_a = (gpointer)(uintptr_t)a->tupc;
    gpointer key_b = (gpointer)(uintptr_t)b->tupc;

    long sum_a = (long)(uintptr_t)g_hash_table_lookup(tupc_sum_map, key_a);
    long sum_b = (long)(uintptr_t)g_hash_table_lookup(tupc_sum_map, key_b);

    if (sum_a != sum_b) {
        return (sum_a > sum_b) ? -1 : 1;
    }

    if (a->tupc != b->tupc) {
        return (a->tupc < b->tupc) ? -1 : 1;
    }

    if (a->run_rdtime != b->run_rdtime) {
        return (a->run_rdtime > b->run_rdtime) ? -1 : 1;
    }

    return 0;
}

static void tb_exec_dump_info_head(GString *buf)
{
    g_string_append_printf(buf, "ID II ");
    g_string_append_printf(buf, "pc                end_pc            exec_times  exit_times     ");
    g_string_append_printf(buf, "pushpop eliminate  ");
#ifdef CONFIG_LATX_TU
    g_string_append_printf(buf, "tupc              ");
#endif
    g_string_append_printf(buf, "avg-rdtime ");
    g_string_append_printf(buf, "rdtime [rdtime/all] ");
    g_string_append_printf(buf, "TLBR-rdtime [TLBR/rdtime] ");
    g_string_append_printf(buf, "mmio-rdtime mmio-lock-rdtime ");
    g_string_append_printf(buf, "[TB/all] [IR2/all] ");
    g_string_append_printf(buf, "run_ir1_num  run_ir2_num  run_exp_rate  ");
    g_string_append_printf(buf, "ir1_num  ir2_num  exp_rate    ");
    g_string_append_printf(buf, "hamt_error ");
    g_string_append_printf(buf, "eflags tb flags bytes\n");
}

static void tb_exec_dump_info_tb(GString *buf,
        struct tb_tree_stats *tst,
        struct tb_exec_stats *ele,
        uint64_t tb_rdtime,
        uint64_t tb_exec_num,
        uint64_t tb_exec_ir2_num)
{
    TBProfile *tb_pro = &(ele->profile);
    TranslationBlock *tb = ele->tb;
    // pc endpc exec_times exit_times
    g_string_append_printf(buf, TARGET_FMT_lx "  " TARGET_FMT_lx "  %-10" PRIu64 "  %-10" PRIu64 "  ",
                ele->pc, ele->end_pc, tb_pro->exec_times, tb_pro->exit_times);
    g_string_append_printf(buf, "%-10" PRIu64  "  ",
                    tb_pro->pushpop_sub - tb_pro->pushpop_add);
#ifdef CONFIG_LATX_TU
    if(ele->tupc) {
        g_string_append_printf(buf, TARGET_FMT_lx "  ", ele->tupc);
    } else {
        g_string_append_printf(buf, "               0" "  ");
    }
#endif
    // avg-rdtime rdtime
    g_string_append_printf(buf, "%ld ", ele->run_rdtime_avg);
    g_string_append_printf(buf, "%ld ", ele->run_rdtime);
    // [rdtime/all]
    if (tst->tb_rdtime_all != 0) {
        g_string_append_printf(buf, "%.2f%% ",
                (double)tb_rdtime * 100.0 / (double)tst->tb_rdtime_all);
    } else {
        g_string_append_printf(buf, "-%% ");
    }
    // TLBR-rdtime
    g_string_append_printf(buf, "%ld ", ele->run_tlbr_rdtime);
    if (ele->run_rdtime != 0) {
        g_string_append_printf(buf, "%.2f%% ",
                (double)ele->run_tlbr_rdtime * 100.0 / (double)ele->run_rdtime);
    } else {
        g_string_append_printf(buf, "-%% ");
    }
    // mmio-rdtime mmio-lock-rdtime
    g_string_append_printf(buf, "%ld ", ele->run_mmio_rdtime);
    g_string_append_printf(buf, "%ld ", ele->run_mmio_lock_rdtime);
    // [TB/all]
    if (tst->tb_exec_num_all != 0) {
        g_string_append_printf(buf, "%.2f%% ",
                (double)tb_exec_num * 100.0 / (double)tst->tb_exec_num_all);
    } else {
        g_string_append_printf(buf, "-%% ");
    }
    // [IR2/all]
    if (tst->tb_exec_ir2_num_all != 0) {
        g_string_append_printf(buf, "%.2f%% ",
                (double)tb_exec_ir2_num * 100.0 / (double)tst->tb_exec_ir2_num_all);
    } else {
        g_string_append_printf(buf, "-%% ");
    }
    // run_ir1_num run_ir2_num run_exp_rate
    g_string_append_printf(buf, "%-10" PRIu64 "   %-10" PRIu64 "   %-10.2f    ",
                ele->run_ir1_num, ele->run_ir2_num, ele->run_exp_rate);
    // ir1_num ir2_num exp_rate
    g_string_append_printf(buf, "%-7" PRIu64 "  %-7" PRIu64 "  %-10.2f  ",
                ele->ir1_num, tb_pro->nr_code, ele->exp_rate);
    // hamt_ptw_error 
    g_string_append_printf(buf, "%ld ", ele->profile.hamt_error[HAMT_PTW_ERROR_PF]);
    g_string_append_printf(buf, "%ld ", ele->profile.hamt_error[HAMT_PTW_ERROR_SMC]);
    g_string_append_printf(buf, "%ld ", ele->profile.hamt_error[HAMT_PTW_ERROR_MMIO]);
    g_string_append_printf(buf, "%ld ", ele->profile.hamt_error[HAMT_PTW_ERROR_DISABLE]);
    g_string_append_printf(buf, "%ld ", ele->profile.hamt_error[HAMT_PTW_ERROR_NOTRAM]);
    g_string_append_printf(buf, "%ld ", ele->profile.hamt_error[HAMT_PTW_ERROR_VGA]);
    // eflags
    g_string_append_printf(buf, "%-7" PRIu64 " ", ele->eflags_run);
    // TB.flags
    g_string_append_printf(buf, "%lx %x ", (uint64_t)tb, tb->flags);
    // TB bytes
    if (tb_page_addr0(tb) != -1) {
        uint8_t *p = qemu_ram_addr_to_host(tb->page_addr[0]);
        if (tb->page_addr[1] == -1) {
            for (int j = 0; j < tb->size; ++j) {
                g_string_append_printf(buf, "%02x", *(p + j));
            }
        } else {
            int p0size = 0x1000 - (tb->page_addr[0] & 0xfff);
            for (int j = 0; j < p0size; ++j) {
                g_string_append_printf(buf, "%02x", *(p + j));
            }
            p = qemu_ram_addr_to_host(tb->page_addr[1]);
            for (int j = 0; j < (tb->size - p0size); ++j) {
                g_string_append_printf(buf, "%02x", *(p + j));
            }
        }
    } else {
        g_string_append_printf(buf, "[invalid code page]\n");
    }
    g_string_append_printf(buf, "\n");
}

#define SDICPL0 50
#define SDICPL3 50

struct dump_index {
    int i;   // real index in exec_info
    int ii;  // index after filter
};

struct dump_index dump_index_cpl0[SDICPL0];
struct dump_index dump_index_cpl3[SDICPL3];

#ifdef CONFIG_LATX_TU
static void tb_exec_dump_info_tu(GArray *exec_info,
        GString *buf,
        struct tb_tree_stats *tst)
{
    tupc_sum_map = g_hash_table_new(g_direct_hash, g_direct_equal);
    for (guint i = 0; i < exec_info->len; i++) {
        struct tb_exec_stats *bi = &g_array_index(exec_info, struct tb_exec_stats, i);
        long old_sum = (long)(uintptr_t)g_hash_table_lookup(tupc_sum_map,
                        (gpointer)(uintptr_t)bi->tupc);
        g_hash_table_insert(tupc_sum_map,
                        (gpointer)(uintptr_t)bi->tupc,
                        (gpointer)(uintptr_t)(old_sum + bi->run_rdtime));
    }
    g_array_sort(exec_info, tb_in_tu_info_cmp);
    g_hash_table_destroy(tupc_sum_map);

    uint64_t tu_index = 0;
    target_ulong tu_pc = 0;

    struct tb_exec_stats *ele = NULL;
    guint i = 0, ii = 0;

    int splitsort = latx_tb_profile_split_sort;
    guint ii0 = 0, ii3 = 0;

    uint64_t tb_exec_num = 0;
    uint64_t tb_exec_ir2_num = 0;
    uint64_t tb_rdtime = 0;
    uint64_t tb_rdtime_all_cpl0 = 0;
    uint64_t tb_rdtime_all_cpl3 = 0;
    uint64_t tb_tlbr_rdtime_all_cpl0 = 0;
    uint64_t tb_tlbr_rdtime_all_cpl3 = 0;

    for (i = 0; i < exec_info->len; ++i) {
        ele = &g_array_index(exec_info, struct tb_exec_stats, i);

        if (ele->tb->flags & 0x3) {
            tb_rdtime_all_cpl3 += ele->run_rdtime;
            tb_tlbr_rdtime_all_cpl3 += ele->run_tlbr_rdtime;
        } else {
            tb_rdtime_all_cpl0 += ele->run_rdtime;
            tb_tlbr_rdtime_all_cpl0 += ele->run_tlbr_rdtime;
        }

        if(ele->tupc != tu_pc)
        {
            tu_index ++;
            tu_pc = ele->tupc;
            guint j = i;
            guint ii_tmp = ii, ii0_tmp = ii0, ii3_tmp = ii3;
            uint64_t tu_end = 0;
            uint64_t tb_exec_num_temp = tb_exec_num;
            uint64_t tb_exec_ir2_num_temp = tb_exec_ir2_num;
            uint64_t tb_rdtime_temp = tb_rdtime;
            uint64_t tu_run_rdtime_avg = 0;
            uint64_t tu_run_rdtime = 0;
            uint64_t tu_run_tlbr_rdtime = 0;
            uint64_t tu_run_mmio_rdtime = 0;
            uint64_t tu_run_mmio_lock_rdtime = 0;
            uint64_t tu_run_ir1_num = 0;
            uint64_t tu_run_ir2_num = 0;
            uint64_t tu_ir1_num = 0;
            uint64_t tu_nr_code = 0;
            for (; j < exec_info->len; ++j)
            {
                struct tb_exec_stats *ele_t = &g_array_index(exec_info, struct tb_exec_stats, j);
                if (tu_pc != ele_t->tupc) {
                    break;
                }
                // index after filter
                ii_tmp += 1;

                if (splitsort) {
                    if (ele_t->tb->flags & 0x3) {
                        if (ii3_tmp < SDICPL3) {
                            ii3_tmp += 1;
                        }
                    } else {
                        if (ii0_tmp < SDICPL0) {
                            ii0_tmp += 1;
                        }
                    }
                    if (ii0_tmp == SDICPL0 && ii3_tmp == SDICPL3) {
                        break;
                    }
                    continue;
                }
                tb_exec_num_temp += ele_t->profile.exec_times;
                tb_exec_ir2_num_temp += ele_t->run_ir2_num;
                tb_rdtime_temp += ele_t->run_rdtime;
                tu_end = ele_t->end_pc > tu_end ? ele_t->end_pc : tu_end;
                tu_run_rdtime_avg += ele_t->run_rdtime_avg;
                tu_run_rdtime += ele_t->run_rdtime;
                tu_run_tlbr_rdtime += ele_t->run_tlbr_rdtime;
                tu_run_mmio_rdtime += ele_t->run_mmio_rdtime;
                tu_run_mmio_lock_rdtime += ele_t->run_mmio_lock_rdtime;
                tu_run_ir1_num += ele_t->run_ir1_num;
                tu_run_ir2_num += ele_t->run_ir2_num;
                tu_ir1_num += ele_t->ir1_num;
                tu_nr_code += ele_t->profile.nr_code;
            }
            g_string_append_printf(buf, "\n");
            g_string_append_printf(buf, "TU ID ");
            g_string_append_printf(buf, "pc                end_pc            ");
            g_string_append_printf(buf, "avg-rdtime    ");
            g_string_append_printf(buf, "rdtime    [rdtime/all]    ");
            g_string_append_printf(buf, "TLBR-rdtime     [TLBR/rdtime]    ");
            g_string_append_printf(buf, "mmio-rdtime     mmio-lock-rdtime    ");
            g_string_append_printf(buf, "run_ir1_num     run_ir2_num    ");
            g_string_append_printf(buf, "ir1_num     ir2_num  \n");
            g_string_append_printf(buf, "%lu ", tu_index);
            g_string_append_printf(buf, TARGET_FMT_lx "  " TARGET_FMT_lx "    ",
                tu_pc, tu_end);
            g_string_append_printf(buf, "    %ld    ", tu_run_rdtime_avg);
            g_string_append_printf(buf, "    %ld    ", tu_run_rdtime);
            if (tst->tb_rdtime_all != 0) {
                g_string_append_printf(buf, "    %.2f%%    ",
                        (double)tb_rdtime_temp * 100.0 / (double)tst->tb_rdtime_all);
            } else {
                g_string_append_printf(buf, "    -%%    ");
            }
            g_string_append_printf(buf, "        %ld    ", tu_run_tlbr_rdtime);
            if (tu_run_rdtime != 0) {
                g_string_append_printf(buf, "        %.2f%%      ",
                        (double)tu_run_tlbr_rdtime * 100.0 / (double)tu_run_rdtime);
            } else {
                g_string_append_printf(buf, "        -%%      ");
            }
            g_string_append_printf(buf, "        %ld       ", tu_run_mmio_rdtime);
            g_string_append_printf(buf, "        %ld       ", tu_run_mmio_lock_rdtime);
            g_string_append_printf(buf, "       %-10" PRIu64 "     %-10" PRIu64 "   ",
                tu_run_ir1_num, tu_run_ir2_num);
            g_string_append_printf(buf, "   %-7" PRIu64 "   %-7" PRIu64 "  \n",
                tu_ir1_num, tu_nr_code);
            tb_exec_dump_info_head(buf);
        }

        // index after filter
        ii += 1;

        if (splitsort) {
            if (ele->tb->flags & 0x3) {
                if (ii3 < SDICPL3) {
                    dump_index_cpl3[ii3].i = i;
                    dump_index_cpl3[ii3].ii = ii;
                    ii3 += 1;
                }
            } else {
                if (ii0 < SDICPL0) {
                    dump_index_cpl0[ii0].i = i;
                    dump_index_cpl0[ii0].ii = ii;
                    ii0 += 1;
                }
            }
            if (ii0 == SDICPL0 && ii3 == SDICPL3) {
                break;
            }
            continue;
        } else {
            if (tu_index > 100) break;
        }

        tb_exec_num += ele->profile.exec_times;
        tb_exec_ir2_num += ele->run_ir2_num;
        tb_rdtime += ele->run_rdtime;

        g_string_append_printf(buf, "%d %d ", i, ii);
        tb_exec_dump_info_tb(buf, tst, ele, tb_rdtime, tb_exec_num, tb_exec_ir2_num);
    }

    // save the i
    ii = i;

    // dump only
    if (splitsort) {
        g_string_append_printf(buf, "=============================== CPL0 ===============================\n");
        tb_rdtime = 0; tb_exec_num = 0; tb_exec_ir2_num = 0;
        for (i = 0; i < exec_info->len; ++i) {
            if (i >= SDICPL0) break;
            struct dump_index *di = &(dump_index_cpl0[i]);
            ele = &g_array_index(exec_info, struct tb_exec_stats, di->i);
            tb_rdtime += ele->run_rdtime;
            tb_exec_num += ele->profile.exec_times;
            tb_exec_ir2_num += ele->run_ir2_num;
            g_string_append_printf(buf, "%d %d ", di->i, di->ii);
            tb_exec_dump_info_tb(buf, tst, ele, tb_rdtime, tb_exec_num, tb_exec_ir2_num);
        }
        g_string_append_printf(buf, "=============================== CPL3 ===============================\n");
        tb_rdtime = 0; tb_exec_num = 0; tb_exec_ir2_num = 0;
        for (i = 0; i < exec_info->len; ++i) {
            if (i >= SDICPL3) break;
            struct dump_index *di = &(dump_index_cpl3[i]);
            ele = &g_array_index(exec_info, struct tb_exec_stats, di->i);
            tb_rdtime += ele->run_rdtime;
            tb_exec_num += ele->profile.exec_times;
            tb_exec_ir2_num += ele->run_ir2_num;
            g_string_append_printf(buf, "%d %d ", di->i, di->ii);
            tb_exec_dump_info_tb(buf, tst, ele, tb_rdtime, tb_exec_num, tb_exec_ir2_num);
        }
    }

    g_string_append_printf(buf, "tb profile rdtime CPL0(%ld)(%.4f)TLBR(%ld)(%.4f) CPL3(%ld)(%.4f)TLBR(%ld)(%.4f) ALL(%ld)\n",
            tb_rdtime_all_cpl0, 100 * (double)tb_rdtime_all_cpl0 / (double)tst->tb_rdtime_all,
            tb_tlbr_rdtime_all_cpl0, 100 * (double)tb_tlbr_rdtime_all_cpl0 / (double)tst->tb_rdtime_all,
            tb_rdtime_all_cpl3, 100 * (double)tb_rdtime_all_cpl3 / (double)tst->tb_rdtime_all,
            tb_tlbr_rdtime_all_cpl3, 100 * (double)tb_tlbr_rdtime_all_cpl3 / (double)tst->tb_rdtime_all,
            tst->tb_rdtime_all);

    // restore the i
    for (i = ii; i < exec_info->len; ++i) {
        ele = &g_array_index(exec_info, struct tb_exec_stats, i);
        if (ele->tb->flags & 0x3) {
            tb_rdtime_all_cpl3 += ele->run_rdtime;
            tb_tlbr_rdtime_all_cpl3 += ele->run_tlbr_rdtime;
        } else {
            tb_rdtime_all_cpl0 += ele->run_rdtime;
            tb_tlbr_rdtime_all_cpl0 += ele->run_tlbr_rdtime;
        }
    }

    g_string_append_printf(buf, "tb profile rdtime %d CPL0(%ld)(%.4f)TLBR(%ld)(%.4f) CPL3(%ld)(%.4f)TLBR(%ld)(%.4f) ALL(%ld)\n",
            exec_info->len,
            tb_rdtime_all_cpl0, 100 * (double)tb_rdtime_all_cpl0 / (double)tst->tb_rdtime_all,
            tb_tlbr_rdtime_all_cpl0, 100 * (double)tb_tlbr_rdtime_all_cpl0 / (double)tst->tb_rdtime_all,
            tb_rdtime_all_cpl3, 100 * (double)tb_rdtime_all_cpl3 / (double)tst->tb_rdtime_all,
            tb_tlbr_rdtime_all_cpl3, 100 * (double)tb_tlbr_rdtime_all_cpl3 / (double)tst->tb_rdtime_all,
            tst->tb_rdtime_all);
    return;
}
#endif

static void tb_exec_dump_info(void *ptr, GString *buf, int tbbytime)
{
    struct tb_tree_stats *tst = (struct tb_tree_stats *)ptr;
    GArray *exec_info = tst->tb_execinfo;
    if (tbbytime == 1) {
        g_array_sort (exec_info, tb_exec_rdtime_cmp);
        g_string_append_printf(buf, "TB Profile Sort by TB.rdtime\n");
    } else if (tbbytime == 2) {
        g_array_sort (exec_info, tb_exec_rdtime_cmp_avg);
        g_string_append_printf(buf, "TB Profile Sort by TB.avg_rdtime\n");
    } else if (tbbytime == 3) {
        g_array_sort (exec_info, tb_exec_tlbr_rdtime_cmp);
        g_string_append_printf(buf, "TB Profile Sort by TB.tlbr_rdtime\n");
    } else if (tbbytime == 4) {
        g_array_sort (exec_info, tb_exec_tlbr_rdtime_p_cmp);
        g_string_append_printf(buf, "TB Profile Sort by TB.tlbr_rdtime\n");
#ifdef CONFIG_LATX_TU
    } else if (tbbytime == 5) {
        g_string_append_printf(buf, "TB Profile Sort by TU.rdtime\n");
        tb_exec_dump_info_tu(exec_info, buf, tst);
        return;
#endif
    } else {
        g_array_sort (exec_info, tb_exec_cmp);
        g_string_append_printf(buf, "TB Profile Sort by TB.run_ir2_num\n");
    }
    struct tb_exec_stats *ele = NULL;
    guint i = 0, ii = 0;

    tb_exec_dump_info_head(buf);
    g_string_append_printf(buf, "TB exec detail info:\n");

    int splitsort = latx_tb_profile_split_sort;
    guint ii0 = 0, ii3 = 0;

    uint64_t tb_exec_num = 0;
    uint64_t tb_exec_ir2_num = 0;
    uint64_t tb_rdtime = 0;
    uint64_t tb_rdtime_all_cpl0 = 0;
    uint64_t tb_rdtime_all_cpl3 = 0;
    uint64_t tb_tlbr_rdtime_all_cpl0 = 0;
    uint64_t tb_tlbr_rdtime_all_cpl3 = 0;

    for (i = 0; i < exec_info->len; ++i) {
        ele = &g_array_index(exec_info, struct tb_exec_stats, i);
        if (ele->tb->flags & 0x3) {
            tb_rdtime_all_cpl3 += ele->run_rdtime;
            tb_tlbr_rdtime_all_cpl3 += ele->run_tlbr_rdtime;
        } else {
            tb_rdtime_all_cpl0 += ele->run_rdtime;
            tb_tlbr_rdtime_all_cpl0 += ele->run_tlbr_rdtime;
        }

        // filter some info in some mode
        if (ele->profile.exec_times < 100) {
            if (tbbytime == 2) continue;
            if (tbbytime == 4) continue;
        }

        // index after filter
        ii += 1;

        if (splitsort) {
            if (ele->tb->flags & 0x3) {
                if (ii3 < SDICPL3) {
                    dump_index_cpl3[ii3].i = i;
                    dump_index_cpl3[ii3].ii = ii;
                    ii3 += 1;
                }
            } else {
                if (ii0 < SDICPL0) {
                    dump_index_cpl0[ii0].i = i;
                    dump_index_cpl0[ii0].ii = ii;
                    ii0 += 1;
                }
            }
            if (ii0 == SDICPL0 && ii3 == SDICPL3) {
                break;
            }
            continue;
        } else {
            if (ii > 100) break;
        }

        tb_exec_num += ele->profile.exec_times;
        tb_exec_ir2_num += ele->run_ir2_num;
        tb_rdtime += ele->run_rdtime;

        g_string_append_printf(buf, "%d %d ", i, ii);
        tb_exec_dump_info_tb(buf, tst, ele, tb_rdtime, tb_exec_num, tb_exec_ir2_num);
    }

    // save the i
    ii = i;

    // dump only
    if (splitsort) {
        g_string_append_printf(buf, "=============================== CPL0 ===============================\n");
        tb_rdtime = 0; tb_exec_num = 0; tb_exec_ir2_num = 0;
        for (i = 0; i < exec_info->len; ++i) {
            if (i >= SDICPL0) break;
            struct dump_index *di = &(dump_index_cpl0[i]);
            ele = &g_array_index(exec_info, struct tb_exec_stats, di->i);
            tb_rdtime += ele->run_rdtime;
            tb_exec_num += ele->profile.exec_times;
            tb_exec_ir2_num += ele->run_ir2_num;
            g_string_append_printf(buf, "%d %d ", di->i, di->ii);
            tb_exec_dump_info_tb(buf, tst, ele, tb_rdtime, tb_exec_num, tb_exec_ir2_num);
        }
        g_string_append_printf(buf, "=============================== CPL3 ===============================\n");
        tb_rdtime = 0; tb_exec_num = 0; tb_exec_ir2_num = 0;
        for (i = 0; i < exec_info->len; ++i) {
            if (i >= SDICPL3) break;
            struct dump_index *di = &(dump_index_cpl3[i]);
            ele = &g_array_index(exec_info, struct tb_exec_stats, di->i);
            tb_rdtime += ele->run_rdtime;
            tb_exec_num += ele->profile.exec_times;
            tb_exec_ir2_num += ele->run_ir2_num;
            g_string_append_printf(buf, "%d %d ", di->i, di->ii);
            tb_exec_dump_info_tb(buf, tst, ele, tb_rdtime, tb_exec_num, tb_exec_ir2_num);
        }
    }

    g_string_append_printf(buf, "tb profile rdtime CPL0(%ld)(%.4f)TLBR(%ld)(%.4f) CPL3(%ld)(%.4f)TLBR(%ld)(%.4f) ALL(%ld)\n",
            tb_rdtime_all_cpl0, 100 * (double)tb_rdtime_all_cpl0 / (double)tst->tb_rdtime_all,
            tb_tlbr_rdtime_all_cpl0, 100 * (double)tb_tlbr_rdtime_all_cpl0 / (double)tst->tb_rdtime_all,
            tb_rdtime_all_cpl3, 100 * (double)tb_rdtime_all_cpl3 / (double)tst->tb_rdtime_all,
            tb_tlbr_rdtime_all_cpl3, 100 * (double)tb_tlbr_rdtime_all_cpl3 / (double)tst->tb_rdtime_all,
            tst->tb_rdtime_all);

    // restore the i
    for (i = ii; i < exec_info->len; ++i) {
        ele = &g_array_index(exec_info, struct tb_exec_stats, i);
        if (ele->tb->flags & 0x3) {
            tb_rdtime_all_cpl3 += ele->run_rdtime;
            tb_tlbr_rdtime_all_cpl3 += ele->run_tlbr_rdtime;
        } else {
            tb_rdtime_all_cpl0 += ele->run_rdtime;
            tb_tlbr_rdtime_all_cpl0 += ele->run_tlbr_rdtime;
        }
    }

    g_string_append_printf(buf, "tb profile rdtime %d CPL0(%ld)(%.4f)TLBR(%ld)(%.4f) CPL3(%ld)(%.4f)TLBR(%ld)(%.4f) ALL(%ld)\n",
            exec_info->len,
            tb_rdtime_all_cpl0, 100 * (double)tb_rdtime_all_cpl0 / (double)tst->tb_rdtime_all,
            tb_tlbr_rdtime_all_cpl0, 100 * (double)tb_tlbr_rdtime_all_cpl0 / (double)tst->tb_rdtime_all,
            tb_rdtime_all_cpl3, 100 * (double)tb_rdtime_all_cpl3 / (double)tst->tb_rdtime_all,
            tb_tlbr_rdtime_all_cpl3, 100 * (double)tb_tlbr_rdtime_all_cpl3 / (double)tst->tb_rdtime_all,
            tst->tb_rdtime_all);
}

static void cpu_exec_dump_info(GString *buf)
{
    int i = 0;
    g_string_append_printf(buf, "cpu profile  time %ld\n", CPU_PRO_TIME);
    for (i = 0; i < 16; ++i) {
        g_string_append_printf(buf, "cpu[%2d] halt time %ld\n",
                                     i, CPU_HLT_TIME(i));
    }
    RESET_CPU_HLT_TIME();
}

#endif

static gboolean tb_tree_stats_iter(gpointer key, gpointer value, gpointer data)
{
    TranslationBlock *tb = value;
    struct tb_tree_stats *tst = data;

    tst->nb_tbs++;
#if defined(CONFIG_LATX_TCG_MODE)
    tst->host_size += tb->tc.size;
#else
    tst->host_size += tb->ir2_num * 4;
#endif
    tst->target_size += tb->size;
    if (tb->size > tst->max_target_size) {
        tst->max_target_size = tb->size;
    }
    if (tb_page_addr1(tb) != -1) {
        tst->cross_page++;
    }
    if (tb->jmp_reset_offset[0] != TB_JMP_OFFSET_INVALID) {
        tst->direct_jmp_count++;
        if (tb->jmp_reset_offset[1] != TB_JMP_OFFSET_INVALID) {
            tst->direct_jmp2_count++;
        }
    }
#ifdef CONFIG_LATX_PROFILER
    /* get tb exec stats */
    struct tb_exec_stats tb_stats = {};
    /* copy tb data */
    tb_stats_copy(tb, (void *)&tb_stats);
    g_array_append_val(tst->tb_execinfo, tb_stats);
    tst->tb_exec_num_all += tb_stats.profile.exec_times;
    tst->tb_exec_ir2_num_all += tb_stats.run_ir2_num;
    tst->tb_rdtime_all += tb_stats.run_rdtime;
#endif
    return false;
}

void dump_exec_info(GString *buf, int tbbytime)
{
    struct tb_tree_stats tst = {};
    struct qht_stats hst;
    size_t nb_tbs, flush_full, flush_part, flush_elide;

#ifdef CONFIG_LATX_PROFILER
    tst.tb_execinfo = g_array_new(FALSE, FALSE, sizeof(struct tb_exec_stats));
    tst.tb_exec_num_all = 0;
    tst.tb_exec_ir2_num_all = 0;
#endif
    tcg_tb_foreach(tb_tree_stats_iter, &tst);
    nb_tbs = tst.nb_tbs;
#ifdef CONFIG_LATX_PROFILER
#if defined(TARGET_I386) && defined(CONFIG_SOFTMMU)
    CPUState *atomic_cpu;
    CPU_FOREACH(atomic_cpu) {
        CPUArchState *env = atomic_cpu->env_ptr;
        g_string_append_printf(buf, "LLVM atomic helper %u %" PRIu64 "\n",
                               atomic_cpu->cpu_index, env->aot_native_atomic.completed);
    }
#endif
    for (guint i = 0; i < tst.tb_execinfo->len; ++i) {
        const struct tb_exec_stats *entry =
            &g_array_index(tst.tb_execinfo, struct tb_exec_stats, i);
        if (entry->profile.exec_times || entry->profile.llvm_exec_times) {
            g_string_append_printf(buf, "TB mode " TARGET_FMT_lx
                                   " %" PRIx64 " %x %x\n", entry->pc,
                                   (uint64_t)entry->tb->cs_base, entry->tb->flags,
                                   tb_cflags(entry->tb) & ~(CF_INVALID | CF_NO_ACTIVE));
        }
        if (entry->profile.llvm_exec_times) {
            g_string_append_printf(buf, "LLVM entry " TARGET_FMT_lx
                                   " %" PRIxPTR " %" PRIu64 "\n",
                                   entry->pc, (uintptr_t)entry->tb,
                                   entry->profile.llvm_exec_times);
        }
        if (entry->profile.exec_times) {
            g_string_append_printf(buf, "Ordinary TB " TARGET_FMT_lx
                                   " " TARGET_FMT_lx " %" PRIx64
                                   " %x %x %" PRIu64 "\n", entry->pc,
                                   entry->end_pc, (uint64_t)entry->tb->cs_base,
                                   entry->tb->flags,
                                   tb_cflags(entry->tb) & ~(CF_INVALID | CF_NO_ACTIVE),
                                   entry->profile.exec_times);
        }
    }
    for (int i = 0; i < tb_ctx.n; ++i) {
        g_string_append_printf(buf, "TB partition flush %d %u\n", i,
                               qatomic_read(&tb_ctx.tb_flush_count_mtcc[i]));
    }
    tb_exec_dump_info((void *)&tst, buf, tbbytime);
    cpu_exec_dump_info(buf);
#endif
    /* XXX: avoid using doubles ? */
    g_string_append_printf(buf, "Translation buffer state:\n");
    /*
     * Report total code size including the padding and TB structs;
     * otherwise users might think "-accel tcg,tb-size" is not honoured.
     * For avg host size we use the precise numbers from tb_tree_stats though.
     */
    g_string_append_printf(buf, "gen code size       %zu/%zu\n",
                           tcg_code_size(), tcg_code_capacity());
    g_string_append_printf(buf, "TB count            %zu\n", nb_tbs);
    g_string_append_printf(buf, "TB avg target size  %zu max=%zu bytes\n",
                           nb_tbs ? tst.target_size / nb_tbs : 0,
                           tst.max_target_size);
    g_string_append_printf(buf, "TB avg host size    %zu bytes "
                           "(expansion ratio: %0.1f)\n",
                           nb_tbs ? tst.host_size / nb_tbs : 0,
                           tst.target_size ?
                           (double)tst.host_size / tst.target_size : 0);
    g_string_append_printf(buf, "cross page TB count %zu (%zu%%)\n",
                           tst.cross_page,
                           nb_tbs ? (tst.cross_page * 100) / nb_tbs : 0);
    g_string_append_printf(buf, "direct jump count   %zu (%zu%%) "
                           "(2 jumps=%zu %zu%%)\n",
                           tst.direct_jmp_count,
                           nb_tbs ? (tst.direct_jmp_count * 100) / nb_tbs : 0,
                           tst.direct_jmp2_count,
                           nb_tbs ? (tst.direct_jmp2_count * 100) / nb_tbs : 0);

    for (int i = 0; i < tb_ctx.n; ++i) {
        qht_statistics_init(&tb_ctx.htable[i], &hst);
        print_qht_statistics(hst, buf);
    }
    qht_statistics_destroy(&hst);

    g_string_append_printf(buf, "\nStatistics:\n");
    g_string_append_printf(buf, "TB flush count      %u\n",
                           qatomic_read(&tb_ctx.tb_flush_count));
    g_string_append_printf(buf, "TB invalidate count %u\n",
                           qatomic_read(&tb_ctx.tb_phys_invalidate_count));

    tlb_flush_counts(&flush_full, &flush_part, &flush_elide);
    g_string_append_printf(buf, "TLB full flushes    %zu\n", flush_full);
    g_string_append_printf(buf, "TLB partial flushes %zu\n", flush_part);
    g_string_append_printf(buf, "TLB elided flushes  %zu\n", flush_elide);
#if defined(TARGET_I386) && defined(CONFIG_SOFTMMU)
    if (option_pcid_stats) {
        CPUState *cpu;
        CPU_FOREACH(cpu) {
            CPUX86State *env = cpu->env_ptr;
            g_string_append_printf(buf, "PCID cpu=%d cr3=%" PRIu64
                                   " enabled=%" PRIu64 " noflush=%" PRIu64
                                   " retained=%" PRIu64 " bindings=%" PRIu64,
                                   cpu->cpu_index,
                                   qatomic_read(&env->pcid_stats.cr3_writes),
                                   qatomic_read(&env->pcid_stats.pcide_writes),
                                   qatomic_read(&env->pcid_stats.noflush),
                                   qatomic_read(&env->pcid_stats.retained),
                                   qatomic_read(&env->pcid_stats.bindings));
            g_string_append_printf(buf, " cr3_ns=%" PRIu64 " enables=%" PRIu64
                                   " hw_refills=%" PRIu64
                                   " refill_ticks=%" PRIu64,
                                   qatomic_read(&env->pcid_stats.cr3_ns),
                                   qatomic_read(&env->pcid_stats.enables),
                                   qatomic_read(&env->pcid_refills),
                                   qatomic_read(&env->pcid_refill_ticks));
            g_string_append_printf(buf, " cache_retained=%" PRIu64
                                   " cache_cold=%" PRIu64,
                                   qatomic_read(
                                       &env->pcid_stats.cache_retained),
                                   qatomic_read(&env->pcid_stats.cache_cold));
            g_string_append(buf, " invpcid=");
            for (unsigned i = 0; i < 4; i++) {
                uint64_t n = qatomic_read(&env->pcid_stats.invpcid[i]);
                g_string_append_printf(buf, "%s%" PRIu64, i ? "," : "", n);
            }
            g_string_append(buf, " pcids=");
            for (unsigned i = 0; i < 64; i++) {
                uint64_t seen = qatomic_read(&env->pcid_stats.seen[i]);
                while (seen) {
                    g_string_append_printf(buf, " %u", i * 64 + ctz64(seen));
                    seen &= seen - 1;
                }
            }
            g_string_append_c(buf, '\n');
        }
    }
#endif
    tcg_dump_info(buf);

#ifdef CONFIG_LATX_PROFILER
    g_array_free(tst.tb_execinfo, TRUE);
#endif

}

#ifdef CONFIG_LATX_PROFILER
void latx_tb_profile_print_and_clear(int tbbytime)
{
    g_autoptr(GString) buf = g_string_new("");
    if (!latx_tb_profile_enabled) {
        latx_profile_cpu_time[1] = latx_host_rdtime();
    }
    dump_exec_info(buf, tbbytime);
    if (latx_tb_profile_enabled) {
        latx_profile_cpu_time[0] = latx_host_rdtime();
        printf("%s:%d clear previous profile data. start at %ld\n",
                __func__, __LINE__, latx_profile_cpu_time[0]);
    } else {
        printf("%s:%d print profile result. end at %ld\n",
                __func__, __LINE__, latx_profile_cpu_time[1]);
        printf("%s\n", buf->str);
    }
}
#endif

#else /* CONFIG_USER_ONLY */

void cpu_interrupt(CPUState *cpu, int mask)
{
    g_assert(qemu_mutex_iothread_locked());
    qatomic_or(&cpu->interrupt_request, mask);
    /*
     * Order the request store before the exit flag: the vCPU side checks
     * icount_decr in generated code and then reads interrupt_request in
     * cpu-exec.  On weakly-ordered hosts (LoongArch) a missing barrier
     * lets the vCPU observe the flag without the request, consume the
     * flag, and never exit again — the interrupt starves forever.
     */
    smp_wmb();
    lats_request_tb_exit(cpu);
}

#ifdef CONFIG_LATX_PROFILER
static void tb_exec_dump_info(void *ptr)
{
    struct tb_tree_stats *tst = (struct tb_tree_stats *)ptr;
    GArray *exec_info = tst->tb_execinfo;
    g_array_sort (exec_info, tb_exec_cmp);
    struct tb_exec_stats *ele = NULL;
    TBProfile *tb_pro = NULL;
    guint i;
    qemu_log("TB exec detail info:\n");
    qemu_log("pc                end_pc            exec_times  exit_times  ");
    qemu_log("run_ir1_num  run_ir2_num  run_exp_rate  ");
    qemu_log("ir1_num  ir2_num  exp_rate    ");
    qemu_log("eflags \n");
    //for (i = 0; i < exec_info->len; ++i) {
    for (i = 0; i < 100; ++i) {
        ele = &g_array_index(exec_info, struct tb_exec_stats, i);
        tb_pro = &(ele->profile);
        qemu_log(TARGET_FMT_lx "  " TARGET_FMT_lx "  %-10" PRIu64 "  %-10" PRIu64 "  ",
                    ele->pc, ele->end_pc, tb_pro->exec_times, tb_pro->exit_times);
        qemu_log("%-10" PRIu64 "   %-10" PRIu64 "   %-10.2f    ",
                    ele->run_ir1_num, ele->run_ir2_num, ele->run_exp_rate);
        qemu_log("%-7" PRIu64 "  %-7" PRIu64 "  %-10.2f  ",
                    ele->ir1_num, tb_pro->nr_code, ele->exp_rate);
        qemu_log("%-7" PRIu64 "\n", ele->eflags_run);
    }
}
static gboolean tb_tree_stats_iter(gpointer key, gpointer value, gpointer data)
{
    TranslationBlock *tb = value;
    struct tb_tree_stats *tst = data;

    tst->nb_tbs++;
#if defined(CONFIG_LATX_TCG_MODE)
    tst->host_size += tb->tc.size;
#else
    tst->host_size += tb->ir2_num * 4;
#endif
    tst->target_size += tb->size;
    if (tb->size > tst->max_target_size) {
        tst->max_target_size = tb->size;
    }
    if (tb_page_addr1(tb) != -1) {
        tst->cross_page++;
    }
    if (tb->jmp_reset_offset[0] != TB_JMP_OFFSET_INVALID) {
        tst->direct_jmp_count++;
        if (tb->jmp_reset_offset[1] != TB_JMP_OFFSET_INVALID) {
            tst->direct_jmp2_count++;
        }
    }
    /* get tb exec stats */
    struct tb_exec_stats tb_stats = {};
    /* copy tb data */
    tb_stats_copy(tb, (void *)&tb_stats);
    g_array_append_val(tst->tb_execinfo, tb_stats);
    return false;
}
void latx_sys_user_dump_exec_info(void);
void latx_sys_user_dump_exec_info(void)
{
    struct tb_tree_stats tst = {};

    tst.tb_execinfo = g_array_new(FALSE, FALSE, sizeof(struct tb_exec_stats));
    tcg_tb_foreach(tb_tree_stats_iter, &tst);
    tb_exec_dump_info((void *)&tst);
    g_array_free(tst.tb_execinfo, TRUE);
}
#endif

#endif /* CONFIG_USER_ONLY */

/*
 * Called by generic code at e.g. cpu reset after cpu creation,
 * therefore we must be prepared to allocate the jump cache.
 */
void tcg_flush_jmp_cache(CPUState *cpu)
{
    CPUJumpCache *jc = cpu->tb_jmp_cache;

    if (option_soft_ras && !option_rastoptp) {
        latxs_flush_soft_ras(cpu);
    }

    /* During early initialization, the cache may not yet be allocated. */
    if (unlikely(jc == NULL)) {
        return;
    }

    tb_jmp_cache_flush_all(cpu);
}

/* This is a wrapper for common code that can not use CONFIG_SOFTMMU */
void tcg_flush_softmmu_tlb(CPUState *cs)
{
#ifdef CONFIG_SOFTMMU
    tlb_flush(cs);
#endif
}
