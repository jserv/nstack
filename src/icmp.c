#include <errno.h>
#include <string.h>

#include "logger.h"
#include "nstack_icmp.h"
#include "nstack_ip.h"

static void icmp_hton(const struct icmp *host, struct icmp *net)
{
    net->icmp_type = host->icmp_type;
    net->icmp_code = host->icmp_code;
    net->icmp_csum = host->icmp_csum;
}

static void icmp_ntoh(const struct icmp *net, struct icmp *host)
{
    host->icmp_type = net->icmp_type;
    host->icmp_code = net->icmp_code;
    host->icmp_csum = net->icmp_csum;
}

static int icmp_input(const struct ip_hdr *ip_hdr __unused,
                      uint8_t *payload,
                      size_t bsize)
{
    struct icmp *net_msg = (struct icmp *) payload;
    struct icmp hdr;

    if (bsize < sizeof(struct icmp)) {
        LOG(LOG_ERR, "Invalid ICMP message size");

        return -EBADMSG;
    }

    icmp_ntoh(net_msg, &hdr);

    LOG(LOG_DEBUG, "ICMP type: %d", hdr.icmp_type);
    switch (hdr.icmp_type) {
    case ICMP_TYPE_ECHO_REQUEST:
        net_msg->icmp_type = ICMP_TYPE_ECHO_REPLY;
        net_msg->icmp_csum = 0;
        net_msg->icmp_csum = ip_checksum(net_msg, bsize);

        return bsize;
    default:
        LOG(LOG_INFO, "Unkown ICMP message type");

        return -ENOMSG;
    }
}
IP_PROTO_INPUT_HANDLER(IP_PROTO_ICMP, icmp_input);

int icmp_generate_dest_unreachable(struct ip_hdr *hdr,
                                   int code,
                                   uint8_t *buf,
                                   size_t bsize)
{
    struct icmp_destunreac *msg = (struct icmp_destunreac *) buf;
    size_t msg_size;

    /*
     * We assume there is always some space in the frame to move things around.
     */

    bsize = min(sizeof(msg->data), bsize);
    msg_size = sizeof(struct icmp_destunreac) + bsize;

    memmove(msg->data, buf, bsize);
    msg->icmp = (struct icmp){
        .icmp_type = ICMP_TYPE_DESTUNREAC,
        .icmp_code = code,
    };
    /* TODO Next-hop MTU if code is 4*/
    icmp_hton(&msg->icmp, &msg->icmp);
    msg->icmp.icmp_csum = ip_checksum(msg, msg_size);
    ip_hton(hdr, &msg->old_ip_hdr);

    hdr->ip_vhl = IP_VHL_DEFAULT;
    hdr->ip_tos = IP_TOS_DEFAULT;
    hdr->ip_proto = IP_PROTO_ICMP;
    msg_size += ip_reply_header(hdr, msg_size);

    return msg_size;
}
