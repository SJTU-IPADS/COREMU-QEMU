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

    unsigned long q_addr;
    uint8_t __oldv;
    uint8_t value;

    CM_GET_QEMU_ADDR(q_addr, a0);
#ifdef CONFIG_REPLAY
    memobj_t *mo = cm_start_atomic_insn((const void *)q_addr);
#endif

    /* This is different from TX.
     * Note that, when using register bitoffset, the value can be larger than
     * operand size - 1 (operand size can be 16/32/64), refer to intel manual 2A
     * page 3-11. */
    q_addr += offset >> 3;
    do {
        __oldv = value = ldub_raw((uint8_t *)q_addr);
        old_byte = value;
        {COMMAND;};
        mb();
    } while (__oldv != atomic_compare_exchangeb((uint8_t *)q_addr, __oldv, value));

    CC_SRC = (old_byte >> (offset & 0x7));
    CC_DST = 0;
    eflags = helper_cc_compute_all(CC_OP_SARB + ot);
    CC_SRC = eflags;

#ifdef CONFIG_REPLAY
    cm_end_atomic_insn(mo);
#endif
}

#undef INS
#undef INSN
#undef COMMAND
