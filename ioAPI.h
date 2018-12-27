#ifndef __IOAPI_H__
#define __IOAPI_H__
#include "thermit.h"

int ioDeviceOpen(uint8_t *devName);     /*returns device handle*/
int ioDeviceClose(int devHandle);       
int ioDeviceRead(thermit_t *inst, uint8_t *buf, int16_t maxLen); 
int ioDeviceWrite(thermit_t *inst, uint8_t *buf, int16_t len); 

int16_t ioFileOpen(thermit_t *inst, uint8_t *fileName, int mode); 
int16_t ioFileClose(thermit_t *inst, bool in);
int16_t ioFileRead(thermit_t *inst, uint16_t offset, uint8_t *buf, int16_t len); 
int16_t ioFileWrite(thermit_t *inst, uint16_t offset, uint8_t *buf, int16_t len); 


#endif //__IOAPI_H__