#include "thermit.h"
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
} thermitPrv_t;



