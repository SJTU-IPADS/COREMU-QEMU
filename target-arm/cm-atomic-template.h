#if DATA_BITS == 32
#  define DATA_TYPE uint32_t
#  define SUFFIX l
#elif DATA_BITS == 16
#  define DATA_TYPE uint16_t
#  define SUFFIX w
#elif DATA_BITS == 8
#  define DATA_TYPE uint8_t
#  define SUFFIX b
#else
#error unsupported data size
#endif

void HELPER(glue(load_exclusive, SUFFIX))(uint32_t reg, uint32_t addr)
{
    ram_addr_t q_addr = 0;
    DATA_TYPE val = 0;

    cm_exclusive_addr = addr;
    CM_GET_QEMU_ADDR(q_addr,addr);
    val = *(DATA_TYPE *)q_addr;
    cm_exclusive_val = val;
    cpu_single_env->regs[reg] = val;
}

void HELPER(glue(store_exclusive, SUFFIX))(uint32_t res, uint32_t reg, uint32_t addr)
{
    ram_addr_t q_addr = 0;
    DATA_TYPE val = 0;
    DATA_TYPE r = 0;

    if(addr != cm_exclusive_addr)
        goto fail;

    CM_GET_QEMU_ADDR(q_addr,addr);
    val = (DATA_TYPE)cpu_single_env->regs[reg];

    r = glue(atomic_compare_exchange, SUFFIX)((DATA_TYPE *)q_addr,
                                    (DATA_TYPE)cm_exclusive_val, val);

    if(r == (DATA_TYPE)cm_exclusive_val) {
        cpu_single_env->regs[res] = 0;
        goto done;
    } else {
        goto fail;
    }

fail:
    cpu_single_env->regs[res] = 1;

done:
    cm_exclusive_addr = -1;
    return;
}

#undef DATA_TYPE
#undef SUFFIX
#undef DATA_BITS
