#include "thermit.h"
#include "string.h" //memset




#define THERMIT_INSTANCES_MAX   1

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

  thermitIoSlot_t comLink;
  thermitIoSlot_t inFile;
  thermitIoSlot_t outFile;

  /*buffering. This buffer will be used for both incoming and outgoing messages*/
  uint8_t mBuf[THERMIT_MSG_SIZE_MAX];
  int16_t mLen;

  bool isHost;

  thermitParameters_t parameters;
  thermitDiagnostics_t diagnostics;
} thermitPrv_t;




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
  mSetFileWriteCb  
};



#define THERMIT_RESERVED_MAGIC_VALUE 0xA55B

static thermitPrv_t thermitInstances[THERMIT_INSTANCES_MAX];

static thermitPrv_t* reserveInstance()
{
  thermitPrv_t *inst = NULL;
  int i;

  for(i=0; i<THERMIT_INSTANCES_MAX; i++)
  {
    if(thermitInstances[i].reserved != THERMIT_RESERVED_MAGIC_VALUE)
    {
      inst = &(thermitInstances[i]);

      memset(inst, 0, sizeof(thermitPrv_t));
      thermitInstances[i].reserved = THERMIT_RESERVED_MAGIC_VALUE;      

      break;  /*found!*/
    }
  }
  return inst;
}

static void releaseInstance(thermitPrv_t* prv)
{
  if(prv && (prv->reserved == THERMIT_RESERVED_MAGIC_VALUE))
  {    
    memset(prv, 0, sizeof(thermitPrv_t));
  }
}


thermit_t* thermitNew(uint8_t *linkName, bool isHost)
{
  thermitPrv_t *prv = NULL;

  DEBUG_PRINT("thermitNew()\r\n");

  if(linkName)
  {
    thermitPrv_t *p = reserveInstance();

    if(p)
    {
      p->m = &mTable;
      p->comLink = ioDeviceOpen(linkName, 0);
      p->isHost = isHost;

      DEBUG_PRINT("created %s instance using '%s'.\r\n", (isHost?"host":""), linkName);

      prv = p;  /*return this instance as it was successfully created*/
    }
  }
  else
  {
    DEBUG_PRINT("incorrect linkName - FAILED.\r\n");
  }

  return (thermit_t*)prv;
}



void thermitDelete(thermit_t *inst)
{
  thermitPrv_t *prv = (thermitPrv_t*)inst;

  DEBUG_PRINT("thermitDelete()\r\n");

  if(prv)
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

  if(prefix)
  {
    DEBUG_PRINT("%s ", prefix);
  }

  DEBUG_PRINT("FC:%02X RFId:%02X Feedback:%02X SFId:%02X Chunk:%02X DataLen:%02X(%d) [", buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[5]);
  
  for(i = 0; i < pLen; i++)
  {
    DEBUG_PRINT("%02X%s", buf[6+i], (i==(pLen-1)?"":" "));
  }

  DEBUG_PRINT("] CRC:%04X\r\n", (((uint16_t)buf[6+pLen])<<8)|((uint16_t)buf[6+pLen+1]));
}
#else
static void debugDumpFrame(uint8_t *buf, uint8_t *prefix)
{
  (void)buf;
  (void)len;
  (void)prefix;
}
#endif


static int16_t processFrame(thermitPrv_t *prv)
{
  int16_t ret = -1;

  if(prv)
  {
    ret = 0;  /*at least, incoming parameter is valid*/

    if(prv->mLen > 0)
    {
      /*message was received*/
      debugDumpFrame(prv->mBuf, "Processing frame\r\n->");

      /*prepare outgoing message*/
      prv->mLen = 0;

      prv->mBuf[prv->mLen++] = prv->isHost?0xEE:0xDD; //for recognization
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

      ret = prv->mLen;
    }
  }

  return ret;
}



static thermitState_t mStep(thermit_t *inst)
{
  thermitPrv_t *prv = (thermitPrv_t*)inst;
  thermitState_t ret;
  int16_t processReturn;

  if(prv)
  {
    DEBUG_PRINT("thermit->step()\r\n");

    /*check communication device for incoming messages*/
    prv->mLen = ioDeviceRead(prv->comLink, prv->mBuf, THERMIT_MSG_SIZE_MAX);

    /*call processing function. It will overwrite mBuf/mLen with a response message if
    an outgoing message is needed.*/
    processReturn = processFrame(prv);

    /*if a response was prepared, send outgoing message*/
    if(processReturn > 0)
    {
      ioDeviceWrite(prv->comLink, prv->mBuf, prv->mLen);
    }

    /*if nothing to send, send keepalive periodically*/
    //TBD
  }

  return ret;
}

static void mProgress(thermit_t *inst, thermitProgress_t *progress)
{
  thermitPrv_t *prv = (thermitPrv_t*)inst;

  if(prv)
  {
    DEBUG_PRINT("thermit->progress()\r\n");
    
  }
}

static int16_t mFeed(thermit_t *inst, uint8_t *rxBuf, int16_t rxLen)
{
  thermitPrv_t *prv = (thermitPrv_t*)inst;
  int16_t ret = -1;

  if(prv)
  {
    DEBUG_PRINT("thermit->feed()\r\n");
    
  }

  return ret;
}

static int mReset(thermit_t *inst)
{
  thermitPrv_t *prv = (thermitPrv_t*)inst;
  int ret = -1;

  if(prv)
  {
    DEBUG_PRINT("thermit->reset()\r\n");
    
  }

  return ret;
}

static int mSetDeviceOpenCb(thermit_t *inst, cbDeviceOpen_t cb)
{
  thermitPrv_t *prv = (thermitPrv_t*)inst;
  int ret = -1;

  if(prv)
  {
    DEBUG_PRINT("thermit->setDeviceOpenCb()\r\n");
    
  }

  return ret;
}

static int mSetDeviceCloseCb(thermit_t *inst, cbDeviceClose_t cb)
{
  thermitPrv_t *prv = (thermitPrv_t*)inst;
  int ret = -1;

  if(prv)
  {
    DEBUG_PRINT("thermit->setDeviceCloseCb()\r\n");
    
  }

  return ret;
}


static int mSetDeviceReadCb(thermit_t *inst, cbDeviceRead_t cb)
{
  thermitPrv_t *prv = (thermitPrv_t*)inst;
  int ret = -1;

  if(prv)
  {
    DEBUG_PRINT("thermit->setDeviceReadCb()\r\n");
    
  }

  return ret;
}

static int mSetDeviceWriteCb(thermit_t *inst, cbDeviceWrite_t cb)
{
  thermitPrv_t *prv = (thermitPrv_t*)inst;
  int ret = -1;

  if(prv)
  {
    DEBUG_PRINT("thermit->setDeviceWriteCb()\r\n");
    
  }

  return ret;
}

static int mSetFileOpenCb(thermit_t *inst, cbFileOpen_t cb)
{
  thermitPrv_t *prv = (thermitPrv_t*)inst;
  int ret = -1;

  if(prv)
  {
    DEBUG_PRINT("thermit->setFileOpenCb()\r\n");
    
  }

  return ret;
}

static int mSetFileCloseCb(thermit_t *inst, cbFileClose_t cb)
{
  thermitPrv_t *prv = (thermitPrv_t*)inst;
  int ret = -1;

  if(prv)
  {
    DEBUG_PRINT("thermit->setFileCloseCb()\r\n");
    
  }

  return ret;
}

static int mSetFileReadCb(thermit_t *inst, cbFileRead_t cb)
{
  thermitPrv_t *prv = (thermitPrv_t*)inst;
  int ret = -1;

  if(prv)
  {
    DEBUG_PRINT("thermit->setFileReadCb()\r\n");
    
  }

  return ret;
}

static int mSetFileWriteCb(thermit_t *inst, cbFileWrite_t cb)
{
  thermitPrv_t *prv = (thermitPrv_t*)inst;
  int ret = -1;

  if(prv)
  {
    DEBUG_PRINT("thermit->setFileWriteCb()\r\n");
    
  }

  return ret;
}

