// ============================
// GraceOS Network Stack
// net.h - Network Device Interface
// ============================

#ifndef GRACEOS_NET_H
#define GRACEOS_NET_H

#include "../../lib/libc/int.h"

// ============================
// Network Device
// ============================

struct net_device {
    uint8_t  mac[6];
    uint32_t ip;         // Host byte order
    uint32_t gateway;    // Host byte order
    uint32_t netmask;    // Host byte order

    int (*send)(const void *data, size_t len);
    int (*recv)(void *buffer, size_t max_len);
};

// ============================
// Network Init
// ============================

// Initialize networking subsystem (detect device, set up stack)
void net_init(void);

// Returns 1 if network is available
int net_ready(void);

// Returns the active network device, or NULL
struct net_device *net_get_device(void);

// Register a network device
void net_register_device(struct net_device *dev);

// Set static IP configuration on the active device.
// All values in host byte order.
void net_set_ip(uint32_t ip, uint32_t gateway, uint32_t netmask);

#endif // GRACEOS_NET_H
