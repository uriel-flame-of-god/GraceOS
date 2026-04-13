// ============================
// GraceOS Network Stack
// arp.h - ARP Layer
// ============================

#ifndef GRACEOS_ARP_H
#define GRACEOS_ARP_H

#include "../../../lib/libc/int.h"

// ============================
// ARP Packet
// ============================

#define ARP_HARDWARE_ETHERNET 1
#define ARP_PROTO_IPv4        0x0800
#define ARP_OP_REQUEST        1
#define ARP_OP_REPLY          2

struct arp_packet {
    uint16_t hw_type;        // Big-endian
    uint16_t proto_type;     // Big-endian
    uint8_t  hw_len;
    uint8_t  proto_len;
    uint16_t operation;      // Big-endian
    uint8_t  sender_mac[6];
    uint32_t sender_ip;      // Big-endian
    uint8_t  target_mac[6];
    uint32_t target_ip;      // Big-endian
} __attribute__((packed));

typedef char arp_packet_size_must_be_28[
    (sizeof(struct arp_packet) == 28) ? 1 : -1
];

// ============================
// ARP Cache
// ============================

#define ARP_CACHE_SIZE 32

struct arp_entry {
    uint32_t ip;        // Host byte order
    uint8_t  mac[6];
    uint64_t timestamp; // Tick count when inserted
    uint8_t  valid;
};

// ============================
// ARP API
// ============================

void arp_init(void);

// Send ARP request for the given IP (host byte order)
void arp_send_request(uint32_t target_ip);

// Process an incoming ARP packet (payload only, no Ethernet header)
void arp_receive(const void *data, size_t len);

// Look up MAC for an IP.  Returns 1 and fills mac[6] on hit, 0 on miss.
// On miss, automatically sends an ARP request.
int arp_lookup(uint32_t ip, uint8_t *mac_out);

#endif // GRACEOS_ARP_H
