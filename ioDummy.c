#include <stdbool.h>
#include <stdarg.h>
#include "crc.h"
#include "thermit.h"

#define IODUMMY_DEVICES_MAX 1
#define IODUMMY_FILES_MAX 1



static uint32_t millis(uint32_t *max);
static thermitIoSlot_t ioDeviceOpen(uint8_t *devName, thermitIoMode_t mode);
static int ioDeviceClose(thermitIoSlot_t slot);
static int ioDeviceRead(thermitIoSlot_t slot, uint8_t *buf, int16_t maxLen);
static int ioDeviceWrite(thermitIoSlot_t slot, uint8_t *buf, int16_t len);
static thermitIoSlot_t ioFileOpen(uint8_t *fileName, thermitIoMode_t mode, uint16_t *fileSize);
static int ioFileRead(thermitIoSlot_t slot, uint16_t offset, uint8_t *buf, int16_t maxLen);
static int ioFileWrite(thermitIoSlot_t slot, uint16_t offset, uint8_t *buf, int16_t len);
static int ioFileClose(thermitIoSlot_t slot);
static bool ioFileAvailableForSending(uint8_t *fileNamePtr, uint16_t *sizePtr);

static int dbgPrintf(const char *restrict format, ...);


thermitTargetAdaptationInterface_t ioDummyTargetIf = 
{
  ioDeviceOpen,/*devOpen*/ 
  ioDeviceClose,/*devClose*/    
  ioDeviceRead,/*devRead*/ 
  ioDeviceWrite,/*devWrite*/    
  ioFileOpen,/*fileOpen*/    
  ioFileClose,/*fileClose*/   
  ioFileRead,/*fileRead*/    
  ioFileWrite,/*fileWrite*/
  ioFileAvailableForSending,/*fileAvailableForSending*/
  millis,/*sysGetMs*/    
  dbgPrintf,/*sysPrintf*/
  crc16/*sysCrc16*/
};


static int dbgPrintf(const char *restrict format, ...)
{
  int ret = 0;
  #ifndef THERMIT_NO_DEBUG
  va_list args;
  va_start(args, format);
  ret = vprintf(format, args);
  va_end(args);  
  #endif
  return ret;
}


static uint32_t millis(uint32_t *max)
{
    static uint32_t ret = 0;

    return ret++;
}

static bool ioFileAvailableForSending(uint8_t *fileNamePtr, uint16_t *sizePtr)
{
  bool ret = false;

  *(fileNamePtr++) = 'f';
  *(fileNamePtr++) = '0';
  *(fileNamePtr++) = 0;

  *sizePtr = 3;

  return ret;
}

static thermitIoSlot_t ioDeviceOpen(uint8_t *devName, thermitIoMode_t mode)
{
  thermitIoSlot_t ret = 0;

  (void)devName;
  (void)mode;

  return ret;
}

static int ioDeviceClose(thermitIoSlot_t slot)
{
  int ret = -1;
  (void)slot;

  return ret;
}

/*  read packet from communication device  */
/*
  Call with:
    inst   - pointer to thermit instance
    buf   - pointer to read buffer
    maxLen - maximum bytes to receive

  Returns the number of bytes read, or:
     0   - timeout or other possibly correctable error;
    -1   - fatal error, such as loss of connection, or no buffer to read into.
*/

static int ioDeviceRead(thermitIoSlot_t slot, uint8_t *buf, int16_t maxLen)
{
  int16_t ret = 0;

  (void)slot;
  (void)buf;
  (void)maxLen;

  return ret;
}

/*  send packet to communication device  */
/*
  Call with:
    inst   - pointer to thermit instance
    buf   - pointer to read buffer
    maxLen - maximum bytes to receive
  Returns:
    0 on success
    -1 on failure
*/
static int ioDeviceWrite(thermitIoSlot_t slot, uint8_t *buf, int16_t len)
{
  int ret = 0;
  return ret;
}


/*  open file  */
/*
  Call with:
    fileName  - Pointer to filename.
    mode      - r/w access
    fileSize - pointer to file size value. For written files, this is the 
                final size of the file. For read files, this returns the 
                file size.
  Returns:
    0 on success.
    -1 on failure    
*/
static thermitIoSlot_t ioFileOpen(uint8_t *fileName, thermitIoMode_t mode, uint16_t *fileSize)
{
  thermitIoSlot_t ret = 0;

  (void)fileName;
  (void)mode;
  *fileSize = 10;

  return ret;
}

static int ioFileRead(thermitIoSlot_t slot, uint16_t offset, uint8_t *buf, int16_t maxLen)
{
  int ret = -1;

  while(maxLen--)
  {
    *(buf++) = 0;
  }

  return ret;
}

static int ioFileWrite(thermitIoSlot_t slot, uint16_t offset, uint8_t *buf, int16_t len)
{
  int ret = 0;

  return ret;
}

static int ioFileClose(thermitIoSlot_t slot)
{
  int ret = 0;

  return ret;
}
