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

#include "gio_wiretypes.h"

char *
gio_Err_to_str (int x)
{
	char *t = "Unknown GULM Err";
	switch (x) {
	case gio_Err_Ok:
		t = "Ok";
		break;

	case gio_Err_BadLogin:
		t = "Bad Login";
		break;
	case gio_Err_BadCluster:
		t = "Bad Cluster ID";
		break;
	case gio_Err_BadConfig:
		t = "Incompatible configurations";
		break;
	case gio_Err_BadGeneration:
		t = "Bad Generation ID";
		break;
	case gio_Err_BadWireProto:
		t = "Bad Wire Protocol Version";
		break;

	case gio_Err_NotAllowed:
		t = "Not Allowed";
		break;
	case gio_Err_Unknown_Cs:
		t = "Uknown Client";
		break;
	case gio_Err_BadStateChg:
		t = "Bad State Change";
		break;
	case gio_Err_MemoryIssues:
		t = "Memory Problems";
		break;

	case gio_Err_TryFailed:
		t = "Try Failed";
		break;
	case gio_Err_AlreadyPend:
		t = "Request Already Pending";
		break;
	case gio_Err_Canceled:
		t = "Request Canceled";
		break;
	}
	return t;
}

