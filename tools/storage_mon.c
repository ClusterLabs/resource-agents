#include <stdio.h>
#include <getopt.h>
#include <stdlib.h>
#include <stdint.h>
#include <syslog.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#ifdef __FreeBSD__
#include <sys/disk.h>
#endif
#include <glib.h>
#include <libgen.h>

#define MAX_DEVICES 25
#define DEFAULT_TIMEOUT 10
#define DEFAULT_INTERVAL 30
#define DEFAULT_PIDFILE "/tmp/storage_mon.pid"
#define DEFAULT_ATTRNAME "#health-storage_mon"
#define DEFAULT_HA_SBIN_DIR "/usr/sbin"

char *devices[MAX_DEVICES];
size_t device_count;
int scores[MAX_DEVICES];
int timeout = DEFAULT_TIMEOUT;
const char *attrname = DEFAULT_ATTRNAME;
const char *ha_sbin_dir = DEFAULT_HA_SBIN_DIR;
int verbose;
int inject_error_percent;

GMainLoop *mainloop;
guint timer;
int shutting_down = FALSE;

int write_pid_file(const char *pidfile);

static void usage(char *name, FILE *f)
{
	fprintf(f, "usage: %s [-hv] [-d <device>]... [-s <score>]... [-t <secs>] [-i <secs>] [-p <pidfile>] [-a <attrname>]\n", name);
	fprintf(f, "      --device <dev>       device to test, up to %d instances\n", MAX_DEVICES);
	fprintf(f, "      --score <n>          score if device fails the test. Must match --device count\n");
	fprintf(f, "      --timeout <n>        max time to wait for a device test to come back. in seconds (default %d)\n", DEFAULT_TIMEOUT);
	fprintf(f, "      --interval <n>       interval to test. in seconds (default %d)\n", DEFAULT_INTERVAL);
	fprintf(f, "      --pidfile <path>     file path to record pid (default %s)\n", DEFAULT_PIDFILE);
	fprintf(f, "      --attrname <attr>    attribute name to update test result (default %s)\n", DEFAULT_ATTRNAME);
	fprintf(f, "      --ha-sbin-dir <dir>  directory where attrd_updater is located (default %s)\n", DEFAULT_HA_SBIN_DIR);
	fprintf(f, "      --inject-errors-percent <n>  Generate EIO errors <n>%% of the time (for testing only)\n");
	fprintf(f, "      --verbose            emit extra output to LOG_INFO\n");
	fprintf(f, "      --help               print this message\n");
}

/* Check one device */
static void *test_device(const char *device)
{
	uint64_t devsize;
	int flags = O_RDONLY | O_DIRECT;
	int device_fd;
	int res;
	off_t seek_spot;

	if (verbose) {
		syslog(LOG_INFO, "Testing device %s", device);
	}

	device_fd = open(device, flags);
	if (device_fd < 0) {
		if (errno != EINVAL) {
			syslog(LOG_ERR, "Failed to open %s: %s", device, strerror(errno));
			exit(-1);
		}
		flags &= ~O_DIRECT;
		device_fd = open(device, flags);
		if (device_fd < 0) {
			syslog(LOG_ERR, "Failed to open %s: %s", device, strerror(errno));
			exit(-1);
		}
	}
#ifdef __FreeBSD__
	res = ioctl(device_fd, DIOCGMEDIASIZE, &devsize);
#else
	res = ioctl(device_fd, BLKGETSIZE64, &devsize);
#endif
	if (res < 0) {
		syslog(LOG_ERR, "Failed to get device size for %s: %s", device, strerror(errno));
		goto error;
	}
	if (verbose) {
		syslog(LOG_INFO, "%s: opened %s O_DIRECT, size=%zu", device, (flags & O_DIRECT)?"with":"without", devsize);
	}

	/* Don't fret about real randomness */
	srand(time(NULL) + getpid());
	/* Pick a random place on the device - sector aligned */
	seek_spot = (rand() % (devsize-1024)) & 0xFFFFFFFFFFFFFE00;
	res = lseek(device_fd, seek_spot, SEEK_SET);
	if (res < 0) {
		syslog(LOG_ERR, "Failed to seek %s: %s", device, strerror(errno));
		goto error;
	}
	if (verbose) {
		syslog(LOG_INFO, "%s: reading from pos %ld", device, seek_spot);
	}

	if (flags & O_DIRECT) {
		int sec_size = 0;
		void *buffer;

#ifdef __FreeBSD__
		res = ioctl(device_fd, DIOCGSECTORSIZE, &sec_size);
#else
		res = ioctl(device_fd, BLKSSZGET, &sec_size);
#endif
		if (res < 0) {
			syslog(LOG_ERR, "Failed to get block device sector size for %s: %s", device, strerror(errno));
			goto error;
		}

		if (posix_memalign(&buffer, sysconf(_SC_PAGESIZE), sec_size) != 0) {
			syslog(LOG_ERR, "Failed to allocate aligned memory: %s", strerror(errno));
			goto error;
		}
		res = read(device_fd, buffer, sec_size);
		free(buffer);
		if (res < 0) {
			syslog(LOG_ERR, "Failed to read %s: %s", device, strerror(errno));
			goto error;
		}
		if (res < sec_size) {
			syslog(LOG_ERR, "Failed to read %d bytes from %s, got %d", sec_size, device, res);
			goto error;
		}
	} else {
		char buffer[512];

		res = read(device_fd, buffer, sizeof(buffer));
		if (res < 0) {
			syslog(LOG_ERR, "Failed to read %s: %s", device, strerror(errno));
			goto error;
		}
		if (res < (int)sizeof(buffer)) {
			syslog(LOG_ERR, "Failed to read %ld bytes from %s, got %d", sizeof(buffer), device, res);
			goto error;
		}
	}

	/* Fake an error */
	if (inject_error_percent && ((rand() % 100) < inject_error_percent)) {
		syslog(LOG_ERR, "People, please fasten your seatbelts, injecting errors!");
		goto error;
	}
	res = close(device_fd);
	if (res != 0) {
		syslog(LOG_ERR, "Failed to close %s: %s", device, strerror(errno));
		exit(-1);
	}

	if (verbose) {
		syslog(LOG_INFO, "%s: done", device);
	}
	exit(0);

error:
	close(device_fd);
	exit(-1);
}

static void
child_shutdown(int nsig)
{
	exit(1);
}

static gboolean
timer_cb(gpointer data)
{
	pid_t test_forks[MAX_DEVICES];
	size_t finished_count = 0;
	struct timespec ts;
	time_t start_time;
	size_t i;
	int final_score = 0;
	const char *value;
	pid_t pid, wpid;
	int wstatus;

	if (shutting_down == TRUE) {
		goto done;
	}

	memset(test_forks, 0, sizeof(test_forks));
	for (i=0; i<device_count; i++) {
		test_forks[i] = fork();
		if (test_forks[i] < 0) {
			syslog(LOG_ERR, "Error spawning fork for %s: %s", devices[i], strerror(errno));
			/* Just test the devices we have */
			break;
		}
		/* child */
		if (test_forks[i] == 0) {
			signal(SIGTERM, &child_shutdown);
			test_device(devices[i]);
		}
	}

	/* See if they have finished */
	clock_gettime(CLOCK_REALTIME, &ts);
	start_time = ts.tv_sec;

	while ((finished_count < device_count) && ((start_time + timeout) > ts.tv_sec)) {
		for (i=0; i<device_count; i++) {
			if (test_forks[i] > 0) {
				wpid = waitpid(test_forks[i], &wstatus, WUNTRACED | WNOHANG | WCONTINUED);
				if (wpid < 0) {
					syslog(LOG_ERR, "waitpid on %s failed: %s", devices[i], strerror(errno));
					goto done;
				}

				if (wpid == test_forks[i]) {
					if (WIFEXITED(wstatus)) {
						if (WEXITSTATUS(wstatus) != 0) {
							syslog(LOG_ERR, "I/O error detected on %s", devices[i]);
							final_score += scores[i];
						}

						finished_count++;
						test_forks[i] = 0;
					}
				}
			}
		}

		usleep(100000);

		clock_gettime(CLOCK_REALTIME, &ts);
	}

	/* See which threads have not finished */
	for (i=0; i<device_count; i++) {
		if (test_forks[i] != 0) {
			syslog(LOG_ERR, "I/O error detected on %s (check did not complete within %ds)", devices[i], timeout);
			final_score += scores[i];
		}
	}

	/* Update node health attribute */
	value = (final_score > 0) ? "red" : "green";
	pid = fork();
	if (pid == 0) {
		char path[PATH_MAX];
		snprintf(path, PATH_MAX, "%s/attrd_updater", ha_sbin_dir);
		execl(path, "attrd_updater", "-n", attrname, "-U", value, "-d", "5s", NULL);
		syslog(LOG_ERR, "Failed to execute %s: %s", path, strerror(errno));
		exit(1);
	} else if (pid < 0) {
		syslog(LOG_ERR, "Error spawning fork for attrd_updater: %s", strerror(errno));
		goto done;
	}
	wpid = waitpid(pid, &wstatus, 0);
	if (wpid < 0) {
		syslog(LOG_ERR, "failed to wait for attrd_updater: %s", strerror(errno));
		goto done;
	}
	if (WIFEXITED(wstatus) && (WEXITSTATUS(wstatus) != 0)) {
		syslog(LOG_ERR, "failed to update attribute (%s=%s): attrd_updater exited %d", attrname, value, WEXITSTATUS(wstatus));
		goto done;
	}
	if (verbose) {
		syslog(LOG_INFO, "Updated node health attribute: %s=%s", attrname, value);
	}

	/* See if threads have finished */
	while (finished_count < device_count) {
		for (i=0; i<device_count; i++) {
			if (test_forks[i] > 0
			    && waitpid(test_forks[i], &wstatus, WUNTRACED | WNOHANG | WCONTINUED) == test_forks[i]
			    && WIFEXITED(wstatus)) {
				finished_count++;
				test_forks[i] = 0;
			}
		}
		usleep(100000);
	}

	if (shutting_down == TRUE) {
		goto done;
	}
	if (data != NULL) {
		g_source_remove(timer);
		timer = g_timeout_add(*(int *)data * 1000, timer_cb, NULL);
	}
	return TRUE;

done:
	g_main_loop_quit(mainloop);
	return FALSE;
}

int
write_pid_file(const char *pidfile)
{
	char *pid;
	char *dir, *str = NULL;
	int fd = -1;
	int rc = -1;
	int i, len;

	if (asprintf(&pid, "%jd", (intmax_t)getpid()) < 0) {
		syslog(LOG_ERR, "Failed to allocate memory to store PID");
		pid = NULL;
		goto done;
	}

	str = strdup(pidfile);
	if (str == NULL) {
		syslog(LOG_ERR, "Failed to duplicate string ['%s']", pidfile);
		goto done;
	}
	dir = dirname(str);
	for (i = 1, len = strlen(dir); i < len; i++) {
		if (dir[i] == '/') {
			dir[i] = 0;
			if ((mkdir(dir, 0640) < 0) && (errno != EEXIST)) {
				syslog(LOG_ERR, "Failed to create directory %s: %s", dir, strerror(errno));
				goto done;
			}
			dir[i] = '/';
		}
	}
	if ((mkdir(dir, 0640) < 0) && (errno != EEXIST)) {
		syslog(LOG_ERR, "Failed to create directory %s: %s", dir, strerror(errno));
		goto done;
	}

	fd = open(pidfile, O_CREAT | O_WRONLY, 0640);
	if (fd < 0) {
		syslog(LOG_ERR, "Failed to open %s: %s", pidfile, strerror(errno));
		goto done;
	}

	if (write(fd, pid, strlen(pid)) != strlen(pid)) {
		syslog(LOG_ERR, "Failed to write '%s' to %s: %s", pid, pidfile, strerror(errno));
		goto done;
	}
	close(fd);
	rc = 0;
done:
	if (pid != NULL) {
		free(pid);
	}
	if (str != NULL) {
		free(str);
	}
	return rc;
}

static void
daemon_shutdown(int nsig)
{
	shutting_down = TRUE;
	g_source_remove(timer);
	timer = g_timeout_add(0, timer_cb, NULL);
}

int main(int argc, char *argv[])
{
	int interval = DEFAULT_INTERVAL;
	const char *pidfile = DEFAULT_PIDFILE;
	size_t score_count = 0;
	int opt, option_index;
	struct option long_options[] = {
		{"timeout", required_argument, 0, 't' },
		{"device",  required_argument, 0, 'd' },
		{"score",   required_argument, 0, 's' },
		{"interval", required_argument, 0, 'i' },
		{"pidfile", required_argument, 0, 'p' },
		{"attrname", required_argument, 0, 'a' },
		{"ha-sbin-dir", required_argument, 0, 0 },
		{"inject-errors-percent",   required_argument, 0, 0 },
		{"verbose", no_argument, 0, 'v' },
		{"help",    no_argument, 0,       'h' },
		{0,         0,           0,        0  }
	};
	struct sigaction sa;

	while ( (opt = getopt_long(argc, argv, "hvt:d:s:i:p:a:",
				   long_options, &option_index)) != -1 ) {
		switch (opt) {
			case 0: /* Long-only options */
				if (strcmp(long_options[option_index].name, "inject-errors-percent") == 0) {
					inject_error_percent = atoi(optarg);
					if (inject_error_percent < 1 || inject_error_percent > 100) {
						fprintf(stderr, "inject_error_percent should be between 1 and 100\n");
						return -1;
					}
				}
				if (strcmp(long_options[option_index].name, "ha-sbin-dir") == 0) {
					ha_sbin_dir = strdup(optarg);
					if (ha_sbin_dir == NULL) {
						fprintf(stderr, "Failed to duplicate string ['%s']\n", optarg);
						return -1;
					}
				}
				break;
			case 'd':
				if (device_count < MAX_DEVICES) {
					devices[device_count++] = strdup(optarg);
					if (devices[device_count - 1] == NULL) {
						fprintf(stderr, "Failed to duplicate string ['%s']\n", optarg);
						return -1;
					}
				} else {
					fprintf(stderr, "too many devices, max is %d\n", MAX_DEVICES);
					return -1;
				}
				break;
			case 's':
				if (score_count < MAX_DEVICES) {
					int score = atoi(optarg);
					if (score < 1 || score > 10) {
						fprintf(stderr, "Score must be between 1 and 10 inclusive\n");
						return -1;
					}
					scores[score_count++] = score;
				} else {
					fprintf(stderr, "too many scores, max is %d\n", MAX_DEVICES);
					return -1;
				}
				break;
			case 'v':
				verbose++;
				break;
			case 't':
				timeout = atoi(optarg);
				if (timeout < 1) {
					fprintf(stderr, "invalid timeout %d. Min 1, recommended %d (default)\n", timeout, DEFAULT_TIMEOUT);
					return -1;
				}
				break;
			case 'i':
				interval = atoi(optarg);
				if (interval < 1) {
					fprintf(stderr, "invalid interval %d. Min 1, default is %d\n", interval, DEFAULT_INTERVAL);
					return -1;
				}
				break;
			case 'p':
				pidfile = strdup(optarg);
				if (pidfile == NULL) {
					fprintf(stderr, "Failed to duplicate string ['%s']\n", optarg);
					return -1;
				}
				break;
			case 'a':
				attrname = strdup(optarg);
				if (attrname == NULL) {
					fprintf(stderr, "Failed to duplicate string ['%s']\n", optarg);
					return -1;
				}
				break;
			case 'h':
				usage(argv[0], stdout);
				return 0;
				break;
			default:
				usage(argv[0], stderr);
				return -1;
				break;
		}

	}
	if (device_count == 0) {
		fprintf(stderr, "No devices to test, use the -d  or --device argument\n");
		return -1;
	}

	if (device_count != score_count) {
		fprintf(stderr, "There must be the same number of devices and scores\n");
		return -1;
	}

	openlog("storage_mon", 0, LOG_DAEMON);

	if (daemon(0, 0) < 0) {
		syslog(LOG_ERR, "Failed to daemonize: %s", strerror(errno));
		return -1;
	}

	umask(S_IWGRP | S_IWOTH | S_IROTH);

	memset(&sa, 0, sizeof(struct sigaction));
	sa.sa_handler = daemon_shutdown;
	sa.sa_flags = SA_RESTART;
	if ((sigemptyset(&sa.sa_mask) < 0) || (sigaction(SIGTERM, &sa, NULL) < 0)) {
		syslog(LOG_ERR, "Failed to set handler for signal: %s", strerror(errno));
		return -1;
	}

	if (write_pid_file(pidfile) < 0) {
		return -1;
	}

	mainloop = g_main_loop_new(NULL, FALSE);
	timer = g_timeout_add(0, timer_cb, &interval);
	g_main_loop_run(mainloop);
	g_main_loop_unref(mainloop);

	unlink(pidfile);

	return 0;
}
