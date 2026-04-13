// ============================
// GraceOS Network Stack
// ipv4.c - IPv4 Layer
// ============================

#include "ipv4.h"
#include "../ethernet/ethernet.h"
#include "../arp/arp.h"
#include "../net.h"
#include "../tcp/tcp.h"
#include "../icmp/icmp.h"
#include "../../../kernel/log/klog.h"
#include "../../../lib/libc/string.h"
#include "../../../drivers/video/serial.h"

// ============================
// Byte-order helpers
// ============================

static inline uint16_t htons16(uint16_t h) { return (uint16_t)((h >> 8) | (h << 8)); }
static inline uint16_t ntohs16(uint16_t n) { return htons16(n); }
static inline uint32_t htonl32(uint32_t h) {
    return ((h & 0xFF000000u) >> 24) |
           ((h & 0x00FF0000u) >>  8) |
           ((h & 0x0000FF00u) <<  8) |
           ((h & 0x000000FFu) << 24);
}
static inline uint32_t ntohl32(uint32_t n) { return htonl32(n); }

// ============================
// IP ID counter
// ============================

static uint16_t ip_id_counter = 1;

// ============================
// Active TCP connection
// (single connection, single-threaded)
// ============================

// The single active TCP connection pointer (set externally via tcp_poll / tcp_connect)
tcp_conn_t *ipv4_active_tcp_conn = NULL;

// ============================
// Implementation
// ============================

void ipv4_init(void)
{
    ip_id_counter = 1;
}

uint16_t ipv4_checksum(const void *data, size_t len)
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

// Static TX buffer for one IPv4 packet
static uint8_t ipv4_tx_buf[IPv4_HEADER_LEN + 1500];

#define IPV4_ARP_RETRIES 128

int ipv4_send(uint32_t dst_ip, uint8_t protocol,
              const void *payload, size_t payload_len)
{
    struct net_device *dev = net_get_device();
    if (!dev)
        return -1;
    if (payload_len > 1500)
        return -1;

    // Decide next-hop: if on same subnet, use dst directly; else use gateway
    uint32_t next_hop;
    if ((dst_ip & dev->netmask) == (dev->ip & dev->netmask))
        next_hop = dst_ip;
    else
        next_hop = dev->gateway;

    // ARP lookup for next-hop MAC (bounded poll/retry so callers like TCP
    // don't fail on a single cache miss).
    uint8_t dst_mac[6];
    int resolved = 0;
    for (int tries = 0; tries < IPV4_ARP_RETRIES; tries++)
    {
        if (arp_lookup(next_hop, dst_mac))
        {
            resolved = 1;
            break;
        }
        for (int i = 0; i < 16; i++)
            ethernet_poll();
    }
    if (!resolved)
    {
        klog_warn("ipv4_send: ARP unresolved for next hop");
        return -1;
    }

    // Build the IP header
    struct ipv4_header *hdr = (struct ipv4_header *)ipv4_tx_buf;
    hdr->version_ihl  = 0x45;   // IPv4, 20-byte header
    hdr->tos          = 0;
    hdr->length       = htons16((uint16_t)(IPv4_HEADER_LEN + payload_len));
    hdr->id           = htons16(ip_id_counter++);
    hdr->flags_offset = 0;
    hdr->ttl          = 64;
    hdr->protocol     = protocol;
    hdr->checksum     = 0;
    hdr->src          = htonl32(dev->ip);
    hdr->dst          = htonl32(dst_ip);
    hdr->checksum     = htons16(ipv4_checksum(hdr, IPv4_HEADER_LEN));

    if (payload && payload_len > 0)
        memcpy(ipv4_tx_buf + IPv4_HEADER_LEN, payload, payload_len);

    return ethernet_send(dst_mac, ETHERTYPE_IPv4,
                         ipv4_tx_buf, IPv4_HEADER_LEN + payload_len);
}

void ipv4_receive(const void *data, size_t len)
{
    if (len < IPv4_HEADER_LEN)
        return;

    const struct ipv4_header *hdr = (const struct ipv4_header *)data;
    struct net_device *dev = net_get_device();
    if (!dev)
        return;

    // Only support IPv4
    if ((hdr->version_ihl >> 4) != 4)
        return;

    uint8_t ihl = (hdr->version_ihl & 0x0F) * 4;
    if (ihl < IPv4_HEADER_LEN || (size_t)ihl > len)
        return;
    if (ihl != IPv4_HEADER_LEN)
        return;

    uint16_t total_len = ntohs16(hdr->length);
    if (total_len < ihl || total_len > len)
        return;

    // Validate header checksum — result must be 0 for a correct packet
    if (ipv4_checksum(hdr, ihl) != 0)
        return;

    const void *payload     = (const uint8_t *)data + ihl;
    size_t      payload_len = (size_t)total_len - ihl;

    // Convert src/dst from network to host byte order for upper layers
    uint32_t src_host = htonl32(hdr->src);

    switch (hdr->protocol)
    {
        case IP_PROTO_ICMP:
            icmp_receive(src_host, payload, payload_len);
            break;

        case IP_PROTO_TCP:
            if (ipv4_active_tcp_conn)
            {
                if (payload_len < TCP_HEADER_LEN)
                    break;
                const struct tcp_header *tcp_hdr =
                    (const struct tcp_header *)payload;
                // Respect the actual TCP header length (may include options)
                uint8_t tcp_hdr_len = (uint8_t)((tcp_hdr->data_off >> 4) * 4);
                if (tcp_hdr_len < TCP_HEADER_LEN || (size_t)tcp_hdr_len > payload_len)
                    break;
                const void *tcp_payload =
                    (const uint8_t *)payload + tcp_hdr_len;
                size_t tcp_payload_len = payload_len - (size_t)tcp_hdr_len;
                tcp_receive(ipv4_active_tcp_conn, tcp_hdr,
                            tcp_payload, tcp_payload_len);
            }
            break;

        default:
            break;
    }
}
