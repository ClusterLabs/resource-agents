/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2005 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include <strings.h>

#include "dlm_tool.h"
#include "libdlm.h"

extern char *action;


int make_mode(char *p, int *mode)
{
	if (!strncasecmp(p, "NL", 2))
		*mode = LKM_NLMODE;
	else if (!strncasecmp(p, "CR", 2))
		*mode = LKM_CRMODE;
	else if (!strncasecmp(p, "CW", 2))
		*mode = LKM_CWMODE;
	else if (!strncasecmp(p, "PR", 2))
		*mode = LKM_PRMODE;
	else if (!strncasecmp(p, "PW", 2))
		*mode = LKM_PWMODE;
	else if (!strncasecmp(p, "EX", 2))
		*mode = LKM_EXMODE;
	else
		return -1;
	return 0;
}

int get_flag(char *p, uint32_t *flag)
{
	if (!strcasecmp(p, "NOQUEUE"))
		*flag = LKF_NOQUEUE;
	else if (!strcasecmp(p, "CANCEL"))
		*flag = LKF_CANCEL;
	else if (!strcasecmp(p, "QUECVT"))
		*flag = LKF_QUECVT;
	else if (!strcasecmp(p, "CONVDEADLK"))
		*flag = LKF_CONVDEADLK;
	else if (!strcasecmp(p, "PERSISTENT"))
		*flag = LKF_PERSISTENT;
	else if (!strcasecmp(p, "EXPEDITE"))
		*flag = LKF_EXPEDITE;
	else if (!strcasecmp(p, "NOQUEUEBAST"))
		*flag = LKF_NOQUEUEBAST;
	else if (!strcasecmp(p, "HEADQUE"))
		*flag = LKF_HEADQUE;
	else if (!strcasecmp(p, "NOORDER"))
		*flag = LKF_NOORDER;
	/*
	else if (!strcasecmp(p, "ORPHAN"))
		*flag = LKF_ORPHAN;
	else if (!strcasecmp(p, "ALTPR"))
		*flag = LKF_ALTPR;
	else if (!strcasecmp(p, "ALTCW"))
		*flag = LKF_ALTCW;
	*/
	else
		return -1;
	return 0;
}

int make_flags(char *p, uint32_t *flags)
{
	char fstr[32];
	char *t;
	uint32_t flag, out = 0;
	int i, error;

	if (!p) 
		goto ret;

	for (i = 0; ; i++) {
		t = strsep(&p, ",");
		if (!t)
			break;

		if (sscanf(t, "%s", fstr) != 1)
			break;

		error = get_flag(fstr, &flag);
		if (error < 0) {
			log_error("unknown flag %s", fstr);
			return error;
		}

		out |= flag;
	}
 ret:
	*flags = out;
	return 0;
}

int ls_create(int argc, char **argv)
{
	dlm_lshandle_t *ls;

	if (argc != 1)
		die("%s invalid arguments", action);

	ls = dlm_create_lockspace(argv[0], 0600);
	if (!ls) {
		log_error("unable to create lockspace");
		return -1;
	}
	return 0;
}

int ls_release(int argc, char **argv)
{
	dlm_lshandle_t *ls;
	char *name = argv[0];

	if (argc != 1)
		die("%s invalid arguments", action);

	ls = dlm_open_lockspace(name);
	if (!ls) {
		log_error("lockspace %s not found", name);
		return -1;
	}
	dlm_close_lockspace(ls);
	dlm_release_lockspace(name, ls, 1);
	return 0;
}

int ls_lock(int argc, char **argv)
{
	dlm_lshandle_t *ls;
	struct dlm_lksb lksb;
	uint32_t flags;
	int mode, error;
	char *name, *res, *mode_str, *flags_str = NULL;

	if (argc < 3)
		die("%s invalid arguments", action);
	if (argc > 3)
		flags_str = argv[3];
	name = argv[0];
	res = argv[1];
	mode_str = argv[2];

	ls = dlm_open_lockspace(name);
	if (!ls) {
		log_error("lockspace %s not found", name);
		return -1;
	}

	error = make_mode(mode_str, &mode);
	if (error < 0) {
		log_error("invalid mode %s", mode_str);
		goto out;
	}

	error = make_flags(flags_str, &flags);
	if (error < 0)
		goto out;

	printf("lock: mode 0x%x flags 0x%x name %s\n", mode, flags, res);

	memset(&lksb, 0, sizeof(lksb));

	error = dlm_ls_lock_wait(ls, mode, &lksb, flags, res, strlen(res),
				 0, NULL, NULL, NULL);

	if (error)
		log_error("dlm_ls_lock_wait error %d", error);
	else
		printf("status 0x%x id %u\n", lksb.sb_status, lksb.sb_lkid);
 out:
	dlm_close_lockspace(ls);
	return error;
}

int ls_unlock(int argc, char **argv)
{
	dlm_lshandle_t *ls;
	struct dlm_lksb lksb;
	uint32_t flags, lkid;
	int error;
	char *name, *lkid_str, *flags_str = NULL;

	if (argc < 2)
		die("%s invalid arguments", action);
	if (argc > 2)
		flags_str = argv[2];
	name = argv[0];
	lkid_str = argv[1];

	ls = dlm_open_lockspace(name);
	if (!ls) {
		log_error("lockspace %s not found", name);
		return -1;
	}

	error = make_flags(flags_str, &flags);
	if (error < 0)
		goto out;

	lkid = atoll(lkid_str);

	printf("unlock: lkid %u flags 0x%x\n", lkid, flags);

	memset(&lksb, 0, sizeof(lksb));

	error = dlm_ls_unlock_wait(ls, lkid, flags, &lksb);

	if (error)
		log_error("dlm_ls_unlock_wait error %d", error);
	else
		printf("status 0x%x\n", lksb.sb_status);
 out:
	dlm_close_lockspace(ls);
	return error;
}

int ls_convert(int argc, char **argv)
{
	dlm_lshandle_t *ls;
	struct dlm_lksb lksb;
	uint32_t flags, lkid;
	int error, mode;
	char *name, *lkid_str, *mode_str, *flags_str = NULL;

	if (argc < 3)
		die("%s invalid arguments", action);
	if (argc > 3)
		flags_str = argv[3];
	name = argv[0];
	lkid_str = argv[1];
	mode_str = argv[2];

	ls = dlm_open_lockspace(name);
	if (!ls) {
		log_error("lockspace %s not found", name);
		return -1;
	}

	error = make_flags(flags_str, &flags);
	if (error < 0)
		goto out;

	error = make_mode(mode_str, &mode);
	if (error < 0) {
		log_error("invalid mode %s", mode_str);
		goto out;
	}

	lkid = atoll(lkid_str);

	printf("convert: lkid %u flags 0x%x\n", mode, flags);

	memset(&lksb, 0, sizeof(lksb));

	error = dlm_ls_lock_wait(ls, mode, &lksb, flags | LKF_CONVERT, NULL, 0,
				 0, NULL, NULL, NULL);

	if (error)
		log_error("dlm_ls_lock_wait error %d", error);
	else
		printf("status 0x%x\n", lksb.sb_status);
 out:
	dlm_close_lockspace(ls);
	return error;
}

