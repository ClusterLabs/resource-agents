/** @file
 * Header for expect.c.
 *
 * Expect simple tokens.  Simple expect infrastructure for STONITH API
 *
 * Copyright (c) 2000 Alan Robertson <alanr@unix.sh>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef __EXPECT_H
#	define __EXPECT_H
/*
 *	If we find any of the given tokens in the input stream,
 *	we return it's "toktype", so we can tell which one was
 *	found.
 *
 */

/**
 * A token we pass to ExpectToken()
 */
struct Etoken {
	const char *	string;		/**< The token to look for */
	int		toktype;	/**< The type to return on match */
	int		matchto;	/**< Modified during matches */
};

int ExpectToken(int fd
,	struct Etoken * toklist	/* List of tokens to match against */
				/* Final token has NULL string */
,	int to_secs		/* Timeout value in seconds */
,	char * buf		/* If non-NULL, then all the text
				 * matched/skipped over by this match */
,	int maxline);		/* Size of 'buf' area in bytes */


/*
 *	A handy little routine.  It runs the given process
 *	with it's standard output redirected into our *readfd, and
 *	its standard input redirected from our *writefd
 *
 *	Doing this with all the pipes, etc. required for doing this
 *	is harder than it sounds :-)
 */

int StartProcess(const char * cmd, int* readfd, int* writefd, int redir_err);

#define EXP_STDERR 1
#define EXP_NOCTTY 2

#ifndef EOS
#	define	EOS '\0'
#endif
#endif /*__EXPECT_H*/
