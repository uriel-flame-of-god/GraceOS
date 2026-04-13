// ============================
// GraceOS Network Stack
// tcp.c - TCP Client Layer
// ============================

#include "tcp.h"
#include "../ip/ipv4.h"
#include "../ethernet/ethernet.h"
#include "../net.h"
#include "../../../lib/libc/string.h"
#include "../../../kernel/log/klog.h"
#include "../../include/time.h"

// ============================
// External reference
// ============================

// ipv4.c maintains a pointer to the current TCP connection for dispatch
extern tcp_conn_t *ipv4_active_tcp_conn;

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
// TCP checksum (pseudo-header)
// ============================

static uint16_t tcp_checksum(uint32_t src_ip, uint32_t dst_ip,
                              const void *tcp_seg, size_t tcp_len)
{
    uint32_t sum = 0;

    // Pseudo-header: src IP, dst IP, zero, protocol=6, TCP length
    sum += (src_ip >> 16) & 0xFFFF;
    sum += src_ip & 0xFFFF;
    sum += (dst_ip >> 16) & 0xFFFF;
    sum += dst_ip & 0xFFFF;
    // Pseudo-header fields are checksum words [0x0006, tcp_len].
    sum += (uint16_t)IP_PROTO_TCP;
    sum += (uint16_t)tcp_len;

    // TCP segment
    const uint8_t *buf = (const uint8_t *)tcp_seg;
    for (size_t i = 0; i + 1 < tcp_len; i += 2)
        sum += ((uint32_t)buf[i] << 8) | buf[i + 1];
    if (tcp_len & 1)
        sum += (uint32_t)buf[tcp_len - 1] << 8;

    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);

    return (uint16_t)(~sum);
}

// ============================
// ISN generator
// ============================

static uint32_t isn_seed = 0xdeadbeef;

static uint32_t generate_isn(uint32_t remote_ip, uint16_t remote_port, uint16_t local_port)
{
    /* Advance seed with Fibonacci-hashing constant, mix in 4-tuple */
    isn_seed += 0x9e3779b9u;
    uint32_t h = isn_seed ^ remote_ip
                           ^ ((uint32_t)remote_port << 16)
                           ^ (uint32_t)local_port;
    /* Bit-mixing to spread entropy */
    h ^= h >> 16;
    h *= 0x45d9f3bu;
    h ^= h >> 16;
    return h ? h : 1u;
}

// ============================
// Port counter
// ============================

static uint16_t ephemeral_port = 49152;

static uint16_t alloc_local_port(void)
{
    uint16_t p = ephemeral_port++;
    if (ephemeral_port == 0 || ephemeral_port > 65534)
        ephemeral_port = 49152;
    return p;
}

// ============================
// Static TX buffer
// ============================

static uint8_t tcp_tx_buf[TCP_HEADER_LEN + 1460];

static int tcp_send_segment(tcp_conn_t *conn, uint8_t flags,
                             const void *data, size_t data_len)
{
    struct net_device *dev = net_get_device();
    if (!dev)
        return -1;

    size_t seg_len = TCP_HEADER_LEN + data_len;
    struct tcp_header *hdr = (struct tcp_header *)tcp_tx_buf;

    hdr->src_port = htons16(conn->local_port);
    hdr->dst_port = htons16(conn->remote_port);
    hdr->seq      = htonl32(conn->seq);
    hdr->ack_num  = (flags & TCP_FLAG_ACK) ? htonl32(conn->ack) : 0;
    hdr->data_off = (TCP_HEADER_LEN / 4) << 4;
    hdr->flags    = flags;
    hdr->window   = htons16(4096);
    hdr->checksum = 0;
    hdr->urgent   = 0;

    if (data && data_len > 0)
        memcpy(tcp_tx_buf + TCP_HEADER_LEN, data, data_len);

    // tcp_checksum() expects IP integers in host order and derives 16-bit
    // words to match on-wire big-endian byte pairs.
    hdr->checksum = htons16(tcp_checksum(dev->ip, conn->remote_ip,
                                         tcp_tx_buf, seg_len));

    int rc = ipv4_send(conn->remote_ip, IP_PROTO_TCP, tcp_tx_buf, seg_len);
    if (rc < 0)
        klog_warn("tcp_send_segment: ipv4_send failed");
    return rc;
}

// ============================
// Public API
// ============================

void tcp_init(void)
{
    ephemeral_port = 49152;
}

// Receive is called from ipv4_receive -> tcp_receive
void tcp_receive(tcp_conn_t *conn,
                 const struct tcp_header *hdr,
                 const void *payload, size_t payload_len)
{
    if (!conn || !hdr)
        return;

    // Filter by port
    if (ntohs16(hdr->src_port) != conn->remote_port)
        return;
    if (ntohs16(hdr->dst_port) != conn->local_port)
        return;

    uint8_t flags = hdr->flags;
    uint32_t seg_seq = ntohl32(hdr->seq);
    uint32_t seg_ack = ntohl32(hdr->ack_num);

    switch (conn->state)
    {
        case TCP_SYN_SENT:
            // Expect SYN+ACK
            if ((flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) == (TCP_FLAG_SYN | TCP_FLAG_ACK))
            {
                if (seg_ack != conn->seq)
                {
                    klog_warn("tcp_receive: SYN-ACK ack mismatch");
                    return;  // Wrong ack
                }
                conn->ack = seg_seq + 1;
                conn->remote_seq = seg_seq;
                conn->state = TCP_ESTABLISHED;
                // Send ACK
                tcp_send_segment(conn, TCP_FLAG_ACK, NULL, 0);
            }
            else if (flags & TCP_FLAG_RST)
            {
                klog_warn("tcp_receive: RST in SYN_SENT");
                conn->state = TCP_CLOSED;
            }
            else
            {
                klog_warn("tcp_receive: unexpected flags in SYN_SENT");
            }
            break;

        case TCP_ESTABLISHED:
            if (flags & TCP_FLAG_RST)
            {
                conn->state = TCP_CLOSED;
                conn->recv_closed = 1;
                return;
            }

            // ACK validation
            if ((flags & TCP_FLAG_ACK) && seg_ack > conn->seq)
                conn->seq = seg_ack;  // Advance our send pointer (simplified)

            // Buffer incoming data
            if (payload_len > 0 && (flags & TCP_FLAG_ACK))
            {
                // Only accept if seq matches expected
                if (seg_seq == conn->ack)
                {
                    // Compute available space to prevent circular buffer overflow
                    size_t buf_used = (conn->recv_tail >= conn->recv_head)
                        ? conn->recv_tail - conn->recv_head
                        : TCP_RECV_BUF_SIZE - conn->recv_head + conn->recv_tail;
                    size_t buf_avail = TCP_RECV_BUF_SIZE - buf_used - 1;
                    size_t to_copy = (payload_len < buf_avail) ? payload_len : buf_avail;

                    conn->ack += (uint32_t)to_copy;

                    for (size_t i = 0; i < to_copy; i++)
                    {
                        conn->recv_buf[conn->recv_tail] = ((const uint8_t *)payload)[i];
                        conn->recv_tail = (conn->recv_tail + 1) % TCP_RECV_BUF_SIZE;
                    }

                    // Send ACK
                    tcp_send_segment(conn, TCP_FLAG_ACK, NULL, 0);
                }
            }

            if (flags & TCP_FLAG_FIN)
            {
                conn->ack++;
                conn->recv_closed = 1;
                tcp_send_segment(conn, TCP_FLAG_ACK, NULL, 0);
                conn->state = TCP_FIN_WAIT;
            }
            break;

        case TCP_FIN_WAIT:
            if (flags & TCP_FLAG_ACK)
                conn->state = TCP_CLOSED;
            break;

        default:
            break;
    }
}

// Poll the Ethernet layer for incoming frames targeting this connection
void tcp_poll(tcp_conn_t *conn)
{
    ipv4_active_tcp_conn = conn;
    // Drain all pending frames
    for (int i = 0; i < 64; i++)
    {
        if (!ethernet_poll())
            break;
    }
}

#define TCP_CONNECT_TIMEOUT_MS 5000
#define TCP_SYN_SEND_RETRIES   4

int tcp_connect(tcp_conn_t *conn, uint32_t remote_ip, uint16_t remote_port)
{
    struct net_device *dev = net_get_device();
    if (!dev)
        return -1;

    memset(conn, 0, sizeof(*conn));
    conn->state       = TCP_SYN_SENT;
    conn->remote_ip   = remote_ip;
    conn->remote_port = remote_port;
    conn->local_port  = alloc_local_port();
    conn->seq         = generate_isn(remote_ip, remote_port, conn->local_port);
    conn->ack         = 0;
    conn->recv_head   = 0;
    conn->recv_tail   = 0;
    conn->recv_closed = 0;

    // Register with IPv4 dispatch
    ipv4_active_tcp_conn = conn;

    // Send SYN with a few retries to absorb transient ARP/send races.
    int syn_sent = 0;
    for (int attempt = 0; attempt < TCP_SYN_SEND_RETRIES; attempt++)
    {
        if (tcp_send_segment(conn, TCP_FLAG_SYN, NULL, 0) >= 0)
        {
            syn_sent = 1;
            klog_log("tcp_connect: SYN sent");
            conn->seq++;  // SYN consumes one sequence number
            break;
        }

        // Let ARP replies and RX completions progress before retry.
        for (int i = 0; i < 64; i++)
            tcp_poll(conn);
    }

    if (!syn_sent)
    {
        klog_warn("tcp_connect: SYN send failed");
        conn->state = TCP_CLOSED;
        return -1;
    }

    // Wait for SYN-ACK
    uint64_t t0 = timer_get_ms();
    while ((timer_get_ms() - t0) < TCP_CONNECT_TIMEOUT_MS)
    {
        tcp_poll(conn);
        if (conn->state == TCP_ESTABLISHED)
        {
            klog_log("tcp_connect: established");
            return 0;
        }
        if (conn->state == TCP_CLOSED)
        {
            klog_warn("tcp_connect: closed during handshake");
            return -1;
        }
    }

    klog_warn("tcp_connect: SYN timeout");
    conn->state = TCP_CLOSED;
    return -1;
}

int tcp_send(tcp_conn_t *conn, const void *data, size_t len)
{
    if (!conn || conn->state != TCP_ESTABLISHED)
        return -1;

    int ret = tcp_send_segment(conn, TCP_FLAG_PSH | TCP_FLAG_ACK, data, len);
    if (ret >= 0)
        conn->seq += (uint32_t)len;
    return ret;
}

int tcp_recv(tcp_conn_t *conn, void *buf, size_t max_len)
{
    if (!conn)
        return -1;

    size_t count = 0;
    uint8_t *out = (uint8_t *)buf;

    while (count < max_len && conn->recv_head != conn->recv_tail)
    {
        out[count++] = conn->recv_buf[conn->recv_head];
        conn->recv_head = (conn->recv_head + 1) % TCP_RECV_BUF_SIZE;
    }

    return (int)count;
}

#define TCP_RECV_TIMEOUT_MS 10000

int tcp_recv_wait(tcp_conn_t *conn, void *buf, size_t max_len)
{
    uint64_t t0 = timer_get_ms();
    while ((timer_get_ms() - t0) < TCP_RECV_TIMEOUT_MS)
    {
        tcp_poll(conn);

        int n = tcp_recv(conn, buf, max_len);
        if (n > 0)
            return n;

        if (conn->recv_closed && conn->recv_head == conn->recv_tail)
            return 0;  // Connection closed, no more data
    }
    return 0;
}

void tcp_close(tcp_conn_t *conn)
{
    if (!conn || conn->state == TCP_CLOSED)
        return;

    conn->state = TCP_FIN_WAIT;
    tcp_send_segment(conn, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0);
    conn->seq++;

    // Wait briefly for FIN-ACK
    for (int i = 0; i < 100000; i++)
    {
        tcp_poll(conn);
        if (conn->state == TCP_CLOSED)
            break;
    }

    conn->state = TCP_CLOSED;
    ipv4_active_tcp_conn = NULL;
}
