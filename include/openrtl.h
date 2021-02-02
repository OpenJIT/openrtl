#ifndef OPENRTL_H
#define OPENRTL_H

#include <stddef.h>
#include <stdint.h>

#define OPENRTL_RSP 255
#define OPENRTL_RFP 254
#define OPENRTL_R(n) (n & 0xff)
#define OPENRTL_X(n) (n & 0xff)
#define OPENRTL_V(n) ((n << 2) & 0xff)

typedef struct OpenrtlContext OpenrtlContext;
typedef struct OpenrtlBuffer OpenrtlBuffer;
typedef struct OpenrtlInst OpenrtlInst;
typedef struct OpenrtlTable OpenrtlTable;
typedef struct OpenrtlLinker OpenrtlLinker;
typedef struct OpenrtlMatrix OpenrtlMatrix;

struct OpenrtlEntry {
    const char *name;
    size_t addr;
};

struct OpenrtlTable {
    size_t cap;
    size_t len;
    struct OpenrtlEntry *ptr;
};

enum {
    OPENRTL_SYMBOL_LOCAL,
    OPENRTL_SYMBOL_GLOBAL,
};

struct OpenrtlSymbol {
    int type;
    const char *name;
    size_t offset;
    uint64_t mask;
    uint64_t address;
};

struct OpenrtlLinker {
    size_t cap;
    size_t len;
    struct OpenrtlSymbol *ptr;
};

struct OpenrtlContext {
    size_t cap;
    size_t len;
    OpenrtlBuffer *ptr;
    OpenrtlTable global;
};

enum {
    OPENRTL_UNINIT,
    OPENRTL_IMMEDIATE,
    OPENRTL_GP_REGISTER,
    OPENRTL_FP_REGISTER,
    OPENRTL_V_REGISTER,
    OPENRTL_MEMORY_IMMEDIATE,
    OPENRTL_MEMORY_BASE,
    OPENRTL_MEMORY_INDIRECT,
};

struct OpenrtlElement {
    size_t offset;
    int place;
    int value;
    union {
        uint64_t immediate;
        struct {
            uint8_t base;
            int32_t offset;
        } addr;
        struct {
            uint8_t base;
            uint8_t offset;
        } addri;
        struct {
            uint8_t reg;
            uint8_t size;
            uint8_t ext;
        } general, floating, vector;
    } v1, v2;
};

struct OpenrtlMatrix {
    size_t cap;
    size_t len;
    struct OpenrtlElement *ptr;
};

struct OpenrtlBuffer {
    size_t cap;
    size_t len;
    void *ptr;
    OpenrtlMatrix matrix;
    OpenrtlLinker linker;
    OpenrtlTable local;
};

enum {
    OPENRTL_ISIZE_8,
    OPENRTL_ISIZE_16,
    OPENRTL_ISIZE_32,
    OPENRTL_ISIZE_64,
};

enum {
    OPENRTL_FSIZE_32,
    OPENRTL_FSIZE_64,
};

enum {
    OPENRTL_VSIZE_1,
    OPENRTL_VSIZE_2,
    OPENRTL_VSIZE_3,
    OPENRTL_VSIZE_4,
};

enum {
    // none
    OPENRTL_OP_RETURN,
    // short immediate i
    OPENRTL_OP_ENTER,
    OPENRTL_OP_LEAVE,
    // long relative +i
    OPENRTL_OP_CALL,
    // arith r
    OPENRTL_OP_CALL_INDIRECT,
    OPENRTL_OP_BRANCH,
    OPENRTL_OP_BRANCH_CARRY,
    OPENRTL_OP_BRANCH_OVERFLOW,
    OPENRTL_OP_BRANCH_EQUAL,
    OPENRTL_OP_BRANCH_NOT_EQUAL,
    OPENRTL_OP_BRANCH_LESS,
    OPENRTL_OP_BRANCH_LESS_EQ,
    OPENRTL_OP_BRANCH_GREATER,
    OPENRTL_OP_BRANCH_GREATER_EQ,

    // arith r, r, r
    OPENRTL_OP_IADD,
    OPENRTL_OP_IADD_CARRY,
    OPENRTL_OP_IAND,
    OPENRTL_OP_IOR,
    OPENRTL_OP_IXOR,
    OPENRTL_OP_ISUBTRACT,
    OPENRTL_OP_ICOMPARE,
    OPENRTL_OP_IMULTIPLY_UNSIGNED,
    OPENRTL_OP_IMULTIPLY_SIGNED,
    OPENRTL_OP_IDIVIDE_UNSIGNED,
    OPENRTL_OP_IDIVIDE_SIGNED,
    OPENRTL_OP_IMODULO_UNSIGNED,
    OPENRTL_OP_IMODULO_SIGNED,
    // long immediate r, +i
    OPENRTL_OP_IMOVE_IMMEDIATE,
    // arith/b r, r/b
    OPENRTL_OP_IMOVE_UNSIGNED,
    OPENRTL_OP_IMOVE_SIGNED,
    // arith r, r, r
    OPENRTL_OP_ILOAD,
    OPENRTL_OP_ISTORE,
    // arith r
    OPENRTL_OP_IPOP,
    OPENRTL_OP_IPUSH,

    // arith x, x, x
    OPENRTL_OP_FADD,
    OPENRTL_OP_FSUBTRACT,
    OPENRTL_OP_FCOMPARE,
    OPENRTL_OP_FMULTIPLY,
    OPENRTL_OP_FDIVIDE,
    // arith x, x
    OPENRTL_OP_FMOVE,
    // arith x, r, r
    OPENRTL_OP_FLOAD,
    OPENRTL_OP_FSTORE,
    // arith x
    OPENRTL_OP_FPOP,
    OPENRTL_OP_FPUSH,
    // arith/b r, x/b
    OPENRTL_OP_F2I,
    // arith/b x, r/b
    OPENRTL_OP_I2F,
    // if bits == 0 then extend
    OPENRTL_OP_EXTEND,
    // else truncate
    OPENRTL_OP_TRUNCATE = OPENRTL_OP_EXTEND,
    // arith r, x
    OPENRTL_OP_F2BITS,
    // arith x, r
    OPENRTL_OP_BITS2F,

    // arith v, v, v
    OPENRTL_OP_VADD,
    OPENRTL_OP_VSUBTRACT,
    OPENRTL_OP_VMULTIPLYF,
    OPENRTL_OP_VDIVIDEF,
    OPENRTL_OP_VMULTIPLY,
    OPENRTL_OP_VDIVIDE,
    OPENRTL_OP_VDOT,
    OPENRTL_OP_VCROSS,
    // arith v, r, r
    OPENRTL_OP_VLOAD,
    OPENRTL_OP_VSTORE,
    // arith v, x
    OPENRTL_OP_VEXTEND,
    // arith v
    OPENRTL_OP_VTRUNCATE,

    OPENRTL_OP_COUNT,
};

struct OpenrtlInst {
    unsigned int opcode : 6;
    unsigned int size : 2;
    union {
        struct {
            unsigned char dest;
            unsigned char src1;
            unsigned char src2;
        } arith;
        struct {
            unsigned char dest;
            unsigned char src;
            unsigned char size;
        } arith_b;
        struct {
            unsigned int value : 24;
        } imm;
        struct {
            unsigned char dest;
            unsigned char len; // only 0, 1, 2, 4 or 8 permitted
        } rel;
    };
};

void openrtl_context(OpenrtlContext *ctx);
void openrtl_del_context(OpenrtlContext *ctx);
void openrtl_add_buffer(OpenrtlContext *ctx, const char *name, OpenrtlBuffer *buf);
void openrtl_global(OpenrtlContext *ctx, const char *name, uint64_t addr);
void openrtl_link(OpenrtlContext *ctx);

void openrtl_buffer(OpenrtlBuffer *buf);
void openrtl_del_buffer(OpenrtlBuffer *buf);
void openrtl_local(OpenrtlBuffer *ctx, const char *name, uint64_t addr);
void openrtl_symbol(OpenrtlBuffer *ctx, int type, const char *name);

int openrtl_return(OpenrtlBuffer *buf);
int openrtl_enter(OpenrtlBuffer *buf, uint32_t imm);
int openrtl_leave(OpenrtlBuffer *buf, uint32_t imm);
int openrtl_call(OpenrtlBuffer *buf, uint64_t addr);
int openrtl_call_indirect(OpenrtlBuffer *buf, uint8_t dest);
int openrtl_branch(OpenrtlBuffer *buf, uint64_t addr);
int openrtl_branch_carry(OpenrtlBuffer *buf, uint64_t addr);
int openrtl_branch_overflow(OpenrtlBuffer *buf, uint64_t addr);
int openrtl_branch_equal(OpenrtlBuffer *buf, uint64_t addr);
int openrtl_branch_not_equal(OpenrtlBuffer *buf, uint64_t addr);
int openrtl_branch_less(OpenrtlBuffer *buf, uint64_t addr);
int openrtl_branch_less_eq(OpenrtlBuffer *buf, uint64_t addr);
int openrtl_branch_greater(OpenrtlBuffer *buf, uint64_t addr);
int openrtl_branch_greater_eq(OpenrtlBuffer *buf, uint64_t addr);

int openrtl_iadd(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2);
int openrtl_iadd_carry(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2);
int openrtl_iand(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2);
int openrtl_ior(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2);
int openrtl_ixor(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2);
int openrtl_isubtract(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2);
int openrtl_icompare(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2);
int openrtl_imultiply_unsigned(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2);
int openrtl_imultiply_signed(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2);
int openrtl_idivide_unsigned(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2);
int openrtl_idivide_signed(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2);
int openrtl_imodulo_unsigned(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2);
int openrtl_imodulo_signed(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2);
int openrtl_imove_immediate(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint64_t imm);
int openrtl_imove_unsigned(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src, uint8_t size2);
int openrtl_imove_signed(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src, uint8_t size2);
int openrtl_iload(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2);
int openrtl_istore(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2);
int openrtl_ipop(OpenrtlBuffer *buf, uint8_t dest);
int openrtl_ipush(OpenrtlBuffer *buf, uint8_t src);

int openrtl_fadd(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2);
int openrtl_fsubtract(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2);
int openrtl_fcompare(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2);
int openrtl_fmultiply(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2);
int openrtl_fdivide(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2);
int openrtl_fmove(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src);
int openrtl_fload(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2);
int openrtl_fstore(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2);
int openrtl_fpop(OpenrtlBuffer *buf, uint8_t dest);
int openrtl_fpush(OpenrtlBuffer *buf, uint8_t src);
int openrtl_f2i(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src, uint8_t size2);
int openrtl_i2f(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src, uint8_t size2);
int openrtl_extend(OpenrtlBuffer *buf, uint8_t dest);
int openrtl_truncate(OpenrtlBuffer *buf, uint8_t dest);
int openrtl_f2bits(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src);
int openrtl_bits2f(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src);

int openrtl_vadd(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2);
int openrtl_vsubtract(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2);
int openrtl_vmultiplyf(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2);
int openrtl_vdividef(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2);
int openrtl_vmultiply(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2);
int openrtl_vdivide(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2);
int openrtl_vdot(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2);
int openrtl_vcross(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2);
int openrtl_vload(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2);
int openrtl_vstore(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src1, uint8_t src2);
int openrtl_vextend(OpenrtlBuffer *buf, uint8_t size, uint8_t dest, uint8_t src);
int openrtl_vtruncate(OpenrtlBuffer *buf, uint8_t size, uint8_t dest);

#endif /* OPENRTL_H */
