/*
 * Internal execution defines for qemu
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef ACCEL_TCG_INTERNAL_H
#define ACCEL_TCG_INTERNAL_H

void tcg_alloc_jump_caches(CPUJumpCache **jc, struct LatxNJC **njc);
void tcg_free_jump_caches(CPUJumpCache *jc, struct LatxNJC *njc);

#include "exec/exec-all.h"
#include "exec/translate-all.h"

/*
 * Access to the various translations structures need to be serialised
 * via locks for consistency.  In user-mode emulation access to the
 * memory related structures are protected with mmap_lock.
 * In !user-mode we use per-page locks.
 */
#ifdef CONFIG_USER_ONLY
#define assert_memory_lock() tcg_debug_assert(have_mmap_lock())
#else
#define assert_memory_lock()
#endif

#if defined(CONFIG_SOFTMMU) && defined(CONFIG_DEBUG_TCG)
void assert_no_pages_locked(void);
#else
static inline void assert_no_pages_locked(void) { }
#endif

#ifdef CONFIG_USER_ONLY
static inline void page_table_config_init(void) { }
#else
void page_table_config_init(void);
#endif

#ifdef CONFIG_USER_ONLY
/*
 * For user-only, page_protect sets the page read-only.
 * Since most execution is already on read-only pages, and we'd need to
 * account for other TBs on the same page, defer undoing any page protection
 * until we receive the write fault.
 */
static inline void tb_lock_page0(tb_page_addr_t p0)
{
    page_protect(p0);
}

static inline void tb_lock_page1(tb_page_addr_t p0, tb_page_addr_t p1)
{
    page_protect(p1);
}

static inline bool tb_page_locked(tb_page_addr_t paddr) { g_assert_not_reached(); return false;}
static inline bool tb_trylock_page0(tb_page_addr_t addr) { return false; }
static inline void tb_unlock_page0(tb_page_addr_t paddr) {  }
static inline void tb_unlock_page1(tb_page_addr_t p0, tb_page_addr_t p1) { }
static inline void tb_unlock_pages(TranslationBlock *tb) { }
static inline void tb_unlock_ctx_pages(int reset) { }
#else
bool tb_page_locked(tb_page_addr_t paddr);
bool tb_page_has_code_locked(tb_page_addr_t paddr);
void tb_lock_page0(tb_page_addr_t);
bool tb_trylock_page0(tb_page_addr_t);
void tb_unlock_page0(tb_page_addr_t paddr);
void tb_lock_page1(tb_page_addr_t, tb_page_addr_t);
void tb_unlock_page1(tb_page_addr_t, tb_page_addr_t);
void tb_unlock_pages(TranslationBlock *);
void tb_unlock_ctx_pages(int reset);
void unlock_page(uint64_t addr);
#endif

#ifdef CONFIG_SOFTMMU
void tb_invalidate_phys_range_fast(ram_addr_t ram_addr,
                                   unsigned size,
                                   uintptr_t retaddr);
G_NORETURN void cpu_io_recompile(CPUState *cpu, uintptr_t retaddr);
void page_print_tb(uint64_t page);
#endif /* CONFIG_SOFTMMU */

int get_max_insns(int cflags);
TranslationBlock *tb_gen_code(CPUState *cpu, vaddr pc,
                              uint64_t cs_base, uint32_t flags,
                              int cflags);
TranslationBlock *lat_tb_gen_code(CPUState *cpu, vaddr pc,
        uint64_t cs_base, uint32_t flags, int cflags);
TranslationBlock *lat_tb_gen_code_locked(CPUState *cpu,
        vaddr pc, uint64_t cs_base, uint32_t flags, int cflags,
        tb_page_addr_t phys_pc, void *host_pc, int *error);
#define GENCODE_ERR_NONE            0
#define GENCODE_ERR_FLUSH1          1
#define GENCODE_ERR_FLUSH2          2
#define GENCODE_ERR_TB_EXIST        3
#define GENCODE_ERR_BUF_OVERFLOW    4
#define GENCODE_ERR_TB_ONESHOT      5

/* Internal results handled by lat_tb_gen_code_locked(). */
#define __GENCODE_ERR_BUFFER_OVERFLOW    -1
#define __GENCODE_ERR_TB_TOO_LARGE       -2
#define __GENCODE_ERR_PAGE_LOCK_RETRY    -3
#define __GENCODE_ERR_AUX_DATA_OVERFLOW  -4

void tbmini_set_pointer(uint64_t* tbm, uint64_t tb_addr);
void latxs_tb_reset(TranslationBlock *tb);
int latx_setjmp_gen_code(CPUState *cpu,
        TranslationBlock *tb, vaddr pc, void *host_pc,
        int max_insns, int *search_size);

void page_init(void);
void tb_htable_init(void);
void tb_reset_jump(TranslationBlock *tb, int n);
void unlink_old_dest(TranslationBlock *tb, TranslationBlock *dest, int n_orig);
TranslationBlock *tb_link_page(TranslationBlock *tb, bool keep_private);
TranslationBlock *aot_tb_link_page(TranslationBlock *tb, bool keep_hras_private);
#ifdef CONFIG_LATX_TU
TranslationBlock *aot_tb_replace_page(TranslationBlock *tb,
                                     TranslationBlock *expected);
#endif
TranslationBlock *aot_tb_relink_page(TranslationBlock *tb);
bool tb_invalidate_phys_page_unwind(tb_page_addr_t addr, uintptr_t pc);
void cpu_restore_state_from_tb(CPUState *cpu, TranslationBlock *tb,
                               uintptr_t host_pc);

/* Return the current PC from CPU, which may be cached in TB. */
static inline vaddr log_pc(CPUState *cpu, const TranslationBlock *tb)
{
    if (tb_cflags(tb) & CF_PCREL) {
        return cpu->cc->get_pc(cpu);
    } else {
        return tb->pc;
    }
}

/*
 * Return true if CS is not running in parallel with other cpus, either
 * because there are no other cpus or we are within an exclusive context.
 */
static inline bool cpu_in_serial_context(CPUState *cs)
{
    return !(cs->tcg_cflags & CF_PARALLEL) || cpu_in_exclusive_context(cs);
}

extern int64_t max_delay;
extern int64_t max_advance;

extern bool one_insn_per_tb;

/**
 * tcg_req_mo:
 * @type: TCGBar
 *
 * Filter @type to the barrier that is required for the guest
 * memory ordering vs the host memory ordering.  A non-zero
 * result indicates that some barrier is required.
 *
 * If TCG_GUEST_DEFAULT_MO is not defined, assume that the
 * guest requires strict ordering.
 *
 * This is a macro so that it's constant even without optimization.
 */
#ifdef TCG_GUEST_DEFAULT_MO
# define tcg_req_mo(type) \
    ((type) & TCG_GUEST_DEFAULT_MO & ~TCG_TARGET_DEFAULT_MO)
#else
# define tcg_req_mo(type) ((type) & ~TCG_TARGET_DEFAULT_MO)
#endif

/**
 * cpu_req_mo:
 * @type: TCGBar
 *
 * If tcg_req_mo indicates a barrier for @type is required
 * for the guest memory model, issue a host memory barrier.
 */
#ifdef CONFIG_LATX_OPT
#define cpu_req_mo(type)          \
    do {                          \
    } while (0)
#else
#define cpu_req_mo(type)          \
    do {                          \
        if (tcg_req_mo(type)) {   \
            smp_mb();             \
        }                         \
    } while (0)
#endif

uint64_t get_page_aot_mesage(tb_page_addr_t addr);
void set_page_aot_mesage(tb_page_addr_t addr, uint8_t flags, vaddr gva);

typedef struct AOTTUPageCache {
    uint64_t file_offset;
    uint32_t file_generation;
} AOTTUPageCache;

/* These helpers must be called while the target PageDesc is locked. */
bool get_page_aot_tu_cache(tb_page_addr_t addr, AOTTUPageCache *cache);
void set_page_aot_tu_cache(tb_page_addr_t addr, uint32_t file_generation,
        uint64_t file_offset);

#endif /* ACCEL_TCG_INTERNAL_H */
