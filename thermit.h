#ifndef __THERMIT_H__
#define __THERMIT_H__
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>


#define THERMIT_FILENAME_MAX    32

typedef struct
{
    uint32_t receivedFiles;
    uint32_t receivedBytes;
    uint32_t sentFiles;
    uint32_t sentBytes;
    uint32_t crcErrors;
    uint32_t retransmits;
    uint32_t reconnections;
} thermitDiagnostics_t;

typedef struct
{
    uint16_t version;       /*version identifier of the thermit instance.*/
    uint16_t chunkSize;     /*split the user file into chunks of this size. The chunk is the maximum size that fits into low level line packet. */
    uint16_t maxFileSize;   /*this is the maximum transferable unit size (i.e. the file size)*/
    uint16_t keepAliveMs;   /*0: disable keepalive, 1..65k: idle time after which a keepalive packet is sent*/
    uint16_t burstLength;   /*how many packets to be sent at one step. This is to be auto-tuned during transfer to optimize the hw link buffer usage. */
} thermitParameters_t;

typedef struct
{
    uint16_t initialized;   /*magic value 0xA55B used for detecting initialization, all others: not initialized*/
    int fd;
    FILE *inFile;
    FILE *outFile;

    thermitParameters_t parameters;
    thermitDiagnostics_t diagnostics;
} thermit_t;

typedef struct
{			
    uint8_t name[THERMIT_FILENAME_MAX]; 
    int16_t totalSize;
    int16_t transferredBytes;
} thermitProgress_t;      //IS this needed?

typedef enum
{
    THERMIT_DISCONNECTED = 1,           /*synchronizing - starting up*/
    THERMIT_IDLE = 2,                   /*ok - nothing to do*/
    THERMIT_TRANSFER_ONGOING = 3        /*running - file sending/receiving is currently active*/
} thermitState_t;

int thermitInitialize(thermit_t *inst, uint8_t *portName);
thermitState_t thermitStep(thermit_t *inst);
int thermitProgress(thermit_t *inst, thermitProgress_t *progress);


#endif //__THERMIT_H__