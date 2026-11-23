/*
 * SPDX-FileCopyrightText: 2024-2026 LATS Project Authors
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <stdarg.h>
#include <stddef.h>
#include <errno.h>
#include <stdbool.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <pthread.h>

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/resource.h>

#include "qemu/osdep.h"
#include "qemu/compiler.h"
#include "qemu/host-utils.h"
#include "qemu/typedefs.h"
#include "exec/cpu-defs.h"
#include "cpu.h"
#include "exec/cpu-all.h"
#include "exec/exec-all.h"
#include "qemu/xxhash.h"
#include "exec/ram_addr.h"

#include "latx-perfmap.h"
#include "latx-options.h"

#include "dune-info.h"

#include "hamt-internal.h"
#include "hamt-init.h"
#include "hamt-enter.h"
#include "hamt.h"
#include "hamt-interrupt.h"
#include "hamt-data.h"
#include "hamt-entry-func.h"
#include "latx-sys-hamt.h"
#include "error.h"
#include "latx-hamt.h"
#include "latx-hamt-counter.h"
#include "htimer.h"

#include "hamt-va.h"
int hamt_va;

int hamt_cpus_num;

hamt_cpu_base_def hamt_cpu_base[HAMT_CPU_MAX];

/*
 *        id for different data
 *        ||
 * 0x80_0000_0000
 *      ||
 *      cpuid
 */
#define HAMT_BASE   0x8000000000ULL
#define HAMT_IPI    0x8a00000000ULL

//static void dump_hamt_cpu_base(hamt_cpu_base_def *d, int id)
//{
//    printf("%s:%d CPU[%d] data %p\n", __func__, __LINE__, id, d->data);
//    printf("%s:%d CPU[%d] tlb code %p\n", __func__, __LINE__, id, d->tlb_code);
//    printf("%s:%d CPU[%d] fpe code %p\n", __func__, __LINE__, id, d->fpe_code);
//    printf("%s:%d CPU[%d] err data %p\n", __func__, __LINE__, id, d->err_data);
//    printf("%s:%d CPU[%d] err code %p\n", __func__, __LINE__, id, d->err_code);
//    printf("%s:%d CPU[%d] ipi data %p\n", __func__, __LINE__, id, d->ipi_data);
//    printf("%s:%d ---------------------------------------\n", __func__, __LINE__);
//}

static void __attribute__((__constructor__)) hamt_init(void)
{
    uint64_t i = 0;
    for (; i < HAMT_CPU_MAX; ++i) {
        hamt_cpu_base[i].data     = HAMT_BASE | (i << 24);
        hamt_cpu_base[i].code     = HAMT_BASE | (i << 24) | (1 << 16);
        hamt_cpu_base[i].int_data = HAMT_BASE | (i << 24) | (2 << 16);
        hamt_cpu_base[i].err_data = HAMT_BASE | (i << 24) | (3 << 16);
        hamt_cpu_base[i].ipi_data = HAMT_IPI  | (i << 24);
//        dump_hamt_cpu_base(&hamt_cpu_base[i], i);
    }
}

/*
 *              data area
 * exception -> code_area -> handler
 *
 * exception code : generated during hamt init or compiled in .S
 * code/data area : generated during hamt init
 * handler        : writen in C codes
 */

void hamt_init_options_configuration(void)
{
    uint64_t base_hi = 0;
    if (is_host_kernel_48_va()) {
        base_hi = HAMT_7F;
    }

    for (int i = 0; i < HAMT_CPU_MAX; ++i) {
        hamt_cpu_base[i].data     |= base_hi;
        hamt_cpu_base[i].code     |= base_hi;
        hamt_cpu_base[i].err_data |= base_hi;
        hamt_cpu_base[i].ipi_data |= base_hi;
        hamt_cpu_base[i].int_data |= base_hi;
    }
}

#ifdef CONFIG_LATX_PRINT
#define HAMT_BUILD_VERBOSE  1
#else
#define HAMT_BUILD_VERBOSE  0
#endif

/*
 * hamt data area : maintain cpu context
 * [0]...[31] : GPRs
 *       [32] : EPC
 *       [33] : BADV
 *       [40] : tlbrehi
 *
 * more details in hamt-data.h
 */
uint64_t hamt_data_area[HAMT_CPU_MAX];
uint64_t hamt_int_data_area[HAMT_CPU_MAX];
uint64_t hamt_code_area[HAMT_CPU_MAX];
uint64_t hamt_fpe_handler_code_area[HAMT_CPU_MAX];

uint64_t hamt_err_data_area[HAMT_CPU_MAX];

uint64_t hamt_ipi_data_area[HAMT_CPU_MAX];

static inline void local_flush_icache_range(void)
{
    asm volatile ("\tibar 0\n"::);
}

static void *hamt_entry_tlb;
static void *hamt_entry_tlb_ptr;
static int hamt_entry_tlb_size;

uint64_t tlb_refill_entry_begin[HAMT_CPU_MAX];
uint64_t tlb_refill_entry_end[HAMT_CPU_MAX];
int tlb_refill_entry_size[HAMT_CPU_MAX];

void *tlb_refill_ptw[HAMT_CPU_MAX];

uint64_t tlb_ldstmy_entry_begin[HAMT_CPU_MAX];
uint64_t tlb_ldstmy_entry_end[HAMT_CPU_MAX];
int tlb_ldstmy_entry_size[HAMT_CPU_MAX];

uint64_t error_tlb_entry_begin[HAMT_CPU_MAX];
uint64_t error_tlb_entry_end[HAMT_CPU_MAX];
int error_tlb_entry_size[HAMT_CPU_MAX];

uint64_t break_entry_begin[HAMT_CPU_MAX];
uint64_t break_entry_end[HAMT_CPU_MAX];
int break_entry_size[HAMT_CPU_MAX];

#define HAMT_PREPARE_TLB_INIT() do {                        \
    if (hamt_entry_tlb == NULL) {                           \
        hamt_entry_tlb = mmap(NULL, (1<<14),                \
                PROT_READ | PROT_WRITE | PROT_EXEC,         \
                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);        \
        hamt_entry_tlb_ptr = hamt_entry_tlb;                \
        hamt_entry_tlb_size = 0;                            \
        lsassert(HAMT_HOLE_ASSERT(hamt_entry_tlb_ptr));     \
    }                                                       \
} while (0)

static void hamt_prepare_entry_tlb(int cpuid)
{
    if (tlb_refill_entry_size[cpuid]) return;
    HAMT_PREPARE_TLB_INIT();

    hamt_excp_t d;
    d.codebase = hamt_entry_tlb_ptr;
    d.database = (void *)hamt_data_area[cpuid];
    d.subcodebase = tlb_refill_ptw[cpuid];
    d.type  = HAMT_EXCP_ENTRY_TLBREFILL;
    d.verbose = HAMT_BUILD_VERBOSE;

    d.begin = &tlb_refill_entry_begin[cpuid];
    d.end   = &tlb_refill_entry_end[cpuid];
    d.size  = &tlb_refill_entry_size[cpuid];

    latxs_gen_hamt_excp(&d);
    local_flush_icache_range();

    hamt_entry_tlb_size += tlb_refill_entry_size[cpuid];
    hamt_entry_tlb_ptr = hamt_entry_tlb + hamt_entry_tlb_size;

    assert(hamt_entry_tlb_size < (1<<14));

    char name[64];
    sprintf(name, "hamt_tlb_refill_ptw_s%d", cpuid);
    latx_perfmap_insert((void *)tlb_refill_ptw[cpuid], 4096, name);
    latx_perfmap_flush();                                           \
}

void hamt_tlbrefill_counter_print_before_tb(int cpuid)
{
    if (!hamt_enabled()) return;
    if (!hamt_do_print_tlbrefill()) return;

    uint64_t ns = get_clock();
    static uint64_t so = 0;
    uint64_t sn = ns / 1000000000;
    if (so != sn) {
        so = sn;
        uint64_t *d = (void *)hamt_cpu_base[cpuid].data;
        printf("%s:%d CPU %d SEC %ld HAMT.TLBR" \
                "DBT/X86=(%-6ld/%-6ld) " \
                "2M/4K=(%-6ld/%-6ld) " \
                "4K/1M/1M4K=(%-6ld/%-6ld/%-6ld) " \
                "XX=(%-6ld) " \
                "C0/C3=(%-6ld/%-6ld)\n",
                __func__, __LINE__, cpuid, sn,
                d[HD_CNT_TLBR_DBT_INDEX],
                d[HD_CNT_TLBR_X86_INDEX],
                d[HD_CNT_TLBR_X86_2M_INDEX],
                d[HD_CNT_TLBR_X86_4K_INDEX],
                d[HD_CNT_TLBR_X86_OK_INDEX],
                d[HD_CNT_TLBR_X86_OK1M_INDEX],
                d[HD_CNT_TLBR_X86_OK1M4K_INDEX],
                d[HD_CNT_TLBR_X86_XX_INDEX],
                d[HD_CNT_TLBR_X86_C0_INDEX],
                d[HD_CNT_TLBR_X86_C3_INDEX]
                );
        d[HD_CNT_TLBR_DBT_INDEX] = 0;
        d[HD_CNT_TLBR_X86_INDEX] = 0;
        d[HD_CNT_TLBR_X86_2M_INDEX] = 0;
        d[HD_CNT_TLBR_X86_4K_INDEX] = 0;
        d[HD_CNT_TLBR_X86_OK_INDEX] = 0;
        d[HD_CNT_TLBR_X86_OK1M_INDEX] = 0;
        d[HD_CNT_TLBR_X86_OK1M4K_INDEX] = 0;
        d[HD_CNT_TLBR_X86_XX_INDEX] = 0;
        d[HD_CNT_TLBR_X86_C0_INDEX] = 0;
        d[HD_CNT_TLBR_X86_C3_INDEX] = 0;
    }
}

static void hamt_prepare_entry_tlb_ldstmy(int cpuid)
{
    if (tlb_ldstmy_entry_size[cpuid]) return;
    HAMT_PREPARE_TLB_INIT();

    hamt_excp_t d;
    d.codebase = (void *)hamt_entry_tlb_ptr;
    d.database = (void *)hamt_data_area[cpuid];
    d.subcodebase = (void *)hamt_tlb_handler;
    d.type  = HAMT_EXCP_ENTRY_TLBLDSTMY;
    d.verbose = HAMT_BUILD_VERBOSE;

    d.begin = &tlb_ldstmy_entry_begin[cpuid];
    d.end   = &tlb_ldstmy_entry_end[cpuid];
    d.size  = &tlb_ldstmy_entry_size[cpuid];

    latxs_gen_hamt_exception(&d);
    local_flush_icache_range();

    hamt_entry_tlb_size += tlb_ldstmy_entry_size[cpuid];
    hamt_entry_tlb_ptr = hamt_entry_tlb + hamt_entry_tlb_size;

    assert(hamt_entry_tlb_size < (1<<14));
}

static void hamt_prepare_entry_tlb_error(int cpuid)
{
    if (error_tlb_entry_size[cpuid]) return;
    HAMT_PREPARE_TLB_INIT();

    hamt_excp_t d;
    d.codebase = (void *)hamt_entry_tlb_ptr;
    d.database = (void *)hamt_data_area[cpuid];
    d.subcodebase = (void *)hamt_tlb_handler;
    d.type  = HAMT_EXCP_ENTRY_ERROR_TLB;
    d.verbose = HAMT_BUILD_VERBOSE;

    d.begin = &error_tlb_entry_begin[cpuid];
    d.end   = &error_tlb_entry_end[cpuid];
    d.size  = &error_tlb_entry_size[cpuid];

    latxs_gen_hamt_exception(&d);
    local_flush_icache_range();

    hamt_entry_tlb_size += error_tlb_entry_size[cpuid];
    hamt_entry_tlb_ptr = hamt_entry_tlb + hamt_entry_tlb_size;

    assert(hamt_entry_tlb_size < (1<<14));
}

#define INST_SIZE           4
#define INST_PER_EX_BLOCK   128
#define EX_BLOCK_SIZE       (INST_SIZE * INST_PER_EX_BLOCK)
#define FPE_EX_HANDLER_SIZE (EX_BLOCK_SIZE * 4)

static uint64_t hamt_ine_code_area[HAMT_CPU_MAX];

typedef struct {
    uint64_t *dest;
    size_t   size;
} MemAreaLayout;

#define HAMT_PREPARE_AREA_DO(id) do {                                     \
        /* data */                                                        \
        hamt_data_area[id] = (uint64_t)mmap(                              \
            (void *)hamt_cpu_base[id].data, getpagesize(),                \
            PROT_READ | PROT_WRITE | PROT_EXEC,                           \
            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);                          \
        hamt_err_data_area[id] = (uint64_t)mmap(                          \
            (void *)hamt_cpu_base[id].err_data, getpagesize(),            \
            PROT_READ | PROT_WRITE | PROT_EXEC,                           \
            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);                          \
        hamt_ipi_data_area[id] = (uint64_t)mmap(                          \
            (void *)hamt_cpu_base[id].ipi_data, getpagesize(),            \
            PROT_READ | PROT_WRITE | PROT_EXEC,                           \
            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);                          \
        hamt_int_data_area[id] = (uint64_t)mmap(                          \
            (void *)hamt_cpu_base[id].int_data, getpagesize(),            \
            PROT_READ | PROT_WRITE | PROT_EXEC,                           \
            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);                          \
        /* code */                                                        \
        hamt_code_area[id] = (uint64_t)mmap(                              \
            (void *)hamt_cpu_base[id].code, getpagesize(),                \
            PROT_READ | PROT_WRITE | PROT_EXEC,                           \
            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);                          \
        MemAreaLayout layouts[] = {                                       \
            { (uint64_t *)&tlb_refill_ptw[id], HAMT_PTW_CODE_SIZE },       \
            { &hamt_fpe_handler_code_area[id], FPE_EX_HANDLER_SIZE },     \
        };                                                                \
        uint64_t cur = hamt_code_area[id];                                \
        for (size_t i = 0; i < sizeof(layouts)/sizeof(layouts[0]); i++) { \
            *layouts[i].dest = cur;                                       \
            cur += layouts[i].size;                                       \
        }                                                                 \
        assert((cur - hamt_code_area[id]) <= getpagesize());              \
        hamt_ine_code_area[id] = (uint64_t)mmap(NULL, getpagesize(),     \
            PROT_READ | PROT_WRITE | PROT_EXEC,                           \
            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);                          \
        assert(hamt_ine_code_area[id] != (uint64_t)MAP_FAILED);           \
        assert(hamt_data_area[id] == hamt_cpu_base[id].data);             \
        assert(hamt_err_data_area[id] == hamt_cpu_base[id].err_data);     \
        assert(hamt_ipi_data_area[id] == hamt_cpu_base[id].ipi_data);     \
        assert(hamt_int_data_area[id] == hamt_cpu_base[id].int_data);     \
        assert(hamt_code_area[id] == hamt_cpu_base[id].code);             \
        latx_perfmap_flush();                                             \
} while (0)

#ifdef CONFIG_LATX_PRINT

#define HAMT_PREPARE_AREA_INFO(id) do {                                 \
    dune_info("data[%d] %p", id, (void *)(hamt_data_area[id]));         \
} while (0)
#define HAMT_PREPARE_AREA(id)      \
        HAMT_PREPARE_AREA_DO(id);  \
        HAMT_PREPARE_AREA_INFO(id);

#else

#define HAMT_PREPARE_AREA(id)  HAMT_PREPARE_AREA_DO(id)

#endif

static void *hamt_interrupt_code_base;
static void *hamt_interrupt_code_ptr;
static int hamt_interrupt_code_size;

uint64_t hamt_int_ti_entry_begin[HAMT_CPU_MAX];
uint64_t hamt_int_ti_entry_end[HAMT_CPU_MAX];
int      hamt_int_ti_entry_size[HAMT_CPU_MAX];

static void hamt_prepare_int_ti(int cpuid)
{
    if (!option_htimer ||
        hamt_int_ti_entry_size[cpuid] != 0) {
        return;
    }

    if (hamt_interrupt_code_ptr == NULL) {
        hamt_interrupt_code_ptr = hamt_interrupt_code_base;
    }

    hamt_excp_t d;
    d.codebase = (void *)hamt_interrupt_code_ptr;
    d.database = (void *)hamt_int_data_area[cpuid];
    d.subcodebase = (void *)apic_timer_deliver_from_loongarch;
    d.type  = HAMT_EXCP_ENTRY_TI;
    d.verbose = HAMT_BUILD_VERBOSE;

    d.begin = &hamt_int_ti_entry_begin[cpuid];
    d.end   = &hamt_int_ti_entry_end[cpuid];
    d.size  = &hamt_int_ti_entry_size[cpuid];

    latxs_gen_hamt_int(&d);
    local_flush_icache_range();

    hamt_interrupt_code_size += hamt_int_ti_entry_size[cpuid];
    hamt_interrupt_code_ptr +=  hamt_int_ti_entry_size[cpuid];
}

uint64_t hamt_int_ipi_entry_begin[HAMT_CPU_MAX];
uint64_t hamt_int_ipi_entry_end[HAMT_CPU_MAX];
int      hamt_int_ipi_entry_size[HAMT_CPU_MAX];

static void hamt_prepare_int_ipi(int cpuid)
{
    if (!hamt_enable_ipi() ||
        hamt_int_ipi_entry_size[cpuid] != 0) {
        return;
    }

    if (hamt_interrupt_code_ptr == NULL) {
        hamt_interrupt_code_ptr = hamt_interrupt_code_base;
    }

    hamt_excp_t d;
    d.codebase = hamt_interrupt_code_ptr;
    d.database = (void *)hamt_data_area[cpuid];
    d.database2 = (void *)hamt_ipi_data_area[cpuid];
    d.type = HAMT_EXCP_INTERRUPT_IPI;
    d.begin = &hamt_int_ipi_entry_begin[cpuid];
    d.end   = &hamt_int_ipi_entry_end[cpuid];
    d.size  = &hamt_int_ipi_entry_size[cpuid];
    d.verbose = HAMT_BUILD_VERBOSE;

    latxs_gen_hamt_excp(&d);
    local_flush_icache_range();

    hamt_interrupt_code_size += hamt_int_ipi_entry_size[cpuid];
    hamt_interrupt_code_ptr += hamt_int_ipi_entry_size[cpuid];

    assert(hamt_interrupt_code_size < 4096);
}

void hamt_ipi_counter_print_before_tb(int cpuid)
{
    if (!hamt_enabled() || !hamt_enable_ipi()) return;
    if (!hamt_do_print_ipi()) return;

    uint64_t *d[HAMT_CPU_MAX];
    uint64_t c[HAMT_CPU_MAX];

    int i = 0;

    for (i = 0; i < hamt_cpus_num; ++i) {
        d[i] = (void *)hamt_data_area[i];
        c[i] = d[i][HD_CNT_IPI_INDEX] - d[i][HD_CNT_IPIOLD_INDEX];
    }

    uint64_t ns = get_clock();
    static uint64_t so = 0;
    uint64_t sn = ns / 1000000000;
    if (so != sn) {
        so = sn;
        printf("%s:%d IPI ", __func__, __LINE__);
        for (i = 0; i < hamt_cpus_num; ++i) {
            printf("%-5lu ", c[i]);
            d[i][HD_CNT_IPIOLD_INDEX] = d[i][HD_CNT_IPI_INDEX];
        }
        printf("\n");
    }
}

static void *error_stop_code_base;
static void *error_stop_code_ptr;
static int error_stop_code_size;

uint64_t error_stop_entry_begin[HAMT_CPU_MAX];
uint64_t error_stop_entry_end[HAMT_CPU_MAX];
int error_stop_entry_size[HAMT_CPU_MAX];

static void hamt_prepare_entry_error_stop(int cpuid)
{
    if (error_stop_entry_size[cpuid]) return;

    if (error_stop_code_ptr == NULL) {
        error_stop_code_ptr = error_stop_code_base;
    }

    hamt_excp_t d;
    d.codebase = (void *)error_stop_code_ptr;
    d.database = (void *)hamt_err_data_area[cpuid];
    d.subcodebase = (void *)hamt_err_handler;
    d.type = HAMT_EXCP_ENTRY_ERROR_STOP;
    d.verbose = HAMT_BUILD_VERBOSE;

    d.begin = &error_stop_entry_begin[cpuid];
    d.end   = &error_stop_entry_end[cpuid];
    d.size  = &error_stop_entry_size[cpuid];

    latxs_gen_hamt_exception_error_stop(&d);

    local_flush_icache_range();

    error_stop_code_size += error_stop_entry_size[cpuid];
    error_stop_code_ptr = error_stop_code_base +
                          error_stop_code_size;

    assert(error_stop_code_size < 4096);
}

static void *hamt_entry_fpe;
static void *hamt_entry_fpe_ptr;
static int hamt_entry_fpe_size;

uint64_t ine_entry_begin[HAMT_CPU_MAX];
uint64_t ine_entry_end[HAMT_CPU_MAX];
static int ine_entry_size[HAMT_CPU_MAX];

static void hamt_prepare_entry_ine(int cpuid)
{
    if (ine_entry_size[cpuid]) return;
    hamt_excp_t d = {
        .codebase = (void *)hamt_ine_code_area[cpuid],
        .database = (void *)hamt_err_data_area[cpuid],
        .subcodebase = (void *)hamt_ine_handler,
        .type = HAMT_EXCP_ENTRY_INE,
        .verbose = HAMT_BUILD_VERBOSE,
        .begin = &ine_entry_begin[cpuid],
        .end = &ine_entry_end[cpuid],
        .size = &ine_entry_size[cpuid],
    };
    latxs_gen_hamt_exception(&d);
    local_flush_icache_range();
    assert(ine_entry_size[cpuid] <= EX_BLOCK_SIZE);
}

uint64_t fpe_entry_begin[HAMT_CPU_MAX];
uint64_t fpe_entry_end[HAMT_CPU_MAX];
int fpe_entry_size[HAMT_CPU_MAX];

static void hamt_prepare_entry_fpe(int cpuid)
{
    if (fpe_entry_size[cpuid]) return;

    if (hamt_entry_fpe_ptr == NULL) {
        hamt_entry_fpe_ptr = hamt_entry_fpe;
    }

    hamt_excp_t d;
    d.codebase = (void *)hamt_entry_fpe_ptr;
    d.database = (void *)hamt_data_area[cpuid];
    d.subcodebase = (void *)hamt_fpe_handler;
    d.type  = HAMT_EXCP_ENTRY_FPE;
    d.verbose = HAMT_BUILD_VERBOSE;

    d.begin = &fpe_entry_begin[cpuid];
    d.end   = &fpe_entry_end[cpuid];
    d.size  = &fpe_entry_size[cpuid];

    latxs_gen_hamt_exception(&d);
    local_flush_icache_range();

    hamt_entry_fpe_size += fpe_entry_size[cpuid];
    hamt_entry_fpe_ptr = hamt_entry_fpe + hamt_entry_fpe_size;

    assert(hamt_entry_fpe_size < (1<<14));
}

static void *hamt_entry_ale;
static void *hamt_entry_ale_ptr;
static int hamt_entry_ale_size;

uint64_t ale_entry_begin[HAMT_CPU_MAX];
uint64_t ale_entry_end[HAMT_CPU_MAX];
int ale_entry_size[HAMT_CPU_MAX];

static void hamt_prepare_entry_ale(int cpuid)
{
    if (ale_entry_size[cpuid]) return;

    if (hamt_entry_ale_ptr == NULL) {
        hamt_entry_ale_ptr = hamt_entry_ale;
    }

    hamt_excp_t d;
    d.codebase = (void *)hamt_entry_ale_ptr;
    d.database = (void *)hamt_data_area[cpuid];
    d.subcodebase = (void *)hamt_ale_handler;
    d.type  = HAMT_EXCP_ENTRY_ALE;
    d.verbose = HAMT_BUILD_VERBOSE;

    d.begin = &ale_entry_begin[cpuid];
    d.end   = &ale_entry_end[cpuid];
    d.size  = &ale_entry_size[cpuid];

    latxs_gen_hamt_exception(&d);
    local_flush_icache_range();

    hamt_entry_ale_size += ale_entry_size[cpuid];
    hamt_entry_ale_ptr = hamt_entry_ale + hamt_entry_ale_size;

    assert(hamt_entry_ale_size < (1<<14));
}

void hamt_ras_tp_mirror(int cpuid, uint64_t native_tp)
{
    if (!option_rastoptp) return;
    /* A TLS address cannot be mistaken for top or the sentinel's -1. */
    g_assert(native_tp > LATX_RAS_TP_SIZE && native_tp != UINT64_MAX);
    uint64_t *data = (void *)hamt_data_area[cpuid];

    g_assert(data);
    /*
     * This vCPU's dispatcher owns the four areas. A fresh mapping has
     * a zero TLS slot, and we publish the main slot last.
     */
    if (data[HD_RAS_NATIVE_TP_INDEX] == native_tp) {
        return;
    }

    uint64_t areas[] = { hamt_int_data_area[cpuid], hamt_err_data_area[cpuid],
                         hamt_ipi_data_area[cpuid] };
    for (unsigned i = 0; i < ARRAY_SIZE(areas); ++i) {
        g_assert(areas[i]);
        ((uint64_t *)areas[i])[HD_RAS_NATIVE_TP_INDEX] = native_tp;
    }
    qatomic_store_release(&data[HD_RAS_NATIVE_TP_INDEX], native_tp);
}

static void hamt_prepare_mt(int cpuid)
{
    HAMT_PREPARE_AREA(cpuid);
    /* Before installing any exception entry, including during C boot. */
    if (option_rastoptp) {
        uint64_t native_tp;
        asm volatile("or %0, $tp, $zero" : "=r"(native_tp));
        hamt_ras_tp_mirror(cpuid, native_tp);
    }

    hamt_prepare_entry_tlb(cpuid);
    hamt_prepare_entry_tlb_ldstmy(cpuid);
    hamt_prepare_entry_tlb_error(cpuid);
    hamt_prepare_entry_error_stop(cpuid);
    hamt_prepare_entry_ine(cpuid);
    hamt_prepare_entry_fpe(cpuid);
    hamt_prepare_entry_ale(cpuid);

    hamt_prepare_int_ipi(cpuid);
    if (option_htimer) {
        hamt_prepare_int_ti(cpuid);
    }
    if (cpuid == 0) {
        init_timer_mhz();
    }
}

static void *hamt_entry_common;
static void *hamt_entry_common_ptr;
static int hamt_entry_common_size;

uint64_t syscall_entry_begin;
uint64_t syscall_entry_end;
int syscall_entry_size;

static void hamt_prepare_syscall(void)
{
    if (syscall_entry_begin) {
        return;
    }

    if (hamt_entry_common_ptr == NULL) {
        hamt_entry_common_ptr = hamt_entry_common;
    }

    hamt_excp_t d;
    d.codebase = hamt_entry_common_ptr;
    d.type = HAMT_EXCP_ENTRY_SYSCALL;
    d.begin = &syscall_entry_begin;
    d.end   = &syscall_entry_end;
    d.size  = &syscall_entry_size;
    d.verbose = HAMT_BUILD_VERBOSE;

    latxs_gen_hamt_excp(&d);
    local_flush_icache_range();

    hamt_entry_common_size += syscall_entry_size;
    hamt_entry_common_ptr = hamt_entry_common + hamt_entry_common_size;

    assert(hamt_entry_common_size < 4096);
}

uint64_t fake_fork_entry_begin;
uint64_t fake_fork_entry_end;
int fake_fork_entry_size;

extern void (*hamt_do_fake_fork)(void);

static void hamt_prepare_fake_fork(void)
{
    if (fake_fork_entry_begin) {
        return;
    }

    if (hamt_entry_common_ptr == NULL) {
        hamt_entry_common_ptr = hamt_entry_common;
    }

    hamt_excp_t d;
    d.codebase = hamt_entry_common_ptr;
    d.type = HAMT_EXCP_ENTRY_FAKE_FORK;
    d.begin = &fake_fork_entry_begin;
    d.end   = &fake_fork_entry_end;
    d.size  = &fake_fork_entry_size;
    d.verbose = HAMT_BUILD_VERBOSE;

    latxs_gen_hamt_excp(&d);
    local_flush_icache_range();

    hamt_do_fake_fork = hamt_entry_common_ptr;

    hamt_entry_common_size += fake_fork_entry_size;
    hamt_entry_common_ptr = hamt_entry_common + hamt_entry_common_size;

    assert(hamt_entry_common_size < 4096);
}

static void hamt_alloc_handler_buffer(void)
{
    MemAreaLayout layouts[] = {
        { (uint64_t *)&hamt_interrupt_code_base,  FPE_EX_HANDLER_SIZE * 2 },
        { (uint64_t *)&error_stop_code_base,      FPE_EX_HANDLER_SIZE },
        { (uint64_t *)&hamt_entry_fpe,            FPE_EX_HANDLER_SIZE },
        { (uint64_t *)&hamt_entry_ale,            FPE_EX_HANDLER_SIZE },
        { (uint64_t *)&hamt_entry_common,         FPE_EX_HANDLER_SIZE },
    };

    void *code_base= mmap(NULL, getpagesize(),
            PROT_READ | PROT_WRITE | PROT_EXEC,
            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    uint64_t cur = (unsigned long)code_base;
    for (size_t i = 0; i < sizeof(layouts)/sizeof(layouts[0]); i++) {
        *layouts[i].dest = cur;
        cur += layouts[i].size;
    }
}

void hamt_prepare(int mode, int cpuid)
{
    hamt_alloc_handler_buffer();
    hamt_prepare_mt(cpuid);
    hamt_prepare_syscall();
    hamt_prepare_fake_fork();
}

void hamt_init_kvm_cpu(void *cpu, void *kvmcpu)
{
    CPUX86State *env = ((CPUState*)cpu)->env_ptr;
    env->hamt_dune_kvm_cpu = kvmcpu;
#ifdef CONFIG_LATX_PRINT
    fprintf(stderr, "%-20s CPU %p KVMCPU  %p\n",
            __func__, cpu, env->hamt_dune_kvm_cpu);
#endif
}

void hamt_init_kvm_regs(void *cpu, int size)
{
    CPUX86State *env = ((CPUState*)cpu)->env_ptr;
    env->hamt_kvm_regs = malloc(size);
#ifdef CONFIG_LATX_PRINT
    fprintf(stderr, "%-20s CPU %p KVMREGS %p\n",
            __func__, cpu, env->hamt_kvm_regs);
#endif
}
