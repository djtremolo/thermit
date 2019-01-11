#include "thermit.h"
#include "string.h" //memset
#include "crc.h"
#include "msgBuf.h"


#define THERMIT_INSTANCES_MAX 1

#define DIRTY_CHUNK_NONE    0xFF

#define THERMIT_EASY_MODE   true
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


#define THERMIT_FILE_OFFSET(chunkNo, prv)   ((chunkNo) * ((prv)->parameters.chunkSize))
#define THERMIT_CHUNK_LENGTH_TX(chunkNo, prv)  ((chunkNo) == (((prv)->txProgress.numberOfChunksNeeded)-1) ? ((prv)->txProgress.fileSize % ((prv)->parameters.chunkSize)) : (prv)->parameters.chunkSize)
#define THERMIT_CHUNK_LENGTH_RX(chunkNo, prv)  ((chunkNo) == (((prv)->rxProgress.numberOfChunksNeeded)-1) ? ((prv)->rxProgress.fileSize % ((prv)->parameters.chunkSize)) : (prv)->parameters.chunkSize)


#define THERMIT_PROGRESS_STATUS_LENGTH                DIVISION_ROUNDED_UP(THERMIT_CHUNK_COUNT_MAX, 8)   
#define THERMIT_PROGRESS_STATUS_BYTE_INDEX(chunkNo)   ((chunkNo) / 8)
#define THERMIT_PROGRESS_STATUS_BIT_INDEX(chunkNo)    ((chunkNo) % 8)

#define THERMIT_ADVANCE_TO_NEXT(var, max) ((((var)) + 1) % (max))

typedef struct
{
  bool running;
  uint16_t fileSize;
  thermitIoSlot_t fileHandle;
  uint8_t fileId;
  uint8_t chunkNo;
  uint8_t fileName[THERMIT_FILENAME_MAX+1];

  uint8_t chunkStatus[THERMIT_PROGRESS_STATUS_LENGTH];     /*each bit represents one chunk: 1=dirty 0=done*/
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

  thermitTargetAdaptationInterface_t targetIf;    //devive / file / system adaptation layer

  thermitState_t state;

  thermitIoSlot_t comLink;

  uint8_t nextOutgoingFileId;

  /*store parsed packet after receiving*/
  thermitPacket_t packet;

  bool proposalReceived;
  bool ackReceived;

  uint8_t receivedFeedback;
  uint8_t firstDirtyChunk;

  bool sendWTF;

  bool isMaster;

  thermitProgress_t txProgress;
  thermitProgress_t rxProgress;

  thermitParameters_t parameters;
  thermitDiagnostics_t diagnostics;
} thermitPrv_t;

static int deSerializeParameterStruct(uint8_t *buf, uint8_t len, thermitParameters_t *params);
static int serializeParameterStruct(uint8_t *buf, uint8_t *len, thermitParameters_t *params);
static int findBestCommonParameterSet(thermitParameters_t *p1, thermitParameters_t *p2, thermitParameters_t *result);

static void debugDumpParameters(thermitPrv_t *prv, uint8_t *prefix, uint8_t *postfix);
static void debugDumpFrame(thermitPrv_t *prv, uint8_t *buf, uint8_t *prefix);
static void debugDumpState(thermitPrv_t *prv, uint8_t *prefix, uint8_t *postfix);
static void debugDumpProgress(thermitPrv_t *prv, thermitProgress_t *prog, uint8_t *prefix, uint8_t *postfix);

static int progressInitialize(thermitPrv_t *prv, thermitProgress_t *prog, uint16_t fileSize);
static int progressSetChunkStatus(thermitPrv_t *prv, thermitProgress_t *prog, uint8_t chunkNo, bool done);
static bool progressGetChunkIsDone(thermitPrv_t *prv, thermitProgress_t *prog, uint8_t chunkNo);
static bool progressGetFirstDirty(thermitPrv_t *prv, thermitProgress_t *prog, uint8_t *dirtyChunk);


static void initializeState(thermitPrv_t *prv);

static int parsePacketContent(thermitPrv_t *prv);

static uint8_t* framePrepare(thermitPrv_t *prv);
static int frameFinalize(thermitPrv_t *prv, uint8_t len);


static int handleIncoming(thermitPrv_t *prv);
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
static int mReset(thermit_t *inst);


static const struct thermitMethodTable_t mTable =
{
  mStep,
  mReset,
};


#define THERMIT_RESERVED_MAGIC_VALUE 0xA55B

static thermitPrv_t thermitInstances[THERMIT_INSTANCES_MAX];

static bool validateTargetAdaptation(thermitTargetAdaptationInterface_t *targetIf)
{
  if(targetIf == NULL)
    return false;
  if(targetIf->devOpen == NULL)
    return false;
  if(targetIf->devClose == NULL)
    return false;
  if(targetIf->devRead == NULL)
    return false;
  if(targetIf->devWrite == NULL)
    return false;
  if(targetIf->fileOpen == NULL)
    return false;
  if(targetIf->fileClose == NULL)
    return false;
  if(targetIf->fileRead == NULL)
    return false;
  if(targetIf->fileWrite == NULL)
    return false;
  if(targetIf->sysGetMs == NULL)
    return false;
  if(targetIf->sysCrc16 == NULL)
    return false;

#if THERMIT_DEBUG > THERMIT_DBG_LVL_NONE
  if(targetIf->sysPrintf == NULL)
    return false;
#endif

  return true;
}


static thermitPrv_t *reserveInstance(thermitTargetAdaptationInterface_t *targetIf)
{
  thermitPrv_t *inst = NULL;
  int i;

  if(validateTargetAdaptation(targetIf))
  {
    for (i = 0; i < THERMIT_INSTANCES_MAX; i++)
    {
      if (thermitInstances[i].reserved != THERMIT_RESERVED_MAGIC_VALUE)
      {
        inst = &(thermitInstances[i]);

        memset(inst, 0, sizeof(thermitPrv_t));
        thermitInstances[i].reserved = THERMIT_RESERVED_MAGIC_VALUE;

        memcpy(&(inst->targetIf), targetIf, sizeof(thermitTargetAdaptationInterface_t));

        break; /*found!*/
      }
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


thermit_t *thermitNew(uint8_t *linkName, bool isMaster, thermitTargetAdaptationInterface_t *targetIf)
{
  thermitPrv_t *returnedPrivateInstance = NULL;

  if (linkName)
  {
    thermitPrv_t *p = reserveInstance(targetIf);

    if (p)
    {
      DEBUG_INFO(p, "thermitNew(): sizeof(thermitPrv_t)=%d\r\n", sizeof(thermitPrv_t));
      //don't allow creating modes that are not supported
      #if !THERMIT_MASTER_MODE_SUPPORT
      if(isMaster)
      {
        DEBUG_FATAL(p, "Fatal: Master mode not supported!\r\n");
        releaseInstance(p);
        return NULL;
      }
      #endif
      #if !THERMIT_SLAVE_MODE_SUPPORT
      if(!isMaster)
      {
        DEBUG_FATAL(p, "Fatal: Slave mode not supported!\r\n");
        releaseInstance(p);
        return NULL;
      }
      #endif

      /*try to open communication device.*/
      p->comLink = targetIf->devOpen(linkName, 0);

      if(p->comLink >= 0)
      {
        p->m = &mTable;
        p->isMaster = isMaster;

        initializeParameters(p);

        debugDumpParameters(p, "Initial parameters: ", "\r\n");

        initializeState(p);

        DEBUG_INFO(p, "created %s instance using '%s'.\r\n", (isMaster ? "master" : "slave"), linkName);

        returnedPrivateInstance = p; /*return this instance as it was successfully created*/
      }
      else
      {
        DEBUG_ERR(p, "Error: Could not open communication device '%s'.\r\n", linkName);
        releaseInstance(p);
      }
    }
  }

  return (thermit_t *)returnedPrivateInstance;
}

void thermitDelete(thermit_t *inst)
{
  thermitPrv_t *prv = (thermitPrv_t *)inst;

  DEBUG_INFO(prv, "thermitDelete()\r\n");

  if (prv)
  {
    thermitTargetAdaptationInterface_t *tgt = &(prv->targetIf);
    prv->comLink = tgt->devClose(prv->comLink);
    releaseInstance(prv);
    DEBUG_INFO(prv, "instance deleted\r\n");
  }
  else
  {
    DEBUG_ERR(prv, "deletion FAILED\r\n");
  }
}


static int progressInitialize(thermitPrv_t *prv, thermitProgress_t *progress, uint16_t fileSize)
{
  int ret = -1;

  if(prv && progress && (fileSize <= prv->parameters.maxFileSize) && (fileSize > 0))
  {
    uint8_t fullBytes;
    uint8_t extraBits;
    uint8_t byteIdx, bitIdx;
    uint8_t extraByteValue = 0;

    memset(progress, 0, sizeof(thermitProgress_t));

    progress->fileSize = fileSize;

    /*remember number of chunks needed for this file*/
    progress->numberOfChunksNeeded = DIVISION_ROUNDED_UP(fileSize, prv->parameters.chunkSize);

    /*preparation*/
    fullBytes = (progress->numberOfChunksNeeded / 8);
    extraBits = (progress->numberOfChunksNeeded % 8);

    /*mark all used chunks dirty. First, go through full bytes*/
    for(byteIdx = 0; byteIdx < fullBytes; byteIdx++)
    {
      progress->chunkStatus[byteIdx] = 0xFF; /*all bits marked dirty*/
    }

    /*then, only the used bits in the highest byte will be marked dirty*/
    for(bitIdx=0; bitIdx < extraBits; bitIdx++)
    {
      extraByteValue |= (1 << bitIdx);
    }
    progress->chunkStatus[fullBytes] = extraByteValue;

    /*calculate how many percents of the whole file is transferred in one chunk. Value 1234 means 12.34%*/
    progress->oneChunkPercentScaled100 = ((100 * 100) / progress->numberOfChunksNeeded);

    progress->running = true;

    ret = 0;
  }
  return ret;
}

static int progressSetChunkStatus(thermitPrv_t *prv, thermitProgress_t *progress, uint8_t chunkNo, bool done)
{
  int ret = -1;
  if(prv && progress && (chunkNo < progress->numberOfChunksNeeded))
  {
    uint8_t byteIdx = THERMIT_PROGRESS_STATUS_BYTE_INDEX(chunkNo);
    uint8_t bitIdx = THERMIT_PROGRESS_STATUS_BIT_INDEX(chunkNo);

    if(done)
    {
      progress->chunkStatus[byteIdx] &= ~(1 << bitIdx);  /*clear bit -> done*/
    }
    else
    {
      progress->chunkStatus[byteIdx] |= (1 << bitIdx);  /*set bit -> dirty*/
    } 

    DEBUG_INFO(prv, "chunk %d = %s\r\n", chunkNo, done?"OK":"DIRTY");

    ret = 0;
  }
  return ret;
}

static bool progressGetChunkIsDone(thermitPrv_t *prv, thermitProgress_t *progress, uint8_t chunkNo)
{
  bool chunkIsDone = false;
  if(prv && progress && (chunkNo < progress->numberOfChunksNeeded))
  {
    uint8_t byteIdx = THERMIT_PROGRESS_STATUS_BYTE_INDEX(chunkNo);
    uint8_t bitIdx = THERMIT_PROGRESS_STATUS_BIT_INDEX(chunkNo);

    if((progress->chunkStatus[byteIdx] & (1 << bitIdx)) == 0)
    {
      chunkIsDone = true;
    } 
  }
  return chunkIsDone;
}

static bool progressGetFirstDirty(thermitPrv_t *prv, thermitProgress_t *progress, uint8_t *dirtyChunk)
{
  bool ret = false;

  if(prv && progress && dirtyChunk)
  {
    uint8_t bitIdx = 0;
    uint8_t byteIdx = 0;
    uint8_t chunksLeft = progress->numberOfChunksNeeded;

    while(chunksLeft--)
    {
      uint8_t walkedByte = progress->chunkStatus[byteIdx];

      if(walkedByte == 0)
      {
        /*there's no point in walking through this byte as it is full zeros. Jump to next if possible.*/
        uint8_t jumps = 8 - bitIdx;

        if(chunksLeft > jumps)
        {
          /*we are jumping to bit0 of the next byte*/
          bitIdx = 0;
          byteIdx++;
          chunksLeft -= jumps;
        }
        else
        {
          /*there's no next byte available, quit*/
          break;
        }
      }
      else
      {
        if(((walkedByte >> bitIdx) & 0x01) == 0x01)
        {
          /*found!*/
          *dirtyChunk = (byteIdx * 8) + bitIdx;

          DEBUG_INFO(prv, "first dirty chunk = %d\r\n", *dirtyChunk);

          ret = true;
          break;  /*stop searching*/
        }

        /*advance to next*/
        bitIdx++;
        if(bitIdx == 8)
        {
          byteIdx++;
        }
      }
    }
  }
  return ret;
}

#if THERMIT_DEBUG >= THERMIT_DBG_LVL_INFO
#define PROGRESS_DUMP_LINE_LENGTH   40
static void debugDumpProgress(thermitPrv_t *prv, thermitProgress_t *prog, uint8_t *prefix, uint8_t *postfix)
{
  uint8_t i;
  uint8_t byteIdx = 0;
  uint8_t bitIdx = 0;

  if(prv && prog)
  {
    uint8_t linesNeeded = DIVISION_ROUNDED_UP(prog->numberOfChunksNeeded, PROGRESS_DUMP_LINE_LENGTH);
    uint8_t totalChunksToBeReported = prog->numberOfChunksNeeded;
    bool finalRound = false;

    if (prefix)
    {
      DEBUG_INFO(prv, "%s", prefix);
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
        DEBUG_INFO(prv, "%03d: [", i*PROGRESS_DUMP_LINE_LENGTH);
        DEBUG_INFO(prv, "%s]", line);
        if(finalRound)
        {
          break;  /*break out from for loop*/
        }
        DEBUG_INFO(prv, "\r\n");
      }
    }

    if (postfix)
    {
      DEBUG_INFO(prv, "%s", postfix);
    }
  }
}
#else
static void debugDumpProgress(thermitPrv_t *prv, thermitProgress_t *prog, uint8_t *prefix, uint8_t *postfix)
{
  (void)prv;
  (void)prog;
  (void)prefix;
  (void)postfix;
}
#endif




#if THERMIT_DEBUG >= THERMIT_DBG_LVL_INFO
static void debugDumpFrame(thermitPrv_t *prv, uint8_t *buf, uint8_t *prefix)
{
  int i;
  int pLen = (int)buf[5];

  if (prefix)
  {
    DEBUG_INFO(prv, "%s ", prefix);
  }

  DEBUG_INFO(prv, "FC:%02X RFId:%02X Feedback:%02X SFId:%02X Chunk:%02X DataLen:%02X(%d) [", buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[5]);

  for (i = 0; i < pLen; i++)
  {
    DEBUG_INFO(prv, "%02X%s", buf[6 + i], (i == (pLen - 1) ? "" : " "));
  }

  DEBUG_INFO(prv, "] CRC:%04X\r\n", (((uint16_t)buf[6 + pLen]) << 8) | ((uint16_t)buf[6 + pLen + 1]));
}
#else
static void debugDumpFrame(thermitPrv_t *prv, uint8_t *buf, uint8_t *prefix)
{
  (void)buf;
  (void)prefix;
}
#endif

#if THERMIT_DEBUG >= THERMIT_DBG_LVL_INFO
static void debugDumpParameters(thermitPrv_t *prv, uint8_t *prefix, uint8_t *postfix)
{
  int i;

  if(prv)
  {
    thermitParameters_t *par = &(prv->parameters);

    if (prefix)
    {
      DEBUG_INFO(prv, "%s", prefix);
    }

    DEBUG_INFO(prv, "version = %d, ", par->version);
    DEBUG_INFO(prv, "chunkSize = %d, ", par->chunkSize);
    DEBUG_INFO(prv, "maxFileSize = %d, ", par->maxFileSize);
    DEBUG_INFO(prv, "keepAliveMs = %d, ", par->keepAliveMs);
    DEBUG_INFO(prv, "burstLength = %d", par->burstLength);

    if (postfix)
    {
      DEBUG_INFO(prv, "%s", postfix);
    }
  }
}
#else
static void debugDumpParameters(thermitPrv_t *prv, uint8_t *prefix, uint8_t *postfix)
{
  (void)prv;
  (void)prefix;
  (void)postfix;
}
#endif


#if THERMIT_DEBUG >= THERMIT_DBG_LVL_INFO
static void debugDumpState(thermitPrv_t *prv, uint8_t *prefix, uint8_t *postfix)
{
  int i;

  if(prv)
  {
    thermitState_t state = prv->state;

    if (prefix)
    {
      DEBUG_INFO(prv, "%s", prefix);
    }

    switch(state)
    {
      case THERMIT_WAITING_FOR_CALLBACK_CONFIGURATION:
        DEBUG_INFO(prv, "THERMIT_WAITING_FOR_CALLBACK_CONFIGURATION");
        break;
      case THERMIT_SYNC_FIRST:
        DEBUG_INFO(prv, "THERMIT_SYNC_FIRST");
        break;
      case THERMIT_SYNC_SECOND:
        DEBUG_INFO(prv, "THERMIT_SYNC_SECOND");
        break;
      case THERMIT_RUNNING:
        DEBUG_INFO(prv, "THERMIT_RUNNING");
        break;
      case THERMIT_OUT_OF_SYNC:
        DEBUG_INFO(prv, "THERMIT_OUT_OF_SYNC");
        break;
      default:
        DEBUG_INFO(prv, "Illegal state = %d", state);
        break;
    }

    if (postfix)
    {
      DEBUG_INFO(prv, "%s", postfix);
    }
  }
}
#else
static void debugDumpState(thermitPrv_t *prv, uint8_t *prefix, uint8_t *postfix)
{
  (void)prv;
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
      prv->state = newState;
      debugDumpState(prv, "changeState: ", "\r\n");
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
        thermitTargetAdaptationInterface_t *tgt = &(prv->targetIf);

        receivedCrc = msgGetU16(&crcPtr);
        calculatedCrc = tgt->sysCrc16(p, THERMIT_CRC_OFFSET(plLen));

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

static uint8_t* framePrepare(thermitPrv_t *prv)
{
  uint8_t *p = NULL;

  if(prv)
  {
    thermitProgress_t *rx = &(prv->rxProgress);
    thermitProgress_t *tx = &(prv->txProgress);
    thermitPacket_t *pkt = &(prv->packet);
    p = pkt->rawBuf;

    if(rx->running)
    {
      pkt->recFileId = rx->fileId;
    }
    else
    {
      pkt->recFileId = 0;
    }

    if(tx->running)
    {
      pkt->sndFileId = tx->fileId;
    }
    else
    {
      pkt->sndFileId = 0;
    }


    msgPutU8(&p, pkt->fCode);
    msgPutU8(&p, pkt->recFileId);
    msgPutU8(&p, pkt->recFeedback);
    msgPutU8(&p, pkt->sndFileId);
    msgPutU8(&p, pkt->sndChunkNo);
    msgPutU8(&p, pkt->payloadLen);
  }

  return p;
}

static int frameFinalize(thermitPrv_t *prv, uint8_t len)
{
  int ret = -1;

  if(prv)
  {
    thermitPacket_t *pkt = &(prv->packet);

    if(len <= THERMIT_PAYLOAD_SIZE)
    {
      uint8_t *p = pkt->rawBuf;
      uint8_t *crcPtr;
      uint8_t bytesToCover;
      uint16_t calculatedCrc;
      thermitTargetAdaptationInterface_t *tgt = &(prv->targetIf);

      p[THERMIT_PAYLOAD_LEN_OFFSET] = len;
      bytesToCover = THERMIT_CRC_OFFSET(len);
      crcPtr = &(p[bytesToCover]);

      calculatedCrc = tgt->sysCrc16(pkt->rawBuf, (uint16_t)bytesToCover); 

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
      debugDumpParameters(prv, "proposed parameters: ", "\r\n");

      if(findBestCommonParameterSet(&params, &(prv->parameters), &(prv->parameters)) == 0)
      {
        debugDumpParameters(prv, "best common set: ", "\r\n");
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
      debugDumpParameters(prv, "agreed parameter set: ", "\r\n");
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
        DEBUG_ERR(prv, "Error: the parameter set cannot be negotiated.\r\n");
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


static uint8_t fillFileInfoMessage(uint8_t *plBuf, uint8_t *fileName, uint16_t fileSize)
{
  uint8_t bytesWritten = 0;

  if(plBuf && fileName)
  {
    uint8_t *fnPtr = fileName;
    uint8_t *start = plBuf;
    uint8_t *lenBytePtr;
    uint8_t fnLen = 0;
    uint8_t fnBytesMax = THERMIT_FILENAME_MAX;

    /*
    uint16_t size
    uint8_t fileNameLen
    uint8_t fileName[fileNameLen]    
    */
    msgPutU16(&plBuf, fileSize);
    lenBytePtr = plBuf++; /*store this as we need to update it later*/
    while(fnBytesMax--)
    {
      if(*fnPtr == 0)
      {
        break;
      }
      msgPutU8(&plBuf, *(fnPtr++));
      fnLen++;
    }
    msgPutU8(&plBuf, 0);    //terminate

    /*update file name length*/
    msgPutU8(&lenBytePtr, fnLen+1);

    bytesWritten = msgLen(start, plBuf);
  }
  return bytesWritten;
}

static void handleDataMessage(thermitPrv_t *prv)
{
  if(prv->state == THERMIT_RUNNING)
  {
    thermitTargetAdaptationInterface_t *tgt = &(prv->targetIf);
    thermitProgress_t *rxProgress = &(prv->rxProgress);
    thermitProgress_t *txProgress = &(prv->txProgress);
    thermitPacket_t *pkt = &(prv->packet);

    if(rxProgress->running)
    {
      if(pkt->sndFileId == rxProgress->fileId)
      {
        uint16_t offset = THERMIT_FILE_OFFSET(pkt->sndChunkNo, prv);
        int16_t length = pkt->payloadLen;

        DEBUG_INFO(prv, "Chunk %d of file %d received.\r\n", pkt->sndChunkNo, pkt->sndFileId);
        DEBUG_INFO(prv, "writing offset=%d, length=%d.\r\n", offset, length);

        if(tgt->fileWrite(rxProgress->fileHandle, offset, pkt->payloadPtr, length) == 0)
        {
          uint8_t dirtyChunk;

          progressSetChunkStatus(prv, rxProgress, pkt->sndChunkNo, true);
          debugDumpProgress(prv, rxProgress, "", "\r\n");

          /*check if the file is ready*/
          if(progressGetFirstDirty(prv, rxProgress, &dirtyChunk) == false)
          {
            thermitTargetAdaptationInterface_t *tgt = &(prv->targetIf);
            DEBUG_INFO(prv, "successfully received file, closing rx file transfer.\r\n");

            tgt->fileClose(rxProgress->fileHandle);
            rxProgress->running = false;
          }
        }
        else
        {
          DEBUG_INFO(prv, "file writing failed.\r\n");
        }
      }
      else
      {
        DEBUG_INFO(prv, "THERMIT_FEEDBACK: FileID does not match, expected %d, got %d.\r\n", rxProgress->fileId, pkt->sndFileId);
      }
    }

    if(txProgress->running)
    {
      if(pkt->recFileId == txProgress->fileId)
      {
        switch(pkt->recFeedback)
        {
          case THERMIT_FEEDBACK_FILE_IS_READY:
            if(txProgress->running)
            {
              txProgress->running = false;
              DEBUG_INFO(prv, "file sending finished successfully\r\n");
              (void)tgt->fileClose(txProgress->fileHandle);
            }
            break;

          default:
            prv->firstDirtyChunk = pkt->recFeedback;
            break;
        }
      }
    }

  }
}



typedef enum
{
  THERMIT_OUT_NOTHING,
  THERMIT_OUT_KEEP_ALIVE,
  THERMIT_OUT_FILE_INFO,
  THERMIT_OUT_CHUNK,
  THERMIT_OUT_EMPTY_DATA,
  THERMIT_OUT_WRITE_TERMINATED_FORCEFULLY
} outMsgClass_t;


static outMsgClass_t updateOutGoingState(thermitPrv_t *prv)
{
  outMsgClass_t whatToSend = THERMIT_OUT_NOTHING;

  if(prv)
  {
    thermitProgress_t *txProgress = &(prv->txProgress);
    thermitTargetAdaptationInterface_t *tgt = &(prv->targetIf);

    /*send error message in case of recent failure*/
    if(prv->sendWTF)
    {
      prv->sendWTF = false;

      return THERMIT_OUT_WRITE_TERMINATED_FORCEFULLY;
    }

    /*check if outgoing file transfer is currently active. If not, check
    if a new file is available for sending. If yes, open it and start sending.*/
    if(txProgress->running)
    {
      /*send next chunk*/
      whatToSend = THERMIT_OUT_CHUNK;
    }
    else
    {
      /*open new file for sending if available*/
      uint16_t fileSize;
      uint8_t fName[THERMIT_FILENAME_MAX+1];

#if THERMIT_EASY_MODE
      //easy mode: only master sends, slave receives
      if((prv->isMaster) && tgt->fileAvailableForSending(fName, &fileSize))
#else
      if(tgt->fileAvailableForSending(fName, &fileSize))
#endif        
      {
        thermitIoSlot_t fileHandle;

        fileHandle = tgt->fileOpen(fName, THERMIT_READ, &fileSize);
        if(fileHandle >= 0)
        {
          thermitProgress_t *txProgress = &(prv->txProgress);

          if(progressInitialize(prv, txProgress, fileSize) >= 0)
          {
            uint8_t *plBuf;
            uint16_t fileInfoLen;
            thermitPacket_t *pkt = &(prv->packet);

            DEBUG_INFO(prv, "starting new file transfer\r\n");

            txProgress->running = true;
            txProgress->fileSize = fileSize;
            txProgress->fileHandle = fileHandle;
            txProgress->fileId = prv->nextOutgoingFileId;
            txProgress->chunkNo = 0;
            strncpy(txProgress->fileName, fName, THERMIT_FILENAME_MAX);   /*todo optimize*/
            prv->nextOutgoingFileId = THERMIT_ADVANCE_TO_NEXT(prv->nextOutgoingFileId, THERMIT_FILEID_MAX);

            whatToSend = THERMIT_OUT_FILE_INFO;
          }
          else
          {
            DEBUG_ERR(prv, "file opening for read failed\r\n");
            tgt->fileClose(fileHandle);
          }
        }
      }
      else
      {
        whatToSend = THERMIT_OUT_EMPTY_DATA;
        DEBUG_INFO(prv, "waiting for new file to be sent\r\n");
      }
    }
  }

  if(whatToSend == THERMIT_OUT_NOTHING)
  {
    DEBUG_INFO(prv, "nothing to send\r\n");
  }

  return whatToSend;
}


uint8_t getFeedback(thermitPrv_t *prv)
{
  uint8_t fb = THERMIT_FEEDBACK_FILE_IS_READY;

  if(prv)
  {
    thermitProgress_t *rxProgress = &(prv->rxProgress);
    if(rxProgress->running)
    {
      uint8_t dirtyChunk = 0;
      if(progressGetFirstDirty(prv, rxProgress, &dirtyChunk))
      {
        fb = dirtyChunk;
      }
    }
  }

  return fb;
}


static int sendDataMessage(thermitPrv_t *prv)
{
  int ret = -1;

  if(prv)
  {
    thermitTargetAdaptationInterface_t *tgt = &(prv->targetIf);
    thermitPacket_t *pkt = &(prv->packet);
    thermitProgress_t *txProgress = &(prv->txProgress);
    thermitProgress_t *rxProgress = &(prv->rxProgress);
    uint8_t *plPtr;
    uint8_t plLen;
    uint16_t offset;
    uint16_t length;
    uint16_t readLen;
    int bytesRead;

    pkt->recFeedback = getFeedback(prv);
    pkt->recFileId = rxProgress->fileId;
    pkt->sndChunkNo = txProgress->chunkNo;
    pkt->sndFileId = txProgress->fileId;

    switch(updateOutGoingState(prv))
    {
      case THERMIT_OUT_CHUNK:
        offset = THERMIT_FILE_OFFSET(txProgress->chunkNo, prv);
        length = THERMIT_CHUNK_LENGTH_TX(txProgress->chunkNo, prv);

        pkt->fCode = THERMIT_FCODE_DATA_TRANSFER;
        plPtr = framePrepare(prv);
        plLen = 0;

        DEBUG_INFO(prv, "sending chunk %d: offset=%d, length=%d\r\n", txProgress->chunkNo, offset, length);

        bytesRead = tgt->fileRead(txProgress->fileHandle, offset, plPtr, length);
        if(bytesRead >= 0)
        {
          if((uint16_t)bytesRead == length)
          {
            plLen = length;
            ret = frameFinalize(prv, plLen);

            /*check if frame was correctly prepared, if yes, then advance to next chunk to be sent on the next round*/
            if(ret == 0)
            {
              uint8_t nextChunk = THERMIT_ADVANCE_TO_NEXT(txProgress->chunkNo, THERMIT_CHUNK_COUNT_MAX);

              if(nextChunk < txProgress->numberOfChunksNeeded)
              {
                txProgress->chunkNo = nextChunk;
              }
              else
              {
                if(prv->firstDirtyChunk < (txProgress->numberOfChunksNeeded))
                {
                  txProgress->chunkNo = prv->firstDirtyChunk;
                  DEBUG_INFO(prv, "first round of file transfer was completed, now resending dirty chunk %d.\r\n", txProgress->chunkNo);
                }
              }
            }
          }
          else
          {
            DEBUG_ERR(prv, "file read failed: expected %d bytes, got %d.\r\n", length, readLen);
          }
        }
        else
        {
          DEBUG_ERR(prv, "file read failed: negative return value.\r\n");
        }
        break;

      case THERMIT_OUT_FILE_INFO:
        pkt->fCode = THERMIT_FCODE_NEW_FILE_START;
        plPtr = framePrepare(prv);
        plLen = fillFileInfoMessage(plPtr, txProgress->fileName, txProgress->fileSize);
        ret = frameFinalize(prv, plLen);
        break;

      case THERMIT_OUT_EMPTY_DATA:
        pkt->fCode = THERMIT_FCODE_DATA_TRANSFER;
        (void)framePrepare(prv);
        ret = frameFinalize(prv, 0);
        break;

      case THERMIT_OUT_WRITE_TERMINATED_FORCEFULLY:
        pkt->fCode = THERMIT_FCODE_WRITE_TERMINATED_FORCEFULLY;

        (void)framePrepare(prv);
        ret = frameFinalize(prv, 0);
        break;

      case THERMIT_OUT_KEEP_ALIVE:
        break;

      case THERMIT_OUT_NOTHING:
        break;

      default:
        break;
    }
  }

  return ret;
}


static int parseFileInfoMessage(thermitPrv_t *prv, uint8_t *fileName, uint8_t fileNameMaxLen, uint16_t *fileSizePtr)
{
  int ret = -1;

  if(prv && fileName && fileSizePtr)
  {
    thermitPacket_t *pkt = &(prv->packet);

    uint8_t *p = pkt->payloadPtr;
    uint8_t len = pkt->payloadLen;

    /*
    uint16_t size
    uint8_t fileNameLen
    uint8_t fileName[fileNameLen]    
    */


    if(len > 3)
    {
      uint8_t fnLen;
      uint8_t *fnPtr = fileName;

      *fileSizePtr = msgGetU16(&p);
      fnLen = msgGetU8(&p);
      fnLen = ((fnLen < fileNameMaxLen) ? fnLen : fileNameMaxLen-1);

      while(fnLen--)
      {
        *(fnPtr++) = msgGetU8(&p);
      }

      *fnPtr = 0;

      DEBUG_INFO(prv, "file info: name='%s', size=%d\r\n", fileName, *fileSizePtr);

      ret = 0;
    }
  }
  return ret;
}

static int waitForDataMessage(thermitPrv_t *prv)
{
  int ret = -1;
  thermitPacket_t *pkt = &(prv->packet);
  thermitProgress_t *rxProgress = &(prv->rxProgress);

  switch (pkt->fCode)
  {
  case THERMIT_FCODE_DATA_TRANSFER:
    handleDataMessage(prv);
    ret = 0;
    break;

  case THERMIT_FCODE_NEW_FILE_START:
    if(!rxProgress->running)
    {
      uint8_t fName[THERMIT_FILENAME_MAX+1];
      uint16_t fileSize;

      if(parseFileInfoMessage(prv, fName, THERMIT_FILENAME_MAX, &fileSize) == 0)
      {
        thermitTargetAdaptationInterface_t *tgt = &(prv->targetIf);
        thermitIoSlot_t fileHandle = tgt->fileOpen(rxProgress->fileName, THERMIT_WRITE, &fileSize);

        if(fileHandle >= 0)
        {
          ret = progressInitialize(prv, rxProgress, fileSize);

          rxProgress->running = true;
          rxProgress->fileId = pkt->sndFileId;
          strncpy(rxProgress->fileName, fName, THERMIT_FILENAME_MAX);
        }
        else
        {
          DEBUG_ERR(prv, "opening new rx file failed\r\n");
        }
      }
      else
      {
        /*illegal format in incoming file info frame, send "write terminated forcefully" frame.*/
        DEBUG_ERR(prv, "file info parsing failed, sending error frame\r\n");
        prv->sendWTF = true;
      }
    }
    else
    {
      /*cannot accept new file info during transfer, send "write terminated forcefully" frame.*/
      DEBUG_ERR(prv, "remote tried to start new file during transfer, sending error frame\r\n");
      prv->sendWTF = true;
    }
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
      debugDumpState(prv, "slaveRx: Unknown state: ", ".\r\n");
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
      debugDumpState(prv, "masterRx: Unknown state: ", ".\r\n");
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
    thermitTargetAdaptationInterface_t *tgt = &(prv->targetIf);

    ret = 1; /*return positive non-zero if parameters are valid but there's nothing to do*/

    /*check communication device for incoming messages*/
    pkt->rawLen = tgt->devRead(prv->comLink, pkt->rawBuf, THERMIT_MSG_SIZE_MAX);

    if (parsePacketContent(prv) == 0)
    {
      /*message was received*/
      debugDumpFrame(prv, pkt->rawBuf, "RECV:");

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

    plBuf = framePrepare(prv);
    if(plBuf)
    {
      if(serializeParameterStruct(plBuf, &plLen, &(prv->parameters)) == 0)
      {
        ret = frameFinalize(prv, plLen);
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

      plBuf = framePrepare(prv);
      if(plBuf)
      {
        if(serializeParameterStruct(plBuf, &plLen, &(prv->parameters)) == 0)
        {
          ret = frameFinalize(prv, plLen);
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


static int sendOutOfSync(thermitPrv_t *prv)
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

    (void)framePrepare(prv);
    ret = frameFinalize(prv, 0);
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

    (void)framePrepare(prv);
    ret = frameFinalize(prv, 0);
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
      debugDumpState(prv, "masterTx: Unknown state: ", ".\r\n");
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
      ret = sendOutOfSync(prv);
      initializeState(prv);
      break;

    default:
      debugDumpState(prv, "slaveTx: Unknown state: ", ".\r\n");
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
        thermitTargetAdaptationInterface_t *tgt = &(prv->targetIf);
  
        (void)tgt->devWrite(prv->comLink, pkt->rawBuf, pkt->rawLen);
        debugDumpFrame(prv, pkt->rawBuf, "SEND:");
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
    debugDumpState(prv, "thermit->step(", ")\r\n");

    rxRet = handleIncoming(prv);
    txRet = handleOutgoing(prv);
  }

  return ret;
}

static int mReset(thermit_t *inst)
{
  thermitPrv_t *prv = (thermitPrv_t *)inst;
  int ret = -1;

  if (prv)
  {
    DEBUG_INFO(prv, "thermit->reset()\r\n");
  }

  return ret;
}
