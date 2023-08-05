#if defined(__linux__)
#include <fcntl.h>
#include <linux/random.h>
#else
#include <time.h>
#endif

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "nstack_ip.h"
#include "nstack_socket.h"

#include "collection.h"
#include "logger.h"
#include "nstack_internal.h"
#include "tcp.h"
#include "tree.h"

#define TCP_MSS 1460 /*!< TCP maximum segment size. */

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
 * Current time. Used if RTT is measured using timestamp method.
 */
unsigned tcp_now = 0;

/**
 * TCP Segment.
 */
struct tcp_segment {
    TAILQ_ENTRY(tcp_segment) _link;
    size_t size;
    char *data;
    struct tcp_hdr header;
};

TAILQ_HEAD(tcp_segment_list, tcp_segment);

/**
 * TCP Connection Control Block.
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
    int rtt_est;     /*!< RTT estimator. */
    int rtt_var;     /*!< mean deviation RTT estimator*/
    int rtt;         /*!< RTT sample*/
    int rtt_cur_seq; /*!< Seq number being timed for RTT estimation. */

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
    uint32_t send_una;  /*!< Oldest unacknowledged seqno.*/
    uint32_t send_max;  /*!< Maximum send seqno. An acceptible ACK is the one
                           which the following inequality holds: snd_una <
                           acknowledgment field <= snd_max */
    uint32_t send_wnd;
    uint32_t acked;

    RB_ENTRY(tcp_conn_tcb) _rb_entry;

    /* Segment Lists. */
    struct tcp_segment_list unsent_list;       /*!< Unsent segments. */
    struct tcp_segment_list unacked_list;      /*!< Unacked segments. */
    struct tcp_segment_list oos_segments_list; /*!< Out of seq segments. */

    int timer[TCP_T_NTIMERS];
    pthread_mutex_t mutex;
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
    assert(conn);
    memcpy(&conn->local, &attr->local, sizeof(struct nstack_sockaddr));
    memcpy(&conn->remote, &attr->remote, sizeof(struct nstack_sockaddr));
    pthread_mutex_init(&conn->mutex, NULL);
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

static void tcp_hton_opt(struct tcp_hdr *hdr, int len)
{
    for (int i = 0; i < len;) {
        if (hdr->opt[i] == 0) {
            break;
        }
        if (hdr->opt[i] == 1) {
            i += 1;
            continue;
        }
        struct tcp_option *opt = (struct tcp_option *) (&(hdr->opt[i]));
        switch (opt->option_kind) {
        case 2: /* maximum segment size (2bytes)*/
            opt->mss = htons(opt->mss);
            i += opt->length;
            continue;
        case 3: /*window size (1 byte)*/
            i += opt->length;
            continue;
        case 4:
            i += opt->length;
            continue;
        case 5:
            i += opt->length;
            continue;
        case 8: /*timestamp and echo of previous timestamp(8 bytes)*/
            opt->tsval = htonl(opt->tsval);
            opt->tsecr = htonl(opt->tsecr);
            i += opt->length;
            continue;
        }
    }
}

static void tcp_ntoh_opt(struct tcp_hdr *hdr, int len)
{
    for (int i = 0; i < len;) {
        if (hdr->opt[i] == 0) {
            break;
        }
        if (hdr->opt[i] == 1) {
            i += 1;
            continue;
        }
        struct tcp_option *opt = (struct tcp_option *) (&(hdr->opt[i]));
        switch (opt->option_kind) {
        case 2: /* maximum segment size (2bytes)*/
            opt->mss = ntohs(opt->mss);
            i += opt->length;
            continue;
        case 3: /*window size (1 byte)*/
            i += opt->length;
            continue;
        case 4:
            i += opt->length;
            continue;
        case 5:
            i += opt->length;
            continue;
        case 8: /*timestamp and echo of previous timestamp(8 bytes)*/
            opt->tsval = ntohl(opt->tsval);
            opt->tsecr = ntohl(opt->tsecr);
            i += opt->length;
            continue;
        }
    }
}

static void tcp_hton(const struct nstack_sockaddr *restrict src,
                     const struct nstack_sockaddr *restrict dst,
                     const struct tcp_hdr *host,
                     struct tcp_hdr *net,
                     size_t bsize)
{
    int opt_len = tcp_opt_size(host);
    tcp_hton_opt(host, opt_len);
    net->tcp_sport = htons(host->tcp_sport);
    net->tcp_dport = htons(host->tcp_dport);
    net->tcp_seqno = htonl(host->tcp_seqno);
    net->tcp_ack_num = htonl(host->tcp_ack_num);
    net->tcp_flags = htons(host->tcp_flags);
    net->tcp_win_size = htons(host->tcp_win_size);
    net->tcp_urg_ptr = htons(host->tcp_urg_ptr);
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
    int opt_len = tcp_opt_size(host);
    tcp_ntoh_opt(host, opt_len);
}

static void tcp_rto_update(struct tcp_conn_tcb *conn, int rtt);
static void tcp_ack_segments(struct tcp_conn_tcb *conn, struct tcp_hdr *tcp);

static int tcp_fsm(struct tcp_conn_tcb *conn,
                   struct tcp_hdr *rs,
                   struct ip_hdr *ip_hdr,
                   size_t bsize)
{
    if (conn->rtt && (rs->tcp_ack_num > conn->rtt_cur_seq)) {
        tcp_rto_update(conn, conn->rtt);
    }
    switch (conn->state) {
    case TCP_CLOSED:
        LOG(LOG_INFO, "TCP state: TCP_CLOSED");
        return 0;
    case TCP_SYN_SENT:
        LOG(LOG_INFO, "TCP state: TCP_SYN_SENT");
        if (rs->tcp_flags & (TCP_SYN | TCP_ACK)) {
            LOG(LOG_INFO, "SYN & ACK received");
            rs->tcp_flags = TCP_ACK | 5 << 12;
            rs->tcp_ack_num = rs->tcp_seqno + 1;
            rs->tcp_seqno = conn->send_next;
            conn->recv_next = rs->tcp_ack_num;
            conn->recv_wnd = rs->tcp_win_size;
            LOG(LOG_INFO, "%d", ((uint32_t *) &rs)[3]);
            conn->timer[TCP_T_KEEP] = 0;
            conn->state = TCP_ESTABLISHED;
            conn->timer[TCP_T_REXMT] =
                1; /* TODO: Instead of utilizing retransmission, use another way
                      to send any unsent segments after receiving SYN & ACK.*/
            return tcp_hdr_size(rs);
        }
        if (rs->tcp_flags & (TCP_SYN)) {
            /*Client and server open connection simultaneously*/
            LOG(LOG_INFO, "SYN received, connection opened simultaneously ");
            rs->tcp_flags = (TCP_SYN | TCP_ACK) | 5 << 12;
            rs->tcp_ack_num = rs->tcp_seqno + 1;
            rs->tcp_seqno = conn->send_next;
            conn->recv_next = rs->tcp_ack_num;
            conn->recv_wnd = rs->tcp_win_size;
            LOG(LOG_INFO, "%d", ((uint32_t *) &rs)[3]);
            conn->state = TCP_SYN_RCVD;
            return tcp_hdr_size(rs);
        }
    case TCP_LISTEN:
        LOG(LOG_INFO, "TCP state: TCP_LISTEN");

        if (rs->tcp_flags & TCP_SYN) {
            LOG(LOG_INFO, "SYN received");

            struct nstack_sockaddr sockaddr = {
                .inet4_addr = ip_hdr->ip_dst,
                .port = rs->tcp_dport,
            };
            struct nstack_sock *sock = find_tcp_socket(&sockaddr);
            if (!sock) {
                LOG(LOG_INFO, "Port %d unreachable", sockaddr.port);
                rs->tcp_flags &= ~TCP_SYN;
                rs->tcp_flags |= TCP_RST;
            }
            rs->tcp_flags |= TCP_ACK;
            rs->tcp_ack_num = rs->tcp_seqno + 1;

#if defined(__linux__)
            int fd = open("/dev/urandom", O_RDONLY);
            read(fd, &(rs->tcp_seqno), sizeof(rs->tcp_seqno));
            close(fd);
#else
            srand(time(NULL));
            rs->tcp_seqno = rand() % 100;
#endif

            if (sock) {
                conn->state = TCP_SYN_RCVD;
            } else {
                conn->state = TCP_CLOSED;
            }
            conn->recv_next = rs->tcp_ack_num;
            conn->send_next = rs->tcp_seqno + 1;
            LOG(LOG_INFO, "%d", ((uint32_t *) &rs)[3]);
            return tcp_hdr_size(rs);
        }
        return 0;
    case TCP_SYN_RCVD:
        LOG(LOG_INFO, "TCP state: TCP_SYN_RCVD");
        if ((rs->tcp_flags & TCP_RST) && rs->tcp_seqno == conn->recv_next &&
            rs->tcp_ack_num == conn->send_next) {
            conn->state = TCP_LISTEN;
            return 0;
        }
        if ((rs->tcp_flags & TCP_ACK) && rs->tcp_seqno == conn->recv_next &&
            rs->tcp_ack_num == conn->send_next) {
            conn->timer[TCP_T_KEEP] = 0;
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
        tcp_ack_segments(conn, rs);
        if ((rs->tcp_flags & TCP_ACK) && (rs->tcp_flags & TCP_PSH) &&
            rs->tcp_seqno == conn->recv_next &&
            rs->tcp_ack_num == conn->send_next) {
            /* data handling */
            rs->tcp_flags &= ~TCP_PSH;
            rs->tcp_ack_num = rs->tcp_seqno + (bsize - tcp_hdr_size(rs));
            rs->tcp_seqno = conn->send_next;

            conn->recv_next = rs->tcp_ack_num;
            conn->send_next = rs->tcp_seqno;

            /* forward the payload to application layer */
            struct nstack_sockaddr sockaddr = {
                .inet4_addr = ip_hdr->ip_dst,
                .port = rs->tcp_dport,
            };
            struct nstack_sock *sock = find_tcp_socket(&sockaddr);
            struct nstack_sockaddr srcaddr = {
                .inet4_addr = ip_hdr->ip_src,
                .port = rs->tcp_sport,
            };
            size_t header_size = tcp_hdr_size(rs);
            nstack_sock_dgram_input(sock, &srcaddr,
                                    ((uint8_t *) rs) + header_size,
                                    bsize - header_size);

            return tcp_hdr_size(rs);
        }
        if (rs->tcp_flags & TCP_FIN) { /* Close connection. */
            rs->tcp_flags |= TCP_ACK;
            rs->tcp_ack_num = rs->tcp_seqno + 1;
            rs->tcp_seqno = conn->send_next;

            conn->state = TCP_LAST_ACK; /* Skip TCP_CLOSE_WAIT state */
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
        LOG(LOG_INFO, "TCP state: TCP_LAST_ACK");
        if (rs->tcp_flags & TCP_ACK) {
            RB_REMOVE(tcp_conn_map, &tcp_conn_map, conn);
            conn->state = TCP_CLOSED;
            free(conn);
        }
        return 0;
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
    if ((conn &&
         ((tcp->tcp_flags & TCP_SYN) && (conn->state >= TCP_ESTABLISHED))) ||
        tcp_hdr_size(tcp) < 0) {
        /*Invalid flag, or invalid header size. */
        return -EINVAL; /* TODO any other error handling needed here? */
    }
    if (!conn && (tcp->tcp_flags & TCP_SYN)) { /* New connection */
        char rem_str[IP_STR_LEN];
        char loc_str[IP_STR_LEN];

        ip2str(attr.remote.inet4_addr, rem_str);
        ip2str(attr.local.inet4_addr, loc_str);
        LOG(LOG_INFO, "New connection %s:%i -> %s:%i", rem_str,
            attr.remote.port, loc_str, attr.local.port);

        conn = tcp_new_connection(&attr);
        conn->state = TCP_LISTEN;
    }

    int retval = tcp_fsm(conn, tcp, ip_hdr, bsize);
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

static int tcp_connection_init(struct tcp_conn_tcb *conn)
{
    conn->state = TCP_SYN_SENT;
    conn->mss = TCP_MSS;
#if defined(__linux__)
    int fd = open("/dev/urandom", O_RDONLY);
    read(fd, &(conn->send_next), sizeof(conn->send_next));
    close(fd);
#else
    srand(time(NULL));
    conn->send_next = rand() % 100;
#endif
    conn->rtt_est = TCP_TV_SRTTBASE;
    conn->rtt_var = (TCP_RTTDFT * TCP_TIMER_PR_SLOWHZ) << 2;
    conn->retran_timeout =
        ((TCP_TV_SRTTBASE >> 2) + (TCP_TV_SRTTDFLT << 2)) >> 1;
    conn->send_wnd = 502;
    conn->send_una = conn->send_next;
    conn->send_max = conn->send_next;
    TAILQ_INIT(&conn->unsent_list);
    TAILQ_INIT(&conn->unacked_list);
    TAILQ_INIT(&conn->oos_segments_list);
    return 0;
}

static int tcp_send_syn(struct tcp_conn_tcb *conn)
{
    struct tcp_option opt;
    opt = (struct tcp_option){.option_kind = 2, .length = 4, .mss = TCP_MSS};

    uint8_t buf[sizeof(struct tcp_hdr) + opt.length];
    struct tcp_hdr *tcp = (struct tcp_hdr *) buf;
    memcpy((struct tcp_option *) tcp->opt, &opt, opt.length);
    tcp->tcp_seqno = conn->send_next;
    conn->send_next++;
    tcp->tcp_flags = TCP_SYN | 6 << 12;
    tcp->tcp_win_size = 502;
    tcp->tcp_sport = conn->local.port;
    tcp->tcp_dport = conn->remote.port;
    tcp_hton(&(conn->local), &(conn->remote), tcp, tcp, tcp_hdr_size(tcp));
    conn->state = TCP_SYN_SENT;
    conn->timer[TCP_T_KEEP] = TCP_TV_KEEP_INIT;
    int retval = ip_send(conn->remote.inet4_addr, IP_PROTO_TCP, buf,
                         sizeof(struct tcp_hdr) + opt.length);
    return retval;
}

static int tcp_send_segments(struct tcp_conn_tcb *conn)
{
    struct tcp_segment *seg, *seg_tmp;
    int retval;
    pthread_mutex_lock(&conn->mutex);
    TAILQ_FOREACH_SAFE(seg, &conn->unsent_list, _link, seg_tmp)
    {
        TAILQ_REMOVE(&conn->unsent_list, seg, _link);
        size_t hdr_size = tcp_hdr_size(&seg->header);
        uint8_t payload[hdr_size + seg->size];
        struct tcp_hdr *tcp = (struct tcp_hdr *) (payload);

        memcpy(payload, &seg->header, hdr_size);
        ((struct tcp_hdr *) payload)->tcp_seqno = conn->send_next;
        ((struct tcp_hdr *) payload)->tcp_ack_num = conn->recv_next;
        memcpy(payload + hdr_size, seg->data, seg->size);

        tcp_hton(&(conn->local), &(conn->remote), tcp, tcp,
                 hdr_size + seg->size);
        conn->send_next += seg->size;
        conn->send_max = conn->send_next;
        retval = ip_send(conn->remote.inet4_addr, IP_PROTO_TCP, payload,
                         (hdr_size + seg->size));
        if (retval < 0) {
            return -1;
        }
        TAILQ_INSERT_TAIL(&conn->unacked_list, seg, _link);
    }
    pthread_mutex_unlock(&conn->mutex);
    return 0;
}

static void tcp_ack_segments(struct tcp_conn_tcb *conn, struct tcp_hdr *tcp)
{
    struct tcp_segment *seg, *seg_tmp;
    if (tcp->tcp_ack_num > conn->send_una) {
        conn->send_una = tcp->tcp_ack_num;
        pthread_mutex_lock(&conn->mutex);
        TAILQ_FOREACH_SAFE(seg, &conn->unacked_list, _link, seg_tmp)
        {
            if (seg->header.tcp_seqno < conn->send_una) {
                TAILQ_REMOVE(&conn->unacked_list, seg, _link);
                free(seg);
            }
        }
        pthread_mutex_unlock(&conn->mutex);
        if (conn->send_una == conn->send_max) {
            conn->timer[TCP_T_REXMT] = 0;
        } else {
            conn->send_next = conn->send_una;
            conn->timer[TCP_T_REXMT] = conn->rtt_est;
        }
        return;
    } else {
        return;
    }
}

int nstack_tcp_send(struct nstack_sock *sock, const struct nstack_dgram *dgram)
{
    struct tcp_conn_attr attr;
    struct tcp_hdr tcp;
    struct tcp_segment *seg;
    int retval;
    memset(&attr, 0, sizeof(attr));
    attr.local.inet4_addr = sock->info.sock_addr.inet4_addr;
    attr.local.port = sock->info.sock_addr.port;
    attr.remote.inet4_addr = dgram->dstaddr.inet4_addr;
    attr.remote.port = dgram->dstaddr.port;
    struct tcp_conn_tcb *conn = tcp_find_connection(&attr);
    if (!conn) {
        /*Client, send syn*/
        char rem_str[IP_STR_LEN];
        char loc_str[IP_STR_LEN];
        ip2str(attr.remote.inet4_addr, rem_str);
        ip2str(attr.local.inet4_addr, loc_str);
        LOG(LOG_INFO, "Client request new connection %s:%i -> %s:%i", rem_str,
            attr.remote.port, loc_str, attr.local.port);
        conn = tcp_new_connection(&attr);
        tcp_connection_init(conn);
        tcp = (struct tcp_hdr){
            .tcp_flags = TCP_PSH | TCP_ACK | (5 << TCP_DOFF_OFF),
            .tcp_win_size = 502,
            .tcp_sport = sock->info.sock_addr.port,
            .tcp_dport = dgram->dstaddr.port

        };
        seg = calloc(1, sizeof(struct tcp_segment) + tcp_opt_size(&tcp) +
                            dgram->buf_size);
        seg->data = (seg->header.opt) + tcp_opt_size(&tcp);
        seg->size = dgram->buf_size;
        memcpy(seg->data, dgram->buf, dgram->buf_size);
        memcpy(&seg->header, &tcp, tcp_hdr_size(&tcp));
        pthread_mutex_lock(&conn->mutex);
        TAILQ_INSERT_TAIL(&conn->unsent_list, seg, _link);
        pthread_mutex_unlock(&conn->mutex);
        int retval = tcp_send_syn(conn);
        return retval;
    } else {
        switch (conn->state) {
        case TCP_ESTABLISHED:
            tcp = (struct tcp_hdr){
                .tcp_flags = TCP_PSH | TCP_ACK | (5 << TCP_DOFF_OFF),
                .tcp_win_size = 502,
                .tcp_sport = conn->local.port,
                .tcp_dport = conn->remote.port

            };
            if (conn->rtt == 0) {
                conn->rtt = 1;
                conn->rtt_cur_seq = conn->send_next;
            }
            struct tcp_segment *seg =
                calloc(1, sizeof(struct tcp_segment) + tcp_opt_size(&tcp) +
                              dgram->buf_size);
            seg->data = (seg->header.opt) + tcp_opt_size(&tcp);
            seg->size = dgram->buf_size;
            memcpy(seg->data, dgram->buf, dgram->buf_size);
            memcpy(&seg->header, &tcp, tcp_hdr_size(&tcp));
            pthread_mutex_lock(&conn->mutex);
            TAILQ_INSERT_TAIL(&conn->unsent_list, seg, _link);
            pthread_mutex_unlock(&conn->mutex);
            retval = tcp_send_segments(conn);
            return retval;
        default:
            LOG(LOG_INFO, "TCP state: INVALID (%d)", conn->state);

            return -EINVAL;
        }
    }
}

static void tcp_rto_update(struct tcp_conn_tcb *conn, int rtt)
{
    int delta;
    if (conn->rtt_est != 0) {
        delta = (rtt - 1) - (conn->rtt_est >> TCP_RTT_SHIFT);
        if ((conn->rtt_est += delta) <= 0) {
            conn->rtt_est = 1;
        }
        delta = abs(delta);
        delta -= ((conn->rtt_var) >> (TCP_RTTVAR_SHIFT));
        if ((conn->rtt_var += delta) <= 0) {
            conn->rtt_var = 1;
        }
    } else {
        conn->rtt_est = rtt << TCP_RTT_SHIFT;
        conn->rtt_var = rtt << (TCP_RTTVAR_SHIFT - 1);
    }
    conn->retran_timeout = TCP_REXMTVAL(conn);
    LOG(LOG_INFO, "Update RTO: value = %d", conn->retran_timeout);
    conn->rtt = 0; /*Reset to 0 for timing and transmission of next segment. */
}

static void tcp_rexmt_prepare(struct tcp_conn_tcb *conn)
{
    struct tcp_segment *seg, *seg_tmp;
    pthread_mutex_lock(&conn->mutex);
    TAILQ_FOREACH_SAFE(seg, &conn->unsent_list, _link, seg_tmp)
    {
        TAILQ_REMOVE(&conn->unsent_list, seg, _link);
        TAILQ_INSERT_TAIL(&conn->unacked_list, seg, _link);
    }
    TAILQ_SWAP(&conn->unsent_list, &conn->unacked_list, tcp_segment, _link);
    pthread_mutex_unlock(&conn->mutex);
}

static void tcp_rexmt_commit(struct tcp_conn_tcb *conn)
{
    conn->retran_count++;
    tcp_send_segments(conn);
}


static void tcp_timer(struct tcp_conn_tcb *conn, int counter_index)
{
    switch (counter_index) {
    case TCP_T_REXMT:
        conn->timer[counter_index] = conn->retran_timeout;
        /* Karn's Algorithm: the only segments that are timed by conn->rtt are
         * those that are not retransmitted.
         * TODO: Use timestamps to estimate
         * RTT instead of Karn's Algorithm */
        conn->rtt = 0;
        tcp_rexmt_prepare(conn);
        tcp_rexmt_commit(conn);
        return;
    case TCP_T_PERSIST:
    case TCP_T_KEEP:
        if (conn->state < TCP_ESTABLISHED) {
            RB_REMOVE(tcp_conn_map, &tcp_conn_map, conn);
            free(conn);
            return;
        }

    case TCP_T_2MSL:
        RB_REMOVE(tcp_conn_map, &tcp_conn_map, conn);
        free(conn);
        return;
    }
}
void tcp_slowtimo()
{
    struct tcp_conn_tcb *conn;
    RB_FOREACH (conn, tcp_conn_map, &tcp_conn_map) {
        for (int i = 0; i < TCP_T_NTIMERS; i++) {
            if (conn->timer[i] && (--conn->timer[i] == 0)) {
                tcp_timer(conn, i);
            }
        }
        if (conn->rtt) {
            conn->rtt++;
        }
    }
    tcp_now++;
}