#ifdef ARCH_ARM64

#include "mailbox.h"
#include "io/port.h"

/* ============================
   Mailbox Communication
   ============================ */

void mailbox_init(void)
{
    /* Nothing special to init for basic mailbox usage */
}

/**
 * Send a message via mailbox and wait for response.
 * Returns 0 on success, -1 on timeout or error.
 */
int mailbox_call(mbox_message_t* msg)
{
    if (!msg)
        return -1;

    uint32_t addr = (uint32_t)((uint64_t)msg & 0xFFFFFFF0);
    /* Convert ARM physical address to GPU bus address */
    addr = (addr & 0x3FFFFFFF) | 0xC0000000;
    addr |= MAILBOX_CHANNEL_8;

    /* Wait until mailbox is not full */
    int timeout = 100000;
    while ((mmio_read32(MAILBOX_STATUS) & MAILBOX_FULL) && timeout-- > 0)
        ;
    if (timeout <= 0)
        return -1;

    /* Send the message */
    mmio_write32(MAILBOX_WRITE, addr);

    /* Wait for response */
    timeout = 100000;
    uint32_t response;
    while (timeout-- > 0) {
        while ((mmio_read32(MAILBOX_STATUS) & MAILBOX_EMPTY))
            ;
        response = mmio_read32(MAILBOX_READ);

        /* Check if this is our response (channel bits match) */
        if ((response & 0xF) == MAILBOX_CHANNEL_8)
            break;
    }

    if (timeout <= 0)
        return -1;

    /* Check response code */
    if (msg->code != MBOX_RESPONSE_OK)
        return -1;

    return 0;
}

#endif /* ARCH_ARM64 */
