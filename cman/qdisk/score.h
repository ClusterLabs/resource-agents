/**
  Copyright Red Hat, Inc. 2006

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

  Author: Lon Hohberger <lhh at redhat.com>
 */
/**
  @file Quorum daemon scoring functions + thread header file
 */
#ifndef _SCORE_H
#define _SCORE_H

#include <time.h>
#include <sys/time.h>
#include <sys/types.h>

struct h_data {
	char *	program;
	int	score;
	int	available;
	int	tko;
	int	interval;
	int	misses;
	pid_t	childpid;
	time_t	nextrun;
};

/*
   Grab score data from CCSD
 */
int configure_heuristics(int ccsfd, struct h_data *hp, int max);

/* 
   Stop the thread which runs the scoring applets.
 */
int stop_score_thread(void);

/*
   Start the thread which runs the scoring applets
 */
int start_score_thread(qd_ctx *ctx, struct h_data *h, int count);

/* 
   Get our score + maxscore
 */
int get_my_score(int *score, int *maxscore);

#endif
