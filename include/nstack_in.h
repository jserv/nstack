#ifndef NSTACK_IN_H
#define NSTACK_IN_H

#include <arpa/inet.h> /* TODO Maybe we want to define our own version */
#include <stdint.h>
#include <stdio.h>

typedef uint32_t in_addr_t;
typedef uint16_t in_port_t;

/**
 * Convert an IP address from integer representation to a C string.
 * @note The minimum size of buf is IP_STR_LEN.
 * @param[in] ip is the IP address to be converted.
 * @param[out] buf is the destination buffer.
 */
static inline void ip2str(in_addr_t ip, char *buf)
{
    unsigned char bytes[4];
    bytes[0] = ip & 0xFF;
    bytes[1] = (ip >> 8) & 0xFF;
    bytes[2] = (ip >> 16) & 0xFF;
    bytes[3] = (ip >> 24) & 0xFF;
    sprintf(buf, "%d.%d.%d.%d", bytes[3], bytes[2], bytes[1], bytes[0]);
}

#endif /* NSTACK_IN_H */
