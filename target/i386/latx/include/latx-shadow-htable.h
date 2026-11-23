/*
 * SPDX-FileCopyrightText: 2024-2026 LATS Project Authors
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef _LATX_SHADOW_HTABLE_H_
#define _LATX_SHADOW_HTABLE_H_

extern uint32_t fsht_num;

typedef struct flaged_shadow_hash_table {
    uint32_t flags;
    bool dirty;
    uint64_t base;
} flaged_shadow_hash_table_t;

extern flaged_shadow_hash_table_t fsht[];
extern uint64_t fsht_global_base;

#define FSHT_BITS   20
#define FSHT_SIZE   ((1ULL << FSHT_BITS) << 3)
#define FSHT_MASK   ((1ULL << FSHT_BITS) - 1)

/* The supported flag sets differ in CPL, SS32, TF, CS64, AVX and HAMT.  Keep
 * one compact shared index, rather than a cached pointer in every TB. */
#define FSHT_FLAG_KEY_SIZE 64
extern uint8_t fsht_flag_ids[FSHT_FLAG_KEY_SIZE]; /* table id + 1; zero misses */

static inline unsigned fsht_flags_key(uint32_t flags)
{
    return (flags & 1) | ((flags >> 4) & 2) | ((flags >> 7) & 4) |
           ((flags >> 12) & 8) | ((flags >> 26) & 16) | ((flags >> 23) & 32);
}

static inline int fsht_flags_to_id(uint32_t flags)
{
    unsigned id = fsht_flag_ids[fsht_flags_key(flags)];

    /* Check the entire value: unsupported flags can share the same key. */
    return id && fsht[id - 1].flags == flags ? (int)id - 1 : -1;
}

void flaged_shadow_htable_init(void);
void flaged_shadow_htable_reset(void);
void flaged_shadow_htable_reset_mtcc(int n);

#define FSHT_FOREACH_FLAGS(i, f)    \
    for ((i) = 0;                   \
         (i) < fsht_num &&             \
         (f = fsht[(i)].flags, 1);  \
         ++i)

void flaged_shadow_htable_insert(void *tb);
void flaged_shadow_htable_remove(void *tb);

#endif
