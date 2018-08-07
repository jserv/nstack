#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <linux/if_arp.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "nstack_util.h"

#include "../logger.h"
#include "../nstack_ether.h"

#define DEFAULT_IF "eth0"
#define ETHER_MAX_IF 1

struct ether_linux {
    int el_fd;
    mac_addr_t el_mac;
    struct ifreq el_if_idx;
};

static struct ether_linux ether_if[ETHER_MAX_IF];
static int ether_next_handle;

static struct ether_linux *ether_handle2eth(int handle)
{
    if (handle >= ETHER_MAX_IF) {
        errno = ENODEV;
        return NULL;
    }
    return &ether_if[handle];
}

int ether_handle2addr(int handle, mac_addr_t addr)
{
    struct ether_linux *eth;

    if (!(eth = ether_handle2eth(handle))) {
        errno = ENODEV;
        return -1;
    }

    memcpy(addr, eth->el_mac, sizeof(mac_addr_t));
    return 0;
}

int ether_addr2handle(const mac_addr_t addr __unused)
{
    return 0; /* TODO Implementation of ether_get_handle() */
}

static int linux_ether_bind(struct ether_linux *eth)
{
    struct ifreq ifopts = {0};
    struct sockaddr_ll socket_address = {0};
    int sockopt, retval;

    /* Set the interface to promiscuous mode. */
    strncpy(ifopts.ifr_name, eth->el_if_idx.ifr_name, IFNAMSIZ - 1);
    ioctl(eth->el_fd, SIOCGIFFLAGS, &ifopts);
    ifopts.ifr_flags |= IFF_PROMISC;
    ioctl(eth->el_fd, SIOCSIFFLAGS, &ifopts);
    /* Allow the socket to be reused. */
    retval = setsockopt(eth->el_fd, SOL_SOCKET, SO_REUSEADDR, &sockopt,
                        sizeof(sockopt));
    if (retval == -1)
        return -1;

    socket_address.sll_family = AF_PACKET;
    socket_address.sll_protocol = htons(ETH_P_ALL);
    socket_address.sll_ifindex = eth->el_if_idx.ifr_ifindex;
    socket_address.sll_pkttype =
        PACKET_OTHERHOST | PACKET_BROADCAST | PACKET_MULTICAST | PACKET_HOST;
    /*socket_address.sll_pkttype = PACKET_HOST;*/
    socket_address.sll_halen = ETHER_ALEN,
    socket_address.sll_addr[0] = eth->el_mac[0],
    socket_address.sll_addr[1] = eth->el_mac[1],
    socket_address.sll_addr[2] = eth->el_mac[2],
    socket_address.sll_addr[3] = eth->el_mac[3],
    socket_address.sll_addr[4] = eth->el_mac[4],
    socket_address.sll_addr[5] = eth->el_mac[5],
    socket_address.sll_hatype = 0x0000;
    bind(eth->el_fd, (struct sockaddr *) &socket_address,
         sizeof(socket_address));

    return 0;
}

static int linux_ether_set_rxtimeout(struct ether_linux *eth)
{
    struct timeval tv = {
        .tv_sec = NSTACK_PERIODIC_EVENT_SEC,
    };

    return setsockopt(eth->el_fd, SOL_SOCKET, SO_RCVTIMEO, (char *) &tv,
                      sizeof(struct timeval));
}

int ether_init(char *const args[])
{
    const int handle = ether_next_handle;
    struct ether_linux *eth;
    char if_name[IFNAMSIZ];
    struct ifreq if_mac;

    if (handle >= ETHER_MAX_IF) {
        errno = EAGAIN;
        return -1;
    }
    eth = &ether_if[handle];
    ether_next_handle++;

    /* TODO Parse args */
    if (args[0]) { /* Non-default IF */
        strcpy(if_name, args[0]);
    } else { /* Default IF */
        strcpy(if_name, DEFAULT_IF);
    }

    if ((eth->el_fd = socket(AF_PACKET, SOCK_RAW, IPPROTO_RAW)) == -1)
        return -1;

    /* Get the index of the interface */
    memset(&eth->el_if_idx, 0, sizeof(struct ifreq));
    strncpy(eth->el_if_idx.ifr_name, if_name, IFNAMSIZ - 1);
    if (ioctl(eth->el_fd, SIOCGIFINDEX, &eth->el_if_idx) < 0)
        goto fail;

    /* Get the MAC address of the interface */
    if (args[0] && args[1]) { /* MAC addr given by the user */
        /* TODO Parse MAC addr */
        errno = ENOTSUP;
        return -1;
    } else { /* Use the default MAC addr */
        memset(&if_mac, 0, sizeof(struct ifreq));
        strncpy(if_mac.ifr_name, if_name, IFNAMSIZ - 1);
        if (ioctl(eth->el_fd, SIOCGIFHWADDR, &if_mac) < 0) {
            goto fail;
        }
        eth->el_mac[0] = ((uint8_t *) &if_mac.ifr_hwaddr.sa_data)[0];
        eth->el_mac[1] = ((uint8_t *) &if_mac.ifr_hwaddr.sa_data)[1];
        eth->el_mac[2] = ((uint8_t *) &if_mac.ifr_hwaddr.sa_data)[2];
        eth->el_mac[3] = ((uint8_t *) &if_mac.ifr_hwaddr.sa_data)[3];
        eth->el_mac[4] = ((uint8_t *) &if_mac.ifr_hwaddr.sa_data)[4];
        eth->el_mac[5] = ((uint8_t *) &if_mac.ifr_hwaddr.sa_data)[5];
    }

    if (linux_ether_bind(eth))
        goto fail;

    if (linux_ether_set_rxtimeout(eth))
        goto fail;

    return handle;
fail:
    close(eth->el_fd);
    return -1;
}

void ether_deinit(int handle)
{
    struct ether_linux *eth;

    if (!(eth = ether_handle2eth(handle)))
        return;

    close(eth->el_fd);
}

int ether_receive(int handle, struct ether_hdr *hdr, uint8_t *buf, size_t bsize)
{
    struct ether_linux *eth;
    uint8_t frame[ETHER_MAXLEN] __attribute__((aligned));
    struct ether_hdr *frame_hdr = (struct ether_hdr *) frame;
    int retval;

    assert(hdr != NULL);
    assert(buf != NULL);

    if (!(eth = ether_handle2eth(handle)))
        return -1;

    do {
        retval =
            (int) recvfrom(eth->el_fd, frame, sizeof(frame), 0, NULL, NULL);
        if (retval == -1 &&
            (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS)) {
            return 0;
        } else if (retval == -1) {
            return -1;
        }
    } while (!memcmp(frame_hdr->h_src, eth->el_mac, sizeof(mac_addr_t)));

    memcpy(hdr->h_dst, frame_hdr->h_dst, sizeof(mac_addr_t));
    memcpy(hdr->h_src, frame_hdr->h_src, sizeof(mac_addr_t));
    hdr->h_proto = ntohs(frame_hdr->h_proto);

    retval -= ETHER_HEADER_LEN;
    memcpy(buf, frame + ETHER_HEADER_LEN, min(retval, bsize));

    return retval;
}

int ether_send(int handle,
               const mac_addr_t dst,
               uint16_t proto,
               uint8_t *buf,
               size_t bsize)
{
    struct ether_linux *eth;
    struct sockaddr_ll socket_address;
    const size_t frame_size = ETHER_HEADER_LEN +
                              max(bsize, ETHER_MINLEN - ETHER_FCS_LEN) +
                              ETHER_FCS_LEN;
    uint8_t frame[frame_size] __attribute__((aligned));
    uint32_t fcs;
    uint8_t *data = frame + ETHER_HEADER_LEN;
    struct ether_hdr *frame_hdr = (struct ether_hdr *) frame;
    uint32_t *fcs_p = (uint32_t *) &frame[frame_size - ETHER_FCS_LEN];
    int retval;

    assert(buf != NULL);

    if (frame_size > ETHER_MAXLEN + ETHER_FCS_LEN) {
        retval = -EMSGSIZE;
        goto out;
    }

    if (!(eth = ether_handle2eth(handle))) {
        retval = -errno;
        goto out;
    }

    socket_address = (struct sockaddr_ll){
        .sll_family = AF_PACKET,
        .sll_protocol = htons(proto),
        .sll_ifindex = eth->el_if_idx.ifr_ifindex,
        .sll_halen = ETHER_ALEN,
        .sll_addr[0] = dst[0],
        .sll_addr[1] = dst[1],
        .sll_addr[2] = dst[2],
        .sll_addr[3] = dst[3],
        .sll_addr[4] = dst[4],
        .sll_addr[5] = dst[5],
    };

    memcpy(frame_hdr->h_dst, dst, ETHER_ALEN);
    memcpy(frame_hdr->h_src, eth->el_mac, ETHER_ALEN);
    frame_hdr->h_proto = htons(proto);
    memcpy(data, buf, bsize);
    memset(data + bsize, 0, frame_size - ETHER_HEADER_LEN - bsize);
    fcs = ether_fcs(frame, frame_size - ETHER_FCS_LEN);
    memcpy(fcs_p, &fcs, sizeof(uint32_t));

    retval = (int) sendto(eth->el_fd, frame, frame_size, 0,
                          (struct sockaddr *) (&socket_address),
                          sizeof(socket_address));
    if (retval < 0)
        retval = -errno;
out:
    return retval;
}
