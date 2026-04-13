// ============================
// GraceOS Network Stack
// tcp.h - TCP Client Layer
// ============================

#ifndef GRACEOS_TCP_H
#define GRACEOS_TCP_H

#include "../../../lib/libc/int.h"

// ============================
// TCP States (client only)
// ============================

typedef enum {
    TCP_CLOSED = 0,
    TCP_SYN_SENT,
    TCP_ESTABLISHED,
    TCP_FIN_WAIT,
} tcp_state_t;

// ============================
// TCP Header
// ============================

#define TCP_FLAG_FIN (1 << 0)
#define TCP_FLAG_SYN (1 << 1)
#define TCP_FLAG_RST (1 << 2)
#define TCP_FLAG_PSH (1 << 3)
#define TCP_FLAG_ACK (1 << 4)

struct tcp_header {
    uint16_t src_port;  // Big-endian
    uint16_t dst_port;  // Big-endian
    uint32_t seq;       // Big-endian
    uint32_t ack_num;   // Big-endian
    uint8_t  data_off;  // Upper 4 bits = header length in 32-bit words
    uint8_t  flags;
    uint16_t window;    // Big-endian
    uint16_t checksum;  // Big-endian
    uint16_t urgent;    // Big-endian
} __attribute__((packed));

#define TCP_HEADER_LEN 20

// ============================
// TCP Connection
// ============================

#define TCP_RECV_BUF_SIZE (64 * 1024)

typedef struct {
    tcp_state_t  state;
    uint32_t     remote_ip;    // Host byte order
    uint16_t     remote_port;  // Host byte order
    uint16_t     local_port;   // Host byte order
    uint32_t     seq;          // Next send seq
    uint32_t     ack;          // Next expected remote seq
    uint32_t     remote_seq;   // Last acknowledged remote seq
    uint8_t      recv_buf[TCP_RECV_BUF_SIZE];
    size_t       recv_head;
    size_t       recv_tail;
    int          recv_closed;  // Remote sent FIN
} tcp_conn_t;

// ============================
// TCP API
// ============================

void tcp_init(void);

// Open a TCP connection to remote_ip:remote_port (host byte order).
// Blocks (polls) until ESTABLISHED or timeout.
// Returns 0 on success, -1 on failure.
int tcp_connect(tcp_conn_t *conn, uint32_t remote_ip, uint16_t remote_port);

// Send data on an established connection.
int tcp_send(tcp_conn_t *conn, const void *data, size_t len);

// Receive up to max_len bytes.  Non-blocking: returns 0 if nothing yet.
int tcp_recv(tcp_conn_t *conn, void *buf, size_t max_len);

// Block-poll until at least 1 byte arrives or connection closes.
// Returns number of bytes received, 0 on close.
int tcp_recv_wait(tcp_conn_t *conn, void *buf, size_t max_len);

// Close (send FIN, wait for FIN-ACK).
void tcp_close(tcp_conn_t *conn);

// Feed an incoming TCP segment to this connection (called from ipv4_receive).
void tcp_receive(tcp_conn_t *conn,
                 const struct tcp_header *hdr,
                 const void *payload, size_t payload_len);

// Poll the network device for incoming packets and dispatch them.
void tcp_poll(tcp_conn_t *conn);

#endif // GRACEOS_TCP_H
