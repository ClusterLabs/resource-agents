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
   Start the thread which runs the scoring applets
 */
int start_score_thread(qd_ctx *ctx, struct h_data *h, int count);

/* 
   Get our score + maxscore
 */
int get_my_score(int *score, int *maxscore);

/* 
   Set score + maxscore to 1.  Call if no heuristics are present
   to enable master-wins mode
 */
int fudge_scoring(void);


#endif
