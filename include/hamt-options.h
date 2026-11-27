/*
 * SPDX-FileCopyrightText: 2024-2026 LATS Project Authors
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef _HAMT_OPTIONS_H_
#define _HAMT_OPTIONS_H_

extern int option_hamt;
extern int option_hamt_mode;
extern int option_hamt_submode;
extern int option_hamt_config;
extern int option_hamt_dump;
extern int option_hamt_print;

/* Experimental: permit four-level long-mode data translation when CR4.PSE
 * is clear.  This first version restricts direct data accesses to CPL3. */
#define HAMT_CONFIG_LONG_NO_PSE (1U << 18)
static inline bool hamt_allow_long_no_pse(void) {
    return (option_hamt_config & HAMT_CONFIG_LONG_NO_PSE) != 0;
}

static inline int hamt_enabled(void) {
    return option_hamt;
}

static inline int hamt_dump_enabled(void) {
    return option_hamt_dump;
}

#define HAMT_MODE_GUEST32   0
#define HAMT_MODE_DMW       1
#define HAMT_MODE_HOLE      2

static inline int hamt_mode_32(void) {
    return option_hamt_mode == 0;
}
static inline int hamt_mode_dmw(void) {
    return option_hamt_mode == 1;
}
static inline int hamt_mode_hole(void) {
    return option_hamt_mode == 2;
}

static inline int hamt_hole_mode_40_8(void) {
    return hamt_mode_hole() && option_hamt_submode == 1;
}
static inline int hamt_hole_mode_48_7f(void) {
    return hamt_mode_hole() && option_hamt_submode == 2;
}
static inline int hamt_hole_assert_40_8(void *ptr) {
    return (((uint64_t)ptr >> 39) & 0x1) == 0x1;
}
static inline int hamt_hole_assert_48_7f(void *ptr) {
    return (((uint64_t)ptr >> 40) & 0xff) == 0x7f;
}
static inline int hamt_hole_assert(void *ptr) {
    if (hamt_hole_mode_40_8())  return hamt_hole_assert_40_8(ptr);
    if (hamt_hole_mode_48_7f()) return hamt_hole_assert_48_7f(ptr);
    return 1;
}
#define HAMT_HOLE_ASSERT(p) (hamt_hole_assert((void *)(p)))

static inline int hamt_enable_ipi(void) {
    return option_hamt_config & 0x100;
}
static inline int hamt_enable_ipi_sync(void) {
    return option_hamt_config & 0x200;
}

static inline int hamt_use_pgd(void) {
    return option_hamt_config & 0x10;
}
static inline int hamt_use_ptw(void) {
    return option_hamt_config & 0x20;
}
static inline int hamt_use_ptw_inrefill(void) {
    return option_hamt_config & 0xC0;
}
static inline int hamt_use_ptw_inrefill_4K1(void) {
    return (option_hamt_config & 0xC0) == (0x1 << 6);
}
static inline int hamt_use_ptw_inrefill_4K2(void) {
    return (option_hamt_config & 0xC0) == (0x2 << 6);
}
static inline int hamt_use_ptw_inrefill_4K2M(void) {
    return (option_hamt_config & 0xC0) == (0x3 << 6);
}

static inline bool hamt_do_cpl(int cpl) {
    if (hamt_allow_long_no_pse() && cpl != 3) {
        return false;
    }
    return option_hamt_config & (1 << (cpl & 0x3));
}
static inline bool hamt_do_cpl_any(void) {
    return option_hamt_config & 0xf;
}
static inline bool hamt_do_cpl_only(int cpl) {
    return (option_hamt_config & 0xf) == (1 << (cpl & 0x3));
}

static inline bool hamt_do_taskset(void) {
    return option_hamt_config & 0x1000;
}
static inline bool hamt_do_taskset_nosmt(void) {
    return option_hamt_config & 0x2000;
}
static inline bool hamt_do_vga(void) {
    return option_hamt_config & 0x4000;
}
static inline bool hamt_no_vga_dirty(void) {
    return option_hamt_config & 0x8000;
}
static inline bool hamt_disable_win32k(void) {
    return option_hamt_config & 0x10000;
}
static inline bool hamt_disable_asid(void) {
    return option_hamt_config & 0x20000;
}

static inline bool hamt_do_print_ipi(void) {
    return option_hamt_print & (0x1 << 0);
}
static inline bool hamt_do_print_tlbrefill(void) {
    return option_hamt_print & (0x1 << 1);
}
static inline bool hamt_do_print_ptw(void) {
    return option_hamt_print & (0x1 << 2);
}

bool hamt_is_running(void);
void hamt_start_running(void);
void hamt_stop_running(void);

void *hamt_new_pagetable(void);

#endif
