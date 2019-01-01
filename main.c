#include "stdio.h"
#include "thermit.h"
#include <time.h>
#include <unistd.h>
#include "ioLinux.h"



int main(int argc, char* argv[]);

int main(int argc, char* argv[])
{
    uint8_t myBuf[128];
    bool masterRole = false;
    uint8_t *linkName = NULL;

    if(argc == 3)
    {
        linkName = argv[1];
        masterRole = (argv[2][0] == 'm' ? true : false);
    }
    else
    {
        DEBUG_INFO("syntax: %s devname mode, where:\r\ndevname = '/dev/xyz0'\r\nmode = 'm' (master)\r\nmode = 's' (slave)\r\n", argv[0]);
    }

    if(linkName)
    {
        thermit_t *t = thermitNew(linkName, masterRole, &ioLinuxTargetIf);

        if(t)
        {
            volatile bool end = false;

            DEBUG_INFO("instance %p running in %s role.\r\n", t, masterRole?"master":"slave");
            while(!end)
            {
                t->m->step(t);
                sleep(1);
            }

            thermitDelete(t);
        }
    }

    return 0;
}