#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include "include/openrtl.h"

static void openrtl_alloc_sort_live(struct OpenrtlInterval *intervals, size_t lo, size_t hi);
static size_t openrtl_alloc_partition_live(struct OpenrtlInterval *intervals, size_t lo, size_t hi);

static void openrtl_alloc_sort_active(struct OpenrtlInterval *live, struct OpenrtlActive *active, size_t lo, size_t hi);
static size_t openrtl_alloc_partition_active(struct OpenrtlInterval *live, struct OpenrtlActive *active, size_t lo, size_t hi);

static void openrtl_alloc_memswap(void *a, void *b, size_t size, void *temp);

static void openrtl_alloc_fn(OpenrtlRegalloc *alloc, OpenrtlContext *ctx, OpenrtlBuffer *buf);
static void openrtl_alloc_inst(OpenrtlRegalloc *alloc, OpenrtlContext *ctx, OpenrtlBuffer *buf, OpenrtlInst *inst, void *arg, size_t idx);

static inline int log2ll(unsigned long long val) {
    if (val == 0) return INT_MIN;
    if (val == 1) return 0;
    int log = 0;
    while (val >>= 1) log++;
    return log;
}

struct OpenrtlPurposePair {
    size_t index;
    struct OpenrtlPurpose purpose;
};

void openrtl_alloc_linscan(OpenrtlRegalloc *alloc, size_t regc, size_t paramc, struct OpenrtlGmReg *params) {
    alloc->counter = 0;
    
    alloc->registers.len = regc;
    alloc->registers.cap = regc;
    alloc->registers.registers = malloc(sizeof(struct OpenrtlGmReg) * regc);

    for (size_t i = 0; i < regc; i++) {
        alloc->registers.registers[i].number = i;
    }
    
    alloc->parameters.len = paramc;
    alloc->parameters.cap = paramc;
    alloc->parameters.registers = malloc(sizeof(struct OpenrtlGmReg) * alloc->parameters.cap);

    for (size_t i = 0; i < paramc; i++) {
        alloc->parameters.registers[i] = params[i];
    }
    
    alloc->stack.len = 0;
    alloc->stack.cap = 32;
    alloc->stack.intervals = malloc(sizeof(struct OpenrtlInterval) * alloc->stack.cap);
    
    alloc->live.len = 0;
    alloc->live.cap = 32;
    alloc->live.intervals = malloc(sizeof(struct OpenrtlInterval) * alloc->live.cap);
    
    alloc->active.len = 0;
    alloc->active.cap = 32;
    alloc->active.actives = malloc(sizeof(struct OpenrtlActive) * alloc->active.cap);
    
    alloc->offset = 0;
}

void openrtl_alloc_add(OpenrtlRegalloc *alloc, struct OpenrtlInterval *interval) {
    if (interval->stack || interval->ti.size > 8) {
        if (alloc->stack.len == alloc->stack.cap) {
            alloc->stack.cap *= 2;
            alloc->stack.intervals = realloc(alloc->stack.intervals, alloc->stack.cap * sizeof(struct OpenrtlInterval));
        }

        alloc->stack.intervals[alloc->stack.len++] = *interval;
    } else {
        if (alloc->live.len == alloc->live.cap) {
            alloc->live.cap *= 2;
            alloc->live.intervals = realloc(alloc->live.intervals, alloc->live.cap * sizeof(struct OpenrtlInterval));
        }

        alloc->live.intervals[alloc->live.len++] = *interval;
    }
}

void openrtl_alloc_param(OpenrtlRegalloc *alloc, struct OpenrtlInterval *interval, uint32_t param) {
    if (interval->stack || interval->ti.size > 8) {
        if (alloc->stack.len == alloc->stack.cap) {
            alloc->stack.cap *= 2;
            alloc->stack.intervals = realloc(alloc->stack.intervals, alloc->stack.cap * sizeof(struct OpenrtlInterval));
        }

        alloc->stack.intervals[alloc->stack.len++] = *interval;
    } else {
        if (alloc->live.len == alloc->live.cap) {
            alloc->live.cap *= 2;
            alloc->live.intervals = realloc(alloc->live.intervals, alloc->live.cap * sizeof(struct OpenrtlInterval));
        }

        if (param < alloc->parameters.len) {
            interval->reg = param;
            interval->size = log2ll(interval->ti.size);
        }
        alloc->live.intervals[alloc->live.len++] = *interval;
    }
}

void openrtl_alloc_find(OpenrtlRegalloc *alloc, OpenrtlContext *ctx, OpenrtlBuffer *buf) {
    openrtl_alloc_fn(alloc, ctx, buf);
}

void openrtl_alloc_regtable(struct OpenrtlRegisterTable *dest, OpenrtlRegalloc *alloc) {
    size_t cap = 32;
    dest->len = 0;
    dest->cap = cap;
    dest->entries = malloc(cap * sizeof(struct OpenrtlRegEntry));

    for (size_t i = 0; i < alloc->live.len; i++) {
        if (dest->len == dest->cap) {
            dest->cap *= 2;
            dest->entries = realloc(dest->entries, dest->cap * sizeof(struct OpenrtlRegEntry));
        }

        dest->entries[dest->len].start = alloc->live.intervals[i].start;
        dest->entries[dest->len].end = alloc->live.intervals[i].end;
        dest->entries[dest->len].key = alloc->live.intervals[i].name;
        dest->entries[dest->len++].purpose = alloc->live.intervals[i].purpose;
    }

    for (size_t i = 0; i < alloc->stack.len; i++) {
        if (dest->len == dest->cap) {
            dest->cap *= 2;
            dest->entries = realloc(dest->entries, dest->cap * sizeof(struct OpenrtlRegEntry));
        }

        dest->entries[dest->len].start = alloc->live.intervals[i].start;
        dest->entries[dest->len].end = alloc->live.intervals[i].end;
        dest->entries[dest->len].key = alloc->stack.intervals[i].name;
        dest->entries[dest->len++].purpose = alloc->stack.intervals[i].purpose;
    }
}

static void openrtl_alloc_fn(OpenrtlRegalloc *alloc, OpenrtlContext *ctx, OpenrtlBuffer *buf) {
    for (size_t i = 0; i < buf->params; i++) {
        int stack;
        uint64_t name;
        if (i < alloc->parameters.len) {
            stack = 0;
            name = i << 8 | alloc->parameters.registers[i].number;
        } else {
            stack = 1;
            name = i << 8;
        }
        // openrtl expects that arguments are word-size
        OpenrtlTypeInfo ti = { .size = 8, .align = 8 };
        struct OpenrtlPurpose purpose = {
            .tag = OPENRTL_REG_SPILLED,
            .stack.offset = 0,
            .stack.size = log2ll(ti.size),
            .stack.align = log2ll(ti.align)
        };
        OpenrtlLifetime start = -1;
        OpenrtlLifetime end = -1;
        struct OpenrtlInterval interval = {
            .stack = stack,
            .name = name,
            .ti = ti,
            .purpose = purpose,
            .start = start,
            .end = end
        };
        openrtl_alloc_param(alloc, &interval, i);
    }
    alloc->counter = buf->params;
    for (size_t i = 0; i < buf->len;) {
        OpenrtlInst *inst = (void *) ((char *) buf->ptr + i);
        openrtl_alloc_inst(alloc, ctx, buf, inst, (char *) buf->ptr + 4, i);
        i += 4;
        switch (inst->opcode) {
        case OPENRTL_OP_CALL:
        case OPENRTL_OP_BRANCH:
        case OPENRTL_OP_BRANCH_CARRY:
        case OPENRTL_OP_BRANCH_OVERFLOW:
        case OPENRTL_OP_BRANCH_EQUAL:
        case OPENRTL_OP_BRANCH_NOT_EQUAL:
        case OPENRTL_OP_BRANCH_LESS:
        case OPENRTL_OP_BRANCH_LESS_EQ:
        case OPENRTL_OP_BRANCH_GREATER:
        case OPENRTL_OP_BRANCH_GREATER_EQ:
        case OPENRTL_OP_IMOVE_IMMEDIATE:
            i += inst->rel.len;
            break;
        default:
            break;
        }
    }
}

static void openrtl_alloc_inst(OpenrtlRegalloc *alloc, OpenrtlContext *ctx, OpenrtlBuffer *buf, OpenrtlInst *inst, void *arg, size_t idx) {
    (void) ctx;
    (void) buf;
    (void) arg;
    switch (inst->opcode) {
    // 1
    case OPENRTL_OP_CALL_INDIRECT:
    case OPENRTL_OP_IMOVE_IMMEDIATE:
    case OPENRTL_OP_IPOP:
    case OPENRTL_OP_IPUSH:
    case OPENRTL_OP_FPOP:
    case OPENRTL_OP_FPUSH:
    case OPENRTL_OP_EXTEND:
    case OPENRTL_OP_VTRUNCATE:
        alloc->live.intervals[alloc->variables[inst->arith.dest]].end = idx;
        break;
    // 2
    case OPENRTL_OP_IMOVE_UNSIGNED:
    case OPENRTL_OP_IMOVE_SIGNED:
    case OPENRTL_OP_FMOVE:
    case OPENRTL_OP_F2I:
    case OPENRTL_OP_I2F:
    case OPENRTL_OP_F2BITS:
    case OPENRTL_OP_BITS2F:
    case OPENRTL_OP_VEXTEND:
        alloc->live.intervals[alloc->variables[inst->arith.dest]].end = idx;
        alloc->live.intervals[alloc->variables[inst->arith.src1]].end = idx;
        break;
    // 3
    case OPENRTL_OP_IADD:
    case OPENRTL_OP_IADD_CARRY:
    case OPENRTL_OP_IAND:
    case OPENRTL_OP_IOR:
    case OPENRTL_OP_IXOR:
    case OPENRTL_OP_ISUBTRACT:
    case OPENRTL_OP_ICOMPARE:
    case OPENRTL_OP_IMULTIPLY_UNSIGNED:
    case OPENRTL_OP_IMULTIPLY_SIGNED:
    case OPENRTL_OP_IDIVIDE_UNSIGNED:
    case OPENRTL_OP_IDIVIDE_SIGNED:
    case OPENRTL_OP_IMODULO_UNSIGNED:
    case OPENRTL_OP_IMODULO_SIGNED:
    case OPENRTL_OP_ILOAD:
    case OPENRTL_OP_ISTORE:
    case OPENRTL_OP_FADD:
    case OPENRTL_OP_FSUBTRACT:
    case OPENRTL_OP_FCOMPARE:
    case OPENRTL_OP_FMULTIPLY:
    case OPENRTL_OP_FDIVIDE:
    case OPENRTL_OP_FLOAD:
    case OPENRTL_OP_FSTORE:
    case OPENRTL_OP_VADD:
    case OPENRTL_OP_VSUBTRACT:
    case OPENRTL_OP_VMULTIPLYF:
    case OPENRTL_OP_VDIVIDEF:
    case OPENRTL_OP_VMULTIPLY:
    case OPENRTL_OP_VDIVIDE:
    case OPENRTL_OP_VDOT:
    case OPENRTL_OP_VCROSS:
    case OPENRTL_OP_VLOAD:
    case OPENRTL_OP_VSTORE:
        alloc->live.intervals[alloc->variables[inst->arith.dest]].end = idx;
        alloc->live.intervals[alloc->variables[inst->arith.src1]].end = idx;
        alloc->live.intervals[alloc->variables[inst->arith.src2]].end = idx;
        break;
    default:
        break;
    }

    switch (inst->opcode) {
    case OPENRTL_OP_IMOVE_IMMEDIATE:
    case OPENRTL_OP_IPOP:
    case OPENRTL_OP_FPOP:
    case OPENRTL_OP_EXTEND:
    case OPENRTL_OP_VTRUNCATE:
    case OPENRTL_OP_IMOVE_UNSIGNED:
    case OPENRTL_OP_IMOVE_SIGNED:
    case OPENRTL_OP_FMOVE:
    case OPENRTL_OP_F2I:
    case OPENRTL_OP_I2F:
    case OPENRTL_OP_F2BITS:
    case OPENRTL_OP_BITS2F:
    case OPENRTL_OP_VEXTEND:
    case OPENRTL_OP_IADD:
    case OPENRTL_OP_IADD_CARRY:
    case OPENRTL_OP_IAND:
    case OPENRTL_OP_IOR:
    case OPENRTL_OP_IXOR:
    case OPENRTL_OP_ISUBTRACT:
    case OPENRTL_OP_ICOMPARE:
    case OPENRTL_OP_IMULTIPLY_UNSIGNED:
    case OPENRTL_OP_IMULTIPLY_SIGNED:
    case OPENRTL_OP_IDIVIDE_UNSIGNED:
    case OPENRTL_OP_IDIVIDE_SIGNED:
    case OPENRTL_OP_IMODULO_UNSIGNED:
    case OPENRTL_OP_IMODULO_SIGNED:
    case OPENRTL_OP_ILOAD:
    case OPENRTL_OP_ISTORE:
    case OPENRTL_OP_FADD:
    case OPENRTL_OP_FSUBTRACT:
    case OPENRTL_OP_FCOMPARE:
    case OPENRTL_OP_FMULTIPLY:
    case OPENRTL_OP_FDIVIDE:
    case OPENRTL_OP_FLOAD:
    case OPENRTL_OP_FSTORE:
    case OPENRTL_OP_VADD:
    case OPENRTL_OP_VSUBTRACT:
    case OPENRTL_OP_VMULTIPLYF:
    case OPENRTL_OP_VDIVIDEF:
    case OPENRTL_OP_VMULTIPLY:
    case OPENRTL_OP_VDIVIDE:
    case OPENRTL_OP_VDOT:
    case OPENRTL_OP_VCROSS:
    case OPENRTL_OP_VLOAD:
    case OPENRTL_OP_VSTORE:
        if (alloc->live.len == alloc->live.cap) {
            alloc->live.cap *= 2;
            alloc->live.intervals = realloc(alloc->live.intervals, alloc->live.cap * sizeof(struct OpenrtlInterval));
        }

        alloc->live.intervals[alloc->live.len].name = alloc->counter++ << 8 | inst->arith.dest;
        alloc->live.intervals[alloc->live.len].ti.size = 1 << inst->size;
        alloc->live.intervals[alloc->live.len].ti.align = 1 << inst->size;
        alloc->live.intervals[alloc->live.len].purpose.tag = OPENRTL_REG_SPILLED;
        alloc->live.intervals[alloc->live.len].purpose.stack.offset = 0;
        alloc->live.intervals[alloc->live.len].purpose.stack.size = inst->size;
        alloc->live.intervals[alloc->live.len].purpose.stack.align = inst->size;
        alloc->live.intervals[alloc->live.len].start = idx;
        alloc->live.intervals[alloc->live.len].end = idx;
        alloc->live.intervals[alloc->live.len].stack = 0;
        alloc->live.intervals[alloc->live.len].reserved = 0;
        alloc->variables[inst->arith.dest] = alloc->live.len;
        ++alloc->live.len;
        break;
    default:
        break;
    }
}

int openrtl_alloc_allocate(OpenrtlRegalloc *alloc) {
    openrtl_alloc_sort_live(alloc->live.intervals, 0, alloc->live.len - 1);

    size_t expire_len = 0;
    size_t *expire = malloc(sizeof(size_t) * alloc->live.len);

    size_t delta_len = 0;
    struct OpenrtlPurposePair *delta = malloc(sizeof(struct OpenrtlPurposePair) * alloc->registers.len);

    for (size_t idx = 0; idx < alloc->live.len; idx++) {
        struct OpenrtlInterval *i = alloc->live.intervals + idx;

        openrtl_alloc_sort_active(alloc->live.intervals, alloc->active.actives, 0, alloc->active.len - 1);
        for (size_t idx0 = 0; idx0 < alloc->active.len; idx0++) {
            size_t j = alloc->active.actives[idx0].index;
            if (alloc->live.intervals[j].ti.size) {
                OpenrtlLifetime lt = alloc->live.intervals[j].end;
                if (lt >= i->start) {
                    break;
                }
                expire[expire_len++] = idx0;
            }
        }

        for (size_t idx0 = 0; idx0 < expire_len; idx0++) {
            size_t j = expire[idx0];
            struct OpenrtlActive active = alloc->active.actives[j];
            --alloc->active.len;
            memmove(alloc->active.actives + j, alloc->active.actives + j + 1, (alloc->active.len - j) * sizeof(struct OpenrtlActive));
            for (size_t i = idx0 + 1; i < expire_len; i++) {
                if (expire[i] > j) {
                    --expire[i];
                }
            }
            if (alloc->registers.len == alloc->registers.cap) {
                alloc->registers.cap *= 2;
                alloc->registers.registers = realloc(alloc->registers.registers, sizeof(struct OpenrtlGmReg) * alloc->registers.cap);
            }
            alloc->registers.registers[alloc->registers.len++] = active.reg;
        }
        expire_len = 0;

        if (i->reserved) {
            size_t j;
            for (j = 0; j < alloc->registers.len; j++) {
                if (i->reg == j) {
                    break;
                }
            }

            if (j == alloc->registers.len) {
                fprintf(stderr, "error: cannot find a free register with this name: %u\n", i->reg);
                return 1;
            }
            delta[delta_len].index = idx;
            delta[delta_len].purpose.tag = OPENRTL_REG_ALLOCATED;
            delta[delta_len].purpose.reg.number = i->reg;
            delta[delta_len++].purpose.reg.size = i->size;

            alloc->active.actives[alloc->active.len].index = idx;
            alloc->active.actives[alloc->active.len++].reg.number = i->reg;

            --alloc->registers.len;
            memmove(alloc->registers.registers + j, alloc->registers.registers + j + 1, (alloc->registers.len - j) * sizeof(struct OpenrtlGmReg));
        } else if (!alloc->registers.len) {
            alloc->offset += 8;
            delta[delta_len].index = idx;
            delta[delta_len].purpose.tag = OPENRTL_REG_SPILLED;
            delta[delta_len++].purpose.stack.offset = alloc->offset;
        } else {
            struct OpenrtlGmReg reg = alloc->registers.registers[--alloc->registers.len];
            delta[delta_len].index = idx;
            delta[delta_len].purpose.tag = OPENRTL_REG_ALLOCATED;
            delta[delta_len].purpose.reg.number = reg.number;
            delta[delta_len++].purpose.reg.size = log2ll(i->ti.size);
                
            if (alloc->active.len == alloc->active.cap) {
                alloc->active.cap *= 2;
                alloc->active.actives = realloc(alloc->active.actives, sizeof(struct OpenrtlActive) * alloc->active.cap);
            }
            
            alloc->active.actives[alloc->active.len].index = idx;
            alloc->active.actives[alloc->active.len++].reg = reg;
        }

        for (size_t idx0 = 0; idx0 < delta_len; idx0++) {
            alloc->live.intervals[delta[idx0].index].purpose = delta[idx0].purpose;
        }
        delta_len = 0;
    }

    free(expire);
    free(delta);

    return 0;
}

// quicksort
static void openrtl_alloc_sort_live(struct OpenrtlInterval *intervals, size_t lo, size_t hi) {
    if (hi == (size_t) -1) {
        return;
    }
    if (lo < hi) {
        size_t p = openrtl_alloc_partition_live(intervals, lo, hi);
        openrtl_alloc_sort_live(intervals, lo, p - 1);
        openrtl_alloc_sort_live(intervals, p + 1, hi);
    }
}

static size_t openrtl_alloc_partition_live(struct OpenrtlInterval *intervals, size_t lo, size_t hi) {
    uint8_t temp[sizeof(struct OpenrtlInterval)];
    int64_t pivot = intervals[hi].start;
    size_t i = lo;
    for (size_t j = lo; j < hi; j++) {
        if (intervals[j].start < pivot) {
            openrtl_alloc_memswap(intervals + i, intervals + j, sizeof(struct OpenrtlInterval), temp);
            ++i;
        }
    }
    openrtl_alloc_memswap(intervals + i, intervals + hi, sizeof(struct OpenrtlInterval), temp);
    return i;
}

static void openrtl_alloc_sort_active(struct OpenrtlInterval *live, struct OpenrtlActive *active, size_t lo, size_t hi) {
    if (hi == (size_t) -1) {
        return;
    }
    if (lo < hi) {
        size_t p = openrtl_alloc_partition_active(live, active, lo, hi);
        openrtl_alloc_sort_active(live, active, lo, p - 1);
        openrtl_alloc_sort_active(live, active, p + 1, hi);
    }
}

static size_t openrtl_alloc_partition_active(struct OpenrtlInterval *live, struct OpenrtlActive *active, size_t lo, size_t hi) {
    uint8_t temp[sizeof(struct OpenrtlActive)];
    int64_t pivot = live[hi].end;
    size_t i = lo;
    for (size_t j = lo; j < hi; j++) {
        if (live[j].end < pivot) {
            openrtl_alloc_memswap(active + i, active + j, sizeof(struct OpenrtlActive), temp);
            ++i;
        }
    }
    openrtl_alloc_memswap(active + i, active + hi, sizeof(struct OpenrtlActive), temp);
    return i;
}

static void openrtl_alloc_memswap(void *a, void *b, size_t size, void *temp) {
    memcpy(temp, a, size);
    memcpy(a, b, size);
    memcpy(b, temp, size);
}
