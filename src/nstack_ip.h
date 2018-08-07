/**
 * nstack IP service.
 * @addtogroup IP
 * @{
 */

#ifndef NSTACK_IP_H
#define NSTACK_IP_H

#include "linker_set.h"
#include "nstack_ether.h"
#include "nstack_in.h"

#define IP_STR_LEN 17

/**
 * IP Route descriptor.
 */
struct ip_route {
    in_addr_t r_network; /*!< Network address. */
    in_addr_t r_netmask; /*!< Network mask. */
    in_addr_t r_gw;      /*!< Gateway IP. */
    in_addr_t r_iface;   /*!< Interface address. */
    int r_iface_handle;  /*!< Interface ether_handle. */
};

/**
 * IP Packet Header.
 */
struct ip_hdr {
    uint8_t ip_vhl;
    uint8_t ip_tos;
    uint16_t ip_len;
    uint16_t ip_id;
    uint16_t ip_foff;
    uint8_t ip_ttl;
    uint8_t ip_proto;
    uint16_t ip_csum;
    uint32_t ip_src;
    uint32_t ip_dst;
    uint8_t ip_opt[0];

} __attribute__((packed, aligned(4)));

/**
 * IP Packet Header Defaults
 * @{
 */
/* v4 and 5 * 4 octets */
#define IP_VHL_DEFAULT 0x45 /*!< Default value for version and ihl. */
#define IP_TOS_DEFAULT 0x0  /*!< Default type of service and no ECN */
#define IP_TOFF_DEFAULT 0x4000
#define IP_TTL_DEFAULT 64
/**
 * @}
 */

/**
 * IP Packet header values.
 */

/**
 * Get IP version.
 */
#define IP_VERSION(_ip_hdr_) (((ip_hdr)->ip_vhl & 0x40) >> 4)

#define IP_FALGS_DF 0x4000
#define IP_FLAGS_MF 0x2000

/**
 * Max IP packet size in bytes.
 */
#define IP_MAX_BYTES 65535

/**
 * IP protocol numbers.
 * @{
 */
#define IP_PROTO_ICMP 1
#define IP_PROTO_IGMP 2
#define IP_PROTO_TCP 6
#define IP_PROTO_UDP 17
#define IP_PROTO_SCTP 132
/**
 * @}
 */

/**
 * @}
 */

struct _ip_proto_handler {
    uint16_t proto_id;
    int (*fn)(const struct ip_hdr *hdr, uint8_t *payload, size_t bsize);
};

/**
 * Declare an IP input chain handler.
 */
#define IP_PROTO_INPUT_HANDLER(_proto_id_, _handler_fn_)                 \
    static struct _ip_proto_handler _ip_proto_handler_##_handler_fn_ = { \
        .proto_id = _proto_id_, .fn = _handler_fn_,                      \
    };                                                                   \
    DATA_SET(_ip_proto_handlers, _ip_proto_handler_##_handler_fn_)

int ip_config(int ether_handle, in_addr_t ip_addr, in_addr_t netmask);

/**
 * IP Packet manipulation.
 * @{
 */

/**
 * Calculate the Internet checksum.
 */
uint16_t ip_checksum(void *dp, size_t bsize);

/**
 * Get the header length of an IP packet.
 */
static inline size_t ip_hdr_hlen(const struct ip_hdr *ip)
{
    return (ip->ip_vhl & 0x0f) * 4;
}

/**
 * @}
 */

/**
 * RIB
 * @{
 */

/**
 * Update a route.
 * @param[in] route is a pointer to a route struct; the information will be
 *                  copied from the struct.
 */
int ip_route_update(struct ip_route *route);

/**
 * Remove a route from routing table.
 */
int ip_route_remove(struct ip_route *route);

/**
 * Get routing information for a network.
 * @param[out] route    is a pointer to a ip_route struct that will be updated
 *                      if a route is found.
 */
int ip_route_find_by_network(in_addr_t ip, struct ip_route *route);

/**
 * Get routing information for a source IP addess.
 * The function can be also used for source IP address validation by setting
 * route pointer argument to NULL.
 */
int ip_route_find_by_iface(in_addr_t addr, struct ip_route *route);

/**
 * @}
 */

/**
 * IP packet handling and manipulation.
 * @{
 */
void ip_hton(const struct ip_hdr *host, struct ip_hdr *net);
size_t ip_ntoh(const struct ip_hdr *net, struct ip_hdr *host);

int ip_input(const struct ether_hdr *e_hdr, uint8_t *payload, size_t bsize);

/**
 * Construct a reply header from a received IP packet header.
 * Swaps src and dst etc.
 * @param host_ip_hd is a pointer to a IP packet header that should be reversed.
 * @param bsize is the size of the packet data.
 * @returns Returns the size of the header.
 */
size_t ip_reply_header(struct ip_hdr *host_ip_hdr, size_t bsize);
/**
 * @}
 */

/**
 * Send an IP packet to a destination.
 */
int ip_send(in_addr_t dst, uint8_t proto, const uint8_t *buf, size_t bsize);

/**
 * IP Fragmentation
 * @{
 */

static inline int ip_fragment_is_frag(struct ip_hdr *hdr)
{
    return (!!(hdr->ip_foff & IP_FLAGS_MF) || !!(hdr->ip_foff & 0x1fff));
}

int ip_fragment_input(struct ip_hdr *ip_hdr, uint8_t *rx_packet);

/**
 * @}
 */

#endif /* NSTACK_IP_H */

/**
 * @}
 */
