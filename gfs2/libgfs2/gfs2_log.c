#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>
#include <libintl.h>
#include <sys/select.h>
#include <signal.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "libgfs2.h"

#define _(String) gettext(String)

struct log_state {
	int print_level;
};
static struct log_state _state = {MSG_NOTICE};

void increase_verbosity(void)
{
	_state.print_level++;
}

void decrease_verbosity(void)
{
	_state.print_level--;
}

void print_msg(int priority, char *file, int line, const char *format,
			   va_list args) {

	switch (priority) {

	case MSG_DEBUG:
		printf("(%s:%d)\t", file, line);
		vprintf(format, args);
		fflush(NULL);
		break;
	case MSG_INFO:
	case MSG_NOTICE:
	case MSG_WARN:
		vprintf(format, args);
		fflush(NULL);
		break;
	case MSG_ERROR:
	case MSG_CRITICAL:
	default:
		vfprintf(stderr, format, args);
		break;
	}
	return;
}


void print_fsck_log(int iif, int priority, char *file, int line,
					const char *format, ...)
{

	va_list args;
	const char *transform;

        va_start(args, format);

	transform = _(format);

	if((_state.print_level == priority) ||
	   (!iif && (_state.print_level >= priority)))
		print_msg(priority, file, line, transform, args);

	va_end(args);
}

char gfs2_getch(void)
{
	struct termios termattr, savetermattr;
	char ch;
	ssize_t size;

	tcgetattr (STDIN_FILENO, &termattr);
	savetermattr = termattr;
	termattr.c_lflag &= ~(ICANON | IEXTEN | ISIG);
	termattr.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	termattr.c_cflag &= ~(CSIZE | PARENB);
	termattr.c_cflag |= CS8;
	termattr.c_oflag &= ~(OPOST);
   	termattr.c_cc[VMIN] = 0;
	termattr.c_cc[VTIME] = 0;

	tcsetattr (STDIN_FILENO, TCSANOW, &termattr);
	do {
		size = read(STDIN_FILENO, &ch, 1);
		if (size)
			break;
		usleep(50000);
	} while (!size);

	tcsetattr (STDIN_FILENO, TCSANOW, &savetermattr);
	return ch;
}

char generic_interrupt(const char *caller, const char *where,
		       const char *progress, const char *question,
		       const char *answers)
{
	fd_set rfds;
	struct timeval tv;
	char response;
	int err, i;

	FD_ZERO(&rfds);
	FD_SET(STDIN_FILENO, &rfds);

	tv.tv_sec = 0;
	tv.tv_usec = 0;
	/* Make sure there isn't extraneous input before asking the
	 * user the question */
	while((err = select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv))) {
		if(err < 0) {
			log_debug("Error in select() on stdin\n");
			break;
		}
		if(read(STDIN_FILENO, &response, sizeof(char)) < 0) {
			log_debug("Error in read() on stdin\n");
			break;
		}
	}
	while (TRUE) {
		printf("\n%s interrupted during %s:  ", caller, where);
		if (progress)
			printf("%s.\n", progress);
		printf("%s", question);

		/* Make sure query is printed out */
		fflush(NULL);
		response = gfs2_getch();
		printf("\n");
		fflush(NULL);
		if (strchr(answers, response))
			break;
		printf("Bad response, please type ");
		for (i = 0; i < strlen(answers) - 1; i++)
			printf("'%c', ", answers[i]);
		printf(" or '%c'.\n", answers[i]);
	}
	return response;
}

int gfs2_query(int *setonabort, struct gfs2_options *opts,
	       const char *format, ...)
{

	va_list args;
	const char *transform;
	char response;
	int ret = 0;

	*setonabort = 0;
	if(opts->yes)
		return 1;
	if(opts->no)
		return 0;

	opts->query = TRUE;
	while (1) {
		va_start(args, format);
		transform = _(format);
		vprintf(transform, args);
		va_end(args);

		/* Make sure query is printed out */
		fflush(NULL);
		response = gfs2_getch();

		printf("\n");
		fflush(NULL);
		if (response == 0x3) { /* if interrupted, by ctrl-c */
			response = generic_interrupt("Question", "response",
						     NULL,
						     "Do you want to abort " \
						     "or continue (a/c)?",
						     "ac");
			if (response == 'a') {
				ret = 0;
				*setonabort = 1;
				break;
			}
			printf("Continuing.\n");
		} else if(tolower(response) == 'y') {
                        ret = 1;
                        break;
 		} else if (tolower(response) == 'n') {
			ret = 0;
			break;
		} else {
			printf("Bad response %d, please type 'y' or 'n'.\n",
			       response);
		}
	}

	opts->query = FALSE;
	return ret;
}
