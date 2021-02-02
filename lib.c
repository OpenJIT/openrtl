#include <stdlib.h>
#include <string.h>
#include "include/openrtl.h"

#define DEFAULT_CONTEXT_CAP 32
#define DEFAULT_TABLE_CAP 32
#define DEFAULT_BUFFER_CAP 1024
#define DEFAULT_MATRIX_CAP 256

static int openrtl_none(OpenrtlBuffer *buf, uint8_t opcode);
static int openrtl_arith(OpenrtlBuffer *buf, uint8_t opcode, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2);
static int openrtl_arith_b(OpenrtlBuffer *buf, uint8_t opcode, uint8_t size, uint8_t dest, uint8_t src, uint8_t size2);
static int openrtl_imm(OpenrtlBuffer *buf, uint8_t opcode, uint32_t value);
static int openrtl_rel(OpenrtlBuffer *buf, uint8_t opcode, uint8_t size, uint8_t dest, uint64_t value);
static int openrtl_append(OpenrtlBuffer *buf, struct OpenrtlElement *elem);

void openrtl_context(OpenrtlContext *ctx) {
    ctx->cap = DEFAULT_CONTEXT_CAP;
    ctx->len = 0;
    ctx->ptr = malloc(ctx->cap * sizeof(OpenrtlBuffer));
    ctx->global.cap = DEFAULT_TABLE_CAP;
    ctx->global.len = 0;
    ctx->global.ptr = malloc(ctx->global.cap * sizeof(struct OpenrtlEntry));
}

void openrtl_del_context(OpenrtlContext *ctx) {
    for (size_t i = 0; i < ctx->len; i++) {
        openrtl_del_buffer(ctx->ptr + i);
    }
    for (size_t i = 0; i < ctx->global.len; i++) {
        free((char *) ctx->global.ptr[i].name);
    }
    free(ctx->ptr);
    free(ctx->global.ptr);
}

void openrtl_add_buffer(OpenrtlContext *ctx, const char *name, OpenrtlBuffer *buf) {
    if (ctx->global.len == ctx->global.cap) {
        ctx->global.cap *= 2;
        ctx->global.ptr = realloc(ctx->global.ptr, ctx->global.cap);
    }

    ctx->global.ptr[ctx->global.len].name = malloc(strlen(name) + 1);
    strcpy((char *) ctx->global.ptr[ctx->global.len].name, name);
    ctx->global.ptr[ctx->global.len++].addr = ctx->len;

    if (ctx->len == ctx->cap) {
        ctx->cap *= 2;
        ctx->ptr = realloc(ctx->ptr, ctx->cap);
    }

    ctx->ptr[ctx->len++] = *buf;
}

void openrtl_global(OpenrtlContext *ctx, const char *name, uint64_t addr) {
    if (ctx->global.len == ctx->global.cap) {
        ctx->global.cap *= 2;
        ctx->global.ptr = realloc(ctx->global.ptr, ctx->global.cap);
    }

    ctx->global.ptr[ctx->global.len].name = malloc(strlen(name) + 1);
    strcpy((char *) ctx->global.ptr[ctx->global.len].name, name);
    ctx->global.ptr[ctx->global.len++].addr = addr;
}

void openrtl_link(OpenrtlContext *ctx) {
    for (size_t i = 0; i < ctx->len; i++) {
        OpenrtlBuffer *buf = ctx->ptr + i;
        for (size_t j = 0; j < buf->linker.len; j++) {
            struct OpenrtlSymbol *sym = buf->linker.ptr + j;
            if (sym->type == OPENRTL_SYMBOL_GLOBAL) {
                for (size_t k = 0; k < ctx->global.len; k++) {
                    struct OpenrtlEntry *ent = ctx->global.ptr + k;
                    if (strcmp(sym->name, ent->name) == 0) {
                        sym->address = ent->addr;
                        uint64_t current = 0;
                        memcpy(&current, (char *) buf->ptr + sym->offset, sizeof(uint64_t));
                        current &= ~sym->mask;
                        current |= sym->address & sym->mask;
                        memcpy((char *) buf->ptr + sym->offset, &current, sizeof(uint64_t));
                    }
                }
            } else {
                for (size_t k = 0; k < buf->local.len; k++) {
                    struct OpenrtlEntry *ent = buf->local.ptr + k;
                    if (strcmp(sym->name, ent->name) == 0) {
                        sym->address = ent->addr;
                        uint64_t current = 0;
                        memcpy(&current, (char *) buf->ptr + sym->offset, sizeof(uint64_t));
                        current &= ~sym->mask;
                        current |= sym->address & sym->mask;
                        memcpy((char *) buf->ptr + sym->offset, &current, sizeof(uint64_t));
                    }
                }
            }
        }
    }
}

void openrtl_buffer(OpenrtlBuffer *buf) {
    buf->cap = DEFAULT_BUFFER_CAP;
    buf->len = 0;
    buf->ptr = malloc(buf->cap);
    buf->matrix.cap = DEFAULT_MATRIX_CAP;
    buf->matrix.len = 0;
    buf->matrix.ptr = malloc(buf->matrix.cap * sizeof(struct OpenrtlElement));
    buf->local.cap = DEFAULT_TABLE_CAP;
    buf->local.len = 0;
    buf->local.ptr = malloc(buf->local.cap * sizeof(struct OpenrtlEntry));
}

void openrtl_del_buffer(OpenrtlBuffer *buf) {
    free(buf->ptr);
    free(buf->matrix.ptr);
}

void openrtl_local(OpenrtlBuffer *buf, const char *name, uint64_t addr) {
    if (buf->local.len == buf->local.cap) {
        buf->local.cap *= 2;
        buf->local.ptr = realloc(buf->local.ptr, buf->local.cap);
    }

    buf->local.ptr[buf->local.len].name = malloc(strlen(name) + 1);
    strcpy((char *) buf->local.ptr[buf->local.len].name, name);
    buf->local.ptr[buf->local.len++].addr = addr;
}

void openrtl_symbol(OpenrtlBuffer *buf, int type, const char *name) {
    if (buf->linker.len == buf->linker.cap) {
        buf->linker.cap *= 2;
        buf->linker.ptr = realloc(buf->linker.ptr, buf->linker.cap);
    }

    buf->linker.ptr[buf->linker.len].type = type;
    buf->linker.ptr[buf->linker.len].name = malloc(strlen(name) + 1);
    strcpy((char *) buf->linker.ptr[buf->linker.len].name, name);
    buf->linker.ptr[buf->linker.len].mask = 0xffffffffffffffff;
    buf->linker.ptr[buf->linker.len].offset = buf->len + 4;
    buf->linker.ptr[buf->linker.len].address = 0;
    ++buf->linker.len;
}

int openrtl_return(OpenrtlBuffer *buf) {
    return openrtl_none(buf, OPENRTL_OP_RETURN);
}

int openrtl_enter(OpenrtlBuffer *buf, uint32_t imm) {
    return openrtl_imm(buf, OPENRTL_OP_ENTER, imm);
}

int openrtl_leave(OpenrtlBuffer *buf, uint32_t imm) {
    return openrtl_imm(buf, OPENRTL_OP_LEAVE, imm);
}

int openrtl_call(OpenrtlBuffer *buf, uint64_t addr) {
    return openrtl_rel(buf, OPENRTL_OP_CALL, 0, 0, addr);
}

int openrtl_call_indirect(OpenrtlBuffer *buf, uint8_t dest) {
    return openrtl_rel(buf, OPENRTL_OP_CALL_INDIRECT, dest, OPENRTL_ISIZE_64, 0);
}

int openrtl_branch(OpenrtlBuffer *buf, uint64_t addr) {
    return openrtl_rel(buf, OPENRTL_OP_BRANCH, 0, 0, addr);
}

int openrtl_branch_carry(OpenrtlBuffer *buf, uint64_t addr) {
    return openrtl_rel(buf, OPENRTL_OP_BRANCH_CARRY, 0, 0, addr);
}

int openrtl_branch_overflow(OpenrtlBuffer *buf, uint64_t addr) {
    return openrtl_rel(buf, OPENRTL_OP_BRANCH_OVERFLOW, 0, 0, addr);
}

int openrtl_branch_equal(OpenrtlBuffer *buf, uint64_t addr) {
    return openrtl_rel(buf, OPENRTL_OP_BRANCH_EQUAL, 0, 0, addr);
}

int openrtl_branch_not_equal(OpenrtlBuffer *buf, uint64_t addr) {
    return openrtl_rel(buf, OPENRTL_OP_BRANCH_NOT_EQUAL, 0, 0, addr);
}

int openrtl_branch_less(OpenrtlBuffer *buf, uint64_t addr) {
    return openrtl_rel(buf, OPENRTL_OP_BRANCH_LESS, 0, 0, addr);
}

int openrtl_branch_less_eq(OpenrtlBuffer *buf, uint64_t addr) {
    return openrtl_rel(buf, OPENRTL_OP_BRANCH_LESS_EQ, 0, 0, addr);
}

int openrtl_branch_greater(OpenrtlBuffer *buf, uint64_t addr) {
    return openrtl_rel(buf, OPENRTL_OP_BRANCH_GREATER, 0, 0, addr);
}

int openrtl_branch_greater_eq(OpenrtlBuffer *buf, uint64_t addr) {
    return openrtl_rel(buf, OPENRTL_OP_BRANCH_GREATER_EQ, 0, 0, addr);
}


int openrtl_iadd(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2) {
    return openrtl_arith(buf, OPENRTL_OP_IADD, size, dest, src1, src2);
}

int openrtl_iadd_carry(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2) {
    return openrtl_arith(buf, OPENRTL_OP_IADD_CARRY, size, dest, src1, src2);
}

int openrtl_iand(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2) {
    return openrtl_arith(buf, OPENRTL_OP_IAND, size, dest, src1, src2);
}

int openrtl_ior(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2) {
    return openrtl_arith(buf, OPENRTL_OP_IOR, size, dest, src1, src2);
}

int openrtl_ixor(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2) {
    return openrtl_arith(buf, OPENRTL_OP_IXOR, size, dest, src1, src2);
}

int openrtl_isubtract(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2) {
    return openrtl_arith(buf, OPENRTL_OP_ISUBTRACT, size, dest, src1, src2);
}

int openrtl_icompare(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2) {
    return openrtl_arith(buf, OPENRTL_OP_ICOMPARE, size, dest, src1, src2);
}

int openrtl_imultiply_unsigned(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2) {
    return openrtl_arith(buf, OPENRTL_OP_IMULTIPLY_UNSIGNED, size, dest, src1, src2);
}

int openrtl_imultiply_signed(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2) {
    return openrtl_arith(buf, OPENRTL_OP_IMULTIPLY_SIGNED, size, dest, src1, src2);
}

int openrtl_idivide_unsigned(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2) {
    return openrtl_arith(buf, OPENRTL_OP_IDIVIDE_UNSIGNED, size, dest, src1, src2);
}

int openrtl_idivide_signed(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2) {
    return openrtl_arith(buf, OPENRTL_OP_IDIVIDE_SIGNED, size, dest, src1, src2);
}

int openrtl_imodulo_unsigned(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2) {
    return openrtl_arith(buf, OPENRTL_OP_IMODULO_UNSIGNED, size, dest, src1, src2);
}

int openrtl_imodulo_signed(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2) {
    return openrtl_arith(buf, OPENRTL_OP_IMODULO_SIGNED, size, dest, src1, src2);
}

int openrtl_imove_immediate(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint64_t imm) {
    int status = 0;
    status |= openrtl_rel(buf, OPENRTL_OP_IMOVE_IMMEDIATE, size, dest, imm);

    struct OpenrtlElement elem;
    elem.offset = buf->len;
    elem.place = OPENRTL_GP_REGISTER;
    elem.value = OPENRTL_IMMEDIATE;
    elem.v1.general.reg = dest;
    elem.v1.general.size = size;
    elem.v2.immediate = imm;
    status |= openrtl_append(buf, &elem);

    return status;
}

int openrtl_imove_unsigned(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src, uint8_t size2) {
    int status = 0;
    status |= openrtl_arith_b(buf, OPENRTL_OP_IMOVE_UNSIGNED, size, dest, src, size2);

    struct OpenrtlElement elem;
    elem.offset = buf->len;
    elem.place = OPENRTL_GP_REGISTER;
    elem.value = OPENRTL_GP_REGISTER;
    elem.v1.general.reg = dest;
    elem.v1.general.size = size;
    elem.v2.general.reg = src;
    elem.v2.general.size = size2;
    elem.v2.general.ext = 0;
    status |= openrtl_append(buf, &elem);

    return status;
}

int openrtl_imove_signed(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src, uint8_t size2) {
    int status = 0;
    status |= openrtl_arith_b(buf, OPENRTL_OP_IMOVE_SIGNED, size, dest, src, size2);

    struct OpenrtlElement elem;
    elem.offset = buf->len;
    elem.place = OPENRTL_GP_REGISTER;
    elem.value = OPENRTL_GP_REGISTER;
    elem.v1.general.reg = dest;
    elem.v1.general.size = size;
    elem.v2.general.reg = src;
    elem.v2.general.size = size2;
    elem.v2.general.ext = 1;
    status |= openrtl_append(buf, &elem);

    return status;
}

int openrtl_iload(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2) {
    int status = 0;
    status |= openrtl_arith(buf, OPENRTL_OP_ILOAD, size, dest, src1, src2);

    struct OpenrtlElement elem;
    elem.offset = buf->len;
    elem.place = OPENRTL_GP_REGISTER;
    elem.value = OPENRTL_MEMORY_INDIRECT;
    elem.v1.general.reg = dest;
    elem.v1.general.size = size;
    elem.v2.addri.base = src1;
    elem.v2.addri.offset = src2;
    status |= openrtl_append(buf, &elem);

    return status;
}

int openrtl_istore(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2) {
    int status = 0;
    status |= openrtl_arith(buf, OPENRTL_OP_ISTORE, size, dest, src1, src2);

    struct OpenrtlElement elem;
    elem.offset = buf->len;
    elem.place = OPENRTL_MEMORY_INDIRECT;
    elem.value = OPENRTL_GP_REGISTER;
    elem.v1.addri.base = src1;
    elem.v1.addri.offset = src2;
    elem.v2.general.reg = dest;
    elem.v2.general.size = size;
    status |= openrtl_append(buf, &elem);

    return status;
}

int openrtl_ipop(OpenrtlBuffer *buf, uint8_t dest) {
    int status = 0;
    status |= openrtl_arith(buf, OPENRTL_OP_IPOP, OPENRTL_ISIZE_64, dest, 0, 0);

    struct OpenrtlElement elem;
    elem.offset = buf->len;
    elem.place = OPENRTL_GP_REGISTER;
    elem.value = OPENRTL_MEMORY_BASE;
    elem.v1.general.reg = dest;
    elem.v1.general.size = OPENRTL_ISIZE_64;
    elem.v2.addr.base = OPENRTL_RSP;
    elem.v2.addr.offset = -8;
    status |= openrtl_append(buf, &elem);

    return status;
}

int openrtl_ipush(OpenrtlBuffer *buf, uint8_t src) {
    int status = 0;
    status |= openrtl_arith(buf, OPENRTL_OP_IPUSH, OPENRTL_ISIZE_64, src, 0, 0);

    struct OpenrtlElement elem;
    elem.offset = buf->len;
    elem.place = OPENRTL_MEMORY_INDIRECT;
    elem.value = OPENRTL_GP_REGISTER;
    elem.v1.addr.base = OPENRTL_RSP;
    elem.v1.addr.offset = 0;
    elem.v2.general.reg = src;
    elem.v2.general.size = OPENRTL_ISIZE_64;
    status |= openrtl_append(buf, &elem);

    return status;
}


int openrtl_fadd(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2) {
    return openrtl_arith(buf, OPENRTL_OP_FADD, size, dest, src1, src2);
}

int openrtl_fsubtract(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2) {
    return openrtl_arith(buf, OPENRTL_OP_FSUBTRACT, size, dest, src1, src2);
}

int openrtl_fcompare(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2) {
    return openrtl_arith(buf, OPENRTL_OP_FCOMPARE, size, dest, src1, src2);
}

int openrtl_fmultiply(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2) {
    return openrtl_arith(buf, OPENRTL_OP_FMULTIPLY, size, dest, src1, src2);
}

int openrtl_fdivide(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2) {
    return openrtl_arith(buf, OPENRTL_OP_FDIVIDE, size, dest, src1, src2);
}

int openrtl_fmove(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src) {
    int status = 0;
    status |= openrtl_arith(buf, OPENRTL_OP_FMOVE, size, dest, src, 0);

    struct OpenrtlElement elem;
    elem.offset = buf->len;
    elem.place = OPENRTL_FP_REGISTER;
    elem.value = OPENRTL_FP_REGISTER;
    elem.v1.general.reg = dest;
    elem.v1.general.size = size;
    elem.v2.general.reg = src;
    elem.v2.general.size = size;
    elem.v2.general.ext = 1;
    status |= openrtl_append(buf, &elem);

    return status;
}

int openrtl_fload(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2) {
    int status = 0;
    status |= openrtl_arith(buf, OPENRTL_OP_FLOAD, size, dest, src1, src2);

    struct OpenrtlElement elem;
    elem.offset = buf->len;
    elem.place = OPENRTL_FP_REGISTER;
    elem.value = OPENRTL_MEMORY_INDIRECT;
    elem.v1.general.reg = dest;
    elem.v1.general.size = size;
    elem.v2.addri.base = src1;
    elem.v2.addri.offset = src2;
    status |= openrtl_append(buf, &elem);

    return status;
}

int openrtl_fstore(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2) {
    int status = 0;
    status |= openrtl_arith(buf, OPENRTL_OP_FSTORE, size, dest, src1, src2);

    struct OpenrtlElement elem;
    elem.offset = buf->len;
    elem.place = OPENRTL_MEMORY_INDIRECT;
    elem.value = OPENRTL_FP_REGISTER;
    elem.v1.addri.base = src1;
    elem.v1.addri.offset = src2;
    elem.v2.general.reg = dest;
    elem.v2.general.size = size;
    status |= openrtl_append(buf, &elem);

    return status;
}

int openrtl_fpop(OpenrtlBuffer *buf, uint8_t dest) {
    int status = 0;
    status |= openrtl_arith(buf, OPENRTL_OP_FPOP, OPENRTL_FSIZE_64, dest, 0, 0);

    struct OpenrtlElement elem;
    elem.offset = buf->len;
    elem.place = OPENRTL_FP_REGISTER;
    elem.value = OPENRTL_MEMORY_BASE;
    elem.v1.general.reg = dest;
    elem.v1.general.size = OPENRTL_FSIZE_64;
    elem.v2.addr.base = OPENRTL_RSP;
    elem.v2.addr.offset = -8;
    status |= openrtl_append(buf, &elem);

    return status;
}

int openrtl_fpush(OpenrtlBuffer *buf, uint8_t src) {
    int status = 0;
    status |= openrtl_arith(buf, OPENRTL_OP_FPUSH, OPENRTL_FSIZE_64, src, 0, 0);

    struct OpenrtlElement elem;
    elem.offset = buf->len;
    elem.place = OPENRTL_MEMORY_INDIRECT;
    elem.value = OPENRTL_FP_REGISTER;
    elem.v1.addr.base = OPENRTL_RSP;
    elem.v1.addr.offset = 0;
    elem.v2.general.reg = src;
    elem.v2.general.size = OPENRTL_FSIZE_64;
    status |= openrtl_append(buf, &elem);

    return status;
}

int openrtl_f2i(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src, uint8_t size2) {
    return openrtl_arith_b(buf, OPENRTL_OP_F2I, size, dest, src, size2);
}

int openrtl_i2f(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src, uint8_t size2) {
    return openrtl_arith_b(buf, OPENRTL_OP_I2F, size, dest, src, size2);
}

int openrtl_extend(OpenrtlBuffer *buf, uint8_t dest) {
    return openrtl_arith(buf, OPENRTL_OP_EXTEND, OPENRTL_FSIZE_32, dest, 0, 0);
}

int openrtl_truncate(OpenrtlBuffer *buf, uint8_t dest) {
    return openrtl_arith(buf, OPENRTL_OP_TRUNCATE, OPENRTL_FSIZE_64, dest, 0, 0);
}

int openrtl_f2bits(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src) {
    return openrtl_arith(buf, OPENRTL_OP_F2BITS, size, dest, src, 0);
}

int openrtl_bits2f(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src) {
    return openrtl_arith(buf, OPENRTL_OP_BITS2F, size, dest, src, 0);
}


int openrtl_vadd(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2) {
    return openrtl_arith(buf, OPENRTL_OP_VADD, size, dest, src1, src2);
}

int openrtl_vsubtract(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2) {
    return openrtl_arith(buf, OPENRTL_OP_VSUBTRACT, size, dest, src1, src2);
}

int openrtl_vmultiplyf(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2) {
    return openrtl_arith(buf, OPENRTL_OP_VMULTIPLYF, size, dest, src1, src2);
}

int openrtl_vdividef(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2) {
    return openrtl_arith(buf, OPENRTL_OP_VDIVIDEF, size, dest, src1, src2);
}

int openrtl_vmultiply(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2) {
    return openrtl_arith(buf, OPENRTL_OP_VMULTIPLY, size, dest, src1, src2);
}

int openrtl_vdivide(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2) {
    return openrtl_arith(buf, OPENRTL_OP_VDIVIDE, size, dest, src1, src2);
}

int openrtl_vdot(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2) {
    return openrtl_arith(buf, OPENRTL_OP_VDOT, size, dest, src1, src2);
}

int openrtl_vcross(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2) {
    return openrtl_arith(buf, OPENRTL_OP_VCROSS, size, dest, src1, src2);
}

int openrtl_vload(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2) {
    int status = 0;
    status |= openrtl_arith(buf, OPENRTL_OP_VLOAD, size, dest, src1, src2);

    struct OpenrtlElement elem;
    elem.offset = buf->len;
    elem.place = OPENRTL_V_REGISTER;
    elem.value = OPENRTL_MEMORY_INDIRECT;
    elem.v1.general.reg = dest;
    elem.v1.general.size = size;
    elem.v2.addri.base = src1;
    elem.v2.addri.offset = src2;
    status |= openrtl_append(buf, &elem);

    return status;
}

int openrtl_vstore(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2) {
    int status = 0;
    status |= openrtl_arith(buf, OPENRTL_OP_VSTORE, size, dest, src1, src2);

    struct OpenrtlElement elem;
    elem.offset = buf->len;
    elem.place = OPENRTL_MEMORY_INDIRECT;
    elem.value = OPENRTL_V_REGISTER;
    elem.v1.addri.base = src1;
    elem.v1.addri.offset = src2;
    elem.v2.general.reg = dest;
    elem.v2.general.size = size;
    status |= openrtl_append(buf, &elem);

    return status;
}

int openrtl_vextend(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src) {
    return openrtl_arith(buf, OPENRTL_OP_VEXTEND, size, dest, src, 0);
}

int openrtl_vtruncate(OpenrtlBuffer *buf, uint8_t size, uint8_t dest) {
    return openrtl_arith(buf, OPENRTL_OP_VEXTEND, size, dest, 0, 0);
}

static int openrtl_none(OpenrtlBuffer *buf, uint8_t opcode) {
    if (buf->len + 4 > buf->cap) {
        buf->cap *= 2;
        buf->ptr = realloc(buf->ptr, buf->cap);
    }
    int status = 0;
    OpenrtlInst inst = {0};
    inst.opcode = opcode;
    memcpy((char *) buf->ptr + buf->len, &inst, 4);
    buf->len += 4;
    return status;
}

static int openrtl_arith(OpenrtlBuffer *buf, uint8_t opcode, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2) {
    if (buf->len + 4 > buf->cap) {
        buf->cap *= 2;
        buf->ptr = realloc(buf->ptr, buf->cap);
    }
    int status = 0;
    OpenrtlInst inst = {0};
    inst.opcode = opcode;
    inst.size = size;
    inst.arith.dest = dest;
    inst.arith.src1 = src1;
    inst.arith.src2 = src2;
    memcpy((char *) buf->ptr + buf->len, &inst, 4);
    buf->len += 4;
    return status;
}

static int openrtl_arith_b(OpenrtlBuffer *buf, uint8_t opcode, uint8_t size, uint8_t dest, uint8_t src, uint8_t size2) {
    if (buf->len + 4 > buf->cap) {
        buf->cap *= 2;
        buf->ptr = realloc(buf->ptr, buf->cap);
    }
    int status = 0;
    OpenrtlInst inst = {0};
    inst.opcode = opcode;
    inst.size = size;
    inst.arith_b.dest = dest;
    inst.arith_b.src = src;
    inst.arith_b.size = size2;
    memcpy((char *) buf->ptr + buf->len, &inst, 4);
    buf->len += 4;
    return status;
}

static int openrtl_imm(OpenrtlBuffer *buf, uint8_t opcode, uint32_t value) {
    if (buf->len + 4 > buf->cap) {
        buf->cap *= 2;
        buf->ptr = realloc(buf->ptr, buf->cap);
    }
    int status = 0;
    OpenrtlInst inst = {0};
    inst.opcode = opcode;
    inst.imm.value = value;
    memcpy((char *) buf->ptr + buf->len, &inst, 4);
    buf->len += 4;
    return status;
}

static int openrtl_rel(OpenrtlBuffer *buf, uint8_t opcode, uint8_t size, uint8_t dest, uint64_t value) {
    if (buf->len + 12 > buf->cap) {
        buf->cap *= 2;
        buf->ptr = realloc(buf->ptr, buf->cap);
    }
    int status = 0;
    size_t len;
    if (value == 0) {
        len = 0;
    } else if (value <= 0xff) {
        len = 1;
    } else if (value <= 0xffff) {
        len = 2;
    } else if (value <= 0xffffffff) {
        len = 4;
    } else {
        len = 8;
    }
    OpenrtlInst inst = {0};
    inst.opcode = opcode;
    inst.size = size;
    inst.rel.dest = dest;
    inst.rel.len = len;
    memcpy((char *) buf->ptr + buf->len, &inst, 4);
    buf->len += 4;
    memcpy((char *) buf->ptr + buf->len, &value, len);
    buf->len += len;
    return status;
}

static int openrtl_append(OpenrtlBuffer *buf, struct OpenrtlElement *elem) {
    if (buf->matrix.len + 1 > buf->matrix.cap) {
        buf->matrix.cap *= 2;
        buf->matrix.ptr = realloc(buf->matrix.ptr, buf->matrix.cap);
    }

    buf->matrix.ptr[buf->matrix.len++] = *elem;
    return 0;
}
