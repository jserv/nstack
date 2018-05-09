#include <errno.h>

#include "logger.h"
#include "nstack_ether.h"

SET_DECLARE(_ether_proto_handlers, struct _ether_proto_handler);

const mac_addr_t mac_broadcast_addr = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

int ether_input(const struct ether_hdr *hdr, uint8_t *payload, size_t bsize)
{
    struct _ether_proto_handler **tmpp;
    struct _ether_proto_handler *proto;
    int retval;

    SET_FOREACH(tmpp, _ether_proto_handlers)
    {
        proto = *tmpp;
        if (proto->proto_id == hdr->h_proto) {
            break;
        }
        proto = NULL;
    }

    LOG(LOG_DEBUG, "proto id: 0x%x", (unsigned) hdr->h_proto);

    if (proto) {
        retval = proto->fn(hdr, payload, bsize);
        if (retval < 0) {
            errno = -retval;
            retval = -1;
        }
    } else {
        errno = EPROTONOSUPPORT;
        retval = -1;
    }

    return retval;
}

int ether_output_reply(int ether_handle,
                       const struct ether_hdr *hdr,
                       uint8_t *payload,
                       size_t bsize)
{
    int retval;

    retval = ether_send(ether_handle, hdr->h_src, hdr->h_proto, payload, bsize);
    if (retval < 0) {
        errno = -retval;
        retval = -1;
    }

    return retval;
}
