#include "stdio.h"
#include "thermit.h"



int main(int argc, char* argv[]);

static thermit_t tInst;


int main(int argc, char* argv[])
{
    thermit_t *inst = &tInst;
    uint8_t myBuf[128];
    int i=0;
    bool masterRole = false;
    uint8_t *linkName = NULL;

    if(argc == 3)
    {
        linkName = argv[1];
        masterRole = (argv[2][0] == 'm' ? true : false);
    }

    if(linkName)
    {
        thermit_t *t = thermitNew(linkName);


        DEBUG_PRINT("instance %p running in %s role.\r\n", t, masterRole?"master":"slave");


        thermitDelete(t);
    }

    return 0;
}