// ============================
// GraceOS Network Stack
// icmp.c - ICMP Layer
// ============================

#include "icmp.h"
#include "../ip/ipv4.h"
#include "../ethernet/ethernet.h"
#include "../arp/arp.h"
#include "../net.h"
#include "../../../lib/libc/string.h"
#include "../../log/klog.h"
#include "../../include/time.h"

// ============================
// Byte-order helpers
// ============================

static inline uint16_t htons16(uint16_t h) { return (uint16_t)((h >> 8) | (h << 8)); }
static inline uint16_t ntohs16(uint16_t n) { return htons16(n); }

// ============================
// ICMP checksum
// ============================

static uint16_t icmp_checksum(const void *data, size_t len)
{
    const uint8_t *buf = (const uint8_t *)data;
    uint32_t sum = 0;

    for (size_t i = 0; i + 1 < len; i += 2)
        sum += ((uint32_t)buf[i] << 8) | buf[i + 1];

    if (len & 1)
        sum += (uint32_t)buf[len - 1] << 8;

    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);

    return (uint16_t)(~sum);
}

// ============================
// Ping reply capture state
// ============================

static int      icmp_reply_received = 0;
static uint16_t icmp_expected_id    = 0;
static uint16_t icmp_expected_seq   = 0;

// ============================
// Static TX buffer
// ============================

static uint8_t icmp_tx_buf[ICMP_HEADER_LEN + ICMP_PAYLOAD_SIZE];

// ============================
// icmp_send_echo
// ============================

int icmp_send_echo(uint32_t dst_ip, uint16_t id, uint16_t seq)
{
    struct icmp_header *hdr = (struct icmp_header *)icmp_tx_buf;
    hdr->type     = ICMP_TYPE_ECHO_REQUEST;
    hdr->code     = 0;
    hdr->checksum = 0;
    hdr->id       = htons16(id);
    hdr->seq      = htons16(seq);

    // Fill payload with a simple pattern
    uint8_t *pay = icmp_tx_buf + ICMP_HEADER_LEN;
    for (int i = 0; i < ICMP_PAYLOAD_SIZE; i++)
        pay[i] = (uint8_t)(i & 0xFF);

    hdr->checksum = htons16(icmp_checksum(icmp_tx_buf,
                                          ICMP_HEADER_LEN + ICMP_PAYLOAD_SIZE));

    return ipv4_send(dst_ip, IP_PROTO_ICMP,
                     icmp_tx_buf, ICMP_HEADER_LEN + ICMP_PAYLOAD_SIZE);
}

// ============================
// icmp_receive
// Called from ipv4_receive() for protocol == IP_PROTO_ICMP
// ============================

// Static reply buffer (auto-reply to inbound echo requests)
static uint8_t icmp_reply_buf[ICMP_HEADER_LEN + 1480];

void icmp_receive(uint32_t src_ip, const void *data, size_t len)
{
    if (len < ICMP_HEADER_LEN)
        return;

    const struct icmp_header *hdr = (const struct icmp_header *)data;

    if (hdr->type == ICMP_TYPE_ECHO_REPLY)
    {
        // Check if this is the reply we are waiting for
        if (ntohs16(hdr->id)  == icmp_expected_id &&
            ntohs16(hdr->seq) == icmp_expected_seq)
        {
            icmp_reply_received = 1;
        }
    }
    else if (hdr->type == ICMP_TYPE_ECHO_REQUEST)
    {
        // Auto-reply: reflect the echo back as an echo reply
        if (len > sizeof(icmp_reply_buf))
            len = sizeof(icmp_reply_buf);

        memcpy(icmp_reply_buf, data, len);

        struct icmp_header *rply = (struct icmp_header *)icmp_reply_buf;
        rply->type     = ICMP_TYPE_ECHO_REPLY;
        rply->checksum = 0;
        rply->checksum = htons16(icmp_checksum(icmp_reply_buf, len));

        ipv4_send(src_ip, IP_PROTO_ICMP, icmp_reply_buf, len);
    }
}

// ============================
// icmp_ping_one
// ============================

#define PING_ARP_RETRIES 200

int icmp_ping_one(uint32_t dst_ip, uint16_t id, uint16_t seq,
                  uint32_t timeout_ms)
{
    struct net_device *dev = net_get_device();
    if (!dev)
        return -1;

    // Determine next-hop for ARP
    uint32_t next_hop = ((dst_ip & dev->netmask) == (dev->ip & dev->netmask))
                        ? dst_ip : dev->gateway;

    // ARP warm-up
    uint8_t mac[6];
    int arp_tries = PING_ARP_RETRIES;
    while (arp_tries-- > 0)
    {
        if (arp_lookup(next_hop, mac))
            break;
        for (int j = 0; j < 32; j++)
            ethernet_poll();
    }
    if (!arp_lookup(next_hop, mac))
    {
        klog_warn("icmp_ping_one: ARP resolution failed");
        return -1;
    }

    // Arm the reply capture
    icmp_expected_id  = id;
    icmp_expected_seq = seq;
    icmp_reply_received = 0;

    uint64_t t0 = timer_get_ms();

    if (icmp_send_echo(dst_ip, id, seq) < 0)
        return -1;

    // Poll until reply or timeout
    while ((timer_get_ms() - t0) < (uint64_t)timeout_ms)
    {
        ethernet_poll();
        if (icmp_reply_received)
            return (int)(timer_get_ms() - t0);
    }

    return -1;  // Timeout
}
