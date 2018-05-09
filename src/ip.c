#include <errno.h>
#include <string.h>

#include "nstack_in.h"

#include "ip_defer.h"
#include "logger.h"
#include "nstack_arp.h"
#include "nstack_icmp.h"
#include "nstack_ip.h"

SET_DECLARE(_ip_proto_handlers, struct _ip_proto_handler);

static unsigned ip_global_id; /* Global ID for IP packets. */

int ip_config(int ether_handle, in_addr_t ip_addr, in_addr_t netmask)
{
    mac_addr_t mac;
    struct ip_route route = {
        .r_network = ip_addr & netmask,
        .r_netmask = netmask,
        .r_gw = 0, /* TODO GW support */
        .r_iface = ip_addr,
        .r_iface_handle = ether_handle,
    };

    ether_handle2addr(ether_handle, mac);
    arp_cache_insert(ip_addr, mac, ARP_CACHE_STATIC);

    ip_route_update(&route);

    /* Announce that we are online. */
    for (size_t i = 0; i < 3; i++) {
        arp_gratuitous(ether_handle, ip_addr);
    }

    return 0;
}

uint16_t ip_checksum(void *dp, size_t bsize)
{
    uint8_t *data = (uint8_t *) dp;
    uint32_t acc = 0xffff;
    size_t i;
    uint16_t word;

    for (i = 0; i + 1 < bsize; i += 2) {
        memcpy(&word, data + i, 2);
        acc += word;
        if (acc > 0xffff) {
            acc -= ntohs(0xffff);
        }
    }

    if (bsize & 1) {
        word = 0;
        memcpy(&word, data + bsize - 1, 1);
        acc += word;
        if (acc > 0xffff) {
            acc -= ntohs(0xffff);
        }
    }

    return ~acc;
}

void ip_hton(const struct ip_hdr *host, struct ip_hdr *net)
{
    size_t hlen = ip_hdr_hlen(host);

    net->ip_vhl = host->ip_vhl;
    net->ip_tos = host->ip_tos;
    net->ip_len = htons(host->ip_len);
    net->ip_id = htons(host->ip_id);
    net->ip_foff = htons(host->ip_foff);
    net->ip_ttl = host->ip_ttl;
    net->ip_proto = host->ip_proto;
    net->ip_csum = host->ip_csum;
    net->ip_src = htonl(host->ip_src);
    net->ip_dst = htonl(host->ip_dst);

    net->ip_csum = 0;
    net->ip_csum = ip_checksum(net, hlen);
}

size_t ip_ntoh(const struct ip_hdr *net, struct ip_hdr *host)
{
    host->ip_vhl = net->ip_vhl;
    host->ip_tos = net->ip_tos;
    host->ip_len = ntohs(net->ip_len);
    host->ip_id = ntohs(net->ip_id);
    host->ip_foff = ntohs(net->ip_foff);
    host->ip_ttl = net->ip_ttl;
    host->ip_proto = net->ip_proto;
    host->ip_csum = net->ip_csum;
    host->ip_src = ntohl(net->ip_src);
    host->ip_dst = ntohl(net->ip_dst);

    return ip_hdr_hlen(host);
}

size_t ip_reply_header(struct ip_hdr *host_ip_hdr, size_t bsize)
{
    struct ip_hdr *const ip = host_ip_hdr;
    in_addr_t tmp;

    /* Swap source and destination. */
    tmp = ip->ip_src;
    ip->ip_src = ip->ip_dst;
    ip->ip_dst = tmp;
    ip->ip_ttl = IP_TTL_DEFAULT;

    bsize += ip_hdr_hlen(ip);
    ip->ip_len = bsize;

    /* Back to network order */
    ip_hton(ip, ip);

    return bsize;
}

int ip_input(const struct ether_hdr *e_hdr, uint8_t *payload, size_t bsize)
{
    struct ip_hdr *ip = (struct ip_hdr *) payload;
    struct _ip_proto_handler **tmpp;
    struct _ip_proto_handler *proto;
    size_t hlen;

    if (e_hdr) {
        ip_ntoh(ip, ip);
    }

    if ((ip->ip_vhl & 0x40) != 0x40) {
        LOG(LOG_ERR, "Unsupported IP packet version: 0x%x", ip->ip_vhl);
        return 0;
    }

    hlen = ip_hdr_hlen(ip);
    if (hlen < 20) {
        LOG(LOG_ERR, "Incorrect packet header length: %d", (int) hlen);
        return 0;
    }

    if (ip->ip_len != bsize) {
        LOG(LOG_ERR, "Packet size mismatch. iplen = %d, bsize = %d",
            (int) ip->ip_len, (int) bsize);
        return 0;
    }

    /*
     * RFE The packet header is already modified with ntoh so this wont work.
     */
#if 0
    if (ip_checksum(ip, hlen) != 0) {
        LOG(LOG_ERR, "Drop due to an invalid checksum");
        return;
    }
#endif

    if (ip->ip_tos != IP_TOS_DEFAULT) {
        LOG(LOG_INFO, "Unsupported IP type of service or ECN: 0x%x",
            ip->ip_tos);
    }

    if (e_hdr) {
        /* Insert to ARP table so it's possible/faster to send a reply. */
        arp_cache_insert(ip->ip_src, e_hdr->h_src, ARP_CACHE_DYN);
    }

    if (ip_route_find_by_iface(ip->ip_dst, NULL)) {
        char dst_str[IP_STR_LEN];

        ip2str(ip->ip_dst, dst_str);
        LOG(LOG_WARN, "Invalid destination address %s", dst_str);

        if (NSTACK_IP_SEND_HOSTUNREAC) {
            return icmp_generate_dest_unreachable(ip, ICMP_CODE_HOSTUNREAC,
                                                  payload + hlen, bsize - hlen);
        }
        return 0;
    }

    if (ip_fragment_is_frag(ip)) {
        /*
         * Fragmented packet must be first reassembled.
         */
        ip_fragment_input(ip, payload + hlen);

        return 0;
    }

    SET_FOREACH(tmpp, _ip_proto_handlers)
    {
        proto = *tmpp;
        if (proto->proto_id == ip->ip_proto) {
            break;
        }
        proto = NULL;
    }

    LOG(LOG_DEBUG, "proto id: 0x%x", ip->ip_proto);

    if (proto) {
        int retval;

        retval = proto->fn(ip, payload + hlen, bsize - hlen);
        if (retval > 0) {
            retval = ip_reply_header(ip, retval);
        }
        if (retval == -ENOTSOCK) {
            LOG(LOG_INFO, "Unreachable port");

            return icmp_generate_dest_unreachable(ip, ICMP_CODE_PORTUNREAC,
                                                  payload + hlen, bsize - hlen);
        }
        return retval;
    } else {
        LOG(LOG_INFO, "Unsupported protocol");

        return icmp_generate_dest_unreachable(ip, ICMP_CODE_PROTOUNREAC,
                                              payload + hlen, bsize - hlen);
    }
}
ETHER_PROTO_INPUT_HANDLER(ETHER_PROTO_IPV4, ip_input);

static inline size_t ip_off_round(size_t plen)
{
    return (plen + 7) & ~7;
}

static size_t next_fragment_size(size_t bytes, size_t hlen, size_t mtu)
{
    size_t max, retval;

    max = ip_off_round(mtu - hlen - 8); /* RFE A kernel bug? */
    retval = (bytes < max) ? bytes : max;

    return retval;
}

static int ip_send_fragments(int ether_handle,
                             const mac_addr_t dst_mac,
                             uint8_t *payload,
                             size_t bsize)
{
    struct ip_hdr *ip_hdr_net = (struct ip_hdr *) payload;
    struct ip_hdr ip_hdr;
    uint8_t *data;
    size_t hlen, bytes, offset = 0;
    int retval = 0;

    hlen = ip_ntoh(ip_hdr_net, &ip_hdr);
    data = payload + hlen;
    bytes = bsize - hlen;
    do {
        size_t plen;
        int eret;

        plen = next_fragment_size(bytes, hlen, ETHER_DATA_LEN);
        bytes -= plen;
        ip_hdr.ip_len = hlen + plen;
        ip_hdr.ip_foff = ((bytes != 0) ? IP_FLAGS_MF : 0) | (offset >> 3);

        ip_hton(&ip_hdr, ip_hdr_net);
        memmove(data, data + offset, plen);
        eret = ether_send(ether_handle, dst_mac, ETHER_PROTO_IPV4, payload,
                          ip_hdr.ip_len);
        if (eret < 0) {
            return eret;
        }
        retval += eret;
        offset += plen;
    } while (bytes > 0);

    return retval;
}

static const struct ip_hdr ip_hdr_template = {
    .ip_vhl = IP_VHL_DEFAULT,
    .ip_tos = IP_TOS_DEFAULT,
    .ip_foff = IP_TOFF_DEFAULT,
    .ip_ttl = IP_TTL_DEFAULT,
};

int ip_send(in_addr_t dst, uint8_t proto, const uint8_t *buf, size_t bsize)
{
    mac_addr_t dst_mac;
    size_t packet_size = sizeof(struct ip_hdr) + bsize;
    struct ip_route route;

    if (ip_route_find_by_network(dst, &route)) {
        char ip_str[IP_STR_LEN];

        ip2str(dst, ip_str);
        LOG(LOG_ERR, "No route to host %s", ip_str);
        errno = EHOSTUNREACH;
        return -1;
    }

    if (arp_cache_get_haddr(route.r_iface, dst, dst_mac)) {
        int retval = 0;

        if (errno == EHOSTUNREACH) {
            /*
             * We must defer the operation for now because we are waiting for
             * the reveiver's MAC addr to be resolved.
             */
            retval = ip_defer_push(dst, proto, buf, bsize);
            if (retval == 0 || (retval == -EALREADY)) {
                retval = 0; /* Return 0 to indicate an defered operation. */
            } else {        /* else an error occured. */
                errno = -retval;
                retval = -1;
            }
        }
        return retval;
    }

    {
        uint8_t packet[packet_size];
        struct ip_hdr *hdr = (struct ip_hdr *) packet;
        int retval;

        memcpy(hdr, &ip_hdr_template, sizeof(ip_hdr_template));
        hdr->ip_len = packet_size;
        hdr->ip_id = ip_global_id++;
        hdr->ip_src = route.r_iface;
        hdr->ip_dst = dst;
        hdr->ip_proto = proto;
        memcpy(packet + sizeof(ip_hdr_template), buf, bsize);
        ip_hton(hdr, hdr);

        if (bsize <= ETHER_DATA_LEN) {
            retval = ether_send(route.r_iface_handle, dst_mac, ETHER_PROTO_IPV4,
                                packet, packet_size);
        } else if (1) { /* Check DF flag */
            retval = ip_send_fragments(route.r_iface_handle, dst_mac, packet,
                                       packet_size);
            if (retval < 0) {
                errno = -retval;
                retval = -1;
            }
        } else {
            /* TODO Fail properly */
            errno = EMSGSIZE;
            retval = -1;
        }

        return retval;
    }
}
