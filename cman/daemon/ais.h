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

extern uint64_t incarnation;

extern int comms_init_ais(unsigned short port, char *key_filename);
extern int ais_set_mcast(char *mcast);
extern int ais_add_ifaddr(char *ifaddr);
extern int comms_send_message(void *buf, int len,
			      unsigned char toport, unsigned char fromport,
			      int nodeid,
			      unsigned int flags);
