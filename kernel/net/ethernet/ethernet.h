// ============================
// GraceOS Network Stack
// ethernet.h - Ethernet Layer
// ============================

#ifndef GRACEOS_ETHERNET_H
#define GRACEOS_ETHERNET_H

#include "../../../lib/libc/int.h"

// ============================
// Ethernet Frame
// ============================

#define ETHERTYPE_ARP  0x0806
#define ETHERTYPE_IPv4 0x0800

#define ETHERNET_ADDR_LEN 6
#define ETHERNET_HEADER_LEN 14
#define ETHERNET_MAX_PAYLOAD 1500

struct ethernet_header {
    uint8_t  dst_mac[6];
    uint8_t  src_mac[6];
    uint16_t ethertype;   // Big-endian
} __attribute__((packed));

typedef char ethernet_header_size_must_be_14[
    (sizeof(struct ethernet_header) == ETHERNET_HEADER_LEN) ? 1 : -1
];

// ============================
// Frame Buffer (static, 1 frame)
// ============================

#define ETHERNET_FRAME_MAX (ETHERNET_HEADER_LEN + ETHERNET_MAX_PAYLOAD)

// Send an Ethernet frame
// ethertype is in host byte order; function handles byte-swap
int ethernet_send(const uint8_t *dst_mac, uint16_t ethertype,
                  const void *payload, size_t payload_len);

// Receive and dispatch one pending frame (non-blocking)
// Returns 1 if a frame was processed, 0 if nothing pending
int ethernet_poll(void);

// Broadcast MAC address
extern const uint8_t ethernet_broadcast[6];

#endif // GRACEOS_ETHERNET_H
