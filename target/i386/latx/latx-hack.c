/*
 * SPDX-FileCopyrightText: 2024-2026 LATS Project Authors
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "common.h"
#include "ir1.h"
#include "ir2.h"
#include "lsenv.h"
#include "reg-alloc.h"
#include "latx-options.h"
#include "flag-reduction.h"
#include "translate.h"
#include "latx-config.h"
#include "latx-sys-flag-rdtn.h"
#include "latx-static-codes.h"
#include "latx-tb-trace-sys.h"
#include "latx-sys-inst-ptn.h"
#include "latx-native-asm.h"
#include "latx-hamt.h"
#include "latx-hamt-counter.h"
#include "hamt-ptw.h"
#include "hamt-va.h"
#include "latx-hack.h"

uint64_t latx_hack_rambase;
uint64_t latx_hack_rambase_mask;
uint64_t latx_hack_rambase_tbmask;

#ifdef CONFIG_LATX_HACK_MEM
uint64_t latx_hack_ram_below_4g_end;
uint64_t latx_hack_ram_above_4g_start;
uint64_t latx_hack_ram_above_4g_end;

uint64_t latx_hack_mem_backend_extra_size(void)
{
    /* High RAM aliases start at the 4 GiB backend offset.  Reserve the
     * full 4 GiB low address range so a split as low as 1 GiB, required
     * by the Reims 1 GiB BAR, cannot truncate the alias. */
    return (uint64_t)4 << 30;
}

void latx_hack_mem_set_layout(uint64_t below_4g_end,
                              uint64_t above_4g_start,
                              uint64_t above_4g_size)
{
    latx_hack_ram_below_4g_end = below_4g_end;
    latx_hack_ram_above_4g_start = above_4g_start;
    latx_hack_ram_above_4g_end = above_4g_start + above_4g_size;
}
#endif

static void __attribute__((__constructor__)) latx_hack_init(void)
{
#ifdef CONFIG_SOFTMMU
    if (is_host_kernel_48_va()) {
        latx_hack_rambase        = __HACKRAMBASE;
        latx_hack_rambase_mask   = __HACKRAMBASE_MASK;
        latx_hack_rambase_tbmask = __HACKRAMBASE_TBMASK;
    } else {
        latx_hack_rambase        = __HACKRAMBASE40;
        latx_hack_rambase_mask   = __HACKRAMBASE40_MASK;
        latx_hack_rambase_tbmask = __HACKRAMBASE40_TBMASK;
    }
#endif
}
