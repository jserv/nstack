#include <stddef.h>
#include <stdint.h>

uint32_t ether_fcs(const void *data, size_t bsize)
{
    const uint8_t *dp = (uint8_t *) data;
    const uint32_t crc_table[] = {
        0x4DBDF21C, 0x500AE278, 0x76D3D2D4, 0x6B64C2B0, 0x3B61B38C, 0x26D6A3E8,
        0x000F9344, 0x1DB88320, 0xA005713C, 0xBDB26158, 0x9B6B51F4, 0x86DC4190,
        0xD6D930AC, 0xCB6E20C8, 0xEDB71064, 0xF0000000};
    uint32_t crc = 0;

    for (size_t i = 0; i < bsize; i++) {
        crc = (crc >> 4) ^ crc_table[(crc ^ (dp[i] >> 0)) & 0x0F];
        crc = (crc >> 4) ^ crc_table[(crc ^ (dp[i] >> 4)) & 0x0F];
    }

    return crc;
}
