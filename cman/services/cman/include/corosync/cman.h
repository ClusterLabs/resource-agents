#ifndef CMAN_H_DEFINED
#define CMAN_H_DEFINED

// Actually -- we return proper errnos
typedef enum {
        CMAN_OK = 1,
        CMAN_ERR_LIBRARY = 2,
        CMAN_ERR_TIMEOUT = 5,
        CMAN_ERR_TRY_AGAIN = 6,
        CMAN_ERR_INVALID_PARAM = 7,
        CMAN_ERR_NO_MEMORY = 8,
        CMAN_ERR_BAD_HANDLE = 9,
        CMAN_ERR_ACCESS = 11,
        CMAN_ERR_NOT_EXIST = 12,
        CMAN_ERR_EXIST = 14,
        CMAN_ERR_NOT_SUPPORTED = 20,
        CMAN_ERR_SECURITY = 29
} cman_error_t;

typedef unsigned int cman_handle_t;
typedef void (*cman_callback_t)(cman_handle_t handle, void *privdata, int reason, int arg);
typedef void (*cman_datacallback_t)(cman_handle_t handle, void *privdata,
				    char *buf, int len, uint8_t port, int nodeid);

/* Shutdown flags */
#define SHUTDOWN_ANYWAY           1
#define SHUTDOWN_REMOVE           2

typedef enum {CMAN_REASON_PORTCLOSED,
	      CMAN_REASON_STATECHANGE,
              CMAN_REASON_PORTOPENED,
              CMAN_REASON_TRY_SHUTDOWN,
              CMAN_REASON_CONFIG_UPDATE} cman_call_reason_t;





#endif
