#include "thermit.h"
#include "ioAPI.h"
#include "stdio.h"

int main(int argc, char* argv[]);

static thermit_t tInst;


int main(int argc, char* argv[])
{
    thermit_t *inst = &tInst;
    uint8_t myBuf[128];

    ioDeviceOpen(inst, 1);   /*first port*/

    for(int i=0;i<100;i++)
    {

        if(ioDeviceRead(inst, myBuf, 128) > 0)
        {
            printf("received\r\n");
        } 
    }


    ioDeviceClose(inst);

    return 0;
}