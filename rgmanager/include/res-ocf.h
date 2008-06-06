#ifndef _RES_OCF_H
#define _RES_OCF_H 1

#define OCF_ROOT RESOURCE_ROOTDIR

#define OCF_API_VERSION "1.0"

#define OCF_RES_PREFIX "OCF_RESKEY_"

#define OCF_ROOT_STR "OCF_ROOT"
#define OCF_RA_VERSION_MAJOR_STR "OCF_RA_VERSION_MAJOR"
#define OCF_RA_VERSION_MINOR_STR "OCF_RA_VERSION_MINOR"
#define OCF_RESOURCE_INSTANCE_STR "OCF_RESOURCE_INSTANCE"
#define OCF_CHECK_LEVEL_STR "OCF_CHECK_LEVEL"
#define OCF_RESOURCE_TYPE_STR "OCF_RESOURCE_TYPE"
#define OCF_REFCNT_STR "OCF_RESKEY_RGMANAGER_meta_refcnt"

/*
   LSB return codes 
 */
#define OCF_RA_SUCCESS		0
#define OCF_RA_ERROR		1
#define OCF_RA_INVALID_ARG	2
#define OCF_RA_UNIMPLEMENTED	3
#define OCF_RA_PERMISSION	4
#define OCF_RA_NOT_INSTALLED	5
#define OCF_RA_NOT_CONFIGURED	6
#define OCF_RA_NOT_RUNNING	7
#define OCF_RA_MAX		7

/*
   Resource operations - not ocf-specified
 */
#define RS_START	(0)
#define RS_STOP		(1)
#define RS_STATUS	(2)
#define RS_RESINFO	(3)
#define RS_RESTART	(4)
#define RS_RELOAD	(5)
#define RS_CONDRESTART  (6)
#define	RS_RECOVER	(7)
#define RS_CONDSTART	(8)	/** Start if flagged with RF_NEEDSTART */
#define RS_CONDSTOP	(9)	/** STOP if flagged with RF_NEEDSTOP */
#define RS_MONITOR	(10)
#define RS_META_DATA	(11)
#define RS_VALIDATE	(12)
#define RS_MIGRATE	(13)
#define RS_RECONFIG	(14)

#endif
