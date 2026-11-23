/*
 * SPDX-FileCopyrightText: 2024-2026 LATS Project Authors
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "qemu/osdep.h"
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include "hamt-va.h"
#include "hamt-tlb.h"
#include "hamt-init.h"
#include "target/i386/latx/tb-record.h"
#include "latx-options.h"
#include "target/i386/latx/include/latx-hamt.h"
#include "include/hamt-fake-fork.h"

/* Stub implementations for non-LoongArch hosts */

/* Forward declarations for functions without header prototypes */
void hamt_init(void);
void hamt_ptwdata_smc_protect_page(uint64_t page);
void hamt_ptwdata_smc_unprotect_page(uint64_t page);
uint64_t *hamt_ptwdata_get_page(uint64_t page);
void hamt_ptwdata_set_page(uint64_t page, uint64_t *p);
void hamt_ptwdata_set_mmio(uint64_t start, uint64_t size);
int hamt_ptwdata_smc_range_is_protected(uint64_t start, uint64_t end);
void hamt_ptwdata_vga_snapshot(uint64_t start, uint64_t count, void *bitmap);
void hamt_ptwdata_vga_protect(uint64_t start, uint64_t count);
int hamt_ptwdata_vga_snapshot_is_dirty(uint64_t start, uint64_t count, void *bitmap);
void *hamt_get_ptwdata(void);
void *hamt_get_ptwdata1m(void);
void hamt_realloc_ptwdata(void *opaque);
int dune_enter(void *cpu, int mode, int cpuid);
void latxs_init_rr_thread_sigfpe(void);
void hamt_ras_tp_mirror(int cpuid, uint64_t native_tp);

int hamt_cpus_num = 0;
int hamt_va = 0;

void hamt_init_options_configuration(void)
{
}

void hamt_init(void)
{
}

bool hamt_is_running(void)
{
    return false;
}

pid_t hamt_fake_fork(void *dunekvmcpu, void *fn, void *data)
{
    (void)dunekvmcpu;
    (void)fn;
    (void)data;
    errno = ENOSYS;
    return -1;
}

bool hamt_switch_pcid(struct CPUArchState *env, uint64_t cr3, bool noflush)
{
    return false;
}

bool hamt_invalidate_single_pcid(struct CPUArchState *env, unsigned pcid)
{
    return false;
}

void hamt_flush_tlb_all(void)
{
}

void hamt_flush_tlb_page(uint64_t vaddr)
{
}

void hamt_protect_code(uint64_t guest_phys_page, int full)
{
}

int hamt_ptwdata_smc_page_is_protected(uint64_t page)
{
    return 0;
}

void hamt_ptwdata_smc_protect_page(uint64_t page)
{
}

void hamt_ptwdata_smc_unprotect_page(uint64_t page)
{
}

uint64_t *hamt_ptwdata_get_page(uint64_t page)
{
    return NULL;
}

void hamt_ptwdata_set_page(uint64_t page, uint64_t *p)
{
}

void hamt_ptwdata_set_mmio(uint64_t start, uint64_t size)
{
}

int hamt_ptwdata_smc_range_is_protected(uint64_t start, uint64_t end)
{
    return 0;
}

void hamt_ptwdata_vga_snapshot(uint64_t start, uint64_t count, void *bitmap)
{
}

void hamt_ptwdata_vga_protect(uint64_t start, uint64_t count)
{
}

int hamt_ptwdata_vga_snapshot_is_dirty(uint64_t start, uint64_t count, void *bitmap)
{
    return 0;
}

void *hamt_get_ptwdata(void)
{
    return NULL;
}

void *hamt_get_ptwdata1m(void)
{
    return NULL;
}

void hamt_realloc_ptwdata(void *opaque)
{
}

/* dune_enter stub */
int dune_enter(void *cpu, int mode, int cpuid)
{
    return 0;
}

/* latxs_init_rr_thread_sigfpe stub */
void latxs_init_rr_thread_sigfpe(void)
{
}

void hamt_poison_tb_entry(TranslationBlock *tb)
{
}

void hamt_ras_tp_mirror(int cpuid, uint64_t native_tp)
{
}
