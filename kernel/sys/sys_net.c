// ============================
// GraceOS Networking Syscalls
// kernel/sys/sys_net.c
// ============================

#include "../../include/grace/net_syscalls.h"
#include "../include/syscall.h"
#include "../net/net.h"
#include "../net/http/http_client.h"
#include "../net/icmp/icmp.h"
#include "../../lib/libc/string.h"

// ============================
// SYS_NET_INIT (120)
// Reinitialise the network stack.
// Returns 0 on success, -1 if no device.
// ============================

long sys_net_init(long unused1, long unused2, long unused3,
                  long unused4, long unused5, long unused6)
{
    (void)unused1; (void)unused2; (void)unused3;
    (void)unused4; (void)unused5; (void)unused6;

    net_init();
    return net_ready() ? 0 : -1;
}

// ============================
// SYS_NET_STATUS (121)
// Returns 1 if network is ready, 0 otherwise.
// ============================

long sys_net_status(long unused1, long unused2, long unused3,
                    long unused4, long unused5, long unused6)
{
    (void)unused1; (void)unused2; (void)unused3;
    (void)unused4; (void)unused5; (void)unused6;

    return (long)net_ready();
}

// ============================
// SYS_HTTP_FETCH (122)
// arg1: const char *host
// arg2: const char *path
// arg3: void *buffer
// arg4: size_t max_size
// Returns number of bytes written into buffer, or -1.
// ============================

long sys_http_fetch(long host, long path, long buffer, long max_size,
                    long unused1, long unused2)
{
    (void)unused1; (void)unused2;

    return (long)http_fetch((const char *)host, (const char *)path,
                             (void *)buffer, (size_t)max_size);
}

// ============================
// SYS_HTTP_STREAM (123)
// arg1: const char *host
// arg2: const char *path
// arg3: function pointer  int (*write_cb)(const void*, size_t)
// Returns total bytes delivered, or -1.
// ============================

long sys_http_stream(long host, long path, long write_cb,
                     long unused1, long unused2, long unused3)
{
    (void)unused1; (void)unused2; (void)unused3;

    typedef int (*cb_t)(const void *, size_t);
    return (long)http_fetch_stream((const char *)host, (const char *)path,
                                   (cb_t)write_cb);
}

// ============================
// SYS_NET_GET_IP (124)
// Returns the local IP address in host byte order, or 0.
// ============================

long sys_net_get_ip(long unused1, long unused2, long unused3,
                    long unused4, long unused5, long unused6)
{
    (void)unused1; (void)unused2; (void)unused3;
    (void)unused4; (void)unused5; (void)unused6;

    struct net_device *dev = net_get_device();
    if (!dev)
        return 0;
    return (long)dev->ip;
}

// ============================
// SYS_ICMP_PING (125)
// arg1: uint32_t dst_ip  (host byte order)
// arg2: uint16_t id
// arg3: uint16_t seq
// arg4: uint32_t timeout_ms
// Returns RTT in ms, or -1 on timeout/error.
// ============================

long sys_icmp_ping(long dst_ip, long id, long seq, long timeout_ms,
                   long unused1, long unused2)
{
    (void)unused1; (void)unused2;
    return (long)icmp_ping_one((uint32_t)dst_ip, (uint16_t)id,
                               (uint16_t)seq, (uint32_t)timeout_ms);
}

// ============================
// SYS_NET_SET_IP (126)
// arg1: uint32_t ip       (host byte order)
// arg2: uint32_t gateway  (host byte order)
// arg3: uint32_t netmask  (host byte order)
// Returns 0 on success, -1 if no device.
// ============================

long sys_net_set_ip(long ip, long gateway, long netmask,
                    long unused1, long unused2, long unused3)
{
    (void)unused1; (void)unused2; (void)unused3;
    if (!net_get_device())
        return -1;
    net_set_ip((uint32_t)ip, (uint32_t)gateway, (uint32_t)netmask);
    return 0;
}

// ============================
// Registration
// ============================

void net_syscalls_init(syscall_fn_t *table)
{
    table[SYS_NET_INIT]    = (syscall_fn_t)sys_net_init;
    table[SYS_NET_STATUS]  = (syscall_fn_t)sys_net_status;
    table[SYS_HTTP_FETCH]  = (syscall_fn_t)sys_http_fetch;
    table[SYS_HTTP_STREAM] = (syscall_fn_t)sys_http_stream;
    table[SYS_NET_GET_IP]  = (syscall_fn_t)sys_net_get_ip;
    table[SYS_ICMP_PING]   = (syscall_fn_t)sys_icmp_ping;
    table[SYS_NET_SET_IP]  = (syscall_fn_t)sys_net_set_ip;
}
