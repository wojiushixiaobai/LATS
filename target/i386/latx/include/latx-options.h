/*
 * SPDX-FileCopyrightText: 2024-2026 LATS Project Authors
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef _LATX_OPTIONS_H_
#define _LATX_OPTIONS_H_

#include <stdbool.h>

#define LATX_IR1_CACHE_MAX_WAYS 4
#define LATX_IR1_CACHE_WAYS_DEFAULT 4
#define LATX_IR1_CACHE_ENTRIES_DEFAULT UINT64_C(16384)
#define LATX_IR1_FAST_CACHE_ENTRIES_DEFAULT 4096
#define LATX_IR1_FAST_CACHE_ENTRIES_MAX 32768

typedef enum LatxIr1CacheMode {
    LATX_IR1_CACHE_MODE_OFF = 0,
    LATX_IR1_CACHE_MODE_ON,
    LATX_IR1_CACHE_MODE_MAX,
} LatxIr1CacheMode;

typedef enum LatxIr1CacheReplacement {
    LATX_IR1_CACHE_REPLACE_LRU = 0,
    LATX_IR1_CACHE_REPLACE_FIFO,
    LATX_IR1_CACHE_REPLACE_RANDOM,
} LatxIr1CacheReplacement;

typedef enum LatxIr1Hash {
    LATX_IR1_HASH_LEGACY = 0,
    LATX_IR1_HASH_MURMUR,
} LatxIr1Hash;

extern int option_ir1_cache_ways;
extern int option_ir1_cache_replace;
extern int option_ir1_cache_enabled;
extern int option_ir1_cache_mode;
extern int option_hot_decode_enabled;
extern uint64_t option_ir1_cache_entries;
extern int option_ir1_timing;
extern int option_ir1_distribution;
extern int option_ir1_profile;
extern int option_ir1_fast_cache_entries;
extern int option_ir1_hash;

static inline uint64_t latxs_ir1_cache_entries(void)
{
    return option_ir1_cache_entries ? option_ir1_cache_entries :
                                      LATX_IR1_CACHE_ENTRIES_DEFAULT;
}

static inline int latxs_ir1_cache_ways(void)
{
    return option_ir1_cache_ways ? option_ir1_cache_ways :
                                   LATX_IR1_CACHE_WAYS_DEFAULT;
}

static inline uint64_t latxs_ir1_cache_sets(void)
{
    return latxs_ir1_cache_entries() / latxs_ir1_cache_ways();
}

static inline int latxs_ir1_fast_cache_entries(void)
{
    return option_ir1_fast_cache_entries ? option_ir1_fast_cache_entries :
                                           LATX_IR1_FAST_CACHE_ENTRIES_DEFAULT;
}

static inline const char *latxs_ir1_cache_mode_name(int mode)
{
    switch (mode) {
    case LATX_IR1_CACHE_MODE_OFF:
        return "off";
    case LATX_IR1_CACHE_MODE_ON:
        return "on";
    case LATX_IR1_CACHE_MODE_MAX:
        return "max";
    default:
        return "unknown";
    }
}

static inline const char *latxs_ir1_cache_replace_name(int replacement)
{
    switch (replacement) {
    case LATX_IR1_CACHE_REPLACE_LRU:
        return "lru";
    case LATX_IR1_CACHE_REPLACE_FIFO:
        return "fifo";
    case LATX_IR1_CACHE_REPLACE_RANDOM:
        return "random";
    default:
        return "unknown";
    }
}

static inline const char *latxs_ir1_hash_name(int hash)
{
    switch (hash) {
    case LATX_IR1_HASH_LEGACY:
        return "legacy";
    case LATX_IR1_HASH_MURMUR:
        return "murmur";
    default:
        return "unknown";
    }
}

/*
 * TU options are split by translation stage.
 *
 * JIT TU controls runtime translation grouping.  AOT TU controls whether
 * user TBs are retranslated as TUs while generating or loading AOT caches.
 * The two modes can be selected independently with tumode=off|jit|aot|both.
 *
 * "*_requested" reports the CLI setting. "*_enabled" additionally checks
 * build support and required companion options before activating code paths.
 */
typedef enum LatxTuMode {
    LATX_TU_MODE_OFF = 0,
    LATX_TU_MODE_JIT = 1u << 0,
    LATX_TU_MODE_AOT = 1u << 1,
    LATX_TU_MODE_BOTH = LATX_TU_MODE_JIT | LATX_TU_MODE_AOT,
} LatxTuMode;

extern unsigned option_tu_mode;

/* Shared verbosity gate for optional diagnostic output. */
extern bool option_verbose;

/*
 * Parse verbose from -latx/LATS_OPT before regular option parsing,
 * for entry paths such as aot-verify that run before
 * latx_parse_options().
 */
void latx_parse_verbose_early(int argc, char **argv);

/* option_tb_config */
#include "latx-sys-tb-config.h"
extern int option_tb_max_insns;
static inline int latxs_tb_max_insns(void) { return option_tb_max_insns; }

extern int option_dump;
extern int option_dump_host;
extern int option_dump_ir1;
extern int option_dump_ir2;
extern int option_dump_cpl3;

extern int option_trace_simple;
extern unsigned long long option_trace_start_nr;
extern unsigned long long option_trace_start_tb;
extern int option_trace_start_tb_set;
extern int option_traceir1;
extern int option_traceir1_data;
extern int option_smmu_slow;
extern int option_split_strins;
extern int option_tb_flush_on_reboot;
extern int option_htimer;
extern int option_irq_gate_nobql;

extern int option_pushpop;
extern int option_pushpop_ret;
extern int option_pause;
extern int option_soft_ras;
extern int option_rastoptp;
extern int option_tu_hras;
extern int option_tu_hras_opt_copy;
extern int option_tu_max_tb;
extern int option_aot_tu_max_tb;
extern int option_shbr;
extern int option_ghbr;
/* Levels 2 and 3 are performance experiments, not safe defaults. */
#define LATXS_SHBR_DISABLED       0
#define LATXS_SHBR_BASIC          1
#define LATXS_SHBR_IGNORE_MEMORY  2
#define LATXS_SHBR_AGGRESSIVE     3
static inline bool latxs_shbr_enabled(void)
{
    return option_shbr >= LATXS_SHBR_BASIC;
}
static inline bool latxs_shbr_ignore_memory(void)
{
    return option_shbr >= LATXS_SHBR_IGNORE_MEMORY;
}
static inline bool latxs_shbr_aggressive(void)
{
    return option_shbr >= LATXS_SHBR_AGGRESSIVE;
}
/* Level 2 may expose noncanonical high halves on memory slow-path exits. */
#define LATXS_GHBR_DISABLED       0
#define LATXS_GHBR_BASIC          1
#define LATXS_GHBR_IGNORE_MEMORY  2
static inline bool latxs_ghbr_enabled(void)
{
    return option_ghbr >= LATXS_GHBR_BASIC;
}
static inline bool latxs_ghbr_ignore_memory(void)
{
    return option_ghbr >= LATXS_GHBR_IGNORE_MEMORY;
}
extern int option_region_prologue;
extern int option_split_tb;
extern int option_split_search;
extern int option_tb_pcrel;
extern int option_page_pcrel;
extern int option_staticcs;
extern int option_sys_aot;
extern int option_aot_save;
extern int option_aot_fork;
extern int option_wait_aot;
extern int option_aot_load;
extern int option_aot_tu_pretranslate;
extern int option_aot_tu_pre_disasm;
extern int option_aot_verify;
extern int option_aot_debug;
extern int option_aot_gpr_highbits;
extern int option_tu_gpr_highbits;
extern int option_aot_cross_page_tu;
extern int option_llvm_aot;

static inline bool jit_tu_requested(void)
{
    return option_tu_mode & LATX_TU_MODE_JIT;
}

static inline bool aot_tu_requested(void)
{
    return option_tu_mode & LATX_TU_MODE_AOT;
}

static inline bool jit_tu_enabled(void)
{
#ifdef CONFIG_LATX_TU
    return jit_tu_requested();
#else
    return false;
#endif
}

static inline bool aot_tu_enabled(void)
{
#ifdef CONFIG_LATX_TU
    return option_sys_aot && aot_tu_requested();
#else
    return false;
#endif
}

static inline bool tu_runtime_enabled(void)
{
#ifdef CONFIG_LATX_TU
    return jit_tu_requested() || aot_tu_enabled();
#else
    return false;
#endif
}

static inline bool aot_cross_page_tu_enabled(void)
{
#ifdef CONFIG_LATX_TU
    return aot_tu_enabled() && option_aot_cross_page_tu;
#else
    return false;
#endif
}

static inline bool aot_user_cache_enabled(void)
{
#ifdef CONFIG_LATX_TU
    return !aot_tu_requested();
#else
    return true;
#endif
}

extern int option_tb_record;
extern const char *option_tb_record_path;

extern int option_tb_link;
extern int option_cross_page;
extern int option_cross_page_tu;
extern int option_tucp_fast;
extern bool option_external_tu_stub;
extern int option_tucp_generation;
extern uint64_t option_tucp_generation_array_size;
extern int option_intb_link;
extern int option_intb_njc;
extern int option_intb_njc_inline;

static inline bool intb_njc_inline_enabled(void)
{
    return option_intb_njc && option_intb_njc_inline;
}

typedef enum LatxJmpCacheVersion {
    LATX_JMP_CACHE_LEGACY,
    LATX_JMP_CACHE_COMPACT,
} LatxJmpCacheVersion;

extern LatxJmpCacheVersion option_jmp_cache_version;
const char *latx_jmp_cache_version_name(LatxJmpCacheVersion version);

static inline bool latx_jmp_cache_compact_enabled(void)
{
    return option_jmp_cache_version == LATX_JMP_CACHE_COMPACT;
}

typedef enum LatxJmpCacheHashMode {
    LATX_JMP_CACHE_HASH_PAGE,
    LATX_JMP_CACHE_HASH_FULL,
    LATX_JMP_CACHE_HASH_LINE,
} LatxJmpCacheHashMode;

extern LatxJmpCacheHashMode option_jmp_cache_hash;
extern int option_jmp_cache_bits;
/* Default-on SYS optimizations, masked off for incompatible modes. */
extern unsigned option_arch_opt;
extern unsigned option_njc_layout;
/* Shared switch for addressing optimizations based on table layout. */
extern int option_table_addr;
/* 0=original, 2=leaf lookup with a source-local target jump. */
extern unsigned option_njc_source_jump;
extern unsigned option_irq_outline;
extern unsigned option_irq_backedge;
extern unsigned option_tb_htable_bits;
const char *latx_jmp_cache_hash_name(LatxJmpCacheHashMode mode);

static inline LatxJmpCacheHashMode latx_jmp_cache_hash_mode(void)
{
    return option_jmp_cache_hash;
}

static inline int latx_jmp_cache_bits(void)
{
    return option_jmp_cache_bits;
}

static inline unsigned int latx_jmp_cache_size(void)
{
    return 1U << latx_jmp_cache_bits();
}

extern int option_fast_htb_lookup;
extern int option_flaged_sht;
extern int option_lazypc;
extern int option_link_opt;
extern int option_lock_and_lookup_level;
#define LAL_INIT_LEVEL  0
#define lal_enable()    lal_level(1)
static inline bool lal_level(int n) {
    return option_lock_and_lookup_level >= n;
}

extern int option_fast_itlb;
extern int option_fast_itlb_ptw;
extern int option_pcid;
extern int option_pcid_asid;
extern int option_pcid_cache;
extern int option_pcid_stats;

extern int option_opt_syscall;
extern int option_opt_sysret;
extern int option_opt_iret;
extern int option_opt_rdtsc_rdtscp;
extern int option_opt_rdtsc_freq;
#define CONFIG_TSC_FREQ
extern int option_opt_mmio;
static inline int option_opt_mmio_apic(void) { return option_opt_mmio & 0x1; }
static inline int option_opt_mmio_hpet(void) { return option_opt_mmio & 0x2; }

extern int option_int_opt;
#define IRQ_CHECK_NO_OPT        0
// 模式1：每个基本块开始插入中断检测代码
#define IRQ_CHECK_BLOCK_START   1
// 模式2：选择性在基本块结束插入中断检测代码
#define IRQ_CHECK_BLOCK_END     2
// 模式3：产生中断时异步通知vcpu，基本块内无主动检测
#define IRQ_CHECK_ASYNC_NOTIFY  3

/*
 * Ordinary fallback code retains its checked exits. LLVM code is interrupted
 * through the HAMT snapshot path and never emits these checks.
 */
static inline bool latxs_check_interrupt_at_block_end(void)
{
    return option_int_opt == IRQ_CHECK_BLOCK_END ||
           option_int_opt == IRQ_CHECK_ASYNC_NOTIFY;
}


extern int option_imm_cache;
extern int option_imm_rip;
extern int option_imm_pattern;
extern int option_imm_debug;
extern int option_imm_mode;
extern uint64_t imm_skip_pc;
extern uint64_t imm_pc_mask;
extern uint64_t imm_pc_prefix;

extern int option_inhibit_irq;
static inline int  inhibit_irq_default(void) { return 0x7; }
static inline bool inhibit_irq_hflags(void) { return option_inhibit_irq & 0x1; }
static inline bool inhibit_irq_hflags_link(void) { return option_inhibit_irq & 0x2; }
static inline bool inhibit_irq_hflags_link_indir(void) { return option_inhibit_irq & 0x4; }

static inline bool cp_enabled(void)   { return (option_cross_page & 0xf) != 0x0; }
static inline bool cp_link(void)   { return (option_cross_page & 0xf) == 0x1; }
static inline bool cp_check(void)  { return (option_cross_page & 0xf) == 0x5; }
static inline bool cp_tb_enabled(void)   { return (option_cross_page & 0xf0) != 0x00; }
static inline bool cp_tb_link(void)   { return (option_cross_page & 0xf0) == 0x10; }
static inline bool cp_tb_check(void)  { return (option_cross_page & 0xf0) == 0x50; }
#define CP_INLINE_P0 0x100
static inline bool cp_inline_p0(void)
{
    return (option_cross_page & CP_INLINE_P0) && cp_check();
}
#define CP_INLINE_P0_MERGED_CHECK 0x200
static inline bool cp_inline_p0_merged_check(void)
{
    return (option_cross_page & CP_INLINE_P0_MERGED_CHECK) && cp_inline_p0();
}
#define TUCPFAST_ENABLE              0x01
#define TUCPFAST_TU_DIRECT           0x02
#define TUCPFAST_DIRECT_CHECK        0x04
#define TUCPFAST_JMP_CACHE           0x08
#define TUCPFAST_FSHT_CHECK          0x10
#define TUCPFAST_LOOKUP_CHECK        0x20
#define TUCPFAST_ALL                 (TUCPFAST_ENABLE | TUCPFAST_TU_DIRECT | \
                                     TUCPFAST_DIRECT_CHECK | TUCPFAST_JMP_CACHE | \
                                     TUCPFAST_FSHT_CHECK | TUCPFAST_LOOKUP_CHECK)
#define TBLINK_STUB_NONE            0
#define TBLINK_STUB_DIRECT          1
#define TBLINK_STUB_CPCHECK         2
#define TBLINK_STUB_TUCHECK         3
#define TBLINK_STUB_MASK            0xff
#define TBLINK_STUB_CPLABEL         0x100
#define TBLINK_STUB_MODE(n)         ((n) & TBLINK_STUB_MASK)
#define TBLINK_STUB_NEED_CPLABEL(n) ((n) & TBLINK_STUB_CPLABEL)

static inline int cp_tu(void) { return option_cross_page_tu & 0x1; }
static inline int cp_tu_gen_tb(void) { return option_cross_page_tu & 0x2; }
static inline int cp_tu_cptb(void) { return option_cross_page_tu & 0x4; }
static inline int cp_tu_pages(void) { return option_cross_page_tu & 0x8; }
static inline int cp_tu_page_num(void) {
    return ((option_cross_page_tu & 0xf00) >> 8) + 1;
}
static inline bool tucp_fast(void)
{
    return option_tucp_fast & TUCPFAST_ENABLE;
}

static inline bool tucp_fast_tu_direct(void)
{
    return tucp_fast() && (option_tucp_fast & TUCPFAST_TU_DIRECT);
}

static inline bool tucp_fast_direct_check(void)
{
    return tucp_fast() && (option_tucp_fast & TUCPFAST_DIRECT_CHECK);
}

static inline bool tucp_fast_needs_pages_desc(void)
{
    return tucp_fast() && (option_tucp_fast &
            (TUCPFAST_DIRECT_CHECK | TUCPFAST_JMP_CACHE |
             TUCPFAST_FSHT_CHECK | TUCPFAST_LOOKUP_CHECK));
}

/* TLB invalidation cannot clear a page bucket for a non-page hash, or
 * when cached TUs can depend on pages other than their entry page. */
static inline bool jmp_cache_needs_full_flush(void)
{
    return latx_jmp_cache_hash_mode() != LATX_JMP_CACHE_HASH_PAGE ||
           tucp_fast_needs_pages_desc();
}

static inline bool tucp_fast_fsht_check(void)
{
    /* FSHT hits refill NJC: every TU with a descriptor must be checked
     * before publication, even when the explicit FSHT check bit is off. */
    return (tucp_fast() && (option_tucp_fast & TUCPFAST_FSHT_CHECK)) ||
           (option_intb_njc && tucp_fast_needs_pages_desc());
}

static inline bool tucp_fast_lookup_check(void)
{
    return tucp_fast() && (option_tucp_fast & TUCPFAST_LOOKUP_CHECK);
}

static inline bool tucp_fast_link_check(void)
{
    return tucp_fast_direct_check() || tucp_fast_fsht_check();
}

#define TUCPGEN_MEMSET  1
#define TUCPGEN_COUNTER 2
#define TUCPGEN_OFFSET  3

static inline bool tucp_generation_enabled(void)
{
    return option_tucp_generation != 0;
}

static inline bool tucp_generation_memset(void)
{
    return option_tucp_generation == TUCPGEN_MEMSET;
}

static inline bool tucp_generation_counter(void)
{
    return option_tucp_generation == TUCPGEN_COUNTER;
}

static inline bool tucp_generation_offset(void)
{
    return option_tucp_generation == TUCPGEN_OFFSET;
}

static inline uint64_t tucp_generation_entry_size(void)
{
    return tucp_generation_counter() ? sizeof(uint16_t) : sizeof(uint8_t);
}

static inline void __enable_dump_all(void)
{
    option_dump      = 1;
    option_dump_ir1  = 1;
    option_dump_ir2  = 1;
    option_dump_host = 1;
}

static inline void __disable_dump_all(void)
{
    option_dump      = 0;
    option_dump_ir1  = 0;
    option_dump_ir2  = 0;
    option_dump_host = 0;
}

#include "hamt-options.h"

extern int option_sys_flag_reduction;
extern int option_sys_flag_reduction_tu;
extern int option_sys_flag_reduction_tb;
#define FLAGRDTN_TU_PARSE_USE   (0x1 << 0)
#define FLAGRDTN_TU_NEXT        (0x1 << 1)
#define FLAGRDTN_TU_PARSE_USE2  (0x1 << 2)
#define FLAGRDTN_TU_PRE_DISASM  (0x1 << 3)
#define FLAGRDTN_TU_CMP_JCC     (0x1 << 4)
#define FLAGRDTN_TU_TEST_JCC    (0x1 << 5)
#define FLAGRDTN_TU_CMP_XX_JCC  (0x1 << 6)
#define FLAGRDTN_TU_TEST_XX_JCC (0x1 << 7)
#define FLAGRDTN_TU_ADD_JCC     (0x1 << 8)
#define FLAGRDTN_TU_SUB_JCC     (0x1 << 9)
#define FLAGRDTN_TU_AND_JCC     (0x1 << 10)
#define FLAGRDTN_TU_BT_JCC      (0x1 << 11)
#define FLAGRDTN_TU_BT_XX_JCC   (0x1 << 12)
#define FLAGRDTN_TU_OR_JCC      (0x1 << 13)
#define FLAGRDTN_TU_OR_XX_JCC   (0x1 << 14)
#define FLAGRDTN_TU_XOR_JCC     (0x1 << 15)
#define FLAGRDTN_TU_XOR_XX_JCC  (0x1 << 16)
#define FLAGRDTN_TU_TEST_XXCC_CON (0x1 << 17)
#define FLAGRDTN_TU_SAR_JCC     (0x1 << 18)
#define FLAGRDTN_TU_AND_XX_JCC  (0x1 << 19)
#define FLAGRDTN_TU_SHR_JCC     (0x1 << 20)
#define FLAGRDTN_TU_SUB_XX_JCC  (0x1 << 21)
#define FLAGRDTN_TU_SHR_XX_JCC  (0x1 << 22)
#define FLAGRDTN_TU_DEC_JCC     (0x1 << 23)
#define FLAGRDTN_TU_DEFAULT     (0xfdffff)
#define FLAGRDTN_TU_NEED_RECOVER_MASK \
    (FLAGRDTN_TU_TEST_XX_JCC | FLAGRDTN_TU_CMP_XX_JCC | \
     FLAGRDTN_TU_BT_XX_JCC | FLAGRDTN_TU_OR_XX_JCC | \
     FLAGRDTN_TU_XOR_XX_JCC | FLAGRDTN_TU_TEST_XXCC_CON | \
     FLAGRDTN_TU_AND_XX_JCC | FLAGRDTN_TU_SUB_XX_JCC | \
     FLAGRDTN_TU_SHR_XX_JCC)
static inline int flag_reduction_tu_parse_use(void) {
    return option_sys_flag_reduction_tu & FLAGRDTN_TU_PARSE_USE; }
static inline int flag_reduction_tu_next(void) {
    return option_sys_flag_reduction_tu & FLAGRDTN_TU_NEXT; }
static inline int flag_reduction_tu_parse_use2(void) {
    return option_sys_flag_reduction_tu & FLAGRDTN_TU_PARSE_USE2; }
static inline int flag_reduction_tu_pre_disasm(void) {
    return option_sys_flag_reduction_tu & FLAGRDTN_TU_PRE_DISASM; }
static inline int flag_reduction_tu_cmp_jcc(void) {
    return option_sys_flag_reduction_tu & FLAGRDTN_TU_CMP_JCC; }
static inline int flag_reduction_tu_test_jcc(void) {
    return option_sys_flag_reduction_tu & FLAGRDTN_TU_TEST_JCC; }
static inline int flag_reduction_tu_cmp_xx_jcc(void) {
    return option_sys_flag_reduction_tu & FLAGRDTN_TU_CMP_XX_JCC; }
static inline int flag_reduction_tu_test_xx_jcc(void) {
    return option_sys_flag_reduction_tu & FLAGRDTN_TU_TEST_XX_JCC; }
static inline int flag_reduction_tu_need_recover(void) {
    return option_sys_flag_reduction_tu & FLAGRDTN_TU_NEED_RECOVER_MASK;
}
// bit 0: defer the TB-final fused compare+branch M-EFLAGS materialisation to
// the TB exit slow paths (TB mode only).
#define FLAGRDTN_TB_DEFER   (0x1 << 0)
/*
 * bit 1: additionally enable the HAMT in-flight-recovery protocol
 * ($ra marker + entry clear + invalid-entry handler recovery).  This is
 * validation-only: without it, sysflagtb is forcibly disabled under HAMT
 * because a poisoned successor entry can be reached by a jump that already
 * executed with deferred flags.  Do not default this on until the recovery
 * protocol has passed the full guest correctness matrix.
 */
#define FLAGRDTN_TB_HAMT_RECOVER (0x1 << 1)
static inline int flag_reduction_tb_defer(void) {
    return option_sys_flag_reduction_tb & FLAGRDTN_TB_DEFER;
}
static inline int flag_reduction_tb_hamt_recover(void) {
    return option_sys_flag_reduction_tb & FLAGRDTN_TB_HAMT_RECOVER;
}
static inline int flag_reduction_tu_add_jcc(void) {
    return option_sys_flag_reduction_tu & FLAGRDTN_TU_ADD_JCC; }
static inline int flag_reduction_tu_sub_jcc(void) {
    return option_sys_flag_reduction_tu & FLAGRDTN_TU_SUB_JCC; }
static inline int flag_reduction_tu_and_jcc(void)
{
    return option_sys_flag_reduction_tu & FLAGRDTN_TU_AND_JCC;
}
static inline int flag_reduction_tu_and_xx_jcc(void)
{
    return option_sys_flag_reduction_tu & FLAGRDTN_TU_AND_XX_JCC;
}
static inline int flag_reduction_tu_bt_jcc(void)
{
    return option_sys_flag_reduction_tu & FLAGRDTN_TU_BT_JCC;
}
static inline int flag_reduction_tu_bt_xx_jcc(void)
{
    return option_sys_flag_reduction_tu & FLAGRDTN_TU_BT_XX_JCC;
}
static inline int flag_reduction_tu_or_jcc(void)
{
    return option_sys_flag_reduction_tu & FLAGRDTN_TU_OR_JCC;
}
static inline int flag_reduction_tu_or_xx_jcc(void)
{
    return option_sys_flag_reduction_tu & FLAGRDTN_TU_OR_XX_JCC;
}
static inline int flag_reduction_tu_xor_jcc(void)
{
    return option_sys_flag_reduction_tu & FLAGRDTN_TU_XOR_JCC;
}
static inline int flag_reduction_tu_xor_xx_jcc(void)
{
    return option_sys_flag_reduction_tu & FLAGRDTN_TU_XOR_XX_JCC;
}
static inline int flag_reduction_tu_test_xxcc_con(void)
{
    return option_sys_flag_reduction_tu & FLAGRDTN_TU_TEST_XXCC_CON;
}
static inline int flag_reduction_tu_sar_jcc(void)
{
    return option_sys_flag_reduction_tu & FLAGRDTN_TU_SAR_JCC;
}
static inline int flag_reduction_tu_shr_jcc(void)
{
    return option_sys_flag_reduction_tu & FLAGRDTN_TU_SHR_JCC;
}
static inline int flag_reduction_tu_sub_xx_jcc(void)
{
    return option_sys_flag_reduction_tu & FLAGRDTN_TU_SUB_XX_JCC;
}
static inline int flag_reduction_tu_shr_xx_jcc(void)
{
    return option_sys_flag_reduction_tu & FLAGRDTN_TU_SHR_XX_JCC;
}
static inline int flag_reduction_tu_dec_jcc(void)
{
    return option_sys_flag_reduction_tu & FLAGRDTN_TU_DEC_JCC;
}
extern uint64_t option_instptn;
extern int option_risk_mem_offset;
extern int option_risk_simd_ignore_upper64;
extern int option_risk_simd_ignore_upper64_stat_track;
static inline int latxs_simd_stat_tracker(void)
{
    return option_risk_simd_ignore_upper64 &&
           option_risk_simd_ignore_upper64_stat_track;
}
extern int option_risk_in_379;
extern int option_lsfpu;
extern int option_fast_fpr_ldst;
extern int option_by_hand_64;

extern int option_soft_fpu;
extern int option_soft_fpu_fast;
extern int option_sigfpe;
extern int option_fast_masked_v;
extern int option_no_reserve_256;
extern int option_lazy_mxcsr;
extern int option_fast_atomic;
extern int option_fast_excep;
extern int option_aes_opt;
extern int option_enable_lasx;

extern int option_common_slowpath;
/* Move the per-access soft-TLB-miss anchor (pcaddi a3) out of the hot
 * fast path into the per-access cold stub.  Requires option_common_slowpath. */
extern int option_softmmu_pcaddi_cold;
extern int option_softmmu_rmw_reuse;
extern int option_new_assemble;

extern int option_large_code_cache;

void latxs_options_init(void);
void latxs_options_init2(void);
void latx_parse_env_options(void);
bool get_pushpop_option(void *p);

void option_config_dump(int cpl);

void latx_disable_all_tblink(void);

void latx_enable_dump(void);
void latx_disable_dump(void);

#endif
