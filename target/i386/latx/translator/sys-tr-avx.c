/* SPDX-License-Identifier: GPL-2.0-only */

#include "common.h"
#include "cpu.h"
#include "la-ir2.h"
#include "lsenv.h"
#include "latx-options.h"
#include "reg-alloc.h"
#include "sys-excp.h"
#include "sys-aot.h"
#include "translate.h"

#if defined(CONFIG_SOFTMMU) && !defined(CONFIG_LATX_TCG_MODE)

typedef struct AvxBinaryOp {
    IR2_OPCODE op;
    bool avx2_ymm;
} AvxBinaryOp;

#define AVX_OP(x86, lsx) [X86_INS_##x86] = { LISA_##lsx, false }
#define AVX_INT_OP(x86, lsx) [X86_INS_##x86] = { LISA_##lsx, true }

static const AvxBinaryOp avx_binary_ops[X86_INS_ENDING] = {
    AVX_OP(VANDPD, VAND_V),
    AVX_OP(VANDPS, VAND_V),
    AVX_OP(VANDNPD, VANDN_V),
    AVX_OP(VANDNPS, VANDN_V),
    AVX_OP(VORPD, VOR_V),
    AVX_OP(VORPS, VOR_V),
    AVX_OP(VXORPD, VXOR_V),
    AVX_OP(VXORPS, VXOR_V),
    AVX_INT_OP(VPAND, VAND_V),
    AVX_INT_OP(VPANDN, VANDN_V),
    AVX_INT_OP(VPOR, VOR_V),
    AVX_INT_OP(VPXOR, VXOR_V),
    AVX_INT_OP(VPADDB, VADD_B),
    AVX_INT_OP(VPADDW, VADD_H),
    AVX_INT_OP(VPADDD, VADD_W),
    AVX_INT_OP(VPADDQ, VADD_D),
    AVX_INT_OP(VPSUBB, VSUB_B),
    AVX_INT_OP(VPSUBW, VSUB_H),
    AVX_INT_OP(VPSUBD, VSUB_W),
    AVX_INT_OP(VPSUBQ, VSUB_D),
    AVX_INT_OP(VPADDSB, VSADD_B),
    AVX_INT_OP(VPADDSW, VSADD_H),
    AVX_INT_OP(VPADDUSB, VSADD_BU),
    AVX_INT_OP(VPADDUSW, VSADD_HU),
    AVX_INT_OP(VPSUBSB, VSSUB_B),
    AVX_INT_OP(VPSUBSW, VSSUB_H),
    AVX_INT_OP(VPSUBUSB, VSSUB_BU),
    AVX_INT_OP(VPSUBUSW, VSSUB_HU),
    AVX_INT_OP(VPAVGB, VAVGR_BU),
    AVX_INT_OP(VPAVGW, VAVGR_HU),
    AVX_INT_OP(VPMULLW, VMUL_H),
    AVX_INT_OP(VPMULLD, VMUL_W),
    AVX_INT_OP(VPMULHW, VMUH_H),
    AVX_INT_OP(VPMULHUW, VMUH_HU),
    AVX_INT_OP(VPMULDQ, VMULWEV_D_W),
    AVX_INT_OP(VPMULUDQ, VMULWEV_D_WU),
    AVX_INT_OP(VPMINSB, VMIN_B),
    AVX_INT_OP(VPMINSW, VMIN_H),
    AVX_INT_OP(VPMINSD, VMIN_W),
    AVX_INT_OP(VPMINUB, VMIN_BU),
    AVX_INT_OP(VPMINUW, VMIN_HU),
    AVX_INT_OP(VPMINUD, VMIN_WU),
    AVX_INT_OP(VPMAXSB, VMAX_B),
    AVX_INT_OP(VPMAXSW, VMAX_H),
    AVX_INT_OP(VPMAXSD, VMAX_W),
    AVX_INT_OP(VPMAXUB, VMAX_BU),
    AVX_INT_OP(VPMAXUW, VMAX_HU),
    AVX_INT_OP(VPMAXUD, VMAX_WU),
    AVX_INT_OP(VPCMPEQB, VSEQ_B),
    AVX_INT_OP(VPCMPEQW, VSEQ_H),
    AVX_INT_OP(VPCMPEQD, VSEQ_W),
    AVX_INT_OP(VPCMPEQQ, VSEQ_D),
    AVX_INT_OP(VPCMPGTB, VSLT_B),
    AVX_INT_OP(VPCMPGTW, VSLT_H),
    AVX_INT_OP(VPCMPGTD, VSLT_W),
    AVX_INT_OP(VPCMPGTQ, VSLT_D),
    AVX_INT_OP(VPUNPCKLBW, VILVL_B),
    AVX_INT_OP(VPUNPCKLWD, VILVL_H),
    AVX_INT_OP(VPUNPCKLDQ, VILVL_W),
    AVX_INT_OP(VPUNPCKLQDQ, VILVL_D),
    AVX_INT_OP(VPUNPCKHBW, VILVH_B),
    AVX_INT_OP(VPUNPCKHWD, VILVH_H),
    AVX_INT_OP(VPUNPCKHDQ, VILVH_W),
    AVX_INT_OP(VPUNPCKHQDQ, VILVH_D),
    AVX_OP(VUNPCKLPS, VILVL_W),
    AVX_OP(VUNPCKLPD, VILVL_D),
    AVX_OP(VUNPCKHPS, VILVH_W),
    AVX_OP(VUNPCKHPD, VILVH_D),
};

#undef AVX_OP
#undef AVX_INT_OP

typedef struct AvxOperand {
    IR2_OPND low;
    IR2_OPND high;
} AvxOperand;

typedef struct AvxWiden {
    unsigned from;
    unsigned to;
    bool sign;
} AvxWiden;

static const AvxWiden avx_widens[X86_INS_ENDING] = {
    [X86_INS_VPMOVSXBW] = { 0, 1, true },
    [X86_INS_VPMOVSXBD] = { 0, 2, true },
    [X86_INS_VPMOVSXBQ] = { 0, 3, true },
    [X86_INS_VPMOVSXWD] = { 1, 2, true },
    [X86_INS_VPMOVSXWQ] = { 1, 3, true },
    [X86_INS_VPMOVSXDQ] = { 2, 3, true },
    [X86_INS_VPMOVZXBW] = { 0, 1, false },
    [X86_INS_VPMOVZXBD] = { 0, 2, false },
    [X86_INS_VPMOVZXBQ] = { 0, 3, false },
    [X86_INS_VPMOVZXWD] = { 1, 2, false },
    [X86_INS_VPMOVZXWQ] = { 1, 3, false },
    [X86_INS_VPMOVZXDQ] = { 2, 3, false },
};

static bool avx_reserved_vvvv(IR1_INST *insn)
{
    const uint8_t *prefix = insn->info->detail->x86.opcode;
    uint8_t vvvv = prefix[prefix[0] == 0xc4 ? 2 : 1] & 0x78;

    if (vvvv == 0x78) {
        return false;
    }
    if (avx_widens[ir1_opcode(insn)].to) {
        return true;
    }
    switch (ir1_opcode(insn)) {
    case X86_INS_VMOVSS:
    case X86_INS_VMOVSD:
        return ir1_opnd_num(insn) == 2;
    case X86_INS_VPERMILPS:
    case X86_INS_VPERMILPD:
        return ir1_opnd_is_imm(ir1_get_opnd(insn, 2));
    case X86_INS_VZEROALL:
    case X86_INS_VZEROUPPER:
    case X86_INS_VMOVAPD:
    case X86_INS_VMOVAPS:
    case X86_INS_VMOVDQA:
    case X86_INS_VMOVDQU:
    case X86_INS_VMOVUPD:
    case X86_INS_VMOVUPS:
    case X86_INS_VLDDQU:
    case X86_INS_VMOVD:
    case X86_INS_VMOVQ:
    case X86_INS_VPMOVMSKB:
    case X86_INS_VMOVMSKPS:
    case X86_INS_VMOVMSKPD:
    case X86_INS_VPTEST:
    case X86_INS_VTESTPS:
    case X86_INS_VTESTPD:
    case X86_INS_VPERMPD:
    case X86_INS_VPERMQ:
    case X86_INS_VEXTRACTF128:
    case X86_INS_VEXTRACTI128:
    case X86_INS_VEXTRACTPS:
    case X86_INS_VPEXTRB:
    case X86_INS_VPEXTRW:
    case X86_INS_VPEXTRD:
    case X86_INS_VPEXTRQ:
    case X86_INS_VBROADCASTF128:
    case X86_INS_VBROADCASTI128:
    case X86_INS_VBROADCASTSD:
    case X86_INS_VBROADCASTSS:
    case X86_INS_VPBROADCASTB:
    case X86_INS_VPBROADCASTW:
    case X86_INS_VPBROADCASTD:
    case X86_INS_VPBROADCASTQ:
    case X86_INS_VAESIMC:
    case X86_INS_VAESKEYGENASSIST:
    case X86_INS_VPSHUFD:
    case X86_INS_VPABSB:
    case X86_INS_VPABSW:
    case X86_INS_VPABSD:
    /* Two-operand conversions take no VEX.vvvv source. */
    case X86_INS_VCVTPH2PS:
    case X86_INS_VCVTPS2PH:
    case X86_INS_VMOVSHDUP:
    case X86_INS_VMOVSLDUP:
    case X86_INS_VPSHUFLW:
    case X86_INS_VPSHUFHW:
    case X86_INS_VRCPPS:
    case X86_INS_VRSQRTPS:
    case X86_INS_VLDMXCSR:
    case X86_INS_VSTMXCSR:
    /* VEX.128.66.0F38.WIG 41 /r: VEX.vvvv is reserved and must be 1111b. */
    case X86_INS_VPHMINPOSUW:
        return true;
    default:
        return false;
    }
}

static bool avx_unavailable(IR1_INST *insn, bool avx2)
{
    TRANSLATION_DATA *td = lsenv->tr_data;

    if (avx_reserved_vvvv(insn) ||
        !(td->sys.cpuid_ext_features & CPUID_EXT_AVX) ||
        !(td->sys.flags & HF_AVX_EN_MASK) || (td->sys.flags & HF_EM_MASK) ||
        (avx2 && !(td->sys.cpuid_7_0_ebx_features & CPUID_7_0_EBX_AVX2))) {
        latxs_tr_gen_excp_illegal_op(insn, 1);
        return true;
    }
    if (td->sys.flags & HF_TS_MASK) {
        latxs_tr_gen_excp_prex(insn, 1);
        return true;
    }
    return false;
}

static IR2_OPND avx_high_base(void)
{
    IR2_OPND base = latxs_ra_alloc_itemp();

    latxs_load_imm64(&base, offsetof(CPUX86State, ymm_high));
    latxs_append_ir2_opnd3(LISA_ADD_D, &base, &latxs_env_ir2_opnd, &base);
    return base;
}

static void avx_copy(IR2_OPND *dest, IR2_OPND *src)
{
    if (!latxs_ir2_opnd_cmp(dest, src)) {
        latxs_append_ir2_opnd3(LISA_VOR_V, dest, src, src);
    }
}

static void avx_clear(IR2_OPND *dest)
{
    latxs_append_ir2_opnd3(LISA_VXOR_V, dest, dest, dest);
}

static void avx_zero(bool all)
{
    unsigned count = lsenv->tr_data->sys.code64 ? 16 : 8;
    IR2_OPND base = avx_high_base();
    IR2_OPND zero = latxs_ra_alloc_ftemp();

    avx_clear(&zero);
    for (unsigned i = 0; i < count; i++) {
        if (all) {
            IR2_OPND xmm = latxs_ra_alloc_xmm(i);
            avx_clear(&xmm);
        }
        latxs_append_ir2_opnd2i(LISA_VST, &zero, &base, i * 16);
    }
    latxs_ra_free_temp(&base);
}

static IR2_OPND avx_addr(IR1_INST *insn, IR1_OPND *mem, unsigned align)
{
    IR2_OPND addr = latxs_ra_alloc_itemp();

    latxs_convert_mem_opnd_no_offset(&addr, mem);
    if (align) {
        IR2_OPND misaligned = latxs_ra_alloc_itemp();
        IR2_OPND aligned = latxs_ir2_opnd_new_label();

        latxs_append_ir2_opnd2i(LISA_ANDI, &misaligned, &addr, align - 1);
        latxs_append_ir2_opnd2(LISA_BEQZ, &misaligned, &aligned);
        latxs_ra_free_temp(&misaligned);
        latxs_tr_gen_excp_gp(insn, 0);
        latxs_append_ir2_opnd1(LISA_LABEL, &aligned);
    }
    return addr;
}

static IR2_OPND avx_load_scalar(IR1_INST *insn, IR1_OPND *mem, IR2_OPCODE op)
{
    IR2_OPND addr = avx_addr(insn, mem, 0);
    IR2_OPND scalar = latxs_ra_alloc_itemp();

    gen_ldst_softmmu_helper(op, &scalar, &addr, 0, 1);
    latxs_ra_free_temp(&addr);
    return scalar;
}

static void avx_mem_lane(IR2_OPND *vec, IR2_OPND *addr, int offset, bool load)
{
    if (option_fast_fpr_ldst) {
        gen_ldst_softmmu_helper(load ? LISA_VLD : LISA_VST,
                              vec, addr, offset, 1);
        return;
    }

    IR2_OPND word = latxs_ra_alloc_itemp();
    for (int i = 0; i < 2; i++) {
        if (load) {
            gen_ldst_softmmu_helper(LISA_LD_D, &word, addr, offset + i * 8, 1);
            latxs_append_ir2_opnd2i(LISA_VINSGR2VR_D, vec, &word, i);
        } else {
            latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_D, &word, vec, i);
            gen_ldst_softmmu_helper(LISA_ST_D, &word, addr, offset + i * 8, 1);
        }
    }
    latxs_ra_free_temp(&word);
}

static AvxOperand avx_load(IR1_INST *insn, IR1_OPND *op, bool ymm, bool aligned)
{
    AvxOperand value = { 0 };

    if (ir1_opnd_is_mem(op)) {
        IR2_OPND addr = avx_addr(insn, op, aligned ? (ymm ? 32 : 16) : 0);

        value.low = latxs_ra_alloc_ftemp();
        avx_mem_lane(&value.low, &addr, 0, true);
        if (ymm) {
            value.high = latxs_ra_alloc_ftemp();
            avx_mem_lane(&value.high, &addr, 16, true);
        }
        latxs_ra_free_temp(&addr);
    } else {
        int reg = ir1_opnd_base_reg_num(op);
        value.low = latxs_ra_alloc_xmm(reg);

        if (ymm) {
            IR2_OPND base = avx_high_base();
            value.high = latxs_ra_alloc_ftemp();
            latxs_append_ir2_opnd2i(LISA_VLD, &value.high, &base, reg * 16);
            latxs_ra_free_temp(&base);
        }
    }
    return value;
}

/* A VEX.128 register destination clears its corresponding upper half. */
static void avx_write_high(int reg, IR2_OPND *value)
{
    if (value) {
        IR2_OPND base = avx_high_base();

        latxs_append_ir2_opnd2i(LISA_VST, value, &base, reg * 16);
        latxs_ra_free_temp(&base);
    } else {
        unsigned offset = offsetof(CPUX86State, ymm_high) + reg * 16;

        QEMU_BUILD_BUG_ON(offsetof(CPUX86State, ymm_high) +
                          sizeof_field(CPUX86State, ymm_high) > 32768);
        latxs_append_ir2_opnd2i(LISA_STPTR_D, &la_zero, &latxs_env_ir2_opnd,
                                offset / 4);
        latxs_append_ir2_opnd2i(LISA_STPTR_D, &la_zero, &latxs_env_ir2_opnd,
                                (offset + 8) / 4);
    }
}

static void avx_move(IR1_INST *insn, IR1_OPND *dst, bool ymm, bool aligned)
{
    AvxOperand value = avx_load(insn, ir1_get_opnd(insn, 1), ymm, aligned);

    if (ir1_opnd_is_mem(dst)) {
        IR2_OPND addr = avx_addr(insn, dst, aligned ? (ymm ? 32 : 16) : 0);
        avx_mem_lane(&value.low, &addr, 0, false);
        if (ymm) {
            avx_mem_lane(&value.high, &addr, 16, false);
        }
        latxs_ra_free_temp(&addr);
    } else {
        int reg = ir1_opnd_base_reg_num(dst);
        IR2_OPND low = latxs_ra_alloc_xmm(reg);

        avx_copy(&low, &value.low);
        avx_write_high(reg, ymm ? &value.high : NULL);
    }
}

static void avx_move_scalar(IR1_INST *insn, bool qword)
{
    IR1_OPND *dst = ir1_get_opnd(insn, 0);
    bool merge = ir1_opnd_num(insn) == 3;
    IR1_OPND *src = ir1_get_opnd(insn, merge ? 2 : 1);
    IR2_OPCODE pick = qword ? LISA_VPICKVE2GR_D : LISA_VPICKVE2GR_WU;
    IR2_OPCODE insert = qword ? LISA_VINSGR2VR_D : LISA_VINSGR2VR_W;
    IR2_OPND scalar;

    if (ir1_opnd_is_xmm(src)) {
        IR2_OPND xmm = latxs_ra_alloc_xmm(ir1_opnd_base_reg_num(src));

        scalar = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd2i(pick, &scalar, &xmm, 0);
    } else if (ir1_opnd_is_mem(src)) {
        scalar = avx_load_scalar(insn, src, qword ? LISA_LD_D : LISA_LD_WU);
    } else {
        scalar = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(src));
    }

    if (ir1_opnd_is_xmm(dst)) {
        int reg = ir1_opnd_base_reg_num(dst);
        IR2_OPND low = latxs_ra_alloc_xmm(reg);

        if (merge) {
            IR1_OPND *merge_src = ir1_get_opnd(insn, 1);
            IR2_OPND xmm = latxs_ra_alloc_xmm(ir1_opnd_base_reg_num(merge_src));

            avx_copy(&low, &xmm);
        } else {
            avx_clear(&low);
        }
        latxs_append_ir2_opnd2i(insert, &low, &scalar, 0);
        avx_write_high(reg, NULL);
    } else if (ir1_opnd_is_mem(dst)) {
        IR2_OPND addr = avx_addr(insn, dst, 0);

        gen_ldst_softmmu_helper(qword ? LISA_ST_D : LISA_ST_W,
                               &scalar, &addr, 0, 1);
        latxs_ra_free_temp(&addr);
    } else {
        latxs_store_ir2_to_ir1(&scalar, dst);
    }
}

static void avx_movemask(IR1_INST *insn, bool ymm)
{
    IR1_OPCODE opcode = ir1_opcode(insn);
    IR1_OPND *dst = ir1_get_opnd(insn, 0);
    AvxOperand src = avx_load(insn, ir1_get_opnd(insn, 1), ymm, false);
    IR2_OPND mask = latxs_ra_alloc_ftemp();
    IR2_OPND result = latxs_ra_alloc_itemp();
    IR2_OPCODE op = LISA_VMSKLTZ_B;
    unsigned bits = 16;

    if (opcode == X86_INS_VMOVMSKPS) {
        op = LISA_VMSKLTZ_W;
        bits = 4;
    } else if (opcode == X86_INS_VMOVMSKPD) {
        op = LISA_VMSKLTZ_D;
        bits = 2;
    }
    latxs_append_ir2_opnd2(op, &mask, &src.low);
    latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_WU, &result, &mask, 0);
    if (ymm) {
        IR2_OPND high = latxs_ra_alloc_itemp();

        latxs_append_ir2_opnd2(op, &mask, &src.high);
        latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_WU, &high, &mask, 0);
        latxs_append_ir2_opnd2i(LISA_SLLI_D, &high, &high, bits);
        latxs_append_ir2_opnd3(LISA_OR, &result, &result, &high);
        latxs_ra_free_temp(&high);
    }
    latxs_store_ir2_to_ir1(&result, dst);
}

static void avx_test(IR1_INST *insn, bool ymm)
{
    IR1_OPCODE opcode = ir1_opcode(insn);
    AvxOperand a = avx_load(insn, ir1_get_opnd(insn, 0), ymm, false);
    AvxOperand b = avx_load(insn, ir1_get_opnd(insn, 1), ymm, false);
    IR2_OPND temp = latxs_ra_alloc_ftemp();
    IR2_OPND upper = ymm ? latxs_ra_alloc_ftemp() : temp;
    IR2_OPND sign = latxs_ra_alloc_itemp();
    IR2_OPND flags = latxs_ra_alloc_itemp();
    IR2_OPCODE sign_mask = opcode == X86_INS_VTESTPS ? LISA_VMSKLTZ_W :
                           opcode == X86_INS_VTESTPD ? LISA_VMSKLTZ_D :
                                                      LISA_INVALID;
    const IR2_OPCODE ops[] = { LISA_VAND_V, LISA_VANDN_V };

    for (unsigned i = 0; i < ARRAY_SIZE(ops); i++) {
        /* x86 CF tests (~a & b). LSX vandn.v inverts its first input. */
        latxs_append_ir2_opnd3(ops[i], &temp, &a.low, &b.low);
        if (ymm) {
            latxs_append_ir2_opnd3(ops[i], &upper, &a.high, &b.high);
            latxs_append_ir2_opnd3(LISA_VOR_V, &temp, &temp, &upper);
        }
        if (sign_mask != LISA_INVALID) {
            latxs_append_ir2_opnd2(sign_mask, &temp, &temp);
        }
        latxs_append_ir2_opnd2(LISA_VSETEQZ_V, &latxs_fcc0_ir2_opnd, &temp);
        latxs_append_ir2_opnd2(LISA_MOVCF2GR, i ? &sign : &flags,
                               &latxs_fcc0_ir2_opnd);
    }
    latxs_append_ir2_opnd2i(LISA_SLLI_D, &flags, &flags, 6);
    latxs_append_ir2_opnd3(LISA_OR, &flags, &flags, &sign);
    latxs_append_ir2_opnd1i(LISA_X86MTFLAG, &flags, 0x3f);
}

static void avx_masked_lane(IR2_OPND *data, IR2_OPND *mask, IR2_OPND *addr,
                            bool store, unsigned width, unsigned offset)
{
    bool qword = width == 8;
    IR2_OPCODE pick = qword ? LISA_VPICKVE2GR_D : LISA_VPICKVE2GR_W;
    IR2_OPCODE insert = qword ? LISA_VINSGR2VR_D : LISA_VINSGR2VR_W;
    IR2_OPCODE mem_op = store ? (qword ? LISA_ST_D : LISA_ST_W) :
                                (qword ? LISA_LD_D : LISA_LD_WU);
    IR2_OPND scalar = latxs_ra_alloc_itemp();

    if (!store) {
        avx_clear(data);
    }
    for (unsigned i = 0; i < 16 / width; i++) {
        IR2_OPND skip = latxs_ir2_opnd_new_label();

        /* The signed extraction tests the sign bit of each mask element. */
        latxs_append_ir2_opnd2i(pick, &scalar, mask, i);
        latxs_append_ir2_opnd3(LISA_BGE, &scalar, &la_zero, &skip);
        if (store) {
            latxs_append_ir2_opnd2i(pick, &scalar, data, i);
        }
        gen_ldst_softmmu_helper(mem_op, &scalar, addr, offset + i * width, 1);
        if (!store) {
            latxs_append_ir2_opnd2i(insert, data, &scalar, i);
        }
        latxs_append_ir2_opnd1(LISA_LABEL, &skip);
    }
    latxs_ra_free_temp(&scalar);
}

static void avx_masked_move(IR1_INST *insn, bool qword)
{
    IR1_OPND *dst = ir1_get_opnd(insn, 0);
    IR1_OPND *src = ir1_get_opnd(insn, 2);
    bool store = ir1_opnd_is_mem(dst);
    bool ymm = ir1_opnd_size(store ? src : dst) == 256;
    unsigned width = qword ? 8 : 4;
    AvxOperand mask = avx_load(insn, ir1_get_opnd(insn, 1), ymm, false);
    AvxOperand data;
    IR2_OPND addr = avx_addr(insn, store ? dst : src, 0);

    if (store) {
        data = avx_load(insn, src, ymm, false);
    } else {
        /* A fault in a later element leaves the destination untouched. */
        data.low = latxs_ra_alloc_ftemp();
        if (ymm) {
            data.high = latxs_ra_alloc_ftemp();
        }
    }
    avx_masked_lane(&data.low, &mask.low, &addr, store, width, 0);
    if (ymm) {
        avx_masked_lane(&data.high, &mask.high, &addr, store, width, 16);
    }
    latxs_ra_free_temp(&addr);

    if (!store) {
        int reg = ir1_opnd_base_reg_num(dst);
        IR2_OPND low = latxs_ra_alloc_xmm(reg);

        avx_copy(&low, &data.low);
        avx_write_high(reg, ymm ? &data.high : NULL);
    }
}

typedef struct AvxElementOps {
    unsigned count;
    IR2_OPCODE extract;
    IR2_OPCODE insert;
    IR2_OPCODE load;
    IR2_OPCODE store;
} AvxElementOps;

static const AvxElementOps avx_elements[] = {
    { 16, LISA_VPICKVE2GR_BU, LISA_VINSGR2VR_B, LISA_LD_BU, LISA_ST_B },
    {  8, LISA_VPICKVE2GR_HU, LISA_VINSGR2VR_H, LISA_LD_HU, LISA_ST_H },
    {  4, LISA_VPICKVE2GR_WU, LISA_VINSGR2VR_W, LISA_LD_WU, LISA_ST_W },
    {  2, LISA_VPICKVE2GR_D,  LISA_VINSGR2VR_D, LISA_LD_D,  LISA_ST_D },
};

static const AvxElementOps *avx_element_ops(IR1_OPCODE opcode)
{
    switch (opcode) {
    case X86_INS_VPINSRB:
    case X86_INS_VPEXTRB:
        return &avx_elements[0];
    case X86_INS_VPINSRW:
    case X86_INS_VPEXTRW:
        return &avx_elements[1];
    case X86_INS_VPINSRQ:
    case X86_INS_VPEXTRQ:
        return &avx_elements[3];
    default:
        return &avx_elements[2];
    }
}

static void avx_insert_element(IR1_INST *insn)
{
    IR1_OPCODE opcode = ir1_opcode(insn);
    const AvxElementOps *op = avx_element_ops(opcode);
    IR1_OPND *src = ir1_get_opnd(insn, 2);
    unsigned imm = ir1_opnd_uimm(ir1_get_opnd(insn, 3));
    bool insertps = opcode == X86_INS_VINSERTPS;
    unsigned lane = insertps ? (imm >> 4) & 3 : imm & (op->count - 1);
    int reg = ir1_opnd_base_reg_num(ir1_get_opnd(insn, 0));
    IR2_OPND low = latxs_ra_alloc_xmm(reg);
    IR2_OPND base = latxs_ra_alloc_xmm(
        ir1_opnd_base_reg_num(ir1_get_opnd(insn, 1)));
    IR2_OPND scalar;

    if (ir1_opnd_is_mem(src)) {
        scalar = avx_load_scalar(insn, src, op->load);
    } else if (insertps) {
        IR2_OPND xmm = latxs_ra_alloc_xmm(ir1_opnd_base_reg_num(src));

        scalar = latxs_ra_alloc_itemp();
        latxs_append_ir2_opnd2i(op->extract, &scalar, &xmm, imm >> 6);
    } else {
        scalar = latxs_ra_alloc_gpr(ir1_opnd_base_reg_num(src));
    }

    avx_copy(&low, &base);
    latxs_append_ir2_opnd2i(op->insert, &low, &scalar, lane);
    if (insertps) {
        for (unsigned i = 0; i < 4; i++) {
            if (imm & (1u << i)) {
                latxs_append_ir2_opnd2i(op->insert, &low, &la_zero, i);
            }
        }
    }
    avx_write_high(reg, NULL);
}

static void avx_extract_element(IR1_INST *insn)
{
    const AvxElementOps *op = avx_element_ops(ir1_opcode(insn));
    IR1_OPND *dst = ir1_get_opnd(insn, 0);
    IR2_OPND xmm = latxs_ra_alloc_xmm(
        ir1_opnd_base_reg_num(ir1_get_opnd(insn, 1)));
    unsigned lane = ir1_opnd_uimm(ir1_get_opnd(insn, 2)) & (op->count - 1);
    IR2_OPND scalar = latxs_ra_alloc_itemp();

    latxs_append_ir2_opnd2i(op->extract, &scalar, &xmm, lane);
    if (ir1_opnd_is_mem(dst)) {
        IR2_OPND addr = avx_addr(insn, dst, 0);

        gen_ldst_softmmu_helper(op->store, &scalar, &addr, 0, 1);
        latxs_ra_free_temp(&addr);
    } else {
        latxs_store_ir2_to_ir1(&scalar, dst);
    }
}

static void avx_widen_lane(IR2_OPND *dst, IR2_OPND *src, const AvxWiden *widen)
{
    static const IR2_OPCODE ops[][3] = {
        { LISA_VSLLWIL_HU_BU, LISA_VSLLWIL_WU_HU, LISA_VSLLWIL_DU_WU },
        { LISA_VSLLWIL_H_B, LISA_VSLLWIL_W_H, LISA_VSLLWIL_D_W },
    };

    for (unsigned i = widen->from; i < widen->to; i++) {
        latxs_append_ir2_opnd2i(ops[widen->sign][i], dst, src, 0);
        src = dst;
    }
}

static void avx_widen(IR1_INST *insn, bool ymm, const AvxWiden *widen)
{
    IR1_OPND *opnd = ir1_get_opnd(insn, 1);
    unsigned lane_bytes = 16 >> (widen->to - widen->from);
    unsigned bytes = ymm ? 2 * lane_bytes : lane_bytes;
    IR2_OPND src;

    if (ir1_opnd_is_mem(opnd) && bytes < 16) {
        IR2_OPCODE load = bytes == 8 ? LISA_LD_D :
                           bytes == 4 ? LISA_LD_WU : LISA_LD_HU;
        IR2_OPND scalar = avx_load_scalar(insn, opnd, load);

        src = latxs_ra_alloc_ftemp();
        latxs_append_ir2_opnd2i(LISA_VINSGR2VR_D, &src, &scalar, 0);
        latxs_ra_free_temp(&scalar);
    } else {
        src = avx_load(insn, opnd, false, false).low;
    }

    int reg = ir1_opnd_base_reg_num(ir1_get_opnd(insn, 0));
    IR2_OPND low = latxs_ra_alloc_xmm(reg);

    if (ymm) {
        IR2_OPND high = latxs_ra_alloc_ftemp();

        latxs_append_ir2_opnd2i(LISA_VBSRL_V, &high, &src, lane_bytes);
        avx_widen_lane(&high, &high, widen);
        avx_write_high(reg, &high);
    } else {
        avx_write_high(reg, NULL);
    }
    avx_widen_lane(&low, &src, widen);
}

static bool avx_reverse_sources(IR2_OPCODE op)
{
    switch (op) {
    case LISA_VSLT_B: case LISA_VSLT_H: case LISA_VSLT_W: case LISA_VSLT_D:
    case LISA_VILVL_B: case LISA_VILVL_H: case LISA_VILVL_W: case LISA_VILVL_D:
    case LISA_VILVH_B: case LISA_VILVH_H: case LISA_VILVH_W: case LISA_VILVH_D:
        return true;
    default:
        return false;
    }
}

static void avx_binary(IR1_INST *insn, IR1_OPND *dst, bool ymm, IR2_OPCODE op)
{
    AvxOperand left = avx_load(insn, ir1_get_opnd(insn, 1), ymm, false);
    AvxOperand right = avx_load(insn, ir1_get_opnd(insn, 2), ymm, false);
    int reg = ir1_opnd_base_reg_num(dst);
    IR2_OPND low = latxs_ra_alloc_xmm(reg);

    if (avx_reverse_sources(op)) {
        AvxOperand tmp = left;
        left = right;
        right = tmp;
    }

    latxs_append_ir2_opnd3(op, &low, &left.low, &right.low);
    if (ymm) {
        latxs_append_ir2_opnd3(op, &left.high, &left.high, &right.high);
    }
    avx_write_high(reg, ymm ? &left.high : NULL);
}

static void avx_madd_words_lane(IR2_OPND *dst, IR2_OPND *a, IR2_OPND *b)
{
    IR2_OPND sum = latxs_ra_alloc_ftemp();

    latxs_append_ir2_opnd3(LISA_VMULWEV_W_H, &sum, a, b);
    latxs_append_ir2_opnd3(LISA_VMADDWOD_W_H, &sum, a, b);
    avx_copy(dst, &sum);
    latxs_ra_free_temp(&sum);
}

static void avx_madd_words(IR1_INST *insn, bool ymm)
{
    AvxOperand a = avx_load(insn, ir1_get_opnd(insn, 1), ymm, false);
    AvxOperand b = avx_load(insn, ir1_get_opnd(insn, 2), ymm, false);
    int reg = ir1_opnd_base_reg_num(ir1_get_opnd(insn, 0));
    IR2_OPND low = latxs_ra_alloc_xmm(reg);

    avx_madd_words_lane(&low, &a.low, &b.low);
    if (ymm) {
        avx_madd_words_lane(&a.high, &a.high, &b.high);
    }
    avx_write_high(reg, ymm ? &a.high : NULL);
}

/*
 * VPMADDUBSW: unsigned bytes of src1 * signed bytes of src2, then adjacent
 * product pairs are added with signed saturation to int16.  LoongArch's
 * vmulwev/vmulwod.h.bu.b widen exactly (vj unsigned, vk signed); the pair sum
 * must be a saturating add, so vsadd.h is used instead of a plain vadd.h.
 */
static void avx_maddubsw_lane(IR2_OPND *dst, IR2_OPND *a, IR2_OPND *b)
{
    IR2_OPND even = latxs_ra_alloc_ftemp();
    IR2_OPND odd = latxs_ra_alloc_ftemp();

    latxs_append_ir2_opnd3(LISA_VMULWEV_H_BU_B, &even, a, b);
    latxs_append_ir2_opnd3(LISA_VMULWOD_H_BU_B, &odd, a, b);
    latxs_append_ir2_opnd3(LISA_VSADD_H, &even, &even, &odd);
    avx_copy(dst, &even);
    latxs_ra_free_temp(&odd);
    latxs_ra_free_temp(&even);
}

static void avx_maddubsw(IR1_INST *insn, bool ymm)
{
    AvxOperand a = avx_load(insn, ir1_get_opnd(insn, 1), ymm, false);
    AvxOperand b = avx_load(insn, ir1_get_opnd(insn, 2), ymm, false);
    int reg = ir1_opnd_base_reg_num(ir1_get_opnd(insn, 0));
    IR2_OPND low = latxs_ra_alloc_xmm(reg);

    avx_maddubsw_lane(&low, &a.low, &b.low);
    if (ymm) {
        avx_maddubsw_lane(&a.high, &a.high, &b.high);
    }
    avx_write_high(reg, ymm ? &a.high : NULL);
}

/*
 * VPSIGNB/W/D: per element, dst = src2 > 0 ? src1 : src2 < 0 ? -src1 : 0
 * (negation wraps, e.g. 0x80 stays 0x80).  LoongArch vsigncov is exactly
 * "vd = (vj > 0 ? vk : vj < 0 ? -vk : 0)", so the r/m operand is the sign
 * source (vj) and the VEX.vvvv operand the value (vk).
 */
static void avx_sign(IR1_INST *insn, bool ymm, IR2_OPCODE op)
{
    AvxOperand a = avx_load(insn, ir1_get_opnd(insn, 1), ymm, false);
    AvxOperand b = avx_load(insn, ir1_get_opnd(insn, 2), ymm, false);
    int reg = ir1_opnd_base_reg_num(ir1_get_opnd(insn, 0));
    IR2_OPND low = latxs_ra_alloc_xmm(reg);

    latxs_append_ir2_opnd3(op, &low, &b.low, &a.low);
    if (ymm) {
        latxs_append_ir2_opnd3(op, &a.high, &b.high, &a.high);
    }
    avx_write_high(reg, ymm ? &a.high : NULL);
}

/*
 * VPHMINPOSUW (VEX.128 only): unsigned 16-bit minimum of the eight elements,
 * written as dst[15:0]=min, dst[18:16]=lowest matching index, rest cleared.
 * Same reduction as the legacy SSE4.1 translation: three vsrli.d/vmin.hu
 * rounds reduce the four words of each qword, then the two qwords are merged;
 * vseq.h marks matches and vfrstpi.h returns the first (lowest) match index.
 */
static void avx_phminposuw(IR1_INST *insn)
{
    AvxOperand src = avx_load(insn, ir1_get_opnd(insn, 1), false, false);
    int reg = ir1_opnd_base_reg_num(ir1_get_opnd(insn, 0));
    IR2_OPND low = latxs_ra_alloc_xmm(reg);
    IR2_OPND value = latxs_ra_alloc_ftemp();
    IR2_OPND equal = latxs_ra_alloc_ftemp();
    IR2_OPND index = latxs_ra_alloc_ftemp();

    avx_copy(&value, &src.low);
    for (int i = 0; i < 3; i++) {
        latxs_append_ir2_opnd2i(LISA_VSRLI_D, &value, &value, 0x10);
        latxs_append_ir2_opnd3(LISA_VMIN_HU, &value, &value, &src.low);
    }
    /* Merge the two 64-bit halves: broadcast qword 1, unsigned-min wordwise. */
    latxs_append_ir2_opnd2i(LISA_VREPLVEI_D, &index, &value, 1);
    latxs_append_ir2_opnd3(LISA_VMIN_HU, &value, &value, &index);
    latxs_append_ir2_opnd2i(LISA_VREPLVEI_H, &value, &value, 0);

    latxs_append_ir2_opnd3(LISA_VSEQ_H, &equal, &src.low, &value);
    latxs_append_ir2_opnd2i(LISA_VFRSTPI_H, &index, &equal, 0);

    /* {min, index, ...} then clear everything above dst[18:0]. */
    latxs_append_ir2_opnd3(LISA_VPACKEV_H, &value, &index, &value);
    latxs_append_ir2_opnd2i(LISA_VINSGR2VR_D, &value, &latxs_zero_ir2_opnd, 1);
    latxs_append_ir2_opnd2i(LISA_VINSGR2VR_W, &value, &latxs_zero_ir2_opnd, 1);

    avx_copy(&low, &value);
    avx_write_high(reg, NULL);
    latxs_ra_free_temp(&index);
    latxs_ra_free_temp(&equal);
    latxs_ra_free_temp(&value);
}

static void avx_sad_bytes(IR1_INST *insn, bool ymm)
{
    AvxOperand a = avx_load(insn, ir1_get_opnd(insn, 1), ymm, false);
    AvxOperand b = avx_load(insn, ir1_get_opnd(insn, 2), ymm, false);
    int reg = ir1_opnd_base_reg_num(ir1_get_opnd(insn, 0));
    IR2_OPND low = latxs_ra_alloc_xmm(reg);

    latxs_gen_psadbw(&low, &a.low, &b.low);
    if (ymm) {
        latxs_gen_psadbw(&a.high, &a.high, &b.high);
    }
    avx_write_high(reg, ymm ? &a.high : NULL);
}

static const IR2_OPCODE avx_packs[X86_INS_ENDING] = {
    [X86_INS_VPACKSSWB] = LISA_VSSRANI_B_H,
    [X86_INS_VPACKUSWB] = LISA_VSSRANI_BU_H,
    [X86_INS_VPACKSSDW] = LISA_VSSRANI_H_W,
    [X86_INS_VPACKUSDW] = LISA_VSSRANI_HU_W,
};

static void avx_pack(IR1_INST *insn, bool ymm, IR2_OPCODE op)
{
    AvxOperand a = avx_load(insn, ir1_get_opnd(insn, 1), ymm, false);
    AvxOperand b = avx_load(insn, ir1_get_opnd(insn, 2), ymm, false);
    int reg = ir1_opnd_base_reg_num(ir1_get_opnd(insn, 0));
    IR2_OPND low = latxs_ra_alloc_xmm(reg);
    IR2_OPND pack = latxs_ra_alloc_ftemp();

    /* The source supplies the low elements and the old destination the high. */
    avx_copy(&pack, &b.low);
    latxs_append_ir2_opnd2i(op, &pack, &a.low, 0);
    avx_copy(&low, &pack);
    if (ymm) {
        latxs_append_ir2_opnd2i(op, &b.high, &a.high, 0);
    }
    avx_write_high(reg, ymm ? &b.high : NULL);
}

typedef struct AvxFloatCompare {
    IR2_FCMP_COND condition;
    bool reverse;
    bool invert;
} AvxFloatCompare;

static const AvxFloatCompare avx_float_predicates[32] = {
    { FCMP_COND_CEQ },  { FCMP_COND_SLT },
    { FCMP_COND_SLE },  { FCMP_COND_CUN },
    { FCMP_COND_CUNE }, { FCMP_COND_SLT, false, true },
    { FCMP_COND_SLE, false, true }, { FCMP_COND_COR },
    { FCMP_COND_CUEQ }, { FCMP_COND_SULT },
    { FCMP_COND_SULE }, { FCMP_COND_CAF },
    { FCMP_COND_CNE },  { FCMP_COND_SLE, true },
    { FCMP_COND_SLT, true }, { FCMP_COND_CAF, false, true },
    { FCMP_COND_SEQ },  { FCMP_COND_CLT },
    { FCMP_COND_CLE },  { FCMP_COND_SUN },
    { FCMP_COND_SUNE }, { FCMP_COND_CLT, false, true },
    { FCMP_COND_CLE, false, true }, { FCMP_COND_SOR },
    { FCMP_COND_SUEQ }, { FCMP_COND_CULT },
    { FCMP_COND_CULE }, { FCMP_COND_SAF },
    { FCMP_COND_SNE },  { FCMP_COND_CLE, true },
    { FCMP_COND_CLT, true }, { FCMP_COND_SAF, false, true },
};

static void avx_fp_prepare(IR2_OPND *src, IR2_OPND *mask, IR2_OPND *work,
                            IR2_OPND *daz, IR2_OPND *raised, bool qword)
{
    IR2_OPND check = latxs_ra_alloc_itemp();
    IR2_OPND normalized = latxs_ir2_opnd_new_label();
    IR2_OPND done = latxs_ir2_opnd_new_label();
    IR2_OPCODE classify = qword ? LISA_VFCLASS_D : LISA_VFCLASS_S;
    IR2_OPCODE eq = qword ? LISA_VSEQI_D : LISA_VSEQI_W;
    IR2_OPCODE shift_left = qword ? LISA_VSLLI_D : LISA_VSLLI_W;
    IR2_OPCODE shift_right = qword ? LISA_VSRLI_D : LISA_VSRLI_W;
    int sign = qword ? 63 : 31;

    latxs_append_ir2_opnd2(classify, work, src);
    latxs_append_ir2_opnd3(LISA_VAND_V, work, work, mask);
    latxs_append_ir2_opnd2(LISA_BNEZ, daz, &normalized);
    latxs_append_ir2_opnd2(LISA_VSETNEZ_V, &latxs_fcc0_ir2_opnd, work);
    latxs_append_ir2_opnd2(LISA_MOVCF2GR, &check, &latxs_fcc0_ir2_opnd);
    latxs_append_ir2_opnd2i(LISA_SLLI_D, &check, &check, 1);
    latxs_append_ir2_opnd3(LISA_OR, raised, raised, &check);
    latxs_append_ir2_opnd1(LISA_B, &done);

    /* A denormal input becomes a signed zero under MXCSR.DAZ. */
    latxs_append_ir2_opnd1(LISA_LABEL, &normalized);
    latxs_append_ir2_opnd2i(eq, work, work, 0);
    latxs_append_ir2_opnd2i(shift_right, mask, src, sign);
    latxs_append_ir2_opnd2i(shift_left, mask, mask, sign);
    latxs_append_ir2_opnd4(LISA_VBITSEL_V, src, mask, src, work);
    latxs_append_ir2_opnd1(LISA_LABEL, &done);
    latxs_ra_free_temp(&check);
}

static void avx_fp_compare_lane(IR2_OPND *dst, IR2_OPND *left, IR2_OPND *right,
                                IR2_OPND *daz, IR2_OPND *raised, bool qword,
                                const AvxFloatCompare *predicate, bool select)
{
    IR2_OPND a = latxs_ra_alloc_ftemp();
    IR2_OPND b = latxs_ra_alloc_ftemp();
    IR2_OPND mask = latxs_ra_alloc_ftemp();
    IR2_OPND fcsr = latxs_ra_alloc_itemp();
    IR2_OPCODE cmp = qword ? LISA_VFCMP_COND_D : LISA_VFCMP_COND_S;

    avx_copy(&a, left);
    avx_copy(&b, right);
    /* LoongArch class bits 8 and 4 denote positive and negative subnormals. */
    latxs_append_ir2_opnd1i(LISA_VLDI, &mask,
                            VLDI_IMM_TYPE0(qword ? 3 : 2, 0x110));
    avx_fp_prepare(&a, &mask, dst, daz, raised, qword);
    latxs_append_ir2_opnd1i(LISA_VLDI, &mask,
                            VLDI_IMM_TYPE0(qword ? 3 : 2, 0x110));
    avx_fp_prepare(&b, &mask, dst, daz, raised, qword);

    latxs_append_ir2_opnd3i(cmp, &mask, predicate->reverse ? &b : &a,
                             predicate->reverse ? &a : &b,
                             predicate->condition);
    latxs_append_ir2_opnd2(LISA_MOVFCSR2GR, &fcsr, &latxs_fcsr2_ir2_opnd);
    latxs_append_ir2_opnd2ii(LISA_BSTRPICK_W, &fcsr, &fcsr,
                              FCSR_OFF_CAUSE_V, FCSR_OFF_CAUSE_V);
    latxs_append_ir2_opnd3(LISA_OR, raised, raised, &fcsr);
    if (select) {
        /* Min/max ties and unordered cases choose the second input. */
        latxs_append_ir2_opnd4(LISA_VBITSEL_V, dst, &b, &a, &mask);
    } else if (predicate->invert) {
        latxs_append_ir2_opnd3(LISA_VNOR_V, dst, &mask, &mask);
    } else {
        avx_copy(dst, &mask);
    }
    latxs_ra_free_temp(&fcsr);
    latxs_ra_free_temp(&mask);
    latxs_ra_free_temp(&b);
    latxs_ra_free_temp(&a);
}

static void avx_fp_compare(IR1_INST *insn, bool ymm, bool qword,
                           const AvxFloatCompare *predicate, bool select)
{
    AvxOperand a = avx_load(insn, ir1_get_opnd(insn, 1), ymm, false);
    AvxOperand b = avx_load(insn, ir1_get_opnd(insn, 2), ymm, false);
    IR2_OPND result = latxs_ra_alloc_ftemp();
    IR2_OPND mxcsr = latxs_ra_alloc_itemp();
    IR2_OPND daz = latxs_ra_alloc_itemp();
    IR2_OPND raised = latxs_ra_alloc_itemp();
    IR2_OPND enables = latxs_ra_alloc_itemp();
    IR2_OPND commit = latxs_ir2_opnd_new_label();
    unsigned offset = offsetof(CPUX86State, mxcsr) / 4;

    latxs_append_ir2_opnd2i(LISA_LDPTR_W, &mxcsr, &latxs_env_ir2_opnd,
                            offset);
    latxs_append_ir2_opnd2i(LISA_ANDI, &daz, &mxcsr, 1 << 6);
    latxs_append_ir2_opnd2i(LISA_ORI, &raised, &la_zero, 0);
    latxs_append_ir2_opnd2(LISA_MOVFCSR2GR, &enables, &latxs_fcsr1_ir2_opnd);
    latxs_append_ir2_opnd2(LISA_MOVGR2FCSR, &latxs_fcsr1_ir2_opnd, &la_zero);

    avx_fp_compare_lane(&result, &a.low, &b.low, &daz, &raised, qword,
                         predicate, select);
    if (ymm) {
        avx_fp_compare_lane(&a.high, &a.high, &b.high, &daz, &raised, qword,
                             predicate, select);
    }

    latxs_append_ir2_opnd2(LISA_MOVGR2FCSR, &latxs_fcsr1_ir2_opnd, &enables);
    latxs_append_ir2_opnd3(LISA_OR, &mxcsr, &mxcsr, &raised);
    latxs_append_ir2_opnd2i(LISA_STPTR_W, &mxcsr, &latxs_env_ir2_opnd,
                            offset);
    latxs_append_ir2_opnd2i(LISA_SRLI_W, &mxcsr, &mxcsr, 7);
    latxs_append_ir2_opnd3(LISA_ANDN, &raised, &raised, &mxcsr);
    latxs_append_ir2_opnd2(LISA_BEQZ, &raised, &commit);
    latxs_ra_free_temp(&enables);
    latxs_ra_free_temp(&raised);
    latxs_ra_free_temp(&daz);
    latxs_ra_free_temp(&mxcsr);
    latxs_tr_gen_excp_simd(insn);

    latxs_append_ir2_opnd1(LISA_LABEL, &commit);
    int reg = ir1_opnd_base_reg_num(ir1_get_opnd(insn, 0));
    IR2_OPND low = latxs_ra_alloc_xmm(reg);
    avx_copy(&low, &result);
    avx_write_high(reg, ymm ? &a.high : NULL);
}

typedef struct AvxFpArithmetic {
    IR2_OPCODE host_op;
    LatxAvxFpOp op;
    bool qword;
    unsigned fma_order;
} AvxFpArithmetic;

#define FP_ARITH(x86, host, kind)                                            \
    [X86_INS_V##x86##PS] = { LISA_VF##host##_S, LATX_AVX_FP_##kind },          \
    [X86_INS_V##x86##PD] = { LISA_VF##host##_D, LATX_AVX_FP_##kind, true }

#define FP_FMA_OP(tok, order, index, kind)                                  \
    [X86_INS_V##tok##order##PS] = { LISA_V##tok##_S,                         \
                                    LATX_AVX_FP_##kind, false, index },     \
    [X86_INS_V##tok##order##PD] = { LISA_V##tok##_D,                         \
                                    LATX_AVX_FP_##kind, true, index }

static const AvxFpArithmetic avx_fp_arithmetic_ops[X86_INS_ENDING] = {
    FP_ARITH(ADD, ADD, ADD),
    FP_ARITH(SUB, SUB, SUB),
    FP_ARITH(MUL, MUL, MUL),
    FP_ARITH(DIV, DIV, DIV),
    FP_FMA_OP(FMADD, 132, 0, FMA),
    FP_FMA_OP(FMADD, 213, 1, FMA),
    FP_FMA_OP(FMADD, 231, 2, FMA),
    FP_FMA_OP(FMSUB, 132, 0, FMSUB),
    FP_FMA_OP(FMSUB, 213, 1, FMSUB),
    FP_FMA_OP(FMSUB, 231, 2, FMSUB),
    FP_FMA_OP(FNMADD, 132, 0, FNMADD),
    FP_FMA_OP(FNMADD, 213, 1, FNMADD),
    FP_FMA_OP(FNMADD, 231, 2, FNMADD),
    FP_FMA_OP(FNMSUB, 132, 0, FNMSUB),
    FP_FMA_OP(FNMSUB, 213, 1, FNMSUB),
    FP_FMA_OP(FNMSUB, 231, 2, FNMSUB),
};

#undef FP_ARITH
#undef FP_FMA_OP

/* The fused multiply-add/subtract family shares one operand ordering. */
static inline bool avx_op_is_fma(LatxAvxFpOp op)
{
    return op >= LATX_AVX_FP_FMA && op <= LATX_AVX_FP_FNMSUB;
}

static void avx_fp_check_class(IR2_OPND *value, IR2_OPND *mask,
                               IR2_OPND *work, IR2_OPND *slow, bool qword)
{
    latxs_append_ir2_opnd2(qword ? LISA_VFCLASS_D : LISA_VFCLASS_S,
                           work, value);
    latxs_append_ir2_opnd3(LISA_VAND_V, work, work, mask);
    latxs_append_ir2_opnd2(LISA_VSETNEZ_V, &latxs_fcc0_ir2_opnd, work);
    latxs_append_ir2_opnd2(LISA_BCNEZ, &latxs_fcc0_ir2_opnd, slow);
}

static void avx_fp_emit(IR2_OPND *out, IR2_OPND *a, IR2_OPND *b, IR2_OPND *c,
                        const AvxFpArithmetic *arith)
{
    if (c) {
        latxs_append_ir2_opnd4(arith->host_op, out, a, b, c);
    } else {
        latxs_append_ir2_opnd3(arith->host_op, out, a, b);
    }
}

static IR2_OPND avx_fp_scratch_base(void)
{
    IR2_OPND base = latxs_ra_alloc_itemp();

    latxs_load_imm64(&base, offsetof(CPUX86State, avx_fp_operands));
    latxs_append_ir2_opnd3(LISA_ADD_D, &base, &latxs_env_ir2_opnd, &base);
    return base;
}

/*
 * In softfpu mode stmxcsr rebuilds MXCSR bits 5:0 from FCSR2, so flags
 * computed in software must be mirrored into FCSR2 like a native vector
 * operation would leave them.  The helper already ORed the new flags into
 * env->mxcsr; translate that image back to the FCSR2 layout (19:16 flags
 * [z,o,u,p], 20 invalid flag, 27:24/28 cause).  Raise #XM when an unmasked
 * exception was recorded.
 */
static void avx_fp_commit_flags(IR1_INST *insn)
{
    /*
     * In softfpu mode stmxcsr rebuilds MXCSR bits 5:0 from FCSR2, so the
     * flags computed in software must be mirrored into FCSR2 like a native
     * vector operation would leave them.  The helper already ORed the new
     * flags into env->mxcsr; translate that image back to the FCSR2 layout
     * (19:16 flags [z,o,u,p], 20 invalid flag, 27:24/28 cause).
     */
    IR2_OPND flag_mxcsr = latxs_ra_alloc_itemp();
    IR2_OPND flag_bits = latxs_ra_alloc_itemp();
    IR2_OPND flag_extra = latxs_ra_alloc_itemp();
    IR2_OPND flag_ie = latxs_ra_alloc_itemp();

    latxs_append_ir2_opnd2i(LISA_LD_WU, &flag_mxcsr, &latxs_env_ir2_opnd,
                            offsetof(CPUX86State, mxcsr));
    latxs_append_ir2_opnd2i(LISA_SLLI_W, &flag_bits, &flag_mxcsr, 2);
    latxs_append_ir2_opnd2(LISA_BITREV_4B, &flag_bits, &flag_bits);
    latxs_append_ir2_opnd2i(LISA_ANDI, &flag_bits, &flag_bits, 0xf);
    latxs_append_ir2_opnd2i(LISA_SLLI_W, &flag_extra, &flag_bits, 24);
    latxs_append_ir2_opnd2i(LISA_SLLI_W, &flag_bits, &flag_bits, 16);
    latxs_append_ir2_opnd3(LISA_OR, &flag_bits, &flag_bits, &flag_extra);
    latxs_append_ir2_opnd2ii(LISA_BSTRPICK_W, &flag_ie, &flag_mxcsr, 0, 0);
    latxs_append_ir2_opnd2i(LISA_SLLI_W, &flag_extra, &flag_ie, 28);
    latxs_append_ir2_opnd3(LISA_OR, &flag_bits, &flag_bits, &flag_extra);
    latxs_append_ir2_opnd2i(LISA_SLLI_W, &flag_extra, &flag_ie, 20);
    latxs_append_ir2_opnd3(LISA_OR, &flag_bits, &flag_bits, &flag_extra);
    latxs_append_ir2_opnd2(LISA_MOVFCSR2GR, &flag_extra,
                           &latxs_fcsr2_ir2_opnd);
    latxs_append_ir2_opnd3(LISA_OR, &flag_bits, &flag_bits, &flag_extra);
    latxs_append_ir2_opnd2(LISA_MOVGR2FCSR, &latxs_fcsr2_ir2_opnd,
                           &flag_bits);
    latxs_ra_free_temp(&flag_ie);
    latxs_ra_free_temp(&flag_extra);
    latxs_ra_free_temp(&flag_bits);
    latxs_ra_free_temp(&flag_mxcsr);

    IR2_OPND raised = latxs_ra_alloc_itemp();
    IR2_OPND success = latxs_ir2_opnd_new_label();

    latxs_append_ir2_opnd2i(LISA_LDPTR_W, &raised, &latxs_env_ir2_opnd,
                            offsetof(CPUX86State, avx_fp_raised) / 4);
    latxs_append_ir2_opnd2(LISA_BEQZ, &raised, &success);
    latxs_ra_free_temp(&raised);
    latxs_tr_gen_excp_simd(insn);
    latxs_append_ir2_opnd1(LISA_LABEL, &success);
}

static void avx_fp_arithmetic(IR1_INST *insn, const AvxFpArithmetic *arith)
{
    IR1_OPND *dst = ir1_get_opnd(insn, 0);
    bool ymm = ir1_opnd_size(dst) == 256;
    bool fused = avx_op_is_fma(arith->op);
    unsigned count = fused ? 3 : 2;
    AvxOperand inputs[3];
    AvxOperand result = { .low = latxs_ra_alloc_ftemp() };
    static const unsigned fma_operands[][3] = {
        { 0, 2, 1 }, { 1, 0, 2 }, { 1, 2, 0 },
    };
    static const unsigned binary_operands[] = { 1, 2 };
    static const struct { unsigned mxcsr, fcsr; } mxcsr_flag_bits[] = {
        { 5, 24 }, { 4, 25 }, { 3, 26 }, { 2, 27 }, { 0, 28 },
    };
    const unsigned *order = fused ? fma_operands[arith->fma_order] :
                                    binary_operands;
    IR2_OPND slow = latxs_ir2_opnd_new_label();
    IR2_OPND commit = latxs_ir2_opnd_new_label();

    for (unsigned i = 0; i < count; i++) {
        inputs[i] = avx_load(insn, ir1_get_opnd(insn, order[i]), ymm, false);
    }
    if (ymm) {
        result.high = latxs_ra_alloc_ftemp();
    }

    /* The native path handles finite, normal or zero operands. Software
     * handles x86 NaN selection, denormals, FTZ and exceptional results.
     * f8 is the reserved scalar scratch register; using it for the class
     * mask keeps three-operand YMM memory forms within the ftemp pool. */
    IR2_OPND *mask = &latxs_f32_ir2_opnd;
    IR2_OPND work = latxs_ra_alloc_ftemp();
    IR2_OPND state = latxs_ra_alloc_itemp();
    IR2_OPND flags = latxs_ra_alloc_itemp();

    latxs_append_ir2_opnd2i(LISA_LDPTR_W, &state, &latxs_env_ir2_opnd,
                            offsetof(CPUX86State, mxcsr) / 4);
    latxs_append_ir2_opnd2ii(LISA_BSTRPICK_W, &state, &state, 14, 13);
    latxs_append_ir2_opnd2(LISA_BNEZ, &state, &slow);
    latxs_append_ir2_opnd1i(LISA_VLDI, mask,
                            VLDI_IMM_TYPE0(arith->qword ? 3 : 2, 0x157));
    for (unsigned i = 0; i < count; i++) {
        avx_fp_check_class(&inputs[i].low, mask, &work, &slow, arith->qword);
        if (ymm) {
            avx_fp_check_class(&inputs[i].high, mask, &work, &slow,
                               arith->qword);
        }
    }

    latxs_append_ir2_opnd2(LISA_MOVFCSR2GR, &state, &latxs_fcsr1_ir2_opnd);
    latxs_append_ir2_opnd2(LISA_MOVGR2FCSR, &latxs_fcsr1_ir2_opnd, &la_zero);
    avx_fp_emit(&result.low, &inputs[0].low, &inputs[1].low,
                fused ? &inputs[2].low : NULL, arith);
    if (ymm) {
        avx_fp_emit(&result.high, &inputs[0].high, &inputs[1].high,
                    fused ? &inputs[2].high : NULL, arith);
    }
    latxs_append_ir2_opnd2(LISA_MOVFCSR2GR, &flags, &latxs_fcsr2_ir2_opnd);
    latxs_append_ir2_opnd2(LISA_MOVGR2FCSR, &latxs_fcsr1_ir2_opnd, &state);

    /* FNMADD/FNMSUB: keep the old conservative gate (any flag -> softfloat
     * helper).  The native vfnmadd/vfnmsub mapping is not bit-exact with the
     * softfloat negate-product forms once the result is inexact. */
    if (arith->op == LATX_AVX_FP_FNMADD || arith->op == LATX_AVX_FP_FNMSUB) {
        latxs_append_ir2_opnd2(LISA_BNEZ, &flags, &slow);
        latxs_append_ir2_opnd1i(LISA_VLDI, mask,
                                VLDI_IMM_TYPE0(arith->qword ? 3 : 2, 0x110));
        avx_fp_check_class(&result.low, mask, &work, &slow, arith->qword);
        if (ymm) {
            avx_fp_check_class(&result.high, mask, &work, &slow,
                               arith->qword);
        }
        latxs_ra_free_temp(&state);
        latxs_ra_free_temp(&flags);
        latxs_ra_free_temp(&work);
        latxs_append_ir2_opnd1(LISA_B, &commit);
    } else {

    /* Fast flag handling: the native path now keeps the common inexact case.
     * It applies only while every SSE exception is masked (no #XM can be
     * pending) and RC is nearest (checked above).  The hardware's sticky
     * exception flags are ORed into the guest MXCSR flag bits; NaN results
     * are excluded below because x86 NaN selection must stay softfloat-exact. */
    IR2_OPND mxcsr = latxs_ra_alloc_itemp();
    IR2_OPND scratch = latxs_ra_alloc_itemp();

    latxs_append_ir2_opnd2i(LISA_LDPTR_W, &mxcsr, &latxs_env_ir2_opnd,
                            offsetof(CPUX86State, mxcsr) / 4);
    /* All exceptions masked? (MXCSR[12:7] == 0x3f, i.e. the 0x1f80 default) */
    latxs_append_ir2_opnd2ii(LISA_BSTRPICK_W, &scratch, &mxcsr, 12, 7);
    latxs_append_ir2_opnd2i(LISA_XORI, &scratch, &scratch, 0x3f);
    latxs_append_ir2_opnd2(LISA_BNEZ, &scratch, &slow);
    /* OR this operation's sticky FCSR2 flags into MXCSR bits 5,4,3,2,0. */
    for (unsigned i = 0; i < ARRAY_SIZE(mxcsr_flag_bits); i++) {
        latxs_append_ir2_opnd2ii(LISA_BSTRPICK_W, &scratch, &flags,
                                 mxcsr_flag_bits[i].fcsr,
                                 mxcsr_flag_bits[i].fcsr);
        latxs_append_ir2_opnd2i(LISA_SLLI_W, &scratch, &scratch,
                                mxcsr_flag_bits[i].mxcsr);
        latxs_append_ir2_opnd3(LISA_OR, &mxcsr, &mxcsr, &scratch);
    }
    latxs_append_ir2_opnd2i(LISA_ST_W, &mxcsr, &latxs_env_ir2_opnd,
                            offsetof(CPUX86State, mxcsr));
    latxs_ra_free_temp(&mxcsr);
    latxs_ra_free_temp(&scratch);

    /* NaN results must keep x86 (softfloat) NaN selection/quieting: fall
     * back to the helper path. Denormal results are covered by FTZ/DE. */
    latxs_append_ir2_opnd1i(LISA_VLDI, mask,
                            VLDI_IMM_TYPE0(arith->qword ? 3 : 2, 0x113));
    avx_fp_check_class(&result.low, mask, &work, &slow, arith->qword);
    if (ymm) {
        avx_fp_check_class(&result.high, mask, &work, &slow, arith->qword);
    }
    latxs_ra_free_temp(&state);
    latxs_ra_free_temp(&flags);
    latxs_ra_free_temp(&work);
    latxs_append_ir2_opnd1(LISA_B, &commit);
    }

    latxs_append_ir2_opnd1(LISA_LABEL, &slow);
    IR2_OPND base = avx_fp_scratch_base();

    for (unsigned i = 0; i < count; i++) {
        latxs_append_ir2_opnd2i(LISA_VST, &inputs[i].low, &base, i * 32);
        if (ymm) {
            latxs_append_ir2_opnd2i(LISA_VST, &inputs[i].high, &base,
                                    i * 32 + 16);
        }
    }
    latxs_ra_free_temp(&base);
    latxs_tr_gen_call_to_helper4_u64_cfg((ADDR)latxs_avx_fp, arith->op,
                                         (ymm ? 32 : 16) /
                                         (arith->qword ? 8 : 4),
                                         arith->qword, default_helper_cfg,
                                         HELPER_AVX_FP);

    avx_fp_commit_flags(insn);


    base = avx_fp_scratch_base();
    latxs_append_ir2_opnd2i(LISA_VLD, &result.low, &base, 0);
    if (ymm) {
        latxs_append_ir2_opnd2i(LISA_VLD, &result.high, &base, 16);
    }
    latxs_ra_free_temp(&base);

    latxs_append_ir2_opnd1(LISA_LABEL, &commit);
    int reg = ir1_opnd_base_reg_num(dst);
    IR2_OPND low = latxs_ra_alloc_xmm(reg);

    avx_copy(&low, &result.low);
    avx_write_high(reg, ymm ? &result.high : NULL);
}

/*
 * VHADDPS/PD, VHSUBPS/PD: horizontal add/sub inside each 128-bit lane:
 *   dst = [ v0+v1, v2+v3, s0+s1, s2+s3 ]  (v = VEX.vvvv source, s = r/m)
 * vpickev/vpickod turn this into one packed add/sub (vpickev = low elements
 * of each pair, vpickod = high elements, so the softfloat operand order keeps
 * x86 NaN selection).  The operation itself is evaluated by latxs_avx_fp()
 * so denormals, FTZ/DAZ and MXCSR flags match generic TCG bit for bit.
 */
static void avx_horizontal_fp(IR1_INST *insn, bool qword, bool sub)
{
    IR1_OPND *dst = ir1_get_opnd(insn, 0);
    bool ymm = ir1_opnd_size(dst) == 256;
    AvxOperand v = avx_load(insn, ir1_get_opnd(insn, 1), ymm, false);
    AvxOperand s = avx_load(insn, ir1_get_opnd(insn, 2), ymm, false);
    IR2_OPCODE pick_even = qword ? LISA_VPICKEV_D : LISA_VPICKEV_W;
    IR2_OPCODE pick_odd = qword ? LISA_VPICKOD_D : LISA_VPICKOD_W;
    IR2_OPND even = latxs_ra_alloc_ftemp();
    IR2_OPND odd = latxs_ra_alloc_ftemp();
    IR2_OPND base = avx_fp_scratch_base();
    unsigned count = ymm ? (qword ? 4 : 8) : (qword ? 2 : 4);

    latxs_append_ir2_opnd3(pick_even, &even, &s.low, &v.low);
    latxs_append_ir2_opnd3(pick_odd, &odd, &s.low, &v.low);
    latxs_append_ir2_opnd2i(LISA_VST, &even, &base, 0);
    latxs_append_ir2_opnd2i(LISA_VST, &odd, &base, 32);
    if (ymm) {
        latxs_append_ir2_opnd3(pick_even, &even, &s.high, &v.high);
        latxs_append_ir2_opnd3(pick_odd, &odd, &s.high, &v.high);
        latxs_append_ir2_opnd2i(LISA_VST, &even, &base, 16);
        latxs_append_ir2_opnd2i(LISA_VST, &odd, &base, 48);
    }
    latxs_ra_free_temp(&base);

    latxs_tr_gen_call_to_helper4_u64_cfg((ADDR)latxs_avx_fp,
                                         sub ? LATX_AVX_FP_SUB : LATX_AVX_FP_ADD,
                                         count, qword, default_helper_cfg,
                                         HELPER_AVX_FP);
    avx_fp_commit_flags(insn);

    base = avx_fp_scratch_base();
    latxs_append_ir2_opnd2i(LISA_VLD, &even, &base, 0);
    if (ymm) {
        latxs_append_ir2_opnd2i(LISA_VLD, &odd, &base, 16);
    }
    latxs_ra_free_temp(&base);

    int reg = ir1_opnd_base_reg_num(dst);
    IR2_OPND low = latxs_ra_alloc_xmm(reg);

    avx_copy(&low, &even);
    avx_write_high(reg, ymm ? &odd : NULL);
    latxs_ra_free_temp(&odd);
    latxs_ra_free_temp(&even);
}

static void avx_permute_halves(IR1_INST *insn)
{
    AvxOperand a = avx_load(insn, ir1_get_opnd(insn, 1), true, false);
    AvxOperand b = avx_load(insn, ir1_get_opnd(insn, 2), true, false);
    IR2_OPND *lanes[] = { &a.low, &a.high, &b.low, &b.high };
    unsigned imm = ir1_opnd_uimm(ir1_get_opnd(insn, 3));
    int reg = ir1_opnd_base_reg_num(ir1_get_opnd(insn, 0));
    IR2_OPND low = latxs_ra_alloc_xmm(reg);

    avx_write_high(reg, imm & 0x80 ? NULL : lanes[(imm >> 4) & 3]);
    if (imm & 8) {
        avx_clear(&low);
    } else {
        avx_copy(&low, lanes[imm & 3]);
    }
}

static IR2_OPND avx_concat(AvxOperand *operand)
{
    IR2_OPND wide = latxs_ra_alloc_ftemp();

    avx_copy(&wide, &operand->low);
    latxs_append_ir2_opnd2i(LISA_XVPERMI_Q, &wide, &operand->high, 0x02);
    return wide;
}

static void avx_permute_pair(IR2_OPND *dst, AvxOperand *src, unsigned imm)
{
    IR2_OPND *lanes[] = { &src->low, &src->high };
    unsigned first = imm & 3;
    unsigned second = (imm >> 2) & 3;

    latxs_append_ir2_opnd2i(LISA_VREPLVEI_D, dst, lanes[first >> 1],
                            first & 1);
    latxs_append_ir2_opnd2i(LISA_VEXTRINS_D, dst, lanes[second >> 1],
                            0x10 | (second & 1));
}

static void avx_permute_elements(IR1_INST *insn)
{
    IR1_OPCODE opcode = ir1_opcode(insn);
    bool dwords = opcode == X86_INS_VPERMD || opcode == X86_INS_VPERMPS;
    AvxOperand src = avx_load(insn, ir1_get_opnd(insn, dwords ? 2 : 1),
                              true, false);
    int reg = ir1_opnd_base_reg_num(ir1_get_opnd(insn, 0));
    IR2_OPND low = latxs_ra_alloc_xmm(reg);
    IR2_OPND high = latxs_ra_alloc_ftemp();

    if (!option_enable_lasx) {
        IR2_OPND result = latxs_ra_alloc_ftemp();

        if (dwords) {
            AvxOperand selectors = avx_load(insn, ir1_get_opnd(insn, 1),
                                            true, false);

            /* LSX vshuf.w uses its destination as the index vector. */
            avx_copy(&result, &selectors.low);
            latxs_append_ir2_opnd3(LISA_VSHUF_W, &result, &src.high, &src.low);
            avx_copy(&high, &selectors.high);
            latxs_append_ir2_opnd3(LISA_VSHUF_W, &high, &src.high, &src.low);
        } else {
            unsigned imm = ir1_opnd_uimm(ir1_get_opnd(insn, 2));

            avx_permute_pair(&result, &src, imm);
            avx_permute_pair(&high, &src, imm >> 4);
        }
        avx_copy(&low, &result);
        avx_write_high(reg, &high);
        return;
    }

    IR2_OPND wide = avx_concat(&src);
    if (dwords) {
        AvxOperand selectors = avx_load(insn, ir1_get_opnd(insn, 1), true,
                                        false);
        IR2_OPND indices = avx_concat(&selectors);
        latxs_append_ir2_opnd3(LISA_XVPERM_W, &wide, &wide, &indices);
    } else {
        unsigned imm = ir1_opnd_uimm(ir1_get_opnd(insn, 2));
        latxs_append_ir2_opnd2i(LISA_XVPERMI_D, &wide, &wide, imm);
    }

    avx_copy(&low, &wide);
    latxs_append_ir2_opnd2i(LISA_XVPERMI_D, &high, &wide, 0x0e);
    avx_write_high(reg, &high);
}

static void avx_permil_lane(IR2_OPND *dst, IR2_OPND *src, IR2_OPND *index,
                             unsigned imm, bool qword)
{
    if (index) {
        IR2_OPCODE shuffle = qword ? LISA_VSHUF_D : LISA_VSHUF_W;

        avx_copy(dst, index);
        if (qword) {
            latxs_append_ir2_opnd2i(LISA_VSRLI_D, dst, dst, 1);
        }
        latxs_append_ir2_opnd3(shuffle, dst, src, src);
    } else if (qword) {
        latxs_append_ir2_opnd2i(LISA_VREPLVEI_D, dst, src, imm & 1);
        latxs_append_ir2_opnd2i(LISA_VEXTRINS_D, dst, src,
                                0x10 | ((imm >> 1) & 1));
    } else {
        latxs_append_ir2_opnd2i(LISA_VSHUF4I_W, dst, src, imm);
    }
}

static void avx_permil(IR1_INST *insn, bool ymm, bool qword)
{
    AvxOperand src = avx_load(insn, ir1_get_opnd(insn, 1), ymm, false);
    IR1_OPND *control = ir1_get_opnd(insn, 2);
    bool immediate = ir1_opnd_is_imm(control);
    unsigned imm = immediate ? ir1_opnd_uimm(control) : 0;
    AvxOperand index;
    IR2_OPND result = latxs_ra_alloc_ftemp();
    int reg = ir1_opnd_base_reg_num(ir1_get_opnd(insn, 0));
    IR2_OPND low = latxs_ra_alloc_xmm(reg);

    if (!immediate) {
        index = avx_load(insn, control, ymm, false);
    }
    avx_permil_lane(&result, &src.low, immediate ? NULL : &index.low,
                     imm, qword);
    avx_copy(&low, &result);
    if (ymm) {
        avx_permil_lane(&result, &src.high, immediate ? NULL : &index.high,
                         qword ? imm >> 2 : imm, qword);
    }
    avx_write_high(reg, ymm ? &result : NULL);
}

static void avx_insert_half(IR1_INST *insn)
{
    AvxOperand a = avx_load(insn, ir1_get_opnd(insn, 1), true, false);
    AvxOperand b = avx_load(insn, ir1_get_opnd(insn, 2), false, false);
    unsigned imm = ir1_opnd_uimm(ir1_get_opnd(insn, 3));
    int reg = ir1_opnd_base_reg_num(ir1_get_opnd(insn, 0));
    IR2_OPND low = latxs_ra_alloc_xmm(reg);

    avx_write_high(reg, imm & 1 ? &b.low : &a.high);
    avx_copy(&low, imm & 1 ? &a.low : &b.low);
}

static void avx_extract_half(IR1_INST *insn)
{
    IR1_OPND *dst = ir1_get_opnd(insn, 0);
    AvxOperand src = avx_load(insn, ir1_get_opnd(insn, 1), true, false);
    unsigned imm = ir1_opnd_uimm(ir1_get_opnd(insn, 2));
    IR2_OPND *lane = imm & 1 ? &src.high : &src.low;

    if (ir1_opnd_is_mem(dst)) {
        IR2_OPND addr = avx_addr(insn, dst, 0);
        avx_mem_lane(lane, &addr, 0, false);
        latxs_ra_free_temp(&addr);
    } else {
        int reg = ir1_opnd_base_reg_num(dst);
        IR2_OPND low = latxs_ra_alloc_xmm(reg);
        avx_copy(&low, lane);
        avx_write_high(reg, NULL);
    }
}

static void avx_shuffle_dwords(IR1_INST *insn, bool ymm)
{
    AvxOperand src = avx_load(insn, ir1_get_opnd(insn, 1), ymm, false);
    unsigned imm = ir1_opnd_uimm(ir1_get_opnd(insn, 2));
    int reg = ir1_opnd_base_reg_num(ir1_get_opnd(insn, 0));
    IR2_OPND low = latxs_ra_alloc_xmm(reg);

    latxs_append_ir2_opnd2i(LISA_VSHUF4I_W, &low, &src.low, imm);
    if (ymm) {
        latxs_append_ir2_opnd2i(LISA_VSHUF4I_W, &src.high, &src.high, imm);
    }
    avx_write_high(reg, ymm ? &src.high : NULL);
}

static void avx_shuffle_packed_lane(IR2_OPND *dst, IR2_OPND *a, IR2_OPND *b,
                                    unsigned imm, bool qword)
{
    IR2_OPND left = latxs_ra_alloc_ftemp();
    IR2_OPND right = latxs_ra_alloc_ftemp();

    if (qword) {
        latxs_append_ir2_opnd2i(LISA_VREPLVEI_D, &left, a, imm & 1);
        latxs_append_ir2_opnd2i(LISA_VREPLVEI_D, &right, b, (imm >> 1) & 1);
    } else {
        latxs_append_ir2_opnd2i(LISA_VSHUF4I_W, &left, a, imm & 15);
        latxs_append_ir2_opnd2i(LISA_VSHUF4I_W, &right, b, imm >> 4);
    }
    latxs_append_ir2_opnd3(LISA_VILVL_D, dst, &right, &left);
    latxs_ra_free_temp(&right);
    latxs_ra_free_temp(&left);
}

static void avx_shuffle_packed(IR1_INST *insn, bool ymm, bool qword)
{
    AvxOperand a = avx_load(insn, ir1_get_opnd(insn, 1), ymm, false);
    AvxOperand b = avx_load(insn, ir1_get_opnd(insn, 2), ymm, false);
    unsigned imm = ir1_opnd_uimm(ir1_get_opnd(insn, 3));
    int reg = ir1_opnd_base_reg_num(ir1_get_opnd(insn, 0));
    IR2_OPND low = latxs_ra_alloc_xmm(reg);

    avx_shuffle_packed_lane(&low, &a.low, &b.low, imm, qword);
    if (ymm) {
        avx_shuffle_packed_lane(&a.high, &a.high, &b.high,
                                qword ? imm >> 2 : imm, qword);
    }
    avx_write_high(reg, ymm ? &a.high : NULL);
}

static void avx_shuffle_byte_lane(IR2_OPND *dest, IR2_OPND *src,
                                  IR2_OPND *mask, IR2_OPND *zero)
{
    IR2_OPND indices = latxs_ra_alloc_ftemp();
    IR2_OPND select_zero = latxs_ra_alloc_ftemp();

    latxs_append_ir2_opnd2i(LISA_VANDI_B, &indices, mask, 0xf);
    latxs_append_ir2_opnd2i(LISA_VANDI_B, &select_zero, mask, 0x80);
    latxs_append_ir2_opnd2i(LISA_VSRLI_B, &select_zero, &select_zero, 3);
    latxs_append_ir2_opnd3(LISA_VOR_V, &indices, &indices, &select_zero);
    latxs_append_ir2_opnd4(LISA_VSHUF_B, dest, zero, src, &indices);
    latxs_ra_free_temp(&select_zero);
    latxs_ra_free_temp(&indices);
}

static void avx_shuffle_bytes(IR1_INST *insn, bool ymm)
{
    AvxOperand a = avx_load(insn, ir1_get_opnd(insn, 1), ymm, false);
    AvxOperand b = avx_load(insn, ir1_get_opnd(insn, 2), ymm, false);
    IR2_OPND zero = latxs_ra_alloc_ftemp();
    int reg = ir1_opnd_base_reg_num(ir1_get_opnd(insn, 0));
    IR2_OPND low = latxs_ra_alloc_xmm(reg);

    avx_clear(&zero);
    avx_shuffle_byte_lane(&low, &a.low, &b.low, &zero);
    if (ymm) {
        avx_shuffle_byte_lane(&a.high, &a.high, &b.high, &zero);
    }
    avx_write_high(reg, ymm ? &a.high : NULL);
}

static void avx_align_lane(IR2_OPND *dest, IR2_OPND *a, IR2_OPND *b,
                           unsigned count)
{
    if (count >= 32) {
        avx_clear(dest);
    } else if (count >= 16) {
        latxs_append_ir2_opnd2i(LISA_VBSRL_V, dest, a, count - 16);
    } else if (count == 0) {
        avx_copy(dest, b);
    } else {
        IR2_OPND tmp = latxs_ra_alloc_ftemp();

        latxs_append_ir2_opnd2i(LISA_VBSRL_V, &tmp, b, count);
        latxs_append_ir2_opnd2i(LISA_VBSLL_V, dest, a, 16 - count);
        latxs_append_ir2_opnd3(LISA_VOR_V, dest, dest, &tmp);
        latxs_ra_free_temp(&tmp);
    }
}

static void avx_align_bytes(IR1_INST *insn, bool ymm)
{
    AvxOperand a = avx_load(insn, ir1_get_opnd(insn, 1), ymm, false);
    AvxOperand b = avx_load(insn, ir1_get_opnd(insn, 2), ymm, false);
    unsigned count = ir1_opnd_uimm(ir1_get_opnd(insn, 3));
    int reg = ir1_opnd_base_reg_num(ir1_get_opnd(insn, 0));
    IR2_OPND low = latxs_ra_alloc_xmm(reg);

    avx_align_lane(&low, &a.low, &b.low, count);
    if (ymm) {
        avx_align_lane(&a.high, &a.high, &b.high, count);
    }
    avx_write_high(reg, ymm ? &a.high : NULL);
}

typedef struct AvxShift {
    IR2_OPCODE immediate;
    IR2_OPCODE uniform;
    IR2_OPCODE replicate;
    unsigned width;
    bool arithmetic;
} AvxShift;

static const AvxShift avx_shifts[X86_INS_ENDING] = {
    [X86_INS_VPSLLW] = { LISA_VSLLI_H, LISA_VSLL_H, LISA_VREPLGR2VR_H, 16 },
    [X86_INS_VPSLLD] = { LISA_VSLLI_W, LISA_VSLL_W, LISA_VREPLGR2VR_W, 32 },
    [X86_INS_VPSLLQ] = { LISA_VSLLI_D, LISA_VSLL_D, LISA_VREPLGR2VR_D, 64 },
    [X86_INS_VPSRLW] = { LISA_VSRLI_H, LISA_VSRL_H, LISA_VREPLGR2VR_H, 16 },
    [X86_INS_VPSRLD] = { LISA_VSRLI_W, LISA_VSRL_W, LISA_VREPLGR2VR_W, 32 },
    [X86_INS_VPSRLQ] = { LISA_VSRLI_D, LISA_VSRL_D, LISA_VREPLGR2VR_D, 64 },
    [X86_INS_VPSRAW] = { LISA_VSRAI_H, LISA_VSRA_H, LISA_VREPLGR2VR_H, 16, true },
    [X86_INS_VPSRAD] = { LISA_VSRAI_W, LISA_VSRA_W, LISA_VREPLGR2VR_W, 32, true },
    [X86_INS_VPSLLDQ] = { .immediate = LISA_VBSLL_V, .width = 16 },
    [X86_INS_VPSRLDQ] = { .immediate = LISA_VBSRL_V, .width = 16 },
};

static void avx_shift_lane(IR2_OPND *dest, IR2_OPND *src, unsigned count,
                           const AvxShift *shift)
{
    if (count >= shift->width && !shift->arithmetic) {
        avx_clear(dest);
    } else {
        latxs_append_ir2_opnd2i(shift->immediate, dest, src,
                                MIN(count, shift->width - 1));
    }
}

static void avx_shift_immediate(IR1_INST *insn, bool ymm,
                                const AvxShift *shift)
{
    AvxOperand src = avx_load(insn, ir1_get_opnd(insn, 1), ymm, false);
    unsigned count = ir1_opnd_uimm(ir1_get_opnd(insn, 2));
    int reg = ir1_opnd_base_reg_num(ir1_get_opnd(insn, 0));
    IR2_OPND low = latxs_ra_alloc_xmm(reg);

    avx_shift_lane(&low, &src.low, count, shift);
    if (ymm) {
        avx_shift_lane(&src.high, &src.high, count, shift);
    }
    avx_write_high(reg, ymm ? &src.high : NULL);
}

static void avx_shift_uniform(IR1_INST *insn, bool ymm, const AvxShift *shift)
{
    AvxOperand src = avx_load(insn, ir1_get_opnd(insn, 1), ymm, false);
    AvxOperand count_src = avx_load(insn, ir1_get_opnd(insn, 2), false, false);
    int reg = ir1_opnd_base_reg_num(ir1_get_opnd(insn, 0));
    IR2_OPND low = latxs_ra_alloc_xmm(reg);
    IR2_OPND counts = latxs_ra_alloc_ftemp();
    IR2_OPND count = latxs_ra_alloc_itemp();
    IR2_OPND in_range = latxs_ra_alloc_itemp();
    IR2_OPND apply = latxs_ir2_opnd_new_label();
    IR2_OPND done = latxs_ir2_opnd_new_label();

    latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_D, &count, &count_src.low, 0);
    latxs_append_ir2_opnd2i(LISA_SLTUI, &in_range, &count, shift->width);
    latxs_append_ir2_opnd2(LISA_BNEZ, &in_range, &apply);
    if (shift->arithmetic) {
        latxs_append_ir2_opnd2i(LISA_ORI, &count, &la_zero, shift->width - 1);
    } else {
        avx_clear(&low);
        if (ymm) {
            avx_clear(&src.high);
        }
        latxs_append_ir2_opnd1(LISA_B, &done);
    }
    latxs_append_ir2_opnd1(LISA_LABEL, &apply);
    latxs_append_ir2_opnd2(shift->replicate, &counts, &count);
    latxs_append_ir2_opnd3(shift->uniform, &low, &src.low, &counts);
    if (ymm) {
        latxs_append_ir2_opnd3(shift->uniform, &src.high, &src.high, &counts);
    }
    latxs_append_ir2_opnd1(LISA_LABEL, &done);
    latxs_ra_free_temp(&in_range);
    latxs_ra_free_temp(&count);
    avx_write_high(reg, ymm ? &src.high : NULL);
}

static const IR2_OPCODE avx_variable_shifts[X86_INS_ENDING] = {
    [X86_INS_VPSLLVD] = LISA_VSLL_W,
    [X86_INS_VPSLLVQ] = LISA_VSLL_D,
    [X86_INS_VPSRLVD] = LISA_VSRL_W,
    [X86_INS_VPSRLVQ] = LISA_VSRL_D,
    [X86_INS_VPSRAVD] = LISA_VSRA_W,
};

static void avx_shift_variable_lane(IR2_OPND *dst, IR2_OPND *src,
                                    IR2_OPND *counts, IR2_OPND *limit,
                                    IR2_OPCODE op)
{
    IR2_OPND adjusted = latxs_ra_alloc_ftemp();
    bool qword = op == LISA_VSLL_D || op == LISA_VSRL_D;

    /* LSX masks the count. x86 clamps arithmetic shifts and zeros
     * logical shifts whose unsigned count is at least the element width. */
    if (op == LISA_VSRA_W) {
        latxs_append_ir2_opnd3(LISA_VMIN_WU, &adjusted, counts, limit);
        latxs_append_ir2_opnd3(op, dst, src, &adjusted);
    } else {
        IR2_OPCODE cmp = qword ? LISA_VSLE_DU : LISA_VSLE_WU;
        latxs_append_ir2_opnd3(cmp, &adjusted, counts, limit);
        latxs_append_ir2_opnd3(op, dst, src, counts);
        latxs_append_ir2_opnd3(LISA_VAND_V, dst, dst, &adjusted);
    }
    latxs_ra_free_temp(&adjusted);
}

static void avx_shift_variable(IR1_INST *insn, bool ymm, IR2_OPCODE op)
{
    AvxOperand src = avx_load(insn, ir1_get_opnd(insn, 1), ymm, false);
    AvxOperand counts = avx_load(insn, ir1_get_opnd(insn, 2), ymm, false);
    int reg = ir1_opnd_base_reg_num(ir1_get_opnd(insn, 0));
    IR2_OPND low = latxs_ra_alloc_xmm(reg);
    IR2_OPND limit = latxs_ra_alloc_ftemp();
    bool qword = op == LISA_VSLL_D || op == LISA_VSRL_D;
    latxs_append_ir2_opnd1i(LISA_VLDI, &limit,
                            VLDI_IMM_TYPE0(qword ? 3 : 2, qword ? 63 : 31));
    avx_shift_variable_lane(&low, &src.low, &counts.low, &limit, op);
    if (ymm) {
        avx_shift_variable_lane(&src.high, &src.high, &counts.high, &limit, op);
    }
    avx_write_high(reg, ymm ? &src.high : NULL);
}

typedef struct AvxBroadcast {
    IR2_OPCODE load;
    IR2_OPCODE from_gpr;
    IR2_OPCODE from_xmm;
    bool avx2;
} AvxBroadcast;

static const AvxBroadcast avx_broadcasts[X86_INS_ENDING] = {
    [X86_INS_VBROADCASTSS] = {
        LISA_LD_WU, LISA_VREPLGR2VR_W, LISA_VREPLVEI_W,
    },
    [X86_INS_VBROADCASTSD] = {
        LISA_LD_D, LISA_VREPLGR2VR_D, LISA_VREPLVEI_D,
    },
    [X86_INS_VPBROADCASTB] = {
        LISA_LD_BU, LISA_VREPLGR2VR_B, LISA_VREPLVEI_B, true,
    },
    [X86_INS_VPBROADCASTW] = {
        LISA_LD_HU, LISA_VREPLGR2VR_H, LISA_VREPLVEI_H, true,
    },
    [X86_INS_VPBROADCASTD] = {
        LISA_LD_WU, LISA_VREPLGR2VR_W, LISA_VREPLVEI_W, true,
    },
    [X86_INS_VPBROADCASTQ] = {
        LISA_LD_D, LISA_VREPLGR2VR_D, LISA_VREPLVEI_D, true,
    },
};

static void avx_broadcast(IR1_INST *insn, bool ymm, const AvxBroadcast *op)
{
    IR1_OPND *src = ir1_get_opnd(insn, 1);
    int reg = ir1_opnd_base_reg_num(ir1_get_opnd(insn, 0));
    IR2_OPND low = latxs_ra_alloc_xmm(reg);

    if (ir1_opnd_is_mem(src)) {
        IR2_OPND scalar = avx_load_scalar(insn, src, op->load);

        latxs_append_ir2_opnd2(op->from_gpr, &low, &scalar);
        latxs_ra_free_temp(&scalar);
    } else {
        IR2_OPND xmm = latxs_ra_alloc_xmm(ir1_opnd_base_reg_num(src));

        latxs_append_ir2_opnd2i(op->from_xmm, &low, &xmm, 0);
    }
    avx_write_high(reg, ymm ? &low : NULL);
}

static void avx_blend_dwords_lane(IR2_OPND *dst, IR2_OPND *a, IR2_OPND *b,
                                 unsigned select)
{
    if (select == 0 || select == 15) {
        avx_copy(dst, select ? b : a);
        return;
    }

    IR2_OPND mask = latxs_ra_alloc_ftemp();
    IR2_OPND ones = latxs_ra_alloc_itemp();

    avx_clear(&mask);
    latxs_load_imm64(&ones, -1);
    for (unsigned i = 0; i < 4; i++) {
        if (select & (1u << i)) {
            latxs_append_ir2_opnd2i(LISA_VINSGR2VR_W, &mask, &ones, i);
        }
    }
    latxs_append_ir2_opnd4(LISA_VBITSEL_V, dst, a, b, &mask);
    latxs_ra_free_temp(&ones);
    latxs_ra_free_temp(&mask);
}

static void avx_blend_dwords(IR1_INST *insn, bool ymm)
{
    AvxOperand a = avx_load(insn, ir1_get_opnd(insn, 1), ymm, false);
    AvxOperand b = avx_load(insn, ir1_get_opnd(insn, 2), ymm, false);
    unsigned select = ir1_opnd_uimm(ir1_get_opnd(insn, 3));
    int reg = ir1_opnd_base_reg_num(ir1_get_opnd(insn, 0));
    IR2_OPND low = latxs_ra_alloc_xmm(reg);

    avx_blend_dwords_lane(&low, &a.low, &b.low, select & 15);
    if (ymm) {
        avx_blend_dwords_lane(&a.high, &a.high, &b.high, select >> 4);
    }
    avx_write_high(reg, ymm ? &a.high : NULL);
}

/* VBLENDPD: qword immediate blend.  Each selector bit covers one qword,
 * which is two adjacent dwords, so expand the selector into the dword mask
 * used by the shared blend lane helper. */
static void avx_blend_qwords(IR1_INST *insn, bool ymm)
{
    AvxOperand a = avx_load(insn, ir1_get_opnd(insn, 1), ymm, false);
    AvxOperand b = avx_load(insn, ir1_get_opnd(insn, 2), ymm, false);
    unsigned select = ir1_opnd_uimm(ir1_get_opnd(insn, 3));
    int reg = ir1_opnd_base_reg_num(ir1_get_opnd(insn, 0));
    IR2_OPND low = latxs_ra_alloc_xmm(reg);
    unsigned low_mask = (select & 1 ? 0x3 : 0) | (select & 2 ? 0xc : 0);
    unsigned high_mask = (select & 4 ? 0x3 : 0) | (select & 8 ? 0xc : 0);

    avx_blend_dwords_lane(&low, &a.low, &b.low, low_mask);
    if (ymm) {
        avx_blend_dwords_lane(&a.high, &a.high, &b.high, high_mask);
    }
    avx_write_high(reg, ymm ? &a.high : NULL);
}

static void avx_abs(IR1_INST *insn, bool ymm, IR2_OPCODE op)
{
    AvxOperand src = avx_load(insn, ir1_get_opnd(insn, 1), ymm, false);
    IR2_OPND zero = latxs_ra_alloc_ftemp();
    int reg = ir1_opnd_base_reg_num(ir1_get_opnd(insn, 0));
    IR2_OPND low = latxs_ra_alloc_xmm(reg);

    avx_clear(&zero);
    latxs_append_ir2_opnd3(op, &low, &src.low, &zero);
    if (ymm) {
        latxs_append_ir2_opnd3(op, &src.high, &src.high, &zero);
    }
    avx_write_high(reg, ymm ? &src.high : NULL);
}

static void avx_crypto128(IR1_INST *insn)
{
    IR1_OPCODE opcode = ir1_opcode(insn);
    bool clmul = opcode == X86_INS_VPCLMULQDQ;
    uint32_t feature = clmul ? CPUID_EXT_PCLMULQDQ : CPUID_EXT_AES;
    IR1_OPND *dst = ir1_get_opnd(insn, 0);

    if (ir1_opnd_size(dst) != 128 ||
        !(lsenv->tr_data->sys.cpuid_ext_features & feature)) {
        latxs_tr_gen_excp_illegal_op(insn, 1);
        return;
    }
    if (avx_unavailable(insn, false)) {
        return;
    }

    AvxOperand src = avx_load(insn, ir1_get_opnd(insn, 1), false, false);
    int reg = ir1_opnd_base_reg_num(dst);
    IR2_OPND low = latxs_ra_alloc_xmm(reg);
    IR2_OPND result = latxs_ra_alloc_ftemp();

    if (opcode == X86_INS_VAESIMC) {
        latxs_gen_avx_aes(opcode, result, src.low, src.low, 0);
    } else if (opcode == X86_INS_VAESKEYGENASSIST) {
        uint8_t imm = ir1_opnd_uimm(ir1_get_opnd(insn, 2));
        latxs_gen_avx_aes(opcode, result, src.low, src.low, imm);
    } else {
        AvxOperand rhs = avx_load(insn, ir1_get_opnd(insn, 2), false, false);
        if (clmul) {
            uint8_t imm = ir1_opnd_uimm(ir1_get_opnd(insn, 3));
            latxs_gen_pclmulqdq(result, src.low, rhs.low, imm);
        } else {
            latxs_gen_avx_aes(opcode, result, src.low, rhs.low, 0);
        }
    }

    avx_copy(&low, &result);
    avx_write_high(reg, NULL);
}

#endif

#if defined(CONFIG_SOFTMMU) && !defined(CONFIG_LATX_TCG_MODE)
static IR2_OPND avx_cvt_scratch_base(void)
{
    IR2_OPND base = latxs_ra_alloc_itemp();

    latxs_load_imm64(&base, offsetof(CPUX86State, avx_cvt_operands));
    latxs_append_ir2_opnd3(LISA_ADD_D, &base, &latxs_env_ir2_opnd, &base);
    return base;
}

/* Store one conversion source operand into the scratch buffer at off.
 * Register operands are copied as one XMM register; memory operands use the
 * exact architectural width so a 4/8-byte access cannot cross a page. */
static void avx_cvt_store(IR1_INST *insn, IR1_OPND *op, IR2_OPND *base,
                          unsigned off, unsigned bytes)
{
    if (ir1_opnd_is_mem(op) && bytes < 128) {
        IR2_OPND value = latxs_ra_alloc_itemp();

        latxs_load_ir1_to_ir2(&value, op, bytes == 64 ? EXMode_N : EXMode_Z);
        latxs_append_ir2_opnd2i(bytes == 64 ? LISA_ST_D : LISA_ST_W,
                                &value, base, off);
        latxs_ra_free_temp(&value);
        return;
    }

    AvxOperand value = avx_load(insn, op, bytes == 256, false);

    latxs_append_ir2_opnd2i(LISA_VST, &value.low, base, off);
    if (bytes == 256) {
        latxs_append_ir2_opnd2i(LISA_VST, &value.high, base, off + 16);
    }
}

static void avx_cvt_write_result(IR1_INST *insn, IR2_OPND *base,
                                 unsigned bytes, bool gpr_dest)
{
    IR1_OPND *dst = ir1_get_opnd(insn, 0);

    if (gpr_dest) {
        IR2_OPND result = latxs_ra_alloc_itemp();

        latxs_append_ir2_opnd2i(LISA_LD_D, &result, base, 0);
        latxs_store_ir2_to_ir1(&result, dst);
        latxs_ra_free_temp(&result);
        return;
    }

    int reg = ir1_opnd_base_reg_num(dst);
    IR2_OPND low = latxs_ra_alloc_xmm(reg);

    latxs_append_ir2_opnd2i(LISA_VLD, &low, base, 0);
    if (bytes == 32) {
        IR2_OPND high = latxs_ra_alloc_ftemp();

        latxs_append_ir2_opnd2i(LISA_VLD, &high, base, 16);
        avx_write_high(reg, &high);
        latxs_ra_free_temp(&high);
    } else {
        avx_write_high(reg, NULL);
    }
}

/* Store a conversion result to a memory destination (VCVTPS2PH only). */
static void avx_cvt_store_mem(IR1_INST *insn, IR2_OPND *base, unsigned bytes)
{
    IR1_OPND *dst = ir1_get_opnd(insn, 0);
    IR2_OPND addr = avx_addr(insn, dst, 0);

    if (bytes == 8) {
        IR2_OPND value = latxs_ra_alloc_itemp();

        latxs_append_ir2_opnd2i(LISA_LD_D, &value, base, 0);
        gen_ldst_softmmu_helper(LISA_ST_D, &value, &addr, 0, 1);
        latxs_ra_free_temp(&value);
    } else {
        IR2_OPND value = latxs_ra_alloc_ftemp();

        latxs_append_ir2_opnd2i(LISA_VLD, &value, base, 0);
        avx_mem_lane(&value, &addr, 0, false);
        latxs_ra_free_temp(&value);
    }
    latxs_ra_free_temp(&addr);
}

static bool avx_translate_scalar_cmp(IR1_INST *insn, unsigned pred,
                                     bool qword);

/* Translate one VEX conversion instruction through latxs_avx_fp(). */
/* ---- scalar FP fast path (VEX SS/SD arithmetic) ------------------------- *
 * Same gates as the packed fast path: MXCSR.RC=nearest, every SSE exception
 * masked, operand/result elements free of NaN/Inf/denormal.  LoongArch scalar
 * fp instructions evaluate lane 0; the VEX.vvvv operand supplies the untouched
 * upper bits and VEX.128 clears the ymm upper half.
 */
static void avx_fast_gate(IR2_OPND *fail)
{
    IR2_OPND m = latxs_ra_alloc_itemp();
    IR2_OPND t = latxs_ra_alloc_itemp();

    latxs_append_ir2_opnd2i(LISA_LDPTR_W, &m, &latxs_env_ir2_opnd,
                            offsetof(CPUX86State, mxcsr) / 4);
    latxs_append_ir2_opnd2ii(LISA_BSTRPICK_W, &t, &m, 14, 13);
    latxs_append_ir2_opnd2(LISA_BNEZ, &t, fail);
    latxs_append_ir2_opnd2ii(LISA_BSTRPICK_W, &t, &m, 12, 7);
    latxs_append_ir2_opnd2i(LISA_XORI, &t, &t, 0x3f);
    latxs_append_ir2_opnd2(LISA_BNEZ, &t, fail);
    latxs_ra_free_temp(&t);
    latxs_ra_free_temp(&m);
}

static void avx_fast_class_gate(IR2_OPND *vec, bool qword, unsigned mask,
                                IR2_OPND *fail)
{
    IR2_OPND cls = latxs_ra_alloc_ftemp();
    IR2_OPND g = latxs_ra_alloc_itemp();

    latxs_append_ir2_opnd2(qword ? LISA_VFCLASS_D : LISA_VFCLASS_S, &cls, vec);
    latxs_append_ir2_opnd2i(qword ? LISA_VPICKVE2GR_D : LISA_VPICKVE2GR_W,
                            &g, &cls, 0);
    latxs_append_ir2_opnd2i(LISA_ANDI, &g, &g, mask);
    latxs_append_ir2_opnd2(LISA_BNEZ, &g, fail);
    latxs_ra_free_temp(&g);
    latxs_ra_free_temp(&cls);
}

static void avx_fast_commit_flags(void)
{
    static const struct { unsigned mxcsr, fcsr; } fbits[] = {
        { 5, 24 }, { 4, 25 }, { 3, 26 }, { 2, 27 }, { 0, 28 },
    };
    IR2_OPND mxcsr = latxs_ra_alloc_itemp();
    IR2_OPND bits = latxs_ra_alloc_itemp();
    IR2_OPND t = latxs_ra_alloc_itemp();

    latxs_append_ir2_opnd2i(LISA_LDPTR_W, &mxcsr, &latxs_env_ir2_opnd,
                            offsetof(CPUX86State, mxcsr) / 4);
    latxs_append_ir2_opnd2(LISA_MOVFCSR2GR, &bits, &latxs_fcsr2_ir2_opnd);
    for (unsigned i = 0; i < ARRAY_SIZE(fbits); i++) {
        latxs_append_ir2_opnd2ii(LISA_BSTRPICK_W, &t, &bits, fbits[i].fcsr,
                                 fbits[i].fcsr);
        latxs_append_ir2_opnd2i(LISA_SLLI_W, &t, &t, fbits[i].mxcsr);
        latxs_append_ir2_opnd3(LISA_OR, &mxcsr, &mxcsr, &t);
    }
    latxs_append_ir2_opnd2i(LISA_ST_W, &mxcsr, &latxs_env_ir2_opnd,
                            offsetof(CPUX86State, mxcsr));
    latxs_ra_free_temp(&t);
    latxs_ra_free_temp(&bits);
    latxs_ra_free_temp(&mxcsr);
}

/* Emit the scalar fast path for the given conversion kind; returns false when
 * this kind is not handled (the generic helper path is used).  The caller
 * places a `fail` label so that any runtime gate failure falls through to the
 * generic helper path (no destination write / no MXCSR change happened). */
static bool avx_scalar_fast_emit(IR1_INST *insn, unsigned kind, bool qword,
                                 IR2_OPND *fail)
{
    IR2_OPCODE op;
    bool unary = false;

    switch (kind) {
    case LATX_AVX_CVT_ADDSS: op = LISA_FADD_S; break;
    case LATX_AVX_CVT_ADDSD: op = LISA_FADD_D; break;
    case LATX_AVX_CVT_SUBSS: op = LISA_FSUB_S; break;
    case LATX_AVX_CVT_SUBSD: op = LISA_FSUB_D; break;
    case LATX_AVX_CVT_MULSS: op = LISA_FMUL_S; break;
    case LATX_AVX_CVT_MULSD: op = LISA_FMUL_D; break;
    case LATX_AVX_CVT_DIVSS: op = LISA_FDIV_S; break;
    case LATX_AVX_CVT_DIVSD: op = LISA_FDIV_D; break;
    case LATX_AVX_CVT_SQRTSS: op = LISA_FSQRT_S; unary = true; break;
    case LATX_AVX_CVT_SQRTSD: op = LISA_FSQRT_D; unary = true; break;
    default:
        return false;
    }

    IR1_OPND *dst = ir1_get_opnd(insn, 0);
    IR1_OPND *merge = ir1_get_opnd(insn, 1);
    IR1_OPND *src = ir1_get_opnd(insn, 2);
    /* The convert() caller does not set qword for scalar kinds; the SSD/SD
     * pair decides the floating element width here. */
    bool dbl = kind == LATX_AVX_CVT_ADDSD || kind == LATX_AVX_CVT_SUBSD ||
               kind == LATX_AVX_CVT_MULSD || kind == LATX_AVX_CVT_DIVSD ||
               kind == LATX_AVX_CVT_SQRTSD;

    if (ir1_opnd_size(dst) != 128 || !ir1_opnd_is_xmm(merge)) {
        return false;
    }

    AvxOperand m = avx_load(insn, merge, false, false);
    AvxOperand s = avx_load(insn, src, false, false);
    IR2_OPND value = latxs_ra_alloc_ftemp();
    IR2_OPND save = latxs_ra_alloc_itemp();

    avx_fast_gate(fail);
    avx_fast_class_gate(&m.low, dbl, 0x157, fail);
    avx_fast_class_gate(&s.low, dbl, 0x157, fail);
    latxs_append_ir2_opnd2(LISA_MOVFCSR2GR, &save, &latxs_fcsr1_ir2_opnd);
    latxs_append_ir2_opnd2(LISA_MOVGR2FCSR, &latxs_fcsr1_ir2_opnd, &la_zero);
    if (unary) {
        latxs_append_ir2_opnd2(op, &value, &s.low);
    } else {
        latxs_append_ir2_opnd3(op, &value, &m.low, &s.low);
    }
    latxs_append_ir2_opnd2(LISA_MOVGR2FCSR, &latxs_fcsr1_ir2_opnd, &save);
    latxs_ra_free_temp(&save);
    avx_fast_class_gate(&value, dbl, 0x113, fail);
    avx_fast_commit_flags();

    int reg = ir1_opnd_base_reg_num(dst);
    IR2_OPND low = latxs_ra_alloc_xmm(reg);

    avx_copy(&low, &m.low);
    latxs_append_ir2_opnd2i(dbl ? LISA_VEXTRINS_D : LISA_VEXTRINS_W,
                            &low, &value, VEXTRINS_IMM_4_0(0, 0));
    avx_write_high(reg, NULL);
    latxs_ra_free_temp(&value);
    return true;
}

/* Finish a scalar VEX.128 result: dst = merge (upper bits) with lane 0
 * replaced by value, and the ymm upper half cleared. */
static void avx_fast_write_scalar(IR1_INST *insn, IR1_OPND *merge,
                                  IR2_OPND *value, bool dbl)
{
    int reg = ir1_opnd_base_reg_num(ir1_get_opnd(insn, 0));
    IR2_OPND low = latxs_ra_alloc_xmm(reg);

    if (merge) {
        AvxOperand m = avx_load(insn, merge, false, false);
        avx_copy(&low, &m.low);
    }
    latxs_append_ir2_opnd2i(dbl ? LISA_VEXTRINS_D : LISA_VEXTRINS_W,
                            &low, value, VEXTRINS_IMM_4_0(0, 0));
    avx_write_high(reg, NULL);
}

/* Materialise an f64 constant from its raw bits into an FPR. */
static IR2_OPND avx_fast_fp_constant(uint64_t bits)
{
    IR2_OPND g = latxs_ra_alloc_itemp();
    IR2_OPND f = latxs_ra_alloc_ftemp();

    latxs_load_imm64_to_ir2(&g, bits);
    latxs_append_ir2_opnd2(LISA_MOVGR2FR_D, &f, &g);
    latxs_ra_free_temp(&g);
    return f;
}

/* Native fast path for int<->fp and fp<->fp conversions.  Returns false for
 * kinds that stay on the softfloat helper.  All paths fall through to the
 * caller's `fail` label if a runtime gate rejects the operands (no dest write,
 * no MXCSR change). */
static bool avx_convert_fast_emit(IR1_INST *insn, unsigned kind, bool qword,
                                  IR2_OPND *fail)
{
    bool gpr_src = false, gpr_dest = false;
    bool merge = false, dbl = false, srcdbl = false;
    IR2_OPCODE op = LISA_INVALID;
    int target_bits = 0;

    switch (kind) {
    case LATX_AVX_CVT_SI2SS:
        gpr_src = merge = true; op = LISA_FFINT_S_W;
        if (qword) op = LISA_FFINT_S_L;
        break;
    case LATX_AVX_CVT_SI2SD:
        gpr_src = merge = true; dbl = true; op = LISA_FFINT_D_W;
        if (qword) op = LISA_FFINT_D_L;
        break;
    case LATX_AVX_CVT_SS2SD:
        merge = true; dbl = true; srcdbl = false; op = LISA_FCVT_D_S;
        break;
    case LATX_AVX_CVT_SD2SS:
        merge = true; srcdbl = true; op = LISA_FCVT_S_D;
        break;
    case LATX_AVX_CVT_SS2SI:
    case LATX_AVX_CVT_TSS2SI:
        gpr_dest = true; srcdbl = false; target_bits = qword ? 64 : 32;
        op = kind == LATX_AVX_CVT_SS2SI ?
             (qword ? LISA_FTINT_L_S : LISA_FTINT_W_S) :
             (qword ? LISA_FTINTRZ_L_S : LISA_FTINTRZ_W_S);
        break;
    case LATX_AVX_CVT_SD2SI:
    case LATX_AVX_CVT_TSD2SI:
        gpr_dest = true; srcdbl = true; target_bits = qword ? 64 : 32;
        op = kind == LATX_AVX_CVT_SD2SI ?
             (qword ? LISA_FTINT_L_D : LISA_FTINT_W_D) :
             (qword ? LISA_FTINTRZ_L_D : LISA_FTINTRZ_W_D);
        break;
    default:
        return false;
    }

    avx_fast_gate(fail);

    if (gpr_src) {
        IR1_OPND *src = ir1_get_opnd(insn, 2);
        IR2_OPND g = latxs_ra_alloc_itemp();
        IR2_OPND value = latxs_ra_alloc_ftemp();
        IR2_OPND save = latxs_ra_alloc_itemp();

        latxs_load_ir1_to_ir2(&g, src, qword ? EXMode_N : EXMode_Z);
        latxs_append_ir2_opnd2(LISA_MOVGR2FR_D, &value, &g);
        latxs_ra_free_temp(&g);
        latxs_append_ir2_opnd2(LISA_MOVFCSR2GR, &save, &latxs_fcsr1_ir2_opnd);
        latxs_append_ir2_opnd2(LISA_MOVGR2FCSR, &latxs_fcsr1_ir2_opnd,
                               &la_zero);
        latxs_append_ir2_opnd2(op, &value, &value);
        latxs_append_ir2_opnd2(LISA_MOVGR2FCSR, &latxs_fcsr1_ir2_opnd,
                               &save);
        latxs_ra_free_temp(&save);
        avx_fast_commit_flags();
        avx_fast_write_scalar(insn, ir1_get_opnd(insn, 1), &value, dbl);
    } else if (gpr_dest) {
        IR1_OPND *src = ir1_get_opnd(insn, 1);
        AvxOperand s = avx_load(insn, src, false, false);
        IR2_OPND conv = latxs_ra_alloc_ftemp();
        IR2_OPND g = latxs_ra_alloc_itemp();
        IR2_OPND save = latxs_ra_alloc_itemp();
        IR2_OPND bound = avx_fast_fp_constant(
            target_bits == 64 ? UINT64_C(0x43e0000000000000) :
                                UINT64_C(0x41e0000000000000));   /* +2^63 / +2^31 */
        IR2_OPND neg = avx_fast_fp_constant(
            target_bits == 64 ? UINT64_C(0xc3e0000000000000) :
                                UINT64_C(0xc1e0000000000000));

        avx_fast_class_gate(&s.low, srcdbl, 0x157, fail);
        /* -bound < x < bound (strict) keeps truncation inside the target. */
        latxs_append_ir2_opnd3i(srcdbl ? LISA_FCMP_COND_D : LISA_FCMP_COND_S,
                                &latxs_fcc0_ir2_opnd, &s.low, &bound,
                                FCMP_COND_CLT);
        latxs_append_ir2_opnd2(LISA_BCEQZ, &latxs_fcc0_ir2_opnd, fail);
        latxs_append_ir2_opnd3i(srcdbl ? LISA_FCMP_COND_D : LISA_FCMP_COND_S,
                                &latxs_fcc0_ir2_opnd, &neg, &s.low,
                                FCMP_COND_CLT);
        latxs_append_ir2_opnd2(LISA_BCEQZ, &latxs_fcc0_ir2_opnd, fail);
        latxs_ra_free_temp(&bound);
        latxs_ra_free_temp(&neg);
        latxs_append_ir2_opnd2(LISA_MOVFCSR2GR, &save, &latxs_fcsr1_ir2_opnd);
        latxs_append_ir2_opnd2(LISA_MOVGR2FCSR, &latxs_fcsr1_ir2_opnd,
                               &la_zero);
        latxs_append_ir2_opnd2(op, &conv, &s.low);
        latxs_append_ir2_opnd2(LISA_MOVGR2FCSR, &latxs_fcsr1_ir2_opnd,
                               &save);
        latxs_ra_free_temp(&save);
        latxs_append_ir2_opnd2(LISA_MOVFR2GR_D, &g, &conv);
        latxs_ra_free_temp(&conv);
        avx_fast_commit_flags();
        latxs_store_ir2_to_ir1(&g, ir1_get_opnd(insn, 0));
        latxs_ra_free_temp(&g);
    } else {
        IR1_OPND *src = ir1_get_opnd(insn, 2);
        AvxOperand s = avx_load(insn, src, false, false);
        IR2_OPND value = latxs_ra_alloc_ftemp();
        IR2_OPND save = latxs_ra_alloc_itemp();

        avx_fast_class_gate(&s.low, srcdbl, 0x157, fail);
        latxs_append_ir2_opnd2(LISA_MOVFCSR2GR, &save, &latxs_fcsr1_ir2_opnd);
        latxs_append_ir2_opnd2(LISA_MOVGR2FCSR, &latxs_fcsr1_ir2_opnd,
                               &la_zero);
        latxs_append_ir2_opnd2(op, &value, &s.low);
        latxs_append_ir2_opnd2(LISA_MOVGR2FCSR, &latxs_fcsr1_ir2_opnd,
                               &save);
        latxs_ra_free_temp(&save);
        avx_fast_class_gate(&value, dbl, 0x113, fail);
        avx_fast_commit_flags();
        avx_fast_write_scalar(insn, ir1_get_opnd(insn, 1), &value, dbl);
    }
    return true;
}

static bool avx_translate_convert(IR1_INST *insn)
{
    IR1_OPCODE opcode = ir1_opcode(insn);
    bool ymm = false;
    bool qword = false;
    bool gpr_src = false;
    bool gpr_dest = false;
    bool merge = false;
    bool need_avx2 = false;
    bool mem_dest = false;
    unsigned kind;
    unsigned src_bytes = 128;
    unsigned res_bytes = 16;
    unsigned store_bytes = 0;
    unsigned imm_idx = 0;
    unsigned merge_bytes = 128;
    IR1_OPND *src;

    if (opcode >= X86_INS_VCMPEQSS && opcode <= X86_INS_VCMPTRUE_USSS) {
        return avx_translate_scalar_cmp(insn,
                                        opcode - X86_INS_VCMPEQSS, false);
    }
    if (opcode >= X86_INS_VCMPEQSD && opcode <= X86_INS_VCMPTRUE_USSD) {
        return avx_translate_scalar_cmp(insn,
                                        opcode - X86_INS_VCMPEQSD, true);
    }
    if (opcode == X86_INS_VCMPSS || opcode == X86_INS_VCMPSD) {
        bool qword = opcode == X86_INS_VCMPSD;
        unsigned pred = ir1_opnd_num(insn) > 3 ?
            (ir1_opnd_uimm(ir1_get_opnd(insn, 3)) & 31) : 0;

        return avx_translate_scalar_cmp(insn, pred, qword);
    }

    switch (opcode) {
    case X86_INS_VCVTSI2SS:
    case X86_INS_VCVTSI2SD:
        kind = opcode == X86_INS_VCVTSI2SS ? LATX_AVX_CVT_SI2SS :
                                             LATX_AVX_CVT_SI2SD;
        src = ir1_get_opnd(insn, 2);
        gpr_src = merge = true;
        qword = ir1_opnd_size(src) == 64;
        src_bytes = qword ? 64 : 32;
        break;
    case X86_INS_VCVTSS2SD:
    case X86_INS_VCVTSD2SS:
        kind = opcode == X86_INS_VCVTSS2SD ? LATX_AVX_CVT_SS2SD :
                                             LATX_AVX_CVT_SD2SS;
        src = ir1_get_opnd(insn, 2);
        merge = true;
        src_bytes = opcode == X86_INS_VCVTSS2SD ? 32 : 64;
        break;
    case X86_INS_VCVTDQ2PS:
    case X86_INS_VCVTDQ2PD:
    case X86_INS_VCVTPS2DQ:
    case X86_INS_VCVTTPS2DQ:
    case X86_INS_VCVTPD2PSX:
    case X86_INS_VCVTPD2PS:
    case X86_INS_VCVTPD2DQX:
    case X86_INS_VCVTPD2DQ:
    case X86_INS_VCVTTPD2DQX:
    case X86_INS_VCVTTPD2DQ:
    case X86_INS_VCVTPS2PD:
        src = ir1_get_opnd(insn, 1);
        /* Widening conversions select their width by destination, the
         * narrowing ones by source; the other forms keep a single width. */
        if (opcode == X86_INS_VCVTPD2PSX || opcode == X86_INS_VCVTPD2PS ||
            opcode == X86_INS_VCVTPD2DQX || opcode == X86_INS_VCVTPD2DQ ||
            opcode == X86_INS_VCVTTPD2DQX ||
            opcode == X86_INS_VCVTTPD2DQ) {
            ymm = ir1_opnd_size(src) == 256;
        } else {
            ymm = ir1_opnd_size(ir1_get_opnd(insn, 0)) == 256;
        }
        switch (opcode) {
        case X86_INS_VCVTDQ2PS:
            kind = LATX_AVX_CVT_DQ2PS;
            src_bytes = ymm ? 256 : 128;
            res_bytes = ymm ? 32 : 16;
            break;
        case X86_INS_VCVTDQ2PD:
        case X86_INS_VCVTPS2PD:
            kind = opcode == X86_INS_VCVTDQ2PD ? LATX_AVX_CVT_DQ2PD :
                                                 LATX_AVX_CVT_PS2PD;
            src_bytes = ymm ? 128 : 64;
            res_bytes = ymm ? 32 : 16;
            break;
        case X86_INS_VCVTPS2DQ:
        case X86_INS_VCVTTPS2DQ:
            kind = opcode == X86_INS_VCVTPS2DQ ? LATX_AVX_CVT_PS2DQ :
                                                 LATX_AVX_CVT_TPS2DQ;
            src_bytes = ymm ? 256 : 128;
            res_bytes = ymm ? 32 : 16;
            break;
        default:
            /* Narrowing conversions always leave an XMM destination. */
            kind = opcode == X86_INS_VCVTPD2PSX ||
                   opcode == X86_INS_VCVTPD2PS ? LATX_AVX_CVT_PD2PS :
                   opcode == X86_INS_VCVTPD2DQX ||
                   opcode == X86_INS_VCVTPD2DQ ? LATX_AVX_CVT_PD2DQ :
                                                 LATX_AVX_CVT_TPD2DQ;
            src_bytes = ymm ? 256 : 128;
            res_bytes = 16;
            break;
        }
        break;
    case X86_INS_VCVTPH2PS:
        kind = LATX_AVX_CVT_PH2PS;
        src = ir1_get_opnd(insn, 1);
        /* Width comes from the destination: xmm<-m64/xmm, ymm<-m128/xmm. */
        ymm = ir1_opnd_size(ir1_get_opnd(insn, 0)) == 256;
        src_bytes = ir1_opnd_is_mem(src) ? (ymm ? 128 : 64) : 128;
        res_bytes = ymm ? 32 : 16;
        break;
    case X86_INS_VCVTPS2PH:
        kind = LATX_AVX_CVT_PS2PH;
        src = ir1_get_opnd(insn, 1);
        /* Width comes from the register source; the destination is an xmm
         * register or an m64/m128 store. */
        ymm = ir1_opnd_size(src) == 256;
        src_bytes = ymm ? 256 : 128;
        store_bytes = ymm ? 16 : 8;
        mem_dest = ir1_opnd_is_mem(ir1_get_opnd(insn, 0));
        imm_idx = 2;
        res_bytes = 16;
        break;
    case X86_INS_VMOVSHDUP:
    case X86_INS_VMOVSLDUP:
        kind = opcode == X86_INS_VMOVSHDUP ? LATX_AVX_CVT_SHDUP :
                                             LATX_AVX_CVT_SLDUP;
        src = ir1_get_opnd(insn, 1);
        ymm = ir1_opnd_size(ir1_get_opnd(insn, 0)) == 256;
        src_bytes = ymm ? 256 : 128;
        res_bytes = ymm ? 32 : 16;
        break;
    case X86_INS_VRCPPS:
    case X86_INS_VRSQRTPS:
        kind = opcode == X86_INS_VRCPPS ? LATX_AVX_CVT_RCPPS :
                                          LATX_AVX_CVT_RSQRTPS;
        src = ir1_get_opnd(insn, 1);
        ymm = ir1_opnd_size(ir1_get_opnd(insn, 0)) == 256;
        src_bytes = ymm ? 256 : 128;
        res_bytes = ymm ? 32 : 16;
        break;
    case X86_INS_VRCPSS:
    case X86_INS_VRSQRTSS:
        kind = opcode == X86_INS_VRCPSS ? LATX_AVX_CVT_RCPSS :
                                          LATX_AVX_CVT_RSQRTSS;
        src = ir1_get_opnd(insn, 2);
        merge = true;
        src_bytes = 32;
        res_bytes = 16;
        break;
    case X86_INS_VADDSS:
    case X86_INS_VADDSD:
    case X86_INS_VSUBSS:
    case X86_INS_VSUBSD:
    case X86_INS_VMULSS:
    case X86_INS_VMULSD:
    case X86_INS_VDIVSS:
    case X86_INS_VDIVSD:
    case X86_INS_VMINSS:
    case X86_INS_VMINSD:
    case X86_INS_VMAXSS:
    case X86_INS_VMAXSD:
        switch (opcode) {
        case X86_INS_VADDSS:
            kind = LATX_AVX_CVT_ADDSS;
            break;
        case X86_INS_VADDSD:
            kind = LATX_AVX_CVT_ADDSD;
            break;
        case X86_INS_VSUBSS:
            kind = LATX_AVX_CVT_SUBSS;
            break;
        case X86_INS_VSUBSD:
            kind = LATX_AVX_CVT_SUBSD;
            break;
        case X86_INS_VMULSS:
            kind = LATX_AVX_CVT_MULSS;
            break;
        case X86_INS_VMULSD:
            kind = LATX_AVX_CVT_MULSD;
            break;
        case X86_INS_VDIVSS:
            kind = LATX_AVX_CVT_DIVSS;
            break;
        case X86_INS_VDIVSD:
            kind = LATX_AVX_CVT_DIVSD;
            break;
        case X86_INS_VMINSS:
            kind = LATX_AVX_CVT_MINSS;
            break;
        case X86_INS_VMINSD:
            kind = LATX_AVX_CVT_MINSD;
            break;
        case X86_INS_VMAXSS:
            kind = LATX_AVX_CVT_MAXSS;
            break;
        default:
            kind = LATX_AVX_CVT_MAXSD;
            break;
        }
        src = ir1_get_opnd(insn, 2);
        merge = true;
        src_bytes = (opcode == X86_INS_VADDSS || opcode == X86_INS_VSUBSS ||
                     opcode == X86_INS_VMULSS || opcode == X86_INS_VDIVSS ||
                     opcode == X86_INS_VMINSS || opcode == X86_INS_VMAXSS) ?
                    32 : 64;
        break;
    case X86_INS_VPBLENDW:
        kind = LATX_AVX_CVT_PBLENDW;
        src = ir1_get_opnd(insn, 2);
        /* VEX.vvvv is the first blend source; imm8 selects word lanes. */
        merge = true;
        imm_idx = 3;
        ymm = ir1_opnd_size(ir1_get_opnd(insn, 0)) == 256;
        need_avx2 = ymm;
        src_bytes = ymm ? 256 : 128;
        merge_bytes = src_bytes;
        res_bytes = ymm ? 32 : 16;
        break;
    case X86_INS_VPMULHRSW:
        kind = LATX_AVX_CVT_PMULHRSW;
        src = ir1_get_opnd(insn, 2);
        /* VEX.vvvv is the first multiplicand, not a merge source. */
        merge = true;
        ymm = ir1_opnd_size(ir1_get_opnd(insn, 0)) == 256;
        need_avx2 = ymm;
        src_bytes = ymm ? 256 : 128;
        merge_bytes = src_bytes;
        res_bytes = ymm ? 32 : 16;
        break;
    case X86_INS_VMOVDDUP:
        kind = LATX_AVX_CVT_DDUP;
        src = ir1_get_opnd(insn, 1);
        ymm = ir1_opnd_size(ir1_get_opnd(insn, 0)) == 256;
        src_bytes = ir1_opnd_is_mem(src) ? ir1_opnd_size(src) :
                     (ymm ? 256 : 128);
        res_bytes = ymm ? 32 : 16;
        break;
    case X86_INS_VPSHUFLW:
    case X86_INS_VPSHUFHW:
        /* VEX.128.F2/F3.0F 70 /r ib: two-operand word shuffle, vvvv is
         * reserved (checked by avx_reserved_vvvv).  The VEX.256 form is the
         * AVX2 lane-wise variant; imm8 applies independently per 128-bit
         * lane and only the selected 64-bit half of each lane is shuffled. */
        kind = opcode == X86_INS_VPSHUFLW ? LATX_AVX_CVT_PSHUFLW :
                                            LATX_AVX_CVT_PSHUFHW;
        src = ir1_get_opnd(insn, 1);
        imm_idx = 2;
        ymm = ir1_opnd_size(ir1_get_opnd(insn, 0)) == 256;
        need_avx2 = ymm;
        src_bytes = ymm ? 256 : 128;
        res_bytes = ymm ? 32 : 16;
        break;
    case X86_INS_VPHADDW:
    case X86_INS_VPHADDD:
    case X86_INS_VPHADDSW:
    case X86_INS_VPHSUBW:
    case X86_INS_VPHSUBD:
    case X86_INS_VPHSUBSW:
        switch (opcode) {
        case X86_INS_VPHADDW:
            kind = LATX_AVX_CVT_PHADDW;
            break;
        case X86_INS_VPHADDD:
            kind = LATX_AVX_CVT_PHADDD;
            break;
        case X86_INS_VPHADDSW:
            kind = LATX_AVX_CVT_PHADDSW;
            break;
        case X86_INS_VPHSUBW:
            kind = LATX_AVX_CVT_PHSUBW;
            break;
        case X86_INS_VPHSUBD:
            kind = LATX_AVX_CVT_PHSUBD;
            break;
        default:
            kind = LATX_AVX_CVT_PHSUBSW;
            break;
        }
        src = ir1_get_opnd(insn, 2);
        merge = true;
        ymm = ir1_opnd_size(ir1_get_opnd(insn, 0)) == 256;
        src_bytes = ymm ? 256 : 128;
        merge_bytes = src_bytes;
        res_bytes = ymm ? 32 : 16;
        break;
    case X86_INS_VROUNDPS:
    case X86_INS_VROUNDPD:
        kind = opcode == X86_INS_VROUNDPS ? LATX_AVX_CVT_ROUNDPS :
                                            LATX_AVX_CVT_ROUNDPD;
        src = ir1_get_opnd(insn, 1);
        imm_idx = 2;
        ymm = ir1_opnd_size(ir1_get_opnd(insn, 0)) == 256;
        src_bytes = ymm ? 256 : 128;
        res_bytes = ymm ? 32 : 16;
        break;
    case X86_INS_VROUNDSS:
    case X86_INS_VROUNDSD:
        kind = opcode == X86_INS_VROUNDSS ? LATX_AVX_CVT_ROUNDSS :
                                            LATX_AVX_CVT_ROUNDSD;
        src = ir1_get_opnd(insn, 2);
        imm_idx = 3;
        merge = true;
        src_bytes = opcode == X86_INS_VROUNDSS ? 32 : 64;
        res_bytes = 16;
        break;
    case X86_INS_VSQRTPS:
    case X86_INS_VSQRTPD:
        kind = opcode == X86_INS_VSQRTPS ? LATX_AVX_CVT_SQRTPS :
                                           LATX_AVX_CVT_SQRTPD;
        src = ir1_get_opnd(insn, 1);
        ymm = ir1_opnd_size(ir1_get_opnd(insn, 0)) == 256;
        src_bytes = ymm ? 256 : 128;
        res_bytes = ymm ? 32 : 16;
        break;
    case X86_INS_VSQRTSS:
    case X86_INS_VSQRTSD:
        kind = opcode == X86_INS_VSQRTSS ? LATX_AVX_CVT_SQRTSS :
                                           LATX_AVX_CVT_SQRTSD;
        src = ir1_get_opnd(insn, 2);
        merge = true;
        src_bytes = opcode == X86_INS_VSQRTSS ? 32 : 64;
        res_bytes = 16;
        break;
    case X86_INS_VCVTSS2SI:
    case X86_INS_VCVTSD2SI:
    case X86_INS_VCVTTSS2SI:
    case X86_INS_VCVTTSD2SI:
        kind = opcode == X86_INS_VCVTSS2SI ? LATX_AVX_CVT_SS2SI :
               opcode == X86_INS_VCVTSD2SI ? LATX_AVX_CVT_SD2SI :
               opcode == X86_INS_VCVTTSS2SI ? LATX_AVX_CVT_TSS2SI :
                                              LATX_AVX_CVT_TSD2SI;
        src = ir1_get_opnd(insn, 1);
        gpr_dest = true;
        qword = ir1_opnd_size(ir1_get_opnd(insn, 0)) == 64;
        src_bytes = (opcode == X86_INS_VCVTSS2SI ||
                     opcode == X86_INS_VCVTTSS2SI) ? 32 : 64;
        break;
    default:
        return false;
    }

    if (avx_unavailable(insn, need_avx2)) {
        return true;
    }

    IR2_OPND fast_fail = latxs_ir2_opnd_new_label();
    IR2_OPND fast_done = latxs_ir2_opnd_new_label();
    bool fast = avx_scalar_fast_emit(insn, kind, qword, &fast_fail);
    if (!fast) {
        fast = avx_convert_fast_emit(insn, kind, qword, &fast_fail);
    }
    if (fast) {
        latxs_append_ir2_opnd1(LISA_B, &fast_done);
        latxs_append_ir2_opnd1(LISA_LABEL, &fast_fail);
    }

    IR2_OPND base = avx_cvt_scratch_base();

    if (merge) {
        /* The VEX.vvvv source provides the untouched destination bits. */
        avx_cvt_store(insn, ir1_get_opnd(insn, 1), &base, 0, merge_bytes);
    }
    if (gpr_src && !ir1_opnd_is_mem(src)) {
        IR2_OPND value = latxs_ra_alloc_itemp();

        latxs_load_ir1_to_ir2(&value, src, qword ? EXMode_N : EXMode_Z);
        latxs_append_ir2_opnd2i(qword ? LISA_ST_D : LISA_ST_W, &value, &base,
                                128);
        latxs_ra_free_temp(&value);
    } else {
        avx_cvt_store(insn, src, &base, 128, src_bytes);
    }
    if (imm_idx) {
        IR2_OPND imm = latxs_ra_alloc_itemp();

        latxs_load_imm64(&imm, ir1_opnd_uimm(ir1_get_opnd(insn, imm_idx)));
        latxs_append_ir2_opnd2i(LISA_ST_D, &imm, &base, 64);
        latxs_ra_free_temp(&imm);
    }
    latxs_ra_free_temp(&base);

    latxs_tr_gen_call_to_helper4_u64_cfg((ADDR)latxs_avx_fp, kind,
                                         ymm ? 1 : 0, qword,
                                         default_helper_cfg, HELPER_AVX_FP);
    avx_fp_commit_flags(insn);

    base = avx_cvt_scratch_base();
    if (mem_dest) {
        avx_cvt_store_mem(insn, &base, store_bytes);
    } else {
        avx_cvt_write_result(insn, &base, res_bytes, gpr_dest);
    }
    latxs_ra_free_temp(&base);
    if (fast) {
        latxs_append_ir2_opnd1(LISA_LABEL, &fast_done);
    }
    return true;
}

/* VMOVHLPS / VMOVLPS / VMOVHPS / VMOVLPD / VMOVHPD: 64-bit lane moves that
 * merge with the untouched 64-bit half of the destination. */
static bool avx_translate_move_lane(IR1_INST *insn)
{
    IR1_OPCODE opcode = ir1_opcode(insn);
    bool memory_high = false;
    bool register_high = false;
    bool register_low = false;

    switch (opcode) {
    case X86_INS_VMOVHLPS:
        register_high = true;
        memory_high = false;
        break;
    case X86_INS_VMOVLHPS:
        /* VEX.128.0F 16 /r with ModRM.mod == 11; QEMU also accepts m64 in
         * place of SRC2 (VMOVLHPS xmm1, xmm2, m64). */
        register_low = true;
        memory_high = false;
        break;
    case X86_INS_VMOVLPS:
    case X86_INS_VMOVLPD:
        register_high = false;
        memory_high = false;
        break;
    case X86_INS_VMOVHPS:
    case X86_INS_VMOVHPD:
        register_high = false;
        memory_high = true;
        break;
    default:
        return false;
    }

    if (avx_unavailable(insn, false)) {
        return true;
    }

    if (ir1_opnd_is_mem(ir1_get_opnd(insn, 0))) {
        /* VMOVLPS/VMOVLPD/VMOVHPS/VMOVHPD store: m64 <- xmm[low|high]. */
        int srcreg = ir1_opnd_base_reg_num(ir1_get_opnd(insn, 1));
        IR2_OPND vec = latxs_ra_alloc_xmm(srcreg);
        IR2_OPND word = latxs_ra_alloc_itemp();
        IR2_OPND addr = avx_addr(insn, ir1_get_opnd(insn, 0), 0);

        latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_D, &word, &vec,
                                memory_high ? 1 : 0);
        gen_ldst_softmmu_helper(LISA_ST_D, &word, &addr, 0, 1);
        latxs_ra_free_temp(&addr);
        latxs_ra_free_temp(&word);
        return true;
    }

    int reg = ir1_opnd_base_reg_num(ir1_get_opnd(insn, 0));
    IR2_OPND low = latxs_ra_alloc_xmm(reg);
    IR2_OPND value = latxs_ra_alloc_itemp();

    if (memory_high) {
        IR2_OPND src = latxs_ra_alloc_xmm(
            ir1_opnd_base_reg_num(ir1_get_opnd(insn, 1)));

        avx_copy(&low, &src);
        value = avx_load_scalar(insn, ir1_get_opnd(insn, 2), LISA_LD_D);
        latxs_append_ir2_opnd2i(LISA_VINSGR2VR_D, &low, &value, 1);
    } else if (register_low) {
        IR2_OPND src1 = latxs_ra_alloc_xmm(
            ir1_opnd_base_reg_num(ir1_get_opnd(insn, 1)));

        /* DEST[63:0] = SRC1[63:0]; DEST[127:64] = SRC2[63:0].  Both sources
         * are read before DEST is written, so aliasing (e.g. vmovlhps
         * %xmm1,%xmm1,%xmm1) stays correct. */
        latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_D, &value, &src1, 0);
        if (ir1_opnd_is_mem(ir1_get_opnd(insn, 2))) {
            IR2_OPND memval = avx_load_scalar(insn, ir1_get_opnd(insn, 2),
                                              LISA_LD_D);
            latxs_append_ir2_opnd2i(LISA_VINSGR2VR_D, &low, &value, 0);
            latxs_append_ir2_opnd2i(LISA_VINSGR2VR_D, &low, &memval, 1);
        } else {
            IR2_OPND src2 = latxs_ra_alloc_xmm(
                ir1_opnd_base_reg_num(ir1_get_opnd(insn, 2)));
            IR2_OPND value2 = latxs_ra_alloc_itemp();
            latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_D, &value2, &src2, 0);
            latxs_append_ir2_opnd2i(LISA_VINSGR2VR_D, &low, &value, 0);
            latxs_append_ir2_opnd2i(LISA_VINSGR2VR_D, &low, &value2, 1);
            latxs_ra_free_temp(&value2);
        }
    } else if (register_high) {
        IR2_OPND src1 = latxs_ra_alloc_xmm(
            ir1_opnd_base_reg_num(ir1_get_opnd(insn, 1)));
        IR2_OPND src2 = latxs_ra_alloc_xmm(
            ir1_opnd_base_reg_num(ir1_get_opnd(insn, 2)));

        latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_D, &value, &src2, 1);
        latxs_append_ir2_opnd2i(LISA_VINSGR2VR_D, &low, &value, 0);
        latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_D, &value, &src1, 1);
        latxs_append_ir2_opnd2i(LISA_VINSGR2VR_D, &low, &value, 1);
    } else if (ir1_opnd_is_mem(ir1_get_opnd(insn, 2))) {
        IR2_OPND src1 = latxs_ra_alloc_xmm(
            ir1_opnd_base_reg_num(ir1_get_opnd(insn, 1)));

        value = avx_load_scalar(insn, ir1_get_opnd(insn, 2), LISA_LD_D);
        latxs_append_ir2_opnd2i(LISA_VINSGR2VR_D, &low, &value, 0);
        latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_D, &value, &src1, 1);
        latxs_append_ir2_opnd2i(LISA_VINSGR2VR_D, &low, &value, 1);
    } else {
        IR2_OPND src1 = latxs_ra_alloc_xmm(
            ir1_opnd_base_reg_num(ir1_get_opnd(insn, 1)));
        IR2_OPND src2 = latxs_ra_alloc_xmm(
            ir1_opnd_base_reg_num(ir1_get_opnd(insn, 2)));

        avx_copy(&low, &src1);
        latxs_append_ir2_opnd2i(LISA_VPICKVE2GR_D, &value, &src2, 0);
        latxs_append_ir2_opnd2i(LISA_VINSGR2VR_D, &low, &value, 0);
    }
    avx_write_high(reg, NULL);
    latxs_ra_free_temp(&value);
    return true;
}

/* VEX scalar compare with a predicate immediate; the upper lane is taken
 * from the VEX.vvvv source, matching QEMU's scalar compare helpers. */
static bool avx_translate_scalar_cmp(IR1_INST *insn, unsigned pred, bool qword)
{
    if (avx_unavailable(insn, false)) {
        return true;
    }

    IR2_OPND base = avx_cvt_scratch_base();

    avx_cvt_store(insn, ir1_get_opnd(insn, 1), &base, 0, 128);
    avx_cvt_store(insn, ir1_get_opnd(insn, 2), &base, 128,
                  qword ? 64 : 32);
    latxs_ra_free_temp(&base);

    latxs_tr_gen_call_to_helper4_u64_cfg((ADDR)latxs_avx_fp,
                                         (qword ? LATX_AVX_CVT_CMPSD_BASE :
                                                  LATX_AVX_CVT_CMPSS_BASE) +
                                         pred, 0, qword, default_helper_cfg,
                                         HELPER_AVX_FP);
    avx_fp_commit_flags(insn);

    base = avx_cvt_scratch_base();
    avx_cvt_write_result(insn, &base, 16, false);
    latxs_ra_free_temp(&base);
    return true;
}

/* VEX variable blend: byte/dword/qword select from the mask's sign bit. */
static bool avx_translate_blendv(IR1_INST *insn)
{
    IR1_OPCODE opcode = ir1_opcode(insn);
    unsigned kind;

    switch (opcode) {
    case X86_INS_VPBLENDVB:
        kind = LATX_AVX_CVT_PBLENDVB;
        break;
    case X86_INS_VBLENDVPS:
        kind = LATX_AVX_CVT_BLENDVPS;
        break;
    case X86_INS_VBLENDVPD:
        kind = LATX_AVX_CVT_BLENDVPD;
        break;
    default:
        return false;
    }
    if (avx_unavailable(insn, false)) {
        return true;
    }

    bool ymm = ir1_opnd_size(ir1_get_opnd(insn, 0)) == 256;
    unsigned bytes = ymm ? 256 : 128;
    IR2_OPND base = avx_cvt_scratch_base();

    avx_cvt_store(insn, ir1_get_opnd(insn, 1), &base, 0, bytes);
    avx_cvt_store(insn, ir1_get_opnd(insn, 2), &base, 64, bytes);
    avx_cvt_store(insn, ir1_get_opnd(insn, 3), &base, 128, bytes);
    latxs_ra_free_temp(&base);

    latxs_tr_gen_call_to_helper4_u64_cfg((ADDR)latxs_avx_fp, kind,
                                         ymm ? 1 : 0, 0,
                                         default_helper_cfg, HELPER_AVX_FP);
    avx_fp_commit_flags(insn);

    base = avx_cvt_scratch_base();
    avx_cvt_write_result(insn, &base, ymm ? 32 : 16, false);
    latxs_ra_free_temp(&base);
    return true;
}

/* VEX-encoded scalar FMA: only the low element is updated, the high 64 bits
 * of the destination are preserved.  Values are staged in the conversion
 * scratch buffer and merged back with VINSGR2VR. */
static bool avx_translate_scalar_fma(IR1_INST *insn)
{
    IR1_OPCODE opcode = ir1_opcode(insn);
    LatxAvxFpOp kind = LATX_AVX_FP_SFMA;
    unsigned order = 0;
    bool qword = false;

#define SFMA_CASE(tok, ord, idx, w, opk, qw)                                \
    case X86_INS_V##tok##ord##w:                                            \
        kind = LATX_AVX_FP_##opk;                                           \
        order = idx;                                                        \
        qword = qw;                                                         \
        break

    switch (opcode) {
    SFMA_CASE(FMADD, 132, 0, SS, SFMA, false);
    SFMA_CASE(FMADD, 213, 1, SS, SFMA, false);
    SFMA_CASE(FMADD, 231, 2, SS, SFMA, false);
    SFMA_CASE(FMADD, 132, 0, SD, SFMA, true);
    SFMA_CASE(FMADD, 213, 1, SD, SFMA, true);
    SFMA_CASE(FMADD, 231, 2, SD, SFMA, true);
    SFMA_CASE(FMSUB, 132, 0, SS, SFMSUB, false);
    SFMA_CASE(FMSUB, 213, 1, SS, SFMSUB, false);
    SFMA_CASE(FMSUB, 231, 2, SS, SFMSUB, false);
    SFMA_CASE(FMSUB, 132, 0, SD, SFMSUB, true);
    SFMA_CASE(FMSUB, 213, 1, SD, SFMSUB, true);
    SFMA_CASE(FMSUB, 231, 2, SD, SFMSUB, true);
    SFMA_CASE(FNMADD, 132, 0, SS, SFNMADD, false);
    SFMA_CASE(FNMADD, 213, 1, SS, SFNMADD, false);
    SFMA_CASE(FNMADD, 231, 2, SS, SFNMADD, false);
    SFMA_CASE(FNMADD, 132, 0, SD, SFNMADD, true);
    SFMA_CASE(FNMADD, 213, 1, SD, SFNMADD, true);
    SFMA_CASE(FNMADD, 231, 2, SD, SFNMADD, true);
    SFMA_CASE(FNMSUB, 132, 0, SS, SFNMSUB, false);
    SFMA_CASE(FNMSUB, 213, 1, SS, SFNMSUB, false);
    SFMA_CASE(FNMSUB, 231, 2, SS, SFNMSUB, false);
    SFMA_CASE(FNMSUB, 132, 0, SD, SFNMSUB, true);
    SFMA_CASE(FNMSUB, 213, 1, SD, SFNMSUB, true);
    SFMA_CASE(FNMSUB, 231, 2, SD, SFNMSUB, true);
    default:
        return false;
    }
#undef SFMA_CASE

    if (avx_unavailable(insn, false)) {
        return true;
    }

    /* x86 orders: 132 = dest*src3 + src2, 213 = src2*dest + src3,
     * 231 = src2*src3 + dest.  The helper computes a*b +/- c. */
    static const unsigned fma_seq[][3] = {
        { 0, 2, 1 }, { 1, 0, 2 }, { 1, 2, 0 },
    };
    const unsigned *seq = fma_seq[order];
    IR2_OPND base = avx_cvt_scratch_base();

    for (unsigned i = 0; i < 3; i++) {
        IR1_OPND *op = ir1_get_opnd(insn, seq[i]);
        unsigned bytes = ir1_opnd_is_mem(op) ? ir1_opnd_size(op) : 16;

        avx_cvt_store(insn, op, &base, i * 64, bytes);
    }
    latxs_ra_free_temp(&base);

    latxs_tr_gen_call_to_helper4_u64_cfg((ADDR)latxs_avx_fp, kind, 1, qword,
                                         default_helper_cfg, HELPER_AVX_FP);
    avx_fp_commit_flags(insn);

    base = avx_cvt_scratch_base();
    IR2_OPND scalar = latxs_ra_alloc_itemp();

    latxs_append_ir2_opnd2i(qword ? LISA_LD_D : LISA_LD_WU, &scalar, &base,
                            192);
    int reg = ir1_opnd_base_reg_num(ir1_get_opnd(insn, 0));
    IR2_OPND low = latxs_ra_alloc_xmm(reg);

    latxs_append_ir2_opnd2i(qword ? LISA_VINSGR2VR_D : LISA_VINSGR2VR_W,
                            &low, &scalar, 0);
    avx_write_high(reg, NULL);
    latxs_ra_free_temp(&scalar);
    latxs_ra_free_temp(&base);
    return true;
}

#endif

bool latxs_translate_avx(IR1_INST *insn)
{
#if defined(CONFIG_SOFTMMU) && !defined(CONFIG_LATX_TCG_MODE)
    IR1_OPCODE opcode = ir1_opcode(insn);
    const uint8_t *prefix = insn->info->detail->x86.opcode;

    if (prefix[0] != 0xc4 && prefix[0] != 0xc5) {
        return false;
    }

    const AvxFpArithmetic *arith = &avx_fp_arithmetic_ops[opcode];

    if (arith->host_op) {
        if (avx_op_is_fma(arith->op) &&
            !(lsenv->tr_data->sys.cpuid_ext_features & CPUID_EXT_FMA)) {
            latxs_tr_gen_excp_illegal_op(insn, 1);
        } else if (!avx_unavailable(insn, false)) {
            avx_fp_arithmetic(insn, arith);
        }
        return true;
    }

    if (avx_translate_convert(insn)) {
        return true;
    }
    if (avx_translate_scalar_fma(insn)) {
        return true;
    }
    if (avx_translate_blendv(insn)) {
        return true;
    }
    if (avx_translate_move_lane(insn)) {
        return true;
    }
    if (opcode == X86_INS_VUCOMISD || opcode == X86_INS_VUCOMISS ||
        opcode == X86_INS_VCOMISD || opcode == X86_INS_VCOMISS) {
        if (!avx_unavailable(insn, false)) {
            switch (opcode) {
            case X86_INS_VUCOMISD:
                return latxs_translate_ucomisd(insn);
            case X86_INS_VUCOMISS:
                return latxs_translate_ucomiss(insn);
            case X86_INS_VCOMISD:
                return latxs_translate_comisd(insn);
            default:
                return latxs_translate_comiss(insn);
            }
        }
        return true;
    }

    bool cmp_ps = opcode >= X86_INS_VCMPPS && opcode <= X86_INS_VCMPTRUE_USPS;
    bool cmp_pd = opcode >= X86_INS_VCMPPD && opcode <= X86_INS_VCMPTRUE_USPD;
    if (cmp_ps || cmp_pd) {
        if (!avx_unavailable(insn, false)) {
            unsigned base = cmp_pd ? X86_INS_VCMPPD : X86_INS_VCMPPS;
            /* Capstone gives the 32 named predicates consecutive IDs. */
            unsigned imm = opcode == base ?
                ir1_opnd_uimm(ir1_get_opnd(insn, 3)) & 31 : opcode - base - 1;
            bool ymm = ir1_opnd_size(ir1_get_opnd(insn, 0)) == 256;

            avx_fp_compare(insn, ymm, cmp_pd, &avx_float_predicates[imm], false);
        }
        return true;
    }

    switch (opcode) {
    case X86_INS_VZEROALL:
    case X86_INS_VZEROUPPER:
        if (!avx_unavailable(insn, false)) {
            avx_zero(opcode == X86_INS_VZEROALL);
        }
        return true;
    case X86_INS_VMOVAPD:
    case X86_INS_VMOVAPS:
    case X86_INS_VMOVDQA:
    case X86_INS_VMOVDQU:
    case X86_INS_VMOVUPD:
    case X86_INS_VMOVUPS:
    case X86_INS_VLDDQU:
        if (!avx_unavailable(insn, false)) {
            IR1_OPND *dst = ir1_get_opnd(insn, 0);
            bool aligned = opcode == X86_INS_VMOVAPD ||
                           opcode == X86_INS_VMOVAPS ||
                           opcode == X86_INS_VMOVDQA;
            avx_move(insn, dst, ir1_opnd_size(dst) == 256, aligned);
        }
        return true;
    case X86_INS_VMOVD:
    case X86_INS_VMOVQ:
    case X86_INS_VMOVSS:
    case X86_INS_VMOVSD:
        if (!avx_unavailable(insn, false)) {
            avx_move_scalar(insn, opcode == X86_INS_VMOVQ ||
                                  opcode == X86_INS_VMOVSD);
        }
        return true;
    case X86_INS_VHADDPD:
    case X86_INS_VHADDPS:
        if (!avx_unavailable(insn, false)) {
            avx_horizontal_fp(insn, opcode == X86_INS_VHADDPD, false);
        }
        return true;
    case X86_INS_VHSUBPD:
    case X86_INS_VHSUBPS:
        if (!avx_unavailable(insn, false)) {
            avx_horizontal_fp(insn, opcode == X86_INS_VHSUBPD, true);
        }
        return true;
    case X86_INS_VLDMXCSR:
    case X86_INS_VSTMXCSR:
        /* VEX.128.0F.WIG AE /2,/3: memory-only forms, VEX.L must be 0. */
        if (prefix[prefix[0] == 0xc4 ? 2 : 1] & 0x04) {
            latxs_tr_gen_excp_illegal_op(insn, 1);
        } else if (!avx_unavailable(insn, false)) {
            return opcode == X86_INS_VSTMXCSR ?
                   latxs_translate_stmxcsr(insn) :
                   latxs_translate_ldmxcsr(insn);
        }
        return true;
    case X86_INS_VPMOVMSKB:
    case X86_INS_VMOVMSKPS:
    case X86_INS_VMOVMSKPD: {
        bool ymm = ir1_opnd_size(ir1_get_opnd(insn, 1)) == 256;

        if (!avx_unavailable(insn, ymm && opcode == X86_INS_VPMOVMSKB)) {
            avx_movemask(insn, ymm);
        }
        return true;
    }
    case X86_INS_VPTEST:
    case X86_INS_VTESTPS:
    case X86_INS_VTESTPD:
        if (!avx_unavailable(insn, false)) {
            avx_test(insn, ir1_opnd_size(ir1_get_opnd(insn, 0)) == 256);
        }
        return true;
    case X86_INS_VMINPS:
    case X86_INS_VMINPD:
    case X86_INS_VMAXPS:
    case X86_INS_VMAXPD:
        if (!avx_unavailable(insn, false)) {
            AvxFloatCompare predicate = {
                .condition = FCMP_COND_SLT,
                .reverse = opcode == X86_INS_VMAXPS || opcode == X86_INS_VMAXPD,
            };
            bool ymm = ir1_opnd_size(ir1_get_opnd(insn, 0)) == 256;
            bool qword = opcode == X86_INS_VMINPD || opcode == X86_INS_VMAXPD;

            avx_fp_compare(insn, ymm, qword, &predicate, true);
        }
        return true;
    case X86_INS_VMASKMOVPS:
    case X86_INS_VMASKMOVPD:
    case X86_INS_VPMASKMOVD:
    case X86_INS_VPMASKMOVQ:
        if (!avx_unavailable(insn, opcode == X86_INS_VPMASKMOVD ||
                                  opcode == X86_INS_VPMASKMOVQ)) {
            avx_masked_move(insn, opcode == X86_INS_VMASKMOVPD ||
                                  opcode == X86_INS_VPMASKMOVQ);
        }
        return true;
    case X86_INS_VPERM2F128:
    case X86_INS_VPERM2I128:
        if (!avx_unavailable(insn, opcode == X86_INS_VPERM2I128)) {
            avx_permute_halves(insn);
        }
        return true;
    case X86_INS_VPERMD:
    case X86_INS_VPERMPS:
    case X86_INS_VPERMPD:
    case X86_INS_VPERMQ:
        if (!avx_unavailable(insn, true)) {
            avx_permute_elements(insn);
        }
        return true;
    case X86_INS_VPERMILPS:
    case X86_INS_VPERMILPD:
        if (!avx_unavailable(insn, false)) {
            bool ymm = ir1_opnd_size(ir1_get_opnd(insn, 0)) == 256;

            avx_permil(insn, ymm, opcode == X86_INS_VPERMILPD);
        }
        return true;
    case X86_INS_VINSERTF128:
    case X86_INS_VINSERTI128:
        if (!avx_unavailable(insn, opcode == X86_INS_VINSERTI128)) {
            avx_insert_half(insn);
        }
        return true;
    case X86_INS_VEXTRACTF128:
    case X86_INS_VEXTRACTI128:
        if (!avx_unavailable(insn, opcode == X86_INS_VEXTRACTI128)) {
            avx_extract_half(insn);
        }
        return true;
    case X86_INS_VINSERTPS:
    case X86_INS_VPINSRB:
    case X86_INS_VPINSRW:
    case X86_INS_VPINSRD:
    case X86_INS_VPINSRQ:
        if (!avx_unavailable(insn, false)) {
            avx_insert_element(insn);
        }
        return true;
    case X86_INS_VEXTRACTPS:
    case X86_INS_VPEXTRB:
    case X86_INS_VPEXTRW:
    case X86_INS_VPEXTRD:
    case X86_INS_VPEXTRQ:
        if (!avx_unavailable(insn, false)) {
            avx_extract_element(insn);
        }
        return true;
    case X86_INS_VBROADCASTF128:
    case X86_INS_VBROADCASTI128:
        if (!avx_unavailable(insn, opcode == X86_INS_VBROADCASTI128)) {
            IR1_OPND *src = ir1_get_opnd(insn, 1);
            AvxOperand value = avx_load(insn, src, false, false);
            int reg = ir1_opnd_base_reg_num(ir1_get_opnd(insn, 0));
            IR2_OPND low = latxs_ra_alloc_xmm(reg);

            avx_copy(&low, &value.low);
            avx_write_high(reg, &low);
        }
        return true;
    case X86_INS_VPBLENDD:
    case X86_INS_VBLENDPS:
        if (!avx_unavailable(insn, opcode == X86_INS_VPBLENDD)) {
            bool ymm = ir1_opnd_size(ir1_get_opnd(insn, 0)) == 256;
            avx_blend_dwords(insn, ymm);
        }
        return true;
    case X86_INS_VBLENDPD:
        if (!avx_unavailable(insn, false)) {
            bool ymm = ir1_opnd_size(ir1_get_opnd(insn, 0)) == 256;
            avx_blend_qwords(insn, ymm);
        }
        return true;
    case X86_INS_VAESDEC:
    case X86_INS_VAESDECLAST:
    case X86_INS_VAESENC:
    case X86_INS_VAESENCLAST:
    case X86_INS_VAESIMC:
    case X86_INS_VAESKEYGENASSIST:
    case X86_INS_VPCLMULQDQ:
        avx_crypto128(insn);
        return true;
    case X86_INS_VPSHUFD: {
        bool ymm = ir1_opnd_size(ir1_get_opnd(insn, 0)) == 256;
        if (!avx_unavailable(insn, ymm)) {
            avx_shuffle_dwords(insn, ymm);
        }
        return true;
    }
    case X86_INS_VSHUFPS:
    case X86_INS_VSHUFPD:
        if (!avx_unavailable(insn, false)) {
            bool ymm = ir1_opnd_size(ir1_get_opnd(insn, 0)) == 256;
            avx_shuffle_packed(insn, ymm, opcode == X86_INS_VSHUFPD);
        }
        return true;
    case X86_INS_VPMADDWD: {
        bool ymm = ir1_opnd_size(ir1_get_opnd(insn, 0)) == 256;

        if (!avx_unavailable(insn, ymm)) {
            avx_madd_words(insn, ymm);
        }
        return true;
    }
    case X86_INS_VPMADDUBSW: {
        bool ymm = ir1_opnd_size(ir1_get_opnd(insn, 0)) == 256;

        if (!avx_unavailable(insn, ymm)) {
            avx_maddubsw(insn, ymm);
        }
        return true;
    }
    case X86_INS_VPSIGNB:
    case X86_INS_VPSIGNW:
    case X86_INS_VPSIGND: {
        bool ymm = ir1_opnd_size(ir1_get_opnd(insn, 0)) == 256;
        IR2_OPCODE op = opcode == X86_INS_VPSIGNB ? LISA_VSIGNCOV_B :
                        opcode == X86_INS_VPSIGNW ? LISA_VSIGNCOV_H :
                                                    LISA_VSIGNCOV_W;

        if (!avx_unavailable(insn, ymm)) {
            avx_sign(insn, ymm, op);
        }
        return true;
    }
    case X86_INS_VPHMINPOSUW:
        /* VEX.128.66.0F38.WIG 41 /r: VEX.vvvv reserved, VEX.L must be 0. */
        if (prefix[prefix[0] == 0xc4 ? 2 : 1] & 0x04) {
            latxs_tr_gen_excp_illegal_op(insn, 1);
        } else if (!avx_unavailable(insn, false)) {
            avx_phminposuw(insn);
        }
        return true;
    case X86_INS_VPSADBW: {
        bool ymm = ir1_opnd_size(ir1_get_opnd(insn, 0)) == 256;

        if (!avx_unavailable(insn, ymm)) {
            avx_sad_bytes(insn, ymm);
        }
        return true;
    }
    case X86_INS_VPSHUFB:
    case X86_INS_VPALIGNR: {
        bool ymm = ir1_opnd_size(ir1_get_opnd(insn, 0)) == 256;
        if (!avx_unavailable(insn, ymm)) {
            if (opcode == X86_INS_VPSHUFB) {
                avx_shuffle_bytes(insn, ymm);
            } else {
                avx_align_bytes(insn, ymm);
            }
        }
        return true;
    }
    case X86_INS_VPABSB:
    case X86_INS_VPABSW:
    case X86_INS_VPABSD: {
        bool ymm = ir1_opnd_size(ir1_get_opnd(insn, 0)) == 256;
        IR2_OPCODE op = opcode == X86_INS_VPABSB ? LISA_VABSD_B :
                        opcode == X86_INS_VPABSW ? LISA_VABSD_H : LISA_VABSD_W;
        if (!avx_unavailable(insn, ymm)) {
            avx_abs(insn, ymm, op);
        }
        return true;
    }
    default:
        break;
    }

    const AvxBinaryOp *bin = &avx_binary_ops[opcode];
    if (bin->op) {
        IR1_OPND *dst = ir1_get_opnd(insn, 0);
        bool ymm = ir1_opnd_size(dst) == 256;
        if (!avx_unavailable(insn, ymm && bin->avx2_ymm)) {
            avx_binary(insn, dst, ymm, bin->op);
        }
        return true;
    }
    const AvxShift *shift = &avx_shifts[opcode];
    if (shift->immediate) {
        bool immediate = ir1_opnd_is_imm(ir1_get_opnd(insn, 2));
        bool ymm = ir1_opnd_size(ir1_get_opnd(insn, 0)) == 256;
        if (!immediate && !shift->uniform) {
            return false;
        }
        if (!avx_unavailable(insn, ymm)) {
            if (immediate) {
                avx_shift_immediate(insn, ymm, shift);
            } else {
                avx_shift_uniform(insn, ymm, shift);
            }
        }
        return true;
    }
    IR2_OPCODE variable_shift = avx_variable_shifts[opcode];
    if (variable_shift) {
        bool ymm = ir1_opnd_size(ir1_get_opnd(insn, 0)) == 256;
        if (!avx_unavailable(insn, true)) {
            avx_shift_variable(insn, ymm, variable_shift);
        }
        return true;
    }
    IR2_OPCODE pack = avx_packs[opcode];
    if (pack) {
        bool ymm = ir1_opnd_size(ir1_get_opnd(insn, 0)) == 256;
        if (!avx_unavailable(insn, ymm)) {
            avx_pack(insn, ymm, pack);
        }
        return true;
    }
    const AvxBroadcast *broadcast = &avx_broadcasts[opcode];
    if (broadcast->load) {
        bool ymm = ir1_opnd_size(ir1_get_opnd(insn, 0)) == 256;
        bool reg = !ir1_opnd_is_mem(ir1_get_opnd(insn, 1));
        if (!avx_unavailable(insn, broadcast->avx2 || reg)) {
            avx_broadcast(insn, ymm, broadcast);
        }
        return true;
    }
    const AvxWiden *widen = &avx_widens[opcode];
    if (widen->to) {
        bool ymm = ir1_opnd_size(ir1_get_opnd(insn, 0)) == 256;
        if (!avx_unavailable(insn, ymm)) {
            avx_widen(insn, ymm, widen);
        }
        return true;
    }
#endif
    return false;
}
