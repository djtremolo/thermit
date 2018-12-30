#ifndef __MSGBUF_H__
#define __MSGBUF_H__
#include <stdint.h>


uint8_t msgLen(uint8_t *start, uint8_t *currentPosition);
void msgPutU16(uint8_t **buf, uint16_t val);
void msgPutU8(uint8_t **buf, uint8_t val);
uint16_t msgGetU16(uint8_t **buf);
uint8_t msgGetU8(uint8_t **buf);


#endif   //__MSGBUF_H__