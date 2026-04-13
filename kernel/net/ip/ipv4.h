// ============================
// GraceOS Network Stack
// ipv4.h - IPv4 Layer
// ============================

#ifndef GRACEOS_IPV4_H
#define GRACEOS_IPV4_H

#include "../../../lib/libc/int.h"

// ============================
// IPv4 Header
// ============================

#define IP_PROTO_ICMP 1
#define IP_PROTO_TCP  6

struct ipv4_header {
    uint8_t  version_ihl;   // version(4) | IHL(4)
    uint8_t  tos;
    uint16_t length;        // Big-endian, total length including header
    uint16_t id;            // Big-endian
    uint16_t flags_offset;  // Big-endian
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum;      // Big-endian
    uint32_t src;           // Big-endian
    uint32_t dst;           // Big-endian
} __attribute__((packed));

#define IPv4_HEADER_LEN 20

// ============================
// IPv4 API
// ============================

void ipv4_init(void);

// Send an IPv4 packet.  dst_ip in host byte order.
int ipv4_send(uint32_t dst_ip, uint8_t protocol,
              const void *payload, size_t payload_len);

// Receive and dispatch an IPv4 packet (payload = whole IP datagram)
void ipv4_receive(const void *data, size_t len);

// Compute IPv4 checksum over len bytes
uint16_t ipv4_checksum(const void *data, size_t len);

#endif // GRACEOS_IPV4_H
