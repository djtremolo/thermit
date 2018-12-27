#ifndef __THERMIT_H__
#define __THERMIT_H__
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>


#define THERMIT_FILENAME_MAX    32

typedef struct
{			
    uint8_t name[THERMIT_FILENAME_MAX]; 
    int16_t totalSize;
    int16_t transferredBytes;
} thermitProgress_t;      //IS this needed?

typedef enum
{
    THERMIT_NOT_CONFIGURED = 1,         /*not configured - callbacks not set*/
    THERMIT_DISCONNECTED = 2,           /*synchronizing - starting up*/
    THERMIT_IDLE = 3,                   /*ok - nothing to do*/
    THERMIT_TRANSFER_ONGOING = 4        /*running - file sending/receiving is currently active*/
} thermitState_t;

typedef struct
{
    struct thermitMethodTable_t *m;
} thermit_t;


typedef int16_t(*cbDeviceOpen_t)(thermit_t *inst, uint8_t *portName);
typedef int16_t(*cbDeviceClose_t)(thermit_t *inst);
typedef int16_t(*cbDeviceRead_t)(thermit_t *inst, uint8_t *buf, int16_t maxLen); 
typedef int16_t(*cbDeviceWrite_t)(thermit_t *inst, uint8_t *buf, int16_t len); 
typedef int16_t(*cbFileOpen_t)(thermit_t *inst, uint8_t *fileName, int mode); 
typedef int16_t(*cbFileClose_t)(thermit_t *inst, bool in);
typedef int16_t(*cbFileRead_t)(thermit_t *inst, uint16_t offset, uint8_t *buf, int16_t len); 
typedef int16_t(*cbFileWrite_t)(thermit_t *inst, uint16_t offset, uint8_t *buf, int16_t len); 


struct thermitMethodTable_t
{
    thermitState_t (*step)(thermit_t *inst);
    void (*progress)(thermit_t *inst, thermitProgress_t *progress);
    int16_t (*feed)(thermit_t *inst, uint8_t *rxBuf, int16_t rxLen);
    int16_t (*reset)(thermit_t *inst);

    int16_t (*setDeviceOpenCb)(thermit_t *inst, cbDeviceOpen_t cb);
    int16_t (*setDeviceCloseCb)(thermit_t *inst, cbDeviceClose_t cb);
    int16_t (*setDeviceReadCb)(thermit_t *inst, cbDeviceRead_t cb);
    int16_t (*setDeviceWriteCb)(thermit_t *inst, cbDeviceWrite_t cb);
    int16_t (*setFileOpenCb)(thermit_t *inst, cbFileOpen_t cb);
    int16_t (*setFileCloseCb)(thermit_t *inst, cbFileClose_t cb);
    int16_t (*setFileReadCb)(thermit_t *inst, cbFileRead_t cb);
    int16_t (*setFileWriteCb)(thermit_t *inst, cbFileWrite_t cb);
} thermitMethodTable_t;


thermit_t* thermitNew(uint8_t *linkName, uint8_t nameLen);
void thermitDelete(thermit_t *inst);


#endif //__THERMIT_H__