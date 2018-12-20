#ifndef __IOAPI_H__
#define __IOAPI_H__
#include "thermit.h"

int16_t ioDeviceOpen(thermit_t *inst, uint8_t portIdx);
int16_t ioDeviceClose(thermit_t *inst);
int16_t ioDeviceRead(thermit_t *inst, uint8_t *buf, int16_t maxLen); 
int16_t ioDeviceWrite(thermit_t *inst, uint8_t *buf, int16_t len); 

int16_t ioFileOpen(thermit_t *inst, uint8_t *fileName, int mode); 
int16_t ioFileClose(thermit_t *inst, bool in);
int16_t ioFileRead(thermit_t *inst, uint16_t offset, uint8_t *buf, int16_t len); 
int16_t ioFileWrite(thermit_t *inst, uint16_t offset, uint8_t *buf, int16_t len); 


#endif //__IOAPI_H__