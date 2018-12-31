#ifndef __THERMIT_H__
#define __THERMIT_H__
#include <stdint.h>
#include <stdbool.h>
#include "ioAPI.h"
#include "thermitDebug.h"




#define THERMIT_VERSION                   0

#define THERMIT_FILENAME_MAX              32

#define THERMIT_MASTER_MODE_SUPPORT       true
#define THERMIT_SLAVE_MODE_SUPPORT        true


#define L2_MTU 128
#define L2_HEADER_SIZE 8
#define L2_FOOTER_SIZE 0
#define L2_PAYLOAD_SIZE (L2_MTU - L2_HEADER_SIZE - L2_FOOTER_SIZE)

#define THERMIT_HEADER_LENGTH 6
#define THERMIT_FOOTER_LENGTH 2
#define THERMIT_PAYLOAD_SIZE (L2_PAYLOAD_SIZE - THERMIT_HEADER_LENGTH - THERMIT_FOOTER_LENGTH)
#define THERMIT_MSG_SIZE_MAX L2_PAYLOAD_SIZE

#define THERMIT_CHUNK_COUNT_MAX     250   //when adjusting this, please take a look at the THERMIT_FEEDBACK definitions

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
#define THERMIT_FEEDBACK_FILE_SO_FAR_OK 0xFE

typedef enum
{
  THERMIT_FCODE_SYNC_PROPOSAL = 1, //this is the first frame sent by master. It includes the best set of parameters that the master can handle
  THERMIT_FCODE_SYNC_RESPONSE = 2, //this is the response from the slave. It includes the best compromise of parameter set that is supported by both ends.
  THERMIT_FCODE_SYNC_ACK = 3,      //master acknowledges the parameter set
  THERMIT_FCODE_DATA_TRANSFER = 4, //data transfer frame. If file is to be sent, this frame contains one chunk. The frame can also be sent as feedback frame with empty data.
  THERMIT_FCODE_OUT_OF_SYNC = 0xFF //error frame. Can be sent if the incoming frame is not supported in active protocol state.
} thermitFCode_t;

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

typedef struct
{
  uint8_t name[THERMIT_FILENAME_MAX];
  int16_t totalSize;
  int16_t transferredBytes;
} thermitProgress_t; //IS this needed?

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

typedef int16_t (*cbDeviceOpen_t)(thermit_t *inst, uint8_t *portName);
typedef int16_t (*cbDeviceClose_t)(thermit_t *inst);
typedef int16_t (*cbDeviceRead_t)(thermit_t *inst, uint8_t *buf, int16_t maxLen);
typedef int16_t (*cbDeviceWrite_t)(thermit_t *inst, uint8_t *buf, int16_t len);
typedef int16_t (*cbFileOpen_t)(thermit_t *inst, uint8_t *fileName, int mode);
typedef int16_t (*cbFileClose_t)(thermit_t *inst, bool in);
typedef int16_t (*cbFileRead_t)(thermit_t *inst, uint16_t offset, uint8_t *buf, int16_t len);
typedef int16_t (*cbFileWrite_t)(thermit_t *inst, uint16_t offset, uint8_t *buf, int16_t len);

struct thermitMethodTable_t
{
  thermitState_t (*step)(thermit_t *inst);
  void (*progress)(thermit_t *inst, thermitProgress_t *progress);
  int16_t (*feed)(thermit_t *inst, uint8_t *rxBuf, int16_t rxLen);
  int (*reset)(thermit_t *inst);

  int (*setDeviceOpenCb)(thermit_t *inst, cbDeviceOpen_t cb);
  int (*setDeviceCloseCb)(thermit_t *inst, cbDeviceClose_t cb);
  int (*setDeviceReadCb)(thermit_t *inst, cbDeviceRead_t cb);
  int (*setDeviceWriteCb)(thermit_t *inst, cbDeviceWrite_t cb);
  int (*setFileOpenCb)(thermit_t *inst, cbFileOpen_t cb);
  int (*setFileCloseCb)(thermit_t *inst, cbFileClose_t cb);
  int (*setFileReadCb)(thermit_t *inst, cbFileRead_t cb);
  int (*setFileWriteCb)(thermit_t *inst, cbFileWrite_t cb);
} thermitMethodTable_t;

thermit_t *thermitNew(uint8_t *linkName, bool isMaster);
void thermitDelete(thermit_t *inst);

#endif //__THERMIT_H__