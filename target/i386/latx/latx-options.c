/*
 * SPDX-FileCopyrightText: 2024-2026 LATS Project Authors
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "error.h"
#include "latx-options.h"
#include "ir1.h"
#include "latx-hamt.h"
#include "qemu/osdep.h"
#include "lsenv.h"
#include "host/cpuinfo.h"
#include <larchintrin.h>
#include <stdarg.h>

bool option_verbose;

int option_tb_config;
int option_tb_max_insns;

int option_dump;
int option_dump_host;
int option_dump_ir1;
int option_dump_ir2;
int option_dump_cpl3;

int option_trace_simple;
unsigned long long option_trace_start_nr;
unsigned long long option_trace_start_tb;
int option_trace_start_tb_set;
int option_traceir1;
int option_traceir1_data;
int option_smmu_slow;
int option_split_strins;
int option_tb_flush_on_reboot;

int option_pushpop;
int option_pushpop_ret;
int option_pause;
int option_soft_ras;
int option_rastoptp;
/* Experimental TU-JIT comparison: 0=original, 1=direct B, 2=direct BL. */
int option_tu_hras;
int option_tu_hras_opt_copy;
unsigned option_tu_mode;
int option_tu_max_tb;
int option_aot_tu_max_tb;
int option_shbr;
int option_ghbr;
int option_split_tb;
int option_split_search;
int option_region_prologue;
/* for tb-grained reg map for eip, sp = env->eip */
int option_tb_pcrel = 0;
/* for page-grained reg map, sp = env->eip & PAGE_MASK */
int option_page_pcrel = 0;
int option_sys_aot;
int option_aot_save;
int option_aot_fork = 0;
int option_wait_aot = 0;
int option_aot_load;
int option_aot_tu_pretranslate;
int option_aot_tu_pre_disasm;
int option_aot_verify = 0;
int option_aot_debug = 0;
int option_aot_gpr_highbits;
int option_tu_gpr_highbits;
int option_aot_cross_page_tu;
int option_llvm_aot = 0;

int option_tb_record = 0;
const char *option_tb_record_path = NULL;

int option_staticcs;

int option_tb_link;
int option_cross_page;
int option_cross_page_tu;
int option_tucp_fast;
bool option_external_tu_stub;
int option_tucp_generation;
uint64_t option_tucp_generation_array_size;
int option_tb_counter;
int option_cross_page_counter;
int option_intb_counter;
int option_intb_link;
int option_intb_njc;
int option_intb_njc_inline;
LatxJmpCacheVersion option_jmp_cache_version;
LatxJmpCacheHashMode option_jmp_cache_hash;
int option_jmp_cache_bits;
unsigned option_njc_layout;
int option_table_addr;
unsigned option_njc_source_jump;
unsigned option_irq_outline;
unsigned option_irq_backedge;
unsigned option_tb_htable_bits;
unsigned option_arch_opt;
static bool option_jmp_cache_bits_set;
int option_lazypc;
int option_link_opt;
int option_lock_and_lookup_level;

int option_int_opt;
int option_inhibit_irq;

int option_hamt;
int option_hamt_mode;
int option_hamt_submode;
int option_hamt_config;
int option_hamt_dump;
int option_hamt_print;
int option_htimer;
int option_irq_gate_nobql;

int option_ir1_cache_ways;
int option_ir1_cache_replace;
int option_ir1_cache_enabled;
int option_ir1_cache_mode;
int option_hot_decode_enabled;
uint64_t option_ir1_cache_entries;
int option_ir1_timing;
int option_ir1_distribution;
int option_ir1_profile;
int option_ir1_fast_cache_entries;
int option_ir1_hash;

int option_sys_flag_reduction;
int option_sys_flag_reduction_tu;
uint64_t option_instptn;
int option_risk_mem_offset;
int option_risk_simd_ignore_upper64;
int option_risk_simd_ignore_upper64_stat_track;
int option_risk_in_379;
int option_fast_atomic;
int option_fast_excep;
int option_lsfpu;
int option_fast_fpr_ldst;
int option_by_hand_64; /* 64-bit byhand */

int option_no_reserve_256;
int option_soft_fpu;
int option_soft_fpu_fast;
int option_sigfpe;
int option_fast_masked_v;
int option_lazy_mxcsr;
int option_flaged_sht;
int option_fast_itlb;
int option_fast_itlb_ptw;
int option_pcid;
int option_pcid_asid;
int option_pcid_cache;
int option_pcid_stats;
int option_aes_opt;
int option_enable_lasx;

int option_common_slowpath;
int option_softmmu_pcaddi_cold;
int option_softmmu_rmw_reuse;
int option_new_assemble;

int option_large_code_cache;
int option_opt_syscall;
int option_opt_sysret;
int option_opt_iret;

int option_opt_mmio;
int option_opt_rdtsc_rdtscp;
int option_opt_rdtsc_freq;

int option_imm_cache;
int option_imm_rip;
int option_imm_pattern;
int option_imm_debug;
int option_imm_mode = 0; //0 static 1 dynamic(use dynamic option)
uint64_t imm_skip_pc;
uint64_t imm_pc_mask;
uint64_t imm_pc_prefix;

#define OPTIONS_DUMP_FUNC 0
#define OPTIONS_DUMP_IR1 1
#define OPTIONS_DUMP_IR2 2
#define OPTIONS_DUMP_HOST 3
#define OPTIONS_DUMP_CPL3 4

uint64_t qcondwait3_max = 100000;

void option_config_dump(int cpl)
{
    if (option_dump_cpl3) {
        if (cpl == 3) __enable_dump_all();
        else __disable_dump_all();
    }
}

bool get_pushpop_option(void *p)
{
    TRANSLATION_DATA *td = (TRANSLATION_DATA *)p;
    if (option_pushpop &&
        (td->sys.flags & HF_CPL_MASK) &&
        td->curr_ir1_nr < (td->ir1_nr - 1)) {
        return true;
    }
    return false;
}

static void options_parse_dump(const char *bits)
{
    if (!bits) {
        return;
    }

    if (bits[OPTIONS_DUMP_CPL3] == '1') {
        option_dump_cpl3 = 1;
    } else if (bits[OPTIONS_DUMP_CPL3] == '0') {
        option_dump_cpl3 = 0;
    } else {
        lsassertm(0, "wrong options for dump cpl3.");
    }

    if (bits[OPTIONS_DUMP_IR1] == '1') {
        option_dump_ir1 = 1;
    } else if (bits[OPTIONS_DUMP_IR1] == '0') {
        option_dump_ir1 = 0;
    } else {
        lsassertm(0, "wrong options for dump ir1.");
    }

    if (bits[OPTIONS_DUMP_IR2] == '1') {
        option_dump_ir2 = 1;
    } else if (bits[OPTIONS_DUMP_IR2] == '0') {
        option_dump_ir2 = 0;
    } else {
        lsassertm(0, "wrong options for dump ir2.");
    }

    if (bits[OPTIONS_DUMP_HOST] == '1') {
        option_dump_host = 1;
    } else if (bits[OPTIONS_DUMP_HOST] == '0') {
        option_dump_host = 0;
    } else {
        lsassertm(0, "wrong options for dump host.");
    }

    if (bits[OPTIONS_DUMP_FUNC] == '1') {
        option_dump = 1;
    } else if (bits[OPTIONS_DUMP_FUNC] == '0') {
        option_dump = 0;
    } else {
        lsassertm(0, "wrong options for dump func.");
    }
}

#define OPTIONS_IMM_CACHE 0
#define OPTIONS_IMM_RIP 1
#define OPTIONS_IMM_PATTERN 2
#define OPTIONS_IMM_DEBUG 3
static void options_parse_imm_cache(const char *bits)
{
    if (!bits) {
        return;
    }

    if (strlen(bits) != OPTIONS_IMM_DEBUG + 1 ||
        strspn(bits, "01") != OPTIONS_IMM_DEBUG + 1) {
        fprintf(stderr,
                "ERROR: imm_cache expects exactly four binary digits.\n");
        _exit(1);
    }

    if (bits[OPTIONS_IMM_DEBUG] == '1') {
        option_imm_debug = 1;
    } else if (bits[OPTIONS_IMM_DEBUG] == '0') {
        option_imm_debug = 0;
    } else {
        lsassertm(0, "wrong options for imm debug.");
    }

    if (bits[OPTIONS_IMM_PATTERN] == '1') {
        option_imm_pattern = 1;
    } else if (bits[OPTIONS_IMM_PATTERN] == '0') {
        option_imm_pattern = 0;
    } else {
        lsassertm(0, "wrong options for imm pattern.");
    }

    if (bits[OPTIONS_IMM_RIP] == '1') {
        option_imm_rip = 1;
    } else if (bits[OPTIONS_IMM_RIP] == '0') {
        option_imm_rip = 0;
    } else {
        lsassertm(0, "wrong options for imm rip.");
    }

    if (bits[OPTIONS_IMM_CACHE] == '1') {
        option_imm_cache = 1;
    } else if (bits[OPTIONS_IMM_CACHE] == '0') {
        option_imm_cache = 0;
    } else {
        lsassertm(0, "wrong options for imm cache.");
    }
}

#include "./include/qemu-def.h"
#include "qemu/option.h"
#include "qemu/config-file.h"
#include "qemu/option_int.h"
#include "qemu/cutils.h"

#define parse_options_bool(name, var) do {  \
    opt = qemu_opt_find(opts, name);        \
    if (opt) {                              \
        var = (opt->value.boolean) ? 1 : 0; \
    }                                       \
} while (0)

#define parse_options_uint(name, var) do {  \
    opt = qemu_opt_find(opts, name);        \
    if (opt) {                              \
        var = opt->value.uint;              \
    }                                       \
} while (0)

static void options_set_tucpopt(const char *mode)
{
    if (strcmp(mode, "max") && strcmp(mode, "ultra") &&
        strcmp(mode, "balance") && strcmp(mode, "off")) {
        error_report("invalid tucpopt %s: expected max, ultra, balance or off",
                     mode);
        exit(EXIT_FAILURE);
    }

    bool off = !strcmp(mode, "off");
    bool max = !strcmp(mode, "max");

    option_cross_page_tu = off ? 0 :
        (!strcmp(mode, "balance") ? 0x30f : 0x70f);
    option_tu_max_tb = off ? 50 : 128;
    option_tucp_fast = max ? TUCPFAST_ALL : 0;
    option_tucp_generation = max ? TUCPGEN_OFFSET : 0;
}

static void parse_options_tucpopt(QemuOpts *opts)
{
    QemuOpt *opt = qemu_opt_find(opts, "tucpopt");

    if (opt) {
        options_set_tucpopt(opt->str);
    }
}

static void parse_options_tucpfast(QemuOpts *opts)
{
    QemuOpt *opt = qemu_opt_find(opts, "tucpfast");
    uint64_t value;
    const char *end;

    if (!opt) {
        return;
    }
    if (!strcmp(opt->str, "on")) {
        option_tucp_fast = TUCPFAST_ALL;
        return;
    }
    if (!strcmp(opt->str, "off")) {
        option_tucp_fast = 0;
        return;
    }
    if (qemu_strtou64(opt->str, &end, 0, &value) || *end ||
        value > UINT32_MAX) {
        error_report("invalid tucpfast value: %s", opt->str);
        exit(1);
    }
    option_tucp_fast = value;
}

const char *latx_jmp_cache_hash_name(LatxJmpCacheHashMode mode)
{
    switch (mode) {
    case LATX_JMP_CACHE_HASH_PAGE:
        return "page";
    case LATX_JMP_CACHE_HASH_FULL:
        return "full";
    case LATX_JMP_CACHE_HASH_LINE:
        return "line";
    default:
        return "invalid";
    }
}

const char *latx_jmp_cache_version_name(LatxJmpCacheVersion version)
{
    switch (version) {
    case LATX_JMP_CACHE_LEGACY:
        return "legacy";
    case LATX_JMP_CACHE_COMPACT:
        return "compact";
    default:
        return "invalid";
    }
}

static void parse_options_njcver(QemuOpts *opts)
{
    QemuOpt *opt = qemu_opt_find(opts, "njcver");

    if (!opt) {
        return;
    }
    if (!strcmp(opt->str, "legacy")) {
        option_jmp_cache_version = LATX_JMP_CACHE_LEGACY;
    } else if (!strcmp(opt->str, "compact")) {
        option_jmp_cache_version = LATX_JMP_CACHE_COMPACT;
    } else {
        error_report("invalid njcver value: %s", opt->str);
        _exit(1);
    }
}

static void parse_options_njchash(QemuOpts *opts)
{
    QemuOpt *opt = qemu_opt_find(opts, "njchash");

    if (!opt) {
        return;
    }
    if (!strcmp(opt->str, "page")) {
#ifndef CONFIG_SOFTMMU
        fprintf(stderr, "WARNING: njchash=page is not supported in user mode; "
                "using full instead.\n");
        option_jmp_cache_hash = LATX_JMP_CACHE_HASH_FULL;
#else
        option_jmp_cache_hash = LATX_JMP_CACHE_HASH_PAGE;
#endif
    } else if (!strcmp(opt->str, "full")) {
        option_jmp_cache_hash = LATX_JMP_CACHE_HASH_FULL;
    } else if (!strcmp(opt->str, "line")) {
        option_jmp_cache_hash = LATX_JMP_CACHE_HASH_LINE;
    } else {
        error_report("invalid njchash value: %s", opt->str);
        _exit(1);
    }
}

static void parse_options_njcbits(QemuOpts *opts)
{
    QemuOpt *opt = qemu_opt_find(opts, "njcbits");
    uint64_t value;
    const char *end;

    if (!opt) {
        return;
    }
    if (qemu_strtou64(opt->str, &end, 0, &value) || *end ||
        value < 8 || value > 20) {
        error_report("invalid njcbits value: %s (expected an integer from "
                     "8 through 20)", opt->str);
        _exit(1);
    }
    option_jmp_cache_bits = value;
    option_jmp_cache_bits_set = true;
}

static void parse_options_tucpgen(QemuOpts *opts)
{
    QemuOpt *opt = qemu_opt_find(opts, "tucpgen");
    uint64_t value;
    const char *end;

    if (!opt) {
        return;
    }
    if (!strcmp(opt->str, "on") || !strcmp(opt->str, "memset")) {
        option_tucp_generation = TUCPGEN_MEMSET;
        return;
    }
    if (!strcmp(opt->str, "counter")) {
        option_tucp_generation = TUCPGEN_COUNTER;
        return;
    }
    if (!strcmp(opt->str, "offset")) {
        option_tucp_generation = TUCPGEN_OFFSET;
        return;
    }
    if (!strcmp(opt->str, "off")) {
        option_tucp_generation = 0;
        return;
    }
    if (qemu_strtou64(opt->str, &end, 0, &value) || *end ||
        value > TUCPGEN_OFFSET) {
        error_report("invalid tucpgen value: %s", opt->str);
        exit(1);
    }
    option_tucp_generation = value;
}

static void parse_options_tu_mode_bit(QemuOpts *opts, const char *name,
                                      unsigned bit)
{
    QemuOpt *opt = qemu_opt_find(opts, name);

    if (!opt) {
        return;
    }
    if (opt->value.boolean) {
        option_tu_mode |= bit;
    } else {
        option_tu_mode &= ~bit;
    }
}

static void parse_options_shbr(QemuOpts *opts)
{
    QemuOpt *opt = qemu_opt_find(opts, "shbr");
    uint64_t value;
    const char *end;

    if (!opt) {
        return;
    }
    if (!strcmp(opt->str, "on")) {
        option_shbr = LATXS_SHBR_BASIC;
        return;
    }
    if (!strcmp(opt->str, "off")) {
        option_shbr = LATXS_SHBR_DISABLED;
        return;
    }
    if (qemu_strtou64(opt->str, &end, 0, &value) || *end ||
        value > LATXS_SHBR_AGGRESSIVE) {
        error_report("invalid shbr level: %s (expected 0..3, on, or off)",
                     opt->str);
        exit(1);
    }
    option_shbr = value;
}

static void parse_options_ghbr(QemuOpts *opts)
{
    QemuOpt *opt = qemu_opt_find(opts, "ghbr");
    uint64_t value;
    const char *end;

    if (!opt) {
        return;
    }
    if (!strcmp(opt->str, "on")) {
        option_ghbr = LATXS_GHBR_BASIC;
        return;
    }
    if (!strcmp(opt->str, "off")) {
        option_ghbr = LATXS_GHBR_DISABLED;
        return;
    }
    if (qemu_strtou64(opt->str, &end, 0, &value) || *end ||
        value > LATXS_GHBR_IGNORE_MEMORY) {
        error_report("invalid ghbr level: %s (expected 0..2, on, or off)",
                     opt->str);
        exit(1);
    }
    option_ghbr = value;
}

void dump_latxs_options(void);

static FILE *latx_options_fp;
static char latx_options_path[64];

static void latx_options_dump_cleanup(void)
{
    if (latx_options_fp) {
        fclose(latx_options_fp);
        latx_options_fp = NULL;
    }

    if (latx_options_path[0]) {
        unlink(latx_options_path);
        latx_options_path[0] = '\0';
    }
}

static void latx_options_dump_prepare(void)
{
    int flags;
    int fd;

    if (!option_verbose || latx_options_fp) {
        return;
    }

    snprintf(latx_options_path, sizeof(latx_options_path),
             "/tmp/lats-%ld.options", (long)getpid());
    flags = O_WRONLY | O_CREAT | O_TRUNC;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    fd = open(latx_options_path, flags, 0600);
    if (fd < 0) {
        latx_options_path[0] = '\0';
        return;
    }

    fchmod(fd, 0600);
#ifndef O_CLOEXEC
    fcntl(fd, F_SETFD, FD_CLOEXEC);
#endif
    latx_options_fp = fdopen(fd, "w");
    if (!latx_options_fp) {
        close(fd);
        unlink(latx_options_path);
        latx_options_path[0] = '\0';
        return;
    }

    if (atexit(latx_options_dump_cleanup) != 0) {
        latx_options_dump_cleanup();
    }
}

static void G_GNUC_PRINTF(1, 2) latx_options_printf(const char *fmt, ...)
{
    va_list ap;
    va_list aq;
    FILE *fp;

    if (!option_verbose) {
        return;
    }

    va_start(ap, fmt);
    va_copy(aq, ap);

    vprintf(fmt, ap);
    fp = latx_options_fp;
    if (fp) {
        vfprintf(fp, fmt, aq);
        fflush(fp);
    }

    va_end(aq);
    va_end(ap);
}

QemuOptsList qemu_latx_opts = {
    .name = "latx",
    .implied_opt_name="optm",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_latx_opts.head),
    .desc = {
        {
            .name = "optm",
            .type = QEMU_OPT_STRING,
            .help = "none/select/all optimizations to enable",
        }, {
            .name = "ir1cachemode",
            .type = QEMU_OPT_STRING,
            .help = "IR1 cache preset: off, on (fast 4096/main 16384) or "
                    "max (fast 32768/main 65536), default: on",
        }, {
            .name = "ir1cache",
            .type = QEMU_OPT_BOOL,
            .help = "enable/disable the IR1 decode cache (default: on)",
        }, {
            .name = "hotdecode",
            .type = QEMU_OPT_BOOL,
            .help = "enable/disable the IR1 hot decoder (default: on)",
        }, {
            .name = "ir1cacheentries",
            .type = QEMU_OPT_NUMBER,
            .help = "IR1 decode cache entries; power of two (default: 16384)",
        }, {
            .name = "ir1cacheways",
            .type = QEMU_OPT_NUMBER,
            .help = "IR1 decode cache associativity: 1, 2 or 4 (default: 4)",
        }, {
            .name = "ir1cachereplace",
            .type = QEMU_OPT_STRING,
            .help = "IR1 decode cache replacement: lru, fifo or random "
                    "(default: lru)",
        }, {
            .name = "ir1timing",
            .type = QEMU_OPT_BOOL,
            .help = "measure IR1 cache-on hit/miss or cache-off direct "
                    "disassembly time (default: off)",
        }, {
            .name = "ir1dist",
            .type = QEMU_OPT_BOOL,
            .help = "collect IR1 cache set, way, opcode and page "
                    "distribution (default: off)",
        }, {
            .name = "ir1profile",
            .type = QEMU_OPT_BOOL,
            .help = "collect IR1 cache count and timing profiling "
                    "(default: off)",
        }, {
            .name = "ir1fastcacheentries",
            .type = QEMU_OPT_NUMBER,
            .help = "IR1 fast cache entries; power of two from 64 to 32768 "
                    "(default: 4096)",
        }, {
            .name = "ir1hash",
            .type = QEMU_OPT_STRING,
            .help = "IR1 cache hash: legacy or murmur (default: murmur)",
        },{
            .name = "tblink",
            .type = QEMU_OPT_BOOL,
            .help = "enable/disable TB-link(aka TB-chaining)",
        }, {
            .name = "intblink",
            .type = QEMU_OPT_BOOL,
            .help = "enable/disable indirect TB-link",
        }, {
            .name = "sigfpe",
            .type = QEMU_OPT_BOOL,
            .help = "emulate X87fpu & sse rounding mode & exception",
        }, {
            .name = "fast_masked_v",
            .type = QEMU_OPT_BOOL,
            .help = "emulate the masked fpe v in SIGFPE handler",
        }, {
            .name = "fsht",
            .type = QEMU_OPT_BOOL,
            .help = "flaged shadow hashtable lookup",
        }, {
            .name = "fastitlb",
            .type = QEMU_OPT_BOOL,
            .help = "fast instruction TLB",
        }, {
            .name = "tableaddr",
            .type = QEMU_OPT_BOOL,
            .help = "address generation optimizations based on table layout",
        }, {
            .name = "pcid",
            .type = QEMU_OPT_BOOL,
            .help = "allow guest PCID and INVPCID in system emulation",
        }, {
            .name = "pcidasid",
            .type = QEMU_OPT_BOOL,
            .help = "retain tagged DMW translations across CR3 writes",
        }, {
            .name = "pcidcache",
            .type = QEMU_OPT_BOOL,
            .help = "retain software caches with validated PCID tags",
        }, {
            .name = "pcidstats",
            .type = QEMU_OPT_BOOL,
            .help = "count guest PCID use and ASID retention (info jit)",
        }, {
            .name = "fastitlbptw",
            .type = QEMU_OPT_BOOL,
            .help = "ptw support for fast instruction TLB",
        }, {
            .name = "newassembal",
            .type = QEMU_OPT_BOOL,
            .help = "use new ir2 assemble",
        }, {
            .name = "trbh64",
            .type = QEMU_OPT_BOOL,
            .help = "enable/disable translate by hand 64",
        }, {
            .name = "lsfpu",
            .type = QEMU_OPT_BOOL,
            .help = "enable/disable using LoongArch FPU",
        }, {
            .name = "staticcs",
            .type = QEMU_OPT_BOOL,
            .help = "use static generated codes for context switch",
        }, {
            .name = "largecc",
            .type = QEMU_OPT_BOOL,
            .help = "use pcaddu18i and jirl for large than 128MB code cache",
        }, {
            .name = "softfpu",
            .type = QEMU_OPT_BOOL,
            .help = "use soft float for x87 emulation",
        }, {
            .name = "softfpu_fast",
            .type = QEMU_OPT_NUMBER,
            .help = "use soft fast for x87 emulation",
        }, {
            .name = "ffldst",
            .type = QEMU_OPT_BOOL,
            .help = "fast fpr load/store",
        }, {
            .name = "comsmmu",
            .type = QEMU_OPT_BOOL,
            .help = "common slow path for softmmu helper",
        }, {
            .name = "smmupcaddi",
            .type = QEMU_OPT_BOOL,
            .help = "keep the softmmu slow-path pcaddi anchor in the cold path",
        }, {
            .name = "smmurmw",
            .type = QEMU_OPT_BOOL,
            .help = "experimental reuse of softmmu lookup for 64-bit ADD RMW",
        }, {
            .name = "njc",
            .type = QEMU_OPT_BOOL,
            .help = "use Native Jmp Cache lookup",
        }, {
            .name = "njcinline",
            .type = QEMU_OPT_BOOL,
            .help = "inline Native Jmp Cache lookup into translated TBs",
        }, {
            .name = "njchash",
            .type = QEMU_OPT_STRING,
            .help = "NJC hash mode: page, full, or line",
        }, {
            .name = "njcbits",
            .type = QEMU_OPT_NUMBER,
            .help = "full/line NJC size bits: an integer from 8 through 20",
        }, {
            .name = "njcver",
            .type = QEMU_OPT_STRING,
            .help = "NJC implementation: legacy or compact (default)",
        }, {
            .name = "optmmio",
            .type = QEMU_OPT_NUMBER,
            .help = "optimize TB MMIO access",
        }, {
            .name = "hamt",
            .type = QEMU_OPT_NUMBER,
            .help = "hardware accelerated memory translation",
        }, {
            .name = "hamtmode",
            .type = QEMU_OPT_NUMBER,
            .help = "HAMT mode : 1=dmw 2=hole",
        }, {
            .name = "hamtsubmode",
            .type = QEMU_OPT_NUMBER,
            .help = "HAMT sub mode",
        }, {
            .name = "hamtconfig",
            .type = QEMU_OPT_NUMBER,
            .help = "HAMT configuration",
        }, {
            .name = "hamtdump",
            .type = QEMU_OPT_NUMBER,
            .help = "dump hamt exception codes",
        }, {
            .name = "hamtprint",
            .type = QEMU_OPT_NUMBER,
            .help = "print hamt during execution",
        }, {
            .name = "htimer",
            .type = QEMU_OPT_BOOL,
            .help = "Hardware accelerated LAPIC Timer",
        }, {
            .name = "irqgatenobql",
            .type = QEMU_OPT_BOOL,
            .help = "skip BQL while maskable interrupt gates are closed",
        }, {
            .name = "sysflag",
            .type = QEMU_OPT_NUMBER,
            .help = "optimization for eflags",
        }, {
            .name = "sysflagtu",
            .type = QEMU_OPT_NUMBER,
            .help = "optimization for eflags in tu",
        }, {
            .name = "instptn",
            .type = QEMU_OPT_NUMBER,
            .help = "instruction pattern",
        }, {
            .name = "pushpop",
            .type = QEMU_OPT_BOOL,
            .help = "optimize continues push and pop",
        }, {
            .name = "pushpopret",
            .type = QEMU_OPT_BOOL,
            .help = "fold existing push/pop adjustment into a near RET",
        }, {
            .name = "risky",
            .type = QEMU_OPT_NUMBER,
            .help = "optimization with risk",
        }, {
            .name = "ras",
            .type = QEMU_OPT_BOOL,
            .help = "return address stack for call,return",
        }, {
            .name = "rastoptp",
            .type = QEMU_OPT_BOOL,
            .help = "HAMT JIT software RAS with top cached in r2 (default off)",
        }, {
            .name = "tuhras",
            .type = QEMU_OPT_NUMBER,
            .help = "JIT TU hardware return prediction mode (0..2)",
        }, {
            .name = "tuhrascopy",
            .type = QEMU_OPT_BOOL,
            .help = "copy existing local TU topology for HRAS",
        }, {
            .name = "tu",
            .type = QEMU_OPT_BOOL,
            .help = "deprecated JIT TU switch; use tumode",
        }, {
            .name = "tumode",
            .type = QEMU_OPT_STRING,
            .help = "TU translation mode: off, jit, aot, both",
        }, {
            .name = "aottu",
            .type = QEMU_OPT_BOOL,
            .help = "deprecated user AOT TU switch; use tumode",
        }, {
            .name = "aottupretb",
            .type = QEMU_OPT_BOOL,
            .help = "pretranslate unobserved same-page TBs for AOT TU",
        }, {
            .name = "aottupredisasm",
            .type = QEMU_OPT_BOOL,
            .help = "pre-disassemble successors while optimizing AOT TUs",
        }, {
            .name = "shbr",
            .type = QEMU_OPT_STRING,
            .help = "XMM high-bit redundancy elimination level (0..3)",
        }, {
            .name = "ghbr",
            .type = QEMU_OPT_STRING,
            .help = "GPR high-bit redundancy elimination level (0..2)",
        }, {
            .name = "regionprologue",
            .type = QEMU_OPT_BOOL,
            .help = "copy prologue to each region",
        }, {
            .name = "sptb",
            .type = QEMU_OPT_BOOL,
            .help = "split TB from code cache",
        }, {
            .name = "sleb",
            .type = QEMU_OPT_BOOL,
            .help = "split TB from code cache",
        }, {
            .name = "fastexcep",
            .type = QEMU_OPT_BOOL,
            .help = "fast handle exception",
        }, {
            .name = "smmu",
            .type = QEMU_OPT_BOOL,
            .help = "use softmmu slow path only (debug)",
        }, {
            .name = "spstrins",
            .type = QEMU_OPT_BOOL,
            .help = "split string instruction translation",
        }, {
            .name = "tbflushonreboot",
            .type = QEMU_OPT_BOOL,
            .help = "flush all TBs on CPU reboot (default: on)",
        }, {
            .name = "tbsizei",
            .type = QEMU_OPT_NUMBER,
            .help = "limit TB's max num of instructions. 0 for default 255",
        }, {
            .name = "tbconfig",
            .type = QEMU_OPT_NUMBER,
            .help = "config tb translation",
        }, {
            .name = "dump",
            .type = QEMU_OPT_STRING,
            .help = "dump flags for func,ir1,ir2,host",
        }, {
            .name = "traces",
            .type = QEMU_OPT_NUMBER,
            .help = "simple trace tb execution",
        }, {
            .name = "tracestnr",
            .type = QEMU_OPT_NUMBER,
            .help = "start to print trace after @number of TB's execution",
        }, {
            .name = "tracesttb",
            .type = QEMU_OPT_NUMBER,
            .help = "start to print trace after TB.PC",
        }, {
            .name = "traceir1",
            .type = QEMU_OPT_NUMBER,
            .help = "trace ir1 by type or id",
        }, {
            .name = "traceir1data",
            .type = QEMU_OPT_NUMBER,
            .help = "another value for trace ir1",
        }, {
            .name = "cp",
            .type = QEMU_OPT_NUMBER,
            .help = "cross page tb link mode",
        }, {
            .name = "tucpopt",
            .type = QEMU_OPT_STRING,
            .help = "TU cross-page preset: max (default), ultra, balance, off; individual options override",
        }, {
            .name = "tucp",
            .type = QEMU_OPT_NUMBER,
            .help = "allow tu cross page ",
        }, {
            .name = "tucpfast",
            .type = QEMU_OPT_STRING,
            .help = "fast TU page-check bitmap (on=0x3f, off=0)",
        }, {
            .name = "externaltustub",
            .type = QEMU_OPT_BOOL,
            .help = "external JIT-TB TU check stubs (system default on, user default off)",
        }, {
            .name = "tucpgen",
            .type = QEMU_OPT_STRING,
            .help = "TU check cache: off/0, memset/on/1, counter/2, offset/3",
        }, {
            .name = "tucpgensize",
            .type = QEMU_OPT_SIZE,
            .help = "per-vCPU TU generation array size",
        }, {
            .name = "tumaxtb",
            .type = QEMU_OPT_NUMBER,
            .help = "max number of TB in tu (one page)",
        }, {
            .name = "aottumaxtb",
            .type = QEMU_OPT_NUMBER,
            .help = "AOT TU TB limit, 1 to 1024 (default 1024)",
        }, {
            .name = "aottucp",
            .type = QEMU_OPT_BOOL,
            .help = "load and save AOT TUs spanning multiple guest pages",
        }, {
            .name = "aotfork",
            .type = QEMU_OPT_BOOL,
            .help = "fork AOT cache saves on code-cache flush (default: off)",
        }, {
            .name = "waitaot",
            .type = QEMU_OPT_BOOL,
            .help = "wait for the forked final AOT save at shutdown (default: off)",
        }, {
            .name = "llvmaot",
            .type = QEMU_OPT_BOOL,
            .help = "optimize saved user AOT with native LLVM backend (default: off)",
        }, {
            .name = "cpcounter",
            .type = QEMU_OPT_BOOL,
            .help = "enable counter for cross-page check (need latx-print)",
        }, {
            .name = "tbcounter",
            .type = QEMU_OPT_BOOL,
            .help = "enable counter for intb lookup (need latx-print)",
        }, {
            .name = "intbcounter",
            .type = QEMU_OPT_BOOL,
            .help = "enable counter for intb lookup (need latx-print)",
        }, {
            .name = "lazypc",
            .type = QEMU_OPT_BOOL,
            .help = "update env.pc after cpu tb exec",
        }, {
            .name = "linkopt",
            .type = QEMU_OPT_BOOL,
            .help = "tb link stub override mode",
        }, {
            .name = "intopt",
            .type = QEMU_OPT_NUMBER,
            .help = "interrupt optimization",
        }, {
            .name = "inhibitirq",
            .type = QEMU_OPT_NUMBER,
            .help = "emulation for x86 cpu inhibit irq",
        }, {
            .name = "verbose",
            .type = QEMU_OPT_BOOL,
            .help = "pint the options parsing result",
        }, {
            .name = "opt_syscall",
            .type = QEMU_OPT_BOOL,
            .help = "syscall optimization",
        }, {
            .name = "opt_sysret",
            .type = QEMU_OPT_BOOL,
            .help = "sysret optimization",
        }, {
            .name = "opt_iret",
            .type = QEMU_OPT_BOOL,
            .help = "iret optimization",
        }, {
            .name = "pause",
            .type = QEMU_OPT_BOOL,
            .help = "remove pause (repz nop)",
        }, {
            .name = "rdtsc",
            .type = QEMU_OPT_BOOL,
            .help = "rdtsc optimization",
        }, {
            .name = "aes",
            .type = QEMU_OPT_NUMBER,
            .help = "aes optimization",
        }, {
            .name = "aotsys",
            .type = QEMU_OPT_NUMBER,
            .help = "aot optimization",
        }, {
            .name = "aotgprhb",
            .type = QEMU_OPT_BOOL,
            .help = "AOT GPR high-bits optimization",
        }, {
            .name = "tugprhb",
            .type = QEMU_OPT_BOOL,
            .help = "dynamic user TU GPR high-bits optimization",
        }, {
            .name = "lasx",
            .type = QEMU_OPT_BOOL,
            .help = "lasx enable",
        }, {
            .name = "lockandlookup",
            .type = QEMU_OPT_NUMBER,
            .help = "tb lookup after tb lock to avoid redundent translation",
        }, {
            .name="imm_cache",
            .type = QEMU_OPT_STRING,
            .help = "config imm cache options",
        }, {
            .name="imm_skip_pc",
            .type = QEMU_OPT_NUMBER,
            .help = "config imm skip pc",
        }, {
            .name="imm_pc_mask",
            .type = QEMU_OPT_NUMBER,
            .help = "config imm pc_mask",
        }, {
            .name="imm_pc_prefix",
            .type = QEMU_OPT_NUMBER,
            .help = "config imm pc_prefix",
        }, {
            .name="imm_mode",
            .type = QEMU_OPT_NUMBER,
            .help = "config imm run mode",
        }, {
            .name = "tbrecord",
            .type = QEMU_OPT_STRING,
            .help = "enable TB recording with output file path (e.g. tbrecord=/tmp/tb_records.bin)",
        },
        { /* end of list */ }
    },
};

static void latxs_options_check_fpe(void)
{
    if(option_fast_masked_v && (!option_hamt)) {
        option_fast_masked_v = 0;
        fprintf(stderr, "fast masked v should be used with hamt! disasble fast masked v!\n");
    }
}

static void latxs_options_check_sysflagtu(void)
{
    if (tu_runtime_enabled() && (!option_hamt || !hamt_do_cpl(3)) &&
        flag_reduction_tu_need_recover())
    {
        option_sys_flag_reduction_tu &= ~FLAGRDTN_TU_NEED_RECOVER_MASK;
        fprintf(stderr, "tu flag reduction xx_jcc disabled. "
                "option tu %d hamt %d hamtconfig %x\n",
                tu_runtime_enabled(), option_hamt, option_hamt_config);
    }
}

/*
 * option smmupcaddi moves the per-access softmmu anchor from the hot path
 * into a stub of the shared slow path emitted by
 * tr_gen_softmmu_slow_path_common().  Two configurations cannot use it:
 *
 *  - comsmmu=off: there is no shared body/stub at all (each access gets its
 *    own helper call), so the option is meaningless.  Force it off instead of
 *    letting the dump report a setting that code generation ignores.
 *
 *  - TU/AOT-TU: in TU mode the old per-access pcaddi was already emitted only
 *    on the miss direction of the branch, so this option saves no executed
 *    instruction there (code size only), while the cold stub would have to
 *    reach back across a whole TU with a PCADDI si20 displacement (fail-closed
 *    abort above ~2 MiB of TU host code).  Keep it to ordinary TB mode, which
 *    is where the per-access instruction saving exists.
 */
static void latxs_options_check_smmupcaddi(void)
{
    if (!option_softmmu_pcaddi_cold) {
        return;
    }
    if (!option_common_slowpath) {
        warn_report("smmupcaddi requires comsmmu=on; disabling smmupcaddi");
        option_softmmu_pcaddi_cold = 0;
        return;
    }
    if (tu_runtime_enabled()) {
        warn_report("smmupcaddi has no TB-mode runtime benefit under TU/AOT "
                    "TU; disabling smmupcaddi");
        option_softmmu_pcaddi_cold = 0;
    }
}

static void latxs_options_check_tu_hras(void)
{
    if (option_tu_hras < 0 || option_tu_hras > 2) {
        error_report("tuhras must be 0, 1 or 2");
        exit(1);
    }

    /* A software-RAS hit enters the private return without another page
     * check. Every entry to its owning TU must validate all member pages.
     * Fall back as a whole, including RET, to preserve software-RAS pairing. */
    if (option_tu_hras &&
        !(jit_tu_requested() && cp_tu() && cp_tu_pages() &&
          tucp_fast_lookup_check() &&
          tucp_fast_direct_check() && tucp_fast_fsht_check())) {
        warn_report("tuhras requires JIT TU page tracking and lookup, direct "
                    "and FSHT page checks; using software RAS");
        option_tu_hras = 0;
    }
}

static void latxs_arch_options_init(void)
{
#ifdef CONFIG_SOFTMMU
    option_arch_opt = 1;
    option_table_addr = 1;
    option_njc_layout = 1;
    option_njc_source_jump = 2;
    option_irq_outline = 3;
    option_irq_backedge = 1;
    option_tb_htable_bits = 18;
#endif
}

static void latxs_arch_options_check(void)
{
    bool no_aot = !option_sys_aot && !option_aot_save && !option_aot_load;
    bool hras_jit = no_aot && jit_tu_requested() && option_hamt &&
                    option_tu_hras == 2;
    bool plain_tb = no_aot && !jit_tu_requested() && option_hamt &&
                    option_table_addr;

    /* The master switch also restores the pre-existing QHT capacity.
     * It takes precedence over individual experiment overrides. */
    if (!option_arch_opt) {
        option_table_addr = 0;
        option_njc_layout = 0;
        option_njc_source_jump = 0;
        option_irq_outline = 0;
        option_irq_backedge = 0;
        option_tb_htable_bits = 0;
        return;
    }
    if (!option_table_addr || !no_aot || !latx_jmp_cache_compact_enabled()) {
        option_njc_layout = 0;
    }
    /* Respect explicit inline lookup and shared-stub counter modes. */
    if (!(hras_jit || plain_tb || (option_hamt && aot_tu_enabled())) ||
        !latx_jmp_cache_compact_enabled() || !option_intb_njc ||
        option_intb_njc_inline || !option_flaged_sht || !option_intb_link ||
        !option_rastoptp || option_intb_counter ||
        option_int_opt == IRQ_CHECK_ASYNC_NOTIFY) {
        option_njc_source_jump = 0;
    }
    if (!hras_jit || option_int_opt != IRQ_CHECK_BLOCK_END) {
        option_irq_outline = 0;
    }
    if ((!hras_jit && !(option_hamt && aot_tu_enabled())) ||
        option_int_opt != IRQ_CHECK_BLOCK_END) {
        option_irq_backedge = 0;
    }
}

static void latx_sys_check_options(void)
{
    if (hamt_allow_long_no_pse()) {
#if !defined(CONFIG_SOFTMMU) || !defined(CONFIG_LATX_HACK_MEM)
        warn_report("HAMT long-no-PSE requires system mode with hack mem; "
                    "disabling experimental mode");
        option_hamt_config &= ~HAMT_CONFIG_LONG_NO_PSE;
#else
        if (!option_hamt || tu_runtime_enabled() || option_sys_aot ||
            option_aot_save || option_aot_load || option_aot_fork) {
            warn_report("HAMT long-no-PSE requires HAMT and ordinary TB "
                        "mode without AOT; disabling experimental mode");
            option_hamt_config &= ~HAMT_CONFIG_LONG_NO_PSE;
        } else {
            /* In-refill and inline instruction walkers have independent
             * protection and lifecycle constraints; use the C walker. */
            if (option_hamt_config & 0xc0) {
                warn_report("HAMT long-no-PSE disables in-refill PTW");
                option_hamt_config &= ~0xc0;
            }
            if (option_fast_itlb_ptw) {
                warn_report("HAMT long-no-PSE disables fastitlbptw");
                option_fast_itlb_ptw = 0;
            }
        }
#endif
    }
    if (option_pcid &&
        (!hamt_enabled() || !hamt_mode_dmw() || hamt_disable_asid() ||
         !hamt_do_taskset())) {
        fprintf(stderr, "WARNING: pcid requires pinned HAMT vCPUs and DMW ASIDs. "
                "disabling pcid, pcidasid and pcidcache.\n");
        option_pcid = 0;
        option_pcid_asid = 0;
        option_pcid_cache = 0;
    }
    if (option_pcid_asid &&
        (!option_pcid || !latx_jmp_cache_compact_enabled())) {
        fprintf(stderr, "WARNING: pcidasid requires pcid and compact NJC. "
                "disabling pcidasid and pcidcache.\n");
        option_pcid_asid = 0;
        option_pcid_cache = 0;
    }
    if (option_pcid_cache &&
        (!option_pcid_asid || !latx_jmp_cache_compact_enabled())) {
        fprintf(stderr, "WARNING: pcidcache requires pcidasid and compact NJC. "
                "disabling pcidcache.\n");
        option_pcid_cache = 0;
    }
    if (option_rastoptp && !option_hamt) option_rastoptp = 0;
    if (option_rastoptp) {
        if (option_tb_pcrel || option_page_pcrel) {
            error_report("rastoptp requires non-PC-relative code");
            exit(EXIT_FAILURE);
        }
        option_soft_ras = 1;
    }
    latxs_options_check_fpe();
    latxs_options_check_sysflagtu();
    latxs_options_check_smmupcaddi();

    if (option_aot_gpr_highbits && !aot_tu_enabled()) {
        fprintf(stderr, "WARNING: aotgprhb requires AOT TU "
                "(aotsys enabled and tumode=aot/both); "
                "disabling aotgprhb.\n");
        option_aot_gpr_highbits = 0;
    }
    if (option_tu_gpr_highbits && !jit_tu_requested()) {
        fprintf(stderr, "WARNING: tugprhb requires JIT TU "
                "(tumode=jit/both); disabling tugprhb.\n");
        option_tu_gpr_highbits = 0;
    }
    if (option_shbr && !tu_runtime_enabled()) {
        fprintf(stderr, "WARNING: shbr requires JIT TU or AOT TU; "
                "shbr disabled.\n");
        option_shbr = 0;
    }
    if (option_ghbr && !tu_runtime_enabled()) {
        warn_report("ghbr requires JIT TU or AOT TU; ghbr disabled");
        option_ghbr = 0;
    }
    if (option_ghbr == LATXS_GHBR_IGNORE_MEMORY) {
        warn_report("ghbr=2 ignores memory liveness barriers; fault, "
                    "MMIO/helper, and watchpoint exits are unsafe");
    }
    if (option_shbr == LATXS_SHBR_IGNORE_MEMORY) {
        fprintf(stderr, "WARNING: shbr=2 ignores memory liveness barriers; "
                "fault, MMIO/helper, and watchpoint exits are unsafe.\n");
    } else if (option_shbr == LATXS_SHBR_AGGRESSIVE) {
        fprintf(stderr, "WARNING: shbr=3 ignores memory and in-TU system-exit "
                "liveness barriers; this mode is unsafe.\n");
    }

    if (option_aot_cross_page_tu && !aot_tu_enabled()) {
        fprintf(stderr, "WARNING: aottucp requires aotsys=on and aottu=on.\n");
        fprintf(stderr, "WARNING: aottucp disabled.\n");
        option_aot_cross_page_tu = 0;
    }

    if (option_tucp_fast & ~TUCPFAST_ALL) {
        fprintf(stderr, "WARNING: tucpfast only supports bits 0x%x.\n", TUCPFAST_ALL);
        fprintf(stderr, "WARNING: tucpfast disabled.\n");
        option_tucp_fast = 0;
    }
    if (tucp_fast()) {
        bool valid_cp = option_cross_page == 0x55 ||
                        option_cross_page == 0x155 ||
                        option_cross_page == 0x355;
        bool jit_tucp = jit_tu_enabled() && cp_tu() && cp_tu_pages();
        bool aot_tucp = aot_cross_page_tu_enabled();

        if (!valid_cp) {
            fprintf(stderr, "WARNING: tucpfast requires cp=0x55, 0x155, or 0x355.\n");
            fprintf(stderr, "WARNING: tucpfast disabled.\n");
            option_tucp_fast = 0;
        }
        if (!option_split_tb || (!jit_tucp && !aot_tucp)) {
            fprintf(stderr, "WARNING: tucpfast requires sptb=on and either "
                    "JIT TU page tracking or aottucp=on.\n");
            fprintf(stderr, "WARNING: tucpfast disabled.\n");
            option_tucp_fast = 0;
        }
        if (!option_fast_itlb || !option_fast_itlb_ptw) {
            fprintf(stderr, "WARNING: tucpfast requires fastitlb=on and "
                    "fastitlbptw=on.\n");
            fprintf(stderr, "WARNING: tucpfast disabled.\n");
            option_tucp_fast = 0;
        }
    }
    latxs_options_check_tu_hras();
    if (tucp_generation_enabled() && !tucp_fast_needs_pages_desc()) {
        fprintf(stderr, "WARNING: tucpgen requires TU page checks from tucpfast.\n");
        fprintf(stderr, "WARNING: tucpgen disabled.\n");
        option_tucp_generation = 0;
    }
    option_tucp_generation_array_size &=
        ~(tucp_generation_entry_size() - 1);
    if (tucp_generation_enabled() &&
        option_tucp_generation_array_size <
        2 * tucp_generation_entry_size()) {
        fprintf(stderr, "WARNING: tucpgensize must hold ID 0 and at least one TU.\n");
        fprintf(stderr, "WARNING: tucpgen disabled.\n");
        option_tucp_generation = 0;
    }
    if (tucp_generation_offset() &&
        option_tucp_generation_array_size > (uint64_t)INT32_MAX + 1) {
        fprintf(stderr, "WARNING: tucpgen=offset requires tucpgensize <= 2G.\n");
        fprintf(stderr, "WARNING: tucpgen disabled.\n");
        option_tucp_generation = 0;
    }

    if (option_jmp_cache_hash == LATX_JMP_CACHE_HASH_PAGE &&
        option_jmp_cache_bits_set) {
        error_report("njcbits is only supported with njchash=full or line; "
                     "page mode uses a fixed 12-bit cache");
        _exit(1);
    }

    if(option_fast_atomic && (!option_hamt)) {
        option_fast_atomic = 0;
        fprintf(stderr, "fast atomic should be used with hamt! disasble fast atomic!\n");
    }

    if(option_htimer && (!option_hamt)) {
        option_htimer = 0;
        fprintf(stderr, "htimer should be used with hamt! disasble htimer!\n");
    }

#ifdef CONFIG_SOFTMMU
    hamt_init_options_configuration();
#endif

    if (option_fast_itlb_ptw && !option_fast_itlb) {
        fprintf(stderr, "ERROR: fastitlbptw needs fastitlb enabled.\n");
        exit(1);
    }

#if !defined(CONFIG_LATX_HACK_MEM)
    if (hamt_use_ptw() || hamt_use_ptw_inrefill()) {
        fprintf(stderr, "HAMT PTW needs hack mem enabled.\n");
        fprintf(stderr, "Reconfigure with --enable-latx-hack-mem " \
                "or set hamtconfig to not use ptw.\n");
        exit(1);
    }

    if (option_fast_itlb_ptw) {
        fprintf(stderr, "ERROR: fastitlbptw needs hack mem enabled.\n");
        fprintf(stderr, "Reconfigure with --enable-latx-hack-mem " \
                        "or set fastitlbptw to off.\n");
        exit(1);
    }
#endif
    latxs_arch_options_check();
}

#define LATXS_RISKY_MEM_OFFSET  0x1
#define LATXS_RISKY_SIMD_IGNORE_UPPER64 0x2
#define LATXS_RISKY_SIMD_IGNORE_UPPER64_STAT_TRACK 0x4
#define LATXS_RISKY_LAZY_LOAD_STORE_MXCSR 0x8
#define LATXS_RISKY_IN_379  0x10
#define LATXS_RISKY_FAST_ATOMIC 0x20
static void parse_options_risky(unsigned long long t)
{
    option_risk_mem_offset =
        (t & LATXS_RISKY_MEM_OFFSET) ? 1 : 0;
    option_risk_simd_ignore_upper64 =
        (t & LATXS_RISKY_SIMD_IGNORE_UPPER64) ? 1 : 0;
    option_risk_simd_ignore_upper64_stat_track =
        (t & LATXS_RISKY_SIMD_IGNORE_UPPER64_STAT_TRACK) ? 1 : 0;
    option_lazy_mxcsr =
        (t & LATXS_RISKY_LAZY_LOAD_STORE_MXCSR) ? 1 : 0;
#if !defined(CONFIG_LATX_RR)
    option_risk_in_379 =
        (t & LATXS_RISKY_IN_379) ? 1 : 0;
#endif
    option_fast_atomic =
        (t & LATXS_RISKY_FAST_ATOMIC) ? 1 : 0;
}

static void parse_options_tbsizei(unsigned long long i)
{
    if (i && i < 255) {
        option_tb_max_insns = i;
    }
}

static void options_parse_tracestart_tb(unsigned long long tbpc)
{
    option_trace_start_tb = tbpc;
    option_trace_start_tb_set = 1;
}

static void set_ir1_cache_mode(int mode)
{
    option_ir1_cache_mode = mode;
    switch (mode) {
    case LATX_IR1_CACHE_MODE_OFF:
        option_ir1_cache_enabled = 0;
        break;
    case LATX_IR1_CACHE_MODE_ON:
        option_ir1_cache_enabled = 1;
        option_ir1_cache_entries = LATX_IR1_CACHE_ENTRIES_DEFAULT;
        option_ir1_fast_cache_entries =
            LATX_IR1_FAST_CACHE_ENTRIES_DEFAULT;
        break;
    case LATX_IR1_CACHE_MODE_MAX:
        option_ir1_cache_enabled = 1;
        option_ir1_cache_entries = UINT64_C(65536);
        option_ir1_fast_cache_entries = 32768;
        break;
    default:
        g_assert_not_reached();
    }
}

static void parse_options_ir1_cache(QemuOpts *opts)
{
    QemuOpt *opt;
    uint64_t entries;
    int ways;

    parse_options_bool("ir1cache", option_ir1_cache_enabled);
    parse_options_bool("hotdecode", option_hot_decode_enabled);
    opt = qemu_opt_find(opts, "ir1cacheentries");
    if (opt) {
        option_ir1_cache_entries = opt->value.uint;
    }
    opt = qemu_opt_find(opts, "ir1cacheways");
    if (opt) {
        option_ir1_cache_ways = opt->value.uint;
    }
    opt = qemu_opt_find(opts, "ir1cachereplace");
    if (opt) {
        if (!strcmp(opt->str, "lru")) {
            option_ir1_cache_replace = LATX_IR1_CACHE_REPLACE_LRU;
        } else if (!strcmp(opt->str, "fifo")) {
            option_ir1_cache_replace = LATX_IR1_CACHE_REPLACE_FIFO;
        } else if (!strcmp(opt->str, "random")) {
            option_ir1_cache_replace = LATX_IR1_CACHE_REPLACE_RANDOM;
        } else {
            error_report("invalid ir1cachereplace '%s': expected lru, fifo "
                         "or random", opt->str);
            exit(EXIT_FAILURE);
        }
    }
    parse_options_bool("ir1timing", option_ir1_timing);
    parse_options_bool("ir1dist", option_ir1_distribution);
    parse_options_bool("ir1profile", option_ir1_profile);
    opt = qemu_opt_find(opts, "ir1fastcacheentries");
    if (opt) {
        option_ir1_fast_cache_entries = opt->value.uint;
    }
    opt = qemu_opt_find(opts, "ir1hash");
    if (opt) {
        if (!strcmp(opt->str, "legacy")) {
            option_ir1_hash = LATX_IR1_HASH_LEGACY;
        } else if (!strcmp(opt->str, "murmur")) {
            option_ir1_hash = LATX_IR1_HASH_MURMUR;
        } else {
            error_report("invalid ir1hash '%s': expected legacy or murmur",
                         opt->str);
            exit(EXIT_FAILURE);
        }
    }
    opt = qemu_opt_find(opts, "ir1cachemode");
    if (opt) {
        if (!strcmp(opt->str, "off")) {
            set_ir1_cache_mode(LATX_IR1_CACHE_MODE_OFF);
        } else if (!strcmp(opt->str, "on")) {
            set_ir1_cache_mode(LATX_IR1_CACHE_MODE_ON);
        } else if (!strcmp(opt->str, "max")) {
            set_ir1_cache_mode(LATX_IR1_CACHE_MODE_MAX);
        } else {
            error_report("invalid ir1cachemode '%s': expected off, on or max",
                         opt->str);
            exit(EXIT_FAILURE);
        }
    }

    ways = latxs_ir1_cache_ways();
    entries = latxs_ir1_cache_entries();
    if (ways != 1 && ways != 2 && ways != 4) {
        error_report("ir1cacheways must be 1, 2 or 4");
        exit(EXIT_FAILURE);
    }
    if (entries < 64 || entries > (UINT64_C(1) << 22) ||
        (entries & (entries - 1)) || entries % ways) {
        error_report("ir1cacheentries must be a power of two from 64 through "
                     "4194304 and divisible by ir1cacheways");
        exit(EXIT_FAILURE);
    }
    if (latxs_ir1_fast_cache_entries() < 64 ||
        latxs_ir1_fast_cache_entries() > LATX_IR1_FAST_CACHE_ENTRIES_MAX ||
        (latxs_ir1_fast_cache_entries() &
         (latxs_ir1_fast_cache_entries() - 1))) {
        error_report("ir1fastcacheentries must be a power of two from 64 "
                     "through 32768");
        exit(EXIT_FAILURE);
    }
}

void latx_parse_options(QemuOpts *opts);
void latx_parse_options(QemuOpts *opts)
{
    QemuOpt     *opt;
    const char  *str;

    parse_options_bool("verbose", option_verbose);

    if (option_verbose) {
        qemu_opts_print(opts,"\n");
        printf("\n");
    }

    /* optm=none|select|all */
    opt = qemu_opt_find(opts, "optm");
    str = opt->str;
    if (strncmp(str, "all", 3) == 0) {
    } else if (strncmp(str, "none", 4) == 0) {
    } else if (strncmp(str, "select", 6) == 0) {
    } else {
        lsassertm(0, "optm must be one of {none, select, all}\n");
    }

    parse_options_ir1_cache(opts);

    opt = qemu_opt_find(opts, "dump");
    if (opt) options_parse_dump(opt->str);

    parse_options_uint("traces", option_trace_simple);
    parse_options_uint("tracestnr", option_trace_start_nr);
    opt = qemu_opt_find(opts, "tracesttb");
    if (opt) options_parse_tracestart_tb(opt->value.uint);

    parse_options_bool("smmu", option_smmu_slow);
    parse_options_bool("spstrins", option_split_strins);
    parse_options_bool("tbflushonreboot", option_tb_flush_on_reboot);

    opt = qemu_opt_find(opts, "tbsizei");
    if (opt) parse_options_tbsizei(opt->value.uint);

    parse_options_uint("tbconfig", option_tb_config);

    parse_options_uint("cp", option_cross_page);
    /* Apply the preset before individual overrides, independent of option order. */
    parse_options_tucpopt(opts);
    parse_options_uint("tucp", option_cross_page_tu);
    parse_options_tucpfast(opts);
    parse_options_bool("externaltustub", option_external_tu_stub);
    parse_options_tucpgen(opts);
    parse_options_uint("tucpgensize", option_tucp_generation_array_size);
    parse_options_bool("cpcounter", option_cross_page_counter);

    parse_options_uint("traceir1", option_traceir1);
    parse_options_uint("traceir1data", option_traceir1_data);

    parse_options_uint("intopt", option_int_opt);
    parse_options_uint("inhibitirq", option_inhibit_irq);

    parse_options_bool("tblink", option_tb_link);
    parse_options_bool("lsfpu", option_lsfpu);
    parse_options_bool("ffldst", option_fast_fpr_ldst);
    parse_options_bool("sigfpe", option_sigfpe);
    parse_options_bool("fast_masked_v", option_fast_masked_v);
    parse_options_bool("no_reserve_256", option_no_reserve_256);
    parse_options_bool("lazy_mxcsr", option_lazy_mxcsr);
    parse_options_bool("fsht", option_flaged_sht);
    parse_options_bool("fastitlb", option_fast_itlb);
    parse_options_bool("fastitlbptw", option_fast_itlb_ptw);
    parse_options_bool("pcid", option_pcid);
    parse_options_bool("pcidasid", option_pcid_asid);
    parse_options_bool("pcidcache", option_pcid_cache);
    parse_options_bool("pcidstats", option_pcid_stats);
    parse_options_bool("tableaddr", option_table_addr);
    parse_options_bool("newassembal", option_new_assemble);
    parse_options_bool("staticcs", option_staticcs);
    parse_options_bool("largecc", option_large_code_cache);
    parse_options_bool("softfpu", option_soft_fpu);
    parse_options_uint("softfpu_fast", option_soft_fpu_fast);
    parse_options_bool("comsmmu", option_common_slowpath);
    parse_options_bool("smmupcaddi", option_softmmu_pcaddi_cold);
    parse_options_bool("smmurmw", option_softmmu_rmw_reuse);
    parse_options_bool("njc", option_intb_njc);
    parse_options_bool("njcinline", option_intb_njc_inline);
    parse_options_njcver(opts);
    parse_options_njchash(opts);
    parse_options_njcbits(opts);
    parse_options_bool("lazypc", option_lazypc);
    parse_options_bool("linkopt", option_link_opt);
    parse_options_bool("aes", option_aes_opt);
    parse_options_bool("aotsys", option_sys_aot);
    parse_options_bool("aotsys", option_aot_save);
    parse_options_bool("aotfork", option_aot_fork);
    parse_options_bool("waitaot", option_wait_aot);
    parse_options_bool("llvmaot", option_llvm_aot);
    parse_options_bool("aotsys", option_aot_load);
    parse_options_bool("aotgprhb", option_aot_gpr_highbits);
    parse_options_bool("tugprhb", option_tu_gpr_highbits);
    parse_options_bool("lasx", option_enable_lasx);
    parse_options_bool("fastexcep", option_fast_excep);
    parse_options_uint("lockandlookup", option_lock_and_lookup_level);

    parse_options_uint("hamt", option_hamt);
    parse_options_uint("hamtmode", option_hamt_mode);
    parse_options_uint("hamtsubmode", option_hamt_submode);
    parse_options_uint("hamtconfig", option_hamt_config);
    parse_options_uint("hamtdump", option_hamt_dump);
    parse_options_uint("hamtprint", option_hamt_print);

    parse_options_bool("htimer", option_htimer);
    parse_options_bool("irqgatenobql", option_irq_gate_nobql);
    
    opt = qemu_opt_find(opts, "imm_cache");
    if(opt) options_parse_imm_cache(opt->str);
    parse_options_uint("imm_mode", option_imm_mode);
    parse_options_uint("imm_skip_pc", imm_skip_pc);
    parse_options_uint("imm_pc_mask", imm_pc_mask);
    parse_options_uint("imm_pc_prefix", imm_pc_prefix);

    parse_options_uint("optmmio", option_opt_mmio);

    parse_options_bool("sptb", option_split_tb);
    parse_options_bool("sleb", option_split_search);
    parse_options_uint("sysflag", option_sys_flag_reduction);
    parse_options_uint("sysflagtu", option_sys_flag_reduction_tu);
    parse_options_uint("instptn", option_instptn);
    parse_options_bool("pushpop", option_pushpop);
    parse_options_bool("pushpopret", option_pushpop_ret);

    parse_options_bool("ras", option_soft_ras);
    parse_options_bool("rastoptp", option_rastoptp);
    parse_options_uint("tuhras", option_tu_hras);
    parse_options_bool("tuhrascopy", option_tu_hras_opt_copy);
    parse_options_tu_mode_bit(opts, "tu", LATX_TU_MODE_JIT);
    parse_options_tu_mode_bit(opts, "aottu", LATX_TU_MODE_AOT);
    parse_options_bool("aottupretb", option_aot_tu_pretranslate);
    parse_options_bool("aottupredisasm", option_aot_tu_pre_disasm);
    if (qemu_opt_find(opts, "tu") || qemu_opt_find(opts, "aottu")) {
        fprintf(stderr, "WARNING: tu/aottu are deprecated; use tumode. "
                "If specified, tumode takes precedence over both.\n");
    }
    opt = qemu_opt_find(opts, "tumode");
    if (opt) {
        if (!strcmp(opt->str, "off")) {
            option_tu_mode = LATX_TU_MODE_OFF;
        } else if (!strcmp(opt->str, "jit")) {
            option_tu_mode = LATX_TU_MODE_JIT;
        } else if (!strcmp(opt->str, "aot")) {
            option_tu_mode = LATX_TU_MODE_AOT;
        } else if (!strcmp(opt->str, "both")) {
            option_tu_mode = LATX_TU_MODE_BOTH;
        } else {
            error_report("invalid tumode '%s': expected off, jit, aot or both",
                         opt->str);
            exit(1);
        }
    }
    parse_options_uint("tumaxtb", option_tu_max_tb);
    opt = qemu_opt_find(opts, "aottumaxtb");
    if (opt) {
        if (opt->value.uint < 1 || opt->value.uint > 1024) {
            error_report("aottumaxtb must be between 1 and 1024");
            exit(EXIT_FAILURE);
        }
        option_aot_tu_max_tb = opt->value.uint;
    }
    parse_options_bool("aottucp", option_aot_cross_page_tu);
    parse_options_shbr(opts);
    parse_options_ghbr(opts);
    parse_options_bool("regionprologue", option_region_prologue);
    parse_options_bool("intblink", option_intb_link);
    parse_options_bool("intbcounter", option_intb_counter);
    parse_options_bool("tbcounter", option_tb_counter);
    parse_options_bool("trbh64", option_by_hand_64);

    parse_options_bool("opt_syscall", option_opt_syscall);
    parse_options_bool("opt_sysret", option_opt_sysret);
    parse_options_bool("opt_iret", option_opt_iret);

    parse_options_bool("pause", option_pause);

    parse_options_bool("rdtsc", option_opt_rdtsc_rdtscp);
    opt = qemu_opt_find(opts, "risky");
    if (opt) parse_options_risky(opt->value.uint);

    opt = qemu_opt_find(opts, "tbrecord");
    if (opt && opt->str) {
        option_tb_record = 1;
        option_tb_record_path = opt->str;
    }

#if !defined(CONFIG_SOFTMMU)
    latx_options_dump_prepare();
    dump_latxs_options();
#endif

    return;
}

void latx_parse_env_options(void)
{
#ifdef CONFIG_SOFTMMU
    const char *env = getenv("LATS_OPT");
    const char *arch_opt = getenv("LATS_ARCH_OPT");
    const char *irq_layout = getenv("LATS_IRQ_LAYOUT");
    const char *njc_return = getenv("LATS_NJC_RETURN");
    const char *tb_htable_bits = getenv("LATS_TB_HTABLE_BITS");
    QemuOpts *opts;

    if (arch_opt) {
        if (strcmp(arch_opt, "0") && strcmp(arch_opt, "1")) {
            error_report("LATS_ARCH_OPT must be 0 or 1");
            exit(EXIT_FAILURE);
        }
        option_arch_opt = arch_opt[0] - '0';
    }

    if (irq_layout) {
        if (strcmp(irq_layout, "0") && strcmp(irq_layout, "1")) {
            error_report("LATS_IRQ_LAYOUT must be 0 or 1");
            exit(EXIT_FAILURE);
        }
        option_irq_outline = irq_layout[0] == '1' ? 3 : 0;
        option_irq_backedge = irq_layout[0] == '1';
    }

    if (tb_htable_bits) {
        if (strcmp(tb_htable_bits, "16") && strcmp(tb_htable_bits, "18") &&
            strcmp(tb_htable_bits, "20") && strcmp(tb_htable_bits, "22")) {
            error_report("LATS_TB_HTABLE_BITS must be 16, 18, 20 or 22");
            exit(EXIT_FAILURE);
        }
        option_tb_htable_bits = strtoul(tb_htable_bits, NULL, 10);
    }

    if (njc_return) {
        if (strcmp(njc_return, "0") && strcmp(njc_return, "2")) {
            error_report("LATS_NJC_RETURN must be 0 or 2");
            exit(EXIT_FAILURE);
        }
        option_njc_source_jump = njc_return[0] - '0';
    }

    if (!env || !env[0]) {
        return;
    }

    opts = qemu_opts_parse_noisily(qemu_find_opts("latx"), env, true);
    if (!opts) {
        exit(1);
    }

    latx_parse_options(opts);
#endif
}

static bool parse_verbose_bool(const char *value)
{
    return !strcmp(value, "on") || !strcmp(value, "yes") ||
           !strcmp(value, "true") || !strcmp(value, "1");
}

static void parse_verbose_string(const char *options)
{
    const char *p = options;

    while (p && *p) {
        const char *end = strchr(p, ',');
        size_t len = end ? (size_t)(end - p) : strlen(p);

        if (len == strlen("verbose") && !strncmp(p, "verbose", len)) {
            option_verbose = true;
        } else if (len > strlen("verbose=") &&
                   !strncmp(p, "verbose=", strlen("verbose="))) {
            option_verbose = parse_verbose_bool(p + strlen("verbose="));
        }
        p = end ? end + 1 : NULL;
    }
}

void latx_parse_verbose_early(int argc, char **argv)
{
    for (int i = 1; i < argc - 1; i++) {
        if (!strcmp(argv[i], "-latx")) {
            parse_verbose_string(argv[i + 1]);
        }
    }

#if defined(CONFIG_SOFTMMU)
    if (getenv("LATS_OPT")) {
        parse_verbose_string(getenv("LATS_OPT"));
    }
#endif
}

void dump_latxs_options(void)
{
    if (!option_verbose) return;
    latx_options_printf("[LATX-SYS] -----------------------------------------------------\n");
    latx_options_printf("[LATX-SYS] sizeof(TranslationBlock) = %ld\n", sizeof(TranslationBlock));
    latx_options_printf("[LATX-SYS] -----------------------------------------------------\n");
    latx_options_printf("[LATX-SYS] dump      = %d\n", option_dump);
    latx_options_printf("[LATX-SYS] dump host = %d\n", option_dump_host);
    latx_options_printf("[LATX-SYS] dump ir1  = %d\n", option_dump_ir1);
    latx_options_printf("[LATX-SYS] dump ir2  = %d\n", option_dump_ir2);
    latx_options_printf("[LATX-SYS] -----------------------------------------------------\n");
    latx_options_printf("[LATX-SYS] trace simple    = %d\n", option_trace_simple);
    latx_options_printf("[LATX-SYS] trace start nr  = %lld\n", option_trace_start_nr);
    latx_options_printf("[LATX-SYS] trace start tb  = 0x%llx\n", option_trace_start_tb);
    latx_options_printf("[LATX-SYS] -----------------------------------------------------\n");
    latx_options_printf("[LATX-SYS]       tb link   = %d\n", option_tb_link);
    latx_options_printf("[LATX-SYS] indir tb link   = %d\n", option_intb_link);
    latx_options_printf("[LATX-SYS] indir tb njc    = %d\n", option_intb_njc);
    latx_options_printf("[LATX-SYS] inline tb njc   = %d\n",
            option_intb_njc_inline);
    latx_options_printf("[LATX-SYS]       tb ras    = %d\n", option_soft_ras);
    latx_options_printf("[LATX-SYS]      ras top tp = %d\n", option_rastoptp);
    latx_options_printf("[LATX-SYS]       tu hras   = %d\n", option_tu_hras);
    latx_options_printf("[LATX-SYS]  tu hras copy  = %d\n",
                        option_tu_hras_opt_copy);
    latx_options_printf("[LATX-SYS]       jit tu    = %d\n",
                        jit_tu_requested() ? 1 : 0);
    latx_options_printf("[LATX-SYS]       aot tu    = %d\n",
                        aot_tu_requested() ? 1 : 0);
    latx_options_printf("[LATX-SYS] aot tu pre-tb   = %d\n",
            option_aot_tu_pretranslate);
    latx_options_printf("[LATX-SYS] aot tu disasm   = %d\n",
            option_aot_tu_pre_disasm);
    latx_options_printf("[LATX-SYS]       tu max tb = %d\n", option_tu_max_tb);
    latx_options_printf("[LATX-SYS]   aot tu max tb = %d\n",
                       option_aot_tu_max_tb);
    latx_options_printf("[LATX-SYS]       shbr      = %d\n", option_shbr);
    latx_options_printf("[LATX-SYS]       ghbr      = %d\n", option_ghbr);
    latx_options_printf("[LATX-SYS] region prologue = %d\n", option_region_prologue);
    latx_options_printf("[LATX-SYS] lazy pc update  = %d\n", option_lazypc);
    latx_options_printf("[LATX-SYS] link override   = %d\n", option_link_opt);
    latx_options_printf("[LATX-SYS] No pause & nop  = %d\n", option_pause);
    latx_options_printf("[LATX-SYS] lock and lookup = %d\n", option_lock_and_lookup_level);
    latx_options_printf("[LATX-SYS] -----------------------------------------------------\n");
    latx_options_printf("[LATX-SYS] tb configuration = %d\n", option_tb_config);
    latx_options_printf("[LATX-SYS]        exit stub = %d\n", (option_tb_config & 0x1)?1:0);
    latx_options_printf("[LATX-SYS]        user mode = %d\n", (option_tb_config & 0x2)?1:0);
    latx_options_printf("[LATX-SYS] -----------------------------------------------------\n");
    latx_options_printf("[LATX-SYS] cross page link      = %d\n", cp_link());
    latx_options_printf("[LATX-SYS] cross page check     = %d\n", cp_check());
    latx_options_printf("[LATX-SYS] cross page tb link   = %d\n", cp_tb_link());
    latx_options_printf("[LATX-SYS] cross page tb check  = %d\n", cp_tb_check());
    latx_options_printf("[LATX-SYS] cross page inline p0 = %d\n", cp_inline_p0());
    latx_options_printf("[LATX-SYS] cross page merged p0 = %d\n",
            cp_inline_p0_merged_check());
    latx_options_printf("[LATX-SYS] cross page tu        = %x\n", cp_tu());
    latx_options_printf("[LATX-SYS] cross page tu gen tb = %d\n", cp_tu_gen_tb() ? 1 : 0);
    latx_options_printf("[LATX-SYS] cross page tu   cptb = %d\n", cp_tu_cptb() ? 1 : 0);
    latx_options_printf("[LATX-SYS] cross page tu  pages = %d\n", cp_tu_pages() ? 1 : 0);
    latx_options_printf("[LATX-SYS] cross page tu npages = %d\n", cp_tu_page_num());
    latx_options_printf("[LATX-SYS] fast TU page check   = 0x%x\n",
                        option_tucp_fast);
    latx_options_printf("[LATX-SYS] externaltustub       = %d\n",
                        option_external_tu_stub);
    latx_options_printf("[LATX-SYS] TU check generation = %d\n",
                        option_tucp_generation);
    latx_options_printf("[LATX-SYS] TU generation bytes = %" PRIu64 "\n",
                        option_tucp_generation_array_size);
    latx_options_printf("[LATX-SYS] -----------------------------------------------------\n");
#ifdef CONFIG_SOFTMMU
    latx_options_printf("[LATX-SYS] hamt enabled = 0x%x\n", option_hamt);
    latx_options_printf("[LATX-SYS] hamt mode = %d\n", option_hamt_mode);
    latx_options_printf("[LATX-SYS]   submode = %d\n", option_hamt_submode);
    latx_options_printf("[LATX-SYS] hamt cfg  = 0x%x\n", option_hamt_config);
    latx_options_printf("[LATX-SYS] kvm init htimer = 0x%x\n", option_htimer);
    latx_options_printf("[LATX-SYS] irq gate no-BQL = 0x%x\n",
                        option_irq_gate_nobql);
#endif
    latx_options_printf("[LATX-SYS] -----------------------------------------------------\n");
    latx_options_printf("[LATX-SYS] pcid = %d\n", option_pcid);
    latx_options_printf("[LATX-SYS] pcidasid = %d\n", option_pcid_asid);
    latx_options_printf("[LATX-SYS] pcidcache = %d\n", option_pcid_cache);
    latx_options_printf("[LATX-SYS] pcidstats = %d\n", option_pcid_stats);
    latx_options_printf("[LATX-SYS] -----------------------------------------------------\n");
    latx_options_printf("[LATX-SYS] lsfpu  = %d\n", option_lsfpu);
    latx_options_printf("[LATX-SYS] ffldst = %d\n", option_fast_fpr_ldst);
    latx_options_printf("[LATX-SYS] -----------------------------------------------------\n");
    latx_options_printf("[LATX-SYS] risk : embeded memory offset = %d\n", option_risk_mem_offset);
    latx_options_printf("[LATX-SYS] risk : simd upper 64 ignored = %d\n", option_risk_simd_ignore_upper64);
    latx_options_printf("[LATX-SYS] risk : simd upper 64 tracker = %d\n", option_risk_simd_ignore_upper64_stat_track);
    latx_options_printf("[LATX-SYS] risk : lazy mxcsr = %d\n", option_lazy_mxcsr);
    latx_options_printf("[LATX-SYS] risk : in 0x379 = %d\n", option_risk_in_379);
    latx_options_printf("[LATX-SYS] risk : fast atomic = %d\n", option_fast_atomic);
    latx_options_printf("[LATX-SYS] -----------------------------------------------------\n");
    latx_options_printf("[LATX-SYS] flag reduction : inside tb = %d\n", option_sys_flag_reduction);
    latx_options_printf("[LATX-SYS] flag reduction : cross tb in tu = %x\n", option_sys_flag_reduction_tu);
    latx_options_printf("[LATX-SYS] -----------------------------------------------------\n");
    latx_options_printf("[LATX-SYS] push pop  = %d\n", option_pushpop);
    latx_options_printf("[LATX-SYS] instruction pattern = %" PRIu64 "\n",
                        option_instptn);
    latx_options_printf("[LATX-SYS] -----------------------------------------------------\n");
    latx_options_printf("[LATX-SYS] 64-bit by hand = %d\n", option_by_hand_64);
    latx_options_printf("[LATX-SYS] -----------------------------------------------------\n");
    latx_options_printf("[LATX-SYS] code cache large = %d\n", option_large_code_cache);
    latx_options_printf("[LATX-SYS] IR1 decode cache mode = %s, enabled = %s"
                        ", entries = %" PRIu64 ", ways = %d, replace = %s"
                        ", hash = %s, profile = %s, timing = %s"
                        ", distribution = %s, hot decoder = %s"
                        ", fast entries = %d\n",
                        latxs_ir1_cache_mode_name(option_ir1_cache_mode),
                        option_ir1_cache_enabled ? "on" : "off",
                        latxs_ir1_cache_entries(), latxs_ir1_cache_ways(),
                        latxs_ir1_cache_replace_name(
                            option_ir1_cache_replace),
                        latxs_ir1_hash_name(option_ir1_hash),
                        option_ir1_profile ? "on" : "off",
                        option_ir1_timing ? "on" : "off",
                        option_ir1_distribution ? "on" : "off",
                        option_hot_decode_enabled ? "on" : "off",
                        latxs_ir1_fast_cache_entries());
    latx_options_printf("[LATX-SYS] split tb = %d\n", option_split_tb);
    latx_options_printf("[LATX-SYS] split search = %d\n", option_split_search);
    latx_options_printf("[LATX-SYS] -----------------------------------------------------\n");
    latx_options_printf("[LATX-SYS] static context switch = %d\n", option_staticcs);
    latx_options_printf("[LATX-SYS] common softmmu helper = %d\n", option_common_slowpath);
    latx_options_printf("[LATX-SYS] softmmu pcaddi cold path = %d\n",
                        option_softmmu_pcaddi_cold);
    latx_options_printf("[LATX-SYS] softmmu RMW reuse = %d\n",
                        option_softmmu_rmw_reuse);
    latx_options_printf("[LATX-SYS] flaged shadow hash table = %d\n", option_flaged_sht);
    latx_options_printf("[LATX-SYS] fast handle excep = %d\n", option_fast_excep);
    latx_options_printf("[LATX-SYS] -----------------------------------------------------\n");
    latx_options_printf("[LATX-SYS] fast instruction TLB = %d\n", option_fast_itlb);
    latx_options_printf("[LATX-SYS] fast instruction TLB ptw = %d\n", option_fast_itlb_ptw);
    latx_options_printf("[LATX-SYS] -----------------------------------------------------\n");
    latx_options_printf("[LATX-SYS] soft fpu = %d\n", option_soft_fpu);
    latx_options_printf("[LATX-SYS] soft fpu fast = %x\n", option_soft_fpu_fast);
    latx_options_printf("[LATX-SYS] sigfpe = %d\n", option_sigfpe);
    latx_options_printf("[LATX-SYS] fast masked v = %d\n", option_fast_masked_v);
    latx_options_printf("[LATX-SYS] no reserve 256 = %d\n", option_no_reserve_256);
    latx_options_printf("[LATX-SYS] aes opt = %d\n", option_aes_opt);
    latx_options_printf("[LATX-SYS] aot sys opt = %d\n", option_sys_aot);
    latx_options_printf("[LATX-SYS] aot save opt = %d\n", option_aot_save);
    latx_options_printf("[LATX-SYS] aot load opt = %d\n", option_aot_load);
    latx_options_printf("[LATX-SYS] aot GPR high-bits opt = %d\n",
                        option_aot_gpr_highbits);
    latx_options_printf("[LATX-SYS] dynamic TU GPR high-bits opt = %d\n",
                        option_tu_gpr_highbits);
    latx_options_printf("[LATX-SYS] aot cross-page tu = %d\n",
                        option_aot_cross_page_tu);
    latx_options_printf("[LATX-SYS] llvm aot opt = %d\n", option_llvm_aot);
    latx_options_printf("[LATX-SYS] lasx enable = %d\n", option_enable_lasx);
    latx_options_printf("[LATX-SYS] -----------------------------------------------------\n");
    latx_options_printf("[LATX-SYS] slow soft mmu  = %d\n", option_smmu_slow);
    latx_options_printf("[LATX-SYS] split string ins = %d\n", option_split_strins);
    latx_options_printf("[LATX-SYS] TB flush on reboot = %d\n",
                        option_tb_flush_on_reboot);
    latx_options_printf("[LATX-SYS] max inst of TB = %d\n", option_tb_max_insns);
    latx_options_printf("[LATX-SYS] -----------------------------------------------------\n");
    latx_options_printf("[LATX-SYS] interrupt optimize = %d\n", option_int_opt);
    latx_options_printf("[LATX-SYS]   control-flow analysis = %d\n", option_int_opt);
    latx_options_printf("[LATX-SYS] inhibit irq = %x\n", option_inhibit_irq);
    latx_options_printf("[LATX-SYS]   use hflags  = %d\n", inhibit_irq_hflags());
    latx_options_printf("[LATX-SYS]   enable link = %d\n", inhibit_irq_hflags_link());
    latx_options_printf("[LATX-SYS]   link indir  = %d\n", inhibit_irq_hflags_link_indir());
    latx_options_printf("[LATX-SYS] -----------------------------------------------------\n");
    latx_options_printf("[LATX-SYS] option_opt_syscall = %d\n", option_opt_syscall);
    latx_options_printf("[LATX-SYS] option_opt_sysret = %d\n", option_opt_sysret);
    latx_options_printf("[LATX-SYS] option_opt_iret = %d\n", option_opt_iret);
#ifdef CONFIG_TSC_FREQ
    latx_options_printf("[LATX-SYS] option_opt_rdtsc_rdtscp = %d (freq:1 = %d)\n", option_opt_rdtsc_rdtscp, option_opt_rdtsc_freq);
#else
    latx_options_printf("[LATX-SYS] option_opt_rdtsc_rdtscp = %d (freq:0 = %d)\n", option_opt_rdtsc_rdtscp, option_opt_rdtsc_freq);
#endif
    latx_options_printf("[LATX-SYS] option_opt_mmio = %x\n", option_opt_mmio);
    latx_options_printf("[LATX-SYS] -----------------------------------------------------\n");
    latx_options_printf("[LATX-SYS] option_imm_cache = %d\n", option_imm_cache);
    latx_options_printf("[LATX-SYS] option_imm_rip = %d\n", option_imm_rip);
    latx_options_printf("[LATX-SYS] option_imm_pattern = %d\n", option_imm_pattern);
    latx_options_printf("[LATX-SYS] option_imm_debug = %d\n", option_imm_debug);
    latx_options_printf("[LATX-SYS] imm_skip_pc = %lx\n", imm_skip_pc);
    latx_options_printf("[LATX-SYS] imm_pc_prefix = %lx\n", imm_pc_prefix);
    latx_options_printf("[LATX-SYS] imm_pc_mask = %lx\n", imm_pc_mask);
    latx_options_printf("[LATX-SYS] -----------------------------------------------------\n");

#if defined(CONFIG_LATX_OPT)
    latx_options_printf("[LATX-SYS] [1] %-50s\n", "configure enable-latx-opt");
#else
    latx_options_printf("[LATX-SYS] [0] %-50s\n", "configure enable-latx-opt");
#endif
#if defined(CONFIG_LATX_OPT_NOFP)
    latx_options_printf("[LATX-SYS] [1] %-50s\n", "configure enable-latx-opt-nofp");
#else
    latx_options_printf("[LATX-SYS] [0] %-50s\n", "configure enable-latx-opt-nofp");
#endif
#if defined(CONFIG_LATX_OPT_EFLAGS)
    latx_options_printf("[LATX-SYS] [1] %-50s\n", "configure enable-latx-opt-eflags");
#else
    latx_options_printf("[LATX-SYS] [0] %-50s\n", "configure enable-latx-opt-eflags");
#endif
#if defined(CONFIG_LATX_OPT_BQL)
    latx_options_printf("[LATX-SYS] [1] %-50s %ld\n", "configure enable-latx-opt-bql", qcondwait3_max);
#else
    latx_options_printf("[LATX-SYS] [0] %-50s\n", "configure enable-latx-opt-bql");
#endif
#if defined(CONFIG_LATX_OPT_MMIO_LOCKLESS)
    latx_options_printf("[LATX-SYS] [1] %-50s\n", "configure enable-latx-opt-mmio-lockless");
#else
    latx_options_printf("[LATX-SYS] [0] %-50s\n", "configure enable-latx-opt-mmio-lockless");
#endif
#if defined(CONFIG_LATX_HACK_MEM)
    latx_options_printf("[LATX-SYS] [1] %-50s\n", "configure enable-latx-hack-mem");
#else
    latx_options_printf("[LATX-SYS] [0] %-50s\n", "configure enable-latx-hack-mem");
#endif
#if defined(CONFIG_ATOMIC)
    latx_options_printf("[LATX-SYS] [1] %-50s\n", "configure enable-atomic");
#else
    latx_options_printf("[LATX-SYS] [0] %-50s\n", "configure enable-atomic");
#endif
#if defined(CONFIG_VICTIM_TLB)
    latx_options_printf("[LATX-SYS] [1] %-50s\n", "configure enable-victim-tlb");
#else
    latx_options_printf("[LATX-SYS] [0] %-50s\n", "configure enable-victim-tlb");
#endif
#if defined(CONFIG_PCREL)
    latx_options_printf("[LATX-SYS] [1] %-50s\n", "configure enable-pcrel");
#else
    latx_options_printf("[LATX-SYS] [0] %-50s\n", "configure enable-pcrel");
#endif
#if defined(CONFIG_TCG_REGION_TREE_ENABLE)
    latx_options_printf("[LATX-SYS] [1] %-50s\n", "configure enable-tcg-region-tree");
#else
    latx_options_printf("[LATX-SYS] [0] %-50s\n", "configure enable-tcg-region-tree");
#endif
    latx_options_printf("[LATX-SYS] -----------------------------------------------------\n");
#if defined(CONFIG_LATX_PRINT)
    latx_options_printf("[LATX-SYS] [1] %-50s\n", "configure enable-latx-print");
#else
    latx_options_printf("[LATX-SYS] [0] %-50s\n", "configure enable-latx-print");
#endif
#if defined(CONFIG_CAPSTONE_DIET)
    latx_options_printf("[LATX-SYS] [1] %-50s\n", "configure enable-capstone-diet");
#else
    latx_options_printf("[LATX-SYS] [0] %-50s\n", "configure enable-capstone-diet");
#endif
#if defined(CONFIG_LATX_RR)
    latx_options_printf("[LATX-SYS] [1] %-50s\n", "configure enable-latx-rr");
#else
    latx_options_printf("[LATX-SYS] [0] %-50s\n", "configure enable-latx-rr");
#endif
#if defined(CONFIG_LATX_PROFILER)
    latx_options_printf("[LATX-SYS] [1] %-50s\n", "configure enable-latx-profiler");
#else
    latx_options_printf("[LATX-SYS] [0] %-50s\n", "configure enable-latx-profiler");
#endif
#if defined(CONFIG_DEBUG_TCG)
    latx_options_printf("[LATX-SYS] [1] %-50s\n", "configure enable-debug");
#else
    latx_options_printf("[LATX-SYS] [0] %-50s\n", "configure enable-debug");
#endif
#if defined(CONFIG_LATX_TB_EXEC_TRACE)
    latx_options_printf("[LATX-SYS] [1] %-50s\n", "configure enable-latx-tb-exec-trace");
#else
    latx_options_printf("[LATX-SYS] [0] %-50s\n", "configure enable-latx-tb-exec-trace");
#endif
#if defined(CONFIG_LATX_TCG_MODE)
    latx_options_printf("[LATX-SYS] [1] %-50s\n", "configure enable-latx-tcg-mode");
#else
    latx_options_printf("[LATX-SYS] [0] %-50s\n", "configure enable-latx-tcg-mode");
#endif
    latx_options_printf("[LATX-SYS] -----------------------------------------------------\n");

}

#if defined(CONFIG_LATX_TCG_MODE)
void latxs_tcg_mode_configure(void);
void latxs_tcg_mode_configure(void)
{
    latx_options_printf("[LATX-SYS] -----------------------------------------------------\n");

    option_sigfpe = 0;
    latx_options_printf("[LATX-SYS] %s:%d sigfpe = 0\n", __func__, __LINE__);

    option_split_tb = 0;
    latx_options_printf("[LATX-SYS] %s:%d split tb = 0\n", __func__, __LINE__);

    option_split_search = 0;
    latx_options_printf("[LATX-SYS] %s:%d split search = 0\n", __func__, __LINE__);

    option_soft_ras = 0;
    option_rastoptp = 0;
    latx_options_printf("[LATX-SYS] %s:%d tb ras = 0\n", __func__, __LINE__);

    option_pause = 0;
    latx_options_printf("[LATX-SYS] %s:%d pause = 0\n", __func__, __LINE__);

    option_pushpop = 0;
    latx_options_printf("[LATX-SYS] %s:%d pushpop = 0\n", __func__, __LINE__);

    option_tu_mode = LATX_TU_MODE_OFF;
    latx_options_printf("[LATX-SYS] %s:%d latx tu mode = off\n",
                        __func__, __LINE__);

    option_aot_tu_pretranslate = 0;
    option_aot_tu_pre_disasm = 0;
    option_aot_cross_page_tu = 0;

    option_shbr = 0;
    latx_options_printf("[LATX-SYS] %s:%d shbr = 0\n", __func__, __LINE__);
    option_ghbr = 0;

    option_region_prologue = 0;
    latx_options_printf("[LATX-SYS] %s:%d option_region_prologue = 0\n", __func__, __LINE__);

    option_lazypc = 0;
    latx_options_printf("[LATX-SYS] %s:%d lazypc = 0\n", __func__, __LINE__);

    option_fast_itlb_ptw = 0;
    latx_options_printf("[LATX-SYS] %s:%d fast itlb ptw = 0\n", __func__, __LINE__);

    option_sys_aot = 0;
    option_aot_save = 0;
    option_aot_load = 0;
    option_aot_gpr_highbits = 0;
    option_tu_gpr_highbits = 0;
    latx_options_printf("[LATX-SYS] %s:%d sysaot = 0\n", __func__, __LINE__);

    option_hamt = 0;
    option_hamt_config = 0;
    latx_options_printf("[LATX-SYS] %s:%d hamt = 0\n", __func__, __LINE__);
    option_fast_atomic = 0;
    latx_options_printf("[LATX-SYS] %s:%d fast atomic = 0\n", __func__, __LINE__);
    option_fast_masked_v = 0;
    latx_options_printf("[LATX-SYS] %s:%d fast masked v = 0\n", __func__, __LINE__);

    latx_options_printf("[LATX-SYS] -----------------------------------------------------\n");
}
#endif

#ifdef CONFIG_TSC_FREQ
static int probe_cpu_max_freq(void)
{
    FILE *fp = NULL;
    char freq[64];
    unsigned long freq_mhz = 2000;
    float f = 2000.00;
    fp = fopen("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq", "r");
    if (fp) {
        if (fgets(freq, sizeof(freq), fp)) {
            freq_mhz = strtoul(freq, NULL, 10) / 1000;
        }
        fclose(fp);
    } else {
        fp = fopen("/proc/cpuinfo", "r");
        if (fp) {
            while (fgets(freq, sizeof(freq), fp)) {
                if (strncmp(freq, "CPU MHz", 7) == 0 ||
                    strncmp(freq, "cpu MHz", 7) == 0)
                {
                    sscanf(freq, "%*[^:]: %f", &f);
                    freq_mhz = (int)f;
                    break;
                }
            }
            fclose(fp);
        }
    }
    return freq_mhz / 100;
}
#endif

void latxs_options_init(void)
{
    latx_disable_dump();
    latxs_arch_options_init();

    option_ir1_cache_entries = 0;
    option_ir1_cache_ways = LATX_IR1_CACHE_WAYS_DEFAULT;
    option_ir1_cache_replace = LATX_IR1_CACHE_REPLACE_LRU;
    option_ir1_cache_enabled = 1;
    option_ir1_cache_mode = LATX_IR1_CACHE_MODE_ON;
    option_hot_decode_enabled = 1;
    option_ir1_timing = 0;
    option_ir1_distribution = 0;
    option_ir1_profile = 0;
    option_ir1_fast_cache_entries = LATX_IR1_FAST_CACHE_ENTRIES_DEFAULT;
    option_ir1_hash = LATX_IR1_HASH_MURMUR;

#ifdef CONFIG_PCREL
    option_page_pcrel = 1;
    option_tb_pcrel = 1;
#else
    option_page_pcrel = 0;
    option_tb_pcrel = 0;
#endif

    option_staticcs = 1; // staticcs

    /* ====== 1. Optimization for TB Link ====== */
    option_tb_link = 1; // tblink
    option_intb_link = 1; // intblink
#ifdef CONFIG_SOFTMMU
#ifdef CONFIG_LATX_RR
    option_intb_njc = 0; // njc
    option_cross_page = 0x2; // cp: always lookup
#else
    option_intb_njc = 1; // njc
    option_cross_page = 0x155; // cp: link with inline P0 check
#endif
#else /* user mode */
    option_intb_njc = 1; // njc
    option_cross_page = 0x11; // cp: link with check
#endif
    option_cross_page_tu = 0; // tucp
    option_tucp_fast = 0; // tucpfast
#ifdef CONFIG_SOFTMMU
    option_external_tu_stub = true;
#else
    option_external_tu_stub = false;
#endif
    option_tucp_generation = 0; // tucpgen
#ifdef CONFIG_SOFTMMU
    option_jmp_cache_hash = LATX_JMP_CACHE_HASH_PAGE; // njchash
#else
    option_jmp_cache_hash = LATX_JMP_CACHE_HASH_FULL; // njchash
#endif
    option_jmp_cache_bits = 12; // njcbits
    option_jmp_cache_bits_set = false;
    option_jmp_cache_version = LATX_JMP_CACHE_COMPACT; // njcver
    option_intb_njc_inline = 0; // njcinline
    option_tucp_generation_array_size = 1ULL << 20; // tucpgensize
    option_cross_page_counter = false; // cpcounter
    option_tb_counter = false; // tbcounter
    option_intb_counter = false; // intbcounter
    option_soft_ras = 1; // ras
    option_rastoptp = 1; // rastoptp
    option_tu_hras = 2; // tuhras
    option_tu_hras_opt_copy = 0; // tuhrascopy
    option_pause = 1; // pause
#ifdef CONFIG_SOFTMMU
    option_tu_mode = LATX_TU_MODE_AOT;
#else
    option_tu_mode = LATX_TU_MODE_JIT;
#endif
    option_aot_tu_pretranslate = 1;
    option_aot_tu_pre_disasm = 1;
    option_aot_cross_page_tu = 1;
    option_tu_max_tb = 50;
    option_aot_tu_max_tb = 1024;
    option_shbr = 2;
    option_ghbr = 2;
#ifdef CONFIG_SOFTMMU
    options_set_tucpopt("max");
#else
    options_set_tucpopt("off");
#endif
#ifdef CONFIG_SOFTMMU
    option_fast_excep = 1; // fastexcep
    option_flaged_sht = 1; // fsht
    option_fast_itlb = 1;
    option_fast_itlb_ptw = 1;
#if defined(CONFIG_LATX_RR)
    option_lazypc = 0; // lazypc
#else
    option_lazypc = 1; // lazypc
#endif
    option_link_opt = 1; // linkopt
    option_tb_config = 1; /* always generate exit stub */
#else /* user mode */
    option_fast_excep = 0; // fastexcep
    option_flaged_sht = 1; // fsht
    option_fast_itlb = 0;
    option_fast_itlb_ptw = 0;
    option_lazypc = 1; // lazypc
    option_link_opt = 1; // linkopt
    option_tb_config = 2; /* no exit stub and mark user mode */
#endif

    /* ====== 2. Optimization of Translation ====== */
    option_lsfpu = 1; // lsfpu
    option_fast_fpr_ldst = 1; // ffldst
    option_sys_flag_reduction = 1; // sysflag
    option_sys_flag_reduction_tu = FLAGRDTN_TU_DEFAULT; // sysflagtu
    option_instptn = 0x3ffffffffULL; /* instptn */
    option_pushpop = 1; // pushpop
    option_pushpop_ret = 1;
    option_by_hand_64 = 1; // byhand64
    option_risk_mem_offset = 1; // risky[0]
    option_risk_simd_ignore_upper64 = 0; // risky[1]
    option_risk_simd_ignore_upper64_stat_track = 0; // risky[2]
    option_lazy_mxcsr = 1; // risky[3]
#if defined(CONFIG_LATX_RR)
    option_risk_in_379 = 0; // risky[4]
#else
    option_risk_in_379 = 1; // risky[4]
#endif
    option_fast_atomic = 1; // risky[5]
    option_sigfpe = 1; // sigfpe
#ifdef CONFIG_SOFTMMU
    option_fast_masked_v = 1; // fast_masked_v
#else
    option_fast_masked_v = 0; // fast_masked_v
#endif
    option_soft_fpu = 1; // softfpu
    option_soft_fpu_fast = 0xfffffff; // softfpu_fast
    option_no_reserve_256 = 1; //don't reserve [255:128] in cvtps2pd
    option_aes_opt = 0x7f; // aes
#ifdef CONFIG_SOFTMMU
	option_sys_aot = 1;
	option_aot_save = 1;
	option_aot_load = 1;
        option_aot_cross_page_tu = 1;
#else
	option_sys_aot = 0;
	option_aot_save = 0;
	option_aot_load = 0;
        option_aot_cross_page_tu = 0;
#endif
    option_aot_gpr_highbits = 1;
    option_tu_gpr_highbits = jit_tu_requested();
#ifdef CONFIG_SOFTMMU
	option_lock_and_lookup_level = LAL_INIT_LEVEL; // lockandlookup
#else
	option_lock_and_lookup_level = 0; // lockandlookup
#endif

    if (__cpucfg(0x2) & CPUCFG2_LASX) {
//    if (cpuinfo & CPUINFO_LASX) {
        option_enable_lasx = 1;
    } else {
        option_enable_lasx = 0;
    }

    option_imm_cache = 1;
    option_imm_rip = 1;
    option_imm_pattern = 1;
    option_imm_debug = 0;
    option_imm_mode = 0;

    /* ====== 3. Optimization of Memory Access ====== */
#ifdef CONFIG_SOFTMMU
    option_hamt = 1; // hamt
    option_hamt_mode = HAMT_MODE_DMW; // hamtmode
    option_hamt_submode = 0; // hamtsubmode
    option_hamt_config = 0x33e9; // hamtconfig, taskset and nosmt
    option_pcid = 1;
    option_pcid_asid = 1;
    option_pcid_cache = 1;
    option_htimer = 0;
    option_irq_gate_nobql = 1;
#else
    option_hamt = 0; /* no hamt in user mode */
    option_hamt_mode = 0;
    option_hamt_submode = 0;
    option_hamt_config = 0;
    option_htimer = 0;
    option_irq_gate_nobql = 0;
#endif
    option_common_slowpath = 1; // comsmmu
    option_softmmu_pcaddi_cold = 0; // smmupcaddi (experimental, default off)
    option_softmmu_rmw_reuse = 0; // smmurmw (experimental, default off)

    /* ====== 4. Code Cache Configuration ====== */
    option_large_code_cache = 1; // largecc

    /* ====== 5. Misc Optimization ====== */
#ifdef CONFIG_SOFTMMU
#if defined(CONFIG_LATX_RR)
    option_int_opt = IRQ_CHECK_NO_OPT; // intopt
#else
    option_int_opt = IRQ_CHECK_BLOCK_END; // intopt
#endif
#else /* user mode */
    option_int_opt = IRQ_CHECK_NO_OPT; // intopt
#endif
    option_inhibit_irq = inhibit_irq_default(); // inhibitirq
    option_region_prologue = 1; // regionprologue
    option_split_tb = 1; // sptb
#if defined(CONFIG_LATX_RR)
    option_split_search = 0; // sleb
#else
    option_split_search = 1; // sleb
#endif
    option_new_assemble = 1;  // newassembal
#ifdef CONFIG_SOFTMMU
    option_opt_syscall = 2; // opt_syscall
    option_opt_sysret = 1; // opt_sysret
#else /* user mode */
    option_opt_syscall = 0; // opt_syscall
    option_opt_sysret = 0; // opt_sysret
#endif
    option_opt_iret = 0; // opt_iret
    option_opt_rdtsc_rdtscp = 1; //use rdtime_d for rdtsc
#ifdef CONFIG_TSC_FREQ
    option_opt_rdtsc_freq = probe_cpu_max_freq();
#endif
    option_opt_mmio = 0x3; // opt_mmio

    /* ====== 6. Misc Configuration ====== */
    option_smmu_slow = 0; // smmu
    option_split_strins = 0; // spstrins
    option_tb_flush_on_reboot = 1; // tbflushonreboot
    option_tb_max_insns = MAX_IR1_NUM_PER_TB; // tbsizei
    option_traceir1 = 0; // traceir1
    option_traceir1_data = 0; // traceir1data
    option_trace_simple = 0; // traces
    option_trace_start_nr = 0; // tracestnr
    option_trace_start_tb = 0; // tracesttb
    option_trace_start_tb_set = 0;

#if defined(CONFIG_LATX_TCG_MODE)
    latxs_tcg_mode_configure();
#endif
}

void latxs_options_init2(void)
{
    latx_options_dump_prepare();
    latx_sys_check_options();
    dump_latxs_options();
    latx_options_printf("[LATX-SYS] arch optimizations: tableaddr=%d njc_layout=%u "
            "njc_return=%u irq_outline=%u irq_backedge=%u "
            "qht_initial_override=%u\n",
            option_table_addr, option_njc_layout, option_njc_source_jump, option_irq_outline,
            option_irq_backedge, option_tb_htable_bits);
    latx_options_printf("[LATX-SYS] jmp cache hash = %s\n",
            latx_jmp_cache_hash_name(option_jmp_cache_hash));
    latx_options_printf("[LATX-SYS] jmp cache bits = %d\n",
            latx_jmp_cache_bits());
    latx_options_printf("[LATX-SYS] jmp cache implementation = %s\n",
            latx_jmp_cache_version_name(option_jmp_cache_version));
    latx_options_printf("[LATX-SYS] jmp cache inline = %s\n",
            option_intb_njc_inline ? "on" : "off");
    latx_options_printf("[LATX-SYS] jmp cache TLB flush = %s\n",
            jmp_cache_needs_full_flush() ? "all" : "page");
}

extern void tcg_region_get_info(size_t *size, int *n_mtcc, int *mtcc_n);

void latxs_verbose_tcg_options(void);
void latxs_verbose_tcg_options(void)
{
    if (!option_verbose) return;

    size_t region_size; // MB
    int region_n_mtcc;
    int region_mtcc_n;
    tcg_region_get_info(&region_size, &region_n_mtcc, &region_mtcc_n);

    latx_options_printf("[LATX-SYS] binary translation engine (mode) = %s\n",
            qemu_tcg_mode_2_lat() ? "lat" : "tcg");
    latx_options_printf("[LATX-SYS] vCPU thread mode (thread) = %s\n",
            qemu_tcg_mttcg_enabled() ? "multi" : "single");
    latx_options_printf("[LATX-SYS] multi-stat code cache (mtcc) = %d\n", qemu_tcg_mtcc_enabled());
    latx_options_printf("[LATX-SYS] faster mmu translate (fastmmu) = %d\n",
            qemu_tcg_fast_mmu_enabled());
    latx_options_printf("[LATX-SYS] code cache flush mode (flush) = %s\n",
            qemu_tcg_flush_full() ? "full flush" : "unknown");
    latx_options_printf("[LATX-SYS] region.size = %ld MB\n", region_size);
    latx_options_printf("[LATX-SYS] region.n    = %d\n", region_mtcc_n);
    latx_options_printf("[LATX-SYS] region.mtcc = %d\n", region_n_mtcc);
    latx_options_printf("[LATX-SYS] size of  each code cache = %ld MB = %ld MB * %d\n",
            region_size * region_mtcc_n, region_size, region_mtcc_n);
    latx_options_printf("[LATX-SYS] size of total code cahce = %ld MB = %ld MB * %d * %d\n",
            region_size * region_mtcc_n * region_n_mtcc,
            region_size, region_mtcc_n, region_n_mtcc);
    latx_options_printf("[LATX-SYS] -----------------------------------------------------\n");
}

void latx_disable_all_tblink(void)
{
    option_tb_link              = 0;
    option_intb_link            = 0;
    option_intb_njc             = 0;
    option_cross_page           = 0;
}

void latx_enable_dump(void)
{
    option_dump      = 1;
    option_dump_ir1  = 1;
    option_dump_ir2  = 1;
    option_dump_host = 1;
}

void latx_disable_dump(void)
{
    option_dump      = 0;
    option_dump_ir1  = 0;
    option_dump_ir2  = 0;
    option_dump_host = 0;
}

void optbql_parse_options(QemuOpts *opts);
void optbql_parse_options(QemuOpts *opts)
{
    QemuOpt *opt;

    opt = qemu_opt_find(opts, "waitn");
    if (opt) {
        qcondwait3_max = opt->value.uint;
    }
#if defined(CONFIG_LATX_PRINT)
    printf("%s:%d set qcondwait3 max = %ld\n", __func__, __LINE__,
                      qcondwait3_max);
#endif
}
