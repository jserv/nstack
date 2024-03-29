/**
 * @addtogroup ip_defer
 * IP defer can be used to defer IP packet transmission processing.
 * This is useful for example while waiting for an ARP reply.
 * @{
 */

#pragma once

#include "nstack_in.h"

int ip_defer_push(in_addr_t dst,
                  uint8_t proto,
                  const uint8_t *buf,
                  size_t bsize);

void ip_defer_handler(int delta_time);

/**
 * @}
 */
