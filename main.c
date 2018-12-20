#include "thermit.h"
#include "ioAPI.h"
#include "stdio.h"

int main(int argc, char* argv[]);

static thermit_t tInst;


int main(int argc, char* argv[])
{
    thermit_t *inst = &tInst;

    harhar();

    devopen(inst, 1);   /*first port*/

    for(int i=0;i<100;i++)
    {
        printf("jello\r\n");

    }


    devclose(inst);

    return 0;
}