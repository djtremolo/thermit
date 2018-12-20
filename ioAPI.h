#ifndef __IOAPI_H__
#define __IOAPI_H__
#include "thermit.h"

int16_t devopen(thermit_t *inst, uint8_t portIdx);
int16_t readpkt(thermit_t *inst, uint8_t *buf, int16_t maxLen); 
int16_t tx_data(thermit_t *inst, uint8_t *buf, int16_t len); 
int16_t openfile(thermit_t *inst, uint8_t *fileName, int mode); 
int16_t readfile(thermit_t *inst, uint16_t offset, uint8_t *buf, int16_t len); 
int16_t writefile(thermit_t *inst, uint16_t offset, uint8_t *buf, int16_t len); 
int16_t closefile(thermit_t *inst, bool in);


#endif //__IOAPI_H__