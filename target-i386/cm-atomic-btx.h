#if INS == 1
# define INSN bts
# define COMMAND value |= (1 << (offset & 0x7))
#elif INS == 2
# define INSN btr
# define COMMAND value &= ~(1 << (offset & 0x7))
#elif INS == 3
# define INSN btc
# define COMMAND value ^= (1 << (offset & 0x7))
#endif

void glue(helper_atomic_, INSN)(target_ulong a0, target_ulong offset,
        int ot)
{
    uint8_t old_byte;
    int eflags;

    unsigned long __q_addr;
    uint8_t value;

    CM_GET_QEMU_ADDR(__q_addr, a0);
    /* This is the address that's actually gets changed. */
    __q_addr += offset >> 3;
#if defined(CONFIG_MEM_ORDER) && !defined(NO_LOCK)
    CM_START_ATOMIC_INSN(__q_addr);
    /* Since we get a lock to the page, no other core can read/write this page.
     * So no transaction is needed. */
    old_byte = value = *((uint8_t *)__q_addr);
    {COMMAND;};
    *((uint8_t *)__q_addr) = value;
    CM_END_ATOMIC_INSN(value);
#else
    uint8_t __oldv;
    /* This is different from TX.
     * Note that, when using register bitoffset, the value can be larger than
     * operand size - 1 (operand size can be 16/32/64), refer to intel manual 2A
     * page 3-11. */
    do {
        __oldv = value = *((uint8_t *)__q_addr);
        old_byte = value;
        {COMMAND;};
        mb();
    } while (__oldv != atomic_compare_exchangeb((uint8_t *)__q_addr, __oldv, value));
#endif

    CC_SRC = (old_byte >> (offset & 0x7));
    CC_DST = 0;
    eflags = helper_cc_compute_all(CC_OP_SARB + ot);
    CC_SRC = eflags;
}

#undef INS
#undef INSN
#undef COMMAND
