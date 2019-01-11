#ifndef __THERMIT_H__
#define __THERMIT_H__
#include <stdint.h>
#include <stdbool.h>
#include "thermitDebug.h"
#include "msgBuf.h"


#define DIVISION_ROUNDED_UP(value, divider) ((value) % (divider) == 0 ? (value) / (divider) : ((value) / (divider)) +1)


#define THERMIT_VERSION                   0

#define THERMIT_FILENAME_MAX              32

#define THERMIT_MASTER_MODE_SUPPORT       true
#define THERMIT_SLAVE_MODE_SUPPORT        true

#define L2_MTU   (16+60)         //128
#define L2_HEADER_SIZE 8
#define L2_FOOTER_SIZE 0
#define L2_PAYLOAD_SIZE (L2_MTU - L2_HEADER_SIZE - L2_FOOTER_SIZE)

#define THERMIT_HEADER_LENGTH 6
#define THERMIT_FOOTER_LENGTH 2
#define THERMIT_PAYLOAD_SIZE (L2_PAYLOAD_SIZE - THERMIT_HEADER_LENGTH - THERMIT_FOOTER_LENGTH)
#define THERMIT_MSG_SIZE_MAX L2_PAYLOAD_SIZE


#define THERMIT_MAX_REQUIRED_FILE_SIZE      512

#define THERMIT_CHUNK_COUNT_MAX     250//(DIVISION_ROUNDED_UP(THERMIT_MAX_REQUIRED_FILE_SIZE, THERMIT_PAYLOAD_SIZE))   //when adjusting this, please take a look at the THERMIT_FEEDBACK definitions

#define THERMIT_FCODE_OFFSET 0
#define THERMIT_REC_FILEID_OFFSET 1
#define THERMIT_REC_FEEDBACK_OFFSET 2
#define THERMIT_SND_FILEID_OFFSET 3
#define THERMIT_SND_CHUNKNO_OFFSET 4
#define THERMIT_PAYLOAD_LEN_OFFSET 5
#define THERMIT_PAYLOAD_OFFSET 6
#define THERMIT_CRC_OFFSET(_PLLEN) ((_PLLEN) + THERMIT_PAYLOAD_OFFSET)
#define THERMIT_EXPECTED_LENGHT(_PLLEN) ((_PLLEN) + THERMIT_PAYLOAD_OFFSET + 2)

#define THERMIT_FEEDBACK_FILE_IS_READY 0xFF

#define THERMIT_FILEID_MAX         250
#define THERMIT_FILEID_MAX         250
#define THERMIT_FILEID_INACTIVE    0xFF


typedef enum
{
  THERMIT_FCODE_SYNC_PROPOSAL = 1, //this is the first frame sent by master. It includes the best set of parameters that the master can handle
  THERMIT_FCODE_SYNC_RESPONSE = 2, //this is the response from the slave. It includes the best compromise of parameter set that is supported by both ends.
  THERMIT_FCODE_SYNC_ACK = 3,      //master acknowledges the parameter set
  THERMIT_FCODE_DATA_TRANSFER = 4, //data transfer frame. If file is to be sent, this frame contains one chunk. The frame can also be sent as feedback frame with empty data.
  THERMIT_FCODE_NEW_FILE_START = 5,//contains file info about next file to be sent
  THERMIT_FCODE_WRITE_TERMINATED_FORCEFULLY = 0xFE, //sent if wrong file/illegal chunk is received
  THERMIT_FCODE_OUT_OF_SYNC = 0xFF //error frame. Can be sent if the incoming frame is not supported in active protocol state.
} thermitFCode_t;



typedef int thermitIoSlot_t;
typedef enum 
{
  THERMIT_READ,
  THERMIT_WRITE
} thermitIoMode_t;


typedef struct
{
  /*raw data: This buffer will be used for both incoming and outgoing messages*/
  uint8_t rawBuf[THERMIT_MSG_SIZE_MAX];
  int16_t rawLen;

  /*parsed data:*/
  thermitFCode_t fCode;
  uint8_t recFileId;
  uint8_t recFeedback;
  uint8_t sndFileId;
  uint8_t sndChunkNo;
  uint8_t payloadLen;
  uint8_t *payloadPtr;
} thermitPacket_t;

typedef enum
{
  THERMIT_FIRST_DUMMY_STATE,                    //NEVER use this. It is just for range check and keeping other states in non-zero values.
  /*real states:*/
  THERMIT_WAITING_FOR_CALLBACK_CONFIGURATION,   //cannot run before the callback functions are set
  THERMIT_SYNC_FIRST,                           //master:send proposal and receive response, slave:wait for proposal and send response
  THERMIT_SYNC_SECOND,                          //master:send ack and switch to running, slave:wait for ack and switch to running
  THERMIT_RUNNING,                              //running - file sending/receiving is currently active
  THERMIT_OUT_OF_SYNC,                          //faulty state - inform other end
  /*real states end*/
  THERMIT_LAST_DUMMY_STATE,                    //NEVER use this. It is just for range check.
} thermitState_t;

typedef struct
{
  const struct thermitMethodTable_t *m;
} thermit_t;


typedef thermitIoSlot_t (*cbDeviceOpen_t)(uint8_t *devName, thermitIoMode_t mode);
typedef int (*cbDeviceClose_t)(thermitIoSlot_t slot);
typedef int (*cbDeviceRead_t)(thermitIoSlot_t slot, uint8_t *buf, int16_t maxLen);
typedef int (*cbDeviceWrite_t)(thermitIoSlot_t slot, uint8_t *buf, int16_t len);
typedef thermitIoSlot_t (*cbFileOpen_t)(uint8_t *fileName, thermitIoMode_t mode, uint16_t *fileSize);
typedef int (*cbFileClose_t)(thermitIoSlot_t slot);
typedef int (*cbFileRead_t)(thermitIoSlot_t slot, uint16_t offset, uint8_t *buf, int16_t maxLen);
typedef int (*cbFileWrite_t)(thermitIoSlot_t slot, uint16_t offset, uint8_t *buf, int16_t len);
typedef bool (*cbFileAvailableForSending_t)(uint8_t *fileNamePtr, uint16_t *sizePtr);

typedef uint32_t (*cbSystemGetMilliseconds_t)(uint32_t *maxMs);
typedef int (*cbSystemDebugPrintf_t)(const char *restrict format, ...);
typedef uint16_t (*cbSystemCrc16_t)(const uint8_t *data, uint16_t size);

typedef struct
{
  cbDeviceOpen_t devOpen;
  cbDeviceClose_t devClose;
  cbDeviceRead_t devRead;
  cbDeviceWrite_t devWrite;
  cbFileOpen_t fileOpen;
  cbFileClose_t fileClose;
  cbFileRead_t fileRead;
  cbFileWrite_t fileWrite;
  cbFileAvailableForSending_t fileAvailableForSending;
  cbSystemGetMilliseconds_t sysGetMs;
  cbSystemDebugPrintf_t sysPrintf;
  cbSystemCrc16_t sysCrc16;
} thermitTargetAdaptationInterface_t;


struct thermitMethodTable_t
{
  thermitState_t (*step)(thermit_t *inst);
  int (*reset)(thermit_t *inst);
} thermitMethodTable_t;

thermit_t *thermitNew(uint8_t *linkName, bool isMaster, thermitTargetAdaptationInterface_t *targetIf);
void thermitDelete(thermit_t *inst);

#endif //__THERMIT_H__