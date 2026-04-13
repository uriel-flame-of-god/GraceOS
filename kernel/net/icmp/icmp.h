// ============================
// GraceOS Network Stack
// icmp.h - ICMP Layer
// ============================

#ifndef GRACEOS_ICMP_H
#define GRACEOS_ICMP_H

#include "../../../lib/libc/int.h"

// ============================
// ICMP Type Codes
// ============================

#define ICMP_TYPE_ECHO_REPLY   0
#define ICMP_TYPE_ECHO_REQUEST 8

// ============================
// ICMP Header
// ============================

struct icmp_header {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;  // Big-endian
    uint16_t id;        // Big-endian
    uint16_t seq;       // Big-endian
} __attribute__((packed));

#define ICMP_HEADER_LEN   8
#define ICMP_PAYLOAD_SIZE 32

// ============================
// ICMP API
// ============================

// Send an ICMP echo request to dst_ip (host byte order).
int icmp_send_echo(uint32_t dst_ip, uint16_t id, uint16_t seq);

// Feed an incoming ICMP packet to the ICMP layer.
// src_ip is in host byte order.  data/len cover the ICMP header + payload.
void icmp_receive(uint32_t src_ip, const void *data, size_t len);

// Send one ICMP echo and poll until a matching reply arrives or timeout.
// Returns RTT in ms on success, -1 on error or timeout.
int icmp_ping_one(uint32_t dst_ip, uint16_t id, uint16_t seq,
                  uint32_t timeout_ms);

#endif // GRACEOS_ICMP_H
