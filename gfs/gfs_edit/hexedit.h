#ifndef __HEXVIEW_DOT_H__
#define __HEXVIEW_DOT_H__


#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

/*  Extern Macro  */

#ifndef EXTERN
#define EXTERN extern
#define INIT(X)
#else
#undef EXTERN
#define EXTERN
#define INIT(X) =X 
#endif

#define DISPLAY_MODES 3
enum dsp_mode { HEX_MODE = 0, GFS_MODE = 1, EXTENDED_MODE = 2 };

EXTERN char *prog_name;
EXTERN int fd;
EXTERN int64 block INIT(0);
EXTERN int64 previous_block INIT(0);
EXTERN int64 block_in_mem INIT(-1);
EXTERN int edit_mode INIT(0);
EXTERN int line, termlines;
EXTERN char edit_fmt[80];
EXTERN char edit_string[1024];
EXTERN int verbose INIT(FALSE);
EXTERN uint64 dev_offset INIT(0);
EXTERN uint64 max_block INIT(0);
EXTERN char *buf INIT(NULL);
EXTERN uint64 bufsize INIT(4096);
EXTERN int Quit INIT(FALSE);
EXTERN int termlines INIT(30);
EXTERN int insert INIT(0);
EXTERN const char *termtype;
EXTERN int line INIT(1);
EXTERN int struct_len INIT(0);
EXTERN unsigned int offset;
EXTERN int edit_row[DISPLAY_MODES], edit_col[DISPLAY_MODES];
EXTERN int edit_size[DISPLAY_MODES], edit_last[DISPLAY_MODES];
EXTERN char edit_string[1024], edit_fmt[80];
EXTERN struct gfs_sb sb;
EXTERN struct gfs_dinode di;
EXTERN int screen_chunk_size INIT(512); /* how much of the 4K can fit on screen */
EXTERN int gfs_struct_type;
EXTERN uint64 indirect_block[512]; /* more than the most indirect ptrs possible
									  for any given 4K block */
EXTERN int indirect_blocks INIT(0);  /* count of indirect blocks */
EXTERN enum dsp_mode display_mode INIT(HEX_MODE);

#define STRLEN (256)
#define SCREEN_HEIGHT   (16)
#define SCREEN_WIDTH    (16)

#define die(fmt, args...) \
{ \
  fprintf(stderr, "%s: ", prog_name); \
  fprintf(stderr, fmt, ## args); \
  exit(EXIT_FAILURE); \
}

/*  I/O macros  */

#define do_lseek(fd, off) \
{ \
  if (lseek((fd), (off), SEEK_SET) != (off)) \
    die("bad seek: %s on line %d of file %s\n", \
	strerror(errno),__LINE__, __FILE__); \
}

#define do_read(fd, buff, len) \
{ \
  if (read((fd), (buff), (len)) != (len)) \
    die("bad read: %s on line %d of file %s\n", \
	strerror(errno), __LINE__, __FILE__); \
}

#define do_write(fd, buff, len) \
{ \
  if (write((fd), (buff), (len)) != (len)) \
    die("bad write: %s on line %d of file %s\n", \
	strerror(errno), __LINE__, __FILE__); \
}



/*  Memory macros  */

#define type_zalloc(ptr, type, count) \
{ \
  (ptr) = (type *)malloc(sizeof(type) * (count)); \
  if ((ptr)) \
    memset((char *)(ptr), 0, sizeof(type) * (count)); \
  else \
    die("unable to allocate memory on line %d of file %s\n", \
	__LINE__, __FILE__); \
}

#define type_alloc(ptr, type, count) \
{ \
  (ptr) = (type *)malloc(sizeof(type) * (count)); \
  if (!(ptr)) \
    die("unable to allocate memory on line %d of file %s\n", \
	__LINE__, __FILE__); \
}

#define pa(struct, member, count) print_array(#member, struct->member, count);
#define printk printw
#define pv(struct, member, fmt) do { \
		if (line < termlines) { \
			if (line == edit_row[display_mode] + 3) {	\
				attrset(COLOR_PAIR(5));				\
			}										\
			move(line,0);							\
			printw("  "#member":");					\
			move(line,24);							\
			printw(fmt, struct->member);			\
			move(line, 50);							\
			if (strstr(fmt,"X") || strstr(fmt,"x")) \
				printw("(hex)");					\
			else if (strstr(fmt,"s"))				\
				printw("(string)");					\
			else									\
				printw("(decimal)");				\
			refresh();								\
			if (line == edit_row[display_mode] + 3) {		\
				sprintf(edit_string, fmt, struct->member);	\
				strcpy(edit_fmt, fmt);						\
				edit_size[display_mode] = strlen(edit_string);	\
				attrset(COLOR_PAIR(2));						\
			}												\
			if (line - 3 > edit_last[display_mode])				\
				edit_last[display_mode] = line - 3;				\
			line++;													\
			move(line,0); /* this seemingly redundant move needed */	\
		} \
	} while (FALSE);

#define pv2(struct, member, fmt) do { \
		if (line < termlines) { \
			if (line == edit_row[display_mode] + 3) {	\
				attrset(COLOR_PAIR(5));				\
			}										\
			move(line,24);							\
			printw(fmt, struct->member);			\
			move(line, 50);							\
			if (strstr(fmt,"X") || strstr(fmt,"x")) \
				printw("(hex)");					\
			else if (strstr(fmt,"s"))				\
				printw("(string)");					\
			else									\
				printw("(decimal)");				\
			refresh();								\
			if (line == edit_row[display_mode] + 3) {		\
				sprintf(edit_string, fmt, struct->member);	\
				strcpy(edit_fmt, fmt);						\
				edit_size[display_mode] = strlen(edit_string);	\
				attrset(COLOR_PAIR(2));						\
			}												\
			if (line - 3 > edit_last[display_mode])				\
				edit_last[display_mode] = line - 3;				\
			line++;													\
			move(line,0); /* this seemingly redundant move needed */	\
		} \
	} while (FALSE);


/*  Divide x by y.  Round up if there is a remainder.  */
#define DIV_RU(x, y) (((x) + (y) - 1) / (y))


#endif /* __HEXVIEW_DOT_H__ */
