// ============================
// GraceOS Network Stack
// ethernet.c - Ethernet Frame Layer
// ============================

#include "ethernet.h"
#include "../net.h"
#include "../arp/arp.h"
#include "../ip/ipv4.h"
#include "../../../lib/libc/string.h"
#include "../../../drivers/video/serial.h"

const uint8_t ethernet_broadcast[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

// Static TX buffer
static uint8_t tx_frame[ETHERNET_FRAME_MAX];

// Byte-swap helpers (network byte order is big-endian)
static inline uint16_t htons16(uint16_t h) {
    return (uint16_t)((h >> 8) | (h << 8));
}
static inline uint16_t ntohs16(uint16_t n) {
    return htons16(n);
}

static void net_log_ethertype(uint16_t etype)
{
    serial_write("[NET] RX frame type=");
    serial_hex((uint64_t)etype);
    serial_write("\n");
}

int ethernet_send(const uint8_t *dst_mac, uint16_t ethertype,
                  const void *payload, size_t payload_len)
{
    struct net_device *dev = net_get_device();
    if (!dev)
        return -1;
    if (payload_len > ETHERNET_MAX_PAYLOAD)
        return -1;

    struct ethernet_header *hdr = (struct ethernet_header *)tx_frame;
    memcpy(hdr->dst_mac, dst_mac, 6);
    memcpy(hdr->src_mac, dev->mac, 6);
    hdr->ethertype = htons16(ethertype);

    if (payload && payload_len > 0)
        memcpy(tx_frame + ETHERNET_HEADER_LEN, payload, payload_len);

    return dev->send(tx_frame, ETHERNET_HEADER_LEN + payload_len);
}

// Static RX buffer
static uint8_t rx_frame[ETHERNET_FRAME_MAX];

int ethernet_poll(void)
{
    struct net_device *dev = net_get_device();
    if (!dev)
        return 0;

    int len = dev->recv(rx_frame, sizeof(rx_frame));
    if (len <= 0)
        return 0;
    if ((size_t)len < ETHERNET_HEADER_LEN)
        return 0;

    struct ethernet_header *hdr = (struct ethernet_header *)rx_frame;
    uint16_t etype = ntohs16(hdr->ethertype);
    const void *payload = rx_frame + ETHERNET_HEADER_LEN;
    size_t payload_len = (size_t)len - ETHERNET_HEADER_LEN;

    net_log_ethertype(etype);

    switch (etype)
    {
        case ETHERTYPE_ARP:
            arp_receive(payload, payload_len);
            break;
        case ETHERTYPE_IPv4:
            ipv4_receive(payload, payload_len);
            break;
        default:
            break;
    }

    return 1;
}
