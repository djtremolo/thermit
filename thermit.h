#ifndef __THERMIT_H__
#define __THERMIT_H__

#include <stdio.h>

#if 1
#include <stdint.h>
#include <stdbool.h>
#else
typedef signed short int16_t;
typedef unsigned short unt16_t;
typedef signed long int32_t;
typedef unsigned long uint32_t;
typedef signed char int8_t;
typedef unsigned char uint8_t;
#endif


typedef struct
{
    int fd;
    FILE *inFile;
    FILE *outFile;

} thermit_t;




#endif //__THERMIT_H__