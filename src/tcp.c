#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "nstack_ip.h"
#include "nstack_socket.h"

#include "collection.h"
#include "logger.h"
#include "nstack_internal.h"
#include "tcp.h"
#include "tree.h"

#define TCP_TIMER_MS 250
#define TCP_FIN_WAIT_TIMEOUT_MS 20000
#define TCP_SYN_RCVD_TIMEOUT_MS 20000

/*
 * TCP Connection Flags.
 */
#define TCP_FLAG_ACK_DELAY 0x01
#define TCP_FLAG_ACK_NOW 0x02
#define TCP_FLAG_RESET 0x04
#define TCP_FLAG_CLOSED 0x08
#define TCP_FLAG_GOT_FIN 0x10
#define TCP_FLAG_NODELAY 0x20 /*!< Disable nagle algorithm. */

/**
 * TCP Segment.
 */
struct tcp_segment {
    TAILQ_ENTRY(tcp_segment) _link;
    size_t size;
    struct tcp_hdr header;
    char data[0];
};

TAILQ_HEAD(tcp_segment_list, tcp_segment);

/**
 * TCP Connection Cotrol Block.
 */
struct tcp_conn_tcb {
    struct nstack_sockaddr local;  /*!< Local address and port. */
    struct nstack_sockaddr remote; /*!< Remote address and port. */

    enum tcp_state state;   /*!< Connection state. */
    unsigned flags;         /*!< Connection status flags. */
    size_t mss;             /*!< Maximum Segment Size. */
    unsigned keepalive;     /*!< Keepalive time. */
    unsigned keepalive_cnt; /*!< Keepalive counter. */

    /* RTT Estimation. */
    int rtt_est;     /*!< RTT estimate. */
    int rtt_cur_seq; /*!< Seq number being timed for RTT. */

    unsigned retran_timeout; /*!< Retransmission timeout. */
    unsigned retran_count;   /*!< Number of retransmissions. */

    /* Fast Retransmit. */
    uint32_t fastre_last_ack;
    unsigned fastre_dup_acks;

    /* Receiver. */
    uint32_t recv_next; /*!< Next seqno expected. */
    uint32_t recv_wnd;  /*!< Receiver window. */

    /* Sender. */
    uint32_t send_next; /*!< Next seqno to be used. */
    uint32_t send_wnd;
    uint32_t acked;

    RB_ENTRY(tcp_conn_tcb) _rb_entry;

    /* Segment Lists. */
    struct tcp_segment_list unsent_list;       /*!< Unsent segments. */
    struct tcp_segment_list unacked_list;      /*!< Unacked segments. */
    struct tcp_segment_list oos_segments_list; /*!< Out of seq segments. */
};

struct tcp_conn_attr {
    struct nstack_sockaddr local;
    struct nstack_sockaddr remote;
};

RB_HEAD(tcp_conn_map, tcp_conn_tcb);

static struct tcp_conn_map tcp_conn_map = RB_INITIALIZER();

static int tcp_conn_cmp(struct tcp_conn_tcb *a, struct tcp_conn_tcb *b)
{
    const int local =
        memcmp(&a->local, &b->local, sizeof(struct nstack_sockaddr));
    const int remote =
        memcmp(&a->remote, &b->remote, sizeof(struct nstack_sockaddr));

    return local + remote;
}

RB_GENERATE_STATIC(tcp_conn_map, tcp_conn_tcb, _rb_entry, tcp_conn_cmp);

static struct tcp_conn_tcb *tcp_find_connection(struct tcp_conn_attr *find)
{
    struct tcp_conn_tcb *find_p = (struct tcp_conn_tcb *) find;

    return RB_FIND(tcp_conn_map, &tcp_conn_map, find_p);
}

static struct tcp_conn_tcb *tcp_new_connection(const struct tcp_conn_attr *attr)
{
    struct tcp_conn_tcb *conn = calloc(1, sizeof(struct tcp_conn_tcb));
    memcpy(&conn->local, &attr->local, sizeof(struct nstack_sockaddr));
    memcpy(&conn->remote, &attr->remote, sizeof(struct nstack_sockaddr));

    RB_INSERT(tcp_conn_map, &tcp_conn_map, conn);

    return conn;
}

RB_HEAD(tcp_sock_tree, nstack_sock);

static struct tcp_sock_tree tcp_sock_tree_head = RB_INITIALIZER();

static int tcp_socket_cmp(struct nstack_sock *a, struct nstack_sock *b)
{
    return memcmp(&a->info.sock_addr, &b->info.sock_addr,
                  sizeof(struct nstack_sockaddr));
}

RB_GENERATE_STATIC(tcp_sock_tree, nstack_sock, data.tcp._entry, tcp_socket_cmp);

static struct nstack_sock *find_tcp_socket(const struct nstack_sockaddr *addr)
{
    struct nstack_sock_info find = {
        .sock_addr = *addr,
    };

    return RB_FIND(tcp_sock_tree, &tcp_sock_tree_head,
                   (struct nstack_sock *) (&find));
}

static uint16_t tcp_checksum(const struct nstack_sockaddr *restrict src,
                             const struct nstack_sockaddr *restrict dst,
                             struct tcp_hdr *restrict dp,
                             size_t bsize)
{
    const uint8_t *restrict data = (uint8_t *) dp;
    uint32_t acc = 0xffff;
    size_t i;
    uint16_t word;
    struct {
        uint32_t src_addr;
        uint32_t dst_addr;
        uint8_t zeros;
        uint8_t proto;
        uint16_t tcp_len;
    } __attribute__((packed, aligned(4))) pseudo_header = {
        .src_addr = htonl(src->inet4_addr),
        .dst_addr = htonl(dst->inet4_addr),
        .zeros = 0,
        .proto = IP_PROTO_TCP,
        .tcp_len = htons(bsize),
    };

    for (i = 0; i + 1 < 12; i += 2) {
        memcpy(&word, (uint8_t *) (&pseudo_header) + i, 2);
        acc += word;
        if (acc > 0xffff)
            acc -= ntohs(0xffff);
    }

    for (i = 0; i + 1 < bsize; i += 2) {
        memcpy(&word, data + i, 2);
        acc += word;
        if (acc > 0xffff)
            acc -= ntohs(0xffff);
    }

    if (bsize & 1) {
        word = 0;
        memcpy(&word, data + bsize - 1, 1);
        acc += word;
        if (acc > 0xffff)
            acc -= ntohs(0xffff);
    }

    return ~acc;
}

static void tcp_hton(const struct nstack_sockaddr *restrict src,
                     const struct nstack_sockaddr *restrict dst,
                     const struct tcp_hdr *host,
                     struct tcp_hdr *net,
                     size_t bsize)
{
    net->tcp_sport = htons(host->tcp_sport);
    net->tcp_dport = htons(host->tcp_dport);
    net->tcp_seqno = htonl(host->tcp_seqno);
    net->tcp_ack_num = htonl(host->tcp_ack_num);
    net->tcp_flags = htons(host->tcp_flags);
    net->tcp_win_size = htons(host->tcp_win_size);
    net->tcp_urg_ptr = htons(host->tcp_urg_ptr);
    /* TODO Handle opts */
    net->tcp_checksum = 0;
    net->tcp_checksum = tcp_checksum(src, dst, net, bsize);
}

static void tcp_ntoh(const struct tcp_hdr *net, struct tcp_hdr *host)
{
    host->tcp_sport = ntohs(net->tcp_sport);
    host->tcp_dport = ntohs(net->tcp_dport);
    host->tcp_seqno = ntohl(net->tcp_seqno);
    host->tcp_ack_num = ntohl(net->tcp_ack_num);
    host->tcp_flags = ntohs(net->tcp_flags);
    host->tcp_win_size = ntohs(net->tcp_win_size);
    host->tcp_urg_ptr = ntohs(net->tcp_urg_ptr);
    /* TODO Handle opts */
}

static int tcp_fsm(struct tcp_conn_tcb *conn, struct tcp_hdr *rs, size_t bsize)
{
    switch (conn->state) {
    case TCP_CLOSED:
        LOG(LOG_INFO, "TCP state: TCP_CLOSED");

        return 0;
    case TCP_LISTEN:
        LOG(LOG_INFO, "TCP state: TCP_LISTEN");

        if (rs->tcp_flags & TCP_SYN) {
            LOG(LOG_INFO, "SYN received");

            rs->tcp_flags |= TCP_ACK;
            rs->tcp_ack_num = rs->tcp_seqno + 1;
            srand(time(NULL));
            rs->tcp_seqno = rand() % 100;

            conn->state = TCP_SYN_RCVD;
            conn->recv_next = rs->tcp_ack_num;
            conn->send_next = rs->tcp_seqno + 1;
            LOG(LOG_INFO, "%d", ((uint32_t *) &rs)[3]);
            return tcp_hdr_size(rs);
        }
        return 0;
    case TCP_SYN_RCVD:
        LOG(LOG_INFO, "TCP state: TCP_SYN_RCVD");

        if ((rs->tcp_flags & TCP_ACK) && rs->tcp_seqno == conn->recv_next &&
            rs->tcp_ack_num == conn->send_next) {
            conn->state = TCP_ESTABLISHED;
            return 0;
        }
        rs->tcp_flags &= ~TCP_ACK;
        rs->tcp_ack_num = rs->tcp_seqno;
        rs->tcp_seqno = conn->send_next;

        conn->recv_next = rs->tcp_ack_num;
        conn->send_next = rs->tcp_seqno + 1;
        return tcp_hdr_size(rs);
    case TCP_ESTABLISHED:
        LOG(LOG_INFO, "TCP state: TCP_ESTABLISHED");
        if ((rs->tcp_flags & TCP_ACK) && (rs->tcp_flags & TCP_PSH) &&
            rs->tcp_seqno == conn->recv_next &&
            rs->tcp_ack_num == conn->send_next) {
            /* data handling */
            rs->tcp_flags &= ~TCP_PSH;
            rs->tcp_ack_num = rs->tcp_seqno + (bsize - tcp_hdr_size(rs));
            rs->tcp_seqno = conn->send_next;

            conn->recv_next = rs->tcp_ack_num;
            conn->send_next = rs->tcp_seqno;
            /* TODO forward the payload to application layer */
            return tcp_hdr_size(rs);
        }
        if (rs->tcp_flags & TCP_FIN) { /* Close connection. */
            rs->tcp_flags |= TCP_ACK;
            rs->tcp_ack_num = rs->tcp_seqno + 1;
            rs->tcp_seqno = conn->send_next;

            conn->state = TCP_LAST_ACK;
            conn->recv_next = rs->tcp_ack_num;
            conn->send_next = rs->tcp_seqno + 1;
            return tcp_hdr_size(rs);
        }
        return 0;
    case TCP_FIN_WAIT_1:
    case TCP_FIN_WAIT_2:
    case TCP_CLOSE_WAIT:
    case TCP_CLOSING:
        LOG(LOG_INFO, "TCP state: TCP_CLOSING");

        return 0;
    case TCP_LAST_ACK:
        if (rs->tcp_flags & TCP_ACK) {
            /* TODO Remove the connection */
            conn->state = TCP_CLOSED;
            return 0;
        }
    /* TODO handle error? */
    case TCP_TIME_WAIT:
        LOG(LOG_INFO, "TCP state: TCP_TIME_WAIT");
    default:
        LOG(LOG_INFO, "TCP state: INVALID (%d)", conn->state);

        return -EINVAL;
    }
}

/**
 * TCP input chain.
 * IP -> TCP
 */
static int tcp_input(const struct ip_hdr *ip_hdr,
                     uint8_t *payload,
                     size_t bsize)
{
    struct tcp_conn_attr attr;
    struct tcp_hdr *tcp = (struct tcp_hdr *) payload;

    if (bsize < sizeof(struct tcp_hdr)) {
        LOG(LOG_INFO, "Datagram size too small");

        return -EBADMSG;
    }

    memset(&attr, 0, sizeof(attr));
    attr.local.inet4_addr = ip_hdr->ip_dst;
    attr.local.port = ntohs(tcp->tcp_dport);
    attr.remote.inet4_addr = ip_hdr->ip_src;
    attr.remote.port = ntohs(tcp->tcp_sport);

/* TODO Can't verify on LXC env */
#if 0
    if (tcp_checksum(&attr.remote, &attr.local, tcp, bsize) != 0) {
        LOG(LOG_INFO, "TCP checksum fail");
        /* TODO Fail properly */
        return -EBADMSG;
    }
#endif

    tcp_ntoh(tcp, tcp);

    struct tcp_conn_tcb *conn = tcp_find_connection(&attr);
    if (!conn && (tcp->tcp_flags & TCP_SYN)) { /* New connection */
        char rem_str[IP_STR_LEN];
        char loc_str[IP_STR_LEN];

        ip2str(attr.remote.inet4_addr, rem_str);
        ip2str(attr.local.inet4_addr, loc_str);
        LOG(LOG_INFO, "New connection %s:%i -> %s:%i", rem_str,
            attr.remote.port, loc_str, attr.local.port);

        /* TODO Check if we listen the port */
        conn = tcp_new_connection(&attr);
        conn->state = TCP_LISTEN;
    } else if (!conn || (tcp->tcp_flags & TCP_SYN) || tcp_hdr_size(tcp) < 0) {
        /* No connection initiated, invalid flag, or invalid header size. */
        return -EINVAL; /* TODO any other error handling needed here? */
    }

    int retval = tcp_fsm(conn, tcp, bsize);
    if (retval > 0) { /* Fast reply */
        tcp->tcp_sport = attr.local.port;
        tcp->tcp_dport = attr.remote.port;
        tcp_hton(&attr.local, &attr.remote, tcp, tcp, retval);
    }

    return retval;
}
IP_PROTO_INPUT_HANDLER(IP_PROTO_TCP, tcp_input);

int nstack_tcp_bind(struct nstack_sock *sock)
{
    if (sock->info.sock_addr.port > NSTACK_SOCK_PORT_MAX) {
        errno = EINVAL;
        return -1;
    }

    if (find_tcp_socket(&sock->info.sock_addr)) {
        errno = EADDRINUSE;
        return -1;
    }

    RB_INSERT(tcp_sock_tree, &tcp_sock_tree_head, sock);

    return 0;
}

int nstack_tcp_send(struct nstack_sock *sock, const struct nstack_dgram *dgram)
{
    return -1;
}
