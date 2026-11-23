/*
 * SPDX-FileCopyrightText: 2024-2026 LATS Project Authors
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef _HAMT_TLB_H_
#define _HAMT_TLB_H_

/* A collision only widens INVLPG to a local full flush. */
#define HAMT_LARGE_PAGE_SHIFT 21
static inline uint32_t hamt_large_page_bit(uint64_t va)
{
    return UINT32_C(1) << ((va >> HAMT_LARGE_PAGE_SHIFT) & 31);
}

void hamt_set_hardware_tlb(void *env,
        uint64_t vaddr, uint64_t paddr,
        int prot, int istlbr);
void hamt_set_hardware_tlb_nocheck(void *env,
        uint64_t vaddr, uint64_t paddr,
        int prot, int is_tlbr);

void hamt_protect_code(uint64_t guest_phys_page, int full);
void hamt_unprotect_code(uint64_t guest_phys_page);

void hamt_set_tlb(void *env,
        uint64_t vaddr, uint64_t paddr,
        int prot, bool mode);
struct CPUArchState;
bool hamt_switch_pcid(struct CPUArchState *env, uint64_t cr3, bool noflush);
bool hamt_invalidate_single_pcid(struct CPUArchState *env, unsigned pcid);
void hamt_flush_tlb_all(void);
void hamt_flush_tlb_page(uint64_t vaddr);

void hamt_set_tlb_simple(uint64_t ehi,
        uint64_t elo0, uint64_t elo1,
        int is_tlbr);

#endif
