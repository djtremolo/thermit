#include "thermit.h"
#include "string.h" //memset
#include "crc.h"
#include "msgBuf.h"


#define THERMIT_INSTANCES_MAX 1


#define DIVISION_ROUNDED_UP(value, divider) ((value) % (divider) == 0 ? (value) / (divider) : ((value) / (divider)) +1)

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


#define THERMIT_PROGRESS_STATUS_LENGTH                DIVISION_ROUNDED_UP(THERMIT_CHUNK_COUNT_MAX, 8)   
#define THERMIT_PROGRESS_STATUS_BYTE_INDEX(chunkNo)   ((chunkNo) / 8)
#define THERMIT_PROGRESS_STATUS_BIT_INDEX(chunkNo)    ((chunkNo) % 8)
typedef struct
{
  uint8_t chunkStatus[THERMIT_PROGRESS_STATUS_LENGTH];     /*each bit represents one chunk: 1=dirty 0=done*/
  uint8_t firstDirtyChunk;
  uint8_t progressPercent;
  uint16_t progressBytesDone;
  uint16_t oneChunkPercentScaled100;   // this value represents how many percents one chunk is of the whole file, multiplied by 100
  uint8_t numberOfChunksNeeded;
} thermitProgress_t;


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

  bool proposalReceived;
  bool ackReceived;

  bool isMaster;

  thermitProgress_t progress;
  thermitParameters_t parameters;
  thermitDiagnostics_t diagnostics;
} thermitPrv_t;

static int deSerializeParameterStruct(uint8_t *buf, uint8_t len, thermitParameters_t *params);
static int serializeParameterStruct(uint8_t *buf, uint8_t *len, thermitParameters_t *params);
static int findBestCommonParameterSet(thermitParameters_t *p1, thermitParameters_t *p2, thermitParameters_t *result);

static void debugDumpParameters(thermitParameters_t *par, uint8_t *prefix, uint8_t *postfix);
static void debugDumpFrame(uint8_t *buf, uint8_t *prefix);
static void debugDumpState(thermitState_t state, uint8_t *prefix, uint8_t *postfix);
static void debugDumpProgress(thermitProgress_t *prog, uint8_t *prefix, uint8_t *postfix);

static int progressInitialize(thermitPrv_t *prv, uint16_t fileSize);
static int progressSetChunkStatus(thermitPrv_t *prv, uint8_t chunkNo, bool done);
static bool progressGetChunkIsDone(thermitPrv_t *prv, uint8_t chunkNo);
static bool progressGetFirstDirty(thermitPrv_t *prv, uint8_t *dirtyChunk);


static void initializeState(thermitPrv_t *prv);
static int parsePacketContent(thermitPrv_t *prv);
static int handleIncoming(thermitPrv_t *prv);

static uint8_t* framePrepare(thermitPacket_t *pkt);
static int frameFinalize(thermitPacket_t *pkt, uint8_t len);
static int handleOutgoing(thermitPrv_t *prv);


static int sendSyncProposal(thermitPrv_t *prv);
static int sendSyncResponse(thermitPrv_t *prv);
static int sendSyncAck(thermitPrv_t *prv);
static int sendDataMessage(thermitPrv_t *prv);

static int waitForSyncProposal(thermitPrv_t *prv);
static int waitForSyncAck(thermitPrv_t *prv);
static int waitForSyncResponse(thermitPrv_t *prv);
static int waitForDataMessage(thermitPrv_t *prv);




static thermitState_t mStep(thermit_t *inst);
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

static void initializeParameters(thermitPrv_t *prv)
{
  if(prv)
  {
    thermitParameters_t *params = &(prv->parameters);

    params->version = THERMIT_VERSION;
    params->chunkSize = THERMIT_PAYLOAD_SIZE;
    params->maxFileSize = params->chunkSize * THERMIT_CHUNK_COUNT_MAX;
    params->burstLength = 4;
    params->keepAliveMs = 1000;
  }
}


thermit_t *thermitNew(uint8_t *linkName, bool isMaster)
{
  thermitPrv_t *prv = NULL;

  DEBUG_INFO("thermitNew()\r\n");

  //don't allow creating modes that are not supported
#if !THERMIT_MASTER_MODE_SUPPORT
  if(isMaster)
  {
    DEBUG_FATAL("Fatal: Master mode not supported!\r\n");
    return NULL;
  }
#endif
#if !THERMIT_SLAVE_MODE_SUPPORT
  if(!isMaster)
  {
    DEBUG_FATAL("Fatal: Slave mode not supported!\r\n");
    return NULL;
  }
#endif


  if (linkName)
  {
    thermitPrv_t *p = reserveInstance();

    if (p)
    {
      p->comLink = ioDeviceOpen(linkName, 0);

      if(p->comLink >= 0)
      {
        p->m = &mTable;
        p->isMaster = isMaster;

        initializeParameters(p);

        debugDumpParameters(&(p->parameters), "Initial parameters: ", "\r\n");

        initializeState(p);

        DEBUG_INFO("created %s instance using '%s'.\r\n", (isMaster ? "master" : "slave"), linkName);

        prv = p; /*return this instance as it was successfully created*/
      }
      else
      {
        DEBUG_ERR("Error: Could not open communication device '%s'.\r\n", linkName);
        releaseInstance(p);
      }
    }
  }
  else
  {
    DEBUG_ERR("incorrect linkName - FAILED.\r\n");
  }

  return (thermit_t *)prv;
}

void thermitDelete(thermit_t *inst)
{
  thermitPrv_t *prv = (thermitPrv_t *)inst;

  DEBUG_INFO("thermitDelete()\r\n");

  if (prv)
  {
    ioDeviceClose(prv->comLink);
    releaseInstance(prv);
    DEBUG_INFO("instance deleted\r\n");
  }
  else
  {
    DEBUG_ERR("deletion FAILED\r\n");
  }
}


static int progressInitialize(thermitPrv_t *prv, uint16_t fileSize)
{
  int ret = -1;

  if(prv && (fileSize <= prv->parameters.maxFileSize) && (fileSize > 0))
  {
    thermitProgress_t *p = &(prv->progress);
    uint8_t fullBytes;
    uint8_t extraBits;
    uint8_t byteIdx, bitIdx;
    uint8_t extraByteValue = 0;

    memset(p, 0, sizeof(thermitProgress_t));

    /*remember number of chunks needed for this file*/
    p->numberOfChunksNeeded = DIVISION_ROUNDED_UP(fileSize, prv->parameters.chunkSize);

    /*preparation*/
    fullBytes = (p->numberOfChunksNeeded / 8);
    extraBits = (p->numberOfChunksNeeded % 8);

    /*mark all used chunks dirty. First, go through full bytes*/
    for(byteIdx = 0; byteIdx < fullBytes; byteIdx++)
    {
      p->chunkStatus[byteIdx] = 0xFF; /*all bits marked dirty*/
    }

    /*then, only the used bits in the highest byte will be marked dirty*/
    for(bitIdx=0; bitIdx < extraBits; bitIdx++)
    {
      extraByteValue |= (1 << bitIdx);
    }
    p->chunkStatus[fullBytes] = extraByteValue;

    /*calculate how many percents of the whole file is transferred in one chunk. Value 1234 means 12.34%*/
    p->oneChunkPercentScaled100 = ((100 * 100) / p->numberOfChunksNeeded);
  }
}

static int progressSetChunkStatus(thermitPrv_t *prv, uint8_t chunkNo, bool done)
{
  int ret = -1;
  if(prv && (chunkNo < THERMIT_CHUNK_COUNT_MAX))
  {
    thermitProgress_t *p = &(prv->progress);
    uint8_t byteIdx = THERMIT_PROGRESS_STATUS_BYTE_INDEX(chunkNo);
    uint8_t bitIdx = THERMIT_PROGRESS_STATUS_BIT_INDEX(chunkNo);

    if(done)
    {
      p->chunkStatus[byteIdx] &= ~(1 << bitIdx);  /*clear bit -> done*/
    }
    else
    {
      p->chunkStatus[byteIdx] |= (1 << bitIdx);  /*set bit -> dirty*/
    } 

    DEBUG_INFO("chunk %d = %s\r\n", chunkNo, done?"OK":"DIRTY");

    ret = 0;
  }
  return ret;
}

static bool progressGetChunkIsDone(thermitPrv_t *prv, uint8_t chunkNo)
{
  bool chunkIsDone = false;
  if(prv && (chunkNo < THERMIT_CHUNK_COUNT_MAX))
  {
    thermitProgress_t *p = &(prv->progress);
    uint8_t byteIdx = THERMIT_PROGRESS_STATUS_BYTE_INDEX(chunkNo);
    uint8_t bitIdx = THERMIT_PROGRESS_STATUS_BIT_INDEX(chunkNo);

    if((p->chunkStatus[byteIdx] & (1 << bitIdx)) == 0)
    {
      chunkIsDone = true;
    } 
  }
  return chunkIsDone;
}

static bool progressGetFirstDirty(thermitPrv_t *prv, uint8_t *dirtyChunk)
{
  bool ret = false;
  if(prv && dirtyChunk)
  {
    thermitProgress_t *p = &(prv->progress);
    uint8_t byteIdx;

    for(byteIdx = 0; byteIdx < THERMIT_PROGRESS_STATUS_LENGTH; byteIdx++)
    {
      uint8_t walkedByte = p->chunkStatus[byteIdx];
      if(walkedByte != 0)
      {
        uint8_t bitIdx;

        /*dirty chunk is found at this group*/
        for(bitIdx = 0; bitIdx < 8; bitIdx++)
        {
          if(((walkedByte >> bitIdx) & 0x01) == 0x01)
          {
            /*found!*/
            *dirtyChunk = (byteIdx * 8) + bitIdx;

            DEBUG_INFO("first dirty chunk = %d\r\n", *dirtyChunk);

            ret = true;
            break;  /*stop searching*/
          }
        }
      }
    }
  }
  return ret;
}

#if THERMIT_DEBUG >= THERMIT_DBG_LVL_INFO
#define PROGRESS_DUMP_LINE_LENGTH   40
static void debugDumpProgress(thermitProgress_t *prog, uint8_t *prefix, uint8_t *postfix)
{
  uint8_t i;
  uint8_t byteIdx = 0;
  uint8_t bitIdx = 0;
  uint8_t linesNeeded = DIVISION_ROUNDED_UP(prog->numberOfChunksNeeded, PROGRESS_DUMP_LINE_LENGTH);
  uint8_t totalChunksToBeReported = prog->numberOfChunksNeeded;
  bool finalRound = false;

  if (prefix)
  {
    DEBUG_INFO("%s", prefix);
  }

  if(totalChunksToBeReported > 0)
  {
    for(i = 0; i < linesNeeded; i++)
    {
      uint8_t line[PROGRESS_DUMP_LINE_LENGTH+1];
      uint8_t cntr = PROGRESS_DUMP_LINE_LENGTH;
      uint8_t *ptr = line;

      while(cntr--)
      {

        *(ptr++) = ((((prog->chunkStatus[byteIdx] >> bitIdx) & 0x01) == 0) ? 'G' : '-');
        totalChunksToBeReported--;
        if(totalChunksToBeReported == 0)
        {
          finalRound = true;  // force breaking out from for loop
          break;            // break out from while loop
        }

        /*advance to next*/
        bitIdx++;
        if(bitIdx == 8)
        {
          bitIdx=0;
          byteIdx++;
        }
      }

      *(ptr++) = 0; //terminate
      DEBUG_INFO("%03d: [", i*PROGRESS_DUMP_LINE_LENGTH);
      DEBUG_INFO("%s]", line);
      if(finalRound)
      {
        break;  /*break out from for loop*/
      }
      DEBUG_INFO("\r\n");
    }
  }

  if (postfix)
  {
    DEBUG_INFO("%s", postfix);
  }
}
#else
static void debugDumpProgress(thermitProgress_t *prog, uint8_t *prefix, uint8_t *postfix)
{
  (void)prog;
  (void)prefix;
  (void)postfix;
}
#endif




#if THERMIT_DEBUG >= THERMIT_DBG_LVL_INFO
static void debugDumpFrame(uint8_t *buf, uint8_t *prefix)
{
  int i;
  int pLen = (int)buf[5];

  if (prefix)
  {
    DEBUG_INFO("%s ", prefix);
  }

  DEBUG_INFO("FC:%02X RFId:%02X Feedback:%02X SFId:%02X Chunk:%02X DataLen:%02X(%d) [", buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[5]);

  for (i = 0; i < pLen; i++)
  {
    DEBUG_INFO("%02X%s", buf[6 + i], (i == (pLen - 1) ? "" : " "));
  }

  DEBUG_INFO("] CRC:%04X\r\n", (((uint16_t)buf[6 + pLen]) << 8) | ((uint16_t)buf[6 + pLen + 1]));
}
#else
static void debugDumpFrame(uint8_t *buf, uint8_t *prefix)
{
  (void)buf;
  (void)prefix;
}
#endif

#if THERMIT_DEBUG >= THERMIT_DBG_LVL_INFO
static void debugDumpParameters(thermitParameters_t *par, uint8_t *prefix, uint8_t *postfix)
{
  int i;

  if (prefix)
  {
    DEBUG_INFO("%s", prefix);
  }

  DEBUG_INFO("version = %d, ", par->version);
  DEBUG_INFO("chunkSize = %d, ", par->chunkSize);
  DEBUG_INFO("maxFileSize = %d, ", par->maxFileSize);
  DEBUG_INFO("keepAliveMs = %d, ", par->keepAliveMs);
  DEBUG_INFO("burstLength = %d", par->burstLength);

  if (postfix)
  {
    DEBUG_INFO("%s", postfix);
  }
}
#else
static void debugDumpParameters(thermitParameters_t *par, uint8_t *prefix, uint8_t *postfix)
{
  (void)par;
  (void)prefix;
  (void)postfix;
}
#endif


#if THERMIT_DEBUG >= THERMIT_DBG_LVL_INFO
static void debugDumpState(thermitState_t state, uint8_t *prefix, uint8_t *postfix)
{
  int i;

  if (prefix)
  {
    DEBUG_INFO("%s", prefix);
  }

  switch(state)
  {
    case THERMIT_WAITING_FOR_CALLBACK_CONFIGURATION:
      DEBUG_INFO("THERMIT_WAITING_FOR_CALLBACK_CONFIGURATION");
      break;
    case THERMIT_SYNC_FIRST:
      DEBUG_INFO("THERMIT_SYNC_FIRST");
      break;
    case THERMIT_SYNC_SECOND:
      DEBUG_INFO("THERMIT_SYNC_SECOND");
      break;
    case THERMIT_RUNNING:
      DEBUG_INFO("THERMIT_RUNNING");
      break;
    case THERMIT_OUT_OF_SYNC:
      DEBUG_INFO("THERMIT_OUT_OF_SYNC");
      break;
    default:
      DEBUG_INFO("Illegal state = %d", state);
      break;
  }

  if (postfix)
  {
    DEBUG_INFO("%s", postfix);
  }
}
#else
static void debugDumpState(thermitState_t state, uint8_t *prefix, uint8_t *postfix)
{
  (void)state;
  (void)prefix;
  (void)postfix;
}
#endif


static int changeState(thermitPrv_t *prv, thermitState_t newState)
{
  int ret = -1;
  if(prv)
  {
    if((newState > THERMIT_FIRST_DUMMY_STATE) && (newState < THERMIT_LAST_DUMMY_STATE))
    {
      debugDumpState(newState, "changeState: ", "\r\n");
      prv->state = newState;
      ret = 0;
    }
  }
  return ret;
}


static int parsePacketContent(thermitPrv_t *prv)
{
  int ret = -1;

  if(prv)
  {
    thermitPacket_t *pkt = &(prv->packet);

    if ((pkt->rawLen > 0) && (pkt->rawLen <= THERMIT_MSG_SIZE_MAX))
    {
      uint8_t *p = pkt->rawBuf;
      uint8_t plLen = p[THERMIT_PAYLOAD_LEN_OFFSET];

      if ((plLen <= THERMIT_PAYLOAD_SIZE) && (THERMIT_EXPECTED_LENGHT(plLen) == pkt->rawLen))
      {
        uint8_t *crcPtr = &(p[THERMIT_CRC_OFFSET(plLen)]);
        uint16_t calculatedCrc;
        uint16_t receivedCrc;

        receivedCrc = msgGetU16(&crcPtr);
        calculatedCrc = crc16(p, THERMIT_CRC_OFFSET(plLen));

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

static uint8_t* framePrepare(thermitPacket_t *pkt)
{
  uint8_t *p = NULL;

  if(pkt)
  {
    p = pkt->rawBuf;

    msgPutU8(&p, pkt->fCode);
    msgPutU8(&p, pkt->recFileId);
    msgPutU8(&p, pkt->recFeedback);
    msgPutU8(&p, pkt->sndFileId);
    msgPutU8(&p, pkt->sndChunkNo);
    msgPutU8(&p, pkt->payloadLen);
  }

  return p;
}

static int frameFinalize(thermitPacket_t *pkt, uint8_t len)
{
  int ret = -1;

  if(pkt)
  {
    if(len <= THERMIT_PAYLOAD_SIZE)
    {
      uint8_t *p = pkt->rawBuf;
      uint8_t *crcPtr;
      uint8_t bytesToCover;
      uint16_t calculatedCrc;

      p[THERMIT_PAYLOAD_LEN_OFFSET] = len;
      bytesToCover = THERMIT_CRC_OFFSET(len);
      crcPtr = &(p[bytesToCover]);

      calculatedCrc = crc16(pkt->rawBuf, (uint16_t)bytesToCover); 

      msgPutU16(&crcPtr, calculatedCrc);

      pkt->rawLen = msgLen(pkt->rawBuf, crcPtr);

      ret = 0;
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

static int compareParameterSet(thermitParameters_t *p1, thermitParameters_t *p2)
{
  int ret = -1; // -1 = incompatible parameters -> fatal error

  if(p1 && p2)
  {
    int bytesToCheck = sizeof(thermitParameters_t);
    uint8_t *a = (uint8_t*) p1;
    uint8_t *b = (uint8_t*) p2;

    while(--bytesToCheck)
    {
      if(*(a++) != *(b++))
      {
        break;
      }
    }

    if(bytesToCheck == 0)
    {
      ret = 0;
    }
  }

  return ret;
}


static int waitForSyncProposal(thermitPrv_t *prv)
{
  int ret = -1;
  thermitPacket_t *pkt = &(prv->packet);
  thermitParameters_t params;

  switch (pkt->fCode)
  {
  case THERMIT_FCODE_SYNC_PROPOSAL:
    if(deSerializeParameterStruct(pkt->payloadPtr, pkt->payloadLen, &params) == 0)
    {
      debugDumpParameters(&params, "proposed parameters: ", "\r\n");

      if(findBestCommonParameterSet(&params, &(prv->parameters), &(prv->parameters)) == 0)
      {
        debugDumpParameters(&(prv->parameters), "best common set: ", "\r\n");
        prv->proposalReceived = true; /*this makes the TX function to send response*/

        ret = 0;
      }      
    }
    break;

  default:
    /*all other function codes are considered illegal. Jump to beginning.*/
    ret = changeState(prv, THERMIT_OUT_OF_SYNC);
    break;
  }
}

static int waitForSyncAck(thermitPrv_t *prv)
{
  int ret = -1;

  if(prv)
  {
    thermitPacket_t *pkt = &(prv->packet);

    switch (pkt->fCode)
    {
    case THERMIT_FCODE_SYNC_ACK:
      debugDumpParameters(&(prv->parameters), "agreed parameter set: ", "\r\n");
      ret = 0;
      break;

    default:
      /*all other function codes are considered illegal. Jump to beginning.*/
      ret = changeState(prv, THERMIT_OUT_OF_SYNC);
      break;
    }
  }
  return ret;
}


static int waitForSyncResponse(thermitPrv_t *prv)
{
  int ret = -1;

  if(prv)
  {
    thermitPacket_t *pkt = &(prv->packet);
    thermitParameters_t params;
    thermitParameters_t result;

    switch (pkt->fCode)
    {
    case THERMIT_FCODE_SYNC_RESPONSE:
      if(deSerializeParameterStruct(pkt->payloadPtr, pkt->payloadLen, &params) == 0)
      {
        if(findBestCommonParameterSet(&params, &(prv->parameters), &result) == 0)
        {
          /*check if I can agree on the compromise parameter set?*/
          if(compareParameterSet(&params, &result) == 0)
          {
            /*now we agree on the parameter set. It will be sent to master at tx stage.*/
            ret = changeState(prv, THERMIT_SYNC_SECOND);
          }
        }      
      }

      if(ret != 0)
      {
        DEBUG_ERR("Error: the parameter set cannot be negotiated.\r\n");
      }
      break;

    default:
      /*all other function codes are considered illegal. Jump to beginning.*/
      ret = changeState(prv, THERMIT_OUT_OF_SYNC);
      break;
    }
  }
  return ret;
}

static int sendDataMessage(thermitPrv_t *prv)
{
  int ret = -1;
  if(prv)
  {
    thermitPacket_t *pkt = &(prv->packet);  
    
    pkt->fCode = THERMIT_FCODE_DATA_TRANSFER;
    pkt->recFeedback = 0;
    pkt->recFileId = 0;
    pkt->sndChunkNo = 0;
    pkt->sndFileId = 0;

    if(framePrepare(pkt) != NULL)
    {
      ret = frameFinalize(pkt, 0);
    }
  }

  return ret;
}


static int waitForDataMessage(thermitPrv_t *prv)
{
  int ret = -1;
  thermitPacket_t *pkt = &(prv->packet);

  switch (pkt->fCode)
  {
  case THERMIT_FCODE_DATA_TRANSFER:
    ret = 0;
    break;

  default:
    /*all other function codes are considered illegal. Jump to beginning.*/
    (void)changeState(prv, THERMIT_OUT_OF_SYNC);
    break;
  }
}


static void initializeState(thermitPrv_t *prv)
{
  if(prv)
  {
    changeState(prv, THERMIT_SYNC_FIRST);
    prv->proposalReceived = false;
    prv->ackReceived = false;
  }
}

#if THERMIT_SLAVE_MODE_SUPPORT
static int slaveRx(thermitPrv_t *prv)
{
  int ret = -1;

  if(prv)
  {
    thermitPacket_t *pkt = &(prv->packet);

    switch (prv->state)
    {
    case THERMIT_RUNNING:
      ret = waitForDataMessage(prv);
      break;

    case THERMIT_SYNC_FIRST:
      ret = waitForSyncProposal(prv);
      break;

    case THERMIT_SYNC_SECOND:
      ret = waitForSyncAck(prv);
      if(ret == 0)
      {
        prv->ackReceived = true;
      }
      break;

    case THERMIT_WAITING_FOR_CALLBACK_CONFIGURATION:
      /*no action*/
      ret = 0;
      break;

    default:
      debugDumpState(prv->state, "slaveRx: Unknown state: ", ".\r\n");
      /*unknown state -> require sync*/
      initializeState(prv);
      break;
    }
  }

  return ret;
}
#endif

#if THERMIT_MASTER_MODE_SUPPORT
static int masterRx(thermitPrv_t *prv)
{
  int ret = -1;

  if(prv)
  {
    thermitPacket_t *pkt = &(prv->packet);

    switch (prv->state)
    {
    case THERMIT_RUNNING:
      ret = waitForDataMessage(prv);
      break;

    case THERMIT_SYNC_FIRST:
      ret = waitForSyncResponse(prv);    
      break;

    case THERMIT_SYNC_SECOND:
      ret = waitForSyncAck(prv);
      if(ret == 0)
      {
        (void)changeState(prv, THERMIT_RUNNING);
      }
      break;

    case THERMIT_WAITING_FOR_CALLBACK_CONFIGURATION:
      /*no action*/
      ret = 0;
      break;

    case THERMIT_OUT_OF_SYNC:
      initializeState(prv);
      ret = 0;
      break;

    default:
      debugDumpState(prv->state, "masterRx: Unknown state: ", ".\r\n");
      /*unknown state -> require sync*/
      initializeState(prv);
      ret = 1;
      break;
    }
  }
  return ret;
}
#endif

static int handleIncoming(thermitPrv_t *prv)
{
  int ret = -1;

  if (prv)
  {
    thermitPacket_t *pkt = &(prv->packet);

    ret = 1; /*return positive non-zero if parameters are valid but there's nothing to do*/

    /*check communication device for incoming messages*/
    pkt->rawLen = ioDeviceRead(prv->comLink, pkt->rawBuf, THERMIT_MSG_SIZE_MAX);

    if (parsePacketContent(prv) == 0)
    {
      /*message was received*/
      debugDumpFrame(pkt->rawBuf, "RECV:");

      /*master and slave mode have different states, therefore the handling is separated here*/
      if(prv->isMaster)
      {
        #if THERMIT_MASTER_MODE_SUPPORT        
        ret = masterRx(prv);
        #endif
      }
      else
      {
        #if THERMIT_SLAVE_MODE_SUPPORT        
        ret = slaveRx(prv);
        #endif
      }
    }
  }

  return ret;
}


static int sendSyncProposal(thermitPrv_t *prv)
{
  int ret = -1;

  if(prv)
  {
    thermitPacket_t *pkt = &(prv->packet);
    uint8_t *plBuf;
    uint8_t plLen = THERMIT_PAYLOAD_SIZE;
    
    pkt->fCode = THERMIT_FCODE_SYNC_PROPOSAL;
    pkt->recFeedback = 0;
    pkt->recFileId = 0;
    pkt->sndChunkNo = 0;
    pkt->sndFileId = 0;

    plBuf = framePrepare(pkt);
    if(plBuf)
    {
      if(serializeParameterStruct(plBuf, &plLen, &(prv->parameters)) == 0)
      {
        ret = frameFinalize(pkt, plLen);
      }
    }
  }
  return ret;
}


static int sendSyncResponse(thermitPrv_t *prv)
{
  int ret = -1;

  if(prv)
  {
    thermitPacket_t *pkt = &(prv->packet);
    uint8_t *plBuf;
    uint8_t plLen = THERMIT_PAYLOAD_SIZE;
    
    if(prv->proposalReceived)
    {
      pkt->fCode = THERMIT_FCODE_SYNC_RESPONSE;
      pkt->recFeedback = 0;
      pkt->recFileId = 0;
      pkt->sndChunkNo = 0;
      pkt->sndFileId = 0;

      plBuf = framePrepare(pkt);
      if(plBuf)
      {
        if(serializeParameterStruct(plBuf, &plLen, &(prv->parameters)) == 0)
        {
          ret = frameFinalize(pkt, plLen);
        }
      }

      if(ret == 0)
      {
        ret = changeState(prv, THERMIT_SYNC_SECOND);
      }
    }
  }

  return ret;
}


static int sendOutOfSyncSync(thermitPrv_t *prv)
{
  int ret = -1;
  if(prv)
  {
    thermitPacket_t *pkt = &(prv->packet);  
    
    pkt->fCode = THERMIT_FCODE_OUT_OF_SYNC;
    pkt->recFeedback = 0;
    pkt->recFileId = 0;
    pkt->sndChunkNo = 0;
    pkt->sndFileId = 0;

    (void)framePrepare(pkt);
    ret = frameFinalize(pkt, 0);
  }

  return ret;
}

static int sendSyncAck(thermitPrv_t *prv)
{
  int ret = -1;
  if(prv)
  {
    thermitPacket_t *pkt = &(prv->packet);  
    
    pkt->fCode = THERMIT_FCODE_SYNC_ACK;
    pkt->recFeedback = 0;
    pkt->recFileId = 0;
    pkt->sndChunkNo = 0;
    pkt->sndFileId = 0;

    (void)framePrepare(pkt);
    ret = frameFinalize(pkt, 0);
  }

  return ret;
}

#if THERMIT_MASTER_MODE_SUPPORT
static int masterTx(thermitPrv_t *prv)
{
  int ret = -1;

  if(prv)
  {
    thermitPacket_t *pkt = &(prv->packet);

    switch (prv->state)
    {
    case THERMIT_RUNNING:
      ret = sendDataMessage(prv);
      break;

    case THERMIT_SYNC_FIRST:
      ret = sendSyncProposal(prv);
      break;

    case THERMIT_SYNC_SECOND:
      ret = sendSyncAck(prv);
      break;

    case THERMIT_WAITING_FOR_CALLBACK_CONFIGURATION:
      /*no action*/
      ret = 0;
      break;

    case THERMIT_OUT_OF_SYNC:
      initializeState(prv);
      ret = 0;
      break;

    default:
      debugDumpState(prv->state, "masterTx: Unknown state: ", ".\r\n");
      /*unknown state -> require sync*/
      initializeState(prv);
      break;
    }
  }
  return ret;
}
#endif

#if THERMIT_SLAVE_MODE_SUPPORT
static int slaveTx(thermitPrv_t *prv)
{
  int ret = -1;

  if(prv)
  {
    thermitPacket_t *pkt = &(prv->packet);

    switch (prv->state)
    {
    case THERMIT_RUNNING:
      ret = sendDataMessage(prv);
      break;

    case THERMIT_SYNC_FIRST:
      ret = sendSyncResponse(prv);
      break;

    case THERMIT_SYNC_SECOND:
      if(prv->ackReceived)
      {
        ret = sendSyncAck(prv);
        (void)changeState(prv, THERMIT_RUNNING);
      }
      break;

    case THERMIT_WAITING_FOR_CALLBACK_CONFIGURATION:
      /*no action*/
      ret = 0;
      break;

    case THERMIT_OUT_OF_SYNC:
      ret = sendOutOfSyncSync(prv);
      initializeState(prv);
      break;

    default:
      debugDumpState(prv->state, "slaveTx: Unknown state: ", ".\r\n");
      /*unknown state -> require sync*/
      initializeState(prv);
      break;
    }
  }

  return ret;
}
#endif




static int handleOutgoing(thermitPrv_t *prv)
{
  int ret = -1;

  if (prv)
  {
    thermitPacket_t *pkt = &(prv->packet);

    /*clear first to avoid sending back old message*/
    pkt->rawLen = 0;

    ret = 1; /*return positive non-zero if parameters are valid but there's nothing to do*/

    /*master and slave mode have different states, therefore the handling is separated here*/
    if(prv->isMaster)
    {
      #if THERMIT_MASTER_MODE_SUPPORT        
      ret = masterTx(prv);
      #endif
    }
    else
    {
      #if THERMIT_SLAVE_MODE_SUPPORT        
      ret = slaveTx(prv);
      #endif
    }

    /*if a outgoing message was prepared, send it*/
    if (ret == 0)
    {
      thermitPacket_t *pkt = &(prv->packet);

      if(pkt->rawLen > 0)
      {
        ioDeviceWrite(prv->comLink, pkt->rawBuf, pkt->rawLen);
        debugDumpFrame(pkt->rawBuf, "SEND:");
      }
    }

  }

  return ret;
}


static thermitState_t mStep(thermit_t *inst)
{
  thermitPrv_t *prv = (thermitPrv_t *)inst;
  thermitState_t ret;

  if (prv)
  {
    int rxRet, txRet;
    debugDumpState(prv->state, "thermit->step(", ")\r\n");

    rxRet = handleIncoming(prv);
    txRet = handleOutgoing(prv);
  }

  return ret;
}

static int16_t mFeed(thermit_t *inst, uint8_t *rxBuf, int16_t rxLen)
{
  thermitPrv_t *prv = (thermitPrv_t *)inst;
  int16_t ret = -1;

  if (prv)
  {
    DEBUG_INFO("thermit->feed()\r\n");
  }

  return ret;
}

static int mReset(thermit_t *inst)
{
  thermitPrv_t *prv = (thermitPrv_t *)inst;
  int ret = -1;

  if (prv)
  {
    DEBUG_INFO("thermit->reset()\r\n");
  }

  return ret;
}

static int mSetDeviceOpenCb(thermit_t *inst, cbDeviceOpen_t cb)
{
  thermitPrv_t *prv = (thermitPrv_t *)inst;
  int ret = -1;

  if (prv)
  {
    DEBUG_INFO("thermit->setDeviceOpenCb()\r\n");
  }

  return ret;
}

static int mSetDeviceCloseCb(thermit_t *inst, cbDeviceClose_t cb)
{
  thermitPrv_t *prv = (thermitPrv_t *)inst;
  int ret = -1;

  if (prv)
  {
    DEBUG_INFO("thermit->setDeviceCloseCb()\r\n");
  }

  return ret;
}

static int mSetDeviceReadCb(thermit_t *inst, cbDeviceRead_t cb)
{
  thermitPrv_t *prv = (thermitPrv_t *)inst;
  int ret = -1;

  if (prv)
  {
    DEBUG_INFO("thermit->setDeviceReadCb()\r\n");
  }

  return ret;
}

static int mSetDeviceWriteCb(thermit_t *inst, cbDeviceWrite_t cb)
{
  thermitPrv_t *prv = (thermitPrv_t *)inst;
  int ret = -1;

  if (prv)
  {
    DEBUG_INFO("thermit->setDeviceWriteCb()\r\n");
  }

  return ret;
}

static int mSetFileOpenCb(thermit_t *inst, cbFileOpen_t cb)
{
  thermitPrv_t *prv = (thermitPrv_t *)inst;
  int ret = -1;

  if (prv)
  {
    DEBUG_INFO("thermit->setFileOpenCb()\r\n");
  }

  return ret;
}

static int mSetFileCloseCb(thermit_t *inst, cbFileClose_t cb)
{
  thermitPrv_t *prv = (thermitPrv_t *)inst;
  int ret = -1;

  if (prv)
  {
    DEBUG_INFO("thermit->setFileCloseCb()\r\n");
  }

  return ret;
}

static int mSetFileReadCb(thermit_t *inst, cbFileRead_t cb)
{
  thermitPrv_t *prv = (thermitPrv_t *)inst;
  int ret = -1;

  if (prv)
  {
    DEBUG_INFO("thermit->setFileReadCb()\r\n");
  }

  return ret;
}

static int mSetFileWriteCb(thermit_t *inst, cbFileWrite_t cb)
{
  thermitPrv_t *prv = (thermitPrv_t *)inst;
  int ret = -1;

  if (prv)
  {
    DEBUG_INFO("thermit->setFileWriteCb()\r\n");
  }

  return ret;
}
