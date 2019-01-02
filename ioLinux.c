#include <stdio.h>
#include <stdbool.h>
#include <stdarg.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include "crc.h"
#include "thermit.h"
#include "streamFraming.h"

#define IOLINUX_DEVICES_MAX 1
#define IOLINUX_FILES_MAX 1



static uint32_t millis(uint32_t *max);
static thermitIoSlot_t ioDeviceOpen(uint8_t *devName, thermitIoMode_t mode);
static int ioDeviceClose(thermitIoSlot_t slot);
static int ioDeviceRead(thermitIoSlot_t slot, uint8_t *buf, int16_t maxLen);
static int ioDeviceWrite(thermitIoSlot_t slot, uint8_t *buf, int16_t len);
static thermitIoSlot_t ioFileOpen(uint8_t *fileName, thermitIoMode_t mode);
static int ioFileRead(thermitIoSlot_t slot, uint16_t offset, uint8_t *buf, int16_t maxLen);
static int ioFileWrite(thermitIoSlot_t slot, uint16_t offset, uint8_t *buf, int16_t len);
static int ioFileClose(thermitIoSlot_t slot);
static int dbgPrintf(const char *restrict format, ...);


thermitTargetAdaptationInterface_t ioLinuxTargetIf = 
{
  ioDeviceOpen,/*devOpen*/ 
  ioDeviceClose,/*devClose*/    
  ioDeviceRead,/*devRead*/ 
  ioDeviceWrite,/*devWrite*/    
  ioFileOpen,/*fileOpen*/    
  ioFileClose,/*fileClose*/   
  ioFileRead,/*fileRead*/    
  ioFileWrite,/*fileWrite*/   
  millis,/*sysGetMs*/    
  dbgPrintf,/*sysPrintf*/
  crc16/*sysCrc16*/
};



typedef struct
{
  bool active;
  int handle;
} ioDeviceObject_t;

typedef struct
{
  bool active;
  FILE *handle;
} ioFileObject_t;

static ioDeviceObject_t communicationDevices[IOLINUX_DEVICES_MAX];
static ioFileObject_t storageFiles[IOLINUX_FILES_MAX];


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
    uint32_t ret = 0;
    struct timespec tp;

    if(max)
        *max = 0xFFFFFFFF;

    if(clock_gettime(CLOCK_MONOTONIC, &tp) == 0)
    {
        ret = (tp.tv_nsec / 1000000) + (tp.tv_sec * 1000);
    }
    return ret;
}

static void initDevices()
{
  static bool initialized = false;
  int i;

  for (i = 0; i < IOLINUX_DEVICES_MAX; i++)
  {
    communicationDevices[i].active = false;
  }
  initialized = true;
}

static void initFiles()
{
  static bool initialized = false;
  int i;

  for (i = 0; i < IOLINUX_FILES_MAX; i++)
  {
    storageFiles[i].active = false;
  }
  initialized = true;
}

static thermitIoSlot_t reserveDevice()
{
  thermitIoSlot_t i;
  for (i = 0; i < IOLINUX_DEVICES_MAX; i++)
  {
    if (communicationDevices[i].active == false)
    {
      /*reserve*/
      communicationDevices[i].active = true;
      return i; /*found a free slot*/
    }
  }

  return -1;
}

static int reserveFile()
{
  thermitIoSlot_t i;
  for (i = 0; i < IOLINUX_FILES_MAX; i++)
  {
    if (storageFiles[i].active == false)
    {
      /*reserve*/
      storageFiles[i].active = true;
      return i; /*found a free slot*/
    }
  }

  return -1;
}

static bool deviceSlotIsValid(thermitIoSlot_t slot)
{
  if ((slot >= 0) && (slot < IOLINUX_FILES_MAX))
  {
    return true;
  }

  return false;
}
static bool fileSlotIsValid(thermitIoSlot_t slot)
{
  if ((slot >= 0) && (slot < IOLINUX_FILES_MAX))
  {
    return true;
  }

  return false;
}

static void releaseDevice(thermitIoSlot_t slot)
{
  if (deviceSlotIsValid(slot))
  {
    communicationDevices[slot].active = false;
  }
}

static void releaseFile(thermitIoSlot_t slot)
{
  if (fileSlotIsValid(slot))
  {
    storageFiles[slot].active = false;
  }
}

#ifndef THERMIT_NO_DEBUG
#define error_message(...) printf(__VA_ARGS__)
#else
#define error_message(...)
#endif
/*
the set_interface_attribs and set_blocking functions are copied from:
https://stackoverflow.com/questions/6947413/how-to-open-read-and-write-from-serial-port-in-c
Author: https://stackoverflow.com/users/198536/wallyk
*/
static int set_interface_attribs(int fd, int speed, int parity)
{
  struct termios tty;
  memset(&tty, 0, sizeof tty);
  if (tcgetattr(fd, &tty) != 0)
  {
    error_message("error %d from tcgetattr", errno);
    return -1;
  }

  cfsetospeed(&tty, speed);
  cfsetispeed(&tty, speed);

  tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8; // 8-bit chars
  // disable IGNBRK for mismatched speed tests; otherwise receive break
  // as \000 chars
  tty.c_iflag &= ~IGNBRK; // disable break processing
  tty.c_lflag = 0;        // no signaling chars, no echo,
                          // no canonical processing
  tty.c_oflag = 0;        // no remapping, no delays
  tty.c_cc[VMIN] = 0;     // read doesn't block
  tty.c_cc[VTIME] = 5;    // 0.5 seconds read timeout

  tty.c_iflag &= ~(IXON | IXOFF | IXANY); // shut off xon/xoff ctrl

  tty.c_cflag |= (CLOCAL | CREAD);   // ignore modem controls,
                                     // enable reading
  tty.c_cflag &= ~(PARENB | PARODD); // shut off parity
  tty.c_cflag |= parity;
  tty.c_cflag &= ~CSTOPB;
  //tty.c_cflag &= ~CRTSCTS;

  if (tcsetattr(fd, TCSANOW, &tty) != 0)
  {
    error_message("error %d from tcsetattr", errno);
    return -1;
  }
  return 0;
}

static void set_blocking(int fd, int should_block)
{
  struct termios tty;
  memset(&tty, 0, sizeof tty);
  if (tcgetattr(fd, &tty) != 0)
  {
    error_message("error %d from tggetattr", errno);
    return;
  }

  tty.c_cc[VMIN] = should_block ? 1 : 0;
  tty.c_cc[VTIME] = 5; // 0.5 seconds read timeout

  if (tcsetattr(fd, TCSANOW, &tty) != 0)
    error_message("error %d setting term attributes", errno);
}

static thermitIoSlot_t ioDeviceOpen(uint8_t *devName, thermitIoMode_t mode)
{
  thermitIoSlot_t ret = -1;
  int slot;

  (void)mode;

  dbgPrintf("ioDeviceOpen()\r\n");

  /*call initialization for communication devices. It is safe to call it anytime. It will 
    initialize only at first call and jump out when already initialized.*/
  initDevices();

  slot = reserveDevice();

  if (deviceSlotIsValid(slot))
  {
    if (devName != NULL)
    {
      int fd = open(devName, O_RDWR | O_NOCTTY | O_SYNC);
      if (fd >= 0)
      {
        set_interface_attribs(fd, B38400, 0); // set speed to 115,200 bps, 8n1 (no parity)
        set_blocking(fd, 0);                  // set no blocking

        communicationDevices[slot].handle = fd;

        dbgPrintf("device '%s' opened\r\n", devName);

        ret = slot;
      }
    }
  }

  return ret;
}

static int ioDeviceClose(thermitIoSlot_t slot)
{
  int ret = -1;

  dbgPrintf("ioDeviceClose()\r\n");

  if (deviceSlotIsValid(slot))
  {
    close(slot);
    releaseDevice(slot);

    dbgPrintf("device closed\r\n");
    ret = 0;
  }

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
  int16_t ret = -1;
  uint8_t tmpByte;
  uint8_t tmpBuf[128];
  static streamFraming_t frameStorage[IOLINUX_DEVICES_MAX];
  static bool initialized = false;

  /*first call to ioDeviceRead initializes the frames for each instance*/
  if (!initialized)
  {
    int i;
    for(i=0; i<IOLINUX_DEVICES_MAX; i++)
    {
      streamFramingInitialize(&frameStorage[i]);
    }
    initialized = true;
  }

  if (deviceSlotIsValid(slot))
  {
    int fd = communicationDevices[slot].handle;
    streamFraming_t *frame = &(frameStorage[slot]);

    /*the serial port should be fine*/
    ret = 0;

    /*check if something is coming in and collect all received bytes until a frame end is found*/
    while ((frame->isReady == false) && (read(fd, &tmpByte, sizeof(tmpByte)) == 1))
    {
      streamFramingFollow(frame, tmpByte);

      if (frame->isReady && (frame->len <= maxLen))
      {
        memcpy(buf, frame->buf, frame->len);
        ret = (int16_t)frame->len;

        streamFramingInitialize(frame);

        break; /*done. jump out of the loop*/
      }
    }
  }

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
  int ret = -1;
  uint8_t startSequence[2] = {START_CHAR, START_CHAR};
  uint8_t stopSequence[2] = {STOP_CHAR, STOP_CHAR};

  if (deviceSlotIsValid(slot))
  {
    int fd = communicationDevices[slot].handle;

    if (write(fd, startSequence, sizeof(startSequence)) == sizeof(startSequence))
    {
      int max = 10; /*used to stop the loop if sending fails*/

      while (len > 0)
      {
        int sentBytes = write(fd, buf, len);

        if (sentBytes < 0 || --max < 1) /* Errors are fatal */
        {
          return (-1);
        }
        len -= sentBytes;
        buf += sentBytes;
      }

      if (write(fd, stopSequence, sizeof(stopSequence)) == sizeof(stopSequence))
      {
        ret = 0;
      }
    }
  }
  return ret;
}


/*  open file  */
/*
  Call with:
    fileName  - Pointer to filename.
    mode - 1 = read, 2 = write
  Returns:
    0 on success.
    -1 on failure    
*/
static thermitIoSlot_t ioFileOpen(uint8_t *fileName, thermitIoMode_t mode)
{
  thermitIoSlot_t ret = -1;
  int slot;

  /*call initialization for files. It is safe to call it anytime. It will 
    initialize only at first call and jump out when already initialized.*/
  initFiles();

  slot = reserveFile();

  if (fileSlotIsValid(slot))
  {
    FILE *f;
    switch (mode)
    {
      case 1: /* Read */
        if (f = fopen(fileName, "rb"))
        {
          ret = 0;
        }
        break;

      case 2: /* Write (create) */
        if (f = fopen(fileName, "wb"))
        {
          ret = 0;
        }
        break;

      default:
        break;
    }

    /*check success*/
    if(ret == 0)
    {
      storageFiles[slot].handle = f;
    }    
    else
    {
      releaseFile(slot);
    }
    
  }

  return ret;
}

static int ioFileRead(thermitIoSlot_t slot, uint16_t offset, uint8_t *buf, int16_t maxLen)
{
  int ret = -1;

  if (fileSlotIsValid(slot))
  {
    FILE *f = storageFiles[slot].handle;

    if (fseek(f, offset, SEEK_SET) >= 0)
    {
      if (fread(buf, 1, maxLen, f) == maxLen)
      {
        ret = 0;
      }
    }
  }

  return ret;
}

static int ioFileWrite(thermitIoSlot_t slot, uint16_t offset, uint8_t *buf, int16_t len)
{
  int ret = -1;

  if (fileSlotIsValid(slot))
  {
    FILE *f = storageFiles[slot].handle;
    if (fseek(f, offset, SEEK_SET) >= 0)
    {
      if (fwrite(buf, 1, len, f) == len)
      {
        ret = 0;
      }
    }
  }

  return ret;
}

static int ioFileClose(thermitIoSlot_t slot)
{
  int ret = -1;

  if (fileSlotIsValid(slot))
  {
    fclose(storageFiles[slot].handle);
    releaseFile(slot);
    ret = 0;
  }

  return ret;
}
