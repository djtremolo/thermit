#include <errno.h>
#include <fcntl.h> 
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "ioAPI.h"


#include <errno.h>
#include <fcntl.h> 
#include <string.h>
#include <termios.h>
#include <unistd.h>


/*
the set_interface_attribs and set_blocking functions are copied from:
https://stackoverflow.com/questions/6947413/how-to-open-read-and-write-from-serial-port-in-c
Author: https://stackoverflow.com/users/198536/wallyk
*/
int
set_interface_attribs (int fd, int speed, int parity)
{
        struct termios tty;
        memset (&tty, 0, sizeof tty);
        if (tcgetattr (fd, &tty) != 0)
        {
                error_message ("error %d from tcgetattr", errno);
                return -1;
        }

        cfsetospeed (&tty, speed);
        cfsetispeed (&tty, speed);

        tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;     // 8-bit chars
        // disable IGNBRK for mismatched speed tests; otherwise receive break
        // as \000 chars
        tty.c_iflag &= ~IGNBRK;         // disable break processing
        tty.c_lflag = 0;                // no signaling chars, no echo,
                                        // no canonical processing
        tty.c_oflag = 0;                // no remapping, no delays
        tty.c_cc[VMIN]  = 0;            // read doesn't block
        tty.c_cc[VTIME] = 5;            // 0.5 seconds read timeout

        tty.c_iflag &= ~(IXON | IXOFF | IXANY); // shut off xon/xoff ctrl

        tty.c_cflag |= (CLOCAL | CREAD);// ignore modem controls,
                                        // enable reading
        tty.c_cflag &= ~(PARENB | PARODD);      // shut off parity
        tty.c_cflag |= parity;
        tty.c_cflag &= ~CSTOPB;
        tty.c_cflag &= ~CRTSCTS;

        if (tcsetattr (fd, TCSANOW, &tty) != 0)
        {
                error_message ("error %d from tcsetattr", errno);
                return -1;
        }
        return 0;
}

void
set_blocking (int fd, int should_block)
{
        struct termios tty;
        memset (&tty, 0, sizeof tty);
        if (tcgetattr (fd, &tty) != 0)
        {
                error_message ("error %d from tggetattr", errno);
                return;
        }

        tty.c_cc[VMIN]  = should_block ? 1 : 0;
        tty.c_cc[VTIME] = 5;            // 0.5 seconds read timeout

        if (tcsetattr (fd, TCSANOW, &tty) != 0)
                error_message ("error %d setting term attributes", errno);
}




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
