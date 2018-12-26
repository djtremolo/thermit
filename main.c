#include "stdio.h"
#include "thermit.h"

#include "ioAPI.h"


int main(int argc, char* argv[]);

static thermit_t tInst;


int main(int argc, char* argv[])
{
    thermit_t *inst = &tInst;
    uint8_t myBuf[128];
    int i=0;
    char role = 0;

    if(argc > 1)
    {
        role = argv[1][0] - '0';
    }

    ioDeviceOpen(inst, role==0?"/dev/tnt1":"/dev/tnt0");


    if(role == 0)
    {
        while(1)
        {
            int16_t recLen = ioDeviceRead(inst, myBuf, 128);
            if(recLen > 0)
            {
                printf("received:");
                for(i=0; i<recLen; i++)
                {
                    printf(" %02X", myBuf[i]);
                }
                printf("\r\n");

                //break;
            } 
        }
    }
    else
    {
        getchar();
        printf("sending\r\n");
        uint8_t tmsg[10] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x02, 0xAA, 0xBB, 0x00, 0x00};
        ioDeviceWrite(inst, tmsg, sizeof(tmsg));
    }


    ioDeviceClose(inst);

    return 0;
}