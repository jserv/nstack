/**
 * nstack TCP service.
 * @addtogroup TCP
 * @{
 */

#pragma once

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

/**
 * TCP packet header option.
 */
struct tcp_option {
    uint8_t option_kind;
    uint8_t length;
    union {
        uint16_t mss;
        uint8_t window_scale;
        struct {
            uint32_t tsval;
            uint32_t tsecr;
        };
    };
} __attribute__((packed, aligned(4)));
/*TODO: support Selective ACKnowledgement (SACK)*/


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

inline static int tcp_opt_size(struct tcp_hdr *hdr)
{
    const size_t doff = (hdr->tcp_flags & TCP_DOFF_MASK) >> TCP_DOFF_OFF;
    if (doff < 5 || doff > 15) {
        return -EINVAL;
    }

    return doff * 4 - sizeof(struct tcp_hdr);
}

/**
 * TCP Connection State.
 * Passive Open = responder/server
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
    TCP_CLOSING,    /* Simultaneous Close */
    TCP_LAST_ACK,   /* Responder */
    TCP_TIME_WAIT,  /* Initiator/Simultaneous */
};

/**
 * TCP timer counter index
 */

#define TCP_T_REXMT 0   /*<! Index of retransmission timer counter. */
#define TCP_T_PERSIST 1 /*<! Index persist timer counter. */
#define TCP_T_KEEP                                                             \
    2 /*<! Index of keep alive timer counter or connection establishment timer \
       * counter.                                                              \
       */
#define TCP_T_2MSL \
    3 /*<! Index of 2MSL timer counter or FIN_WAIT2 timer counter. */
#define TCP_T_NTIMERS 4 /*<! Number of timer counter. */


/**
 * TCP timer counter value (ticks for 500-ms clock)
 */
#define TCP_TV_MSL 60        /*<! MSL, maximum segment lifetime. */
#define TCP_TV_MIN 2         /*<! Minimum value of retransmission timer. */
#define TCP_TV_REXMTMAX 128  /*<! Maximum value of retransmission timer. */
#define TCP_TV_PERSMIN 10    /*<! Minimum value of persist timer. */
#define TCP_TV_PERSMAX 120   /*<! Maximum value of persist timer. */
#define TCP_TV_KEEP_INIT 150 /*<! Connection-established timer value. */
#define TCP_TV_KEEP_IDLE \
    14400 /*<! Idle time for connection between first probe. (2 hours)*/
#define TCP_TV_KEEPINTVL 150 /*<! Time between probes for no response. */
#define TCP_TV_SRTTBASE \
    0 /*<! Special value to denote no measurement yet for connection. */
#define TCP_TV_SRTTDFLT \
    6 /*<! Default RTT when no measurements yet for connection. */
/* End of TCP timer counter value. */

#define TCP_RTTDFT 3          /*<! Defaut RTT if no data. (3 seconds)*/
#define TCP_TIMER_PR_SLOWHZ 2 /*<! Number of timer ticks per seconds.*/

/**
 * Multipliers and shifters for RTT estimators.
 */
#define TCP_RTT_SCALE 8    /*<! For multiplying. */
#define TCP_RTT_SHIFT 3    /*<! For shifting. */
#define TCP_RTTVAR_SCALE 4 /*<! For multiplying. */
#define TCP_RTTVAR_SHIFT 2 /*<! For shifting. */

/**
 * RTO (Retransmission timeout) calculation.
 */
#define TCP_REXMTVAL(conn) \
    ((((conn)->rtt_est) >> TCP_RTT_SHIFT) + (conn)->rtt_var)

struct nstack_sockaddr;

/**
 * Allocate a UDP socket descriptor.
 */
struct nstack_sock *nstack_udp_alloc_sock(void);

int nstack_tcp_bind(struct nstack_sock *sock);
int nstack_tcp_send(struct nstack_sock *sock, const struct nstack_dgram *dgram);

/**
 * @}
 */
