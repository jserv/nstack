#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "nstack_socket.h"
#include "nstack_util.h"

/* TODO bind fn for outbound connections */

static void block_sigusr2(void)
{
    sigset_t sigset;

    sigemptyset(&sigset);
    sigaddset(&sigset, SIGUSR2);

    if (pthread_sigmask(SIG_BLOCK, &sigset, NULL) == -1)
        abort();
}

void *nstack_listen(const char *socket_path)
{
    int fd;
    void *pa;

    fd = open(socket_path, O_RDWR);
    if (fd == -1)
        return NULL;

    pa = mmap(0, NSTACK_SHMEM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (pa == MAP_FAILED)
        return NULL;

    block_sigusr2();
    NSTACK_SOCK_CTRL(pa)->pid_end = getpid();

    return pa;
}

ssize_t nstack_recvfrom(void *socket,
                        void *restrict buffer,
                        size_t length,
                        int flags,
                        struct nstack_sockaddr *restrict address)
{
    struct queue_cb *ingress_q = NSTACK_INGRESS_QADDR(socket);
    struct nstack_dgram *dgram;
    sigset_t sigset;
    int dgram_index;
    ssize_t rd;

    sigemptyset(&sigset);
    sigaddset(&sigset, SIGUSR2);

    do {
        struct timespec timeout = {
            .tv_sec = NSTACK_PERIODIC_EVENT_SEC, .tv_nsec = 0,
        };

        sigtimedwait(&sigset, NULL, &timeout);
    } while (!queue_peek(ingress_q, &dgram_index));
    dgram =
        (struct nstack_dgram *) (NSTACK_INGRESS_DADDR(socket) + dgram_index);

    if (address)
        *address = dgram->srcaddr;
    rd = smin(length, dgram->buf_size);
    memcpy(buffer, dgram->buf, rd);
    dgram = NULL;

    if (!(flags & NSTACK_MSG_PEEK))
        queue_discard(ingress_q, 1);

    return rd;
}

ssize_t nstack_sendto(void *socket,
                      const void *buffer,
                      size_t length,
                      int flags,
                      const struct nstack_sockaddr *dest_addr)
{
    const struct nstack_sock_ctrl *ctrl = NSTACK_SOCK_CTRL(socket);
    struct queue_cb *egress_q = NSTACK_EGRESS_QADDR(socket);
    struct nstack_dgram *dgram;
    int dgram_index;

    if (length > NSTACK_DATAGRAM_SIZE_MAX) {
        errno = ENOBUFS;
        return -1;
    }

    while ((dgram_index = queue_alloc(egress_q)) == -1)
        ;
    dgram = (struct nstack_dgram *) (NSTACK_EGRESS_DADDR(socket) + dgram_index);

    /* Ignored by the implementation */
    memset(&dgram->srcaddr, 0, sizeof(struct nstack_sockaddr));

    dgram->dstaddr = *dest_addr;
    memcpy(dgram->buf, buffer, length);

    queue_commit(egress_q);
    kill(ctrl->pid_inetd, SIGUSR2);

    return length;
}
