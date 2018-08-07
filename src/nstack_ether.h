/**
 * @addtogroup Ether
 * @{
 */

#ifndef NSTACK_ETHER_H
#define NSTACK_ETHER_H

#include <stddef.h>
#include <stdint.h>

#include "linker_set.h"
#include "nstack_link.h"

/**
 * Ethernet frame
 * @verbatim
 * +---------+---------+---------+-----------+-----+
 * | dst MAC | src MAC | Type ID | Data      | FCS |
 * +---------+---------+---------+-----------+-----+
 *  6         6         2         45 - 1500
 * |-----------------------------|-----------------|
 *          Datalink header         Data and CRC
 * @endverbatim
 * @{
 */
#define ETHER_ALEN LINK_MAC_ALEN
#define ETHER_HEADER_LEN 14
#define ETHER_DATA_LEN 1500 /*!< Max length of data */
#define ETHER_FCS_LEN 4
#define ETHER_MINLEN 60
#define ETHER_MAXLEN 1514
/**
 * @}
 */

/**
 * Protocol type IDs.
 * @{
 */
#define ETHER_PROTO_LOOP 0x0060  /*!< Loopback */
#define ETHER_PROTO_IPV4 0x0800  /*!< IPv4 */
#define ETHER_PROTO_ARP 0x0806   /*!< Address Resolution Protocol */
#define ETHER_PROTO_RARP 0x8035  /*!< Reverse Address Resolution Protocol */
#define ETHER_PROTO_WOL 0x0842   /*!< Wake-on-LAN */
#define ETHER_PROTO_8021Q 0x8100 /*!< VLAN-tagged frame */
#define ETHER_PROTO_IPV6 0x86DD  /*!< IPv6 */
/**
 * @}
 */

/**
 * Ethernet frame header.
 */
struct ether_hdr {
    mac_addr_t h_dst; /*!< Destination ethernet address */
    mac_addr_t h_src; /*!< Source ethernet address */
    uint16_t h_proto; /*!< Packet type ID */
} __attribute__((packed));

struct _ether_proto_handler {
    uint16_t proto_id;
    int (*fn)(const struct ether_hdr *hdr, uint8_t *payload, size_t bsize);
};

/**
 * Declare an ethernet input chain handler.
 */
#define ETHER_PROTO_INPUT_HANDLER(_proto_id_, _handler_fn_)                    \
    static struct _ether_proto_handler _ether_proto_handler_##_handler_fn_ = { \
        .proto_id = _proto_id_, .fn = _handler_fn_,                            \
    };                                                                         \
    DATA_SET(_ether_proto_handlers, _ether_proto_handler_##_handler_fn_)


const mac_addr_t mac_broadcast_addr;

int ether_init(char *const args[]);
void ether_deinit(int ether_handle);
uint32_t ether_fcs(const void *data, size_t bsize);

/* Platform dependent functions */

/**
 * Get the MAC address of an interface.
 * @param[in] handle is the ether handle.
 * @param[out] addr is the destination buffer.
 */
int ether_handle2addr(int handle, mac_addr_t addr);

/**
 * Get the corresponding handle of an MAC address.
 */
int ether_addr2handle(const mac_addr_t addr);

/**
 * Raw Ethernet RX and TX functions.
 * @{
 */

/**
 * Receive a frame from ether.
 * @retval >0 the size of the received frame;
 * @retval  0 read timed out;
 * @retval -1 an read error occured, errno is set.
 */
int ether_receive(int handle,
                  struct ether_hdr *hdr,
                  uint8_t *buf,
                  size_t bsize);
/**
 * Send a frame to a destionation over ether.
 */
int ether_send(int handle,
               const mac_addr_t dst,
               uint16_t proto,
               uint8_t *buf,
               size_t bsize);
/**
 * @}
 */

/**
 * Ethernet input and output chains.
 * @{
 */

/**
 * Handle the received ethernet frame.
 * @retval >0 the size of the reply written back to payload;
 * @retval  0 if no reply should be sent;
 * @retval -1 an error occured, errno is set.
 */
int ether_input(const struct ether_hdr *hdr, uint8_t *payload, size_t bsize);

/**
 * Send back a reply message.
 * @param hdr must be untouched header received by ether_receive().
 */
int ether_output_reply(int ether_handle,
                       const struct ether_hdr *hdr,
                       uint8_t *payload,
                       size_t bsize);

/**
 * @}
 */

#endif /* NSTACK_ETHER_H */

/**
 * @}
 */
