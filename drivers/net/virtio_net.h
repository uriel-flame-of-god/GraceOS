// ============================
// GraceOS virtio-net Driver
// drivers/net/virtio_net.h
// ============================

#ifndef GRACEOS_VIRTIO_NET_H
#define GRACEOS_VIRTIO_NET_H

#include "../../lib/libc/int.h"
#include "../../kernel/net/net.h"

// ============================
// virtio PCI IDs
// ============================

#define VIRTIO_VENDOR_ID   0x1AF4
#define VIRTIO_NET_DEVICE  0x1000

// virtio-net feature bits
#define VIRTIO_NET_F_MAC (1 << 5)

// virtio PCI register offsets (legacy interface, I/O space)
#define VIRTIO_PCI_HOST_FEATURES  0x00
#define VIRTIO_PCI_GUEST_FEATURES 0x04
#define VIRTIO_PCI_QUEUE_ADDR     0x08
#define VIRTIO_PCI_QUEUE_SIZE     0x0C
#define VIRTIO_PCI_QUEUE_SEL      0x0E
#define VIRTIO_PCI_QUEUE_NOTIFY   0x10
#define VIRTIO_PCI_STATUS         0x12
#define VIRTIO_PCI_ISR            0x13
#define VIRTIO_PCI_CONFIG         0x14    // device-specific config
#define VIRTIO_PCI_GUEST_PAGE_SIZE 0x28   // legacy interface page size (bytes)

// virtio device status bits
#define VIRTIO_STATUS_RESET       0x00
#define VIRTIO_STATUS_ACK         0x01
#define VIRTIO_STATUS_DRIVER      0x02
#define VIRTIO_STATUS_DRIVER_OK   0x04
#define VIRTIO_STATUS_FEATURES_OK 0x08
#define VIRTIO_STATUS_FAILED      0x80

// virtio-net header size
struct virtio_net_hdr {
    uint8_t  flags;
    uint8_t  gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
} __attribute__((packed));

#define VIRTIO_NET_HDR_SIZE ((uint16_t)sizeof(struct virtio_net_hdr))

// virtqueue descriptor flags
#define VRING_DESC_F_NEXT  1
#define VRING_DESC_F_WRITE 2

// ============================
// virtqueue sizes (power of 2)
// ============================

#define VRING_SIZE 256

struct vring_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

struct vring_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[VRING_SIZE];
} __attribute__((packed));

struct vring_used_elem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed));

struct vring_used {
    uint16_t flags;
    uint16_t idx;
    struct vring_used_elem ring[VRING_SIZE];
} __attribute__((packed));

// ============================
// API
// ============================

// Probe PCI bus for a virtio-net device and register it.
// Returns 1 if a device was found, 0 otherwise.
int virtio_net_init(void);

#endif // GRACEOS_VIRTIO_NET_H
