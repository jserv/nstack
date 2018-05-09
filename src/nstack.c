#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "linker_set.h"
#include "nstack_in.h"
#include "nstack_socket.h"

#include "logger.h"
#include "collection.h"
#include "tcp.h"
#include "udp.h"
#include "nstack_ether.h"
#include "nstack_internal.h"
#include "nstack_ip.h"

/**
 * nstack ingress and egress thread state.
 */
enum nstack_state {
    NSTACK_STOPPED = 0, /*!< Ingress and egress threads are not running. */
    NSTACK_RUNNING,     /*!< Ingress and egress threads are running. */
    NSTACK_DYING,       /*!< Waiting for ingress and engress threads to stop. */
};

SET_DECLARE(_nstack_periodic_tasks, void);

/*
 * nstack state variables.
 */
static enum nstack_state nstack_state = NSTACK_STOPPED;
static pthread_t ingress_tid, egress_tid;
static int ether_handle;

static nstack_send_fn *proto_send[] = {
    [XIP_PROTO_TCP] = nstack_tcp_send,
    [XIP_PROTO_UDP] = nstack_udp_send,
};

static struct nstack_sock sockets[] = {
    {
        .info.sock_dom = XF_INET4,
        .info.sock_type = XSOCK_DGRAM,
        .info.sock_proto = XIP_PROTO_UDP,
        .info.sock_addr =
            (struct nstack_sockaddr){
                .inet4_addr = 167772162,
                .port = 10,
            },
        .shmem_path = "/tmp/unetcat.sock",
    },
};

static enum nstack_state get_state(void)
{
    enum nstack_state *state = &nstack_state;

    return *state;
}

static void set_state(enum nstack_state state)
{
    nstack_state = state;
}

static int delta_time;
static int eval_timer(void)
{
    static struct timespec start;
    struct timespec now;

    clock_gettime(CLOCK_MONOTONIC, &now);
    delta_time = now.tv_sec - start.tv_sec;
    if (delta_time >= NSTACK_PERIODIC_EVENT_SEC) {
        start = now;
        return !0;
    }
    return 0;
}

/**
 * Bind an address to a socket.
 * @param[in] sock is a pointer to the socket returned by nstack_socket().
 * @returns Uppon succesful completion returns 0;
 *          Otherwise -1 is returned and errno is set.
 */
static int nstack_bind(struct nstack_sock *sock)
{
    switch (sock->info.sock_proto) {
    case XIP_PROTO_UDP:
        return nstack_udp_bind(sock);
    default:
        errno = EPROTOTYPE;
        return -1;
    }
}

int nstack_sock_dgram_input(struct nstack_sock *sock,
                            struct nstack_sockaddr *srcaddr,
                            uint8_t *buf,
                            size_t bsize)
{
    int dgram_index;
    struct nstack_dgram *dgram;

    while ((dgram_index = queue_alloc(sock->ingress_q)) == -1)
        ;
    dgram = (struct nstack_dgram *) (sock->ingress_data + dgram_index);

    dgram->srcaddr = *srcaddr;
    dgram->dstaddr = sock->info.sock_addr;
    dgram->buf_size = bsize;
    memcpy(dgram->buf, buf, bsize);

    queue_commit(sock->ingress_q);
    kill(sock->ctrl->pid_end, SIGUSR2);

    return 0;
}

static void run_periodic_tasks(int delta_time)
{
    void **taskp;

    SET_FOREACH(taskp, _nstack_periodic_tasks)
    {
        nstack_periodic_task_t *task = *(nstack_periodic_task_t **) taskp;

        if (task)
            task(delta_time);
    }
}

/**
 * Handle the ingress traffic.
 * All ingress data is handled in a single pipeline until this point where
 * the data is demultiplexed to sockets.
 * transport -> socket fd
 */
static void *nstack_ingress_thread(void *arg)
{
    static uint8_t rx_buffer[ETHER_MAXLEN];

    while (1) {
        struct ether_hdr hdr;
        int retval;

        LOG(LOG_DEBUG, "Waiting for rx");

        retval =
            ether_receive(ether_handle, &hdr, rx_buffer, sizeof(rx_buffer));
        if (retval == -1) {
            LOG(LOG_ERR, "Rx failed: %d", errno);
        } else if (retval > 0) {
            LOG(LOG_DEBUG, "Frame received!");

            retval = ether_input(&hdr, rx_buffer, retval);
            if (retval == -1) {
                LOG(LOG_ERR, "Protocol handling failed: %d", errno);
            } else if (retval > 0) {
                retval =
                    ether_output_reply(ether_handle, &hdr, rx_buffer, retval);
                if (retval < 0) {
                    LOG(LOG_ERR, "Reply failed: %d", errno);
                }
            }
        }

        if (eval_timer()) {
            LOG(LOG_DEBUG, "tick");
            run_periodic_tasks(delta_time);
        }

        if (get_state() == NSTACK_DYING) {
            break;
        }
    }

    pthread_exit(NULL);
}

/**
 * Handle the egress traffic.
 * All egress traffic is mux'd and serialized through one egress pipe.
 * socket fd -> transport
 */
static void *nstack_egress_thread(void *arg)
{
    sigset_t sigset;

    sigemptyset(&sigset);
    sigaddset(&sigset, SIGUSR2);

    if (pthread_sigmask(SIG_BLOCK, &sigset, NULL) == -1) {
        LOG(LOG_ERR, "Unable to ignore SIGUSR2");
        abort();
    }

    while (1) {
        struct timespec timeout = {
            .tv_sec = NSTACK_PERIODIC_EVENT_SEC,
            .tv_nsec = 0,
        };

        sigtimedwait(&sigset, NULL, &timeout);

        for (size_t i = 0; i < num_elem(sockets); i++) {
            struct nstack_sock *sock = sockets + i;

            if (!queue_isempty(sock->egress_q)) {
                int dgram_index;
                struct nstack_dgram *dgram;
                enum nstack_sock_proto proto;

                while (!queue_peek(sock->egress_q, &dgram_index))
                    ;
                dgram =
                    (struct nstack_dgram *) (sock->egress_data + dgram_index);

                LOG(LOG_DEBUG, "Sending a datagram");
                proto = sock->info.sock_proto;
                if (proto > XIP_PROTO_NONE && proto < XIP_PROTO_LAST) {
                    if (proto_send[proto](sock, dgram) < 0) {
                        LOG(LOG_ERR, "Failed to send a datagram");
                    }
                } else {
                    LOG(LOG_ERR, "Invalid protocol");
                }

                queue_discard(sock->egress_q, 1);
            }
        }

        if (get_state() == NSTACK_DYING) {
            break;
        }
    }

    pthread_exit(NULL);
}

static void nstack_init(void)
{
    pid_t mypid = getpid();

    for (size_t i = 0; i < num_elem(sockets); i++) {
        struct nstack_sock *sock = sockets + i;
        int fd;
        void *pa;

        fd = open(sock->shmem_path, O_RDWR);
        if (fd == -1) {
            perror("Failed to open shmem file");
            exit(1);
        }

        pa = mmap(0, NSTACK_SHMEM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
                  0);
        if (pa == MAP_FAILED) {
            perror("Failed to mmap() shared mem");
            exit(1);
        }
        memset(pa, 0, NSTACK_SHMEM_SIZE);

        sock->ctrl = NSTACK_SOCK_CTRL(pa);
        *sock->ctrl = (struct nstack_sock_ctrl){
            .pid_inetd = mypid,
            .pid_end = 0,
        };

        sock->ingress_data = NSTACK_INGRESS_DADDR(pa);
        sock->ingress_q = NSTACK_INGRESS_QADDR(pa);
        *sock->ingress_q =
            queue_create(NSTACK_DATAGRAM_SIZE_MAX, NSTACK_DATAGRAM_BUF_SIZE);

        sock->egress_data = NSTACK_EGRESS_DADDR(pa);
        sock->egress_q = NSTACK_EGRESS_QADDR(pa);
        *sock->egress_q =
            queue_create(NSTACK_DATAGRAM_SIZE_MAX, NSTACK_DATAGRAM_BUF_SIZE);

        if (nstack_bind(sock) < 0) {
            perror("Failed to bind a socket");
            exit(1);
        }
    }
}

int nstack_start(int handle)
{
    ether_handle = handle;

    if (get_state() != NSTACK_STOPPED) {
        errno = EALREADY;
        return -1;
    }

    nstack_init();

    if (pthread_create(&ingress_tid, NULL, nstack_ingress_thread, NULL)) {
        return -1;
    }

    if (pthread_create(&egress_tid, NULL, nstack_egress_thread, NULL)) {
        pthread_cancel(ingress_tid);
        return -1;
    }

    set_state(NSTACK_RUNNING);
    return 0;
}

void nstack_stop(void)
{
    set_state(NSTACK_DYING);

    pthread_join(ingress_tid, NULL);
    pthread_join(egress_tid, NULL);

    set_state(NSTACK_STOPPED);
}

int main(int argc, char *argv[])
{
    char *const ether_args[] = {
        argv[1],
        NULL,
    };
    int handle;
    sigset_t sigset;

    if (argc == 1) {
        fprintf(stderr, "Usage: %s INTERFACE\n", argv[0]);
        exit(1);
    }

    sigemptyset(&sigset);
    sigaddset(&sigset, SIGUSR1);

    /* Block sigset for all future threads */
    sigprocmask(SIG_SETMASK, &sigset, NULL);

    handle = ether_init(ether_args);
    if (handle == -1) {
        perror("Failed to init");
        exit(1);
    }

    if (ip_config(handle, 167772162, 4294967040)) {
        perror("Failed to config IP");
        exit(1);
    }

    nstack_start(handle);

    sigwaitinfo(&sigset, NULL);

    fprintf(stderr, "Stopping the IP stack...\n");

    nstack_stop();

    ether_deinit(handle);

    return 0;
}
