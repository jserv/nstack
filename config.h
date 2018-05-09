/**
 * @addtogroup NSTACK_CONFIG
 * @{
 */

#ifndef CONFIG_H
#define CONFIG_H

#define NSTACK_DATAGRAM_SIZE_MAX 4096

#define NSTACK_DATAGRAM_BUF_SIZE 16384

/**
 * Periodic IP event tick.
 * How often should periodic tasks run.
 * This is handled by IP but it's meant to be more generic.
 */
#define NSTACK_PERIODIC_EVENT_SEC 10

/**
 * ARP Configuration.
 * @{
 */

/**
 * ARP Cache size.
 * The size of ARP cache in entries.
 * If ARP runs out of slots it will free the oldest validdynamic entry in
 * the cache; if all entries all static and thus there is no more empty
 * slots left the ARP insert will fail.
 */
#define NSTACK_ARP_CACHE_SIZE 50

/**
 * @}
 */

/*
 * @{
 * IP Configuration.
 */

/**
 * RIB (Routing Information Base) size in the number of entries.
 */
#define NSTACK_IP_RIB_SIZE 5

/**
 * Max number of deferred IP packets.
 * Maximum number of IP packets waiting for transmission, ie. waiting for ARP
 * to provide a destination MAC address.
 */
#define NSTACK_IP_DEFER_MAX 20

/**
 * Unreachable destination IP.
 * + 0 = Drop silently
 * + 1 = Send ICMP Destination host unreachable
 */
#define NSTACK_IP_SEND_HOSTUNREAC 1

/**
 * The number of buffers reserved for IP fragment reassembly.
 */
#define NSTACK_IP_FRAGMENT_BUF 4

/**
 * IP fragment reassembly timer lower bound [sec].
 * The RFC recommends a default value of 15 seconds.
 */
#define NSTACK_IP_FRAGMENT_TLB 15

/**
 * @}
 */

#endif /* CONFIG_H */

/**
 * @}
 */
