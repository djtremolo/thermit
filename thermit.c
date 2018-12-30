#include "thermit.h"
#include "string.h" //memset
#include "crc.h"
#include "msgBuf.h"


#define THERMIT_INSTANCES_MAX 1

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
  uint16_t version;     /*version identifier of the thermit instance.*/
  uint16_t chunkSize;   /*split the user file into chunks of this size. The chunk is the maximum size that fits into low level line packet. */
  uint16_t maxFileSize; /*this is the maximum transferable unit size (i.e. the file size)*/
  uint16_t keepAliveMs; /*0: disable keepalive, 1..65k: idle time after which a keepalive packet is sent*/
  uint16_t burstLength; /*how many packets to be sent at one step. This is to be auto-tuned during transfer to optimize the hw link buffer usage. */
} thermitParameters_t;

typedef struct
{
  /*public interface:*/
  const struct thermitMethodTable_t *m;

  /*private members*/
  uint16_t reserved; /*magic value 0xA55B used for detecting reservation, all others: not initialized*/

  thermitState_t state;

  thermitIoSlot_t comLink;
  thermitIoSlot_t inFile;
  thermitIoSlot_t outFile;

  /*store parsed packet after receiving*/
  thermitPacket_t packet;

  bool isMaster;

  thermitParameters_t parameters;
  thermitDiagnostics_t diagnostics;
} thermitPrv_t;

static int deSerializeParameterStruct(uint8_t *buf, uint8_t len, thermitParameters_t *params);
static int serializeParameterStruct(uint8_t *buf, uint8_t *len, thermitParameters_t *params);
static int findBestCommonParameterSet(thermitParameters_t *p1, thermitParameters_t *p2, thermitParameters_t *result);

static int handleStateSyncWaitingForProposal_NNC(thermitPrv_t *prv);
static void initializeState_NNC(thermitPrv_t *prv);
static int handlePacket_NNC(thermitPrv_t *prv);
static int handleIncoming(thermitPrv_t *prv);


static thermitState_t mStep(thermit_t *inst);
static void mProgress(thermit_t *inst, thermitProgress_t *progress);
static int16_t mFeed(thermit_t *inst, uint8_t *rxBuf, int16_t rxLen);
static int mReset(thermit_t *inst);
static int mSetDeviceOpenCb(thermit_t *inst, cbDeviceOpen_t cb);
static int mSetDeviceCloseCb(thermit_t *inst, cbDeviceClose_t cb);
static int mSetDeviceReadCb(thermit_t *inst, cbDeviceRead_t cb);
static int mSetDeviceWriteCb(thermit_t *inst, cbDeviceWrite_t cb);
static int mSetFileOpenCb(thermit_t *inst, cbFileOpen_t cb);
static int mSetFileCloseCb(thermit_t *inst, cbFileClose_t cb);
static int mSetFileReadCb(thermit_t *inst, cbFileRead_t cb);
static int mSetFileWriteCb(thermit_t *inst, cbFileWrite_t cb);

static const struct thermitMethodTable_t mTable =
    {
        mStep,
        mProgress,
        mFeed,
        mReset,
        mSetDeviceOpenCb,
        mSetDeviceCloseCb,
        mSetDeviceReadCb,
        mSetDeviceWriteCb,
        mSetFileOpenCb,
        mSetFileCloseCb,
        mSetFileReadCb,
        mSetFileWriteCb};

#define THERMIT_RESERVED_MAGIC_VALUE 0xA55B

static thermitPrv_t thermitInstances[THERMIT_INSTANCES_MAX];

static thermitPrv_t *reserveInstance()
{
  thermitPrv_t *inst = NULL;
  int i;

  for (i = 0; i < THERMIT_INSTANCES_MAX; i++)
  {
    if (thermitInstances[i].reserved != THERMIT_RESERVED_MAGIC_VALUE)
    {
      inst = &(thermitInstances[i]);

      memset(inst, 0, sizeof(thermitPrv_t));
      thermitInstances[i].reserved = THERMIT_RESERVED_MAGIC_VALUE;

      break; /*found!*/
    }
  }
  return inst;
}

static void releaseInstance(thermitPrv_t *prv)
{
  if (prv && (prv->reserved == THERMIT_RESERVED_MAGIC_VALUE))
  {
    memset(prv, 0, sizeof(thermitPrv_t));
  }
}

thermit_t *thermitNew(uint8_t *linkName, bool isMaster)
{
  thermitPrv_t *prv = NULL;

  DEBUG_PRINT("thermitNew()\r\n");

  if (linkName)
  {
    thermitPrv_t *p = reserveInstance();

    if (p)
    {
      p->m = &mTable;
      p->comLink = ioDeviceOpen(linkName, 0);
      p->isMaster = isMaster;

      DEBUG_PRINT("created %s instance using '%s'.\r\n", (isMaster ? "master" : "slave"), linkName);

      prv = p; /*return this instance as it was successfully created*/
    }
  }
  else
  {
    DEBUG_PRINT("incorrect linkName - FAILED.\r\n");
  }

  return (thermit_t *)prv;
}

void thermitDelete(thermit_t *inst)
{
  thermitPrv_t *prv = (thermitPrv_t *)inst;

  DEBUG_PRINT("thermitDelete()\r\n");

  if (prv)
  {
    ioDeviceClose(prv->comLink);
    releaseInstance(prv);
    DEBUG_PRINT("instance deleted\r\n");
  }
  else
  {
    DEBUG_PRINT("deletion FAILED\r\n");
  }
}

#if THERMIT_DEBUG
static void debugDumpFrame(uint8_t *buf, uint8_t *prefix)
{
  int i;
  int pLen = (int)buf[5];

  if (prefix)
  {
    DEBUG_PRINT("%s ", prefix);
  }

  DEBUG_PRINT("FC:%02X RFId:%02X Feedback:%02X SFId:%02X Chunk:%02X DataLen:%02X(%d) [", buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[5]);

  for (i = 0; i < pLen; i++)
  {
    DEBUG_PRINT("%02X%s", buf[6 + i], (i == (pLen - 1) ? "" : " "));
  }

  DEBUG_PRINT("] CRC:%04X\r\n", (((uint16_t)buf[6 + pLen]) << 8) | ((uint16_t)buf[6 + pLen + 1]));
}
#else
static void debugDumpFrame(uint8_t *buf, uint8_t *prefix)
{
  (void)buf;
  (void)len;
  (void)prefix;
}
#endif

static int parsePacket(thermitPrv_t *prv)
{
  int ret = -1;

  if (prv)
  {
    thermitPacket_t *pkt = &(prv->packet);

    if ((pkt->rawLen > 0) && (pkt->rawLen <= THERMIT_MSG_SIZE_MAX))
    {
      thermitPacket_t *pkt = &(prv->packet);
      uint8_t *p = pkt->rawBuf;
      uint16_t calculatedCrc;
      uint8_t plLen = p[THERMIT_PAYLOAD_LEN_OFFSET];

      if ((plLen <= THERMIT_PAYLOAD_SIZE) && (THERMIT_EXPECTED_LENGHT(plLen) == pkt->rawLen))
      {
        uint8_t *crcPtr = &(p[THERMIT_CRC_OFFSET(plLen)]);
        uint16_t receivedCrc;
        thermitPacket_t *pkt = &(prv->packet);

        receivedCrc = msgGetU16(&crcPtr);
        calculatedCrc = crc16(p, THERMIT_CRC_OFFSET(pkt->payloadLen));

        if (receivedCrc == calculatedCrc)
        {
          pkt->fCode = p[THERMIT_FCODE_OFFSET];
          pkt->recFileId = p[THERMIT_REC_FILEID_OFFSET];
          pkt->recFeedback = p[THERMIT_REC_FEEDBACK_OFFSET];
          pkt->sndFileId = p[THERMIT_SND_FILEID_OFFSET];
          pkt->sndChunkNo = p[THERMIT_SND_CHUNKNO_OFFSET];
          pkt->payloadLen = plLen;
          pkt->payloadPtr = ((plLen > 0) ? &(p[THERMIT_PAYLOAD_OFFSET]) : NULL);

          ret = 0;
        }
        else
        {
          prv->diagnostics.crcErrors++;
        }
      }
    }
  }

  return ret;
}

static int deSerializeParameterStruct(uint8_t *buf, uint8_t len, thermitParameters_t *params)
{
  int ret = -1;

  if (buf && params)
  {
    uint8_t expectedLen = sizeof(uint16_t) * 5;
    if (len == expectedLen)
    {
      params->version = msgGetU16(&buf);
      params->chunkSize = msgGetU16(&buf);
      params->maxFileSize = msgGetU16(&buf);
      params->keepAliveMs = msgGetU16(&buf);
      params->burstLength = msgGetU16(&buf);

      ret = 0;
    }
  }
  return ret;
}

static int serializeParameterStruct(uint8_t *buf, uint8_t *len, thermitParameters_t *params)
{
  int ret = -1;
  uint8_t expectedLen = sizeof(uint16_t) * 5;
  uint8_t *bufStart = buf;

  if (buf && len && params && (*len >= expectedLen))
  {
    msgPutU16(&buf, params->version);
    msgPutU16(&buf, params->chunkSize);
    msgPutU16(&buf, params->maxFileSize);
    msgPutU16(&buf, params->keepAliveMs);
    msgPutU16(&buf, params->burstLength);

    *len = msgLen(bufStart, buf);

    ret = 0;
  }

  return ret;
}

#define GET_MAX(_a, _b) ((_a) > (_b) ? (_a) : (_b))
#define GET_MIN(_a, _b) ((_a) < (_b) ? (_a) : (_b))

static int findBestCommonParameterSet(thermitParameters_t *p1, thermitParameters_t *p2, thermitParameters_t *result)
{
  int ret = -1; // -1 = incompatible parameters -> fatal error

  if(p1 && p2 && result)
  {
    /*simple comparision to find the best compatible settings*/
    result->version = GET_MIN(p1->version, p2->version);
    result->chunkSize = GET_MIN(p1->chunkSize, p2->chunkSize);
    result->maxFileSize = GET_MIN(p1->maxFileSize, p2->maxFileSize);
    result->keepAliveMs = GET_MIN(p1->keepAliveMs, p2->keepAliveMs);
    result->burstLength = GET_MIN(p1->burstLength, p2->burstLength);

    /*check that the max file size still makes sense*/
    result->maxFileSize = GET_MIN(result->maxFileSize, result->chunkSize * THERMIT_CHUNK_COUNT_MAX);

    /*there is no point sending longer bursts than the max file size allows*/
    result->burstLength = GET_MIN(p1->burstLength, result->maxFileSize / result->chunkSize);

    ret = 0;
  }

  return ret;
}


static int handleStateSyncWaitingForProposal_NNC(thermitPrv_t *prv)
{
  int ret = -1;
  thermitPacket_t *pkt = &(prv->packet);
  thermitParameters_t params;

  switch (pkt->fCode)
  {
  case THERMIT_FCODE_SYNC_PROPOSAL:
    if(deSerializeParameterStruct(pkt->payloadPtr, pkt->payloadLen, &params) == 0)
    {
      if(findBestCommonParameterSet(&params, &(prv->parameters), &(prv->parameters)) == 0)
      {
        /*now we have found the best parameter set. It will be sent to master at tx stage.*/
        ret = 0;
      }      
    }
    break;

  default:
    /*all other function codes are considered illegal. Jump to beginning.*/
    initializeState_NNC(prv);
    break;
  }
}

static void initializeState_NNC(thermitPrv_t *prv)
{
  prv->state = prv->isMaster ? THERMIT_SYNC_SENDING_PROPOSAL : THERMIT_SYNC_WAITING_FOR_PROPOSAL;
}

static int handlePacket_NNC(thermitPrv_t *prv)
{
  int ret = -1;
  thermitPacket_t *pkt = &(prv->packet);

  switch (prv->state)
  {
  case THERMIT_RUNNING:
    DEBUG_PRINT("handlePacket: THERMIT_RUNNING\r\n");
    //ret = handleStateRunning(prv);
    break;

  case THERMIT_SYNC_WAITING_FOR_PROPOSAL:
    DEBUG_PRINT("handlePacket: THERMIT_SYNC_WAITING_FOR_PROPOSAL\r\n");
    ret = handleStateSyncWaitingForProposal_NNC(prv);
    break;

  case THERMIT_SYNC_WAITING_FOR_RESPONSE:
    DEBUG_PRINT("handlePacket: THERMIT_SYNC_WAITING_FOR_RESPONSE\r\n");
    //ret = handleStateSyncWaitingForResponse(prv);
    break;

  case THERMIT_SYNC_WAITING_FOR_ACK:
    DEBUG_PRINT("handlePacket: THERMIT_SYNC_WAITING_FOR_ACK\r\n");
    //ret = handleStateSyncWaitingForAck(prv);
    break;

  case THERMIT_WAITING_FOR_CALLBACK_CONFIGURATION:
    DEBUG_PRINT("handlePacket: THERMIT_WAITING_FOR_CALLBACK_CONFIGURATION\r\n");
    /*no action*/
    ret = 0;
    break;

  case THERMIT_SYNC_SENDING_PROPOSAL:
    DEBUG_PRINT("handlePacket: THERMIT_SYNC_SENDING_PROPOSAL\r\n");
    /*no action*/
    ret = 0;
    break;


  default:
    DEBUG_PRINT("handlePacket: Unknown state\r\n");
    /*unknown state -> require sync*/
    initializeState_NNC(prv);
    ret = 1;
    break;
  }

  return ret;
}

static int handleIncoming(thermitPrv_t *prv)
{
  int ret = -1;

  if (prv)
  {
    thermitPacket_t *pkt = &(prv->packet);

    ret = 1; /*return positive non-zero if parameters are valid but there's nothing to do*/

    /*check communication device for incoming messages*/
    pkt->rawLen = ioDeviceRead(prv->comLink, pkt->rawBuf, THERMIT_MSG_SIZE_MAX);

    if (parsePacket(prv) == 0)
    {
      /*message was received*/
      debugDumpFrame(pkt->rawBuf, "Processing frame\r\n->");

      ret = handlePacket_NNC(prv);
    }

#if 0
      /*prepare outgoing message*/
      prv->mLen = 0;

      prv->mBuf[prv->mLen++] = prv->isMaster?0xEE:0xDD; //for recognization
      prv->mBuf[prv->mLen++] = 0x02;
      prv->mBuf[prv->mLen++] = 0x03;
      prv->mBuf[prv->mLen++] = 0x04;
      prv->mBuf[prv->mLen++] = 0x05;
      prv->mBuf[prv->mLen++] = 0x02;
      prv->mBuf[prv->mLen++] = 0xAA;
      prv->mBuf[prv->mLen++] = 0xBB;
      prv->mBuf[prv->mLen++] = 0xCD;
      prv->mBuf[prv->mLen++] = 0xEF;

      debugDumpFrame(prv->mBuf, "Sending frame\r\n<-");
#endif
  }

  return ret;
}

static thermitState_t mStep(thermit_t *inst)
{
  thermitPrv_t *prv = (thermitPrv_t *)inst;
  thermitState_t ret;
  int16_t processReturn;

  if (prv)
  {
    DEBUG_PRINT("thermit->step()\r\n");

    /*call processing function. It will overwrite pkt->rawBuf & pkt->rawLen with a response message if
    an outgoing message is needed.*/
    processReturn = handleIncoming(prv);

    /*if a response was prepared, send outgoing message*/
    if (processReturn > 0)
    {
      thermitPacket_t *pkt = &(prv->packet);

      ioDeviceWrite(prv->comLink, pkt->rawBuf, pkt->rawLen);
    }

    /*if nothing to send, send keepalive periodically*/
    //TBD
  }

  return ret;
}

static void mProgress(thermit_t *inst, thermitProgress_t *progress)
{
  thermitPrv_t *prv = (thermitPrv_t *)inst;

  if (prv)
  {
    DEBUG_PRINT("thermit->progress()\r\n");
  }
}

static int16_t mFeed(thermit_t *inst, uint8_t *rxBuf, int16_t rxLen)
{
  thermitPrv_t *prv = (thermitPrv_t *)inst;
  int16_t ret = -1;

  if (prv)
  {
    DEBUG_PRINT("thermit->feed()\r\n");
  }

  return ret;
}

static int mReset(thermit_t *inst)
{
  thermitPrv_t *prv = (thermitPrv_t *)inst;
  int ret = -1;

  if (prv)
  {
    DEBUG_PRINT("thermit->reset()\r\n");
  }

  return ret;
}

static int mSetDeviceOpenCb(thermit_t *inst, cbDeviceOpen_t cb)
{
  thermitPrv_t *prv = (thermitPrv_t *)inst;
  int ret = -1;

  if (prv)
  {
    DEBUG_PRINT("thermit->setDeviceOpenCb()\r\n");
  }

  return ret;
}

static int mSetDeviceCloseCb(thermit_t *inst, cbDeviceClose_t cb)
{
  thermitPrv_t *prv = (thermitPrv_t *)inst;
  int ret = -1;

  if (prv)
  {
    DEBUG_PRINT("thermit->setDeviceCloseCb()\r\n");
  }

  return ret;
}

static int mSetDeviceReadCb(thermit_t *inst, cbDeviceRead_t cb)
{
  thermitPrv_t *prv = (thermitPrv_t *)inst;
  int ret = -1;

  if (prv)
  {
    DEBUG_PRINT("thermit->setDeviceReadCb()\r\n");
  }

  return ret;
}

static int mSetDeviceWriteCb(thermit_t *inst, cbDeviceWrite_t cb)
{
  thermitPrv_t *prv = (thermitPrv_t *)inst;
  int ret = -1;

  if (prv)
  {
    DEBUG_PRINT("thermit->setDeviceWriteCb()\r\n");
  }

  return ret;
}

static int mSetFileOpenCb(thermit_t *inst, cbFileOpen_t cb)
{
  thermitPrv_t *prv = (thermitPrv_t *)inst;
  int ret = -1;

  if (prv)
  {
    DEBUG_PRINT("thermit->setFileOpenCb()\r\n");
  }

  return ret;
}

static int mSetFileCloseCb(thermit_t *inst, cbFileClose_t cb)
{
  thermitPrv_t *prv = (thermitPrv_t *)inst;
  int ret = -1;

  if (prv)
  {
    DEBUG_PRINT("thermit->setFileCloseCb()\r\n");
  }

  return ret;
}

static int mSetFileReadCb(thermit_t *inst, cbFileRead_t cb)
{
  thermitPrv_t *prv = (thermitPrv_t *)inst;
  int ret = -1;

  if (prv)
  {
    DEBUG_PRINT("thermit->setFileReadCb()\r\n");
  }

  return ret;
}

static int mSetFileWriteCb(thermit_t *inst, cbFileWrite_t cb)
{
  thermitPrv_t *prv = (thermitPrv_t *)inst;
  int ret = -1;

  if (prv)
  {
    DEBUG_PRINT("thermit->setFileWriteCb()\r\n");
  }

  return ret;
}
