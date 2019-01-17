#ifndef NSTACK_INTERNAL_H
#define NSTACK_INTERNAL_H

#include <stdatomic.h>
#include <sys/time.h>

#include "nstack_socket.h"
#include "tree.h"

#define NSTACK_CTRL_FLAG_DYING 0x8000

typedef void nstack_periodic_task_t(int delta_time);

/**
 * Declare a periodic task.
 */
#define NSTACK_PERIODIC_TASK(_task_fn_) \
    DATA_SET(_nstack_periodic_tasks, _task_fn_)

struct queue_cb;

/**
 * A generic socket descriptor.
 */
struct nstack_sock {
    struct nstack_sock_info info; /* Must be first */

    struct nstack_sock_ctrl *ctrl;
    uint8_t *ingress_data;
    struct queue_cb *ingress_q;
    uint8_t *egress_data;
    struct queue_cb *egress_q;

    union {
        struct {
            RB_ENTRY(nstack_sock) _entry;
        } udp;
        struct {
            RB_ENTRY(nstack_sock) _entry;
        } tcp;
    } data;
    char shmem_path[80];
};

/**
 * Socket datagram wrapping.
 * @{
 */

/**
 * Handle socket input data.
 * Transport -> Socket
 */
int nstack_sock_dgram_input(struct nstack_sock *sock,
                            struct nstack_sockaddr *srcaddr,
                            uint8_t *buf,
                            size_t bsize);

typedef int nstack_send_fn(struct nstack_sock *sock,
                           const struct nstack_dgram *dgram);

/**
 * @}
 */

#endif /* NSTACK_INTERNAL_H */
