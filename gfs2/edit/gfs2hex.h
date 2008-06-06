#ifndef __GFS2HEX_DOT_H__
#define __GFS2HEX_DOT_H__


int display_gfs2(void);
int edit_gfs2(void);
void do_dinode_extended(struct gfs2_dinode *di, char *buf);
void print_gfs2(const char *fmt, ...);

#endif /*  __GFS2HEX_DOT_H__  */
