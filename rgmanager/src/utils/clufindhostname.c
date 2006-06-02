/*
  Copyright Red Hat, Inc. 2002
  Copyright Mission Critical Linux, 2000

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the
  Free Software Foundation; either version 2, or (at your option) any
  later version.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; see the file COPYING.  If not, write to the
  Free Software Foundation, Inc.,  675 Mass Ave, Cambridge, 
  MA 02139, USA.
*/
/** @file
 * Utility/command to return name found by gethostbyname and gethostbyaddr.
 *
 * Author: Richard Rabbat <rabbat@missioncriticallinux.com>
 */
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>

void usage(char* progname)
{
    fprintf (stderr, "Usage: %s [-i ip_addr] [-n ip_name]\n", progname);
}

int main (int argc, char** argv) 
{
    struct hostent*   hp;
    unsigned long int address;
    int		      opt;

    if (argc != 3)
    {
        usage(argv[0]);
        exit(1);
    }

    while ((opt = getopt(argc, argv, "i:n:")) != EOF) {
        switch (opt) {
	case 'i':
	    address = inet_addr (optarg);

	    if ( !( hp = gethostbyaddr ((char*)&address, 4, AF_INET )))
	    {
	        exit (2);
	    }
	    else
	    {
	        fprintf (stdout, "%s\n", hp->h_name);
	        exit (0);
	    }
	    break;
	case 'n':
	    if ( !( hp = gethostbyname (argv[2])))
	    {
	        exit (2);
	    }
	  else
	    {
	        fprintf (stdout, "%s\n", hp->h_name);
	        exit (0);
	    }
	    break;
	default:
	    break;
	}
    }
    exit (0);
}


