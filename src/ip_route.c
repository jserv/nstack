#include <errno.h>
#include <stddef.h>
#include <string.h>

#include "nstack_util.h"

#include "collection.h"
#include "nstack_ip.h"
#include "tree.h"

struct ip_route_entry {
    struct ip_route route;
    RB_ENTRY(ip_route_entry) _rib_rtree_entry; /*!< Network tree. */
    RB_ENTRY(ip_route_entry) _rib_stree_entry; /*!< Source addr tree. */
    SLIST_ENTRY(ip_route_entry) _rib_freelist_entry;
};

RB_HEAD(rib_routetree, ip_route_entry);
RB_HEAD(rib_sourcetree, ip_route_entry);
SLIST_HEAD(rib_freelist, ip_route_entry);

static struct ip_route_entry rib[NSTACK_IP_RIB_SIZE];
static struct rib_routetree rib_routetree;
static struct rib_sourcetree rib_sourcetree;
static struct rib_freelist rib_freelist;

/**
 * Compare network addresses of two routes.
 */
static int route_network_cmp(struct ip_route_entry *a, struct ip_route_entry *b)
{
    return a->route.r_network - b->route.r_network;
}

static int route_iface_cmp(struct ip_route_entry *a, struct ip_route_entry *b)
{
    return a->route.r_iface - b->route.r_iface;
}

RB_GENERATE_STATIC(rib_routetree,
                   ip_route_entry,
                   _rib_rtree_entry,
                   route_network_cmp);
RB_GENERATE_STATIC(rib_sourcetree,
                   ip_route_entry,
                   _rib_stree_entry,
                   route_iface_cmp);

/**
 * Get a new route entry from the free list.
 */
static struct ip_route_entry *ip_route_entry_alloc(void)
{
    struct ip_route_entry *entry;

    entry = SLIST_FIRST(&rib_freelist);
    SLIST_REMOVE_HEAD(&rib_freelist, _rib_freelist_entry);

    return entry;
}

/**
 * Put a route entry back to the free list.
 */
static void ip_route_entry_free(struct ip_route_entry *entry)
{
    SLIST_INSERT_HEAD(&rib_freelist, entry, _rib_freelist_entry);
}

static void ip_route_tree_insert(struct ip_route_entry *entry)
{
    RB_INSERT(rib_routetree, &rib_routetree, entry);
    RB_INSERT(rib_sourcetree, &rib_sourcetree, entry);
}

static void ip_route_tree_remove(struct ip_route_entry *entry)
{
    RB_REMOVE(rib_routetree, &rib_routetree, entry);
    RB_REMOVE(rib_sourcetree, &rib_sourcetree, entry);
}

static int ip_route_add(struct ip_route *route)
{
    struct ip_route_entry *entry;

    entry = ip_route_entry_alloc();
    if (!entry) {
        errno = ENOMEM;
        return -1;
    }

    entry->route = *route;
    ip_route_tree_insert(entry);

    return 0;
}

int ip_route_update(struct ip_route *route)
{
    struct ip_route_entry *entry;

    entry =
        RB_FIND(rib_routetree, &rib_routetree, (struct ip_route_entry *) route);
    if (entry) { /* Update an existing entry. */
        ip_route_tree_remove(entry);
        entry->route = *route;
        ip_route_tree_insert(entry);
    } else { /* Route not found so we insert it. */
        if (ip_route_add(route))
            return -1;
    }

    return 0;
}

int ip_route_remove(struct ip_route *route)
{
    struct ip_route_entry *entry =
        RB_FIND(rib_routetree, &rib_routetree, (struct ip_route_entry *) route);
    if (!entry) {
        errno = ENOENT;
        return -1;
    }

    ip_route_tree_remove(entry);
    memset(entry, 0, sizeof(struct ip_route_entry));
    ip_route_entry_free(entry);

    return 0;
}

int ip_route_find_by_network(in_addr_t addr, struct ip_route *route)
{
    struct ip_route find[] = {{.r_network = addr}, {.r_network = 0}};
    struct ip_route_entry *entry = NULL;

    switch (0) {
    case 0: /* First we try exact match */
        entry = RB_FIND(rib_routetree, &rib_routetree,
                        (struct ip_route_entry *) (&find));
        if (entry)
            goto match;
    case 1: /* Then with network masks */
        RB_FOREACH (entry, rib_routetree, &rib_routetree) {
            if (entry->route.r_network == (addr & entry->route.r_netmask)) {
                goto match;
            }
        }
    default: /* And finally we check if there is a default gw */
        entry = RB_FIND(rib_routetree, &rib_routetree,
                        (struct ip_route_entry *) (find + 1));
        if (entry)
            goto match;
    }

match:
    if (!entry) {
        errno = ENOENT;
        return -1;
    }

    if (route)
        memcpy(route, &entry->route, sizeof(struct ip_route));

    return 0;
}

int ip_route_find_by_iface(in_addr_t addr, struct ip_route *route)
{
    struct ip_route find = {.r_iface = addr};
    struct ip_route_entry *entry;

    entry = RB_FIND(rib_sourcetree, &rib_sourcetree,
                    (struct ip_route_entry *) (&find));
    if (!entry) {
        errno = ENOENT;
        return -1;
    }

    if (route)
        memcpy(route, &entry->route, sizeof(struct ip_route));

    return 0;
}

__constructor void ip_route_init(void)
{
    RB_INIT(&rib_routetree);
    RB_INIT(&rib_sourcetree);
    SLIST_INIT(&rib_freelist);

    for (size_t i = 0; i < num_elem(rib); i++) {
        SLIST_INSERT_HEAD(&rib_freelist, &rib[i], _rib_freelist_entry);
    }
}
