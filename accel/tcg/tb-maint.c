/*
 * Translation Block Maintaince
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
#include "qemu/rcu.h"
#include "qemu/interval-tree.h"
#include "qemu/qtree.h"
#include "exec/cputlb.h"
#include "exec/log.h"
#include "exec/exec-all.h"
#include "exec/tb-flush.h"
#include "exec/translate-all.h"
#include "sysemu/tcg.h"
#include "tcg/tcg.h"
#include "tb-hash.h"
#include "tb-context.h"
#include "internal.h"
#include "latx-intb-sys.h"
#include "hamt-tlb.h"
#ifndef CONFIG_USER_ONLY
#include "hamt.h"
#endif
#include "latx/include/latx-ras.h"
#include "latx/include/latx-config.h"
#include "latx/include/latx-options.h"
#include "latx/include/latx-shadow-htable.h"
#include "latx/include/latx-misc.h"
#include "latx/include/sys-aot.h"
#include "latx/include/latx-tu.h"

/* List iterators for lists of tagged pointers in TranslationBlock. */
#define TB_FOR_EACH_TAGGED(head, tb, n, field)                          \
    for (n = (head) & 1, tb = (TranslationBlock *)((head) & ~1);        \
         tb; tb = (TranslationBlock *)tb->field[n], n = (uintptr_t)tb & 1, \
             tb = (TranslationBlock *)((uintptr_t)tb & ~1))

#define TB_FOR_EACH_JMP(head_tb, tb, n)                                 \
    TB_FOR_EACH_TAGGED((head_tb)->jmp_list_head, tb, n, jmp_list_next)

static bool tb_cmp(const void *ap, const void *bp)
{
    const TranslationBlock *a = ap;
    const TranslationBlock *b = bp;

    return ((tb_cflags(a) & CF_PCREL ||a->pc == b->pc) &&
            a->cs_base == b->cs_base &&
            a->flags == b->flags &&
            (tb_cflags(a) & ~CF_INVALID) == (tb_cflags(b) & ~CF_INVALID) &&
            tb_page_addr0(a) == tb_page_addr0(b) &&
            tb_page_addr1(a) == tb_page_addr1(b));
}

static size_t tb_htable_initial_capacity(void)
{
    unsigned bits = option_tb_htable_bits ? option_tb_htable_bits :
                                          CODE_GEN_HTABLE_BITS;
    return (size_t)1 << bits;
}

void *(*tb_qht_lookup)(const struct qht *, const void *, uint32_t,
                       qht_lookup_func_t) = qht_lookup_custom;

void tb_htable_init(void)
{
    unsigned int mode = QHT_MODE_AUTO_RESIZE;
    tb_qht_lookup = option_arch_opt ? qht_lookup_custom_unrolled :
                                     qht_lookup_custom;

    tb_ctx.n = qemu_tcg_mtcc_enabled() ? MTCC_NR : 1;

    for (int i = 0; i < tb_ctx.n; ++i) {
        qht_init(&tb_ctx.htable[i], tb_cmp, tb_htable_initial_capacity(), mode);
    }
    if (option_tb_htable_bits) {
        fprintf(stderr, "LATS_TB_HTABLE_INITIAL bits=%u capacity=%zu "
                "tables=%d buckets=%zu auto_resize=1\n",
                option_tb_htable_bits, tb_htable_initial_capacity(), tb_ctx.n,
                qht_n_buckets(&tb_ctx.htable[0]));
    }

    flaged_shadow_htable_init();
}

typedef struct PageDesc PageDesc;

#ifdef CONFIG_USER_ONLY

/*
 * In user-mode page locks aren't used; mmap_lock is enough.
 */
#define assert_page_locked(pd) tcg_debug_assert(have_mmap_lock())

static inline void tb_lock_pages(const TranslationBlock *tb) { }

/*
 * For user-only, since we are protecting all of memory with a single lock,
 * and because the two pages of a TranslationBlock are always contiguous,
 * use a single data structure to record all TranslationBlocks.
 */
static IntervalTreeRoot tb_root;

static void tb_remove_all(void)
{
    assert_memory_lock();
    memset(&tb_root, 0, sizeof(tb_root));
}

/* Call with mmap_lock held. */
static void tb_record(TranslationBlock *tb)
{
    vaddr addr;
    int flags;

    assert_memory_lock();
    tb->itree.last = tb->itree.start + tb->size - 1;

    /* translator_loop() must have made all TB pages non-writable */
    addr = tb_page_addr0(tb);
    flags = page_get_flags(addr);
    assert(!(flags & PAGE_WRITE));

    addr = tb_page_addr1(tb);
    if (addr != -1) {
        flags = page_get_flags(addr);
        assert(!(flags & PAGE_WRITE));
    }

    interval_tree_insert(&tb->itree, &tb_root);
}

/* Call with mmap_lock held. */
static void tb_remove(TranslationBlock *tb)
{
    assert_memory_lock();
    interval_tree_remove(&tb->itree, &tb_root);
}

/* TODO: For now, still shared with translate-all.c for system mode. */
#define PAGE_FOR_EACH_TB(start, last, pagedesc, T, N)   \
    for (T = foreach_tb_first(start, last),             \
         N = foreach_tb_next(T, start, last);           \
         T != NULL;                                     \
         T = N, N = foreach_tb_next(N, start, last))
#define PAGE_FOR_EACH_TB_MTCC(start, last, pagedesc, tb, n, mtcc_id) \
    PAGE_FOR_EACH_TB(start, last, pagedesc, tb, n)

typedef TranslationBlock *PageForEachNext;

static PageForEachNext foreach_tb_first(tb_page_addr_t start,
                                        tb_page_addr_t last)
{
    IntervalTreeNode *n = interval_tree_iter_first(&tb_root, start, last);
    return n ? container_of(n, TranslationBlock, itree) : NULL;
}

static PageForEachNext foreach_tb_next(PageForEachNext tb,
                                       tb_page_addr_t start,
                                       tb_page_addr_t last)
{
    IntervalTreeNode *n;

    if (tb) {
        n = interval_tree_iter_next(&tb->itree, start, last);
        if (n) {
            return container_of(n, TranslationBlock, itree);
        }
    }
    return NULL;
}

#else
/*
 * In system mode we want L1_MAP to be based on ram offsets.
 */
#if HOST_LONG_BITS < TARGET_PHYS_ADDR_SPACE_BITS
# define L1_MAP_ADDR_SPACE_BITS  HOST_LONG_BITS
#else
# define L1_MAP_ADDR_SPACE_BITS  TARGET_PHYS_ADDR_SPACE_BITS
#endif

/* Size of the L2 (and L3, etc) page tables.  */
#define V_L2_BITS 10
#define V_L2_SIZE (1 << V_L2_BITS)

/*
 * L1 Mapping properties
 */
static int v_l1_size;
static int v_l1_shift;
static int v_l2_levels;

/*
 * The bottom level has pointers to PageDesc, and is indexed by
 * anything from 4 to (V_L2_BITS + 3) bits, depending on target page size.
 */
#define V_L1_MIN_BITS 4
#define V_L1_MAX_BITS (V_L2_BITS + 3)
#define V_L1_MAX_SIZE (1 << V_L1_MAX_BITS)

static void *l1_map[V_L1_MAX_SIZE];

#ifdef CONFIG_LATX_TU
/* Source-only pages need not contain any original TB byte range. */
typedef struct NativeSourceOwner {
    TranslationBlock *owner;
    struct NativeSourceOwner *next;
} NativeSourceOwner;
#endif

struct PageDesc {
    QemuSpin lock;
    /* list of TBs intersecting this ram page */
    uintptr_t first_tb[MTCC_NR];
#ifdef CONFIG_LATX_TU
    NativeSourceOwner *native_source_owners[MTCC_NR];
#endif
    /*record pre gva and aot flags. */
    uint64_t aot_mesage;
    /* AOT TU page lookup cache; generation zero marks an invalid entry. */
    uint64_t aot_tu_file_offset;
    uint32_t aot_tu_cache_generation;
};

void page_table_config_init(void)
{
    uint32_t v_l1_bits;

    assert(TARGET_PAGE_BITS);
    /* The bits remaining after N lower levels of page tables.  */
    v_l1_bits = (L1_MAP_ADDR_SPACE_BITS - TARGET_PAGE_BITS) % V_L2_BITS;
    if (v_l1_bits < V_L1_MIN_BITS) {
        v_l1_bits += V_L2_BITS;
    }

    v_l1_size = 1 << v_l1_bits;
    v_l1_shift = L1_MAP_ADDR_SPACE_BITS - TARGET_PAGE_BITS - v_l1_bits;
    v_l2_levels = v_l1_shift / V_L2_BITS - 1;

    assert(v_l1_bits <= V_L1_MAX_BITS);
    assert(v_l1_shift % V_L2_BITS == 0);
    assert(v_l2_levels >= 0);
}

static PageDesc *page_find_alloc(tb_page_addr_t index, bool alloc)
{
    PageDesc *pd;
    void **lp;
    int i;

    /* Level 1.  Always allocated.  */
    lp = l1_map + ((index >> v_l1_shift) & (v_l1_size - 1));

    /* Level 2..N-1.  */
    for (i = v_l2_levels; i > 0; i--) {
        void **p = qatomic_rcu_read(lp);

        if (p == NULL) {
            void *existing;

            if (!alloc) {
                return NULL;
            }
            p = g_new0(void *, V_L2_SIZE);
            existing = qatomic_cmpxchg(lp, NULL, p);
            if (unlikely(existing)) {
                g_free(p);
                p = existing;
            }
        }

        lp = p + ((index >> (i * V_L2_BITS)) & (V_L2_SIZE - 1));
    }

    pd = qatomic_rcu_read(lp);
    if (pd == NULL) {
        void *existing;

        if (!alloc) {
            return NULL;
        }

        pd = g_new0(PageDesc, V_L2_SIZE);
        for (int i = 0; i < V_L2_SIZE; i++) {
            qemu_spin_init(&pd[i].lock);
            pd[i].aot_mesage = PAGE_NO_LOAD;
        }

        existing = qatomic_cmpxchg(lp, NULL, pd);
        if (unlikely(existing)) {
            for (int i = 0; i < V_L2_SIZE; i++) {
                qemu_spin_destroy(&pd[i].lock);
            }
            g_free(pd);
            pd = existing;
        }
    }

    return pd + (index & (V_L2_SIZE - 1));
}

static inline PageDesc *page_find(tb_page_addr_t index)
{
    return page_find_alloc(index, false);
}

uint64_t get_page_aot_mesage(tb_page_addr_t addr)
{
    PageDesc *pd = page_find(addr);
    if (pd) {
        return pd->aot_mesage;
    }
    return 0;

}

/* Need hold lock when call this function. */
void set_page_aot_mesage(tb_page_addr_t addr, uint8_t flags, vaddr gva)
{
    PageDesc *pd = page_find_alloc(addr, true);
    gva &= TARGET_PAGE_MASK;
    pd->aot_mesage = gva | flags;
}

bool get_page_aot_tu_cache(tb_page_addr_t addr, AOTTUPageCache *cache)
{
    PageDesc *pd = page_find(addr);

    if (!pd) {
        memset(cache, 0, sizeof(*cache));
        return false;
    }

    cache->file_offset = pd->aot_tu_file_offset;
    cache->file_generation = pd->aot_tu_cache_generation;
    return cache->file_generation != 0;
}

void set_page_aot_tu_cache(tb_page_addr_t addr, uint32_t file_generation,
        uint64_t file_offset)
{
    PageDesc *pd = page_find_alloc(addr, true);

    g_assert(file_generation != 0);
    pd->aot_tu_file_offset = file_offset;
    pd->aot_tu_cache_generation = file_generation;
}

/**
 * struct page_entry - page descriptor entry
 * @pd:     pointer to the &struct PageDesc of the page this entry represents
 * @index:  page index of the page
 * @locked: whether the page is locked
 *
 * This struct helps us keep track of the locked state of a page, without
 * bloating &struct PageDesc.
 *
 * A page lock protects accesses to all fields of &struct PageDesc.
 *
 * See also: &struct page_collection.
 */
struct page_entry {
    PageDesc *pd;
    tb_page_addr_t index;
    bool locked;
};

/**
 * struct page_collection - tracks a set of pages (i.e. &struct page_entry's)
 * @tree:   Binary search tree (BST) of the pages, with key == page index
 * @max:    Pointer to the page in @tree with the highest page index
 *
 * To avoid deadlock we lock pages in ascending order of page index.
 * When operating on a set of pages, we need to keep track of them so that
 * we can lock them in order and also unlock them later. For this we collect
 * pages (i.e. &struct page_entry's) in a binary search @tree. Given that the
 * @tree implementation we use does not provide an O(1) operation to obtain the
 * highest-ranked element, we use @max to keep track of the inserted page
 * with the highest index. This is valuable because if a page is not in
 * the tree and its index is higher than @max's, then we can lock it
 * without breaking the locking order rule.
 *
 * Note on naming: 'struct page_set' would be shorter, but we already have a few
 * page_set_*() helpers, so page_collection is used instead to avoid confusion.
 *
 * See also: page_collection_lock().
 */
struct page_collection {
    QTree *tree;
    struct page_entry *max;
};

typedef int PageForEachNext;
#define PAGE_FOR_EACH_TB(start, last, pagedesc, tb, n) \
    TB_FOR_EACH_TAGGED((pagedesc)->first_tb[0], tb, n, page_next)
#define PAGE_FOR_EACH_TB_MTCC(start, last, pagedesc, tb, n, mtcc_id) \
    TB_FOR_EACH_TAGGED((pagedesc)->first_tb[mtcc_id], tb, n, page_next)

#ifdef CONFIG_DEBUG_TCG

static __thread GHashTable *ht_pages_locked_debug;

static void ht_pages_locked_debug_init(void)
{
    if (ht_pages_locked_debug) {
        return;
    }
    ht_pages_locked_debug = g_hash_table_new(NULL, NULL);
}

static bool page_is_locked(const PageDesc *pd)
{
    PageDesc *found;

    ht_pages_locked_debug_init();
    found = g_hash_table_lookup(ht_pages_locked_debug, pd);
    return !!found;
}

static void page_lock__debug(PageDesc *pd)
{
    ht_pages_locked_debug_init();
    g_assert(!page_is_locked(pd));
    g_hash_table_insert(ht_pages_locked_debug, pd, pd);
}

static void page_unlock__debug(const PageDesc *pd)
{
    bool removed;

    ht_pages_locked_debug_init();
    g_assert(page_is_locked(pd));
    removed = g_hash_table_remove(ht_pages_locked_debug, pd);
    g_assert(removed);
}

static void do_assert_page_locked(const PageDesc *pd,
                                  const char *file, int line)
{
    if (unlikely(!page_is_locked(pd))) {
        error_report("assert_page_lock: PageDesc %p not locked @ %s:%d",
                     pd, file, line);
        abort();
    }
}
#define assert_page_locked(pd) do_assert_page_locked(pd, __FILE__, __LINE__)

void assert_no_pages_locked(void)
{
    ht_pages_locked_debug_init();
    g_assert(g_hash_table_size(ht_pages_locked_debug) == 0);
}

#else /* !CONFIG_DEBUG_TCG */

static inline void page_lock__debug(const PageDesc *pd) { }
static inline void page_unlock__debug(const PageDesc *pd) { }
static inline void assert_page_locked(const PageDesc *pd) { }

#endif /* CONFIG_DEBUG_TCG */

static void page_lock(PageDesc *pd)
{
    page_lock__debug(pd);
    qemu_spin_lock(&pd->lock);
}

/* Like qemu_spin_trylock, returns false on success */
static bool page_trylock(PageDesc *pd)
{
    bool busy = qemu_spin_trylock(&pd->lock);
    if (!busy) {
        page_lock__debug(pd);
    }
    return busy;
}

static void page_unlock(PageDesc *pd)
{
    qemu_spin_unlock(&pd->lock);
    page_unlock__debug(pd);
}

static bool page_locked(PageDesc *pd)
{
    return pd ? qemu_spin_locked(&pd->lock) : false;
}

#ifdef CONFIG_LATX_TU
/* Page locks protect lists; region flush clears heads before memory reuse. */
static void native_source_prune(PageDesc *pd, unsigned mtcc_id)
{
    NativeSourceOwner **link = &pd->native_source_owners[mtcc_id];

    assert_page_locked(pd);
    while (*link) {
        if (tb_cflags((*link)->owner) & CF_INVALID) {
            *link = (*link)->next;
        } else {
            link = &(*link)->next;
        }
    }
}

bool tb_register_native_source_pages(TranslationBlock *owner)
{
    TranslationUnitPages *pages = TB_TU_PAGES(owner);
    tb_page_addr_t physical[TRANSLATION_UNIT_MAX_PAGES];
    PageDesc *descs[TRANSLATION_UNIT_MAX_PAGES];
    unsigned count = 0;
    unsigned mtcc_id = get_mask_from_miscflags(owner,
            MF_MTCC_ID_MASK, MF_MTCC_ID_SHIFT);
    NativeSourceOwner *links;

    assert_memory_lock();
    if (!pages || !tb_is_tu(owner) || mtcc_id >= tb_ctx.n) {
        return false;
    }
    for (unsigned i = 0; ; i++) {
        uint64_t packed = pages->pages[i].vp_phys_hi;

        if (packed == UINT64_MAX) {
            break;
        }
        if (i == TRANSLATION_UNIT_MAX_PAGES) {
            return false;
        }
        tb_page_addr_t pp = tu_page_desc_phys(packed, pages->pages[i].mix);
        bool duplicate = false;
        for (unsigned j = 0; j < count; j++) {
            duplicate |= physical[j] == pp;
        }
        if (duplicate) {
            continue;
        }
        PageDesc *pd = page_find(pp >> TARGET_PAGE_BITS);
        if (!pd) {
            return false;
        }
        assert_page_locked(pd);
        physical[count] = pp;
        descs[count++] = pd;
    }
    if (!count) {
        return false;
    }
    /* Allocate before publishing any node; failure leaves no partial list. */
    links = tcg_region_alloc_data(tcg_ctx, count * sizeof(*links));
    if (!links) {
        return false;
    }
    for (unsigned i = 0; i < count; i++) {
        native_source_prune(descs[i], mtcc_id);
        links[i].owner = owner;
        links[i].next = descs[i]->native_source_owners[mtcc_id];
        descs[i]->native_source_owners[mtcc_id] = &links[i];
    }
    return true;
}
#endif

bool tb_page_locked(tb_page_addr_t paddr)
{
    return page_locked(page_find_alloc(paddr >> TARGET_PAGE_BITS, false));
}

bool tb_page_has_code_locked(tb_page_addr_t paddr)
{
    PageDesc *page = page_find_alloc(paddr >> TARGET_PAGE_BITS, false);

    assert_page_locked(page);
    for (unsigned i = 0; i < tb_ctx.n; i++) {
        if (page->first_tb[i]) {
            return true;
        }
    }
    return false;
}

void tb_lock_page0(tb_page_addr_t paddr)
{
    page_lock(page_find_alloc(paddr >> TARGET_PAGE_BITS, true));
}

bool tb_trylock_page0(tb_page_addr_t paddr)
{
    bool busy = page_trylock(page_find_alloc(paddr >> TARGET_PAGE_BITS, true));
    return busy;
}
void tb_unlock_page0(tb_page_addr_t paddr)
{
    page_unlock(page_find_alloc(paddr >> TARGET_PAGE_BITS, false));
}

#include "../../target/i386/latx/include/lsenv.h"
void tb_lock_page1(tb_page_addr_t paddr0, tb_page_addr_t paddr1)
{
    tb_page_addr_t pindex0 = paddr0 >> TARGET_PAGE_BITS;
    tb_page_addr_t pindex1 = paddr1 >> TARGET_PAGE_BITS;
    PageDesc *pd0, *pd1;

    if (pindex0 == pindex1) {
        /* Identical pages, and the first page is already locked. */
        return;
    }

    pd1 = page_find_alloc(pindex1, true);
    if (pindex0 < pindex1) {
        /* Correct locking order, we may block. */
        page_lock(pd1);
        return;
    }

    /* Incorrect locking order, we cannot block lest we deadlock. */
    if (!page_trylock(pd1)) {
        return;
    }

    /*
     * Drop the lock on page0 and get both page locks in the right order.
     * Restart translation via longjmp.
     */
    pd0 = page_find_alloc(pindex0, false);
    page_unlock(pd0);
    page_lock(pd1);
    page_lock(pd0);

    if (qemu_tcg_mode_2_lat()) {
        TRANSLATION_DATA *td = lsenv->tr_data;
        td->deadlock_avoidance = __GENCODE_ERR_PAGE_LOCK_RETRY;
    } else {
        siglongjmp(tcg_ctx->jmp_trans, -3);
    }
}

void tb_unlock_page1(tb_page_addr_t paddr0, tb_page_addr_t paddr1)
{
    tb_page_addr_t pindex0 = paddr0 >> TARGET_PAGE_BITS;
    tb_page_addr_t pindex1 = paddr1 >> TARGET_PAGE_BITS;

    if (pindex0 != pindex1) {
        page_unlock(page_find_alloc(pindex1, false));
    }
}

static void tb_lock_pages(TranslationBlock *tb)
{
    tb_page_addr_t paddr0 = tb_page_addr0(tb);
    tb_page_addr_t paddr1 = tb_page_addr1(tb);
    tb_page_addr_t pindex0 = paddr0 >> TARGET_PAGE_BITS;
    tb_page_addr_t pindex1 = paddr1 >> TARGET_PAGE_BITS;

    if (unlikely(paddr0 == -1)) {
        return;
    }
    if (unlikely(paddr1 != -1) && pindex0 != pindex1) {
        if (pindex0 < pindex1) {
            page_lock(page_find_alloc(pindex0, true));
            page_lock(page_find_alloc(pindex1, true));
            return;
        }
        page_lock(page_find_alloc(pindex1, true));
    }
    page_lock(page_find_alloc(pindex0, true));
}

void tb_unlock_pages(TranslationBlock *tb)
{
    tb_page_addr_t paddr0 = tb_page_addr0(tb);
    tb_page_addr_t paddr1 = tb_page_addr1(tb);
    tb_page_addr_t pindex0 = paddr0 >> TARGET_PAGE_BITS;
    tb_page_addr_t pindex1 = paddr1 >> TARGET_PAGE_BITS;

    if (unlikely(paddr0 == -1)) {
        return;
    }
    if (unlikely(paddr1 != -1) && pindex0 != pindex1) {
        page_unlock(page_find_alloc(pindex1, false));
    }
    page_unlock(page_find_alloc(pindex0, false));
}

void tb_unlock_tu_pages(void);

void tb_unlock_ctx_pages(int reset)
{
    if (lsenv->tr_data->is_tu_mode && cp_tu_pages()) {
        tb_unlock_tu_pages();
        if (reset) {
            tcg_ctx->pre_locked_page0 = -1;
            tcg_ctx->pre_locked_page1 = -1;
        }
        return;
    }
    if (tcg_ctx->pre_locked_page1 != -1) {
        if (tcg_ctx->pre_locked_page1 != tcg_ctx->pre_locked_page0) {
            tb_unlock_page0(tcg_ctx->pre_locked_page1);
        }
        if (reset) tcg_ctx->pre_locked_page1 = -1;
    }
    if (tcg_ctx->pre_locked_page0 != -1) {
        tb_unlock_page0(tcg_ctx->pre_locked_page0);
        if (reset) tcg_ctx->pre_locked_page0 = -1;
    }
}

void unlock_page(uint64 addr)
{
    addr >>= TARGET_PAGE_BITS;
    if (unlikely(addr == -1)) {
        return;
    }
    page_unlock(page_find_alloc(addr, false));
}

static inline struct page_entry *
page_entry_new(PageDesc *pd, tb_page_addr_t index)
{
    struct page_entry *pe = g_malloc(sizeof(*pe));

    pe->index = index;
    pe->pd = pd;
    pe->locked = false;
    return pe;
}

static void page_entry_destroy(gpointer p)
{
    struct page_entry *pe = p;

    g_assert(pe->locked);
    page_unlock(pe->pd);
    g_free(pe);
}

/* returns false on success */
static bool page_entry_trylock(struct page_entry *pe)
{
    bool busy = page_trylock(pe->pd);
    if (!busy) {
        g_assert(!pe->locked);
        pe->locked = true;
    }
    return busy;
}

static void do_page_entry_lock(struct page_entry *pe)
{
    page_lock(pe->pd);
    g_assert(!pe->locked);
    pe->locked = true;
}

static gboolean page_entry_lock(gpointer key, gpointer value, gpointer data)
{
    struct page_entry *pe = value;

    do_page_entry_lock(pe);
    return FALSE;
}

static gboolean page_entry_unlock(gpointer key, gpointer value, gpointer data)
{
    struct page_entry *pe = value;

    if (pe->locked) {
        pe->locked = false;
        page_unlock(pe->pd);
    }
    return FALSE;
}

/*
 * Trylock a page, and if successful, add the page to a collection.
 * Returns true ("busy") if the page could not be locked; false otherwise.
 */
static bool page_trylock_add(struct page_collection *set, tb_page_addr_t addr)
{
    tb_page_addr_t index = addr >> TARGET_PAGE_BITS;
    struct page_entry *pe;
    PageDesc *pd;

    pe = q_tree_lookup(set->tree, &index);
    if (pe) {
        return false;
    }

    pd = page_find(index);
    if (pd == NULL) {
        return false;
    }

    pe = page_entry_new(pd, index);
    q_tree_insert(set->tree, &pe->index, pe);

    /*
     * If this is either (1) the first insertion or (2) a page whose index
     * is higher than any other so far, just lock the page and move on.
     */
    if (set->max == NULL || pe->index > set->max->index) {
        set->max = pe;
        do_page_entry_lock(pe);
        return false;
    }
    /*
     * Try to acquire out-of-order lock; if busy, return busy so that we acquire
     * locks in order.
     */
    return page_entry_trylock(pe);
}

static gint tb_page_addr_cmp(gconstpointer ap, gconstpointer bp, gpointer udata)
{
    tb_page_addr_t a = *(const tb_page_addr_t *)ap;
    tb_page_addr_t b = *(const tb_page_addr_t *)bp;

    if (a == b) {
        return 0;
    } else if (a < b) {
        return -1;
    }
    return 1;
}

/*
 * Lock a range of pages ([@start,@last]) as well as the pages of all
 * intersecting TBs.
 * Locking order: acquire locks in ascending order of page index.
 */
static struct page_collection *page_collection_lock(tb_page_addr_t start,
                                                    tb_page_addr_t last)
{
    struct page_collection *set = g_malloc(sizeof(*set));
    tb_page_addr_t index;
    PageDesc *pd;

    start >>= TARGET_PAGE_BITS;
    last >>= TARGET_PAGE_BITS;
    g_assert(start <= last);

    set->tree = q_tree_new_full(tb_page_addr_cmp, NULL, NULL,
                                page_entry_destroy);
    set->max = NULL;
    assert_no_pages_locked();

 retry:
    q_tree_foreach(set->tree, page_entry_lock, NULL);

    for (index = start; index <= last; index++) {
        TranslationBlock *tb;
        PageForEachNext n;

        pd = page_find(index);
        if (pd == NULL) {
            continue;
        }
        if (page_trylock_add(set, index << TARGET_PAGE_BITS)) {
            q_tree_foreach(set->tree, page_entry_unlock, NULL);
            goto retry;
        }
        assert_page_locked(pd);
        uint32_t n_mtcc = qemu_tcg_mtcc_enabled()?MTCC_NR:1;
        for (uint32_t mtcc_id = 0; mtcc_id < n_mtcc; ++mtcc_id) {
#ifdef CONFIG_LATX_TU
        native_source_prune(pd, mtcc_id);
        for (NativeSourceOwner *link = pd->native_source_owners[mtcc_id];
             link; link = link->next) {
            TranslationUnitPages *sources = TB_TU_PAGES(link->owner);
            for (unsigned i = 0; i < TRANSLATION_UNIT_MAX_PAGES + 1; i++) {
                uint64_t packed = sources->pages[i].vp_phys_hi;
                if (packed == UINT64_MAX) {
                    break;
                }
                tb_page_addr_t pp = tu_page_desc_phys(packed,
                                                     sources->pages[i].mix);
                if (page_trylock_add(set, pp)) {
                    q_tree_foreach(set->tree, page_entry_unlock, NULL);
                    goto retry;
                }
            }
        }
#endif
        PAGE_FOR_EACH_TB_MTCC(unused, unused, pd, tb, n, mtcc_id) {
            bool retry_needed = page_trylock_add(set, tb_page_addr0(tb)) ||
                (tb_page_addr1(tb) != -1 &&
                 page_trylock_add(set, tb_page_addr1(tb)));
#ifdef CONFIG_LATX_TU
            if (!retry_needed && tb_is_tu(tb) && TB_TU_PAGES(tb)) {
                for (int i = 0; i < TRANSLATION_UNIT_MAX_PAGES + 1; i++) {
                    uint64_t packed_vp =
                        TB_TU_PAGES(tb)->pages[i].vp_phys_hi;

                    if (packed_vp == UINT64_MAX) {
                        break;
                    }
                    tb_page_addr_t pp = tu_page_desc_phys(packed_vp,
                            TB_TU_PAGES(tb)->pages[i].mix);
                    if (page_trylock_add(set, pp)) {
                        retry_needed = true;
                        break;
                    }
                }
            }
#endif
            if (retry_needed) {
                /* drop all locks, and reacquire in order */
                q_tree_foreach(set->tree, page_entry_unlock, NULL);
                goto retry;
            }
        }
        }
    }
    return set;
}

static void page_collection_unlock(struct page_collection *set)
{
    /* entries are unlocked and freed via page_entry_destroy */
    q_tree_destroy(set->tree);
    g_free(set);
}

/* Set to NULL all the 'first_tb' fields in all PageDescs. */
static void tb_remove_all_1(int level, void **lp)
{
    int i, r;

    if (*lp == NULL) {
        return;
    }
    if (level == 0) {
        PageDesc *pd = *lp;

        for (i = 0; i < V_L2_SIZE; ++i) {
            page_lock(&pd[i]);
            for (r = 0; r < MTCC_NR; ++r) {
                pd[i].first_tb[r] = (uintptr_t)NULL;
#ifdef CONFIG_LATX_TU
                pd[i].native_source_owners[r] = NULL;
#endif
                if (pd[i].aot_mesage & PAGE_LOADED) {
                    pd[i].aot_mesage = PAGE_NO_LOAD;
                }
            }
            page_unlock(&pd[i]);
        }
    } else {
        void **pp = *lp;

        for (i = 0; i < V_L2_SIZE; ++i) {
            tb_remove_all_1(level - 1, pp + i);
        }
    }
}

static void tb_remove_all(void)
{
    int i, l1_sz = v_l1_size;

    for (i = 0; i < l1_sz; i++) {
        tb_remove_all_1(v_l2_levels, l1_map + i);
    }
}

static void tb_remove_all_1_mtcc(int level, void **lp, uint32_t mtcc_id)
{
    int i;

    if (*lp == NULL) {
        return;
    }
    if (level == 0) {
        PageDesc *pd = *lp;

        for (i = 0; i < V_L2_SIZE; ++i) {
            page_lock(&pd[i]);
            pd[i].first_tb[mtcc_id] = (uintptr_t)NULL;
#ifdef CONFIG_LATX_TU
            pd[i].native_source_owners[mtcc_id] = NULL;
#endif
            if (pd[i].aot_mesage & PAGE_LOADED) {
                pd[i].aot_mesage = PAGE_NO_LOAD;
            }
            page_unlock(&pd[i]);
        }
    } else {
        void **pp = *lp;

        for (i = 0; i < V_L2_SIZE; ++i) {
            tb_remove_all_1_mtcc(level - 1, pp + i, mtcc_id);
        }
    }
}

static void tb_remove_mtcc(uint32_t mtcc_id)
{
    assert(qemu_tcg_mtcc_enabled());

    int i, l1_sz = v_l1_size;

    for (i = 0; i < l1_sz; i++) {
        tb_remove_all_1_mtcc(v_l2_levels, l1_map + i, mtcc_id);
    }
}

static void unlock_all_page_1_mtcc(int level, void **lp, uint32_t mtcc_id)
{
    int i;

    if (*lp == NULL) {
        return;
    }
    if (level == 0) {
        PageDesc *pd = *lp;

        for (i = 0; i < V_L2_SIZE; ++i) {
            page_unlock(&pd[i]);
        }
    } else {
        void **pp = *lp;

        for (i = 0; i < V_L2_SIZE; ++i) {
            unlock_all_page_1_mtcc(level - 1, pp + i, mtcc_id);
        }
    }
}

void unlock_all_page(uint32_t mtcc_id)
{
    assert(qemu_tcg_mtcc_enabled());

    int i, l1_sz = v_l1_size;

    for (i = 0; i < l1_sz; i++) {
        unlock_all_page_1_mtcc(v_l2_levels, l1_map + i, mtcc_id);
    }
}

/*
 * Add the tb in the target page and protect it if necessary.
 * Called with @p->lock held.
 */
static void tb_page_add(PageDesc *p, TranslationBlock *tb, unsigned int n)
{
    bool page_already_protected;

    assert_page_locked(p);

    uint32_t mtcc_id = get_mask_from_miscflags(tb,
            MF_MTCC_ID_MASK, MF_MTCC_ID_SHIFT);

    tb->page_next[n] = p->first_tb[mtcc_id];
    page_already_protected = p->first_tb[mtcc_id] != 0;
    p->first_tb[mtcc_id] = (uintptr_t)tb | n;

    /*
     * If some code is already present, then the pages are already
     * protected. So we handle the case where only the first TB is
     * allocated in a physical page.
     */
    if (!page_already_protected) {
        tlb_protect_code(tb->page_addr[n] & TARGET_PAGE_MASK);
    }
}

static void tb_record(TranslationBlock *tb)
{
    tb_page_addr_t paddr0 = tb_page_addr0(tb);
    tb_page_addr_t paddr1 = tb_page_addr1(tb);
    tb_page_addr_t pindex0 = paddr0 >> TARGET_PAGE_BITS;
    tb_page_addr_t pindex1 = paddr1 >> TARGET_PAGE_BITS;

    assert(paddr0 != -1);
    if (unlikely(paddr1 != -1) && pindex0 != pindex1) {
        tb_page_add(page_find_alloc(pindex1, false), tb, 1);
    }
    tb_page_add(page_find_alloc(pindex0, false), tb, 0);
}

static void tb_page_remove(PageDesc *pd, TranslationBlock *tb)
{
    TranslationBlock *tb1;
    uintptr_t *pprev;
    PageForEachNext n1;

    uint32_t mtcc_id = get_mask_from_miscflags(tb,
            MF_MTCC_ID_MASK, MF_MTCC_ID_SHIFT);

    assert_page_locked(pd);
    pprev = &pd->first_tb[mtcc_id];
    PAGE_FOR_EACH_TB_MTCC(unused, unused, pd, tb1, n1, mtcc_id) {
        if (tb1 == tb) {
            *pprev = tb1->page_next[n1];
            return;
        }
        pprev = &tb1->page_next[n1];
    }
    g_assert_not_reached();
}

static void tb_remove(TranslationBlock *tb)
{
    tb_page_addr_t paddr0 = tb_page_addr0(tb);
    tb_page_addr_t paddr1 = tb_page_addr1(tb);
    tb_page_addr_t pindex0 = paddr0 >> TARGET_PAGE_BITS;
    tb_page_addr_t pindex1 = paddr1 >> TARGET_PAGE_BITS;

    assert(paddr0 != -1);
    if (unlikely(paddr1 != -1) && pindex0 != pindex1) {
        tb_page_remove(page_find_alloc(pindex1, false), tb);
    }
    tb_page_remove(page_find_alloc(pindex0, false), tb);
}
#endif /* CONFIG_USER_ONLY */

/* flush all the translation blocks */
static void do_tb_flush(CPUState *cpu, run_on_cpu_data tb_flush_count)
{
    bool did_flush = false;

    mmap_lock();
    /* If it is already been done on request of another CPU, just retry. */
    if (tb_ctx.tb_flush_count != tb_flush_count.host_int) {
        goto done;
    }
    did_flush = true;

    CPU_FOREACH(cpu) {
        if (option_rastoptp) latxs_flush_soft_ras(cpu);
        tcg_flush_jmp_cache(cpu);
        cpu->env_ptr->syscall_tb = NULL;
#ifdef CONFIG_LATX_TU
        tu_generation_invalidate(cpu);
#endif
    }

    for (int i = 0; i < tb_ctx.n; ++i) {
        qht_reset_size(&tb_ctx.htable[i], tb_htable_initial_capacity());
    }
    flaged_shadow_htable_reset();
    tb_remove_all();

#if defined(CONFIG_SOFTMMU) && defined(CONFIG_LATX_HACK_MEM) && defined(NDEBUG)
    latx_supervisor_dispatch_flush();
#endif
    tcg_region_reset_all();
    aot_reloc_reset_on_tb_flush(-1);
#ifdef CONFIG_LATX_TU
    tu_generation_reset_ids();
#endif
    /* XXX: flush processor icache at this point if cache flush is expensive */
    qatomic_inc(&tb_ctx.tb_flush_count);

done:
    mmap_unlock();
    if (did_flush) {
        qemu_plugin_flush_cb();
    }
}

static void do_tb_flush_mtcc(CPUState *cpu, run_on_cpu_data data)
{
    uint32_t count = data.host_ulong & (uint64_t)0xffffffff;
    uint32_t mtcc_id = (data.host_ulong >> 32) & (uint64_t)0xffffffff;

    aot_save_on_tb_flush(mtcc_id);
    bool did_flush = false;

    mmap_lock();
    /* If it is already been done on request of another CPU, just retry. */
    if (tb_ctx.tb_flush_count_mtcc[mtcc_id] != count) {
        goto done;
    }
    did_flush = true;

#ifdef CONFIG_LATX_PRINT
    printf("%s tb flush mtcc %d count %d\n", __func__,
            mtcc_id, count);
#endif

    CPU_FOREACH(cpu) {
        if (option_rastoptp && cpu->env_ptr->ras_tp.initialized &&
            cpu->env_ptr->ras_tp.mtcc_id == mtcc_id) {
            latxs_flush_soft_ras(cpu);
        }
        tcg_flush_jmp_cache(cpu);
        cpu->env_ptr->syscall_tb = NULL;
#ifdef CONFIG_LATX_TU
        /* TU generation is restricted to CPL3, which uses MTCC 1. */
        if (mtcc_id == 1) {
            tu_generation_invalidate(cpu);
        }
#endif
    }

    qht_reset_size(&tb_ctx.htable[mtcc_id], tb_htable_initial_capacity());
    flaged_shadow_htable_reset_mtcc(mtcc_id);
#if !defined(CONFIG_USER_ONLY)
    tb_remove_mtcc(mtcc_id);
#endif

#if defined(CONFIG_SOFTMMU) && defined(CONFIG_LATX_HACK_MEM) && defined(NDEBUG)
    if (mtcc_id == 0) {
        latx_supervisor_dispatch_flush();
    }
#endif
    tcg_region_reset_mtcc(mtcc_id);
    aot_reloc_reset_on_tb_flush(mtcc_id);
#ifdef CONFIG_LATX_TU
    if (mtcc_id == 1) {
        tu_generation_reset_ids();
    }
#endif
    /* XXX: flush processor icache at this point if cache flush is expensive */
    qatomic_inc(&tb_ctx.tb_flush_count_mtcc[mtcc_id]);

done:
    mmap_unlock();
    if (did_flush) {
        qemu_plugin_flush_cb();
    }
}

void tb_flush(CPUState *cpu)
{
    assert(qemu_tcg_flush_full());
    if (tcg_enabled()) {
        unsigned tb_flush_count = qatomic_read(&tb_ctx.tb_flush_count);

        if (cpu_in_serial_context(cpu)) {
            do_tb_flush(cpu, RUN_ON_CPU_HOST_INT(tb_flush_count));
        } else {
            async_safe_run_on_cpu(cpu, do_tb_flush,
                                  RUN_ON_CPU_HOST_INT(tb_flush_count));
        }
    }
}

void tb_flush_mtcc(CPUState *cpu, uint32_t mtcc_id)
{
    assert(qemu_tcg_flush_full());
    if (qemu_tcg_mtcc_enabled() && tcg_enabled()) {
        assert(mtcc_id < tb_ctx.n);
        uint32_t count = qatomic_read(&tb_ctx.tb_flush_count_mtcc[mtcc_id]);

        uint64_t data = ((uint64_t)mtcc_id << 32) | count;
        if (cpu_in_serial_context(cpu) &&
            (!option_sys_aot || !option_aot_save ||
             get_ptr_rcu_reader()->depth == 0)) {
            do_tb_flush_mtcc(cpu, RUN_ON_CPU_HOST_ULONG(data));
        } else {
            async_safe_run_on_cpu(cpu, do_tb_flush_mtcc,
                                  RUN_ON_CPU_HOST_ULONG(data));
        }
    } else {
        tb_flush(cpu);
    }
}

/* remove @orig from its @n_orig-th jump list */
static inline void tb_remove_from_jmp_list(TranslationBlock *orig, int n_orig)
{
    uintptr_t ptr, ptr_locked;
    TranslationBlock *dest;
    TranslationBlock *tb;
    uintptr_t *pprev;
    int n;

    /* mark the LSB of jmp_dest[] so that no further jumps can be inserted */
    ptr = qatomic_or_fetch(&orig->jmp_dest[n_orig], 1);
    dest = (TranslationBlock *)(ptr & ~1);
    if (dest == NULL) {
        return;
    }

    qemu_spin_lock(&dest->jmp_lock);
    /*
     * While acquiring the lock, the jump might have been removed if the
     * destination TB was invalidated; check again.
     */
    ptr_locked = qatomic_read(&orig->jmp_dest[n_orig]);
    if (ptr_locked != ptr) {
        qemu_spin_unlock(&dest->jmp_lock);
        /*
         * The only possibility is that the jump was unlinked via
         * tb_jump_unlink(dest). Seeing here another destination would be a bug,
         * because we set the LSB above.
         */
        g_assert(ptr_locked == 1 && dest->cflags & CF_INVALID);
        return;
    }
    /*
     * We first acquired the lock, and since the destination pointer matches,
     * we know for sure that @orig is in the jmp list.
     */
    pprev = &dest->jmp_list_head;
    TB_FOR_EACH_JMP(dest, tb, n) {
        if (tb == orig && n == n_orig) {
            *pprev = tb->jmp_list_next[n];
            /* no need to set orig->jmp_dest[n]; setting the LSB was enough */
            qemu_spin_unlock(&dest->jmp_lock);
            return;
        }
        pprev = &tb->jmp_list_next[n];
    }
    g_assert_not_reached();
}

void unlink_old_dest(TranslationBlock *tb, TranslationBlock *dest, int n_orig)
{
    TranslationBlock *last;
    uintptr_t *pprev;
    int n;


    qemu_spin_lock(&dest->jmp_lock);
    if ((uintptr_t)dest != qatomic_read(&tb->jmp_dest[n_orig])) {
        qemu_spin_unlock(&dest->jmp_lock);
        return;
    }
    pprev = &dest->jmp_list_head;
    TB_FOR_EACH_JMP(dest, last, n) {
        if (last == tb && n == n_orig) {
            *pprev = last->jmp_list_next[n];
            latx_tb_reset_jmp_target(tb, n);
            qatomic_set(&tb->jmp_dest[n_orig], (uintptr_t)NULL);
            qemu_spin_unlock(&dest->jmp_lock);
            return;
        }
        pprev = &last->jmp_list_next[n];
    }
    g_assert_not_reached();
}

/*
 * Reset the jump entry 'n' of a TB so that it is not chained to another TB.
 */
void tb_reset_jump(TranslationBlock *tb, int n)
{
    uintptr_t addr = (uintptr_t)(tb->tc.ptr + tb->jmp_reset_offset[n]);
    tb_set_jmp_target(tb, n, addr);
}

/* remove any jumps to the TB */
static inline void tb_jmp_unlink(TranslationBlock *dest)
{
    TranslationBlock *tb;
    int n;

    qemu_spin_lock(&dest->jmp_lock);

    TB_FOR_EACH_JMP(dest, tb, n) {
        if (qemu_tcg_mode_2_lat()) {
            latx_tb_reset_jmp_target(tb, n);
        } else {
            tb_reset_jump(tb, n);
        }
        qatomic_and(&tb->jmp_dest[n], (uintptr_t)NULL | 1);
        /* No need to clear the list entry; setting the dest ptr is enough */
    }
    dest->jmp_list_head = (uintptr_t)NULL;

    qemu_spin_unlock(&dest->jmp_lock);
}

void latxs_tb_jmp_unlink(TranslationBlock *dest);
void latxs_tb_jmp_unlink(TranslationBlock *dest)
{
    tb_jmp_unlink(dest);
}

static void tb_jmp_cache_inval_tb(TranslationBlock *tb)
{
    CPUState *cpu;
    /* Kernel MMIO discovery can invalidate a kernel TB after its region
     * was rebuilt. TP RAS never stores kernel targets. User aliases of the
     * same physical page still pass through this function separately. */
    bool flush_ras = option_soft_ras &&
        (!option_rastoptp || (tb->flags & HF_CPL_MASK) == HF_CPL_MASK);

    if (tb_cflags(tb) & CF_PCREL) {
        /* A TB may be at any virtual address */
        CPU_FOREACH(cpu) {
            if (option_rastoptp && flush_ras) latxs_flush_soft_ras(cpu);
            tcg_flush_jmp_cache(cpu);
        }
    } else {
        CPU_FOREACH(cpu) {
            if (flush_ras) {
                latxs_flush_soft_ras(cpu);
            }
            tb_jmp_cache_invalidate(cpu, tb->pc, tb);
        }
    }
}

/*
 * In user-mode, call with mmap_lock held.
 * In !user-mode, if @rm_from_page_list is set, call with the TB's pages'
 * locks held.
 */
static void do_tb_phys_invalidate(TranslationBlock *tb, bool rm_from_page_list)
{
    uint32_t h;
    tb_page_addr_t phys_pc;
    uint32_t orig_cflags = tb_cflags(tb);
    uint32_t mtcc_id = get_mask_from_miscflags(tb,
            MF_MTCC_ID_MASK, MF_MTCC_ID_SHIFT);

    assert_memory_lock();

    if (tb_is_hras_private(tb) && (orig_cflags & CF_INVALID)) {
        return;
    }

    /* make sure no further incoming jumps will be chained to this TB */
    qemu_spin_lock(&tb->jmp_lock);
    uint32_t cflags = (tb->cflags | CF_INVALID) & ~CF_NO_ACTIVE;
    qatomic_set(&tb->cflags, cflags);
#ifndef CONFIG_USER_ONLY
    /* Inactive AOT templates point into the file, not executable code. */
    if (!(orig_cflags & CF_NO_ACTIVE)) {
        hamt_poison_tb_entry(tb);
    }
#endif
    latxs_ras_unlink_target_locked(tb);
    qemu_spin_unlock(&tb->jmp_lock);

    /* CF_INVALID must be visible before either jump cache is cleared. */
    tb_jmp_cache_inval_tb(tb);

    /* Remove this TB's outgoing software-RAS edge as well. */
    latxs_ras_unlink(tb);

    /* remove the TB from the hash list */
    phys_pc = tb_page_addr0(tb);
    h = tb_hash_func(phys_pc, (orig_cflags & CF_PCREL ? 0 : tb->pc),
                     tb->flags, tb->cs_base, orig_cflags);
    /* Private return members own pages and edges, but no global hash key. */
    if (!tb_is_hras_private(tb)) {
        flaged_shadow_htable_remove(tb);
        if (!qht_remove(&tb_ctx.htable[mtcc_id], tb, h)) {
            return;
        }
    }

    /* remove the TB from the page list */
    if (rm_from_page_list) {
        tb_remove(tb);
    }

    /* suppress this TB from the two jump lists */
    tb_remove_from_jmp_list(tb, 0);
    tb_remove_from_jmp_list(tb, 1);

    /* suppress any remaining jumps to this TB */
    tb_jmp_unlink(tb);

    qatomic_set(&tb_ctx.tb_phys_invalidate_count,
                tb_ctx.tb_phys_invalidate_count + 1);
}

static void tb_phys_invalidate__locked(TranslationBlock *tb)
{
    qemu_thread_jit_write();
#ifdef CONFIG_LATX_TU
    if (tb->flags & HF_CPL_MASK) {
        if (tb_is_tu(tb)) {
            if (tb->s_data->next_tb[1]) {
                TranslationBlock *next_tb = tb->s_data->next_tb[1];
                while(next_tb) {
                    if (!next_tb->s_data->is_invalidate) {
                        do_tb_phys_invalidate(next_tb, true);
                        next_tb->s_data->is_invalidate = true;
                    }
                    next_tb = next_tb->s_data->next_tb[1];
                }
            }
            if (tb->s_data->next_tb[0]) {
                TranslationBlock *pre_tb = tb->s_data->next_tb[0];
                while(pre_tb) {
                    if (!pre_tb->s_data->is_invalidate) {
                        do_tb_phys_invalidate(pre_tb, true);
                        pre_tb->s_data->is_invalidate = true;
                    }
                    pre_tb = pre_tb->s_data->next_tb[0];
                }
            }
            do_tb_phys_invalidate(tb, true);
            tb->s_data->is_invalidate = true;
        } else {
            do_tb_phys_invalidate(tb, true);
        }
    } else {
        do_tb_phys_invalidate(tb, true);
    }
#else
    do_tb_phys_invalidate(tb, true);
#endif
    qemu_thread_jit_execute();
}

/*
 * Invalidate one TB.
 * Called with mmap_lock held in user-mode.
 */
void tb_phys_invalidate(TranslationBlock *tb, tb_page_addr_t page_addr)
{
    if (page_addr == -1 && tb_page_addr0(tb) != -1) {
        tb_lock_pages(tb);
        do_tb_phys_invalidate(tb, true);
        tb_unlock_pages(tb);
    } else {
        do_tb_phys_invalidate(tb, false);
    }
}

/*
 * Add a new TB and link it to the physical page tables.
 * Called with mmap_lock held for user-mode emulation.
 *
 * Returns a pointer @tb, or a pointer to an existing TB that matches @tb.
 * Note that in !user-mode, another thread might have already added a TB
 * for the same block of guest code that @tb corresponds to. In that case,
 * the caller should discard the original @tb, and use instead the returned TB.
 */
TranslationBlock *tb_link_page(TranslationBlock *tb)
{
    void *existing_tb = NULL;
    uint32_t h;

    assert_memory_lock();
    tcg_debug_assert(!(tb->cflags & CF_INVALID));

    tb_record(tb);

    /* add in the hash table */
    h = tb_hash_func(tb_page_addr0(tb), (tb->cflags & CF_PCREL ? 0 : tb->pc),
                     tb->flags, tb->cs_base, tb->cflags);

    uint32_t mtcc_id = 0;

re_insert:
    if (qemu_tcg_mtcc_enabled()) {
        mtcc_id = get_mask_from_miscflags(tb,
                MF_MTCC_ID_MASK, MF_MTCC_ID_SHIFT);
        assert(mtcc_id == curr_mtcc(current_cpu->env_ptr));
        qht_insert(&tb_ctx.htable[mtcc_id], tb, h, &existing_tb);
    } else {
        qht_insert(&tb_ctx.htable[0], tb, h, &existing_tb);
    }

    /* remove TB from the page(s) if we couldn't insert it */
    if (unlikely(existing_tb)) {
        /* Just a test for aot. */
        if (((TranslationBlock *)existing_tb)->cflags & CF_INVALID) {
            qht_remove(&tb_ctx.htable[mtcc_id], existing_tb, h);
            existing_tb = NULL;
            fprintf(stderr, "existing_tb is CF_INVALID\n");
            goto re_insert;
        }
        if (option_tu_hras && tb_is_tu(tb) &&
            !(tb->bool_flags & IS_AOT_TB)) {
            /* Keep the canonical lookup target and this TU's local code.
             * tb_record above must remain live for SMC and page unmapping. */
            tb->s_data->tu_tb_mode = TU_TB_MODE_HRAS_PRIVATE;
            return tb;
        }
        tb_remove(tb);
        return existing_tb;
    }

    flaged_shadow_htable_insert(tb);
    return tb;
}

TranslationBlock *aot_tb_link_page(TranslationBlock *tb, bool keep_hras_private)
{
    void *existing_tb = NULL;
    uint32_t h;

    assert_memory_lock();
    tcg_debug_assert(!(tb->cflags & CF_INVALID));

    tb_record(tb);

    /* add in the hash table */
    h = tb_hash_func(tb_page_addr0(tb), (tb->cflags & CF_PCREL ? 0 : tb->pc),
                     tb->flags, tb->cs_base, tb->cflags);

    if (qemu_tcg_mtcc_enabled()) {
        uint32_t mtcc_id = get_mask_from_miscflags(tb,
                MF_MTCC_ID_MASK, MF_MTCC_ID_SHIFT);
        assert(mtcc_id == curr_mtcc(current_cpu->env_ptr));
        qht_insert(&tb_ctx.htable[mtcc_id], tb, h, &existing_tb);
    } else {
        qht_insert(&tb_ctx.htable[0], tb, h, &existing_tb);
    }

    /* remove TB from the page(s) if we couldn't insert it */
    if (unlikely(existing_tb)) {
        if (keep_hras_private) {
            /* The proved paired ordinary TU owns this local continuation.
             * Keep its page/edge registration and the old canonical key. */
            g_assert(tb_is_tu(tb));
            qatomic_set(&tb->s_data->tu_tb_mode, TU_TB_MODE_HRAS_PRIVATE);
            return existing_tb;
        }
        if (((TranslationBlock *)existing_tb)->cflags & CF_INVALID) {
            fprintf(stderr, "aot_tb_link_page existing_tb is CF_INVALID bool flags %x\n",
                    ((TranslationBlock *)existing_tb)->bool_flags);
        }
        tb_remove(tb);
        /* tb_unlock_pages(tb); */
        return existing_tb;
    }

    flaged_shadow_htable_insert(tb);
    /* tb_unlock_pages(tb); */
    return tb;
}

#ifdef CONFIG_LATX_TU
/* The complete new TU is executable before the first member is published.
 * Both its source pages and every expected old owner's pages remain locked.
 * A collision leaves the new member private; it is never freed or invalidated
 * because a concurrent reader can already hold another member of this TU. */
TranslationBlock *aot_tb_replace_page(TranslationBlock *tb,
                                     TranslationBlock *expected)
{
    void *existing = NULL;
    unsigned mtcc = qemu_tcg_mtcc_enabled() ?
        get_mask_from_miscflags(tb, MF_MTCC_ID_MASK, MF_MTCC_ID_SHIFT) : 0;
    uint32_t h = tb_hash_func(tb_page_addr0(tb), tb->pc, tb->flags,
                              tb->cs_base, tb_cflags(tb));
    struct qht *table = &tb_ctx.htable[mtcc];

    assert_memory_lock();
    tb_record(tb);
    if (expected) {
        existing = qht_lookup(table, expected, h);
        if (existing != expected ||
            (tb_cflags(expected) & (CF_INVALID | CF_NO_ACTIVE)) ||
            (expected->bool_flags & IS_AOT_TB) ||
            !qht_remove(table, expected, h)) {
            qatomic_set(&tb->s_data->tu_tb_mode, TU_TB_MODE_HRAS_PRIVATE);
            return existing;
        }
        /* Keep every old edge, page-list entry and host-PC registration.
         * The store/load barrier pairs with the C cache insertion recheck. */
        qatomic_set(&expected->s_data->tu_tb_mode, TU_TB_MODE_HRAS_PRIVATE);
        smp_mb();
        flaged_shadow_htable_remove(expected);
        CPUState *cpu;
        CPU_FOREACH(cpu) {
            tb_jmp_cache_invalidate(cpu, expected->pc, expected);
        }
    }
    existing = NULL;
    if (qht_insert(table, tb, h, &existing)) {
        flaged_shadow_htable_insert(tb);
        return tb;
    }
    /* Never displace an object that failed the caller's exact preflight.
     * The full native owner/records and page registration remain valid, even
     * if other members were published before this unexpected collision. */
    qatomic_set(&tb->s_data->tu_tb_mode, TU_TB_MODE_HRAS_PRIVATE);
    return existing;
}
#endif

TranslationBlock *aot_tb_relink_page(TranslationBlock *tb)
{
    void *existing_tb = NULL;
    uint32_t h;
    uint32_t mtcc_id = get_mask_from_miscflags(tb,
            MF_MTCC_ID_MASK, MF_MTCC_ID_SHIFT);
    assert_memory_lock();
    /* Only an unexecuted file template may be activated. Executed entries
     * can be poisoned and retain their old identity until code flush. */
    if (!(tb_cflags(tb) & CF_NO_ACTIVE)) {
        return NULL;
    }
    /* tcg_debug_assert(!(tb->cflags & CF_INVALID)); */

    /* tb_record(tb); */

    /* add in the hash table */
    h = tb_hash_func(tb_page_addr0(tb), (tb->cflags & CF_PCREL ? 0 : tb->pc),
                     tb->flags, tb->cs_base, tb->cflags);
    qht_remove(&tb_ctx.htable[mtcc_id], tb, h);
    qatomic_set(&tb->cflags, tb->cflags & ~(CF_NO_ACTIVE | CF_INVALID));
    h = tb_hash_func(tb_page_addr0(tb), (tb->cflags & CF_PCREL ? 0 : tb->pc),
                     tb->flags, tb->cs_base, tb->cflags);

    if (qemu_tcg_mtcc_enabled()) {
        uint32_t mtcc_id = get_mask_from_miscflags(tb,
                MF_MTCC_ID_MASK, MF_MTCC_ID_SHIFT);
        assert(mtcc_id == curr_mtcc(current_cpu->env_ptr));
        qht_insert(&tb_ctx.htable[mtcc_id], tb, h, &existing_tb);
    } else {
        qht_insert(&tb_ctx.htable[0], tb, h, &existing_tb);
    }

    /* remove TB from the page(s) if we couldn't insert it */
    if (unlikely(existing_tb)) {
        if (((TranslationBlock *)existing_tb)->cflags & CF_INVALID) {
            fprintf(stderr, "aot_tb_link_page2 existing_tb is CF_INVALID bool flags %x\n",
                    ((TranslationBlock *)existing_tb)->bool_flags);
        }
        tb_remove(tb);
        return existing_tb;
    }

    flaged_shadow_htable_insert(tb);
    return tb;
}

#ifdef CONFIG_USER_ONLY
/*
 * Invalidate all TBs which intersect with the target address range.
 * Called with mmap_lock held for user-mode emulation.
 * NOTE: this function must not be called while a TB is running.
 */
void tb_invalidate_phys_range(tb_page_addr_t start, tb_page_addr_t last)
{
    TranslationBlock *tb;
    PageForEachNext n;

    assert_memory_lock();

    PAGE_FOR_EACH_TB(start, last, unused, tb, n) {
        tb_phys_invalidate__locked(tb);
    }
}

/*
 * Invalidate all TBs which intersect with the target address page @addr.
 * Called with mmap_lock held for user-mode emulation
 * NOTE: this function must not be called while a TB is running.
 */
void tb_invalidate_phys_page(tb_page_addr_t addr)
{
    tb_page_addr_t start, last;

    start = addr & TARGET_PAGE_MASK;
    last = addr | ~TARGET_PAGE_MASK;
    tb_invalidate_phys_range(start, last);
}

/*
 * Called with mmap_lock held. If pc is not 0 then it indicates the
 * host PC of the faulting store instruction that caused this invalidate.
 * Returns true if the caller needs to abort execution of the current
 * TB (because it was modified by this store and the guest CPU has
 * precise-SMC semantics).
 */
bool tb_invalidate_phys_page_unwind(tb_page_addr_t addr, uintptr_t pc)
{
    TranslationBlock *current_tb;
    bool current_tb_modified;
    TranslationBlock *tb;
    PageForEachNext n;
    tb_page_addr_t last;

    /*
     * Without precise smc semantics, or when outside of a TB,
     * we can skip to invalidate.
     */
#ifndef TARGET_HAS_PRECISE_SMC
    pc = 0;
#endif
    if (!pc) {
        tb_invalidate_phys_page(addr);
        return false;
    }

    assert_memory_lock();
    current_tb = tcg_tb_lookup(pc);

    last = addr | ~TARGET_PAGE_MASK;
    addr &= TARGET_PAGE_MASK;
    current_tb_modified = false;

    uint32_t n_mtcc = qemu_tcg_mtcc_enabled()?MTCC_NR:1;
    for (uint32_t mtcc_id = 0; mtcc_id < n_mtcc; ++mtcc_id) {
        PAGE_FOR_EACH_TB_MTCC(addr, last, unused, tb, n, mtcc_id) {
            if (current_tb == tb &&
                (tb_cflags(current_tb) & CF_COUNT_MASK) != 1) {
                /*
                 * If we are modifying the current TB, we must stop its
                 * execution. We could be more precise by checking that
                 * the modification is after the current PC, but it would
                 * require a specialized function to partially restore
                 * the CPU state.
                 */
                current_tb_modified = true;
                cpu_restore_state_from_tb(current_cpu, current_tb, pc);
            }
            tb_phys_invalidate__locked(tb);
        }
    }

    if (current_tb_modified) {
        /* Force execution of one insn next time.  */
        CPUState *cpu = current_cpu;
        cpu->cflags_next_tb =
            1 | CF_LAST_IO | CF_NOIRQ | curr_cflags(current_cpu);
        return true;
    }
    return false;
}
#else
/*
 * @p must be non-NULL.
 * Call with all @pages locked.
 */
static void
tb_invalidate_phys_page_range__locked(struct page_collection *pages,
                                      PageDesc *p, tb_page_addr_t start,
                                      tb_page_addr_t last,
                                      uintptr_t retaddr)
{
    p->aot_mesage = PAGE_NO_LOAD;
    p->aot_tu_file_offset = 0;
    p->aot_tu_cache_generation = 0;

    TranslationBlock *tb;
    PageForEachNext n;
#ifdef TARGET_HAS_PRECISE_SMC
    bool current_tb_modified = false;
    TranslationBlock *current_tb = retaddr ? tcg_tb_lookup(retaddr) : NULL;
#endif /* TARGET_HAS_PRECISE_SMC */

    /* Range may not cross a page. */
    tcg_debug_assert(((start ^ last) & TARGET_PAGE_MASK) == 0);

    uint32_t n_mtcc = qemu_tcg_mtcc_enabled()?MTCC_NR:1;
    uint32_t mtcc_id = 0;

#ifdef CONFIG_LATX_TU
    /* Any byte in this physical source page can change the appended CORE. */
    for (mtcc_id = 0; mtcc_id < n_mtcc; ++mtcc_id) {
        while (p->native_source_owners[mtcc_id]) {
            NativeSourceOwner *link = p->native_source_owners[mtcc_id];
            TranslationBlock *owner = link->owner;

            p->native_source_owners[mtcc_id] = link->next;
            if (tb_cflags(owner) & CF_INVALID) {
                continue;
            }
#ifdef TARGET_HAS_PRECISE_SMC
            if (current_tb && tb_is_tu(current_tb) &&
                tb_tu_identity(current_tb) == tb_tu_identity(owner) &&
                (tb_cflags(current_tb) & CF_COUNT_MASK) != 1) {
                /* HAMT native stores recover before retrying as an ordinary
                 * single-instruction TB. Only ordinary host PCs reach here. */
                current_tb_modified = true;
                cpu_restore_state_from_tb(current_cpu, current_tb, retaddr);
            }
#endif
            tb_phys_invalidate__locked(owner);
        }
    }
#endif

    /*
     * We remove all the TBs in the range [start, last].
     * XXX: see if in some cases it could be faster to invalidate all the code
     */
    for (mtcc_id = 0; mtcc_id < n_mtcc; ++mtcc_id) {
    PAGE_FOR_EACH_TB_MTCC(start, last, p, tb, n, mtcc_id) {
        tb_page_addr_t tb_start, tb_last;

        /* NOTE: this is subtle as a TB may span two physical pages */
        tb_start = tb_page_addr0(tb);
        tb_last = tb_start + tb->size - 1;
        if (n == 0) {
            tb_last = MIN(tb_last, tb_start | ~TARGET_PAGE_MASK);
        } else {
            tb_start = tb_page_addr1(tb);
            tb_last = tb_start + (tb_last & ~TARGET_PAGE_MASK);
        }
        if (!(tb_last < start || tb_start > last)) {
#ifdef TARGET_HAS_PRECISE_SMC
            bool current_tb_invalidated = current_tb == tb;
#ifdef CONFIG_LATX_TU
            if (current_tb && tb_is_tu(current_tb) && tb_is_tu(tb) &&
                (tb->flags & HF_CPL_MASK) &&
                tb_tu_identity(current_tb) == tb_tu_identity(tb)) {
                current_tb_invalidated = true;
            }
#endif
            if (current_tb_invalidated &&
                (tb_cflags(current_tb) & CF_COUNT_MASK) != 1) {
                /*
                 * If we invalidate the current TB or its TU, we must stop
                 * its execution. We could be more precise by checking
                 * that the modification is after the current PC, but it
                 * would require a specialized function to partially
                 * restore the CPU state.
                 */
                current_tb_modified = true;
                cpu_restore_state_from_tb(current_cpu, current_tb, retaddr);
            }
#endif /* TARGET_HAS_PRECISE_SMC */
            tb_phys_invalidate__locked(tb);
        }
    }
    }

    /* if no code remaining, no need to continue to use slow writes */
    bool page_is_clean = true;
    for (mtcc_id = 0; mtcc_id < n_mtcc; ++mtcc_id) {
        if (p->first_tb[mtcc_id]) {
            page_is_clean = false;
        }
#ifdef CONFIG_LATX_TU
        native_source_prune(p, mtcc_id);
        if (p->native_source_owners[mtcc_id]) {
            page_is_clean = false;
        }
#endif
    }
    if (page_is_clean) {
        tlb_unprotect_code(start);
    }

#ifdef TARGET_HAS_PRECISE_SMC
    if (current_tb_modified) {
        page_collection_unlock(pages);
        /* Force execution of one insn next time.  */
        current_cpu->cflags_next_tb =
            1 | CF_LAST_IO | CF_NOIRQ | curr_cflags(current_cpu);
        mmap_unlock();
        cpu_loop_exit_noexc(current_cpu);
    }
#endif
}

/*
 * Invalidate all TBs which intersect with the target physical
 * address page @addr.
 */
void tb_invalidate_phys_page(tb_page_addr_t addr)
{
    struct page_collection *pages;
    tb_page_addr_t start, last;
    PageDesc *p;

    p = page_find(addr >> TARGET_PAGE_BITS);
    if (p == NULL) {
        return;
    }

    start = addr & TARGET_PAGE_MASK;
    last = addr | ~TARGET_PAGE_MASK;
    pages = page_collection_lock(start, last);
    tb_invalidate_phys_page_range__locked(pages, p, start, last, 0);
    page_collection_unlock(pages);
}

/*
 * Invalidate all TBs which intersect with the target physical address range
 * [start;last]. NOTE: start and end may refer to *different* physical pages.
 * 'is_cpu_write_access' should be true if called from a real cpu write
 * access: the virtual CPU will exit the current TB if code is modified inside
 * this TB.
 */
void tb_invalidate_phys_range(tb_page_addr_t start, tb_page_addr_t last)
{
    struct page_collection *pages;
    tb_page_addr_t index, index_last;

    pages = page_collection_lock(start, last);

    index_last = last >> TARGET_PAGE_BITS;
    for (index = start >> TARGET_PAGE_BITS; index <= index_last; index++) {
        PageDesc *pd = page_find(index);
        tb_page_addr_t page_start, page_last;

        if (pd == NULL) {
            continue;
        }
        assert_page_locked(pd);
        page_start = index << TARGET_PAGE_BITS;
        page_last = page_start | ~TARGET_PAGE_MASK;
        page_last = MIN(page_last, last);
        tb_invalidate_phys_page_range__locked(pages, pd,
                                              page_start, page_last, 0);
    }
    page_collection_unlock(pages);
}

/*
 * Call with all @pages in the range [@start, @start + len[ locked.
 */
static void tb_invalidate_phys_page_fast__locked(struct page_collection *pages,
                                                 tb_page_addr_t start,
                                                 unsigned len, uintptr_t ra)
{
    PageDesc *p;

    p = page_find(start >> TARGET_PAGE_BITS);
    if (!p) {
        return;
    }

    assert_page_locked(p);
    tb_invalidate_phys_page_range__locked(pages, p, start, start + len - 1, ra);
}

/*
 * len must be <= 8 and start must be a multiple of len.
 * Called via softmmu_template.h when code areas are written to with
 * iothread mutex not held.
 */
void tb_invalidate_phys_range_fast(ram_addr_t ram_addr,
                                   unsigned size,
                                   uintptr_t retaddr)
{
    struct page_collection *pages;

    pages = page_collection_lock(ram_addr, ram_addr + size - 1);
    tb_invalidate_phys_page_fast__locked(pages, ram_addr, size, retaddr);
    page_collection_unlock(pages);
}

void page_print_tb(uint64_t page)
{
    struct PageDesc *p = page_find(page >> 12);

    int mtcc = 0;
    TranslationBlock *tb = NULL;
    int n = -1;
start:
    tb = (void *)p->first_tb[mtcc];
    printf("%s:%d MTCC = %d\n", __func__, __LINE__, mtcc);
    while (tb) {
        n = (uint64_t)tb & 0x3;
        tb = (void *)(((uint64_t)tb) & ~0x3UL);
        printf("n %d TB %lx F %x I %d PhysPC %lx Size %x (%p) ",
                n, tb->pc, tb->flags, tb->icount,
                tb->page_addr[0], tb->size,
                tb);
        tb = (void *)tb->page_next[n];
        printf("page_next[%d] = %p\n", n, tb);
    }
    if (mtcc == 1) goto out;
    mtcc = 1;
    goto start;
out:
    return;
}

#endif /* CONFIG_USER_ONLY */
