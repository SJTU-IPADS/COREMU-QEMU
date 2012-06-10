#if DATA_BITS == 64
#  define DATA_TYPE uint64_t
#  define SUFFIX q
#  define UPSUFFIX Q
#  define LD ldq_raw
#  define OT 3
#elif DATA_BITS == 32
#  define DATA_TYPE uint32_t
#  define SUFFIX l
#  define UPSUFFIX L
#  define LD ldl_raw
#  define OT 2
#elif DATA_BITS == 16
#  define DATA_TYPE uint16_t
#  define SUFFIX w
#  define UPSUFFIX W
#  define LD lduw_raw
#  define OT 1
#elif DATA_BITS == 8
#  define DATA_TYPE uint8_t
#  define SUFFIX b
#  define UPSUFFIX B
#  define LD ldub_raw
#  define OT 0
#else
#error unsupported data size
#endif

/* Lightweight transactional memory. */
#ifdef CONFIG_REPLAY

#define TX(__q_addr, type, value, command) \
    DATA_TYPE value;                       \
                                           \
    value = *(DATA_TYPE *)__q_addr;        \
    {command;};                            \
    *(DATA_TYPE *)__q_addr = value;

#else /* CONFIG_REPLAY */

#define TX(__q_addr, type, value, command) \
    DATA_TYPE __oldv;                                         \
    DATA_TYPE value;                                          \
                                                              \
    do {                                                      \
        __oldv = value = *(DATA_TYPE *)__q_addr;              \
        {command;};                                           \
        mb();                                                 \
    } while (__oldv != (glue(atomic_compare_exchange, SUFFIX)(        \
                    (DATA_TYPE *)__q_addr, __oldv, value)))

#endif


/* Atomically emulate INC instruction using CAS1 and memory transaction. */
void glue(helper_atomic_inc, SUFFIX)(target_ulong a0, int c)
{
    int eflags_c, eflags;
    int cc_op;

    unsigned long q_addr;
    CM_GET_QEMU_ADDR(q_addr, a0);
    /* compute the previous instruction c flags */
    eflags_c = helper_cc_compute_c(CC_OP);

#ifdef CONFIG_REPLAY
    memobj_t *mo = cm_start_atomic_insn((const void *)q_addr);
#endif
    TX(q_addr, type, value, {
        if (c > 0) {
            value++;
            cc_op = glue(CC_OP_INC, UPSUFFIX);
        } else {
            value--;
            cc_op = glue(CC_OP_DEC, UPSUFFIX);
        }
    });
#ifdef CONFIG_REPLAY
    cm_end_atomic_insn(mo, value);
#endif

    CC_SRC = eflags_c;
    CC_DST = value;

    eflags = helper_cc_compute_all(cc_op);
    CC_SRC = eflags;
}

void glue(helper_xchg, SUFFIX)(target_ulong a0, int reg, int hreg)
{
    DATA_TYPE val, out;
    unsigned long q_addr;

    CM_GET_QEMU_ADDR(q_addr, a0);
    val = (DATA_TYPE)cm_get_reg_val(OT, hreg, reg);
#ifdef CONFIG_REPLAY
    memobj_t *mo = cm_start_atomic_insn((const void *)q_addr);
#endif
    out = glue(atomic_exchange, SUFFIX)((DATA_TYPE *)q_addr, val);
    mb();
#ifdef CONFIG_REPLAY
    cm_end_atomic_insn(mo, val);
#endif

    cm_set_reg_val(OT, hreg, reg, out);
}

void glue(helper_atomic_op, SUFFIX)(target_ulong a0, target_ulong t1,
                       int op)
{
    DATA_TYPE operand;
    int eflags_c, eflags;
    int cc_op;

    unsigned long q_addr;
    CM_GET_QEMU_ADDR(q_addr, a0);
    /* compute the previous instruction c flags */
    eflags_c = helper_cc_compute_c(CC_OP);
    operand = (DATA_TYPE)t1;

#ifdef CONFIG_REPLAY
    memobj_t *mo = cm_start_atomic_insn((const void *)q_addr);
#endif
    TX(q_addr, type, value, {
        switch(op) {
        case OP_ADCL:
            value += operand + eflags_c;
            break;
        case OP_SBBL:
            value = value - operand - eflags_c;
            break;
        case OP_ADDL:
            value += operand;
            break;
        case OP_SUBL:
            value -= operand;
            break;
        default:
        case OP_ANDL:
            value &= operand;
            break;
        case OP_ORL:
            value |= operand;
            break;
        case OP_XORL:
            value ^= operand;
            break;
        case OP_CMPL:
            abort();
            break;
        }
    });
#ifdef CONFIG_REPLAY
    cm_end_atomic_insn(mo, value);
#endif
    switch(op) {
    case OP_ADCL:
        cc_op = glue(CC_OP_ADD, UPSUFFIX) + (eflags_c << 2);
        CC_SRC = operand;
        break;
    case OP_SBBL:
        cc_op = glue(CC_OP_SUB, UPSUFFIX) + (eflags_c << 2);
        CC_SRC = operand;
        break;
    case OP_ADDL:
        cc_op = glue(CC_OP_ADD, UPSUFFIX);
        CC_SRC = operand;
        break;
    case OP_SUBL:
        cc_op = glue(CC_OP_SUB, UPSUFFIX);
        CC_SRC = operand;
        break;
    default:
    case OP_ANDL:
        cc_op = glue(CC_OP_LOGIC, UPSUFFIX);
        break;
    case OP_ORL:
        cc_op = glue(CC_OP_LOGIC, UPSUFFIX);
        break;
    case OP_XORL:
        cc_op = glue(CC_OP_LOGIC, UPSUFFIX);
        break;
    case OP_CMPL:
        abort();
        break;
    }
    CC_DST = value;
    /* successful transaction, compute the eflags */
    eflags = helper_cc_compute_all(cc_op);
    CC_SRC = eflags;
}

void glue(helper_atomic_xadd, SUFFIX)(target_ulong a0, int reg,
                        int hreg)
{
    DATA_TYPE operand, oldv;
    int eflags;

    unsigned long q_addr;
    CM_GET_QEMU_ADDR(q_addr, a0);
    operand = (DATA_TYPE)cm_get_reg_val(
            OT, hreg, reg);

#ifdef CONFIG_REPLAY
    memobj_t *mo = cm_start_atomic_insn((const void *)q_addr);
#endif
    TX(q_addr, type, newv, {
        oldv = newv;
        newv += operand;
    });
#ifdef CONFIG_REPLAY
    cm_end_atomic_insn(mo, newv);
#endif

    /* transaction successes */
    /* xchg the register and compute the eflags */
    cm_set_reg_val(OT, hreg, reg, oldv);
    CC_SRC = oldv;
    CC_DST = newv;

    eflags = helper_cc_compute_all(glue(CC_OP_ADD, UPSUFFIX));
    CC_SRC = eflags;
}

void glue(helper_atomic_cmpxchg, SUFFIX)(target_ulong a0, int reg,
                            int hreg)
{
    DATA_TYPE reg_v, eax_v, res;
    int eflags;
    unsigned long q_addr;

    CM_GET_QEMU_ADDR(q_addr, a0);
    reg_v = (DATA_TYPE)cm_get_reg_val(OT, hreg, reg);
    eax_v = (DATA_TYPE)cm_get_reg_val(OT, 0, R_EAX);

#ifdef CONFIG_REPLAY
    memobj_t *mo = cm_start_atomic_insn((const void *)q_addr);
#endif
    res = glue(atomic_compare_exchange, SUFFIX)(
            (DATA_TYPE *)q_addr, eax_v, reg_v);
    mb();
#ifdef CONFIG_REPLAY
    cm_end_atomic_insn(mo, eax_v);
#endif

    if (res != eax_v)
        cm_set_reg_val(OT, 0, R_EAX, res);

    CC_SRC = res;
    CC_DST = eax_v - res;

    eflags = helper_cc_compute_all(glue(CC_OP_SUB, UPSUFFIX));
    CC_SRC = eflags;
}

void glue(helper_atomic_not, SUFFIX)(target_ulong a0)
{
    unsigned long q_addr;
    CM_GET_QEMU_ADDR(q_addr, a0);

#ifdef CONFIG_REPLAY
    memobj_t *mo = cm_start_atomic_insn((const void *)q_addr);
#endif
    TX(q_addr, type, value, {
        value = ~value;
    });
#ifdef CONFIG_REPLAY
    cm_end_atomic_insn(mo, value);
#endif
}

void glue(helper_atomic_neg, SUFFIX)(target_ulong a0)
{
    int eflags;

    unsigned long q_addr;
    CM_GET_QEMU_ADDR(q_addr, a0);
#ifdef CONFIG_REPLAY
    memobj_t *mo = cm_start_atomic_insn((const void *)q_addr);
#endif
    TX(q_addr, type, value, {
        value = -value;
    });
#ifdef CONFIG_REPLAY
    cm_end_atomic_insn(mo, value);
#endif

    /* We should use the old value to compute CC */
    CC_SRC = CC_DST = -value;

    eflags = helper_cc_compute_all(glue(CC_OP_SUB, UPSUFFIX));
    CC_SRC = eflags;
}

#undef DATA_TYPE
#undef SUFFIX
#undef UPSUFFIX
#undef LD
#undef OT
#undef DATA_BITS
