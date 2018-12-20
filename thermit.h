#ifndef __THERMIT_H__
#define __THERMIT_H__
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>


typedef struct
{
    int fd;
    FILE *inFile;
    FILE *outFile;

} thermit_t;

#endif //__THERMIT_H__