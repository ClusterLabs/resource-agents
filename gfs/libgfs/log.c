#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>
#include <libintl.h>

#include <sys/select.h>
#include <unistd.h>

#include "incore.h"
#include "libgfs.h"

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

void print_msg(int priority, char *file, int line, const char *format, va_list args) {

	switch (priority) {

	case MSG_DEBUG:
		printf("(%s:%d)\t", file, line);
		vprintf(format, args);
		break;
	case MSG_INFO:
	case MSG_NOTICE:
	case MSG_WARN:
		vprintf(format, args);
		break;
	case MSG_ERROR:
	case MSG_CRITICAL:
	default:
		vfprintf(stderr, format, args);
		break;
	}
	return;
}

void print_log_level(int iif, int priority, char *file, int line, const char *format, ...)
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

int query(struct options *opts, const char *format, ...)
{

	va_list args;
	const char *transform;
	char response;
	fd_set rfds;
	struct timeval tv;
	int err = 0;
	int ret = 0;

	va_start(args, format);

	transform = _(format);

	if(opts->yes)
		return 1;
	if(opts->no)
		return 0;

	/* Watch stdin (fd 0) to see when it has input. */
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
		read(STDIN_FILENO, &response, sizeof(char));

	}
 query:
	vprintf(transform, args);

	/* Make sure query is printed out */
	fflush(NULL);

 rescan:
	read(STDIN_FILENO, &response, sizeof(char));

	if(tolower(response) == 'y') {
		ret = 1;
	} else if (tolower(response) == 'n') {
		ret = 0;
	} else if ((response == ' ') || (response == '\t')) {
		goto rescan;
	} else {
		while(response != '\n')
			read(STDIN_FILENO, &response, sizeof(char));
		printf("Bad response, please type 'y' or 'n'.\n");
		goto query;
	}

	/* Clip the input */
	while((err = select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv))) {
		if(err < 0) {
			log_debug("Error in select() on stdin\n");
			break;
		}
		read(STDIN_FILENO, &response, sizeof(char));
	}

	return ret;
}
