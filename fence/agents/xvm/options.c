#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>

/* Local includes */
#include "xvm.h"
#include "simple_auth.h"
#include "mcast.h"
#include "options.h"
#include "debug.h"

LOGSYS_DECLARE_SUBSYS("XVM", LOG_LEVEL_INFO);


/* Assignment functions */

static inline void
assign_debug(fence_xvm_args_t *args, struct arg_info *arg, char *value)
{
	if (!value) {
		/* GNU getopt sets optarg to NULL for options w/o a param
		   We rely on this here... */
		args->debug++;
		return;
	}

	args->debug = atoi(value);
	if (args->debug < 0) {
		args->debug = 1;
	}
}


static inline void
assign_foreground(fence_xvm_args_t *args, struct arg_info *arg,
		  char *value)
{
	args->flags |= F_FOREGROUND;
}


static inline void
assign_family(fence_xvm_args_t *args, struct arg_info *arg,
	      char *value)
{
	if (!strcasecmp(value, "ipv4")) {
		args->family = PF_INET;
	} else if (!strcasecmp(value, "ipv6")) {
		args->family = PF_INET6;
	} else if (!strcasecmp(value, "auto")) {
		args->family = 0;
	} else {
		log_printf(LOG_ERR, "Unsupported family: '%s'\n", value);
		args->flags |= F_ERR;
	}
}


static inline void
assign_address(fence_xvm_args_t *args, struct arg_info *arg, char *value)
{
	if (args->addr)
		free(args->addr);
	args->addr = strdup(value);
}


static inline void
assign_ttl(fence_xvm_args_t *args, struct arg_info *arg, char *value)
{
	int ttl;
	ttl = atoi(value);
	if (ttl < 1 || ttl > 255)
		ttl = DEFAULT_TTL;
	args->ttl = ttl;
}


static inline void
assign_port(fence_xvm_args_t *args, struct arg_info *arg, char *value)
{
	args->port = atoi(value);
	if (args->port <= 0 || args->port >= 65500) {
		log_printf(LOG_ERR, "Invalid port: '%s'\n", value);
		args->flags |= F_ERR;
	}
}


static inline void
assign_retrans(fence_xvm_args_t *args, struct arg_info *arg, char *value)
{
	args->retr_time = atoi(value);
	if (args->retr_time <= 0) {
		log_printf(LOG_ERR, "Invalid retransmit time: '%s'\n", value);
		args->flags |= F_ERR;
	}
}

static inline void
assign_hash(fence_xvm_args_t *args, struct arg_info *arg, char *value)
{
	if (!strcasecmp(value, "none")) {
		args->hash = HASH_NONE;
	} else if (!strcasecmp(value, "sha1")) {
		args->hash = HASH_SHA1;
	} else if (!strcasecmp(value, "sha256")) {
		args->hash = HASH_SHA256;
	} else if (!strcasecmp(value, "sha512")) {
		args->hash = HASH_SHA512;
	} else {
		log_printf(LOG_ERR, "Unsupported hash: %s\n", value);
		args->flags |= F_ERR;
	}
}


static inline void
assign_auth(fence_xvm_args_t *args, struct arg_info *arg, char *value)
{
	if (!strcasecmp(value, "none")) {
		args->auth = AUTH_NONE;
	} else if (!strcasecmp(value, "sha1")) {
		args->auth = AUTH_SHA1;
	} else if (!strcasecmp(value, "sha256")) {
		args->auth = AUTH_SHA256;
	} else if (!strcasecmp(value, "sha512")) {
		args->auth = AUTH_SHA512;
	} else {
		log_printf(LOG_ERR, "Unsupported auth type: %s\n", value);
		args->flags |= F_ERR;
	}
}

static inline void
assign_key(fence_xvm_args_t *args, struct arg_info *arg, char *value)
{
	struct stat st;

	if (args->key_file)
		free(args->key_file);
	args->key_file = strdup(value);

	if (stat(value, &st) == -1) {
 		log_printf(LOG_ERR, "Invalid key file: '%s' (%s)\n", value,
  		       strerror(errno));
		args->flags |= F_ERR;
	}
}


static inline void
assign_op(fence_xvm_args_t *args, struct arg_info *arg, char *value)
{
	if (!strcasecmp(value, "null")) {
		args->op = FENCE_NULL;
	} else if (!strcasecmp(value, "off")) {
		args->op = FENCE_OFF;
	} else if (!strcasecmp(value, "reboot")) {
		args->op = FENCE_REBOOT;
	} else {
		log_printf(LOG_ERR, "Unsupported operation: %s\n", value);
		args->flags |= F_ERR;
	}
}


static inline void
assign_domain(fence_xvm_args_t *args, struct arg_info *arg, char *value)
{
	if (args->domain) {
		log_printf(LOG_ERR,
		   "Domain/UUID may not be specified more than once\n");
		args->flags |= F_ERR;
		return;
	}

	if (args->domain)
		free(args->domain);
	args->domain = strdup(value);

	if (strlen(value) <= 0) {
		log_printf(LOG_ERR, "Invalid domain name\n");
		args->flags |= F_ERR;
	}

	if (strlen(value) >= MAX_DOMAINNAME_LENGTH) {
		errno = ENAMETOOLONG;
		log_printf(LOG_ERR, "Invalid domain name: '%s' (%s)\n",
		       value, strerror(errno));
		args->flags |= F_ERR;
	}
}


static inline void
assign_uuid_lookup(fence_xvm_args_t *args, struct arg_info *arg, char *value)
{
	if (!value) {
		/* GNU getopt sets optarg to NULL for options w/o a param
		   We rely on this here... */
		args->flags |= F_USE_UUID;
		return;
	}

	args->flags |= ( !!atoi(value) ? F_USE_UUID : 0);
}


static inline void
assign_timeout(fence_xvm_args_t *args, struct arg_info *arg, char *value)
{
	args->timeout = atoi(value);
	if (args->timeout <= 0) {
		log_printf(LOG_ERR, "Invalid timeout: '%s'\n", value);
		args->flags |= F_ERR;
	}
}


static inline void
assign_help(fence_xvm_args_t *args, struct arg_info *arg, char *value)
{
	args->flags |= F_HELP;
}


static inline void
assign_version(fence_xvm_args_t *args, struct arg_info *arg, char *value)
{
	args->flags |= F_VERSION;
}


static inline void
assign_noccs(fence_xvm_args_t *args, struct arg_info *arg, char *value)
{
	args->flags |= F_NOCCS;
}


static inline void
assign_nocluster(fence_xvm_args_t *args, struct arg_info *arg, char *value)
{
	args->flags |= F_NOCLUSTER;
}


static inline void
assign_uri(fence_xvm_args_t *args, struct arg_info *arg, char *value)
{
	if (args->uri)
		free(args->uri);

	/* XXX NO validation yet */
	args->uri = strdup(value);
}


/** ALL valid command line and stdin arguments for this fencing agent */
static struct arg_info _arg_info[] = {
	{ '\xff', NULL, "agent",
	  "Not user serviceable",
	  NULL },

	{ '\xff', NULL, "self",
	  "Not user serviceable", 
	  NULL },

	{ 'd', "-d", "debug",
	  "Specify (CCS) / increment (command line) debug level",
	  assign_debug },

	{ 'f', "-f", NULL,
	  "Foreground mode (do not fork)",
	  assign_foreground },

	{ 'i', "-i <family>", "ip_family",
	  "IP Family ([auto], ipv4, ipv6)",
	  assign_family },

	{ 'a', "-a <address>", "multicast_address",
	  "Multicast address (default=225.0.0.12 / ff02::3:1)",
	  assign_address },

	{ 'T', "-T <ttl>", "multicast_ttl",
	  "Multicast time-to-live (in hops; default=2)",
	  assign_ttl },

	{ 'p', "-p <port>", "port",
	  "IP port (default=1229)",
	  assign_port },

	{ 'r', "-r <retrans>", "retrans", 
	  "Multicast retransmit time (in 1/10sec; default=20)",
	  assign_retrans },

	{ 'c', "-c <hash>", "hash",
	  "Packet hash strength (none, sha1, [sha256], sha512)",
	  assign_hash },

	{ 'C', "-C <auth>", "auth",
	  "Authentication (none, sha1, [sha256], sha512)",
	  assign_auth },

	{ 'k', "-k <file>", "key_file",
	  "Shared key file (default=" DEFAULT_KEY_FILE ")",
	  assign_key },

	{ 'o', "-o <operation>", "option",
	  "Fencing operation (null, off, [reboot])",
	  assign_op },

	{ 'H', "-H <domain>", "domain",
	  "Xen host (domain name) to fence",
	  assign_domain },

	{ 'u', "-u", "use_uuid",
	  "Treat <domain> as UUID instead of domain name",
	  assign_uuid_lookup },

	{ 't', "-t <timeout>", "timeout",
	  "Fencing timeout (in seconds; default=30)",
	  assign_timeout },

	{ 'h', "-h", NULL,
 	  "Help",
	  assign_help },

	{ '?', "-?", NULL,
 	  "Help (alternate)", 
	  assign_help },

	{ 'X', "-X", NULL,
 	  "Do not connect to CCS for configuration", 
	  assign_noccs }, 

	{ 'L', "-L", NULL,
 	  "Local mode only (no cluster)",
	  assign_nocluster }, 

	{ 'U', "-U", "uri",
	  "URI for Hypervisor (default: " DEFAULT_HYPERVISOR_URI ")",
	  assign_uri },
	  
	{ 'V', "-V", NULL,
 	  "Display version and exit", 
	  assign_version },

	/* Terminator */
	{ 0, NULL, NULL, NULL, NULL }
};


struct arg_info *
find_arg_by_char(char arg)
{
	int x = 0;

	for (x = 0; _arg_info[x].opt != 0; x++) {
		if (_arg_info[x].opt == arg)
			return &_arg_info[x];
	}

	return NULL;
}


struct arg_info *
find_arg_by_string(char *arg)
{
	int x = 0;

	for (x = 0; _arg_info[x].opt != 0; x++) {
		if (!_arg_info[x].stdin_opt)
			continue;
		if (!strcasecmp(_arg_info[x].stdin_opt, arg))
			return &_arg_info[x];
	}

	return NULL;
}


/* ============================================================= */

/**
  Initialize an args structure.

  @param args		Pointer to args structure to initialize.
 */
void
args_init(fence_xvm_args_t *args)
{
	args->addr = NULL;
	args->domain = NULL;
	args->key_file = strdup(DEFAULT_KEY_FILE);
	args->uri = strdup(DEFAULT_HYPERVISOR_URI);
	args->op = FENCE_REBOOT;
	args->hash = DEFAULT_HASH;
	args->auth = DEFAULT_AUTH;
	args->port = 1229;
	args->family = PF_INET;
	args->timeout = 30;
	args->retr_time = 20;
	args->flags = 0;
	args->debug = 0;
	args->ttl = DEFAULT_TTL;
}


#define _pr_int(piece) dbg_printf(1, "  %s = %d\n", #piece, piece)
#define _pr_str(piece) dbg_printf(1, "  %s = %s\n", #piece, piece)


/**
  Prints out the contents of an args structure for debugging.

  @param args		Pointer to args structure to print out.
 */
void
args_print(fence_xvm_args_t *args)
{
	dbg_printf(1, "-- args @ %p --\n", args);
	_pr_str(args->addr);
	_pr_str(args->domain);
	_pr_str(args->key_file);
	_pr_int(args->op);
	_pr_int(args->hash);
	_pr_int(args->auth);
	_pr_int(args->port);
	_pr_int(args->family);
	_pr_int(args->timeout);
	_pr_int(args->retr_time);
	_pr_int(args->flags);
	_pr_int(args->debug);
	dbg_printf(1, "-- end args --\n");
}


/**
  Print out arguments and help information based on what is allowed in
  the getopt string optstr.

  TODO use log_printf instead of printf 

  @param progname	Program name.
  @param optstr		Getopt(3) style options string
  @param print_stdin	0 = print command line options + description,
			1 = print fence-style stdin args + description
 */
void
args_usage(char *progname, char *optstr, int print_stdin)
{
	int x;
	struct arg_info *arg;

	if (!print_stdin) {
		if (progname) {
			printf("usage: %s [args]\n", progname);
		} else {
			printf("usage: fence_xvm [args]\n");
		}
	}

	for (x = 0; x < strlen(optstr); x++) {
		arg = find_arg_by_char(optstr[x]);
		if (!arg)
			continue;

		if (print_stdin) {
			if (arg && arg->stdin_opt)
				printf("  %-20.20s %-55.55s\n",
				       arg->stdin_opt, arg->desc);
		} else {
			printf("  %-20.20s %-55.55s\n", arg->opt_desc,
			       arg->desc);
		}
	}

	printf("\n");
}


/**
  Remove leading and trailing whitespace from a line of text.

  @param line		Line to clean up
  @param linelen	Max size of line
  @return		0 on success, -1 on failure
 */
int
cleanup(char *line, size_t linelen)
{
	char *p;
	int x;
	
	/* Remove leading whitespace. */
	p = line;
	for (x = 0; x <= linelen; x++) {
		switch (line[x]) {
		case '\t':
		case ' ':
			break;
		case '\n':
		case '\r':
			return -1;
		default:
			goto eol;
		}
	}
eol:
	/* Move the remainder down by as many whitespace chars as we
	   chewed up */
	if (x)
		memmove(p, &line[x], linelen-x);

	/* Remove trailing whitespace. */
	for (x=0; x <= linelen; x++) {
		switch(line[x]) {
		case '\t':
		case ' ':
		case '\r':
		case '\n':
			line[x] = 0;
		case 0:
		/* End of line */
			return 0;
		}
	}

	return -1;
}


/**
  Parse args from stdin and assign to the specified args structure.
  
  @param optstr		Command line option string in getopt(3) format
  @param args		Args structure to fill in.
 */
void
args_get_stdin(char *optstr, fence_xvm_args_t *args)
{
	char in[256];
	int line = 0;
	char *name, *val;
	struct arg_info *arg;

	while (fgets(in, sizeof(in), stdin)) {
		++line;

		if (in[0] == '#')
			continue;

		if (cleanup(in, sizeof(in)) == -1)
			continue;

		name = in;
		if ((val = strchr(in, '='))) {
			*val = 0;
			++val;
		}

		arg = find_arg_by_string(name);
		if (!arg || (arg->opt != '\xff' && 
			     !strchr(optstr, arg->opt))) {
			fprintf(stderr,
				"parse warning: "
				"illegal variable '%s' on line %d\n", name,
				line);
			continue;
		}

		if (arg->assign)
			arg->assign(args, arg, val);
	}
}


/**
  Parse args from stdin and assign to the specified args structure.
  
  @param optstr		Command line option string in getopt(3) format
  @param args		Args structure to fill in.
 */
void
args_get_getopt(int argc, char **argv, char *optstr, fence_xvm_args_t *args)
{
	int opt;
	struct arg_info *arg;

	while ((opt = getopt(argc, argv, optstr)) != EOF) {

		arg = find_arg_by_char(opt);

		if (!arg) {
			args->flags |= F_ERR;
			continue;
		}

		if (arg->assign)
			arg->assign(args, arg, optarg);
	}
}


void
args_finalize(fence_xvm_args_t *args)
{
	char *addr = NULL;

	if (!args->addr) {
		switch(args->family) {
		case 0:
		case PF_INET:
			addr = IPV4_MCAST_DEFAULT;
			break;
		case PF_INET6:
			addr = IPV6_MCAST_DEFAULT;
			break;
		default:
			args->flags |= F_ERR;
		break;
		}
	}

	if (!args->addr)
		args->addr = addr;

	if (!args->addr) {
		printf("No multicast address available\n");
		args->flags |= F_ERR;
	}

	if (!args->addr)
		return;
	if (args->family)
		return;

	/* Set family */
	if (strchr(args->addr, ':'))
		args->family = PF_INET6;
	if (strchr(args->addr, '.'))
		args->family = PF_INET;
	if (!args->family) {
		printf("Could not determine address family\n");
		args->flags |= F_ERR;
	}
}
