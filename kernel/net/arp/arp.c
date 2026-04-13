// ============================
// GraceOS Network Stack
// arp.c - ARP Layer
// ============================

#include "arp.h"
#include "../ethernet/ethernet.h"
#include "../net.h"
#include "../../../lib/libc/string.h"
#include "../../../kernel/log/klog.h"
#include "../../include/time.h"
#include "../../../drivers/video/serial.h"

// ============================
// ARP Cache
// ============================

#define ARP_ENTRY_TTL_MS 60000ULL  /* 60-second cache lifetime */

static struct arp_entry arp_cache[ARP_CACHE_SIZE];

// QEMU user-mode networking (SLiRP) fallback for gateway ARP failures
// Guest IP: 10.0.2.15, Gateway: 10.0.2.2, Gateway MAC: 52:55:0a:00:02:02
#define QEMU_SLIRP_GATEWAY_IP 0x0A000202u
static const uint8_t qemu_slirp_gateway_mac[6] = { 0x52, 0x55, 0x0A, 0x00, 0x02, 0x02 };

static inline uint16_t htons16(uint16_t h) { return (uint16_t)((h >> 8) | (h << 8)); }
static inline uint32_t htonl32(uint32_t h) {
    return ((h & 0xFF000000u) >> 24) |
           ((h & 0x00FF0000u) >>  8) |
           ((h & 0x0000FF00u) <<  8) |
           ((h & 0x000000FFu) << 24);
}
static inline uint32_t ntohl32(uint32_t n) { return htonl32(n); }

void arp_init(void)
{
    memset(arp_cache, 0, sizeof(arp_cache));
}

// Insert or update an entry in the ARP cache
static void arp_cache_insert(uint32_t ip, const uint8_t *mac)
{
    // Search for existing entry
    for (int i = 0; i < ARP_CACHE_SIZE; i++)
    {
        if (arp_cache[i].valid && arp_cache[i].ip == ip)
        {
            memcpy(arp_cache[i].mac, mac, 6);
            arp_cache[i].timestamp = timer_get_ms();
            return;
        }
    }

    // Find empty slot or oldest entry
    int oldest = 0;
    for (int i = 1; i < ARP_CACHE_SIZE; i++)
    {
        if (!arp_cache[i].valid)
        {
            oldest = i;
            break;
        }
        if (arp_cache[i].timestamp < arp_cache[oldest].timestamp)
            oldest = i;
    }

    arp_cache[oldest].ip = ip;
    memcpy(arp_cache[oldest].mac, mac, 6);
    arp_cache[oldest].timestamp = timer_get_ms();
    arp_cache[oldest].valid = 1;
}

void arp_send_request(uint32_t target_ip)
{
    struct net_device *dev = net_get_device();
    if (!dev)
        return;

    struct arp_packet pkt;
    pkt.hw_type    = htons16(ARP_HARDWARE_ETHERNET);
    pkt.proto_type = htons16(ARP_PROTO_IPv4);
    pkt.hw_len     = 6;
    pkt.proto_len  = 4;
    pkt.operation  = htons16(ARP_OP_REQUEST);

    memcpy(pkt.sender_mac, dev->mac, 6);
    pkt.sender_ip = htonl32(dev->ip);

    memset(pkt.target_mac, 0, 6);
    pkt.target_ip = htonl32(target_ip);

    serial_log("[NET] ARP request sent");
    ethernet_send(ethernet_broadcast, ETHERTYPE_ARP, &pkt, sizeof(pkt));
}

void arp_receive(const void *data, size_t len)
{
    if (len < sizeof(struct arp_packet))
        return;

    const struct arp_packet *pkt = (const struct arp_packet *)data;

    uint16_t hw_type    = htons16(pkt->hw_type);
    uint16_t proto_type = htons16(pkt->proto_type);
    uint16_t op         = htons16(pkt->operation);

    if (hw_type != ARP_HARDWARE_ETHERNET || proto_type != ARP_PROTO_IPv4)
        return;

    uint32_t sender_ip = ntohl32(pkt->sender_ip);

    // Cache the sender unconditionally
    arp_cache_insert(sender_ip, pkt->sender_mac);

    struct net_device *dev = net_get_device();
    if (!dev)
        return;

    // If this is a request for our IP, send a reply
    if (op == ARP_OP_REQUEST)
    {
        uint32_t target_ip = ntohl32(pkt->target_ip);
        if (target_ip != dev->ip)
            return;

        struct arp_packet reply;
        reply.hw_type    = htons16(ARP_HARDWARE_ETHERNET);
        reply.proto_type = htons16(ARP_PROTO_IPv4);
        reply.hw_len     = 6;
        reply.proto_len  = 4;
        reply.operation  = htons16(ARP_OP_REPLY);

        memcpy(reply.sender_mac, dev->mac, 6);
        reply.sender_ip = htonl32(dev->ip);
        memcpy(reply.target_mac, pkt->sender_mac, 6);
        reply.target_ip = pkt->sender_ip;

        serial_log("[NET] ARP request for local IP, sending reply");
        ethernet_send(pkt->sender_mac, ETHERTYPE_ARP, &reply, sizeof(reply));
    }
    else if (op == ARP_OP_REPLY)
    {
        serial_log("[NET] ARP reply received");
    }
}

int arp_lookup(uint32_t ip, uint8_t *mac_out)
{
    for (int i = 0; i < ARP_CACHE_SIZE; i++)
    {
        if (!arp_cache[i].valid || arp_cache[i].ip != ip)
            continue;
        // Expire entries older than ARP_ENTRY_TTL_MS
        if ((timer_get_ms() - arp_cache[i].timestamp) > ARP_ENTRY_TTL_MS)
        {
            arp_cache[i].valid = 0;
            break;
        }
        memcpy(mac_out, arp_cache[i].mac, 6);
        return 1;
    }

    // Not found or expired — send request
    arp_send_request(ip);

    // Give RX path a chance to process ARP replies before fallback.
    for (int i = 0; i < 256; i++)
        ethernet_poll();

    // Check cache again after polling
    for (int i = 0; i < ARP_CACHE_SIZE; i++)
    {
        if (arp_cache[i].valid && arp_cache[i].ip == ip)
        {
            memcpy(mac_out, arp_cache[i].mac, 6);
            return 1;
        }
    }

    // Fallback: if ARP replies are not received but we are using QEMU SLiRP,
    // seed the known gateway MAC so higher layers can continue.
    struct net_device *dev = net_get_device();
    if (dev && ip == dev->gateway && ip == QEMU_SLIRP_GATEWAY_IP)
    {
        arp_cache_insert(ip, qemu_slirp_gateway_mac);
        memcpy(mac_out, qemu_slirp_gateway_mac, 6);
        serial_log("[NET] ARP fallback: seeded QEMU gateway MAC");
        return 1;
    }

    return 0;
}
