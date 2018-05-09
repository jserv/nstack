/**
 * nstack UDP service.
 * @addtogroup UDP
 * @{
 */

#ifndef NSTACK_UDP_H
#define NSTACK_UDP_H

#include <stdint.h>

#include "linker_set.h"
#include "nstack_in.h"

#define UDP_MAXLEN 65507

/**
 * Type for an UDP port number.
 */
typedef uint16_t udp_port_t;

/**
 * UDP packet header.
 */
struct udp_hdr {
    udp_port_t udp_sport; /*!< UDP Source port. */
    udp_port_t udp_dport; /*!< UDP Destination port. */
    uint16_t udp_len;     /*!< UDP datagram length. */
    uint16_t udp_csum;    /*!< UDP Checksum. */
    uint8_t data[0];      /*!< Datagram contents. */
};

struct nstack_sock;
struct nstack_dgram;

int nstack_udp_bind(struct nstack_sock *sock);
int nstack_udp_send(struct nstack_sock *sock, const struct nstack_dgram *dgram);

#endif /* NSTACK_UDP_H */

/**
 * @}
 */
