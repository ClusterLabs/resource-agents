/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**  
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/
#ifndef __LOG_H__
#define __LOG_H__

#include <syslog.h>

extern int log_is_open;
extern int log_is_verbose;

void log_set_verbose(void);
void log_open(const char *ident, int option, int facility);
void log_close(void);

/* Note, messages will always be sent to syslog, but userland programs **
** will have to close stdout, stderr if they don't want messages       **
** printed to the controling terminal (as all good daemon's should).   */

#ifdef DEBUG
#define log_dbg(fmt, args...) { \
    if(log_is_open){ \
      syslog(LOG_DEBUG, "[%s:%d] " fmt , __FILE__ , __LINE__ , ## args ); \
    }else { \
      fprintf(stdout, "[%s:%d] " fmt , __FILE__ , __LINE__ , ## args ); \
    } \
}
#else
#define log_dbg(fmt, args...)
#endif

#ifdef DEBUG
#define log_msg(fmt, args...)  {\
    if(log_is_open){ \
      syslog(LOG_NOTICE, fmt, ## args); \
    }else { \
      fprintf(stdout, fmt , ## args ); \
    } \
}
#else
#define log_msg(fmt, args...)  {\
    if(log_is_verbose){ \
      if(log_is_open){ \
        syslog(LOG_NOTICE, fmt, ## args); \
      }else { \
        fprintf(stdout, fmt , ## args ); \
      } \
    } \
}
#endif

#ifdef DEBUG
#define log_msg_always(fmt, args...){ \
    if(log_is_open){ \
      syslog(LOG_NOTICE, "[%s:%d] " fmt , __FILE__ , __LINE__ , ## args ); \
    }else { \
      fprintf(stdout, "[%s:%d] ", __FILE__ , __LINE__); \
      fprintf(stdout, fmt, ## args ); \
    } \
}
#else
#define log_msg_always(fmt, args...){ \
    if(log_is_open){ \
      syslog(LOG_NOTICE, fmt, ## args ); \
    }else { \
      fprintf(stdout, fmt, ## args ); \
    } \
}
#endif


#ifdef DEBUG
#define log_err(fmt, args...){ \
    if(log_is_open){ \
      syslog(LOG_ERR, "[%s:%d] " fmt , __FILE__ , __LINE__ , ## args ); \
    }else { \
      fprintf(stderr, "[%s:%d] ", __FILE__ , __LINE__); \
      fprintf(stderr, fmt, ## args ); \
    } \
}
#else
#define log_err(fmt, args...){ \
    if(log_is_open){ \
      syslog(LOG_ERR, fmt, ## args ); \
    }else { \
      fprintf(stderr, fmt, ## args ); \
    } \
}
#endif


#ifdef DEBUG
#define log_sys_err(fmt, args...){ \
    if(log_is_open){ \
      syslog(LOG_ERR, "[%s:%d] " fmt ": %m\n" , __FILE__ , __LINE__ , ## args ); \
    }else { \
      fprintf(stderr, "[%s:%d] ", __FILE__ , __LINE__); \
      fprintf(stderr, fmt ": " , ## args ); \
      perror(NULL); \
    } \
}
#else
#define log_sys_err(fmt, args...){ \
    if(log_is_open){ \
      syslog(LOG_ERR, fmt ": %m\n" , ## args ); \
    }else { \
      fprintf(stderr, fmt ": " , ## args ); \
      perror(NULL); \
    } \
}
#endif

#define die(ext, fmt, args...) { \
    if(log_is_open){ \
      syslog(LOG_ERR, "In %s, at %d (%s) death by:\n" fmt , __FILE__ , \
            __LINE__ , CCS_RELEASE_NAME , ## args ); exit(ext); \
    }else { \
      fprintf(stderr, "In %s, at %d (%s) death by:\n" fmt , __FILE__ , \
            __LINE__ , CCS_RELEASE_NAME , ## args ); exit(ext); \
    } \
}


#endif /* __LOG_H__ */
