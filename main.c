#include "stdio.h"
#include "thermit.h"
#include <time.h>
#include <unistd.h>


uint32_t millis()
{
    uint32_t ret = 0;
    struct timespec tp;

    if(clock_gettime(CLOCK_MONOTONIC, &tp) == 0)
    {
        ret = (tp.tv_nsec / 1000000) + (tp.tv_sec * 1000);
    }
    return ret;
}


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
        thermit_t *t = thermitNew(linkName, masterRole);

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