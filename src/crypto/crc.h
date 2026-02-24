/*
**  CRC.H - header file for SNIPPETS CRC and checksum functions
*/

#ifndef CRC__H
#define CRC__H

#ifdef __cplusplus
#include <cstdlib>
#include <cinttypes>
#else
#include <stdbool.h>
#include <stdlib.h>           /* For size_t                 */
#include <stdint.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
**  File: CRC_32.C
*/

#define UPDC32(octet,crc) (crc_32_tab[((crc)\
     ^ ((uint8_t)octet)) & 0xff] ^ ((crc) >> 8))

uint32_t updateCRC32(unsigned char ch, uint32_t crc);
bool crc32file(char *name, uint32_t *crc, long *charcnt);

uint32_t crc32buf(const char *buf, size_t len);

uint32_t crc32buf_with_oldcrc(const char *buf, size_t len, uint32_t oldcrc);

#ifdef __cplusplus
}
#endif

#endif /* CRC__H */
