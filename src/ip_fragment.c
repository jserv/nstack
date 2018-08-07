#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "nstack_in.h"

#include "logger.h"
#include "nstack_ip.h"
#include "tree.h"

#define FRAG_MAX 8192
#define FRAG_MAP_SIZE (FRAG_MAX / 32)
#define FRAG_MAP_AI(_i_) ((_i_) >> 5)
#define FRAG_MAP_BI(_i_) ((_i_) &0x1f)

struct fragment_map {
    uint32_t fragmap[FRAG_MAP_SIZE];
};

struct packet_buf {
    int reserved;
    int timer;
    struct fragment_map fragmap;
    struct ip_hdr ip_hdr;
    uint8_t payload[IP_MAX_BYTES];
    RB_ENTRY(packet_buf) _entry;
};

RB_HEAD(packet_buf_tree, packet_buf);

static struct packet_buf packet_buffer[4];
static struct packet_buf_tree packet_buffer_head = RB_INITIALIZER();

/*
 * TODO Timer for giving up
 */

static int packet_buf_cmp(struct packet_buf *a, struct packet_buf *b)
{
    /* Bufid according to RFC 791 */
    struct bufid {
        typeof(a->ip_hdr.ip_src) src;
        typeof(a->ip_hdr.ip_dst) dst;
        typeof(a->ip_hdr.ip_proto) proto;
        typeof(a->ip_hdr.ip_id) id;
    } bufid[2];

    memset(bufid, 0, sizeof(bufid));

    bufid[0] = (struct bufid){
        .src = a->ip_hdr.ip_src,
        .dst = a->ip_hdr.ip_dst,
        .proto = a->ip_hdr.ip_proto,
        .id = a->ip_hdr.ip_id,
    };
    bufid[1] = (struct bufid){
        .src = b->ip_hdr.ip_src,
        .dst = b->ip_hdr.ip_dst,
        .proto = b->ip_hdr.ip_proto,
        .id = b->ip_hdr.ip_id,
    };

    return memcmp(&bufid[0], &bufid[1], sizeof(struct bufid));
}

RB_GENERATE_STATIC(packet_buf_tree, packet_buf, _entry, packet_buf_cmp);

static inline void fragmap_init(struct fragment_map *map)
{
    memset(map->fragmap, 0, FRAG_MAP_SIZE);
}

static inline void fragmap_set(struct fragment_map *map, unsigned i)
{
    map->fragmap[FRAG_MAP_AI(i)] |= 1 << FRAG_MAP_BI(i);
}

static inline void fragmap_clear(struct fragment_map *map, unsigned i)
{
    map->fragmap[FRAG_MAP_AI(i)] &= ~(1 << FRAG_MAP_BI(i));
}

static inline int fragmap_tst(struct fragment_map *map, unsigned i)
{
    return (map->fragmap[FRAG_MAP_AI(i)] & (1 << FRAG_MAP_BI(i)));
}

static inline void release_packet_buffer(struct packet_buf *p)
{
    RB_REMOVE(packet_buf_tree, &packet_buffer_head, p);
    __sync_lock_release(&p->reserved);
}

struct packet_buf *get_packet_buffer(struct ip_hdr *hdr)
{
    struct packet_buf find = {
        .ip_hdr.ip_id = hdr->ip_id,
        .ip_hdr.ip_proto = hdr->ip_proto,
        .ip_hdr.ip_src = hdr->ip_src,
        .ip_hdr.ip_dst = hdr->ip_dst,
    };
    struct packet_buf *p;
    size_t i;

    /*
     * Try to find it.
     */
    p = RB_FIND(packet_buf_tree, &packet_buffer_head, &find);
    if (p)
        return p;

    /*
     * Not yet allocated, so allocate a new buffer.
     */
    for (i = 0; i < num_elem(packet_buffer); i++) {
        struct packet_buf *p = packet_buffer + i;
        const int old = __sync_lock_test_and_set(&p->reserved, 1);

        if (old == 0) {
            fragmap_init(&p->fragmap);
            p->timer = NSTACK_IP_FRAGMENT_TLB;
            p->ip_hdr = *hdr; /* RFE Clear things that aren't needed. */
            p->ip_hdr.ip_foff = 0;
            p->ip_hdr.ip_len = 0;

            if (RB_INSERT(packet_buf_tree, &packet_buffer_head, p)) {
                release_packet_buffer(p);

                return NULL;
            }

            return p;
        }
    }

    return NULL;
}

int ip_fragment_input(struct ip_hdr *ip_hdr, uint8_t *rx_packet)
{
    const size_t off = (ip_hdr->ip_foff & 0x1fff) << 3;
    struct packet_buf *p;
    size_t i;

    if (off > IP_MAX_BYTES)
        return -EMSGSIZE;

    p = get_packet_buffer(ip_hdr);
    if (!p) {
        LOG(LOG_WARN, "Out of fragment buffers");
        return -ENOBUFS;
    }

    memcpy(p->payload + off, rx_packet, ip_hdr->ip_len - ip_hdr_hlen(ip_hdr));
    for (i = off >> 3;
         i < (off >> 3) + ((ip_hdr->ip_len - ip_hdr_hlen(ip_hdr) + 7) >> 3);
         i++) {
        fragmap_set(&p->fragmap, i);
    }

    if (off == 0) {
        p->ip_hdr = *ip_hdr;
        p->ip_hdr.ip_len = 0;
        p->ip_hdr.ip_foff = 0;
    } else if (!(ip_hdr->ip_foff & IP_FLAGS_MF)) {
        p->ip_hdr.ip_len = ip_hdr->ip_len - ip_hdr_hlen(ip_hdr) + off;
    }

    if (p->ip_hdr.ip_len != 0) {
        int t = 0;

        for (i = 0; i < (size_t)((p->ip_hdr.ip_len + 7) >> 3); i++) {
            t |= !fragmap_tst(&p->fragmap, i);
        }
        if (!t) {
            int retval;

            LOG(LOG_DEBUG, "Fragmented packet was fully reassembled (len: %u)",
                (unsigned) p->ip_hdr.ip_len);
            retval = ip_input(NULL, (uint8_t *) (&p->ip_hdr), p->ip_hdr.ip_len);

            ip_ntoh(&p->ip_hdr, &p->ip_hdr);
            retval = ip_send(p->ip_hdr.ip_dst, p->ip_hdr.ip_proto, p->payload,
                             retval);
            if (retval < 0) {
                LOG(LOG_ERR, "Failed to send fragments");
            }

            release_packet_buffer(p);
        }
    }

/*
 * Commenting out this line breaks the RFC but greatly reduces DOS
 * possibilities againts the fragment reassembly implementation.
 */
#if 0
    p->timer = imax(ip->ip_hdr.ip_ttl, p->timer);
#endif

    return 0;
}

void ip_fragment_timer(int delta_time)
{
    for (size_t i = 0; i < num_elem(packet_buffer); i++) {
        struct packet_buf *p = &packet_buffer[i];

        /* TODO not actually as thread safe as the allocation. */
        if (!p->reserved)
            continue;

        p->timer -= delta_time;
        if (p->timer <= 0)
            release_packet_buffer(p);
    }
}
