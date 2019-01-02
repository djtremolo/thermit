#ifndef __THERMITDEBUG_H__
#define __THERMITDEBUG_H__


#define THERMIT_DBG_LVL_INFO        4
#define THERMIT_DBG_LVL_WARN        3
#define THERMIT_DBG_LVL_ERR         2
#define THERMIT_DBG_LVL_FATAL       1
#define THERMIT_DBG_LVL_NONE        0


#ifdef THERMIT_NO_DEBUG
#define THERMIT_DEBUG               THERMIT_DBG_LVL_NONE
#else
#define THERMIT_DEBUG               THERMIT_DBG_LVL_INFO
#endif


#define TGT_PRINTF(prv) ((prv)->targetIf.sysPrintf)


#if THERMIT_DEBUG >= THERMIT_DBG_LVL_FATAL
#include <stdio.h>
#define DEBUG_FATAL(prv, ...) (TGT_PRINTF(prv)(__VA_ARGS__));
#else
#define DEBUG_FATAL(prv, ...)
#endif

#if THERMIT_DEBUG >= THERMIT_DBG_LVL_ERR
#define DEBUG_ERR(prv, ...) (TGT_PRINTF(prv)(__VA_ARGS__));
#else
#define DEBUG_ERR(prv, ...)
#endif

#if THERMIT_DEBUG >= THERMIT_DBG_LVL_WARN
#define DEBUG_WARN(prv, ...) (TGT_PRINTF(prv)(__VA_ARGS__));
#else
#define DEBUG_WARN(prv, ...)
#endif

#if THERMIT_DEBUG >= THERMIT_DBG_LVL_INFO
#define DEBUG_INFO(prv, ...) (TGT_PRINTF(prv)(__VA_ARGS__));
#else
#define DEBUG_INFO(prv, ...)
#endif

#endif //__THERMITDEBUG_H__