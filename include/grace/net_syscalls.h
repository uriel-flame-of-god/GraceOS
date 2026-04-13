// ============================
// GraceOS Networking Syscalls
// include/grace/net_syscalls.h
// ============================

#ifndef GRACEOS_NET_SYSCALLS_H
#define GRACEOS_NET_SYSCALLS_H

// ============================
// Networking Syscall Numbers
// Starting at 120 per spec
// ============================

#define SYS_NET_INIT    120
#define SYS_NET_STATUS  121
#define SYS_HTTP_FETCH  122
#define SYS_HTTP_STREAM 123
#define SYS_NET_GET_IP  124
#define SYS_ICMP_PING   125
#define SYS_NET_SET_IP  126

#endif // GRACEOS_NET_SYSCALLS_H
