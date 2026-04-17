#ifndef GRACEOS_ARM64_MAILBOX_H
#define GRACEOS_ARM64_MAILBOX_H

#include "../../../lib/libc/int.h"

/* ============================
   RPi Mailbox Protocol
   ============================

   Raspberry Pi uses a mailbox interface to communicate with the GPU firmware.
   - RPi 3: base = 0x3F00B880
   - RPi 4: base = 0xFE00B880

   Channel 8 (0x8) is used for property messages (tagged requests).
*/

#ifndef RPI_BASE
#define RPI_BASE 0x3F000000  /* Default to RPi 3 */
#endif

#define MAILBOX_BASE        (RPI_BASE + 0xB880)
#define MAILBOX_READ        (MAILBOX_BASE + 0x00)
#define MAILBOX_STATUS      (MAILBOX_BASE + 0x18)
#define MAILBOX_WRITE       (MAILBOX_BASE + 0x20)

#define MAILBOX_FULL        0x80000000
#define MAILBOX_EMPTY       0x40000000
#define MAILBOX_CHANNEL_8   0x8

/* Property message tags */
#define MBOX_TAG_END                    0x00000000
#define MBOX_TAG_GET_PHYSICAL_SIZE      0x00040003
#define MBOX_TAG_SET_PHYSICAL_SIZE      0x00048003
#define MBOX_TAG_GET_VIRTUAL_SIZE       0x00040004
#define MBOX_TAG_SET_VIRTUAL_SIZE       0x00048004
#define MBOX_TAG_GET_DEPTH              0x00040005
#define MBOX_TAG_SET_DEPTH              0x00048005
#define MBOX_TAG_GET_PIXEL_ORDER        0x00040006
#define MBOX_TAG_SET_PIXEL_ORDER        0x00048006
#define MBOX_TAG_GET_ALPHA_MODE         0x00040007
#define MBOX_TAG_SET_ALPHA_MODE         0x00048007
#define MBOX_TAG_GET_PITCH              0x00040008
#define MBOX_TAG_ALLOCATE_BUFFER        0x00040001
#define MBOX_TAG_RELEASE_BUFFER         0x00048001

/* Response codes */
#define MBOX_REQUEST        0x00000000
#define MBOX_RESPONSE_OK    0x80000000
#define MBOX_RESPONSE_ERR   0x80000001

/* Mailbox message structure (must be 16-byte aligned) */
typedef struct __attribute__((aligned(16))) {
    uint32_t size;          /* Buffer size in bytes (including this header) */
    uint32_t code;          /* Request = 0, Response = 0x80000000 */
    uint32_t tags[256];     /* Tag buffer (flexible, max 1KB message) */
} mbox_message_t;

/* ============================
   Mailbox API
   ============================ */

void mailbox_init(void);
int mailbox_call(mbox_message_t* msg);

#endif /* GRACEOS_ARM64_MAILBOX_H */
