// ============================
// GraceOS virtio-net Driver
// drivers/net/virtio_net.c
//
// Implements the legacy virtio-net PCI device interface.
// Provides raw Ethernet send/recv to the net stack.
// ============================

#include "virtio_net.h"
#include "../../kernel/net/net.h"
#include "../../kernel/arch/x86_64/io/port.h"
#include "../../kernel/mm/vmm/vmm.h"
#include "../../lib/libc/string.h"
#include "../../kernel/log/klog.h"
#include "../../kernel/mm/kheap.h"
#include "../video/serial.h"

// ============================
// PCI configuration space helpers
// ============================

#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

static uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset)
{
    uint32_t addr = (1u << 31)
                  | ((uint32_t)bus  << 16)
                  | ((uint32_t)dev  << 11)
                  | ((uint32_t)func <<  8)
                  | (offset & 0xFC);
    outl(PCI_CONFIG_ADDR, addr);
    return inl(PCI_CONFIG_DATA);
}

static void pci_write32(uint8_t bus, uint8_t dev, uint8_t func,
                        uint8_t offset, uint32_t value)
{
    uint32_t addr = (1u << 31)
                  | ((uint32_t)bus  << 16)
                  | ((uint32_t)dev  << 11)
                  | ((uint32_t)func <<  8)
                  | (offset & 0xFC);
    outl(PCI_CONFIG_ADDR, addr);
    outl(PCI_CONFIG_DATA, value);
}

// ============================
// virtio-net device state
// ============================

#define RX_QUEUE 0
#define TX_QUEUE 1

// One virtqueue: descriptor table + available ring + used ring
// Aligned to 4096 for DMA
struct virtqueue {
    struct vring_desc  desc[VRING_SIZE] __attribute__((aligned(16)));
    struct vring_avail avail            __attribute__((aligned(2)));
    // used ring must be 4K-aligned from queue base; for 256 descriptors
    // desc+avail crosses 4K, so used starts at 8K.
    uint8_t            _pad1[8192 - sizeof(struct vring_desc) * VRING_SIZE
                               - sizeof(struct vring_avail)];
    struct vring_used  used             __attribute__((aligned(4096)));
} __attribute__((packed, aligned(4096)));

#define NET_FRAME_BUF_SIZE (VIRTIO_NET_HDR_SIZE + 1514)

struct virtio_net_state {
    uint16_t io_base;
    uint8_t  mac[6];
    uint32_t ip;
    uint32_t gateway;
    uint32_t netmask;

    // RX virtqueue
    struct virtqueue rxq __attribute__((aligned(4096)));
    uint8_t rxbuf[VRING_SIZE][NET_FRAME_BUF_SIZE];
    uint16_t rx_last_used;

    // TX virtqueue
    struct virtqueue txq __attribute__((aligned(4096)));
    uint8_t txbuf[VRING_SIZE][NET_FRAME_BUF_SIZE];
    uint16_t tx_last_used;
};

// Static allocation (kernel BSS)
static struct virtio_net_state virtio_state;

static uint64_t dma_phys_addr(const void *ptr)
{
    uint64_t va = (uint64_t)(uintptr_t)ptr;
    uint64_t pa = vmm_translate(va);
    if (pa == 0)
    {
        if (va >= KERNEL_BASE)
            pa = va - KERNEL_BASE;
        else
            pa = va;
    }
    return pa;
}

static inline void virtio_mem_barrier(void)
{
    __asm__ volatile ("" ::: "memory");
}

// ============================
// virtio helpers
// ============================

static int vq_setup(uint16_t io_base, uint16_t queue_idx,
                    struct virtqueue *vq, uint16_t size)
{
    outw(io_base + VIRTIO_PCI_QUEUE_SEL, queue_idx);

    // Ensure device queue size matches our ring layout.
    uint16_t max_size = inw(io_base + VIRTIO_PCI_QUEUE_SIZE);
    if (max_size == 0)
    {
        klog_warn("virtio-net: queue unavailable");
        return 0;
    }
    if (size > max_size)
        size = max_size;
    outw(io_base + VIRTIO_PCI_QUEUE_SIZE, size);

    uint16_t active_size = inw(io_base + VIRTIO_PCI_QUEUE_SIZE);
    if (active_size != size)
        klog_warn("virtio-net: queue size mismatch");

    // Zero the queue
    memset(vq, 0, sizeof(*vq));

    // Set descriptors: pre-populate RX queue with write buffers
    (void)size;

    // Write physical address of virtqueue page.
    uint64_t phys = dma_phys_addr(vq);
    outl(io_base + VIRTIO_PCI_QUEUE_ADDR, (uint32_t)(phys >> 12));

    return 1;
}

static void virtio_prime_rx_queue(struct virtio_net_state *s)
{
    s->rxq.avail.flags = 0;
    s->rxq.avail.idx = 0;

    for (uint16_t i = 0; i < VRING_SIZE; i++)
    {
        s->rxq.desc[i].addr  = dma_phys_addr(s->rxbuf[i]);
        s->rxq.desc[i].len   = NET_FRAME_BUF_SIZE;
        s->rxq.desc[i].flags = VRING_DESC_F_WRITE;
        s->rxq.desc[i].next  = 0;

        s->rxq.avail.ring[i] = i;
    }

    virtio_mem_barrier();
    s->rxq.avail.idx = VRING_SIZE;

    outw(s->io_base + VIRTIO_PCI_QUEUE_NOTIFY, RX_QUEUE);
    serial_log("[NET] RX queue primed");
}

// ============================
// Send / Recv callbacks
// ============================

static int tx_kick_logged = 0;
static uint16_t tx_inflight = 0;

static void virtio_net_reclaim_tx(struct virtio_net_state *s)
{
    volatile struct vring_used *used = &s->txq.used;
    uint16_t used_idx_now = used->idx;

    while (s->tx_last_used != used_idx_now)
    {
        uint16_t ring_i = s->tx_last_used % VRING_SIZE;
        (void)used->ring[ring_i].id;
        s->tx_last_used++;
        if (tx_inflight > 0)
            tx_inflight--;
    }
}

static int virtio_net_send(const void *data, size_t len)
{
    struct virtio_net_state *s = &virtio_state;
    if (len > 1514)
        return -1;

    virtio_net_reclaim_tx(s);

    if (tx_inflight >= VRING_SIZE)
    {
        serial_log("[NET] TX queue full (no reclaimed descriptors)");
        return -1;
    }

    if (len >= 14)
    {
        const uint8_t *frame = (const uint8_t *)data;
        uint16_t etype = ((uint16_t)frame[12] << 8) | frame[13];
        serial_write("[NET] TX frame type=");
        serial_hex((uint64_t)etype);
        serial_write(" len=");
        serial_int((int64_t)len);
        serial_write("\n");
        if (etype == 0x0806)
            serial_log("[NET] Sending ARP request/reply");
    }
    else
    {
        serial_write("[NET] TX short frame len=");
        serial_int((int64_t)len);
        serial_write("\n");
    }

    // virtio-net header: all zeros (no offload)
    uint16_t desc_idx = s->txq.avail.idx % VRING_SIZE;
    struct virtio_net_hdr *vh = (struct virtio_net_hdr *)s->txbuf[desc_idx];
    memset(vh, 0, sizeof(*vh));

    memcpy(s->txbuf[desc_idx] + sizeof(*vh), data, len);

    size_t total = sizeof(*vh) + len;

    // Descriptor chain: single buffer
    s->txq.desc[desc_idx].addr  = dma_phys_addr(s->txbuf[desc_idx]);
    s->txq.desc[desc_idx].len   = (uint32_t)total;
    s->txq.desc[desc_idx].flags = 0;
    s->txq.desc[desc_idx].next  = 0;

    s->txq.avail.ring[s->txq.avail.idx % VRING_SIZE] = desc_idx;
    virtio_mem_barrier();
    s->txq.avail.idx++;
    tx_inflight++;

    // Notify device (queue 1 = TX)
    outw(s->io_base + VIRTIO_PCI_QUEUE_NOTIFY, TX_QUEUE);
    if (!tx_kick_logged)
    {
        serial_log("[NET] virtio TX notified (queue=1)");
        tx_kick_logged = 1;
    }

    virtio_net_reclaim_tx(s);

    return (int)len;
}

static int virtio_net_recv(void *buffer, size_t max_len)
{
    struct virtio_net_state *s = &virtio_state;
    volatile struct vring_used *used = &s->rxq.used;

    // Check if the device has placed a packet in the used ring
    uint16_t used_idx_now = used->idx;
    if (used_idx_now == s->rx_last_used)
        return 0;  // Nothing pending

    virtio_mem_barrier();

    uint16_t used_idx = s->rx_last_used % VRING_SIZE;
    uint32_t pkt_len  = used->ring[used_idx].len;
    uint32_t desc_id  = used->ring[used_idx].id;
    s->rx_last_used++;

    // Device should only return descriptors from our RX ring.
    if (desc_id >= VRING_SIZE)
        return 0;

    if (pkt_len <= VIRTIO_NET_HDR_SIZE)
        return 0;

    size_t frame_len = pkt_len - VIRTIO_NET_HDR_SIZE;
    if (frame_len > max_len)
        frame_len = max_len;

    // rxbuf[desc_id] holds [virtio_hdr | ethernet_frame]
    memcpy(buffer,
           s->rxbuf[desc_id % VRING_SIZE] + VIRTIO_NET_HDR_SIZE,
           frame_len);

        // Recycle the consumed descriptor itself back to RX avail.
        // This preserves descriptor/buffer ownership invariants.
        s->rxq.desc[desc_id].addr  = (uint64_t)(uintptr_t)s->rxbuf[desc_id];
        s->rxq.desc[desc_id].len   = NET_FRAME_BUF_SIZE;
        s->rxq.desc[desc_id].flags = VRING_DESC_F_WRITE;
        s->rxq.desc[desc_id].next  = 0;
        s->rxq.avail.ring[s->rxq.avail.idx % VRING_SIZE] = (uint16_t)desc_id;
    s->rxq.avail.idx++;

    // Notify device (queue 0 = RX)
    outw(s->io_base + VIRTIO_PCI_QUEUE_NOTIFY, RX_QUEUE);

    return (int)frame_len;
}

// ============================
// Static net_device instance
// ============================

static struct net_device virtio_net_dev;

static void serial_log_mac6(const char *prefix, const uint8_t mac[6])
{
    const char *hex = "0123456789ABCDEF";
    serial_write(prefix);
    for (int i = 0; i < 6; i++)
    {
        serial_putchar(hex[(mac[i] >> 4) & 0xF]);
        serial_putchar(hex[mac[i] & 0xF]);
        if (i < 5)
            serial_putchar(':');
    }
    serial_write("\n");
}

// ============================
// PCI probe
// ============================

static int scan_pci_for_virtio_net(uint8_t *out_bus, uint8_t *out_dev)
{
    for (uint16_t bus = 0; bus < 256; bus++)
    {
        for (uint8_t device = 0; device < 32; device++)
        {
            uint32_t id = pci_read32((uint8_t)bus, device, 0, 0);
            uint16_t vendor = (uint16_t)(id & 0xFFFF);
            uint16_t devid  = (uint16_t)(id >> 16);

            if (vendor == VIRTIO_VENDOR_ID && devid == VIRTIO_NET_DEVICE)
            {
                *out_bus = (uint8_t)bus;
                *out_dev = device;
                return 1;
            }
        }
    }
    return 0;
}

// ============================
// Static IP configuration
// (QEMU usermode networking defaults - RFC 3330 / QEMU SLIRP)
// ============================

#define QEMU_GUEST_IP   0x0A00020Fu   // 10.0.2.15
#define QEMU_GATEWAY    0x0A000202u   // 10.0.2.2  (QEMU host/gateway)
#define QEMU_NETMASK    0xFFFFFF00u   // 255.255.255.0

int virtio_net_init(void)
{
    uint8_t pci_bus = 0, pci_dev_num = 0;
    if (!scan_pci_for_virtio_net(&pci_bus, &pci_dev_num))
        return 0;

    // Read BAR0 to get I/O base
    uint32_t bar0 = pci_read32(pci_bus, pci_dev_num, 0, 0x10);

    // BAR0 must be I/O space (bit 0 set)
    if (!(bar0 & 1))
    {
        klog_warn("virtio-net: BAR0 is not I/O space");
        return 0;
    }

    uint16_t io_base = (uint16_t)(bar0 & ~0x3u);
    virtio_state.io_base = io_base;

    // Ensure PCI command register enables I/O + Bus Mastering for DMA.
    uint32_t cmdreg = pci_read32(pci_bus, pci_dev_num, 0, 0x04);
    uint16_t cmd = (uint16_t)(cmdreg & 0xFFFF);
    cmd |= 0x0001; // I/O space enable
    cmd |= 0x0004; // Bus master enable
    cmdreg = (cmdreg & 0xFFFF0000u) | cmd;
    pci_write32(pci_bus, pci_dev_num, 0, 0x04, cmdreg);

    serial_write("[NET] PCI CMD=");
    serial_hex((uint64_t)cmd);
    serial_write("\n");

    // Reset device
    outb(io_base + VIRTIO_PCI_STATUS, VIRTIO_STATUS_RESET);
    outb(io_base + VIRTIO_PCI_STATUS, VIRTIO_STATUS_ACK);
    outb(io_base + VIRTIO_PCI_STATUS, VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER);

    // Legacy virtio requires guest page size to interpret QUEUE_ADDR PFNs.
    outl(io_base + VIRTIO_PCI_GUEST_PAGE_SIZE, 4096);

    // Negotiate features: only request MAC
    uint32_t host_features = inl(io_base + VIRTIO_PCI_HOST_FEATURES);
    uint32_t guest_features = host_features & VIRTIO_NET_F_MAC;
    outl(io_base + VIRTIO_PCI_GUEST_FEATURES, guest_features);

    outb(io_base + VIRTIO_PCI_STATUS,
         VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK);
    uint8_t st = inb(io_base + VIRTIO_PCI_STATUS);
    if (!(st & VIRTIO_STATUS_FEATURES_OK))
    {
        klog_warn("virtio-net: FEATURES_OK rejected");
        return 0;
    }

    // Read MAC address (device-specific config at offset VIRTIO_PCI_CONFIG)
    for (int i = 0; i < 6; i++)
        virtio_state.mac[i] = inb(io_base + VIRTIO_PCI_CONFIG + i);

    serial_log_mac6("[NET] virtio MAC=", virtio_state.mac);

    // Set up virtqueues
    if (!vq_setup(io_base, RX_QUEUE, &virtio_state.rxq, VRING_SIZE))
    {
        klog_warn("virtio-net: RX queue setup failed");
        return 0;
    }

    if (!vq_setup(io_base, TX_QUEUE, &virtio_state.txq, VRING_SIZE))
    {
        klog_warn("virtio-net: TX queue setup failed");
        return 0;
    }

    virtio_state.rx_last_used = 0;
    virtio_state.tx_last_used = 0;
    tx_inflight = 0;

    // Driver ready
        outb(io_base + VIRTIO_PCI_STATUS,
            VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER |
            VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK);

    serial_write("[NET] virtio STATUS=");
    serial_hex((uint64_t)inb(io_base + VIRTIO_PCI_STATUS));
    serial_write("\n");

    // Pre-populate RX queue with write buffers only after DRIVER_OK.
    virtio_prime_rx_queue(&virtio_state);

    // Assign default IP configuration (QEMU usermode networking defaults)
    virtio_state.ip      = QEMU_GUEST_IP;
    virtio_state.gateway = QEMU_GATEWAY;
    virtio_state.netmask = QEMU_NETMASK;

    // Populate net_device struct
    memcpy(virtio_net_dev.mac, virtio_state.mac, 6);
    virtio_net_dev.ip      = virtio_state.ip;
    virtio_net_dev.gateway = virtio_state.gateway;
    virtio_net_dev.netmask = virtio_state.netmask;
    virtio_net_dev.send    = virtio_net_send;
    virtio_net_dev.recv    = virtio_net_recv;

    net_register_device(&virtio_net_dev);

    klog_log("virtio-net initialized");
    return 1;
}
