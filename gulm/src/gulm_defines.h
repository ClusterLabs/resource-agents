/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/
/******************************************************************************
 *  "...`gulm' sounds like someone choking on a sardine..."
 */
#ifndef __gulm_defines_h__
#define __gulm_defines_h__

#include <unistd.h>
#include <stdio.h>
#include <execinfo.h>
#include <stdint.h>
#include <inttypes.h>
#include "osi_endian.h" /* because cpu_to_beXX() is damn nice. */

#if !defined(TRUE) || !defined(FALSE)
#undef TRUE
#undef FALSE
#define TRUE  (1)
#define FALSE (0)
#endif

#undef MAX
#define MAX(a,b) ((a>b)?a:b)

#undef MIN
#define MIN(a,b) ((a<b)?a:b)

#define DIV_RU(x, y) (((x) + (y) - 1) / (y))

#define CLUSTERIDLEN        (32)

/* Exit codes.  gulm process will exit with one of the following */
#define ExitGulm_Ok          (0)
#define ExitGulm_TestFlag    (0)
#define ExitGulm_Usage       (0)
#define ExitGulm_ParseFail  (50)
#define ExitGulm_BadOption  (51)
#define ExitGulm_ExecError  (52)
#define ExitGulm_SelfKill   (53)
#define ExitGulm_StopAllReq (54)
#define ExitGulm_LeftLoop   (55)
#define ExitGulm_ShutDown   (56)
#define ExitGulm_PidLock    (57)
#define ExitGulm_InitFailed (58)
#define ExitGulm_NoMemory   (59)
#define ExitGulm_BadLogic   (60)
#define ExitGulm_Assertion  (61)

#include "gulm_log_msg_bits.h"

/* no syslog if debugging. */
#ifndef DEBUG 
#include <syslog.h>
#define log_msg(v, fmt, args...) if(((v)&verbosity)==(v)||(v)==lgm_Always) {\
   syslog(LOG_NOTICE, fmt , ## args );}
#define log_err(fmt, args...){ \
   syslog(LOG_ERR, "ERROR [%s:%d] " fmt , __FILE__ , __LINE__ , ## args ); }
#define log_bug(fmt, args...) { \
   syslog(LOG_NOTICE, "BUG[%s:%d] " fmt , __FILE__ , __LINE__ , ## args ); }
#define die(ext, fmt, args...) { \
   fprintf(stderr, "In %s:%d (%s) death by:\n" fmt , __FILE__ , \
         __LINE__ , RELEASE , ## args ); \
   syslog(LOG_ERR, "In %s:%d (%s) death by:\n" fmt , __FILE__ , \
         __LINE__ , RELEASE , ## args ); exit(ext);}
#define TICK log_bug("TICK.\n")
#else /*DEBUG*/
#define log_msg(v, fmt, args...) if(((v)&verbosity)==(v)||(v)==lgm_Always){ \
   fprintf(stdout, "%s: " fmt , ProgramName , ## args ); }
#define log_err(fmt, args...) {\
   fprintf(stderr, "ERROR [%s:%s:%d] " fmt , ProgramName , __FILE__ , \
         __LINE__ , ## args ); }
#define log_bug(fmt, args...) {\
   fprintf(stderr, "[bug:%s:%s:%d] " fmt , ProgramName , __FILE__ , \
         __LINE__ , ## args ); }
#define die(ext, fmt, args...) {\
   fprintf(stderr, "In %s:%d (%s) death by:\n" fmt , __FILE__ , \
         __LINE__ , RELEASE , ## args ); exit(ext);}
#define TICK log_bug("TICK.\n")
#endif /*DEBUG*/


#define GULMD_ASSERT(x, action) { if( ! (x) ) {\
   void *array[200]; \
   size_t size,i; \
   char **strings; \
   do { action }while(0); \
   size = backtrace(array, 200); \
   strings = backtrace_symbols(array, size); \
   log_msg(lgm_Always, "BACKTRACE\n"); \
   for(i=0;i<size;i++) \
      log_msg(lgm_Always, " %s\n", strings[i]); \
   free(strings); \
         die(ExitGulm_Assertion, "ASSERTION FAILED: %s\n", #x ); } }

#define tvs2uint64(n) ((uint64_t)(n).tv_sec * 1000000 + (n).tv_usec)

#endif /*__gulm_defines_h__*/
/* vim: set ai cin et sw=3 ts=3 : */
