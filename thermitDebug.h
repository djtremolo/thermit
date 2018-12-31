#ifndef __THERMITDEBUG_H__
#define __THERMITDEBUG_H__


#define THERMIT_DBG_LVL_INFO        4
#define THERMIT_DBG_LVL_WARN        3
#define THERMIT_DBG_LVL_ERR         2
#define THERMIT_DBG_LVL_FATAL       1
#define THERMIT_DBG_LVL_NONE        0


#define THERMIT_DEBUG               THERMIT_DBG_LVL_INFO




#if THERMIT_DEBUG >= THERMIT_DBG_LVL_FATAL
#include <stdio.h>
#define DEBUG_FATAL(...) printf(__VA_ARGS__);
#else
#define DEBUG_FATAL(...)
#endif

#if THERMIT_DEBUG >= THERMIT_DBG_LVL_ERR
#define DEBUG_ERR(...) printf(__VA_ARGS__);
#else
#define DEBUG_ERR(...)
#endif

#if THERMIT_DEBUG >= THERMIT_DBG_LVL_WARN
#define DEBUG_WARN(...) printf(__VA_ARGS__);
#else
#define DEBUG_WARN(...)
#endif

#if THERMIT_DEBUG >= THERMIT_DBG_LVL_INFO
#define DEBUG_INFO(...) printf(__VA_ARGS__);
#else
#define DEBUG_INFO(...)
#endif

#endif //__THERMITDEBUG_H__