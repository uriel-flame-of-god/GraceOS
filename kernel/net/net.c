// ============================
// GraceOS Network Stack
// net.c - Network Device Registry & Initialization
// ============================

#include "net.h"
#include "ethernet/ethernet.h"
#include "arp/arp.h"
#include "ip/ipv4.h"
#include "tcp/tcp.h"
#include "../../drivers/net/virtio_net.h"
#include "../../drivers/video/tty.h"
#include "../log/klog.h"

static struct net_device *active_device = NULL;
static int net_initialized = 0;

void net_register_device(struct net_device *dev)
{
    if (!dev)
        return;
    active_device = dev;
}

struct net_device *net_get_device(void)
{
    return active_device;
}

int net_ready(void)
{
    return net_initialized && (active_device != NULL);
}

void net_set_ip(uint32_t ip, uint32_t gateway, uint32_t netmask)
{
    if (!active_device)
        return;
    active_device->ip      = ip;
    active_device->gateway = gateway;
    active_device->netmask = netmask;
}

void net_init(void)
{
    tty_set_color(TTY_CYAN, TTY_BLACK);
    tty_print("[INIT]    ");
    tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
    tty_print("Initializing network stack\n");

    // Try virtio-net
    if (virtio_net_init())
    {
        klog_log("virtio-net device found");
    }
    else
    {
        klog_warn("No network device found");
        return;
    }

    arp_init();
    ipv4_init();
    tcp_init();

    // BSD-style proactive neighbor resolution: warm ARP for default gateway.
    if (active_device && active_device->gateway != 0)
    {
        uint8_t gw_mac[6];
        int resolved = 0;
        for (int tries = 0; tries < 128; tries++)
        {
            if (arp_lookup(active_device->gateway, gw_mac))
            {
                resolved = 1;
                break;
            }
            for (int i = 0; i < 32; i++)
                ethernet_poll();
        }
        if (resolved)
            klog_log("net: gateway ARP warmup resolved");
        else
            klog_warn("net: gateway ARP warmup unresolved");
    }

    net_initialized = 1;

    tty_set_color(TTY_GREEN, TTY_BLACK);
    tty_print("[SUCCESS] ");
    tty_set_color(TTY_LIGHT_GREY, TTY_BLACK);
    tty_print("Network stack ready\n");
}
