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


thermit_t* thermitNew(uint8_t *linkName)
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
      DEBUG_PRINT("created instance using '%s'.\r\n", linkName);

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





static thermitState_t mStep(thermit_t *inst)
{
  thermitPrv_t *prv = (thermitPrv_t*)inst;
  thermitState_t ret;

  if(prv)
  {
    DEBUG_PRINT("thermit->step()\r\n");

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

