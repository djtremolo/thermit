#include <errno.h>
#include <fcntl.h> 
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "ioAPI.h"

int16_t ioDeviceOpen(thermit_t *inst, uint8_t portIdx)
{
    int ret = -1;

    if(portIdx < 8)
    {
        uint8_t portName[10];
        
        sprintf(portName, "/dev/tnt%d", portIdx);

        int fd = open (portName, O_RDWR | O_NOCTTY | O_SYNC);
        if (fd >= 0)
        {
            //set_interface_attribs(fd, B115200, 0);  // set speed to 115,200 bps, 8n1 (no parity)
            //set_blocking(fd, 0);                // set no blocking

            ret = 0;
            inst->fd = fd;
        }
    }

    return ret;
}

int16_t ioDeviceClose(thermit_t *inst)
{
    int ret = -1;

    if(inst->fd >= 0)
    {
        close(inst->fd);
        inst->fd = -1;
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

int16_t ioDeviceRead(thermit_t *inst, uint8_t *buf, int16_t maxLen) 
{
    int16_t ret = -1;

    int n = read (inst->fd, buf, sizeof(buf));

    if(n > 0)
    {
        ret = n;        
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
int16_t ioDeviceWrite(thermit_t *inst, uint8_t *buf, int16_t len) 
{
    int x;
    int max;

    max = 10;                           /* Loop breaker */

    while (len > 0) {                     /* Keep trying till done */
        int sentBytes = write(inst->fd, buf, len);

        if (sentBytes < 0 || --max < 1)         /* Errors are fatal */
          return(-1);
        len -= sentBytes;
	    buf += sentBytes;
    }
    return(0);                       /* Success */
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
int16_t ioFileOpen(thermit_t *inst, uint8_t *fileName, int mode) 
{
    int16_t ret = -1;

    switch (mode) 
    {
      case 1:				/* Read */
	    if (inst->inFile = fopen(fileName, "rb")) 
        {
            ret = 0;
	    }
        break;

      case 2:				/* Write (create) */
	    if (inst->outFile = fopen(fileName, "wb")) 
        {
            ret = 0;
	    }
        break;

      default:
        break;
    }

    return ret;
}

int16_t ioFileRead(thermit_t *inst, uint16_t offset, uint8_t *buf, int16_t len) 
{
    int16_t ret = -1;

    if(inst->inFile >= 0)
    {
        if(fseek(inst->inFile, offset, SEEK_SET) >= 0)
        {
            if(fread(buf, 1, len, inst->inFile) == len)
            {
                ret = 0;
            }
        }
    }

    return ret;
}

int16_t ioFileWrite(thermit_t *inst, uint16_t offset, uint8_t *buf, int16_t len) 
{
    int16_t ret = -1;

    if(inst->outFile >= 0)
    {
        if(fseek(inst->outFile, offset, SEEK_SET) >= 0)
        {
            if(fwrite(buf, 1, len, inst->outFile) == len)
            {
                ret = 0;
            }
        }
    }

    return ret;
}


int16_t ioFileClose(thermit_t *inst, bool in) 
{
    int ret = -1;
    if(in)
    {
        fclose(inst->inFile);
        inst->inFile = NULL;
        ret = 0;
    }
    else
    {
        fclose(inst->outFile);
        inst->outFile = NULL;
        ret = 0;
    }

    return ret;
}
