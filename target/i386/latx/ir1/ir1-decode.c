/*
 * SPDX-FileCopyrightText: 2024-2026 LATS Project Authors
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "qemu/osdep.h"
#include "qemu/atomic.h"
#include "qemu/madvise.h"
#include "qemu/memalign.h"
#include "qemu/timer.h"
#include "../include/ir1-decode.h"
#include "../include/latx-options.h"

#define IR1_DECODE_STATS_MAX_THREADS 256
#define IR1_DECODE_IMM_NONE 0
#define IR1_DECODE_IMM_RELATIVE 1
#define IR1_DECODE_IMM_RAW 2
#define IR1_DECODE_IMM_SIGNED 3
#define IR1_DECODE_IMM_SIGNED_MASK 4
#define IR1_DECODE_IMM_RAW8 5
#define IR1_DECODE_IMM_RAW16 6
#define IR1_DECODE_IMM_RELATIVE_EXACT 7
#define IR1_DECODE_OP_COUNT_SHADOW 16
#define IR1_DECODE_TLS __thread __attribute__((tls_model("initial-exec")))
#define IR1_DECODE_DIST_PAGE_SLOTS 32768
#define IR1_DECODE_DIST_PAGE_WAYS 4
#define IR1_DECODE_DIST_TOP 16
#define IR1_DECODE_DIST_REUSE_BUCKETS 10

static size_t ir1_decode_template_bytes(void);
static size_t ir1_decode_tag_bytes(void);
static size_t ir1_decode_payload_bytes(void);

static inline size_t ir1_decode_template_entries(void)
{
    return (size_t)latxs_ir1_cache_entries();
}

static inline size_t ir1_decode_template_ways(void)
{
    return (size_t)latxs_ir1_cache_ways();
}

static inline size_t ir1_decode_template_sets(void)
{
    return ir1_decode_template_entries() / ir1_decode_template_ways();
}

typedef enum Ir1DecodeModeIndex {
    IR1_DECODE_MODE_16,
    IR1_DECODE_MODE_32,
    IR1_DECODE_MODE_64,
    IR1_DECODE_MODE_COUNT,
} Ir1DecodeModeIndex;

typedef struct Ir1DecodeSetProfile {
    uint64_t lookups;
    uint64_t hits;
    uint64_t misses;
    uint64_t replacements;
} Ir1DecodeSetProfile;

typedef struct Ir1DecodePageProfile {
    uint64_t page;
    uint64_t lookups;
    uint64_t hits;
    uint64_t misses;
    uint64_t hot_hits;
} Ir1DecodePageProfile;

typedef struct Ir1DecodeDistribution {
    size_t entries;
    size_t sets;
    size_t ways;
    Ir1DecodeSetProfile *set_profiles;
    uint8_t *set_occupancy;
    uint16_t *entry_reuse;
    uint64_t way_hits[LATX_IR1_CACHE_MAX_WAYS];
    uint64_t way_replacements[LATX_IR1_CACHE_MAX_WAYS];
    uint64_t fast_way_hits;
    uint64_t page_collisions;
    uint64_t opcode_hits[IR1_DECODE_MODE_COUNT][256];
    uint64_t opcode_inserts[IR1_DECODE_MODE_COUNT][256];
    uint64_t opcode_replacements[IR1_DECODE_MODE_COUNT][256];
    Ir1DecodePageProfile page_profiles[IR1_DECODE_DIST_PAGE_SLOTS];
} Ir1DecodeDistribution;

typedef struct Ir1DecodeStats {
    uint64_t calls[IR1_DECODE_MODE_COUNT];
    uint64_t successes[IR1_DECODE_MODE_COUNT];
    uint64_t hot_hits[IR1_DECODE_MODE_COUNT];
    uint64_t hot_attempt_ns[IR1_DECODE_MODE_COUNT];
    uint64_t hot_success_ns[IR1_DECODE_MODE_COUNT];
    uint64_t hot_failure_ns[IR1_DECODE_MODE_COUNT];
    uint64_t hot_attempt_samples[IR1_DECODE_MODE_COUNT];
    uint64_t hot_success_samples[IR1_DECODE_MODE_COUNT];
    uint64_t hot_failure_samples[IR1_DECODE_MODE_COUNT];
    uint64_t cache_lookups[IR1_DECODE_MODE_COUNT];
    uint64_t cache_unallocated[IR1_DECODE_MODE_COUNT];
    uint64_t cache_hits[IR1_DECODE_MODE_COUNT];
    uint64_t fast_cache_hits[IR1_DECODE_MODE_COUNT];
    uint64_t full_success[IR1_DECODE_MODE_COUNT];
    uint64_t full_fail[IR1_DECODE_MODE_COUNT];
    uint64_t cache_inserts[IR1_DECODE_MODE_COUNT];
    uint64_t cache_replacements[IR1_DECODE_MODE_COUNT];
    uint64_t cache_rejected_address[IR1_DECODE_MODE_COUNT];
    uint64_t cache_rejected_size[IR1_DECODE_MODE_COUNT];
    uint64_t cache_relative_inserts[IR1_DECODE_MODE_COUNT];
    uint64_t cache_relative_hits[IR1_DECODE_MODE_COUNT];
    uint64_t cache_imm_inserts[IR1_DECODE_MODE_COUNT];
    uint64_t cache_imm_hits[IR1_DECODE_MODE_COUNT];
    uint64_t cache_hit_ns[IR1_DECODE_MODE_COUNT];
    uint64_t fast_hit_ns[IR1_DECODE_MODE_COUNT];
    uint64_t main_hit_ns[IR1_DECODE_MODE_COUNT];
    uint64_t cache_miss_ns[IR1_DECODE_MODE_COUNT];
    uint64_t miss_lookup_ns[IR1_DECODE_MODE_COUNT];
    uint64_t miss_capstone_ns[IR1_DECODE_MODE_COUNT];
    uint64_t miss_insert_ns[IR1_DECODE_MODE_COUNT];
    uint64_t miss_lookup_samples[IR1_DECODE_MODE_COUNT];
    uint64_t miss_capstone_samples[IR1_DECODE_MODE_COUNT];
    uint64_t miss_insert_samples[IR1_DECODE_MODE_COUNT];
    uint64_t direct_ns[IR1_DECODE_MODE_COUNT];
    uint64_t cache_misses_by_opcode[IR1_DECODE_MODE_COUNT][256];
    uint64_t cache_allocations;
    uint64_t cache_alloc_bytes;
    uint64_t cache_tag_alloc_bytes;
    uint64_t cache_payload_alloc_bytes;
    uint64_t fast_cache_allocations;
    uint64_t fast_cache_alloc_bytes;
    uint64_t cache_occupied;
} Ir1DecodeStats;

typedef struct Ir1DecodeTiming {
    int mode_index;
    uint64_t total_start;
} Ir1DecodeTiming;

static Ir1DecodeStats *ir1_decode_stats_slots[IR1_DECODE_STATS_MAX_THREADS];
static uint32_t ir1_decode_stats_nr;
static IR1_DECODE_TLS Ir1DecodeStats *ir1_decode_stats;
static IR1_DECODE_TLS bool ir1_decode_last_fast_hit;
static Ir1DecodeDistribution *
    ir1_decode_distribution_slots[IR1_DECODE_STATS_MAX_THREADS];
static uint32_t ir1_decode_distribution_nr;
static IR1_DECODE_TLS Ir1DecodeDistribution *ir1_decode_distribution;

static void ir1_decode_distribution_dump(void);

static int ir1_decode_stats_mode_index(int mode)
{
    switch (mode) {
    case 16:
        return IR1_DECODE_MODE_16;
    case 32:
        return IR1_DECODE_MODE_32;
    case 64:
        return IR1_DECODE_MODE_64;
    default:
        return -1;
    }
}

static Ir1DecodeStats *ir1_decode_stats_get(void)
{
    if (!ir1_decode_stats) {
        uint32_t index;

        ir1_decode_stats = g_new0(Ir1DecodeStats, 1);
        index = qatomic_fetch_inc(&ir1_decode_stats_nr);
        if (index < IR1_DECODE_STATS_MAX_THREADS) {
            ir1_decode_stats_slots[index] = ir1_decode_stats;
        }
    }
    return ir1_decode_stats;
}

static inline bool ir1_decode_profile_enabled(void)
{
    return option_ir1_profile || option_ir1_timing ||
           option_ir1_distribution;
}

static inline Ir1DecodeStats *ir1_decode_profile_stats_get(void)
{
    return ir1_decode_profile_enabled() ? ir1_decode_stats_get() : NULL;
}

static Ir1DecodeDistribution *ir1_decode_distribution_get(void)
{
    if (!option_ir1_distribution) {
        return NULL;
    }
    if (!ir1_decode_distribution) {
        uint32_t index;

        ir1_decode_distribution = g_new0(Ir1DecodeDistribution, 1);
        ir1_decode_distribution->entries = ir1_decode_template_entries();
        ir1_decode_distribution->sets = ir1_decode_template_sets();
        ir1_decode_distribution->ways = ir1_decode_template_ways();
        ir1_decode_distribution->set_profiles =
            g_new0(Ir1DecodeSetProfile, ir1_decode_distribution->sets);
        ir1_decode_distribution->set_occupancy =
            g_new0(uint8_t, ir1_decode_distribution->sets);
        ir1_decode_distribution->entry_reuse =
            g_new(uint16_t, ir1_decode_distribution->entries);
        memset(ir1_decode_distribution->entry_reuse, 0xff,
               sizeof(uint16_t) * ir1_decode_distribution->entries);

        index = qatomic_fetch_inc(&ir1_decode_distribution_nr);
        if (index < IR1_DECODE_STATS_MAX_THREADS) {
            ir1_decode_distribution_slots[index] = ir1_decode_distribution;
        }
    }
    return ir1_decode_distribution;
}

static inline Ir1DecodePageProfile *ir1_decode_distribution_page(
        Ir1DecodeDistribution *distribution, uint64_t address)
{
    uint64_t page = address & ~UINT64_C(0xfff);
    uint64_t hash = page ^ (page >> 12) ^ (page >> 24) ^ (page >> 36);
    size_t sets = IR1_DECODE_DIST_PAGE_SLOTS / IR1_DECODE_DIST_PAGE_WAYS;
    size_t base = (hash & (sets - 1)) * IR1_DECODE_DIST_PAGE_WAYS;
    size_t free_way = IR1_DECODE_DIST_PAGE_WAYS;

    for (size_t way = 0; way < IR1_DECODE_DIST_PAGE_WAYS; way++) {
        Ir1DecodePageProfile *profile =
            &distribution->page_profiles[base + way];

        if (profile->page == page) {
            return profile;
        }
        if (!profile->page && free_way == IR1_DECODE_DIST_PAGE_WAYS) {
            free_way = way;
        }
    }
    if (free_way == IR1_DECODE_DIST_PAGE_WAYS) {
        free_way = (hash >> 32) & (IR1_DECODE_DIST_PAGE_WAYS - 1);
        distribution->page_collisions++;
    }
    Ir1DecodePageProfile *profile =
        &distribution->page_profiles[base + free_way];

    memset(profile, 0, sizeof(*profile));
    profile->page = page;
    return profile;
}

static void ir1_decode_stats_dump(void)
{
    Ir1DecodeStats total = { 0 };
    uint64_t opcode_misses[2][256];
    uint32_t nr = MIN(qatomic_read(&ir1_decode_stats_nr),
                      IR1_DECODE_STATS_MAX_THREADS);
    size_t entries = ir1_decode_template_entries();
    size_t sets = ir1_decode_template_sets();
    size_t ways = ir1_decode_template_ways();
    size_t per_thread_bytes =
        ir1_decode_template_bytes() * entries +
        sizeof(uint8_t) * entries +
        sizeof(uint8_t) * sets;

    if (!nr) {
        return;
    }

    for (uint32_t i = 0; i < nr; i++) {
        Ir1DecodeStats *stats = ir1_decode_stats_slots[i];

        if (!stats) {
            continue;
        }
        for (int mode = 0; mode < IR1_DECODE_MODE_COUNT; mode++) {
            total.calls[mode] += stats->calls[mode];
            total.successes[mode] += stats->successes[mode];
            total.hot_hits[mode] += stats->hot_hits[mode];
            total.hot_attempt_ns[mode] += stats->hot_attempt_ns[mode];
            total.hot_success_ns[mode] += stats->hot_success_ns[mode];
            total.hot_failure_ns[mode] += stats->hot_failure_ns[mode];
            total.hot_attempt_samples[mode] +=
                stats->hot_attempt_samples[mode];
            total.hot_success_samples[mode] +=
                stats->hot_success_samples[mode];
            total.hot_failure_samples[mode] +=
                stats->hot_failure_samples[mode];
            total.cache_lookups[mode] += stats->cache_lookups[mode];
            total.cache_unallocated[mode] += stats->cache_unallocated[mode];
            total.cache_hits[mode] += stats->cache_hits[mode];
            total.fast_cache_hits[mode] += stats->fast_cache_hits[mode];
            total.full_success[mode] += stats->full_success[mode];
            total.full_fail[mode] += stats->full_fail[mode];
            total.cache_inserts[mode] += stats->cache_inserts[mode];
            total.cache_replacements[mode] += stats->cache_replacements[mode];
            total.cache_rejected_address[mode] +=
                stats->cache_rejected_address[mode];
            total.cache_rejected_size[mode] += stats->cache_rejected_size[mode];
            total.cache_relative_inserts[mode] +=
                stats->cache_relative_inserts[mode];
            total.cache_relative_hits[mode] += stats->cache_relative_hits[mode];
            total.cache_imm_inserts[mode] += stats->cache_imm_inserts[mode];
            total.cache_imm_hits[mode] += stats->cache_imm_hits[mode];
            total.cache_hit_ns[mode] += stats->cache_hit_ns[mode];
            total.fast_hit_ns[mode] += stats->fast_hit_ns[mode];
            total.main_hit_ns[mode] += stats->main_hit_ns[mode];
            total.cache_miss_ns[mode] += stats->cache_miss_ns[mode];
            total.miss_lookup_ns[mode] += stats->miss_lookup_ns[mode];
            total.miss_capstone_ns[mode] += stats->miss_capstone_ns[mode];
            total.miss_insert_ns[mode] += stats->miss_insert_ns[mode];
            total.miss_lookup_samples[mode] +=
                stats->miss_lookup_samples[mode];
            total.miss_capstone_samples[mode] +=
                stats->miss_capstone_samples[mode];
            total.miss_insert_samples[mode] +=
                stats->miss_insert_samples[mode];
            total.direct_ns[mode] += stats->direct_ns[mode];
            if (mode > 0) {
                for (int opcode = 0; opcode < 256; opcode++) {
                    total.cache_misses_by_opcode[mode][opcode] +=
                        stats->cache_misses_by_opcode[mode][opcode];
                }
            }
        }
        total.cache_allocations += stats->cache_allocations;
        total.cache_alloc_bytes += stats->cache_alloc_bytes;
        total.cache_tag_alloc_bytes += stats->cache_tag_alloc_bytes;
        total.cache_payload_alloc_bytes += stats->cache_payload_alloc_bytes;
        total.fast_cache_allocations += stats->fast_cache_allocations;
        total.fast_cache_alloc_bytes += stats->fast_cache_alloc_bytes;
        total.cache_occupied += stats->cache_occupied;
    }

    fprintf(stderr,
            "IR1_DECODE_STATS threads=%u "
            "mode16_calls=%" PRIu64 " mode16_ok=%" PRIu64 " "
            "mode16_hot=%" PRIu64 " mode16_cache_lookups=%" PRIu64
            " mode16_cache_hits=%" PRIu64 " mode16_full=%" PRIu64 "\n",
            nr, total.calls[0], total.successes[0], total.hot_hits[0],
            total.cache_lookups[0], total.cache_hits[0], total.full_success[0]);
    fprintf(stderr,
            "IR1_DECODE_STATS32 "
            "calls=%" PRIu64 " ok=%" PRIu64 " hot=%" PRIu64
            " cache_lookups=%" PRIu64 " cache_unallocated=%" PRIu64
            " cache_hits=%" PRIu64 " full_success=%" PRIu64
            " full_fail=%" PRIu64 " inserts=%" PRIu64
            " replacements=%" PRIu64 " reject_address=%" PRIu64
            " reject_size=%" PRIu64 " relative_inserts=%" PRIu64
            " relative_hits=%" PRIu64 " imm_inserts=%" PRIu64
            " imm_hits=%" PRIu64 " fast_hits=%" PRIu64 "\n",
            total.calls[1], total.successes[1], total.hot_hits[1],
            total.cache_lookups[1], total.cache_unallocated[1],
            total.cache_hits[1], total.full_success[1], total.full_fail[1],
            total.cache_inserts[1], total.cache_replacements[1],
            total.cache_rejected_address[1], total.cache_rejected_size[1],
            total.cache_relative_inserts[1], total.cache_relative_hits[1],
            total.cache_imm_inserts[1], total.cache_imm_hits[1],
            total.fast_cache_hits[1]);
    fprintf(stderr,
            "IR1_DECODE_STATS64 "
            "calls=%" PRIu64 " ok=%" PRIu64 " hot=%" PRIu64
            " cache_lookups=%" PRIu64 " cache_unallocated=%" PRIu64
            " cache_hits=%" PRIu64 " full_success=%" PRIu64
            " full_fail=%" PRIu64 " inserts=%" PRIu64
            " replacements=%" PRIu64 " reject_address=%" PRIu64
            " reject_size=%" PRIu64 " relative_inserts=%" PRIu64
            " relative_hits=%" PRIu64 " imm_inserts=%" PRIu64
            " imm_hits=%" PRIu64 " fast_hits=%" PRIu64 "\n",
            total.calls[2], total.successes[2], total.hot_hits[2],
            total.cache_lookups[2], total.cache_unallocated[2],
            total.cache_hits[2], total.full_success[2], total.full_fail[2],
            total.cache_inserts[2], total.cache_replacements[2],
            total.cache_rejected_address[2], total.cache_rejected_size[2],
            total.cache_relative_inserts[2], total.cache_relative_hits[2],
            total.cache_imm_inserts[2], total.cache_imm_hits[2],
            total.fast_cache_hits[2]);
    fprintf(stderr,
            "IR1_DECODE_CACHE allocated_threads=%" PRIu64
            " occupied_slots=%" PRIu64 " per_thread_bytes=%zu"
            " allocated_bytes=%" PRIu64 " entries=%u sets=%u ways=%u"
            " entry_bytes=%zu tag_entry_bytes=%zu payload_entry_bytes=%zu"
            " tag_allocated_bytes=%" PRIu64
            " payload_allocated_bytes=%" PRIu64
            " fast_allocated_threads=%" PRIu64
            " fast_allocated_bytes=%" PRIu64 "\n",
            total.cache_allocations, total.cache_occupied, per_thread_bytes,
            total.cache_alloc_bytes + total.fast_cache_alloc_bytes,
            (unsigned)entries, (unsigned)sets, (unsigned)ways,
            ir1_decode_template_bytes(), ir1_decode_tag_bytes(),
            ir1_decode_payload_bytes(), total.cache_tag_alloc_bytes,
            total.cache_payload_alloc_bytes, total.fast_cache_allocations,
            total.fast_cache_alloc_bytes);
    fprintf(stderr, "IR1_DECODE_POLICY enabled=%s replacement=%s entries=%zu "
            "ways=%zu sets=%zu\n", option_ir1_cache_enabled ? "on" : "off",
            latxs_ir1_cache_replace_name(option_ir1_cache_replace), entries,
            ways, sets);
    for (int mode = 1; mode <= 2; mode++) {
        fprintf(stderr,
                "IR1_HOT_TIMING%d enabled=%s attempts=%" PRIu64
                " attempt_avg_ns=%.2f success=%" PRIu64
                " success_avg_ns=%.2f failure=%" PRIu64
                " failure_avg_ns=%.2f\n",
                mode == 1 ? 32 : 64,
                option_hot_decode_enabled && option_ir1_timing ? "on" : "off",
                total.hot_attempt_samples[mode],
                total.hot_attempt_samples[mode] ?
                    (double)total.hot_attempt_ns[mode] /
                    total.hot_attempt_samples[mode] : 0.0,
                total.hot_success_samples[mode],
                total.hot_success_samples[mode] ?
                    (double)total.hot_success_ns[mode] /
                    total.hot_success_samples[mode] : 0.0,
                total.hot_failure_samples[mode],
                total.hot_failure_samples[mode] ?
                    (double)total.hot_failure_ns[mode] /
                    total.hot_failure_samples[mode] : 0.0);
    }
    for (int mode = 1; mode <= 2; mode++) {
        if (option_ir1_cache_enabled) {
            uint64_t hit_samples = total.cache_hits[mode];
            uint64_t fast_samples = total.fast_cache_hits[mode];
            uint64_t main_samples = hit_samples - fast_samples;
            uint64_t miss_samples = total.cache_lookups[mode] - hit_samples;
            uint64_t total_ns = total.cache_hit_ns[mode] +
                                total.cache_miss_ns[mode];
            uint64_t total_samples = hit_samples + miss_samples;

            fprintf(stderr,
                    "IR1_TIMING%d cache=on hit_ns=%" PRIu64
                    " hit_samples=%" PRIu64 " hit_avg_ns=%.2f"
                    " fast_hit_avg_ns=%.2f main_hit_avg_ns=%.2f"
                    " miss_ns=%" PRIu64 " miss_samples=%" PRIu64
                    " miss_avg_ns=%.2f total_ns=%" PRIu64
                    " total_samples=%" PRIu64 " total_avg_ns=%.2f\n",
                    mode == 1 ? 32 : 64, total.cache_hit_ns[mode],
                    hit_samples,
                    hit_samples ? (double)total.cache_hit_ns[mode] /
                                  hit_samples : 0.0,
                    fast_samples ? (double)total.fast_hit_ns[mode] /
                                   fast_samples : 0.0,
                    main_samples ? (double)total.main_hit_ns[mode] /
                                   main_samples : 0.0,
                    total.cache_miss_ns[mode], miss_samples,
                    miss_samples ? (double)total.cache_miss_ns[mode] /
                                   miss_samples : 0.0,
                    total_ns, total_samples,
                    total_samples ? (double)total_ns / total_samples : 0.0);
            fprintf(stderr,
                    "IR1_TIMING_MISS%d cache=on total_ns=%" PRIu64
                    " lookup_ns=%" PRIu64 " lookup_samples=%" PRIu64
                    " lookup_avg_ns=%.2f capstone_ns=%" PRIu64
                    " capstone_samples=%" PRIu64 " capstone_avg_ns=%.2f"
                    " insert_ns=%" PRIu64 " insert_samples=%" PRIu64
                    " insert_avg_ns=%.2f\n",
                    mode == 1 ? 32 : 64, total.cache_miss_ns[mode],
                    total.miss_lookup_ns[mode],
                    total.miss_lookup_samples[mode],
                    total.miss_lookup_samples[mode] ?
                        (double)total.miss_lookup_ns[mode] /
                        total.miss_lookup_samples[mode] : 0.0,
                    total.miss_capstone_ns[mode],
                    total.miss_capstone_samples[mode],
                    total.miss_capstone_samples[mode] ?
                        (double)total.miss_capstone_ns[mode] /
                        total.miss_capstone_samples[mode] : 0.0,
                    total.miss_insert_ns[mode], total.miss_insert_samples[mode],
                    total.miss_insert_samples[mode] ?
                        (double)total.miss_insert_ns[mode] /
                        total.miss_insert_samples[mode] : 0.0);
        } else {
            uint64_t direct_samples = total.full_success[mode] +
                                      total.full_fail[mode];

            fprintf(stderr,
                    "IR1_TIMING%d cache=off direct_ns=%" PRIu64
                    " direct_samples=%" PRIu64 " direct_avg_ns=%.2f\n",
                    mode == 1 ? 32 : 64, total.direct_ns[mode],
                    direct_samples,
                    direct_samples ? (double)total.direct_ns[mode] /
                                     direct_samples : 0.0);
        }
    }

    if (option_ir1_timing) {
        for (uint32_t i = 0; i < nr; i++) {
            Ir1DecodeStats *stats = ir1_decode_stats_slots[i];

            if (!stats ||
                stats->cache_hits[1] + stats->cache_hits[2] == 0) {
                continue;
            }
            for (int mode = 1; mode <= 2; mode++) {
                uint64_t hit_samples = stats->cache_hits[mode];
                uint64_t fast_samples = stats->fast_cache_hits[mode];
                uint64_t main_samples = hit_samples - fast_samples;

                fprintf(stderr,
                        "IR1_TIMING_THREAD%d_%d hit_avg_ns=%.2f"
                        " fast_hit_avg_ns=%.2f main_hit_avg_ns=%.2f"
                        " hits=%" PRIu64 " fast_hits=%" PRIu64 "\n",
                        i, mode == 1 ? 32 : 64,
                        hit_samples ? (double)stats->cache_hit_ns[mode] /
                                      hit_samples : 0.0,
                        fast_samples ? (double)stats->fast_hit_ns[mode] /
                                       fast_samples : 0.0,
                        main_samples ? (double)stats->main_hit_ns[mode] /
                                       main_samples : 0.0,
                        hit_samples, fast_samples);
            }
        }
    }

    for (int mode = 1; mode <= 2; mode++) {
        memcpy(opcode_misses[mode - 1], total.cache_misses_by_opcode[mode],
               sizeof(opcode_misses[mode - 1]));
        fprintf(stderr, "IR1_DECODE_MISS%d", mode == 1 ? 32 : 64);
        for (int rank = 0; rank < 16; rank++) {
            int best = 0;

            for (int opcode = 1; opcode < 256; opcode++) {
                if (opcode_misses[mode - 1][opcode] >
                    opcode_misses[mode - 1][best]) {
                    best = opcode;
                }
            }
            if (!opcode_misses[mode - 1][best]) {
                break;
            }
            fprintf(stderr, " %02x=%" PRIu64, best,
                    opcode_misses[mode - 1][best]);
            opcode_misses[mode - 1][best] = 0;
        }
        fprintf(stderr, "\n");
    }
    if (option_ir1_distribution) {
        ir1_decode_distribution_dump();
    }
}

static void __attribute__((constructor)) ir1_decode_stats_register(void)
{
    if (atexit(ir1_decode_stats_dump) != 0) {
        fprintf(stderr, "WARNING: cannot register IR1 decode statistics dump\n");
    }
}

/*
 * Translation decodes repeated position-independent instruction encodings.
 * Keep exact decoded instructions as templates after the hot decoder declines
 * them, and retain the full decoder for unsupported forms and cache misses.
 */
typedef union Ir1DecodeTemplateKey {
    uint8_t bytes[16];
    uint64_t words[2];
} Ir1DecodeTemplateKey;

typedef struct Ir1DecodeTag {
    uint8_t mode;
    uint8_t size;
    uint8_t relative_imm_mask;
    uint8_t imm_transform;
    uint32_t hash_tag;
    uint64_t byte_mask[2];
    Ir1DecodeTemplateKey key;
} Ir1DecodeTag;

typedef struct Ir1DecodePayload {
    cs_insn decoded;
#ifdef CONFIG_CAPSTONE_DIET
    cs_x86 x86;
#else
    cs_detail detail;
#endif
} Ir1DecodePayload;

typedef struct Ir1DecodeTemplate {
    Ir1DecodeTag tag;
    Ir1DecodePayload payload;
} Ir1DecodeTemplate;

_Static_assert(sizeof(Ir1DecodeTag) >= 40,
               "IR1 decode tag unexpectedly small");
_Static_assert(sizeof(Ir1DecodeTag) <= 64,
               "IR1 decode tag grew beyond the compact metadata budget");
_Static_assert(sizeof(Ir1DecodePayload) > sizeof(Ir1DecodeTag),
               "IR1 decode payload should exceed tag size");

static inline size_t ir1_decode_tag_bytes(void)
{
    return sizeof(Ir1DecodeTag);
}

static inline size_t ir1_decode_payload_bytes(void)
{
    return sizeof(Ir1DecodePayload);
}

static inline size_t ir1_decode_template_bytes(void)
{
    return ir1_decode_tag_bytes() + ir1_decode_payload_bytes();
}

#define IR1_DECODE_FAST_CACHE_WAYS 1
#define IR1_DECODE_FAST_MAX_OPERANDS 2
#define IR1_DECODE_FAST_CACHE_ALIGN (2 * 1024 * 1024)

static inline size_t ir1_decode_fast_cache_entries(void)
{
    return latxs_ir1_fast_cache_entries();
}

static inline size_t ir1_decode_fast_cache_sets(void)
{
    return ir1_decode_fast_cache_entries() / IR1_DECODE_FAST_CACHE_WAYS;
}

#if defined(CONFIG_CAPSTONE_DIET) && !defined(CONFIG_LATX_PRINT)
#define IR1_DECODE_COMPACT_FAST_TEMPLATE 1
typedef struct Ir1DecodeCompactOp {
    uint64_t payload[3];
    x86_avx_bcast avx_bcast;
    uint8_t type;
    uint8_t size;
    uint8_t access;
    bool avx_zero_opmask;
} Ir1DecodeCompactOp;

typedef uint8_t Ir1DecodeVec16
    __attribute__((vector_size(16), may_alias));

typedef struct Ir1DecodeCompactX86 {
    uint8_t prefix[4];
    uint8_t opcode[4];
    uint8_t rex;
    uint8_t addr_size;
    uint8_t modrm;
    uint8_t sib;
    uint8_t reserved0[4];
    int64_t disp;
    x86_reg sib_index;
    int8_t sib_scale;
    uint8_t reserved1[3];
    x86_reg sib_base;
    x86_xop_cc xop_cc;
    x86_sse_cc sse_cc;
    x86_avx_cc avx_cc;
    bool avx_sae;
    uint8_t reserved2[3];
    x86_avx_rm avx_rm;
    uint64_t eflags;
} Ir1DecodeCompactX86;

_Static_assert(sizeof(Ir1DecodeCompactX86) == 64,
               "compact x86 header size mismatch");
_Static_assert(offsetof(Ir1DecodeCompactX86, disp) ==
               offsetof(cs_x86, disp), "compact x86 disp offset mismatch");
_Static_assert(offsetof(Ir1DecodeCompactX86, sib_index) ==
               offsetof(cs_x86, sib_index),
               "compact x86 sib_index offset mismatch");
_Static_assert(offsetof(Ir1DecodeCompactX86, sib_base) ==
               offsetof(cs_x86, sib_base),
               "compact x86 sib_base offset mismatch");
_Static_assert(offsetof(Ir1DecodeCompactX86, avx_rm) ==
               offsetof(cs_x86, avx_rm),
               "compact x86 avx_rm offset mismatch");
_Static_assert(offsetof(Ir1DecodeCompactX86, eflags) ==
               offsetof(cs_x86, eflags),
               "compact x86 eflags offset mismatch");

typedef struct Ir1DecodeFastTemplate {
    uint64_t byte_mask[2];
    Ir1DecodeTemplateKey key;
    uint32_t id;
    cs_x86_encoding encoding;
    uint8_t relative_imm_mask;
    uint8_t imm_transform;
    uint8_t op_count;
    uint8_t disp_offset;
    uint8_t disp_size;
    bool disp_signed;
    Ir1DecodeCompactX86 x86;
    Ir1DecodeCompactOp operands[IR1_DECODE_FAST_MAX_OPERANDS];
} Ir1DecodeFastTemplate;
#else
#define IR1_DECODE_COMPACT_FAST_TEMPLATE 0
typedef Ir1DecodeTemplate Ir1DecodeFastTemplate;
#endif

typedef struct Ir1DecodeFastEntry {
    uint32_t hash_tag;
    uint8_t size;
    uint8_t mode;
    bool valid;
    uint8_t reserved[3];
    Ir1DecodeFastTemplate entry;
} Ir1DecodeFastEntry;

#if IR1_DECODE_COMPACT_FAST_TEMPLATE
_Static_assert(sizeof(Ir1DecodeFastEntry) == 192,
               "compact IR1 fast entry size mismatch");
#endif

#if IR1_DECODE_COMPACT_FAST_TEMPLATE
static inline void ir1_decode_compact_x86_from_cs_x86(
        Ir1DecodeFastTemplate *fast, const cs_x86 *x86)
{
    memcpy(&fast->x86, x86, sizeof(fast->x86));

    for (int i = 0; i < fast->op_count; i++) {
        const cs_x86_op *source = &x86->operands[i];
        Ir1DecodeCompactOp *dest = &fast->operands[i];

        memcpy(dest->payload, &source->mem, sizeof(dest->payload));
        dest->avx_bcast = source->avx_bcast;
        dest->type = source->type;
        dest->size = source->size;
        dest->access = source->access;
        dest->avx_zero_opmask = source->avx_zero_opmask;
    }
}
#endif

static IR1_DECODE_TLS Ir1DecodeTag *ir1_decode_tags;
static IR1_DECODE_TLS Ir1DecodePayload *ir1_decode_payloads;
static IR1_DECODE_TLS uint8_t *ir1_decode_template_valid;
static IR1_DECODE_TLS uint8_t *ir1_decode_template_policy;
static IR1_DECODE_TLS Ir1DecodeFastEntry *ir1_decode_fast_templates;
#if IR1_DECODE_FAST_CACHE_WAYS > 1
static IR1_DECODE_TLS uint8_t *ir1_decode_fast_policy;
#endif
static IR1_DECODE_TLS uint32_t ir1_decode_template_random_state;
static GPrivate ir1_decode_tags_owner = G_PRIVATE_INIT(g_free);
static GPrivate ir1_decode_payloads_owner = G_PRIVATE_INIT(g_free);
static GPrivate ir1_decode_template_valid_owner = G_PRIVATE_INIT(g_free);
static GPrivate ir1_decode_template_policy_owner = G_PRIVATE_INIT(g_free);
static GPrivate ir1_decode_fast_templates_owner =
    G_PRIVATE_INIT(qemu_vfree);
#if IR1_DECODE_FAST_CACHE_WAYS > 1
static GPrivate ir1_decode_fast_policy_owner = G_PRIVATE_INIT(g_free);
#endif

static const uint32_t ir1_decode_legacy_prefix_bits[8] = {
    0x00000000u, 0x40404040u, 0x00000000u, 0x000000f0u,
    0x00000000u, 0x00000000u, 0x00000000u, 0x000d0000u,
};

static const uint32_t ir1_decode_no_modrm_bits[8] = {
    0x30303030u, 0x30303030u, 0xffff0000u, 0xfffff503u,
    0xffff0000u, 0xffffff0fu, 0x0000ff0cu, 0xff32ffffu,
};

static inline __attribute__((always_inline)) bool
ir1_decode_template_legacy_prefix(uint8_t byte)
{
    return ir1_decode_legacy_prefix_bits[byte >> 5] &
           (UINT32_C(1) << (byte & 31));
}

static inline __attribute__((always_inline)) bool
ir1_decode_template_opcode_has_modrm(uint8_t opcode, bool two_byte)
{
    if (two_byte) {
        return !(opcode >= 0x80 && opcode <= 0x8f);
    }
    return !(ir1_decode_no_modrm_bits[opcode >> 5] &
             (UINT32_C(1) << (opcode & 31)));
}

static inline __attribute__((always_inline)) bool
ir1_decode_template_hash_opcode_has_modrm(uint8_t opcode, bool two_byte,
                                           int mode)
{
    if (option_ir1_hash != LATX_IR1_HASH_LEGACY) {
        if (!two_byte && (opcode == 0xfe || opcode == 0xff)) {
            return true;
        }
        if (!two_byte && mode != 64 &&
            opcode >= 0x40 && opcode <= 0x4f) {
            return false;
        }
    }
    return ir1_decode_template_opcode_has_modrm(opcode, two_byte);
}

static inline __attribute__((always_inline)) uint32_t
ir1_decode_template_hash_finalize(uint32_t hash)
{
    if (option_ir1_hash == LATX_IR1_HASH_MURMUR) {
        hash ^= hash >> 16;
        hash *= UINT32_C(0x85ebca6b);
        hash ^= hash >> 13;
        hash *= UINT32_C(0xc2b2ae35);
        hash ^= hash >> 16;
    }
    return hash;
}

static inline __attribute__((always_inline)) uint32_t
ir1_decode_template_hash_value(const uint8_t *code, size_t size, int mode)
{
    uint32_t hash = 2166136261u ^ (uint32_t)mode;
    size_t pos = 0;
    bool addr16 = mode == 16;

    for (; pos < size; pos++) {
        uint8_t byte = code[pos];

        if (ir1_decode_template_legacy_prefix(byte)) {
            if (byte == 0x67) {
                addr16 = !addr16;
            }
            hash = (hash ^ byte) * 16777619u;
            continue;
        }
        if (mode == 64 && byte >= 0x40 && byte <= 0x4f) {
            hash = (hash ^ byte) * 16777619u;
            continue;
        }
        break;
    }
    if (pos >= size) {
        return ir1_decode_template_hash_finalize(hash);
    }

    uint8_t opcode = code[pos++];
    hash = (hash ^ opcode) * 16777619u;
    if (opcode == 0xc4 || opcode == 0xc5 || opcode == 0x62) {
        for (int i = 0; i < 3 && pos < size; i++) {
            hash = (hash ^ code[pos++]) * 16777619u;
        }
        return ir1_decode_template_hash_finalize(hash);
    }

    bool two_byte = opcode == 0x0f && pos < size;
    if (two_byte) {
        opcode = code[pos++];
        hash = (hash ^ opcode) * 16777619u;
        if ((opcode == 0x38 || opcode == 0x3a) && pos < size) {
            opcode = code[pos++];
            hash = (hash ^ opcode) * 16777619u;
        }
    }
    if (ir1_decode_template_hash_opcode_has_modrm(opcode, two_byte, mode) &&
        pos < size) {
        uint8_t modrm = code[pos++];
        hash = (hash ^ modrm) * 16777619u;
        if ((modrm >> 6) != 3 && (modrm & 7) == 4 && pos < size) {
            hash = (hash ^ code[pos]) * 16777619u;
        }
    }
    return ir1_decode_template_hash_finalize(hash);
}

static uint32_t G_GNUC_UNUSED ir1_decode_template_hash(
        const uint8_t *code, size_t size, int mode)
{
    return ir1_decode_template_hash_value(code, size, mode) &
           (ir1_decode_template_sets() - 1);
}

static cs_insn *ir1_decode_template_output(int ir1_num,
                                                     void *pir1_base)
{
    return (void *)((uintptr_t)pir1_base +
                    ir1_num * IR1_INST_SIZE);
}

static void ir1_decode_output_init(cs_insn *info)
{
    memset(info, 0, IR1_INST_SIZE);
    info->detail = IR1_INST_DETAIL(info);
}

/*
 * Decode the common, regular 32/64-bit x86 forms directly.  This path runs before the
 * cache, has no lookup or warm-up cost, and rejects prefixes and forms that it
 * does not model exactly.  Rejected instructions continue through the exact
 * cache and then the full decoder.
 */
static const x86_reg ir1_hot_gpr64[16] = {
    X86_REG_RAX, X86_REG_RCX, X86_REG_RDX, X86_REG_RBX,
    X86_REG_RSP, X86_REG_RBP, X86_REG_RSI, X86_REG_RDI,
    X86_REG_R8, X86_REG_R9, X86_REG_R10, X86_REG_R11,
    X86_REG_R12, X86_REG_R13, X86_REG_R14, X86_REG_R15,
};

static const x86_reg ir1_hot_gpr32[16] = {
    X86_REG_EAX, X86_REG_ECX, X86_REG_EDX, X86_REG_EBX,
    X86_REG_ESP, X86_REG_EBP, X86_REG_ESI, X86_REG_EDI,
    X86_REG_R8D, X86_REG_R9D, X86_REG_R10D, X86_REG_R11D,
    X86_REG_R12D, X86_REG_R13D, X86_REG_R14D, X86_REG_R15D,
};

static int64_t ir1_hot_read_simm(const uint8_t *code, uint8_t size)
{
    if (size == 1) {
        return *(const int8_t *)code;
    }
    int32_t value;
    memcpy(&value, code, sizeof(value));
    return value;
}

static inline uint64_t ir1_hot_relative_target(uint64_t address,
                                               uint8_t size, int mode,
                                               int64_t displacement)
{
    uint64_t target = address + size + displacement;

    return mode == 64 ? target : (uint32_t)target;
}

static uint64_t ir1_hot_eflags(unsigned int id)
{
    const uint64_t arithmetic =
        X86_EFLAGS_MODIFY_AF | X86_EFLAGS_MODIFY_CF |
        X86_EFLAGS_MODIFY_SF | X86_EFLAGS_MODIFY_ZF |
        X86_EFLAGS_MODIFY_PF | X86_EFLAGS_MODIFY_OF;

    switch (id) {
    case X86_INS_JO:
    case X86_INS_JNO:
        return X86_EFLAGS_TEST_OF;
    case X86_INS_JB:
    case X86_INS_JAE:
        return X86_EFLAGS_TEST_CF;
    case X86_INS_JE:
    case X86_INS_JNE:
        return X86_EFLAGS_TEST_ZF;
    case X86_INS_JBE:
    case X86_INS_JA:
        return X86_EFLAGS_TEST_CF | X86_EFLAGS_TEST_ZF;
    case X86_INS_JS:
    case X86_INS_JNS:
        return X86_EFLAGS_TEST_SF;
    case X86_INS_JP:
    case X86_INS_JNP:
        return X86_EFLAGS_TEST_PF;
    case X86_INS_JL:
    case X86_INS_JGE:
        return X86_EFLAGS_TEST_OF | X86_EFLAGS_TEST_SF;
    case X86_INS_JLE:
    case X86_INS_JG:
        return X86_EFLAGS_TEST_OF | X86_EFLAGS_TEST_SF |
               X86_EFLAGS_TEST_ZF;
    case X86_INS_ADC:
    case X86_INS_SBB:
    case X86_INS_ADD:
    case X86_INS_SUB:
    case X86_INS_CMP:
        return arithmetic;
    case X86_INS_AND:
    case X86_INS_OR:
    case X86_INS_XOR:
    case X86_INS_TEST:
        return X86_EFLAGS_MODIFY_SF | X86_EFLAGS_MODIFY_ZF |
               X86_EFLAGS_MODIFY_PF | X86_EFLAGS_RESET_OF |
               X86_EFLAGS_RESET_CF | X86_EFLAGS_UNDEFINED_AF;
    default:
        return 0;
    }
}

static void ir1_hot_finish_operand_access(cs_insn *info)
{
    cs_x86 *x86 = &info->detail->x86;

    switch (info->id) {
    case X86_INS_PUSH:
        x86->operands[0].access = CS_AC_READ;
        break;
    case X86_INS_POP:
        x86->operands[0].access = CS_AC_WRITE;
        break;
    case X86_INS_MOV:
    case X86_INS_MOVABS:
        x86->operands[0].access = CS_AC_WRITE;
        if (x86->op_count > 1 && x86->operands[1].type != X86_OP_IMM) {
            x86->operands[1].access = CS_AC_READ;
        }
        break;
    case X86_INS_LEA:
        x86->operands[0].access = CS_AC_WRITE;
        x86->operands[1].access = CS_AC_READ;
        break;
    case X86_INS_CMP:
    case X86_INS_TEST:
        x86->operands[0].access = CS_AC_READ;
        if (x86->op_count > 1 && x86->operands[1].type != X86_OP_IMM) {
            x86->operands[1].access = CS_AC_READ;
        }
        break;
    case X86_INS_ADC:
    case X86_INS_ADD:
    case X86_INS_AND:
    case X86_INS_OR:
    case X86_INS_SBB:
    case X86_INS_SUB:
    case X86_INS_XOR:
        x86->operands[0].access = CS_AC_READ | CS_AC_WRITE;
        if (x86->op_count > 1 && x86->operands[1].type != X86_OP_IMM) {
            x86->operands[1].access = CS_AC_READ;
        }
        break;
    default:
        break;
    }
}

static void ir1_hot_finish(cs_insn *info, const uint8_t *code,
                           uint8_t size, uint64_t address, uint8_t rex,
                           uint8_t opcode0, uint8_t opcode1, int mode)
{
    info->size = size;
    info->address = address;
    info->detail->x86.addr_size = mode == 64 ? 8 : 4;
    info->detail->x86.rex = rex;
    info->detail->x86.eflags = ir1_hot_eflags(info->id);
    info->detail->x86.opcode[0] = opcode0;
    info->detail->x86.opcode[1] = opcode1;
    ir1_hot_finish_operand_access(info);
    memcpy(info->bytes, code, size);
    cs_x86 *x86 = &info->detail->x86;
    uint8_t offset = rex ? 2 : 1;
    if (opcode0 >= 0xb8 && opcode0 <= 0xbf) {
        x86->encoding.imm_offset = offset;
        x86->encoding.imm_size = size - offset;
        if (rex & 8) {
            info->id = X86_INS_MOVABS;
        }
    } else if ((opcode0 >= 0x70 && opcode0 <= 0x7f) ||
               opcode0 == 0xe8 || opcode0 == 0xe9 || opcode0 == 0xeb ||
               (opcode0 == 0x0f && opcode1 >= 0x80 && opcode1 <= 0x8f)) {
        x86->encoding.imm_offset = opcode0 == 0x0f ? 2 : 1;
        x86->encoding.imm_size = size - x86->encoding.imm_offset;
    } else if (x86->op_count == 2) {
        x86->encoding.modrm_offset = offset;
        uint8_t modrm = code[offset++];
        uint8_t mod = modrm >> 6;
        uint8_t rm = modrm & 7;
        bool disp32 = mod == 0 && rm == 5;
        if (mod != 3 && rm == 4) {
            uint8_t sib = code[offset++];
            uint8_t index = (sib >> 3) & 7;
            uint8_t base = sib & 7;
            x86->sib = sib;
            x86->sib_scale = 1 << (sib >> 6);
            if (index != 4 || (rex & 2)) {
                x86->sib_index = mode == 64 ?
                    ir1_hot_gpr64[index | ((rex & 2) << 2)] :
                    ir1_hot_gpr32[index];
            }
            if (mod == 0 && base == 5) {
                disp32 = true;
            } else {
                x86->sib_base = mode == 64 ?
                    ir1_hot_gpr64[base | ((rex & 1) << 3)] :
                    ir1_hot_gpr32[base];
            }
        }
        if (mod == 1 || mod == 2 || disp32) {
            x86->encoding.disp_offset = offset;
            x86->encoding.disp_size = mod == 1 ? 1 : 4;
            x86->disp = ir1_hot_read_simm(code + offset,
                                         x86->encoding.disp_size);
        }
        if (opcode0 == 0x83) {
            x86->encoding.imm_offset = size - 1;
            x86->encoding.imm_size = 1;
        }
    }
#ifndef CONFIG_CAPSTONE_DIET
    for (int i = 0; i < info->detail->x86.op_count; i++) {
        cs_x86_op *operand = &info->detail->x86.operands[i];
        if (operand->type != X86_OP_IMM) {
            operand->access = CS_AC_READ;
        }
    }
    switch (info->id) {
    case X86_INS_MOV:
    case X86_INS_LEA:
    case X86_INS_POP:
        info->detail->x86.operands[0].access = CS_AC_WRITE;
        break;
    case X86_INS_ADD:
    case X86_INS_OR:
    case X86_INS_ADC:
    case X86_INS_SBB:
    case X86_INS_AND:
    case X86_INS_SUB:
    case X86_INS_XOR:
        info->detail->x86.operands[0].access = CS_AC_READ | CS_AC_WRITE;
        break;
    default:
        break;
    }
#endif
}

static uint8_t ir1_hot_decode_rm(const uint8_t *code, uint8_t offset,
                                 uint8_t rex, uint8_t width, int mode,
                                 cs_x86_op *operand)
{
    uint8_t modrm = code[offset++];
    uint8_t mod = modrm >> 6;
    uint8_t rm = modrm & 7;
    if (mod == 3) {
        const x86_reg *regs = width == 8 ? ir1_hot_gpr64 : ir1_hot_gpr32;
        operand->type = X86_OP_REG;
        operand->size = width;
        operand->reg = regs[rm | ((rex & 1) << 3)];
        return offset;
    }

    operand->type = X86_OP_MEM;
    operand->size = width;
    operand->mem.segment = X86_REG_INVALID;
    operand->mem.base = X86_REG_INVALID;
    operand->mem.index = X86_REG_INVALID;
    operand->mem.scale = 1;
    operand->mem.disp = 0;

    bool disp32 = false;
    if (rm == 4) {
        uint8_t sib = code[offset++];
        uint8_t index = (sib >> 3) & 7;
        uint8_t base = sib & 7;
        operand->mem.scale = 1 << (sib >> 6);
        if (index != 4 || (rex & 2)) {
            operand->mem.index = mode == 64 ?
                ir1_hot_gpr64[index | ((rex & 2) << 2)] :
                ir1_hot_gpr32[index];
        }
        if (mod == 0 && base == 5) {
            disp32 = true;
        } else {
            operand->mem.base = mode == 64 ?
                ir1_hot_gpr64[base | ((rex & 1) << 3)] :
                ir1_hot_gpr32[base];
        }
    } else if (mod == 0 && rm == 5) {
        operand->mem.base = mode == 64 ? X86_REG_RIP : X86_REG_INVALID;
        disp32 = true;
    } else {
        operand->mem.base = mode == 64 ?
            ir1_hot_gpr64[rm | ((rex & 1) << 3)] : ir1_hot_gpr32[rm];
    }
    if (mod == 1) {
        operand->mem.disp = ir1_hot_read_simm(code + offset, 1);
        offset++;
    } else if (mod == 2 || disp32) {
        operand->mem.disp = ir1_hot_read_simm(code + offset, 4);
        offset += 4;
    }
    return offset;
}

static cs_insn *ir1_hot_decode(const uint8_t *code,
                                         uint64_t address, int ir1_num,
                                         void *pir1_base, int mode)
{
    static const unsigned int jcc[16] = {
        X86_INS_JO, X86_INS_JNO, X86_INS_JB, X86_INS_JAE,
        X86_INS_JE, X86_INS_JNE, X86_INS_JBE, X86_INS_JA,
        X86_INS_JS, X86_INS_JNS, X86_INS_JP, X86_INS_JNP,
        X86_INS_JL, X86_INS_JGE, X86_INS_JLE, X86_INS_JG,
    };
    /* Non-diet/debug builds retain textual and implicit-register metadata. */
#if !defined(CONFIG_CAPSTONE_DIET) || defined(CONFIG_LATX_DEBUG)
    return NULL;
#endif
    if ((mode != 32 && mode != 64) || !pir1_base) {
        return NULL;
    }
    uint8_t offset = 0;
    uint8_t rex = 0;
    if (mode == 32 && code[offset] >= 0x40 && code[offset] <= 0x4f) {
        return NULL;
    }
    if (mode == 64 && code[offset] >= 0x40 && code[offset] <= 0x4f) {
        rex = code[offset++];
    }
    uint8_t opcode = code[offset++];
    cs_insn *info;

    if (!rex && opcode >= 0x70 && opcode <= 0x7f) {
        info = ir1_decode_template_output(ir1_num, pir1_base);
        ir1_decode_output_init(info);
        info->id = jcc[opcode & 0xf];
        info->detail->x86.op_count = 1;
        info->detail->x86.operands[0].type = X86_OP_IMM;
        info->detail->x86.operands[0].size = mode == 64 ? 8 : 4;
        info->detail->x86.operands[0].imm = ir1_hot_relative_target(
            address, 2, mode, ir1_hot_read_simm(code + 1, 1));
        ir1_hot_finish(info, code, 2, address, 0, opcode, 0, mode);
        return info;
    }
    if (!rex && opcode == 0x0f && code[1] >= 0x80 && code[1] <= 0x8f) {
        info = ir1_decode_template_output(ir1_num, pir1_base);
        ir1_decode_output_init(info);
        info->id = jcc[code[1] & 0xf];
        info->detail->x86.op_count = 1;
        info->detail->x86.operands[0].type = X86_OP_IMM;
        info->detail->x86.operands[0].size = mode == 64 ? 8 : 4;
        info->detail->x86.operands[0].imm = ir1_hot_relative_target(
            address, 6, mode, ir1_hot_read_simm(code + 2, 4));
        ir1_hot_finish(info, code, 6, address, 0, 0x0f, code[1], mode);
        return info;
    }
    if (!rex && (opcode == 0xe8 || opcode == 0xe9 || opcode == 0xeb)) {
        uint8_t imm_size = opcode == 0xeb ? 1 : 4;
        uint8_t size = 1 + imm_size;
        info = ir1_decode_template_output(ir1_num, pir1_base);
        ir1_decode_output_init(info);
        info->id = opcode == 0xe8 ? X86_INS_CALL : X86_INS_JMP;
        info->detail->x86.op_count = 1;
        info->detail->x86.operands[0].type = X86_OP_IMM;
        info->detail->x86.operands[0].size = mode == 64 ? 8 : 4;
        info->detail->x86.operands[0].imm = ir1_hot_relative_target(
            address, size, mode, ir1_hot_read_simm(code + 1, imm_size));
        ir1_hot_finish(info, code, size, address, 0, opcode, 0, mode);
        return info;
    }
    if (!rex && (opcode == 0x90 || opcode == 0xc3)) {
        info = ir1_decode_template_output(ir1_num, pir1_base);
        ir1_decode_output_init(info);
        info->id = opcode == 0x90 ? X86_INS_NOP : X86_INS_RET;
        ir1_hot_finish(info, code, 1, address, 0, opcode, 0, mode);
        return info;
    }
    if (opcode >= 0x50 && opcode <= 0x5f && !(rex & 0x0e)) {
        unsigned int reg = (opcode & 7) | ((rex & 1) << 3);
        uint8_t width = mode == 64 ? 8 : 4;
        const x86_reg *regs = width == 8 ? ir1_hot_gpr64 : ir1_hot_gpr32;
        info = ir1_decode_template_output(ir1_num, pir1_base);
        ir1_decode_output_init(info);
        info->id = opcode < 0x58 ? X86_INS_PUSH : X86_INS_POP;
        info->detail->x86.op_count = 1;
        info->detail->x86.operands[0].type = X86_OP_REG;
        info->detail->x86.operands[0].size = width;
        info->detail->x86.operands[0].reg = regs[reg];
        ir1_hot_finish(info, code, offset, address, rex, opcode, 0, mode);
        return info;
    }
    if (opcode >= 0xb8 && opcode <= 0xbf) {
        uint8_t width = rex & 8 ? 8 : 4;
        uint64_t imm = 0;
        memcpy(&imm, code + offset, width);
        unsigned int reg = (opcode & 7) | ((rex & 1) << 3);
        const x86_reg *regs = width == 8 ? ir1_hot_gpr64 : ir1_hot_gpr32;
        info = ir1_decode_template_output(ir1_num, pir1_base);
        ir1_decode_output_init(info);
        info->id = X86_INS_MOV;
        info->detail->x86.op_count = 2;
        info->detail->x86.operands[0].type = X86_OP_REG;
        info->detail->x86.operands[0].size = width;
        info->detail->x86.operands[0].reg = regs[reg];
        info->detail->x86.operands[1].type = X86_OP_IMM;
        info->detail->x86.operands[1].size = width;
        info->detail->x86.operands[1].imm = imm;
        ir1_hot_finish(info, code, offset + width, address, rex, opcode, 0,
                       mode);
        return info;
    }

    if (opcode == 0x83) {
        uint8_t modrm = code[offset];
        uint8_t group = (modrm >> 3) & 7;
        static const unsigned int group_ids[8] = {
            X86_INS_ADD, X86_INS_OR, X86_INS_ADC, X86_INS_SBB,
            X86_INS_AND, X86_INS_SUB, X86_INS_XOR, X86_INS_CMP,
        };
        uint8_t width = rex & 8 ? 8 : 4;
        cs_x86_op rm_operand = { 0 };
        uint8_t end = ir1_hot_decode_rm(code, offset, rex, width, mode,
                                        &rm_operand);
        info = ir1_decode_template_output(ir1_num, pir1_base);
        ir1_decode_output_init(info);
        info->id = group_ids[group];
        info->detail->x86.modrm = modrm;
        info->detail->x86.op_count = 2;
        info->detail->x86.operands[0] = rm_operand;
        info->detail->x86.operands[1].type = X86_OP_IMM;
        info->detail->x86.operands[1].size = width;
        int64_t imm = ir1_hot_read_simm(code + end, 1);
        bool logical = group == 1 || group == 4 || group == 6;
        info->detail->x86.operands[1].imm = width == 4 && logical ?
                                     (uint32_t)imm : imm;
        ir1_hot_finish(info, code, end + 1, address, rex, opcode, 0, mode);
        return info;
    }

    uint8_t modrm = code[offset];
    bool reverse = false;
    bool lea = false;
    unsigned int id;
    switch (opcode) {
    case 0x89:
        id = X86_INS_MOV;
        break;
    case 0x8b:
        id = X86_INS_MOV;
        reverse = true;
        break;
    case 0x8d:
        id = X86_INS_LEA;
        reverse = true;
        lea = true;
        break;
    case 0x85:
        id = X86_INS_TEST;
        break;
    case 0x31:
        id = X86_INS_XOR;
        break;
    case 0x33:
        id = X86_INS_XOR;
        reverse = true;
        break;
    case 0x39:
        id = X86_INS_CMP;
        break;
    case 0x3b:
        id = X86_INS_CMP;
        reverse = true;
        break;
    case 0x01:
        id = X86_INS_ADD;
        break;
    case 0x03:
        id = X86_INS_ADD;
        reverse = true;
        break;
    case 0x29:
        id = X86_INS_SUB;
        break;
    case 0x2b:
        id = X86_INS_SUB;
        reverse = true;
        break;
    case 0x21:
        id = X86_INS_AND;
        break;
    case 0x23:
        id = X86_INS_AND;
        reverse = true;
        break;
    case 0x09:
        id = X86_INS_OR;
        break;
    case 0x0b:
        id = X86_INS_OR;
        reverse = true;
        break;
    default: return NULL;
    }
    unsigned int reg_index = ((modrm >> 3) & 7) | ((rex & 4) << 1);
    uint8_t width = rex & 8 ? 8 : 4;
    const x86_reg *regs = width == 8 ? ir1_hot_gpr64 : ir1_hot_gpr32;
    cs_x86_op rm_operand = { 0 };
    uint8_t end = ir1_hot_decode_rm(code, offset, rex, width, mode,
                                    &rm_operand);
    if (lea && rm_operand.type != X86_OP_MEM) {
        return NULL;
    }
    /* Leave memory TEST's backend-specific access metadata to Capstone. */
    if (id == X86_INS_TEST && rm_operand.type == X86_OP_MEM) {
        return NULL;
    }
    cs_x86_op reg_operand = { 0 };
    reg_operand.type = X86_OP_REG;
    reg_operand.size = width;
    reg_operand.reg = regs[reg_index];
    info = ir1_decode_template_output(ir1_num, pir1_base);
    ir1_decode_output_init(info);
    info->id = id;
    info->detail->x86.modrm = modrm;
    info->detail->x86.op_count = 2;
    info->detail->x86.operands[0] = reverse ? reg_operand : rm_operand;
    info->detail->x86.operands[1] = reverse ? rm_operand : reg_operand;
    ir1_hot_finish(info, code, end, address, rex, opcode, 0, mode);
    return info;
}

static bool ir1_decode_template_is_relative_imm(unsigned int id)
{
    switch (id) {
    case X86_INS_CALL:
    case X86_INS_JMP:
    case X86_INS_JO:
    case X86_INS_JNO:
    case X86_INS_JB:
    case X86_INS_JAE:
    case X86_INS_JE:
    case X86_INS_JNE:
    case X86_INS_JBE:
    case X86_INS_JA:
    case X86_INS_JS:
    case X86_INS_JNS:
    case X86_INS_JP:
    case X86_INS_JNP:
    case X86_INS_JL:
    case X86_INS_JGE:
    case X86_INS_JLE:
    case X86_INS_JG:
    case X86_INS_JCXZ:
    case X86_INS_JECXZ:
    case X86_INS_JRCXZ:
    case X86_INS_LOOP:
    case X86_INS_LOOPE:
    case X86_INS_LOOPNE:
    case X86_INS_XBEGIN:
        return true;
    default:
        return false;
    }
}

static uint8_t ir1_decode_template_relative_imm_mask(const cs_insn *info)
{
    uint8_t mask = 0;

    if (!ir1_decode_template_is_relative_imm(info->id)) {
        return 0;
    }
    for (int i = 0; i < info->detail->x86.op_count && i < 8; i++) {
        if (info->detail->x86.operands[i].type == X86_OP_IMM) {
            mask |= 1u << i;
        }
    }
    return mask;
}

typedef struct Ir1DecodeDisp {
    uint8_t offset;
    uint8_t size;
    bool is_signed;
} Ir1DecodeDisp;

#ifdef CONFIG_CAPSTONE_DIET
static Ir1DecodeDisp ir1_decode_template_disp_info(const uint8_t *code,
                                                    uint8_t insn_size,
                                                    const cs_x86 *x86)
{
    Ir1DecodeDisp info = {
        .offset = x86->encoding.disp_offset,
        .size = x86->encoding.disp_size,
        .is_signed = false,
    };
    uint8_t modrm_offset = x86->encoding.modrm_offset;

    if (!x86->encoding.disp_size || !modrm_offset || x86->opcode[0] == 0x62 ||
        modrm_offset + 1 >= insn_size) {
        return info;
    }
    uint8_t modrm = code[modrm_offset];
    uint8_t mod = modrm >> 6;
    uint8_t rm = modrm & 7;
    bool sib = mod != 3 && rm == 4 && x86->addr_size != 2;
    uint8_t offset = modrm_offset + 1 + sib;

    info.offset = 0;
    info.size = 0;
    info.is_signed = true;
    if (mod == 0 && x86->addr_size == 2 && rm == 6) {
        info.offset = offset;
        info.size = 2;
    } else if (mod == 0 && x86->addr_size != 2 && rm == 5) {
        info.offset = offset;
        info.size = 4;
    } else if (mod == 0 && sib && (code[modrm_offset + 1] & 7) == 5) {
        info.offset = offset;
        info.size = 4;
    } else if (mod == 1) {
        info.offset = offset;
        info.size = 1;
    } else if (mod == 2) {
        info.offset = offset;
        info.size = x86->addr_size == 2 ? 2 : 4;
    }
    return info;
}

#endif

static uint64_t ir1_decode_template_read_value(const uint8_t *code,
                                               uint8_t offset, uint8_t size)
{
    if (!offset || !size) {
        return 0;
    }
    switch (size) {
    case 1:
        return code[offset];
    case 2: {
        uint16_t value;

        memcpy(&value, code + offset, sizeof(value));
        return value;
    }
    case 4: {
        uint32_t value;

        memcpy(&value, code + offset, sizeof(value));
        return value;
    }
    case 8: {
        uint64_t value;

        memcpy(&value, code + offset, sizeof(value));
        return value;
    }
    default: {
        uint64_t value = 0;

        memcpy(&value, code + offset, size);
        return value;
    }
    }
}

static int64_t ir1_decode_template_signed_value(uint64_t value, uint8_t size)
{
    if (size < 8 && (value & (UINT64_C(1) << (size * 8 - 1)))) {
        value |= ~(UINT64_MAX >> ((8 - size) * 8));
    }
    return (int64_t)value;
}

static void ir1_decode_template_make_key(const uint8_t *code,
                                         uint8_t insn_size,
                                         const cs_x86 *x86,
                                         uint8_t relative_imm_mask,
                                         uint8_t imm_transform,
                                         uint8_t key[16])
{
    memset(key, 0, 16);
    memcpy(key, code, insn_size);
#ifdef CONFIG_CAPSTONE_DIET
    Ir1DecodeDisp info = ir1_decode_template_disp_info(code, insn_size, x86);

    if (info.offset && info.size && info.offset + info.size <= insn_size) {
        memset(key + info.offset, 0, info.size);
    }
    if (imm_transform != IR1_DECODE_IMM_NONE &&
        imm_transform != IR1_DECODE_IMM_RELATIVE_EXACT &&
        x86->encoding.imm_offset && x86->encoding.imm_size &&
        x86->encoding.imm_offset + x86->encoding.imm_size <= insn_size) {
        memset(key + x86->encoding.imm_offset, 0,
               x86->encoding.imm_size);
    }
#else
    (void)relative_imm_mask;
    (void)imm_transform;
    (void)x86;
#endif
}

static inline void ir1_decode_template_load_words(const uint8_t bytes[15],
                                                  uint64_t words[2])
{
    words[1] = 0;
    memcpy(&words[0], bytes, sizeof(words[0]));
    memcpy(&words[1], bytes + sizeof(words[0]), 7);
}

#ifdef CONFIG_CAPSTONE_DIET
static uint64_t ir1_decode_template_operand_mask(unsigned int size)
{
    if (!size) {
        return UINT64_MAX;
    }
    if (size >= 8) {
        return UINT64_MAX;
    }
    return (UINT64_C(1) << (size * 8)) - 1;
}

static int64_t ir1_decode_template_predict_imm(int mode, unsigned int id,
                                               unsigned int op_size,
                                               uint8_t imm_size,
                                               uint64_t raw)
{
    int64_t signed_raw = ir1_decode_template_signed_value(raw, imm_size);

    if (id == X86_INS_PUSH && mode == 64 && imm_size == 4) {
        return signed_raw;
    }
    if (op_size == 1) {
        return raw & UINT8_MAX;
    }
    if (imm_size == op_size) {
        return raw;
    }
    switch (id) {
    case X86_INS_RET:
    case X86_INS_RETF:
        return raw & UINT16_MAX;
    case X86_INS_AND:
    case X86_INS_OR:
    case X86_INS_XOR:
        return signed_raw & ir1_decode_template_operand_mask(op_size);
    case X86_INS_IN:
    case X86_INS_OUT:
    case X86_INS_INT:
        return raw & UINT8_MAX;
    case X86_INS_MOVABS:
        return raw;
    default:
        return signed_raw;
    }
}

static uint8_t ir1_decode_template_imm_transform(const uint8_t *code,
                                                 int mode,
                                                 const cs_insn *info,
                                                 uint8_t *imm_operand)
{
    const cs_x86 *x86 = &info->detail->x86;
    uint8_t relative_mask = ir1_decode_template_relative_imm_mask(info);
    int imm_count = 0;
    int imm_index = -1;
    uint8_t offset = x86->encoding.imm_offset;
    uint8_t size = x86->encoding.imm_size;

    *imm_operand = 0;
    if (relative_mask) {
        return IR1_DECODE_IMM_RELATIVE;
    }
    for (int i = 0; i < x86->op_count && i < 8; i++) {
        if (x86->operands[i].type == X86_OP_IMM) {
            imm_count++;
            imm_index = i;
        }
    }
    if (imm_count != 1 || !offset || !size || size > 8 ||
        offset + size > info->size) {
        return IR1_DECODE_IMM_NONE;
    }
    *imm_operand = imm_index;

    uint64_t raw = ir1_decode_template_read_value(code, offset, size);
    int64_t predicted = ir1_decode_template_predict_imm(
        mode, info->id, x86->operands[imm_index].size, size, raw);

    if (predicted != x86->operands[imm_index].imm) {
        return IR1_DECODE_IMM_NONE;
    }
    if (info->id == X86_INS_PUSH && mode == 64 && size == 4) {
        return IR1_DECODE_IMM_SIGNED;
    }
    if (x86->operands[imm_index].size == 1 || info->id == X86_INS_IN ||
        info->id == X86_INS_OUT || info->id == X86_INS_INT) {
        return IR1_DECODE_IMM_RAW8;
    }
    if (info->id == X86_INS_RET || info->id == X86_INS_RETF) {
        return IR1_DECODE_IMM_RAW16;
    }
    if (size == x86->operands[imm_index].size || info->id == X86_INS_MOVABS) {
        return IR1_DECODE_IMM_RAW;
    }
    if (info->id == X86_INS_AND || info->id == X86_INS_OR ||
        info->id == X86_INS_XOR) {
        return IR1_DECODE_IMM_SIGNED_MASK;
    }
    return IR1_DECODE_IMM_SIGNED;
}
#endif

static void ir1_decode_template_relocate(cs_insn *info,
                                         uint8_t relative_imm_mask,
                                         uint64_t template_address,
                                         uint64_t address, int mode)
{
    uint64_t delta = address - template_address;

    for (int i = 0; i < info->detail->x86.op_count && i < 8; i++) {
        cs_x86_op *operand = &info->detail->x86.operands[i];
        uint64_t mask;

        if (!(relative_imm_mask & (1u << i))) {
            continue;
        }
        if (operand->size == 2) {
            mask = UINT16_MAX;
        } else if (mode != 64 && operand->size == 4) {
            mask = UINT32_MAX;
        } else if (mode == 32) {
            mask = UINT32_MAX;
        } else {
            mask = UINT64_MAX;
        }
        operand->imm = (int64_t)(((uint64_t)operand->imm + delta) & mask);
    }
}

static void ir1_decode_template_relocate_encoded(
        cs_insn *info, uint8_t relative_imm_mask, const uint8_t *code,
        uint64_t address, int mode)
{
    const cs_x86 *x86 = &info->detail->x86;
    uint64_t raw = ir1_decode_template_read_value(
        code, x86->encoding.imm_offset, x86->encoding.imm_size);
    int64_t signed_raw = ir1_decode_template_signed_value(
        raw, x86->encoding.imm_size);

    for (int i = 0; i < x86->op_count && i < 8; i++) {
        cs_x86_op *operand = &info->detail->x86.operands[i];
        uint64_t mask;

        if (!(relative_imm_mask & (1u << i))) {
            continue;
        }
        if (operand->size == 2) {
            mask = UINT16_MAX;
        } else if (mode != 64 && operand->size == 4) {
            mask = UINT32_MAX;
        } else if (mode == 32) {
            mask = UINT32_MAX;
        } else {
            mask = UINT64_MAX;
        }
        operand->imm = (int64_t)(((uint64_t)(address + info->size +
                                           signed_raw)) & mask);
    }
}

#ifdef CONFIG_CAPSTONE_DIET
static void ir1_decode_template_apply_imm(cs_insn *info,
                                          uint8_t imm_transform,
                                          const uint8_t *code,
                                          uint8_t imm_operand)
{
    cs_x86 *x86 = &info->detail->x86;
    cs_x86_op *operand;
    uint64_t raw = ir1_decode_template_read_value(
        code, x86->encoding.imm_offset, x86->encoding.imm_size);
    int64_t signed_raw = ir1_decode_template_signed_value(
        raw, x86->encoding.imm_size);
    int64_t value;

    if (imm_operand >= x86->op_count ||
        x86->operands[imm_operand].type != X86_OP_IMM) {
        return;
    }
    operand = &x86->operands[imm_operand];
    switch (imm_transform) {
    case IR1_DECODE_IMM_RAW:
        value = (int64_t)raw;
        break;
    case IR1_DECODE_IMM_SIGNED:
        value = signed_raw;
        break;
    case IR1_DECODE_IMM_SIGNED_MASK:
        operand->imm = signed_raw &
            ir1_decode_template_operand_mask(operand->size);
        return;
    case IR1_DECODE_IMM_RAW8:
        value = raw & UINT8_MAX;
        break;
    case IR1_DECODE_IMM_RAW16:
        value = raw & UINT16_MAX;
        break;
    default:
        return;
    }
    operand->imm = value;
}

#endif

static int ir1_decode_template_lru_age(uint8_t state, int way)
{
    return (state >> (way * 2)) & 3;
}

static void ir1_decode_template_lru_touch(size_t set, int way)
{
    uint8_t state = ir1_decode_template_policy[set];
    int age = ir1_decode_template_lru_age(state, way);
    uint8_t updated = 0;

    for (size_t i = 0; i < ir1_decode_template_ways(); i++) {
        int current = ir1_decode_template_lru_age(state, i);

        if (i != way && current < age) {
            current++;
        }
        if (i == way) {
            current = 0;
        }
        updated |= current << (i * 2);
    }
    ir1_decode_template_policy[set] = updated;
}

static void ir1_decode_template_lru_insert(size_t set, int way)
{
    uint8_t state = ir1_decode_template_policy[set];
    uint8_t updated = 0;

    for (size_t i = 0; i < ir1_decode_template_ways(); i++) {
        int current = ir1_decode_template_lru_age(state, i);

        if (i == way) {
            current = 0;
        } else if (current < 3) {
            current++;
        }
        updated |= current << (i * 2);
    }
    ir1_decode_template_policy[set] = updated;
}

static uint32_t ir1_decode_template_random(void)
{
    uint32_t value = ir1_decode_template_random_state;

    if (!value) {
        value = (uint32_t)(uintptr_t)&ir1_decode_template_random_state ^
                0x9e3779b9u;
    }
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    ir1_decode_template_random_state = value;
    return value;
}

static inline __attribute__((always_inline)) bool ir1_decode_template_matches(
        const Ir1DecodeTag *tag, const uint64_t code_words[2],
        uint32_t hash_tag, int mode, size_t code_size)
{
    if (tag->hash_tag != hash_tag || tag->mode != mode ||
        tag->size > code_size) {
        return false;
    }
    return !(((code_words[0] ^ tag->key.words[0]) & tag->byte_mask[0]) ||
             ((code_words[1] ^ tag->key.words[1]) & tag->byte_mask[1]));
}

static inline __attribute__((always_inline)) bool
ir1_decode_fast_entry_matches(
        const Ir1DecodeFastEntry *fast, const uint64_t code_words[2],
        uint32_t hash_tag, int mode, size_t code_size)
{
#if IR1_DECODE_COMPACT_FAST_TEMPLATE
    const Ir1DecodeTemplateKey *key = &fast->entry.key;
    const uint64_t *byte_mask = fast->entry.byte_mask;
#else
    const Ir1DecodeTemplateKey *key = &fast->entry.tag.key;
    const uint64_t *byte_mask = fast->entry.tag.byte_mask;
#endif
    if (!fast->valid || fast->hash_tag != hash_tag ||
        fast->mode != mode || fast->size > code_size) {
        return false;
    }
    return !(((code_words[0] ^ key->words[0]) & byte_mask[0]) ||
             ((code_words[1] ^ key->words[1]) & byte_mask[1]));
}

static Ir1DecodeFastEntry *
ir1_decode_fast_template_fill(size_t index, const uint8_t *code)
{
    if (!ir1_decode_fast_templates) {
        Ir1DecodeStats *stats = ir1_decode_profile_stats_get();
        size_t fast_bytes =
            sizeof(Ir1DecodeFastEntry) * ir1_decode_fast_cache_entries();
        size_t fast_alloc_bytes =
            ROUND_UP(fast_bytes, IR1_DECODE_FAST_CACHE_ALIGN);

        ir1_decode_fast_templates = qemu_memalign(
            IR1_DECODE_FAST_CACHE_ALIGN, fast_alloc_bytes);
        memset(ir1_decode_fast_templates, 0, fast_alloc_bytes);
        qemu_madvise(ir1_decode_fast_templates, fast_alloc_bytes,
                     QEMU_MADV_HUGEPAGE);
#if IR1_DECODE_FAST_CACHE_WAYS > 1
        ir1_decode_fast_policy =
            g_new0(uint8_t, ir1_decode_fast_cache_sets());
#endif
        g_private_set(&ir1_decode_fast_templates_owner,
                      ir1_decode_fast_templates);
#if IR1_DECODE_FAST_CACHE_WAYS > 1
        g_private_set(&ir1_decode_fast_policy_owner,
                      ir1_decode_fast_policy);
#endif
        if (stats) {
            stats->fast_cache_allocations++;
            stats->fast_cache_alloc_bytes += fast_alloc_bytes +
#if IR1_DECODE_FAST_CACHE_WAYS > 1
                sizeof(uint8_t) * ir1_decode_fast_cache_sets();
#else
                0;
#endif
        }
    }
    Ir1DecodeTag *source = &ir1_decode_tags[index];
    Ir1DecodePayload *payload = &ir1_decode_payloads[index];
    size_t set = source->hash_tag & (ir1_decode_fast_cache_sets() - 1);
    size_t base = set * IR1_DECODE_FAST_CACHE_WAYS;
    int way = -1;

    for (size_t candidate = 0;
         candidate < IR1_DECODE_FAST_CACHE_WAYS; candidate++) {
        Ir1DecodeFastEntry *fast =
            &ir1_decode_fast_templates[base + candidate];
#if IR1_DECODE_COMPACT_FAST_TEMPLATE
        const Ir1DecodeTemplateKey *key = &fast->entry.key;
        const uint64_t *byte_mask = fast->entry.byte_mask;
#else
        const Ir1DecodeTemplateKey *key = &fast->entry.tag.key;
        const uint64_t *byte_mask = fast->entry.tag.byte_mask;
#endif
        if (fast->valid && fast->hash_tag == source->hash_tag &&
            fast->mode == source->mode && fast->size == source->size &&
            key->words[0] == source->key.words[0] &&
            key->words[1] == source->key.words[1] &&
            byte_mask[0] == source->byte_mask[0] &&
            byte_mask[1] == source->byte_mask[1]) {
            way = candidate;
            break;
        }
        if (!fast->valid && way < 0) {
            way = candidate;
        }
    }
    if (way < 0) {
#if IR1_DECODE_FAST_CACHE_WAYS > 1
        way = ir1_decode_fast_policy[set];
#else
        way = 0;
#endif
    }

    Ir1DecodeFastEntry *fast = &ir1_decode_fast_templates[base + way];
#if IR1_DECODE_COMPACT_FAST_TEMPLATE
    if (payload->x86.op_count > IR1_DECODE_FAST_MAX_OPERANDS) {
        return NULL;
    }
    fast->entry.id = payload->decoded.id;
    fast->entry.relative_imm_mask = source->relative_imm_mask;
    fast->entry.imm_transform = source->imm_transform;
    fast->entry.op_count = payload->x86.op_count;
    Ir1DecodeDisp disp_info = ir1_decode_template_disp_info(
        code, source->size, &payload->x86);
    fast->entry.disp_offset = disp_info.offset;
    fast->entry.disp_size = disp_info.size;
    fast->entry.disp_signed = disp_info.is_signed;
    fast->entry.byte_mask[0] = source->byte_mask[0];
    fast->entry.byte_mask[1] = source->byte_mask[1];
    fast->entry.key = source->key;
    ir1_decode_compact_x86_from_cs_x86(&fast->entry, &payload->x86);
    memcpy(&fast->entry.encoding, &payload->x86.encoding,
           sizeof(fast->entry.encoding));
#else
    fast->entry.tag = *source;
    fast->entry.payload = *payload;
#endif
    fast->valid = true;
    fast->hash_tag = source->hash_tag;
    fast->size = source->size;
    fast->mode = source->mode;
#if IR1_DECODE_FAST_CACHE_WAYS > 1
    ir1_decode_fast_policy[set] =
        (way + 1) & (IR1_DECODE_FAST_CACHE_WAYS - 1);
#endif
    return fast;
}

static inline __attribute__((always_inline)) void
ir1_decode_template_copy_x86(cs_insn *output_info, const cs_x86 *template)
{
    cs_x86 *output = &output_info->detail->x86;
    uint8_t max_operands = G_N_ELEMENTS(template->operands);
    uint8_t old_op_count = MIN(output_info->bytes[IR1_DECODE_OP_COUNT_SHADOW],
                               max_operands);
    uint8_t op_count = MIN(template->op_count, max_operands);

    memcpy(output, template, offsetof(cs_x86, operands) +
           sizeof(template->operands[0]) * op_count);
    if (old_op_count > op_count) {
        memset(output->operands + op_count, 0,
               sizeof(template->operands[0]) *
               (old_op_count - op_count));
    }
    memcpy(&output->encoding, &template->encoding,
           sizeof(output->encoding));
    output_info->bytes[IR1_DECODE_OP_COUNT_SHADOW] = op_count;
}

#if IR1_DECODE_COMPACT_FAST_TEMPLATE
static inline __attribute__((always_inline)) void ir1_decode_fast_copy_x86(
        cs_insn *output_info, const Ir1DecodeFastTemplate *template)
{
    cs_x86 *output = &output_info->detail->x86;
    uint8_t max_operands = G_N_ELEMENTS(output->operands);
    uint8_t old_op_count = MIN(output_info->bytes[IR1_DECODE_OP_COUNT_SHADOW],
                               max_operands);
    uint8_t op_count = template->op_count;

#if defined(__loongarch__) || defined(__loongarch64)
    Ir1DecodeVec16 *dest = (Ir1DecodeVec16 *)output;
    const Ir1DecodeVec16 *source =
        (const Ir1DecodeVec16 *)&template->x86;

    dest[0] = source[0];
    dest[1] = source[1];
    dest[2] = source[2];
    dest[3] = source[3];
#else
    memcpy(output, &template->x86, sizeof(template->x86));
#endif
    output->op_count = template->op_count;
    for (int i = 0; i < op_count; i++) {
        cs_x86_op *dest = &output->operands[i];
        const Ir1DecodeCompactOp *source = &template->operands[i];

#if defined(__loongarch__) || defined(__loongarch64)
        Ir1DecodeVec16 *dest_payload = (Ir1DecodeVec16 *)&dest->mem;
        const Ir1DecodeVec16 *source_payload =
            (const Ir1DecodeVec16 *)source->payload;

        dest_payload[0] = source_payload[0];
        *(uint64_t *)((uint8_t *)&dest->mem + 16) =
            *(const uint64_t *)((const uint8_t *)source->payload + 16);
#else
        memcpy(&dest->mem, source->payload, sizeof(source->payload));
#endif
        dest->avx_bcast = source->avx_bcast;
        dest->type = source->type;
        dest->size = source->size;
        dest->access = source->access;
        dest->avx_zero_opmask = source->avx_zero_opmask;
    }
    if (old_op_count > op_count) {
        memset(output->operands + op_count, 0,
               sizeof(output->operands[0]) *
               (old_op_count - op_count));
    }
    memcpy(&output->encoding, &template->encoding,
           sizeof(output->encoding));
    output_info->bytes[IR1_DECODE_OP_COUNT_SHADOW] = op_count;
}
#endif

static inline __attribute__((always_inline)) cs_insn *
ir1_decode_template_finish_output(
        cs_insn *output, const uint8_t *code, uint64_t address, int mode,
        uint8_t relative_imm_mask, uint64_t template_address,
        uint8_t imm_transform, uint8_t imm_operand, Ir1DecodeStats *stats,
        int mode_index, const Ir1DecodeDisp *precomputed_disp)
{
#ifdef CONFIG_CAPSTONE_DIET
    const Ir1DecodeDisp *disp_info = precomputed_disp;
    Ir1DecodeDisp live_disp_info;

    if (!disp_info && output->detail->x86.encoding.disp_size) {
        live_disp_info = ir1_decode_template_disp_info(
            code, output->size, &output->detail->x86);
        disp_info = &live_disp_info;
    }
    if (disp_info && disp_info->size) {
        uint64_t raw_disp = ir1_decode_template_read_value(
            code, disp_info->offset, disp_info->size);
        int64_t disp = disp_info->is_signed ?
            ir1_decode_template_signed_value(raw_disp, disp_info->size) :
            (int64_t)raw_disp;

        output->detail->x86.disp = disp;
        for (int i = 0; i < output->detail->x86.op_count; i++) {
            cs_x86_op *operand = &output->detail->x86.operands[i];

            if (operand->type == X86_OP_MEM) {
                operand->mem.disp = disp;
            }
        }
    }
#endif
    if (imm_transform == IR1_DECODE_IMM_RELATIVE) {
        ir1_decode_template_relocate_encoded(output, relative_imm_mask, code,
                                             address, mode);
        if (stats && mode_index >= 0) {
            stats->cache_relative_hits[mode_index]++;
        }
    } else if (imm_transform == IR1_DECODE_IMM_RELATIVE_EXACT) {
        ir1_decode_template_relocate(output, relative_imm_mask,
                                     template_address, address, mode);
        if (stats && mode_index >= 0) {
            stats->cache_relative_hits[mode_index]++;
        }
#ifdef CONFIG_CAPSTONE_DIET
    } else if (imm_transform) {
        ir1_decode_template_apply_imm(output, imm_transform, code,
                                      imm_operand);
        if (stats && mode_index >= 0) {
            stats->cache_imm_hits[mode_index]++;
        }
#endif
    }
    return output;
}

static cs_insn *ir1_decode_template_emit(const Ir1DecodeTag *tag,
                                         const Ir1DecodePayload *payload,
                                         const uint8_t *code, uint64_t address,
                                         int ir1_num, void *pir1_base,
                                         int mode, Ir1DecodeStats *stats,
                                         int mode_index)
{
    cs_insn *output = ir1_decode_template_output(ir1_num, pir1_base);
#if defined(CONFIG_CAPSTONE_DIET) && !defined(CONFIG_LATX_PRINT)
    output->id = payload->decoded.id;
    output->size = payload->decoded.size;
    output->mnemonic[0] = '\0';
    output->op_str[0] = '\0';
#else
    memcpy(output, &payload->decoded, sizeof(*output));
#endif
    output->detail = IR1_INST_DETAIL(output);
#ifdef CONFIG_CAPSTONE_DIET
    ir1_decode_template_copy_x86(output, &payload->x86);
#else
    memcpy(output->detail, &payload->detail, sizeof(payload->detail));
#endif
    output->address = address;
    memcpy(output->bytes, code, output->size);
    return ir1_decode_template_finish_output(
        output, code, address, mode, tag->relative_imm_mask,
        payload->decoded.address, tag->imm_transform,
        tag->key.bytes[15], stats, mode_index, NULL);
}

static inline __attribute__((always_inline)) cs_insn *
ir1_decode_fast_template_emit(
        const Ir1DecodeFastEntry *fast, const uint8_t *code,
        const uint64_t code_words[2],
        uint64_t address, int ir1_num, void *pir1_base, int mode,
        Ir1DecodeStats *stats, int mode_index)
{
#if IR1_DECODE_COMPACT_FAST_TEMPLATE
    const Ir1DecodeFastTemplate *entry = &fast->entry;
    const uint8_t *cached_code = (const uint8_t *)code_words;
    cs_insn *output = ir1_decode_template_output(ir1_num, pir1_base);

    output->id = entry->id;
    output->size = fast->size;
    output->mnemonic[0] = '\0';
    output->op_str[0] = '\0';
    output->detail = IR1_INST_DETAIL(output);
    ir1_decode_fast_copy_x86(output, entry);
    output->address = address;
    memcpy(output->bytes, &code_words[0], sizeof(code_words[0]));
    memcpy(output->bytes + sizeof(code_words[0]), &code_words[1],
           sizeof(code_words[1]));
    Ir1DecodeDisp disp_info = {
        .offset = entry->disp_offset,
        .size = entry->disp_size,
        .is_signed = entry->disp_signed,
    };
    return ir1_decode_template_finish_output(
        output, cached_code, address, mode, entry->relative_imm_mask,
        0, entry->imm_transform,
        entry->key.bytes[15], stats, mode_index, &disp_info);
#else
    return ir1_decode_template_emit(&fast->entry.tag, &fast->entry.payload,
                                    code, address, ir1_num, pir1_base, mode,
                                    stats, mode_index);
#endif
}

typedef struct Ir1DecodeLookupContext {
    uint32_t hash_tag;
    size_t code_size;
    uint64_t code_words[2];
    Ir1DecodeDistribution *distribution;
    size_t distribution_set;
    Ir1DecodePageProfile *page;
} Ir1DecodeLookupContext;

static void ir1_decode_lookup_context_init(
        Ir1DecodeLookupContext *context, const uint8_t *code, size_t code_size,
        uint64_t address, int mode, bool record_lookup)
{
    context->distribution = ir1_decode_distribution_get();
    context->code_size = code_size;
    context->hash_tag = ir1_decode_template_hash_value(code, code_size, mode);
    context->distribution_set = context->hash_tag &
                                (ir1_decode_template_sets() - 1);
    context->page = context->distribution ?
        ir1_decode_distribution_page(context->distribution, address) : NULL;
    ir1_decode_template_load_words(code, context->code_words);

    if (record_lookup && context->distribution) {
        context->distribution->set_profiles[context->distribution_set].lookups++;
        context->page->lookups++;
    }
}

static void ir1_decode_lookup_record_miss(Ir1DecodeLookupContext *context)
{
    if (context->distribution) {
        context->distribution->set_profiles[context->distribution_set].misses++;
        context->page->misses++;
    }
}

static cs_insn *ir1_decode_fast_lookup_stage(
        Ir1DecodeLookupContext *context, const uint8_t *code, uint64_t address,
        int ir1_num, void *pir1_base, int mode, Ir1DecodeStats *stats,
        int mode_index)
{
    if (!ir1_decode_fast_templates) {
        return NULL;
    }

    size_t set = context->hash_tag & (ir1_decode_fast_cache_sets() - 1);
    size_t base = set * IR1_DECODE_FAST_CACHE_WAYS;

    for (size_t way = 0; way < IR1_DECODE_FAST_CACHE_WAYS; way++) {
        Ir1DecodeFastEntry *fast =
            &ir1_decode_fast_templates[base + way];

        if (!ir1_decode_fast_entry_matches(fast, context->code_words,
                                           context->hash_tag, mode,
                                           context->code_size)) {
            continue;
        }
#if IR1_DECODE_FAST_CACHE_WAYS > 1
        ir1_decode_fast_policy[set] =
            (way + 1) & (IR1_DECODE_FAST_CACHE_WAYS - 1);
#endif
        ir1_decode_last_fast_hit = true;
        if (stats && mode_index >= 0) {
            stats->fast_cache_hits[mode_index]++;
            if (context->distribution) {
                context->distribution->opcode_hits[mode_index][code[0]]++;
            }
        }
        if (context->distribution) {
            context->distribution->set_profiles[context->distribution_set]
                .hits++;
            context->distribution->fast_way_hits++;
            context->page->hits++;
        }
        return ir1_decode_fast_template_emit(fast, code, context->code_words,
                                             address, ir1_num, pir1_base, mode,
                                             stats, mode_index);
    }
    return NULL;
}

static cs_insn *ir1_decode_main_lookup_stage(
        Ir1DecodeLookupContext *context, const uint8_t *code, uint64_t address,
        int ir1_num, void *pir1_base, int mode, Ir1DecodeStats *stats,
        int mode_index)
{
    if (!ir1_decode_tags || !ir1_decode_payloads) {
        return NULL;
    }

    size_t set = context->distribution_set;
    size_t ways = ir1_decode_template_ways();
    size_t base = set * ways;

    for (size_t way = 0; way < ways; way++) {
        size_t index = base + way;
        Ir1DecodeTag *tag = &ir1_decode_tags[index];

        if (!ir1_decode_template_valid[index] ||
            !ir1_decode_template_matches(tag, context->code_words,
                                         context->hash_tag, mode,
                                         context->code_size)) {
            continue;
        }
        if (option_ir1_cache_replace == LATX_IR1_CACHE_REPLACE_LRU) {
            ir1_decode_template_lru_touch(set, way);
        }
        if (context->distribution) {
            uint16_t *reuse = &context->distribution->entry_reuse[index];

            context->distribution->set_profiles[context->distribution_set]
                .hits++;
            context->distribution->way_hits[way]++;
            context->page->hits++;
            if (mode_index >= 0) {
                context->distribution->opcode_hits[mode_index][code[0]]++;
            }
            if (*reuse < UINT16_MAX - 1) {
                *reuse += 1;
            } else if (*reuse != UINT16_MAX) {
                *reuse = UINT16_MAX - 1;
            }
        }
        Ir1DecodeFastEntry *fast = ir1_decode_fast_template_fill(index, code);

        if (fast) {
            return ir1_decode_fast_template_emit(fast, code,
                                                 context->code_words, address,
                                                 ir1_num, pir1_base, mode,
                                                 stats, mode_index);
        }
        return ir1_decode_template_emit(
            tag, &ir1_decode_payloads[index], code, address, ir1_num,
            pir1_base, mode, stats, mode_index);
    }
    return NULL;
}

static cs_insn *ir1_decode_template_lookup_impl(
        const uint8_t *code, size_t code_size, uint64_t address,
        int ir1_num, void *pir1_base, int mode, Ir1DecodeStats *stats,
        int mode_index)
{
    if (!pir1_base) {
        return NULL;
    }

    Ir1DecodeLookupContext context;
    cs_insn *info;

    ir1_decode_last_fast_hit = false;
    ir1_decode_lookup_context_init(&context, code, code_size, address, mode,
                                   true);
    info = ir1_decode_fast_lookup_stage(&context, code, address, ir1_num,
                                        pir1_base, mode, stats, mode_index);
    if (info) {
        return info;
    }
    info = ir1_decode_main_lookup_stage(&context, code, address, ir1_num,
                                        pir1_base, mode, stats, mode_index);
    if (!info) {
        ir1_decode_lookup_record_miss(&context);
    }
    return info;
}

static G_GNUC_UNUSED cs_insn *ir1_decode_template_lookup(
        const uint8_t *code, size_t code_size, uint64_t address,
        int ir1_num, void *pir1_base, int mode)
{
    Ir1DecodeStats *stats = ir1_decode_profile_stats_get();

    return ir1_decode_template_lookup_impl(
        code, code_size, address, ir1_num, pir1_base, mode,
        stats, stats ? ir1_decode_stats_mode_index(mode) : -1);
}

static void ir1_decode_template_insert(const uint8_t *code, size_t code_size,
                                       int mode,
                                       const cs_insn *info)
{
    Ir1DecodeStats *stats = ir1_decode_profile_stats_get();
    Ir1DecodeDistribution *distribution = ir1_decode_distribution_get();
    int mode_index = stats ? ir1_decode_stats_mode_index(mode) : -1;

    if (!info->size || info->size > code_size || info->size > 15) {
        if (stats && mode_index >= 0) {
            stats->cache_rejected_size[mode_index]++;
        }
        return;
    }
    uint8_t relative_imm_mask =
        ir1_decode_template_relative_imm_mask(info);
    uint8_t imm_transform = IR1_DECODE_IMM_NONE;
    uint8_t imm_operand = 0;
#ifdef CONFIG_CAPSTONE_DIET
    imm_transform = ir1_decode_template_imm_transform(
        code, mode, info, &imm_operand);
#else
    imm_transform = relative_imm_mask ? IR1_DECODE_IMM_RELATIVE_EXACT :
                                          IR1_DECODE_IMM_NONE;
#endif
    size_t entries = ir1_decode_template_entries();
    size_t sets = ir1_decode_template_sets();
    size_t ways = ir1_decode_template_ways();
    if (!ir1_decode_tags) {
        ir1_decode_tags = g_new(Ir1DecodeTag, entries);
        ir1_decode_payloads = g_new(Ir1DecodePayload, entries);
        ir1_decode_template_valid = g_new0(uint8_t, entries);
        ir1_decode_template_policy = g_new0(uint8_t, sets);
        g_private_set(&ir1_decode_tags_owner, ir1_decode_tags);
        g_private_set(&ir1_decode_payloads_owner, ir1_decode_payloads);
        g_private_set(&ir1_decode_template_valid_owner,
                      ir1_decode_template_valid);
        g_private_set(&ir1_decode_template_policy_owner,
                      ir1_decode_template_policy);
        if (stats) {
            stats->cache_allocations++;
            stats->cache_tag_alloc_bytes += ir1_decode_tag_bytes() * entries;
            stats->cache_payload_alloc_bytes +=
                ir1_decode_payload_bytes() * entries;
            stats->cache_alloc_bytes +=
                ir1_decode_tag_bytes() * entries +
                ir1_decode_payload_bytes() * entries +
                sizeof(uint8_t) * entries + sizeof(uint8_t) * sets;
        }
    }

    uint32_t hash_tag = ir1_decode_template_hash_value(code, code_size, mode);
    uint64_t code_words[2];
    size_t set = hash_tag & (sets - 1);
    size_t base = set * ways;
    int free_way = -1;
    int same_way = -1;
    bool replaced = false;

    ir1_decode_template_load_words(code, code_words);

    for (size_t way = 0; way < ways; way++) {
        size_t index = base + way;
        Ir1DecodeTag *tag = &ir1_decode_tags[index];

        if (!ir1_decode_template_valid[index]) {
            if (free_way < 0) {
                free_way = way;
            }
        } else if (tag->size == info->size &&
                   ir1_decode_template_matches(tag, code_words, hash_tag,
                                               mode, code_size)) {
            same_way = way;
            break;
        }
    }

    int way = same_way;
    if (way < 0) {
        way = free_way;
    }
    if (way < 0) {
        replaced = true;
        switch (option_ir1_cache_replace) {
        case LATX_IR1_CACHE_REPLACE_FIFO:
            way = ir1_decode_template_policy[set] & (ways - 1);
            break;
        case LATX_IR1_CACHE_REPLACE_RANDOM:
            way = ir1_decode_template_random() % ways;
            break;
        case LATX_IR1_CACHE_REPLACE_LRU:
        default: {
            int oldest_age = -1;

            for (size_t candidate = 0; candidate < ways; candidate++) {
                int age = ir1_decode_template_lru_age(
                    ir1_decode_template_policy[set], candidate);

                if (age > oldest_age) {
                    oldest_age = age;
                    way = candidate;
                }
            }
            break;
        }
        }
        if (stats && mode_index >= 0) {
            stats->cache_replacements[mode_index]++;
        }
    }

    size_t index = base + way;
    Ir1DecodeTag *tag = &ir1_decode_tags[index];
    Ir1DecodePayload *payload = &ir1_decode_payloads[index];
    bool newly_occupied = !ir1_decode_template_valid[index];
    if (stats && newly_occupied) {
        stats->cache_occupied++;
    }
    if (stats && mode_index >= 0) {
        stats->cache_inserts[mode_index]++;
    }
    if (distribution) {
        Ir1DecodeSetProfile *set_profile =
            &distribution->set_profiles[set];

        if (newly_occupied) {
            distribution->set_occupancy[set]++;
        }
        if (replaced) {
            set_profile->replacements++;
            distribution->way_replacements[way]++;
            if (mode_index >= 0) {
                distribution->opcode_replacements[mode_index][code[0]]++;
            }
        }
        if (mode_index >= 0) {
            distribution->opcode_inserts[mode_index][code[0]]++;
        }
        distribution->entry_reuse[index] = 0;
    }
    tag->mode = mode;
    tag->size = info->size;
    tag->relative_imm_mask = relative_imm_mask;
    tag->hash_tag = hash_tag;
    if (stats && (imm_transform == IR1_DECODE_IMM_RELATIVE ||
                  imm_transform == IR1_DECODE_IMM_RELATIVE_EXACT) &&
        mode_index >= 0) {
        stats->cache_relative_inserts[mode_index]++;
    }
    tag->imm_transform = imm_transform;
    if (stats && tag->imm_transform && mode_index >= 0) {
        stats->cache_imm_inserts[mode_index]++;
    }
    ir1_decode_template_make_key(code, info->size, &info->detail->x86,
                                 tag->relative_imm_mask,
                                 tag->imm_transform, tag->key.bytes);
    tag->key.bytes[15] = imm_operand;
    tag->byte_mask[0] = 0;
    tag->byte_mask[1] = 0;
    for (uint8_t i = 0; i < info->size; i++) {
        if (tag->key.bytes[i] == code[i]) {
            tag->byte_mask[i >> 3] |= UINT64_C(0xff) << ((i & 7) * 8);
        }
    }
    memcpy(&payload->decoded, info, sizeof(payload->decoded));
    payload->decoded.detail = NULL;
#ifdef CONFIG_CAPSTONE_DIET
    memcpy(&payload->x86, &info->detail->x86, sizeof(payload->x86));
#else
    memcpy(&payload->detail, info->detail, sizeof(payload->detail));
#endif
    ir1_decode_template_valid[index] = true;
    if (same_way < 0) {
        if (option_ir1_cache_replace == LATX_IR1_CACHE_REPLACE_LRU) {
            ir1_decode_template_lru_insert(set, way);
        } else if (option_ir1_cache_replace ==
                   LATX_IR1_CACHE_REPLACE_FIFO) {
            ir1_decode_template_policy[set] = (way + 1) & (ways - 1);
        }
    }
    ir1_decode_fast_template_fill(index, code);
}

static uint64_t ir1_decode_timing_now_ns(void)
{
#if defined(__loongarch__) || defined(__loongarch64)
    return (uint64_t)cpu_get_host_ticks() * 10;
#else
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) != 0) {
        return 0;
    }
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + ts.tv_nsec;
#endif
}

static inline uint64_t ir1_hot_decode_timing_begin(void)
{
    if (!option_hot_decode_enabled || !option_ir1_timing) {
        return 0;
    }
    return ir1_decode_timing_now_ns();
}

static inline void ir1_hot_decode_timing_record(
        Ir1DecodeStats *stats, int mode_index, uint64_t start, bool success)
{
    if (!stats || !option_hot_decode_enabled || !option_ir1_timing ||
        mode_index < 0 || !start) {
        return;
    }
    uint64_t elapsed = ir1_decode_timing_now_ns() - start;

    stats->hot_attempt_ns[mode_index] += elapsed;
    stats->hot_attempt_samples[mode_index]++;
    if (success) {
        stats->hot_success_ns[mode_index] += elapsed;
        stats->hot_success_samples[mode_index]++;
    } else {
        stats->hot_failure_ns[mode_index] += elapsed;
        stats->hot_failure_samples[mode_index]++;
    }
}

static inline void ir1_decode_timing_begin(Ir1DecodeTiming *timing,
                                           int mode_index,
                                           bool cache_enabled)
{
    if (!option_ir1_timing || mode_index < 0) {
        timing->mode_index = -1;
        return;
    }
    timing->mode_index = mode_index;
    timing->total_start = cache_enabled ? ir1_decode_timing_now_ns() : 0;
}

static inline uint64_t ir1_decode_timing_phase_begin(
        Ir1DecodeTiming *timing)
{
    if (!option_ir1_timing || timing->mode_index < 0) {
        return 0;
    }
    return ir1_decode_timing_now_ns();
}

static inline void ir1_decode_timing_record_lookup(
        Ir1DecodeTiming *timing, Ir1DecodeStats *stats, bool hit)
{
    if (!stats || !option_ir1_timing || timing->mode_index < 0) {
        return;
    }
    uint64_t elapsed = ir1_decode_timing_now_ns() - timing->total_start;
    int mode_index = timing->mode_index;

    if (hit) {
        stats->cache_hit_ns[mode_index] += elapsed;
        if (ir1_decode_last_fast_hit) {
            stats->fast_hit_ns[mode_index] += elapsed;
        } else {
            stats->main_hit_ns[mode_index] += elapsed;
        }
    } else {
        stats->miss_lookup_ns[mode_index] += elapsed;
        stats->miss_lookup_samples[mode_index]++;
    }
}

static inline void ir1_decode_timing_record_capstone(
        Ir1DecodeTiming *timing, Ir1DecodeStats *stats, uint64_t phase_start,
        bool cache_enabled)
{
    if (!stats || !option_ir1_timing || timing->mode_index < 0 ||
        !phase_start) {
        return;
    }
    uint64_t elapsed = ir1_decode_timing_now_ns() - phase_start;
    int mode_index = timing->mode_index;

    if (cache_enabled) {
        stats->miss_capstone_ns[mode_index] += elapsed;
        stats->miss_capstone_samples[mode_index]++;
    } else {
        stats->direct_ns[mode_index] += elapsed;
    }
}

static inline void ir1_decode_timing_record_insert(
        Ir1DecodeTiming *timing, Ir1DecodeStats *stats, uint64_t phase_start)
{
    if (!stats || !option_ir1_timing || timing->mode_index < 0 ||
        !phase_start) {
        return;
    }
    int mode_index = timing->mode_index;

    stats->miss_insert_ns[mode_index] +=
        ir1_decode_timing_now_ns() - phase_start;
    stats->miss_insert_samples[mode_index]++;
}

static inline void ir1_decode_timing_record_miss(
        Ir1DecodeTiming *timing, Ir1DecodeStats *stats, bool cache_enabled)
{
    if (!stats || !option_ir1_timing || timing->mode_index < 0 ||
        !cache_enabled ||
        !timing->total_start) {
        return;
    }
    int mode_index = timing->mode_index;

    stats->cache_miss_ns[mode_index] +=
        ir1_decode_timing_now_ns() - timing->total_start;
}

static inline bool ir1_is_incsspd_bytes(const uint8_t *code)
{
    return code[0] == 0xf3 && code[1] == 0x0f &&
           code[2] == 0xae && ((code[3] & 0xf8) == 0xe8);
}

static inline bool ir1_is_incssp_rex_bytes(const uint8_t *code)
{
    return code[0] == 0xf3 && ((code[1] & 0xf0) == 0x40) &&
           code[2] == 0x0f && code[3] == 0xae &&
           ((code[4] & 0xf8) == 0xe8);
}

/* Cache the raw decode, before per-translation segment/operand fixups. */
size_t latxs_decode_one(csh decoder, const uint8_t *code, uint64_t address,
                        int mode, cs_insn **insn, int ir1_num, void *base)
{
    Ir1DecodeStats *stats = ir1_decode_profile_stats_get();
    int mode_index = stats ? ir1_decode_stats_mode_index(mode) : -1;
    Ir1DecodeTiming timing = { .mode_index = -1 };
    cs_insn *info = NULL;

    if (mode_index >= 0) {
        stats->calls[mode_index]++;
    }

    uint64_t hot_start = ir1_hot_decode_timing_begin();

    if (option_hot_decode_enabled) {
        info = ir1_hot_decode(code, address, ir1_num, base, mode);
    }
    ir1_hot_decode_timing_record(stats, mode_index, hot_start, info != NULL);
    if (info && mode_index >= 0) {
        stats->hot_hits[mode_index]++;
    }
    if (info && option_ir1_distribution) {
        Ir1DecodeDistribution *distribution = ir1_decode_distribution_get();

        ir1_decode_distribution_page(distribution, address)->hot_hits++;
    }

    if (!info) {
        ir1_decode_timing_begin(&timing, mode_index,
                                option_ir1_cache_enabled);
        if (option_ir1_cache_enabled && mode_index >= 0) {
            stats->cache_lookups[mode_index]++;
            if (!ir1_decode_tags || !ir1_decode_payloads) {
                stats->cache_unallocated[mode_index]++;
            }
        }
        if (option_ir1_cache_enabled) {
            info = ir1_decode_template_lookup_impl(
                code, 15, address, ir1_num, base, mode, stats, mode_index);
            ir1_decode_timing_record_lookup(&timing, stats, info != NULL);
        }
        if (info && mode_index >= 0) {
            stats->cache_hits[mode_index]++;
        }
    }
    if (info) {
        if (mode_index >= 0) {
            stats->successes[mode_index]++;
        }
        if (info->detail) {
            info->bytes[IR1_DECODE_OP_COUNT_SHADOW] =
                MIN(info->detail->x86.op_count,
                    (uint8_t)G_N_ELEMENTS(info->detail->x86.operands));
        }
        *insn = info;
        return 1;
    }
    uint64_t capstone_start = ir1_decode_timing_phase_begin(&timing);
    size_t count = cs_disasm(decoder, code, 15, address, 1,
                            insn, ir1_num, base);
    ir1_decode_timing_record_capstone(&timing, stats, capstone_start,
                                      option_ir1_cache_enabled);
    if (count == 1 && (*insn)->detail &&
        (*insn)->id == X86_INS_VBROADCASTF128) {
        unsigned modrm = (*insn)->detail->x86.encoding.modrm_offset;

        if (modrm && code[modrm - 1] == 0x5a) {
            (*insn)->id = X86_INS_VBROADCASTI128;
#ifdef CONFIG_LATX_DEBUG
            strcpy((*insn)->mnemonic, "vbroadcasti128");
#endif
        }
    }
    if (count == 1 && *insn && (*insn)->id == X86_INS_LFENCE &&
        ((mode == 64 && ir1_is_incssp_rex_bytes(code)) ||
         ir1_is_incsspd_bytes(code))) {
        if (mode_index >= 0) {
            stats->full_fail[mode_index]++;
        }
        ir1_decode_timing_record_miss(&timing, stats,
                                      option_ir1_cache_enabled);
        *insn = NULL;
        return 0;
    }
    if (mode_index >= 0) {
        if (count == 1) {
            stats->full_success[mode_index]++;
            stats->successes[mode_index]++;
            if (option_ir1_cache_enabled) {
                stats->cache_misses_by_opcode[mode_index][code[0]]++;
            }
        } else {
            stats->full_fail[mode_index]++;
        }
    }
    if (option_ir1_cache_enabled && count == 1 && *insn && (*insn)->detail) {
        uint64_t insert_start = ir1_decode_timing_phase_begin(&timing);

        (*insn)->bytes[IR1_DECODE_OP_COUNT_SHADOW] =
            MIN((*insn)->detail->x86.op_count,
                (uint8_t)G_N_ELEMENTS((*insn)->detail->x86.operands));
        ir1_decode_template_insert(code, 15, mode, *insn);
        ir1_decode_timing_record_insert(&timing, stats, insert_start);
    }
    ir1_decode_timing_record_miss(&timing, stats, option_ir1_cache_enabled);
    return count;
}

static int ir1_decode_distribution_u64_desc(const void *opaque_a,
                                             const void *opaque_b)
{
    uint64_t a = *(const uint64_t *)opaque_a;
    uint64_t b = *(const uint64_t *)opaque_b;

    return a < b ? 1 : a > b ? -1 : 0;
}

static int ir1_decode_distribution_page_key(const void *opaque_a,
                                             const void *opaque_b)
{
    const Ir1DecodePageProfile *a = opaque_a;
    const Ir1DecodePageProfile *b = opaque_b;

    return a->page < b->page ? -1 : a->page > b->page ? 1 : 0;
}

static int ir1_decode_distribution_page_lookups(const void *opaque_a,
                                                 const void *opaque_b)
{
    const Ir1DecodePageProfile *a = opaque_a;
    const Ir1DecodePageProfile *b = opaque_b;
    uint64_t accesses_a = a->lookups + a->hot_hits;
    uint64_t accesses_b = b->lookups + b->hot_hits;

    return accesses_a < accesses_b ? 1 : accesses_a > accesses_b ? -1 : 0;
}

static unsigned int ir1_decode_distribution_reuse_bucket(uint16_t reuse)
{
    if (reuse == 0) {
        return 0;
    }
    if (reuse == 1) {
        return 1;
    }
    if (reuse < 4) {
        return 2;
    }
    if (reuse < 8) {
        return 3;
    }
    if (reuse < 16) {
        return 4;
    }
    if (reuse < 32) {
        return 5;
    }
    if (reuse < 64) {
        return 6;
    }
    if (reuse < 128) {
        return 7;
    }
    if (reuse < 256) {
        return 8;
    }
    return 9;
}

static void ir1_decode_distribution_dump_opcode(const char *event, int mode,
                                                 const uint64_t values[256])
{
    uint64_t remaining[256];

    memcpy(remaining, values, sizeof(remaining));
    fprintf(stderr, "IR1_DIST_OPCODE%d_%s", mode, event);
    for (int rank = 0; rank < 16; rank++) {
        int best = 0;

        for (int opcode = 1; opcode < 256; opcode++) {
            if (remaining[opcode] > remaining[best]) {
                best = opcode;
            }
        }
        if (!remaining[best]) {
            break;
        }
        fprintf(stderr, " %02x=%" PRIu64, best, remaining[best]);
        remaining[best] = 0;
    }
    fprintf(stderr, "\n");
}

static void ir1_decode_distribution_dump(void)
{
    uint32_t nr = MIN(qatomic_read(&ir1_decode_distribution_nr),
                      IR1_DECODE_STATS_MAX_THREADS);
    uint32_t effective_nr = 0;
    size_t entries = ir1_decode_template_entries();
    size_t sets = ir1_decode_template_sets();
    size_t ways = ir1_decode_template_ways();
    uint64_t *set_lookups = g_new0(uint64_t, sets);
    uint64_t *set_hits = g_new0(uint64_t, sets);
    uint64_t *set_misses = g_new0(uint64_t, sets);
    uint64_t *set_replacements = g_new0(uint64_t, sets);
    uint64_t *sorted = g_new0(uint64_t, MAX(sets * nr, (size_t)1));
    uint64_t occupancy_hist[LATX_IR1_CACHE_MAX_WAYS + 1] = { 0 };
    uint64_t way_hits[LATX_IR1_CACHE_MAX_WAYS] = { 0 };
    uint64_t way_replacements[LATX_IR1_CACHE_MAX_WAYS] = { 0 };
    uint64_t fast_way_hits = 0;
    uint64_t page_collisions = 0;
    uint64_t reuse_hist[IR1_DECODE_DIST_REUSE_BUCKETS] = { 0 };
    uint64_t opcode_hits[IR1_DECODE_MODE_COUNT][256] = { 0 };
    uint64_t opcode_inserts[IR1_DECODE_MODE_COUNT][256] = { 0 };
    uint64_t opcode_replacements[IR1_DECODE_MODE_COUNT][256] = { 0 };
    size_t page_capacity = (size_t)nr * IR1_DECODE_DIST_PAGE_SLOTS;
    Ir1DecodePageProfile *pages = g_new0(Ir1DecodePageProfile,
                                         MAX(page_capacity, (size_t)1));
    size_t page_count = 0;
    uint64_t total_set_lookups = 0;
    uint64_t total_set_hits = 0;
    uint64_t total_set_misses = 0;
    uint64_t total_set_replacements = 0;
    uint64_t sets_allocated = 0;
    uint64_t sets_looked_up = 0;
    uint64_t sets_replaced = 0;
    uint64_t max_set_replacements = 0;
    uint64_t total_slots = 0;
    uint64_t never_inserted = 0;
    uint64_t total_page_lookups = 0;
    uint64_t total_page_hits = 0;
    uint64_t total_page_misses = 0;
    uint64_t total_page_hot_hits = 0;

    if (!nr) {
        fprintf(stderr, "IR1_DISTRIBUTION enabled=on samples=0\n");
        g_free(pages);
        g_free(sorted);
        g_free(set_replacements);
        g_free(set_misses);
        g_free(set_hits);
        g_free(set_lookups);
        return;
    }

    for (uint32_t thread = 0; thread < nr; thread++) {
        Ir1DecodeDistribution *distribution =
            ir1_decode_distribution_slots[thread];

        if (!distribution) {
            continue;
        }
        effective_nr++;
        fast_way_hits += distribution->fast_way_hits;
        page_collisions += distribution->page_collisions;
        for (size_t way = 0; way < ways; way++) {
            way_hits[way] += distribution->way_hits[way];
            way_replacements[way] += distribution->way_replacements[way];
        }
        for (int mode = 0; mode < IR1_DECODE_MODE_COUNT; mode++) {
            for (int opcode = 0; opcode < 256; opcode++) {
                opcode_hits[mode][opcode] +=
                    distribution->opcode_hits[mode][opcode];
                opcode_inserts[mode][opcode] +=
                    distribution->opcode_inserts[mode][opcode];
                opcode_replacements[mode][opcode] +=
                    distribution->opcode_replacements[mode][opcode];
            }
        }
        for (size_t set = 0; set < sets; set++) {
            Ir1DecodeSetProfile *profile =
                &distribution->set_profiles[set];

            set_lookups[set] += profile->lookups;
            set_hits[set] += profile->hits;
            set_misses[set] += profile->misses;
            set_replacements[set] += profile->replacements;
        }
        for (size_t set = 0; set < sets; set++) {
            uint8_t occupancy = distribution->set_occupancy[set];

            occupancy_hist[occupancy]++;
            sets_allocated += occupancy != 0;
            sets_looked_up += distribution->set_profiles[set].lookups != 0;
        }
        for (size_t entry = 0; entry < entries; entry++) {
            uint16_t reuse = distribution->entry_reuse[entry];

            if (reuse == UINT16_MAX) {
                never_inserted++;
            } else {
                reuse_hist[ir1_decode_distribution_reuse_bucket(reuse)]++;
            }
        }
        for (size_t slot = 0; slot < IR1_DECODE_DIST_PAGE_SLOTS; slot++) {
            if (distribution->page_profiles[slot].page) {
                pages[page_count++] = distribution->page_profiles[slot];
            }
        }
    }

    if (!effective_nr) {
        fprintf(stderr, "IR1_DISTRIBUTION enabled=on samples=0\n");
        g_free(pages);
        g_free(sorted);
        g_free(set_replacements);
        g_free(set_misses);
        g_free(set_hits);
        g_free(set_lookups);
        return;
    }

    total_slots = (uint64_t)effective_nr * sets;
    for (size_t set = 0; set < sets; set++) {
        total_set_lookups += set_lookups[set];
        total_set_hits += set_hits[set];
        total_set_misses += set_misses[set];
        total_set_replacements += set_replacements[set];
        sets_replaced += set_replacements[set] != 0;
        max_set_replacements = MAX(max_set_replacements,
                                   set_replacements[set]);
        sorted[set] = set_lookups[set];
    }
    qsort(sorted, total_slots, sizeof(*sorted),
          ir1_decode_distribution_u64_desc);

    uint64_t top1_sets = MAX((total_slots + 99) / 100, UINT64_C(1));
    uint64_t top10_sets = MAX((total_slots + 9) / 10, UINT64_C(1));
    uint64_t top1_lookups = 0;
    uint64_t top10_lookups = 0;
    uint64_t top1_replacements = 0;
    uint64_t top10_replacements = 0;

    for (uint64_t i = 0; i < total_slots; i++) {
        if (i < top1_sets) {
            top1_lookups += sorted[i];
        }
        if (i < top10_sets) {
            top10_lookups += sorted[i];
        }
    }
    for (size_t set = 0; set < sets; set++) {
        sorted[set] = set_replacements[set];
    }
    qsort(sorted, total_slots, sizeof(*sorted),
          ir1_decode_distribution_u64_desc);
    for (uint64_t i = 0; i < total_slots; i++) {
        if (i < top1_sets) {
            top1_replacements += sorted[i];
        }
        if (i < top10_sets) {
            top10_replacements += sorted[i];
        }
    }

    fprintf(stderr,
            "IR1_DIST_SET hash=%s samples=%u entries=%zu sets=%zu ways=%zu "
            "set_instances=%" PRIu64 " lookups=%" PRIu64
            " hits=%" PRIu64 " misses=%" PRIu64
            " replacements=%" PRIu64 " hit_rate=%.4f%%\n",
            latxs_ir1_hash_name(option_ir1_hash), effective_nr,
            entries, sets, ways, total_slots,
            total_set_lookups, total_set_hits, total_set_misses,
            total_set_replacements,
            total_set_lookups ? (double)total_set_hits * 100.0 /
                                total_set_lookups : 0.0);
    fprintf(stderr,
            "IR1_DIST_SET_OCCUPANCY allocated=%" PRIu64
            " empty=%" PRIu64 " full=%" PRIu64 " fill_ratio=%.4f",
            sets_allocated, occupancy_hist[0], occupancy_hist[ways],
            total_slots ? (double)sets_allocated / total_slots : 0.0);
    for (size_t occupancy = 0; occupancy <= ways; occupancy++) {
        fprintf(stderr, " way%zu=%" PRIu64, occupancy,
                occupancy_hist[occupancy]);
    }
    fprintf(stderr, "\n");
    fprintf(stderr,
            "IR1_DIST_SET_ACTIVITY looked_up=%" PRIu64
            " never_looked_up=%" PRIu64 " replaced=%" PRIu64
            " max_replacements=%" PRIu64 " top1_lookup_pct=%.4f"
            " top10_lookup_pct=%.4f\n",
            sets_looked_up, total_slots - sets_looked_up, sets_replaced,
            max_set_replacements,
            total_set_lookups ? (double)top1_lookups * 100.0 /
                                total_set_lookups : 0.0,
            total_set_lookups ? (double)top10_lookups * 100.0 /
                                total_set_lookups : 0.0);
    fprintf(stderr,
            "IR1_DIST_SET_REPLACEMENT total=%" PRIu64
            " max=%" PRIu64 " max_pct=%.4f"
            " top1_pct=%.4f top10_pct=%.4f\n",
            total_set_replacements, max_set_replacements,
            total_set_replacements ? (double)max_set_replacements * 100.0 /
                                     total_set_replacements : 0.0,
            total_set_replacements ? (double)top1_replacements * 100.0 /
                                     total_set_replacements : 0.0,
            total_set_replacements ? (double)top10_replacements * 100.0 /
                                     total_set_replacements : 0.0);

    for (int rank = 0; rank < IR1_DECODE_DIST_TOP; rank++) {
        size_t best = 0;

        for (size_t set = 1; set < sets; set++) {
            if (set_lookups[set] > set_lookups[best]) {
                best = set;
            }
        }
        if (!set_lookups[best]) {
            break;
        }
        uint64_t occupied = 0;

        for (uint32_t thread = 0; thread < nr; thread++) {
            Ir1DecodeDistribution *distribution =
                ir1_decode_distribution_slots[thread];

            if (distribution) {
                occupied += distribution->set_occupancy[best];
            }
        }
        fprintf(stderr,
                "IR1_DIST_TOP_SET rank=%d set=%zu occupancy=%" PRIu64
                "/%" PRIu64 " lookups=%" PRIu64 " hits=%" PRIu64
                " misses=%" PRIu64 " replacements=%" PRIu64
                " hit_rate=%.4f%%\n",
                rank + 1, best, occupied, (uint64_t)effective_nr * ways,
                set_lookups[best], set_hits[best], set_misses[best],
                set_replacements[best],
                set_lookups[best] ? (double)set_hits[best] * 100.0 /
                                    set_lookups[best] : 0.0);
        set_lookups[best] = 0;
    }

    for (size_t way = 0; way < ways; way++) {
        fprintf(stderr,
                "IR1_DIST_WAY way=%zu main_hits=%" PRIu64
                " replacements=%" PRIu64 "\n",
                way, way_hits[way], way_replacements[way]);
    }
    fprintf(stderr, "IR1_DIST_FAST_WAY way=0 hits=%" PRIu64 "\n",
            fast_way_hits);

    fprintf(stderr,
            "IR1_DIST_REUSE total=%" PRIu64 " never_inserted=%" PRIu64
            " hits0=%" PRIu64 " hits1=%" PRIu64 " hits2_3=%" PRIu64
            " hits4_7=%" PRIu64 " hits8_15=%" PRIu64
            " hits16_31=%" PRIu64 " hits32_63=%" PRIu64
            " hits64_127=%" PRIu64 " hits128_255=%" PRIu64
            " hits256plus=%" PRIu64 "\n",
            (uint64_t)effective_nr * entries, never_inserted,
            reuse_hist[0], reuse_hist[1], reuse_hist[2], reuse_hist[3],
            reuse_hist[4], reuse_hist[5], reuse_hist[6], reuse_hist[7],
            reuse_hist[8], reuse_hist[9]);

    for (int mode = 1; mode <= 2; mode++) {
        ir1_decode_distribution_dump_opcode("HIT", mode == 1 ? 32 : 64,
                                             opcode_hits[mode]);
        ir1_decode_distribution_dump_opcode("INSERT", mode == 1 ? 32 : 64,
                                             opcode_inserts[mode]);
        ir1_decode_distribution_dump_opcode("REPLACE", mode == 1 ? 32 : 64,
                                             opcode_replacements[mode]);
    }

    qsort(pages, page_count, sizeof(*pages),
          ir1_decode_distribution_page_key);
    size_t merged_pages = 0;
    for (size_t i = 0; i < page_count; i++) {
        if (merged_pages && pages[merged_pages - 1].page == pages[i].page) {
            pages[merged_pages - 1].lookups += pages[i].lookups;
            pages[merged_pages - 1].hits += pages[i].hits;
            pages[merged_pages - 1].misses += pages[i].misses;
            pages[merged_pages - 1].hot_hits += pages[i].hot_hits;
        } else {
            pages[merged_pages++] = pages[i];
        }
    }
    qsort(pages, merged_pages, sizeof(*pages),
          ir1_decode_distribution_page_lookups);
    for (size_t i = 0; i < merged_pages; i++) {
        total_page_lookups += pages[i].lookups;
        total_page_hits += pages[i].hits;
        total_page_misses += pages[i].misses;
        total_page_hot_hits += pages[i].hot_hits;
    }
    fprintf(stderr,
            "IR1_DIST_PAGE slots=%d pages=%zu collisions=%" PRIu64
            " lookups=%" PRIu64 " hits=%" PRIu64 " misses=%" PRIu64
            " hot_hits=%" PRIu64 " hit_rate=%.4f%%\n",
            IR1_DECODE_DIST_PAGE_SLOTS, merged_pages, page_collisions,
            total_page_lookups, total_page_hits, total_page_misses,
            total_page_hot_hits,
            total_page_lookups ? (double)total_page_hits * 100.0 /
                                 total_page_lookups : 0.0);
    for (size_t rank = 0; rank < MIN((size_t)IR1_DECODE_DIST_TOP,
                                     merged_pages); rank++) {
        fprintf(stderr,
                "IR1_DIST_TOP_PAGE rank=%zu page=0x%" PRIx64
                " lookups=%" PRIu64 " hits=%" PRIu64 " misses=%" PRIu64
                " hot_hits=%" PRIu64 " hit_rate=%.4f%%\n",
                rank + 1, pages[rank].page, pages[rank].lookups,
                pages[rank].hits, pages[rank].misses, pages[rank].hot_hits,
                pages[rank].lookups ? (double)pages[rank].hits * 100.0 /
                                      pages[rank].lookups : 0.0);
    }

    g_free(pages);
    g_free(sorted);
    g_free(set_replacements);
    g_free(set_misses);
    g_free(set_hits);
    g_free(set_lookups);
}
