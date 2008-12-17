#ifndef __DEBUG_DOT_H__
#define __DEBUG_DOT_H__

#define CCSENTER(x) logt_print(LOG_DEBUG, "Entering " x "\n")
#define CCSEXIT(x) logt_print(LOG_DEBUG, "Exiting " x "\n")

extern int debug;

#endif /* __DEBUG_DOT_H__ */
