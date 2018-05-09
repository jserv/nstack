/**
 * nstack sockets.
 * @addtogroup Socket
 * @{
 */

#ifndef NSTACK_SOCKET_H
#define NSTACK_SOCKET_H

#include <stdint.h>
#include <sys/types.h>

#include "linker_set.h"
#include "queue_r.h"
#include "nstack_in.h"

#define NSTACK_SHMEM_SIZE                                            \
    (sizeof(struct nstack_sock_ctrl) + 2 * sizeof(struct queue_cb) + \
     2 * NSTACK_DATAGRAM_BUF_SIZE)

#define NSTACK_SOCK_CTRL(x) ((struct nstack_sock_ctrl *) (x))

#define NSTACK_INGRESS_QADDR(x)                             \
    ((struct queue_cb *) ((uintptr_t) NSTACK_SOCK_CTRL(x) + \
                          sizeof(struct nstack_sock_ctrl)))

#define NSTACK_INGRESS_DADDR(x)                         \
    ((uint8_t *) ((uintptr_t) NSTACK_INGRESS_QADDR(x) + \
                  sizeof(struct queue_cb)))

#define NSTACK_EGRESS_QADDR(x)                                  \
    ((struct queue_cb *) ((uintptr_t) NSTACK_INGRESS_DADDR(x) + \
                          NSTACK_DATAGRAM_BUF_SIZE))

#define NSTACK_EGRESS_DADDR(x) \
    ((uint8_t *) ((uintptr_t) NSTACK_EGRESS_QADDR(x) + sizeof(struct queue_cb)))

/**
 * Socket domain.
 */
enum nstack_sock_dom {
    XF_INET4, /*!< IPv4 address. */
    XF_INET6, /*!< IPv6 address. */
};

/**
 * Socket type.
 */
enum nstack_sock_type {
    XSOCK_DGRAM,  /*!< Unreliable datagram oriented service. */
    XSOCK_STREAM, /*!< Reliable stream oriented service. */
};

/**
 * Socket protocol.
 */
enum nstack_sock_proto {
    XIP_PROTO_NONE = 0,
    XIP_PROTO_TCP, /*!< TCP/IP. */
    XIP_PROTO_UDP, /*!< UDP/IP. */
    XIP_PROTO_LAST
};

/**
 * Max port number.
 */
#define NSTACK_SOCK_PORT_MAX 49151

/**
 * Socket addresss descriptor.
 */
struct nstack_sockaddr {
    union {
        in_addr_t inet4_addr; /*!< IPv4 address. */
    };
    union {
        int port; /*!< Protocol port. */
    };
};

struct nstack_sock_ctrl {
    pid_t pid_inetd;
    pid_t pid_end;
};

struct nstack_sock_info {
    enum nstack_sock_dom sock_dom;
    enum nstack_sock_type sock_type;
    enum nstack_sock_proto sock_proto;
    struct nstack_sockaddr sock_addr;
} info;

struct nstack_dgram {
    struct nstack_sockaddr srcaddr;
    struct nstack_sockaddr dstaddr;
    size_t buf_size;
    uint8_t buf[0];
};

#define NSTACK_MSG_PEEK 0x1

void *nstack_listen(const char *socket_path);
ssize_t nstack_recvfrom(void *socket,
                        void *restrict buffer,
                        size_t length,
                        int flags,
                        struct nstack_sockaddr *restrict address);
ssize_t nstack_sendto(void *socket,
                      const void *buffer,
                      size_t length,
                      int flags,
                      const struct nstack_sockaddr *dest_addr);

#endif /* NSTACK_SOCKET_H */

/**
 * @}
 */
