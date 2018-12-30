#ifndef __IOAPI_H__
#define __IOAPI_H__
#include "msgBuf.h"

typedef int thermitIoSlot_t;
typedef int thermitIoMode_t;

thermitIoSlot_t ioDeviceOpen(uint8_t *devName, thermitIoMode_t mode);
int ioDeviceClose(thermitIoSlot_t slot);
int ioDeviceRead(thermitIoSlot_t slot, uint8_t *buf, int16_t maxLen);
int ioDeviceWrite(thermitIoSlot_t slot, uint8_t *buf, int16_t len);

thermitIoSlot_t ioFileOpen(uint8_t *fileName, thermitIoMode_t mode);
int ioFileClose(thermitIoSlot_t slot);
int ioFileRead(thermitIoSlot_t slot, uint16_t offset, uint8_t *buf, int16_t maxLen);
int ioFileWrite(thermitIoSlot_t slot, uint16_t offset, uint8_t *buf, int16_t len);

#endif //__IOAPI_H__