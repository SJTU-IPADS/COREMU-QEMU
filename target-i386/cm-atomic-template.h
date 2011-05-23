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
#define TX_TMP(vaddr, type, value, command) \
    unsigned long __q_addr;                                   \
    DATA_TYPE __oldv;                                         \
    DATA_TYPE value;                                          \
                                                              \
    CM_GET_QEMU_ADDR(__q_addr, vaddr);                        \
    do {                                                      \
        __oldv = value = LD((DATA_TYPE *)__q_addr);           \
        {command;};                                           \
        mb();                                                 \
    } while (__oldv != (glue(atomic_compare_exchange, SUFFIX)(        \
                    (DATA_TYPE *)__q_addr, __oldv, value)))

/* Atomically emulate INC instruction using CAS1 and memory transaction. */
void glue(helper_atomic_inc, SUFFIX)(target_ulong a0, int c)
{
    int eflags_c, eflags;
    int cc_op;

    /* compute the previous instruction c flags */
    eflags_c = helper_cc_compute_c(CC_OP);

    TX_TMP(a0, type, value, {
        if (c > 0) {
            value++;
            cc_op = glue(CC_OP_INC, UPSUFFIX);
        } else {
            value--;
            cc_op = glue(CC_OP_DEC, UPSUFFIX);
        }
    });

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
    out = glue(atomic_exchange, SUFFIX)((DATA_TYPE *)q_addr, val);
    mb();

    cm_set_reg_val(OT, hreg, reg, out);
}

void glue(helper_atomic_op, SUFFIX)(target_ulong a0, target_ulong t1,
                       int op)
{
    DATA_TYPE operand;
    int eflags_c, eflags;
    int cc_op;

    /* compute the previous instruction c flags */
    eflags_c = helper_cc_compute_c(CC_OP);
    operand = (DATA_TYPE)t1;

    TX_TMP(a0, type, value, {
        switch(op) {
        case OP_ADCL:
            value += operand + eflags_c;
            cc_op = glue(CC_OP_ADD, UPSUFFIX) + (eflags_c << 2);
            CC_SRC = operand;
            break;
        case OP_SBBL:
            value = value - operand - eflags_c;
            cc_op = glue(CC_OP_SUB, UPSUFFIX) + (eflags_c << 2);
            CC_SRC = operand;
            break;
        case OP_ADDL:
            value += operand;
            cc_op = glue(CC_OP_ADD, UPSUFFIX);
            CC_SRC = operand;
            break;
        case OP_SUBL:
            value -= operand;
            cc_op = glue(CC_OP_SUB, UPSUFFIX);
            CC_SRC = operand;
            break;
        default:
        case OP_ANDL:
            value &= operand;
            cc_op = glue(CC_OP_LOGIC, UPSUFFIX);
            break;
        case OP_ORL:
            value |= operand;
            cc_op = glue(CC_OP_LOGIC, UPSUFFIX);
            break;
        case OP_XORL:
            value ^= operand;
            cc_op = glue(CC_OP_LOGIC, UPSUFFIX);
            break;
        case OP_CMPL:
            abort();
            break;
        }
    });
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

    operand = (DATA_TYPE)cm_get_reg_val(
            OT, hreg, reg);

    TX_TMP(a0, type, newv, {
        oldv = newv;
        newv += operand;
    });

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

    res = glue(atomic_compare_exchange, SUFFIX)(
            (DATA_TYPE *)q_addr, eax_v, reg_v);
    mb();

    if (res != eax_v)
        cm_set_reg_val(OT, 0, R_EAX, res);

    CC_SRC = res;
    CC_DST = eax_v - res;

    eflags = helper_cc_compute_all(glue(CC_OP_SUB, UPSUFFIX));
    CC_SRC = eflags;
}

void glue(helper_atomic_not, SUFFIX)(target_ulong a0)
{
    TX_TMP(a0, type, value, {
        value = ~value;
    });
}

void glue(helper_atomic_neg, SUFFIX)(target_ulong a0)
{
    int eflags;

    TX_TMP(a0, type, value, {
        value = -value;
    });

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
