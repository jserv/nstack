/**
 * @addtogroup ICMP
 * @{
 */

#ifndef NSTACK_ICMP_H
#define NSTACK_ICMP_H

#include <stdint.h>

#include "nstack_ip.h"

/**
 * ICMP message.
 */
struct icmp {
    uint8_t icmp_type;
    uint8_t icmp_code;
    uint16_t icmp_csum;
    uint32_t icmp_rest;
    uint8_t icmp_data[0];
} __attribute__((packed));

/**
 * ICMP destination unreachable message.
 */
struct icmp_destunreac {
    struct icmp icmp;
    struct ip_hdr old_ip_hdr;
    uint8_t data[8];
} __attribute__((packed));

/**
 * ICMP Types.
 * @{
 */
#define ICMP_TYPE_ECHO_REPLY 0
#define ICMP_TYPE_DESTUNREAC 3
#define ICMP_TYPE_ECHO_REQUEST 8
/**
 * @}
 */

/**
 * ICMP Codes for ICMP_TYPE_DESTUNREAC.
 * @{
 */
#define ICMP_CODE_DESTUNREAC 0  /*!< Network unreachable error. */
#define ICMP_CODE_HOSTUNREAC 1  /*!< Host unreachable error. */
#define ICMP_CODE_PROTOUNREAC 2 /*!< Protocol unreachable error. */
#define ICMP_CODE_PORTUNREAC 3  /*!< Port unreachable error. */
#define ICMP_CODE_DESTNETUNK 6  /*!< Destination network unknown error. */
#define ICMP_CODE_HOSTUNK 7     /*!< Destination host unknown error. */
/**
 * @}
 */

/**
 * Generate a ICMP destination unreachable message to buf.
 * This function will generate a directly returnable IP packet if hdr
 * is a pointer to a header stored in an ether buffer.
 * @param[in,out] hdr is the received header. It will be updated and
 *                converted to the network order.
 * @param[in] code is one of the ICMP_TYPE_DESTUNREAC error codes.
 * @param[in,out] is the packet buffer given by ether layer.
 * @param[in] bsize is the size of the frame given by ether layer.
 * @return Retuns the number of bytes written.
 */
int icmp_generate_dest_unreachable(struct ip_hdr *hdr,
                                   int code,
                                   uint8_t *buf,
                                   size_t bsize);

#endif /* NSTACK_ICMP_H */

/**
 * @}
 */
