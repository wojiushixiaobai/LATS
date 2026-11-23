#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Verify middle-exit fast binary search, TU chunk-hint lookup, and n=1 native jump entry."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[4]
load_src = (root / "target/i386/latx/sbt/sys-aot-load.c").read_text()
reg_src = (root / "tcg/region.c").read_text()
tr_src = (root / "target/i386/latx/translator/translate.c").read_text()


def extract(src, start_marker, end_marker):
    i = src.index(start_marker)
    j = src.index(end_marker, i) + len(end_marker)
    return src[i:j]


middle_code = extract(
    load_src,
    "typedef struct {\n    const aot_block *blocks;",
    "    return contained && load && exit;\n}",
)

reg_lookup_code = extract(
    reg_src,
    "#ifdef CONFIG_LATX_TU\n#ifdef QEMU_OSDEP_H\nstatic TBMini *lats_tu_chunk_hint[65536];",
    "    return tb;\n}",
)

tr_dce_end = tr_src.index("\n}\n#endif", tr_src.index("void *latxs_aot_direct_call_entry(")) + 2
tr_code = tr_src[tr_src.index("static bool latxs_plt_near_jump("):tr_dce_end]
tr_replay = (
    extract(tr_src, "static bool aot_native_branch_fits(", "(((uint64_t)offset << 10) & 0x3fffc00);\n}")
    + "\n"
    + extract(tr_src, "const void *latxs_aot_ordinary_jump_replay(", "    return entry;\n}")
)
exec_src = (root / "accel/tcg/cpu-exec.c").read_text()
htable_hint_code = extract(
    exec_src,
    "static TranslationBlock *tb_htable_lookup_with_hint(",
    "        return tb;\n    }\n}",
)

c_prog = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

#define QEMU_OSDEP_H 1
#define CONFIG_LATX_TU 1
#define CONFIG_SOFTMMU 1
#define unlikely(x) (x)
#define qatomic_read(p) (*(p))
#define qatomic_set(p, v) (*(p) = (v))
#define qatomic_load_acquire(p) (*(p))
#define qatomic_store_release(p, v) (*(p) = (v))
#define g_assert(c) assert(c)
#define flush_idcache_range(a, b, c) ((void)(a), (void)(b), (void)(c))
#define qemu_spin_lock(l) ((void)(l))
#define qemu_spin_unlock(l) ((void)(l))
#define g_try_new(type, n) ((type *)calloc((n), sizeof(type)))
#define g_free(p) free(p)
#define MAKE_64BIT_MASK(lo, width) (((UINT64_C(1) << (width)) - 1) << (lo))
#define ROUND_DOWN(value, align) ((value) & ~((uintptr_t)(align) - 1))
#define ROUND_UP(value, align) (((value) + ((uintptr_t)(align) - 1)) & ~((uintptr_t)(align) - 1))
#define TB_MAGIC 0x5aa5
#define TU_TBM_MAGIC 0xa55a
#define HOST_VIRT_ADDR_SPACE_BITS 48
#define CODE_GEN_ALIGN 32
#define TB_JMP_OFFSET_INVALID 0xffff
#define TB_JMP_CP_DATA_VALID (1U << 31)
#define TB_JMP_CP_DATA_LINKED (1U << 30)
#define TB_JMP_CP_DATA_OFF 0xffffU
#define IS_AOT_TB (1U << 0)
#define IS_TU_TB (1U << 1)
#define IS_HRAS_CALL (1U << 7)
#define CF_INVALID (1U << 18)
#define CF_NO_ACTIVE (1U << 19)
#define CF_SINGLE_STEP (1U << 20)
#define CF_USE_ICOUNT (1U << 21)
#define CF_NO_GOTO_TB (1U << 23)
#define CF_NO_GOTO_PTR (1U << 24)
#define HF_CS64_MASK (1U << 15)
#define HF_CPL_MASK 3U
#define IRQ_CHECK_ASYNC_NOTIFY 3
#define MF_MTCC_ID_MASK 1U
#define MF_MTCC_ID_SHIFT 0

static inline int64_t sextract64(uint64_t value, int start, int length)
{
    return ((int64_t)(value << (64 - length - start))) >> (64 - length);
}

static uint32_t aot_file_generation = 1;

enum {
    LOAD_TB_ADDR = 1,
    CS_NATIVE_TO_BT_RET_N0 = 2,
    OTHER_REL = 3,
};

typedef struct {
    struct { uint64_t offset_in_tu; } tc;
    uint32_t ir2_num;
    int32_t rel_start;
    int32_t rel_end;
} aot_block;

typedef struct {
    uint32_t kind;
    uint32_t tc_offset;
    uint32_t rel_slots_num;
    uint64_t extra_addend;
    uint32_t next_entry_id;
} aot_rel;

typedef struct TBMini {
    union {
        uint64_t mtbp_uint64;
        struct {
            uint64_t pointer : 48;
            uint16_t magic : 16;
        } mtbp_struct;
    };
} TBMini;

typedef struct {
    uint32_t state_bytes;
} NativeRecoverHeader;

struct TranslationBlock;
typedef struct {
    struct TranslationBlock *tb;
    void *entry, *core, *recover, *gate;
    uint64_t entry_bytes;
    uint64_t callee_pc;
    uint32_t *direct_call_slot;
    uint32_t direct_call_closed;
    bool direct_call_guards_emitted;
} AOTNativeRecord;
typedef struct { unsigned count; AOTNativeRecord records[2]; } AOTNativeTUInfo;
typedef struct { AOTNativeTUInfo *native; uint32_t id; } TranslationUnitPages;
typedef struct {
    uint64_t tu_size;
    uint32_t ras_tp_cold_offset;
    uint8_t ras_tp_cold_insns;
    const void *aot_native_call_entry;
} Data;
typedef struct TranslationBlock {
    struct { void *ptr; size_t offset_in_tu; } tc;
    uint64_t pc;
    uint64_t cs_base;
    uint32_t flags;
    uint32_t cflags;
    uint32_t bool_flags;
    uint32_t misc_flags;
    uint16_t inst_ptr_offset;
    uint16_t jmp_insn_offset[2];
    uint16_t jmp_reset_offset[2];
    uint32_t jmp_cp_data[2];
    uintptr_t jmp_dest[2];
    int jmp_lock;
    unsigned ir2_num;
    Data *s_data;
    TranslationUnitPages *pages;
} TranslationBlock;

#define TB_TU_PAGES(tb) ((tb)->pages)
static inline bool tb_is_tu(const TranslationBlock *tb)
{ return (tb->bool_flags & IS_TU_TB) != 0; }
static inline uint32_t tb_cflags(const TranslationBlock *tb)
{ return tb->cflags; }
static inline uint32_t get_mask_from_miscflags(const TranslationBlock *tb,
                                               uint32_t mask, uint32_t shift)
{ return (tb->misc_flags & mask) >> shift; }
static uintptr_t tb_tu_identity(TranslationBlock *tb)
{ return (uintptr_t)tb->tc.ptr - tb->tc.offset_in_tu; }

static bool option_external_tu_stub = false;
static bool option_soft_ras = true;
static bool option_rastoptp = true;
static int option_int_opt = IRQ_CHECK_ASYNC_NOTIFY;
static bool pages_cover_flag = true;
static bool bypass_check_flag = false;
static bool latxs_hras_jit_translate(const TranslationBlock *tb)
{ (void)tb; return true; }
static bool tucp_fast_direct_check(void) { return true; }
static bool latxs_tucp_pages_cover(const TranslationUnitPages *a,
                                   const TranslationUnitPages *b)
{ (void)a; (void)b; return pages_cover_flag; }
static bool latxs_tucp_link_can_bypass_check(const TranslationBlock *a,
                                             const TranslationBlock *b)
{ (void)a; (void)b; return bypass_check_flag; }

static struct {
    void *start_aligned;
    size_t total_size;
    size_t stride;
    size_t n;
    size_t mtcc_n;
    uint64_t after_prologue[2];
} region;
static int option_region_prologue = 1;

static bool in_code_gen_buffer(const void *p)
{
    return (uintptr_t)p >= (uintptr_t)region.start_aligned &&
           (uintptr_t)p - (uintptr_t)region.start_aligned <= region.total_size;
}
typedef uint64_t vaddr;
typedef int64_t tb_page_addr_t;
typedef struct { void *env_ptr; } CPUState;
struct tb_desc {
    void *env;
    uint64_t cs_base;
    uint32_t flags;
    uint32_t cflags;
    vaddr pc;
    tb_page_addr_t page_addr0;
};
#define CF_PCREL (1U << 22)
#define MTCC_NR 2
static CPUState dummy_cpu, *current_cpu = &dummy_cpu;
static tb_page_addr_t stub_phys_pc = 0x2000;
static int qht_calls = 0, fsht_reinsert_calls = 0;
static int supervisor_lookup_calls = 0;
static bool option_pcid_cache = false;
static bool option_sys_aot = false;
static TranslationBlock *qht_fallback_tb = NULL;
static struct { int htable[MTCC_NR]; } tb_ctx;
static tb_page_addr_t get_page_addr_code(void *env, vaddr pc) { (void)env; (void)pc; return stub_phys_pc; }
static bool qemu_tcg_mtcc_enabled(void) { return true; }
static bool jit_tu_enabled(void) { return false; }
static uint32_t curr_mtcc(void *env) { (void)env; return 0; }
static bool tb_is_lookup_private(const TranslationBlock *tb) { return (tb->bool_flags & IS_HRAS_CALL) != 0; }
static bool tb_lookup_cmp(const void *p, const void *d) {
    const TranslationBlock *tb = p;
    const struct tb_desc *desc = d;
    return tb->pc == desc->pc && tb->cs_base == desc->cs_base &&
           tb->flags == desc->flags && tb->cflags == desc->cflags &&
           desc->page_addr0 == 0x2000;
}
static uint32_t tb_hash_func(tb_page_addr_t phys_pc, vaddr pc, uint32_t flags, uint64_t cs_base, uint32_t cflags) {
    (void)phys_pc; (void)pc; (void)flags; (void)cs_base; (void)cflags;
    return 42;
}
static void *tb_qht_lookup(void *ht, const void *d, uint32_t h, bool (*cmp)(const void *, const void *)) {
    (void)ht; (void)h;
    qht_calls++;
    return (qht_fallback_tb && cmp(qht_fallback_tb, d)) ? qht_fallback_tb : NULL;
}
static TranslationBlock *supervisor_tb_qht_lookup(const struct tb_desc *desc, uint32_t hash) {
    supervisor_lookup_calls++;
    return tb_qht_lookup(&tb_ctx.htable[0], desc, hash, tb_lookup_cmp);
}
static void tb_htable_reinsert_fsht(TranslationBlock *tb, vaddr pc, tb_page_addr_t phys_pc, uint32_t flags) {
    (void)tb; (void)pc; (void)phys_pc; (void)flags;
    fsht_reinsert_calls++;
}
""" + middle_code + "\n" + reg_lookup_code + "\n" + tr_code + "\n" + tr_replay + "\n" + htable_hint_code + r"""

static void write_valid_stub(uint8_t *code, uint64_t cold)
{
    uint32_t stub[4] = {
        0x1400000aU,
        0x0380014aU,
        0x1600000aU,
        0x50000000U,
    };
    memcpy(code + cold, stub, sizeof(stub));
}

int main(void)
{
    uint8_t code[1024] = {0};
    aot_block blocks[3] = {
        { .tc = { 0 },   .ir2_num = 16, .rel_start = 0, .rel_end = 1 },
        { .tc = { 64 },  .ir2_num = 16, .rel_start = -1, .rel_end = -1 },
        { .tc = { 128 }, .ir2_num = 32, .rel_start = 2, .rel_end = 2 },
    };
    /* 1. Valid middle exit at cold = 140 (strictly inside block[2] [128, 256)) */
    write_valid_stub(code, 140);
    aot_rel rels[4] = {
        { .kind = LOAD_TB_ADDR, .tc_offset = 140, .rel_slots_num = 3, .extra_addend = UINT64_MAX, .next_entry_id = 0 },
        { .kind = CS_NATIVE_TO_BT_RET_N0, .tc_offset = 152, .rel_slots_num = 1, .extra_addend = 0, .next_entry_id = 0 },
        { .kind = OTHER_REL, .tc_offset = 64, .rel_slots_num = 1, .extra_addend = 0, .next_entry_id = 0 },
        { .kind = OTHER_REL, .tc_offset = 0, .rel_slots_num = 1, .extra_addend = 0, .next_entry_id = 0 },
    };

    assert(aot_paired_call_middle_exit(blocks, 3, 0, rels, code, 512, 140, 64));
    assert(lats_middle_cache.valid_disjoint);

    /* 2. Boundary cold == blocks[2].tc.offset_in_tu (128) must be rejected */
    write_valid_stub(code, 128);
    rels[0].tc_offset = 128;
    rels[1].tc_offset = 140;
    aot_file_generation++;
    assert(!aot_paired_call_middle_exit(blocks, 3, 0, rels, code, 512, 128, 64));

    /* 3. Relocation in block[2] overlapping [cold, end) must be rejected */
    memset(code, 0, sizeof(code));
    write_valid_stub(code, 140);
    rels[0].tc_offset = 140;
    rels[1].tc_offset = 152;
    rels[2].tc_offset = 12; /* 128 + 12 = 140, overlaps cold! */
    aot_file_generation++;
    assert(!aot_paired_call_middle_exit(blocks, 3, 0, rels, code, 512, 140, 64));
    assert(!lats_middle_cache.valid_disjoint);

    /* 4. Unrelated overlapping relocations outside [cold, end) fall back to slow loop and succeed */
    blocks[1].rel_start = 3;
    blocks[1].rel_end = 3;
    rels[2].tc_offset = 64;  /* 128 + 64 = 192 */
    rels[3].tc_offset = 128; /* 64 + 128 = 192 (overlaps rels[2] outside [140, 156)) */
    aot_file_generation++;
    assert(aot_paired_call_middle_exit(blocks, 3, 0, rels, code, 512, 140, 64));
    assert(!lats_middle_cache.valid_disjoint);

    /* 5. tcg_tb_lookup_fast with lats_tu_chunk_hint and region prologue bound */
    size_t buf_sz = 2 * 1024 * 1024;
    uint8_t *buf = NULL;
    assert(!posix_memalign((void **)&buf, CODE_GEN_ALIGN, buf_sz));
    memset(buf, 0, buf_sz);
    region.start_aligned = buf;
    region.total_size = buf_sz;
    region.stride = 1024 * 1024;
    region.n = 2;
    region.mtcc_n = 2;
    region.after_prologue[0] = 4096;

    /* Address inside Region 1 prologue [1MB, 1MB + 4096) must return NULL immediately */
    assert(tcg_tb_lookup_fast((uintptr_t)buf + 1024 * 1024 + 512) == NULL);

    /* Set up a TU in Region 0 after prologue (at offset 8192) */
    TBMini *hdr = (TBMini *)(buf + 8192);
    hdr[0].mtbp_uint64 = 1 | ((uint64_t)TU_TBM_MAGIC << 48);
    uint8_t *tu_code = buf + 8192 + CODE_GEN_ALIGN;
    NativeRecoverHeader rec_hdr = { .state_bytes = 0 };
    AOTNativeTUInfo nat = {
        .count = 1,
        .records = { {
            .entry = tu_code + 32768,
            .core = tu_code + 32768,
            .entry_bytes = 4096,
            .recover = &rec_hdr,
        } }
    };
    TranslationUnitPages pages = { .native = &nat };
    Data sdata = {
        .tu_size = 65536,
        .ras_tp_cold_offset = 0,
        .ras_tp_cold_insns = 0,
        .aot_native_call_entry = tu_code + 32768,
    };
    TranslationBlock owner = {
        .tc = { .ptr = tu_code, .offset_in_tu = 0 },
        .flags = HF_CS64_MASK | HF_CPL_MASK,
        .bool_flags = IS_AOT_TB | IS_TU_TB,
        .ir2_num = 4,
        .s_data = &sdata,
        .pages = &pages,
    };
    nat.records[0].tb = &owner;
    hdr[1].mtbp_uint64 = ((uintptr_t)&owner & MAKE_64BIT_MASK(0, 48)) | ((uint64_t)16 << 48);

    lats_register_tu_chunk_hint((uintptr_t)hdr, (uintptr_t)tu_code, (uintptr_t)tu_code + 65536);
    assert(tcg_tb_lookup_fast((uintptr_t)tu_code + 33000) == &owner);
    assert(tcg_tb_lookup_fast((uintptr_t)hdr + 4) == NULL);

    /* Stale/corrupted chunk hint falls back to backward scan and still resolves */
    lats_register_tu_chunk_hint((uintptr_t)buf + 4096, (uintptr_t)tu_code, (uintptr_t)tu_code + 65536);
    assert(tcg_tb_lookup_fast((uintptr_t)tu_code + 33000) == &owner);

    /* 6. n=1 ordinary jump continuation to LLVM native entry and replay */
    uint32_t *src_code = (uint32_t *)(buf + 16384);
    TranslationUnitPages src_pages = { .id = 1 };
    Data src_sdata = { .tu_size = 64 };
    TranslationBlock source = {
        .tc = { .ptr = src_code, .offset_in_tu = 0 },
        .flags = HF_CS64_MASK | HF_CPL_MASK,
        .bool_flags = IS_AOT_TB | IS_TU_TB,
        .jmp_insn_offset = { TB_JMP_OFFSET_INVALID, 8 },
        .jmp_reset_offset = { TB_JMP_OFFSET_INVALID, 16 },
        .jmp_cp_data = { 0, TB_JMP_CP_DATA_VALID },
        .s_data = &src_sdata,
        .pages = &src_pages,
    };
    void *direct_n1 = latxs_aot_direct_call_entry(&source, 1, &owner);
    assert(direct_n1 == tu_code + 32768);

    /* Without valid jmp_insn_offset[1], n=1 must return NULL */
    source.jmp_insn_offset[1] = TB_JMP_OFFSET_INVALID;
    assert(latxs_aot_direct_call_entry(&source, 1, &owner) == NULL);
    source.jmp_insn_offset[1] = 8;

    /* Replay checks the patched branch target against direct_n1 */
    source.jmp_dest[1] = (uintptr_t)&owner;
    uint32_t *slot = (uint32_t *)((uintptr_t)src_code + 8);
    *slot = aot_native_branch(0x50000000, (intptr_t)slot, (intptr_t)direct_n1);
    assert(latxs_aot_ordinary_jump_replay(&source, &owner) == direct_n1);

    /* Near target->tc.ptr but far native entry (> 128MB) must be rejected by latxs_plt_link_fits */
    void *far_entry = (void *)((uintptr_t)slot + (1ULL << 28));
    nat.records[0].entry = far_entry;
    nat.records[0].core = far_entry;
    sdata.aot_native_call_entry = far_entry;
    assert(latxs_aot_direct_call_entry(&source, 1, &owner) == NULL);

    /* When both target->tc.ptr and native entry are far (> 128MB), 2-insn pcaddu18i+jirl links and replays */
    owner.tc.ptr = (void *)((uintptr_t)slot + (1ULL << 28) - 4096);
    assert(latxs_aot_direct_call_entry(&source, 1, &owner) == far_entry);
    int64_t far_off = ((intptr_t)far_entry - (intptr_t)slot) >> 2;
    slot[0] = 0x1e000004U | ((((far_off + (1 << 15)) >> 16) & 0xfffff) << 5);
    slot[1] = 0x4c000080U | ((far_off & 0xffff) << 10);
    assert(latxs_aot_ordinary_jump_replay(&source, &owner) == far_entry);

    /* Fixed 8-byte IS_HRAS_CALL slot (n=0) accepts both near and far native entries */
    TranslationBlock hras_caller = source;
    hras_caller.bool_flags |= IS_HRAS_CALL;
    hras_caller.jmp_insn_offset[0] = 8;
    hras_caller.jmp_reset_offset[0] = 16;
    assert(latxs_aot_direct_call_entry(&hras_caller, 0, &owner) == far_entry);
    owner.tc.ptr = tu_code;
    nat.records[0].entry = tu_code + 32768;
    nat.records[0].core = tu_code + 32768;
    sdata.aot_native_call_entry = tu_code + 32768;
    assert(latxs_aot_direct_call_entry(&hras_caller, 0, &owner) == tu_code + 32768);

    /* 7. tb_htable_lookup_with_hint fast-paths valid public hint and falls back on HRAS-private or stale hint */
    owner.pc = 0x401000;
    owner.cs_base = 0;
    owner.cflags = 0;
    owner.misc_flags = 0;
    qht_fallback_tb = &owner;
    qht_calls = 0;
    fsht_reinsert_calls = 0;
    assert(tb_htable_lookup_with_hint(&dummy_cpu, 0x401000, 0, owner.flags, 0, &owner) == &owner);
    assert(qht_calls == 0 && fsht_reinsert_calls == 1);

    /* HRAS-private hint must be rejected and fall back to QHT */
    TranslationBlock hras_hint = owner;
    hras_hint.bool_flags |= IS_HRAS_CALL;
    assert(tb_htable_lookup_with_hint(&dummy_cpu, 0x401000, 0, owner.flags, 0, &hras_hint) == &owner);
    assert(qht_calls == 1 && fsht_reinsert_calls == 2);

    /* Unmapped physical page (-1) returns NULL before consulting hint or QHT */
    stub_phys_pc = -1;
    assert(tb_htable_lookup_with_hint(&dummy_cpu, 0x401000, 0, owner.flags, 0, &owner) == NULL);
    assert(qht_calls == 1 && fsht_reinsert_calls == 2);

    /* PCID supervisor lookup is selected only when its options permit it. */
    stub_phys_pc = 0x2000;
    option_pcid_cache = true;
    assert(tb_htable_lookup_with_hint(&dummy_cpu, 0x401000, 0, owner.flags, 0, &hras_hint) == &owner);
    assert(supervisor_lookup_calls == 1 && qht_calls == 2 && fsht_reinsert_calls == 3);
    option_sys_aot = true;
    assert(tb_htable_lookup_with_hint(&dummy_cpu, 0x401000, 0, owner.flags, 0, &hras_hint) == &owner);
    assert(supervisor_lookup_calls == 1 && qht_calls == 3 && fsht_reinsert_calls == 4);

    free(buf);
    puts("PASS: middle-exit fast/slow equivalence, chunk-hint lookup, and n=1 native jump replay");
    return 0;
}
"""

with tempfile.TemporaryDirectory(prefix="lats-middle-exit-hint-") as tmp:
    p = Path(tmp)
    (p / "test.c").write_text(c_prog)
    subprocess.run(
        ["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", str(p / "test.c"), "-o", str(p / "test")],
        check=True,
    )
    subprocess.run([str(p / "test")], check=True)
