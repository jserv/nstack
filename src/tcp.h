/**
 * nstack TCP service.
 * @addtogroup TCP
 * @{
 */

#ifndef NSTACK_TCP_H
#define NSTACK_TCP_H

#include <stdint.h>

#include "linker_set.h"
#include "nstack_in.h"

/**
 * Type for an TCP port number.
 */
typedef uint16_t tcp_port_t;

/**
 * TCP packet header.
 */
struct tcp_hdr {
    struct {
        uint16_t tcp_sport;   /*!< Source port. */
        uint16_t tcp_dport;   /*!< Destination port. */
        uint32_t tcp_seqno;   /*!< Sequence number.*/
        uint32_t tcp_ack_num; /*!< Acknowledgment number (if ACK). */
        uint16_t tcp_flags;
        uint16_t tcp_win_size; /*!< Window size. */
        uint16_t tcp_checksum; /*!< TCP Checksum. */
        uint16_t tcp_urg_ptr;  /*!< Urgent pointer (if URG). */
    };
    uint8_t opt[0]; /*!< Options. */
} __attribute__((packed, aligned(4)));


#define TCP_DOFF_MASK 0xF000 /*<! Data offset. */
#define TCP_DOFF_OFF 12
#define TCP_NS 0x100  /*!< ECN-nonce concealment prot. */
#define TCP_CWR 0x080 /*!< Congestion Window Reduced. */
#define TCP_ECE 0x040 /*!< ECN-Echo. */
#define TCP_URG 0x020 /*!< Check Urgent pointer. */
#define TCP_ACK 0x010 /*!< Check ack_num. */
#define TCP_PSH 0x008 /*!< Push function. */
#define TCP_RST 0x004 /*!< Reset the connection. */
#define TCP_SYN 0x002 /*!< Synchronize sequence numbers. */
#define TCP_FIN 0x001 /*!< Last package from sender. */

inline static int tcp_hdr_size(struct tcp_hdr *hdr)
{
    const size_t doff = (hdr->tcp_flags & TCP_DOFF_MASK) >> TCP_DOFF_OFF;
    if (doff < 5 || doff > 15) {
        return -EINVAL;
    }

    return doff * 4;
}

/**
 * TCP Connection State.
 * Passive Open = responser/server
 * Active Open = initiator/client
 */
enum tcp_state {
    TCP_CLOSED = 0,
    TCP_LISTEN,   /* Passive Open */
    TCP_SYN_RCVD, /* Passive Open */
    TCP_SYN_SENT, /* Active Open */
    TCP_ESTABLISHED,
    TCP_FIN_WAIT_1, /* Initiator */
    TCP_FIN_WAIT_2, /* Initiator */
    TCP_CLOSE_WAIT, /* Responder */
    TCP_CLOSING,    /* Simultaneus Close */
    TCP_LAST_ACK,   /* Responder */
    TCP_TIME_WAIT,  /* Initiator/Simultaneus */
};

struct nstack_sockaddr;

/**
 * Allocate a UDP socket descriptor.
 */
struct nstack_sock *nstack_udp_alloc_sock(void);

int nstack_tcp_bind(struct nstack_sock *sock);
int nstack_tcp_send(struct nstack_sock *sock, const struct nstack_dgram *dgram);

#endif /* NSTACK_UDP_H */

/**
 * @}
 */
