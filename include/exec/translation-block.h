/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Definition of TranslationBlock.
 *  Copyright (c) 2003 Fabrice Bellard
 */

#ifndef EXEC_TRANSLATION_BLOCK_H
#define EXEC_TRANSLATION_BLOCK_H

#include "qemu/atomic.h"
#include "qemu/thread.h"
#include "qemu/interval-tree.h"
#include "exec/cpu-common.h"
#include "exec/target_page.h"

/*
 * Page tracking code uses ram addresses in system mode, and virtual
 * addresses in userspace mode.  Define tb_page_addr_t to be an
 * appropriate type.
 */
#if defined(CONFIG_USER_ONLY)
typedef vaddr tb_page_addr_t;
#define TB_PAGE_ADDR_FMT "%" VADDR_PRIx
#else
typedef ram_addr_t tb_page_addr_t;
#define TB_PAGE_ADDR_FMT RAM_ADDR_FMT
#endif

/*
 * Translation Cache-related fields of a TB.
 * This struct exists just for convenience; we keep track of TB's in a binary
 * search tree, and the only fields needed to compare TB's in the tree are
 * @ptr and @size.
 * Note: the address of search data can be obtained by adding @size to @ptr.
 */
struct tb_tc {
    const void *ptr;    /* pointer to the translated code */
#if defined(CONFIG_LATX_TCG_MODE) || defined(CONFIG_TCG_REGION_TREE_ENABLE)
    size_t size;
#endif
#ifdef CONFIG_LATX_TU
    size_t offset_in_tu;
#endif
    /* size_t search_size; */
};

typedef struct TBProfile
{
    uint64_t nr_code;       /* ir2 number */
    uint64_t jrra_in;       /* jrra in count, return count */
    uint64_t jrra_miss;     /* jrra miss count */
    uint64_t exec_times;    /* tb executes times */
    uint64_t exit_times;    /* tb context switch times */
    uint64_t exec_rdtime;   /* tb execution absolute time (10ns by rdtime) */
    uint64_t exec_tlbr_rdtime;   /* tb tlbrefill absolute time (10ns by rdtime) */
    uint64_t exec_mmio_rdtime; /* tb mmio rdtime */
    uint64_t exec_mmio_lock_rdtime; /* BQL lock for mmio */
    /* eflags related */
    uint64_t sta_eliminate; /* static eliminate, need plus exec_times */
    uint64_t sta_generate;  /* static generate, include eliminate */
    uint64_t sta_simulate;  /* static simulate */
    uint64_t dyn_eliminate; /* dynamic eliminate */
    uint64_t dyn_generate;  /* dynamic generate */
    uint64_t eflags_num;    /* dynamic TEST flag-generation events */
    uint64_t hamt_error[8];
    uint64_t pushpop_add;
    uint64_t pushpop_sub;
    uint64_t llvm_exec_times; /* LLVM body entries; not ordinary TB executions */
} TBProfile;

#define PROFILE_BEGIN (0)
#define PROFILE_END (1)
#define CLN_TB_PROFILE(tb)              (memset(&(tb->profile), 0, sizeof(TBProfile)))
#define GET_TB_PROFILE(tb, area)        (((tb->profile).area)           )
#define SET_TB_PROFILE(tb, area, value) (((tb->profile).area)  = (value))
#define ADD_TB_PROFILE(tb, area, value) (qatomic_add(&((tb->profile).area) ,value))

#if defined(CONFIG_LATX_TU) || defined(CONFIG_LATX_AOT)
#define TU_TB_INDEX_NEXT 0
#define TU_TB_INDEX_TARGET 1
typedef enum tu_tb_mode_type {
    TU_TB_MODE_NONE = 0,
    TU_TB_MODE_SWITCH_TO_TB,
    TU_TB_MODE_BROKEN,
    TU_TB_MODE_STATIC,
    TB_GEN_CODE,
    BAD_TB,
    TU_TB_MODE_PRIVATE
} tu_tb_mode_type;
#endif

#ifdef CONFIG_LATX_TU
typedef struct TranslationUnitPages TranslationUnitPages;
#endif

struct separated_data{
    TranslationBlock *next_tb[2];
    union {
        void *host_addr[2];
        /* AOT TU sidecars no longer need the translator's host addresses. */
        struct {
            const void *aot_native_call_entry;
            const void *aot_direct_native_entry;
        };
    };
    void *ir1;
    void *opt_ptr;
#ifdef CONFIG_LATX_TU
    union {
        uintptr_t x86_target_pc;
        uint32_t tu_unlink_stub_offset;
    };
    union {
        uintptr_t x86_next_pc;
        uint32_t tu_link_ins;
    };
    TranslationUnitPages *tu_pages;
#endif
    int32_t rel_start;
    int32_t rel_end;
#ifdef CONFIG_LATX_TU
    uint32_t ras_tp_cold_offset;
    uint32_t label_intexit[2];
    /* Source-owned asynchronous TU exits, independent of RAS repairs. */
    uint32_t tu_cold_exit_offset[2];
    int32_t tu_jmp[2];
    union {
        int32_t is_first_tb;
        int32_t tu_size;
    };
    uint16_t tu_generation_offset_patch[2];
#endif
    int16_t tb_too_large_ir1_index;
    int16_t sys_eob_ir1_index;
    uint8_t eflag_use;
    uint8_t eflag_use_real;
    uint8_t  _top_in;
    uint8_t  _top_out;
    bool is_invalidate;
    uint8_t last_ir1_type;
    uint8_t sys_eob_can_link : 1;
    uint8_t sys_eob_is_indir : 1;
    uint8_t sys_eob_use_fsht : 1;
    uint8_t has_hras_check_gate : 1;
    uint8_t ignore_eip_update : 1;
#ifdef CONFIG_LATX_TU
    /* Runtime repair length in four-byte instructions; zero means absent. */
    uint8_t ras_tp_cold_insns : 3;
    uint8_t tu_tb_mode;
#endif
};

#ifdef CONFIG_LATX_TU
/*
 * Successor page-check data is touched only by the optional direct/FSHT TU
 * link checks.  Keep it out of the regular TB payload and the 128-byte
 * general sidecar.  Those modes reserve this extension immediately after the
 * sidecar so generated code can still fetch each cached value with one load.
 */
struct tb_tu_link_data {
    TranslationUnitPages *next_tu_pages[2];
    uint64_t next_tu_generation_offset[2];
};
#endif

#ifdef CONFIG_LATX_TU
#define TRANSLATION_UNIT_MAX_PAGES 16
/* Complete cores use the same ordinary source-TB incoming-edge lifetime. */
typedef struct AOTNativeCoreBinding {
    const void *core;
    uint64_t generation_offset;
} AOTNativeCoreBinding;
typedef struct AOTNativeRecord {
    TranslationBlock *tb;
    uint32_t *entry;
    uint32_t *gate;
    uint32_t *stub;
    uint32_t *continuation;
    uint32_t gate_insn;
    uint32_t saved_ras_offset;
    uint32_t entry_bytes;
    const void *recover;
    uint32_t *core;
    uint32_t core_invalid_insn;
    uint32_t *direct_native_entry;
    uint32_t direct_entry_invalid_insn;
    uint64_t *binding_slot;
    vaddr callee_pc;
    /* A source TB can own multiple LLVM copies of one direct guest CALL. */
    uint32_t *direct_call_slot;
    uint32_t direct_call_closed;
    /* The emitter must retain required page-generation and IRQ checks. */
    bool direct_call_guards_emitted;
    bool direct_call_linked;
    bool linked_call_open;
    /* On caller invalidation replace the first continuation instruction by
     * a branch to a local cold exit. Its entry requires canonical RET state. */
    uint32_t *direct_continuation;
    uint32_t direct_continuation_invalid;
    uint32_t osr_count;
    struct TranslationBlock *osr_tbs[16];
    uint32_t *osr_entries[16];
} AOTNativeRecord;

typedef struct AOTNativeTUInfo {
    uint32_t count;
    AOTNativeRecord records[];
} AOTNativeTUInfo;

struct TranslationUnitPages {
    uint32_t id;
    struct {
        /* The page-aligned GVA retains all 64 bits.  Its unused low 12
         * bits store physical address bits 40..51. */
        uint64_t vp_phys_hi;
        uint64_t mix;
    } pages[TRANSLATION_UNIT_MAX_PAGES + 1];
    /* Runtime-only HRAS lookup, owned by the same MTCC region as its TBs. */
    TranslationBlock **hras_members;
    uint32_t hras_member_count;
    AOTNativeTUInfo *native;
};

/* Page offsets are zero even with LA57; the upper GVA bits are not. */
#define TU_PAGE_DESC_PAGE_BITS 12
#define TU_PAGE_DESC_MIX_PHYS_BITS 40
#define TU_PAGE_DESC_PHYS_HI_MASK ((UINT64_C(1) << TU_PAGE_DESC_PAGE_BITS) - 1)

static inline uint64_t tu_page_desc_pack_vp(uint64_t vp, uint64_t pp)
{
    return (vp & ~TU_PAGE_DESC_PHYS_HI_MASK) |
           ((pp >> TU_PAGE_DESC_MIX_PHYS_BITS) & TU_PAGE_DESC_PHYS_HI_MASK);
}

static inline uint64_t tu_page_desc_vp(uint64_t packed_vp)
{
    return packed_vp & ~TU_PAGE_DESC_PHYS_HI_MASK;
}

static inline uint64_t tu_page_desc_phys(uint64_t packed_vp, uint64_t mix)
{
    uint64_t low = (mix & ((UINT64_C(1) << 28) - 1)) << 12;
    uint64_t high = (packed_vp & TU_PAGE_DESC_PHYS_HI_MASK) <<
                    TU_PAGE_DESC_MIX_PHYS_BITS;

    return high | low;
}
#endif

typedef struct TBNextData {
    uint64_t pc;
    uint64_t page_addr_cache[2];
    uint64_t page_addr_ptw[2];
} TBNextData;

#define TB_NEXT_DATA_OFF(member, n) \
    (offsetof(TranslationBlock, next_tb_data[0].member) + \
     sizeof(TBNextData) * (n))

struct TranslationBlock {
    /* ------ The first cacheline ------ */
    /*
     * Guest PC corresponding to this block.  This must be the true
     * virtual address.  Therefore e.g. x86 stores EIP + CS_BASE, and
     * targets like Arm, MIPS, HP-PA, which reuse low bits for ISA or
     * privilege, must store those bits elsewhere.
     *
     * If CF_PCREL, the opcodes for the TranslationBlock are written
     * such that the TB is associated only with the physical page and
     * may be run in any virtual address context.  In this case, PC
     * must always be taken from ENV in a target-specific manner.
     * Unwind information is taken as offsets from the page, to be
     * deposited into the "current" PC.
     */
    vaddr pc;
    struct tb_tc tc;
    unsigned long hva;

#ifdef CONFIG_USER_ONLY
    IntervalTreeNode itree;
#endif

    /*
     * Target-specific data associated with the TranslationBlock, e.g.:
     * x86: the original user, the Code Segment virtual base,
     * arm: an extension of tb->flags,
     * s390x: instruction data for EXECUTE,
     * sparc: the next pc of the instruction queue (for delay slots).
     */
#if defined(CONFIG_LATX_TCG_MODE)
    uint64_t cs_base;
#else
    uint32_t cs_base;
#endif
    uint32_t ir2_num;
    int preorder_tb_offset;
    int inst_ptr_offset;
#ifndef CONFIG_LATX_TU
    uint32_t ras_tp_cold_offset;
#endif
    int label_pos_nocheck; /* entry without interrupt check */
    /* size of target code for this block (1 <= size <= TARGET_PAGE_SIZE) */
    uint16_t size;
    uint16_t icount;
    tb_page_addr_t page_addr[2];

    /* ------ The second cacheline ------ */
    uint32_t misc_flags;
    /* jmp_lock placed here to fill a 4-byte hole. Its documentation is below */
    QemuSpin jmp_lock;
    /*
     * Each TB has a NULL-terminated list (jmp_list_head) of incoming jumps.
     * Each TB can have two outgoing jumps, and therefore can participate
     * in two lists. The list entries are kept in jmp_list_next[2]. The least
     * significant bit (LSB) of the pointers in these lists is used to encode
     * which of the two list entries is to be used in the pointed TB.
     *
     * List traversals are protected by jmp_lock. The destination TB of each
     * outgoing jump is kept in jmp_dest[] so that the appropriate jmp_lock
     * can be acquired from any origin TB.
     *
     * jmp_dest[] are tagged pointers as well. The LSB is set when the TB is
     * being invalidated, so that no further outgoing jumps from it can be set.
     *
     * jmp_lock also protects the CF_INVALID cflag; a jump must not be chained
     * to a destination TB that has CF_INVALID set.
     */
    uintptr_t jmp_list_head;
    uintptr_t jmp_list_next[2];
    uintptr_t jmp_dest[2];
#ifdef CONFIG_LATX_TU
    struct separated_data *s_data;
    /* Software RAS link state is required for every regular TB. */
    uintptr_t ras_dest;
#define TB_JMP_RESET_OFFSET_INVALID 0xffff /* indicates no jump generated */
#define TU_UNLINK_STUB_INVALID 0xffffffff /* TU no unlink stub. */
#define TU_COLD_EXIT_INVALID 0
#define TU_COLD_EXIT_DIRECT_PENDING UINT32_MAX
/* Eligible indirect CALL/JMP emits its interrupt exit before lookup. */
#define TU_INDIRECT_EXIT_INLINE_PENDING (UINT32_MAX - 1)
#define TU_COLD_EXIT_JMP_INTERRUPT_DEFERRED (UINT32_MAX - 3)
#endif
    /* ------ The third cacheline ------ */
/* Note that TCG_MAX_INSNS is 512; we validate this match elsewhere. */
//#define CF_COUNT_MASK    0x000001ff
#define CF_COUNT_MASK    0x0000007f
#define CF_NO_GOTO_TB    0x00000200 /* Do not chain with goto_tb */
#define CF_NO_GOTO_PTR   0x00000400 /* Do not chain with goto_ptr */
#define CF_SINGLE_STEP   0x00000800 /* gdbstub single-step in effect */
#define CF_NO_ACTIVE     0x00001000 /* AOT TB but not active */
#define CF_LAST_IO       0x00008000 /* Last insn may be an IO access.  */
#define CF_MEMI_ONLY     0x00010000 /* Only instrument memory ops */
#define CF_USE_ICOUNT    0x00020000
#define CF_INVALID       0x00040000 /* TB is stale. Set with @jmp_lock held */
#define CF_PARALLEL      0x00080000 /* Generate code for a parallel context */
#define CF_NOIRQ         0x00100000 /* Generate an uninterruptible TB */
#define CF_PCREL         0x00200000 /* Opcodes in TB are PC-relative */
#define CF_CLUSTER_MASK  0xff000000 /* Top 8 bits are cluster ID */
#define CF_CLUSTER_SHIFT 24

    uintptr_t page_next[2];

    /* The following data are used to directly call another TB from
     * the code of this one. This can be done either by emitting direct or
     * indirect native jump instructions. These jumps are reset so that the TB
     * just continues its execution. The TB can be linked to another one by
     * setting one of the jump targets (or patching the jump instruction). Only
     * two of such jumps are supported.
     */
#define TB_JMP_OFFSET_INVALID 0xffff /* indicates no jump generated */
    uint16_t jmp_reset_offset[2]; /* offset of original jump target */
    uint16_t jmp_insn_offset[2];  /* offset of direct jump insn */
    /* QEMU TCG records writable jump slots while LAT records the original
     * two instructions at those slots.  A process selects exactly one of
     * these execution modes, so the arrays can share storage. */
    union {
        uintptr_t jmp_target_addr[2]; /* QEMU TCG target address */
        uint64_t jmp_inst_hold[2];    /* LAT original jump instructions */
    };
    uint32_t flags; /* flags defining in which context the code was generated */
    uint32_t cflags;    /* compile flags */

#define TB_JMP_CP_DATA_OFF      0x0ff
#define TB_JMP_CP_DATA_VALID    0x100
#define TB_JMP_CP_DATA_LINKED   0x200
    uint16_t jmp_cp_data[2];
    uint8_t link_opt[2];
    uint8_t int_opt[2];
    uint64_t signal_unlink[2];

#define TARGET1_ELIMINATE 0x01
#define OPT_BCC 0x02
#define IS_ENABLE_JRRA 0x04
#define IS_AOT_TB 0x08
#define IS_TUNNEL_LIB 0x10
#define CAN_DO_HAMT 0x20
#define IS_TU_TB 0x40
#define IS_HRAS_CALL 0x80 /* Paired CALL slot; reset-insn is its 4/8-byte size. */
    struct {
        uint8_t eflag_type;
        uint8_t os; /* 8, 16, 32, 64 */
        uint8_t opnd0;
        uint8_t opnd1;
        uint16_t pcoffset;
    } ptn;
    uint8_t bool_flags;
    uint8_t opt_mmio_type;
#define PTN_EFLAG_TYPE_INVALID  0
#define PTN_EFLAG_TYPE_X86SUB   1
#define PTN_EFLAG_TYPE_X86AND   2
#define PTN_EFLAG_TYPE_X86MTFLAG 3
#define PTN_EFLAG_TYPE_X86OR    5
#define PTN_EFLAG_TYPE_X86SRL   6
/*
 * option sysflagtb (TB-mode cross-TB dead-flag elimination) keeps its per-TB
 * state in the ptn payload while ptn.eflag_type stays
 * PTN_EFLAG_TYPE_INVALID.  The HAMT deferred-recovery path keys on
 * eflag_type and therefore never interprets these bits, and the option is
 * only honoured in ordinary TB mode with no TU/AOT runtime (enforced in
 * latxs_options_check_sysflagtb()), where nothing else writes the payload.
 * The fields live here so TranslationBlock keeps its pinned 352-byte size.
 *
 *   ptn.pcoffset: bits 0..5 = 0 only for a verified safe entry; 0x3f
 *                 conservatively rejects deferred-flag links;
 *                 bit 15 = 1 once the translation-time scan has run.
 *   ptn.os:       bit n = this TB's exit edge n skips its TB-final M-EFLAGS
 *                 materialisation on the direct-link fast path.
 */
#define PTN_EFLAGTB_USE_MASK    0x003f
#define PTN_EFLAGTB_VALID_BIT   0x8000
#define TB_EFLAGTB_SKIP(tb)     ((tb)->ptn.os)
#define TB_EFLAGTB_USE(tb)      ((tb)->ptn.pcoffset & PTN_EFLAGTB_USE_MASK)
#define TB_EFLAGTB_USE_VALID(tb) \
    (((tb)->ptn.pcoffset & PTN_EFLAGTB_VALID_BIT) != 0)
    uint64_t page_addr_mix;
#define tb_mix_page_addr_n(val, i, n)  \
    ( (((uint64_t)(val))>>(i)) & ((0x1ULL<<(n))-1) )
#define tb_cal_page_addr_mix(a, b)      \
    (tb_mix_page_addr_n(a, 12, 36) << 28) | tb_mix_page_addr_n(b, 12, 28)
#define TB_OPT_MMIO_NONE    0
#define TB_OPT_MMIO_APIC    1
#define TB_OPT_MMIO_HPET    2

    uint64_t next_pc[2];
#ifdef CONFIG_LATX_TU
    void *jit_tucheck_external[2];
#endif
    void *preorder_next;
    union {
        uint64_t search_size;
        void *search_ptr;
    };
    TBNextData next_tb_data[2];
#ifndef CONFIG_LATX_TU
    int rel_start;
    int rel_end;
    void *opt_ptr;
#endif
#ifdef CONFIG_LATX_PROFILER
    TBProfile profile __attribute__((aligned(8)));
#endif
#ifdef CONFIG_LATX_DEBUG
    int inhibit_irq_type;
#endif
#ifndef CONFIG_LATX_TU
    uint8_t ras_tp_cold_insns;
#endif
};

static inline void tb_next_data_reset(TranslationBlock *tb)
{
    memset(tb->next_tb_data, 0, sizeof(tb->next_tb_data));
    tb->next_tb_data[0].pc = tb->pc;
    tb->next_tb_data[1].pc = tb->pc;
}

#ifdef CONFIG_LATX_TU
/* Cold/AOT and optional TU page-check state lives in the private sidecar.
 * The allocator gives a persistent sidecar to every mode that keeps this
 * state after translation; ordinary translation may reuse the shared one. */
#define TB_REL_START(tb) ((tb)->s_data->rel_start)
#define TB_REL_END(tb) ((tb)->s_data->rel_end)
void *tb_opt_ptr(const TranslationBlock *tb);
void tb_set_opt_ptr(TranslationBlock *tb, void *ptr);
#define TB_TU_PAGES(tb) ((tb)->s_data->tu_pages)
#define TB_PRIVATE_S_DATA_OFFSET(member) \
    (sizeof(TranslationBlock) + offsetof(struct separated_data, member))
#define TB_TU_LINK_DATA_OFFSET(member) \
    (sizeof(TranslationBlock) + sizeof(struct separated_data) + \
     offsetof(struct tb_tu_link_data, member))
static inline struct tb_tu_link_data *tb_tu_link_data(TranslationBlock *tb)
{
    return (void *)((uint8_t *)tb + sizeof(TranslationBlock) +
                    sizeof(struct separated_data));
}
#define TB_NEXT_TU_PAGES(tb, n) \
    (tb_tu_link_data(tb)->next_tu_pages[(n)])
#define TB_NEXT_TU_GENERATION_OFFSET(tb, n) \
    (tb_tu_link_data(tb)->next_tu_generation_offset[(n)])
#else
#define TB_REL_START(tb) ((tb)->rel_start)
#define TB_REL_END(tb) ((tb)->rel_end)
static inline void *tb_opt_ptr(const TranslationBlock *tb)
{
    return tb->opt_ptr;
}

static inline void tb_set_opt_ptr(TranslationBlock *tb, void *ptr)
{
    tb->opt_ptr = ptr;
}
#endif

static inline void tb_set_ptn_pcoffset(TranslationBlock *tb, int64_t offset)
{
    /* A TB cannot span more than one target page, so the pattern PC offset
     * is bounded by TARGET_PAGE_SIZE.  Keep the check at every producer,
     * including AOT input, rather than silently truncating malformed data. */
    g_assert(offset >= 0 && offset <= UINT16_MAX);
    tb->ptn.pcoffset = offset;
}

/* Translation-time exceptions can shorten an already allocated TB.  Split
 * recovery data follows the search array at icount * sizeof(int), so move
 * its live prefix before publishing the smaller count. */
static inline void tb_shrink_icount(TranslationBlock *tb, uint16_t count,
                                     bool split_pushpop)
{
    g_assert(count <= tb->icount);
#ifdef CONFIG_LATX_TU
    if (split_pushpop && tb->search_ptr && count != tb->icount) {
        int *search = tb->search_ptr;

        memmove(search + count, search + tb->icount, count * sizeof(int));
    }
#endif
    tb->icount = count;
}

#ifdef CONFIG_LATX_TU
static inline bool tb_is_tu(const TranslationBlock *tb)
{
    return tb->bool_flags & IS_TU_TB;
}

bool tcg_tb_has_private_s_data(const TranslationBlock *tb);

/* Executable code that owns pages and edges, but no global lookup key.
 * Never read a non-TU's shared translation scratch as persistent state. */
static inline bool tb_is_lookup_private(const TranslationBlock *tb)
{
    return (tb_is_tu(tb) || tcg_tb_has_private_s_data(tb)) &&
        qatomic_read(&tb->s_data->tu_tb_mode) == TU_TB_MODE_PRIVATE;
}

static inline uintptr_t tb_tu_identity(const TranslationBlock *tb)
{
    return (uintptr_t)tb->tc.ptr - tb->tc.offset_in_tu;
}
#endif

#define TB_MAGIC 0xbeefUL
#define HOST_VIRT_ADDR_SPACE_BITS 48
#ifdef CONFIG_LATX_TU
/* A TU member table has a count in place of the ordinary TB pointer. */
#define TU_TBM_MAGIC 0xbeedUL
/* Capacity of a single TU generation attempt. */
#define MAX_TB_IN_TS 1024
#endif
typedef struct MagicTBP {
    unsigned short tb_pointer[3]; /* 48 bits are reserved for vadd */
    unsigned short magic; /* Header tag, or a TU member's code byte count. */
} MagicTBP;

#if !defined(CONFIG_TCG_REGION_TREE_ENABLE)
typedef struct TBMini {
    union {
        MagicTBP mtbp_struct;
        uint64_t mtbp_uint64;
    };
} TBMini;
#endif

/* The alignment given to TranslationBlock during allocation. */
#define CODE_GEN_ALIGN  32

#endif /* EXEC_TRANSLATION_BLOCK_H */
