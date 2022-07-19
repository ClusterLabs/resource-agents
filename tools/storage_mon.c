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

#define MAX_DEVICES 25
#define DEFAULT_TIMEOUT 10

static void usage(char *name, FILE *f)
{
	fprintf(f, "usage: %s [-hv] [-d <device>]... [-s <score>]... [-t <secs>]\n", name);
	fprintf(f, "      --device <dev>  device to test, up to %d instances\n", MAX_DEVICES);
	fprintf(f, "      --score  <n>    score if device fails the test. Must match --device count\n");
	fprintf(f, "      --timeout <n>   max time to wait for a device test to come back. in seconds (default %d)\n", DEFAULT_TIMEOUT);
	fprintf(f, "      --inject-errors-percent <n> Generate EIO errors <n>%% of the time (for testing only)\n");
	fprintf(f, "      --verbose        emit extra output to stdout\n");
	fprintf(f, "      --help           print this message\n");
}

/* Check one device */
static void *test_device(const char *device, int verbose, int inject_error_percent)
{
	uint64_t devsize;
	int device_fd;
	int res;
	off_t seek_spot;
	char buffer[512];

	if (verbose) {
		printf("Testing device %s\n", device);
	}

	device_fd = open(device, O_RDONLY);
	if (device_fd < 0) {
		fprintf(stderr, "Failed to open %s: %s\n", device, strerror(errno));
		exit(-1);
	}
#ifdef __FreeBSD__
	res = ioctl(device_fd, DIOCGMEDIASIZE, &devsize);
#else
	res = ioctl(device_fd, BLKGETSIZE64, &devsize);
#endif
	if (res != 0) {
		fprintf(stderr, "Failed to stat %s: %s\n", device, strerror(errno));
		close(device_fd);
		exit(-1);
	}
	if (verbose) {
		fprintf(stderr, "%s: size=%zu\n", device, devsize);
	}
	/* Don't fret about real randomness */
	srand(time(NULL) + getpid());
	/* Pick a random place on the device - sector aligned */
	seek_spot = (rand() % (devsize-1024)) & 0xFFFFFFFFFFFFFE00;
	res = lseek(device_fd, seek_spot, SEEK_SET);
	if (res < 0) {
		fprintf(stderr, "Failed to seek %s: %s\n", device, strerror(errno));
		close(device_fd);
		exit(-1);
	}

	if (verbose) {
		printf("%s: reading from pos %ld\n", device, seek_spot);
	}

	res = read(device_fd, buffer, sizeof(buffer));
	if (res < 0) {
		fprintf(stderr, "Failed to read %s: %s\n", device, strerror(errno));
		close(device_fd);
		exit(-1);
	}
	if (res < (int)sizeof(buffer)) {
		fprintf(stderr, "Failed to read %ld bytes from %s, got %d\n", sizeof(buffer), device, res);
		close(device_fd);
		exit(-1);
	}

	/* Fake an error */
	if (inject_error_percent && ((rand() % 100) < inject_error_percent)) {
		fprintf(stderr, "People, please fasten your seatbelts, injecting errors!\n");
		close(device_fd);
		exit(-1);
	}
	res = close(device_fd);
	if (res != 0) {
		fprintf(stderr, "Failed to close %s: %s\n", device, strerror(errno));
		close(device_fd);
		exit(-1);
	}

	if (verbose) {
		printf("%s: done\n", device);
	}
	exit(0);
}

int main(int argc, char *argv[])
{
	char *devices[MAX_DEVICES];
	int scores[MAX_DEVICES];
	pid_t test_forks[MAX_DEVICES];
	size_t device_count = 0;
	size_t score_count = 0;
	size_t finished_count = 0;
	int timeout = DEFAULT_TIMEOUT;
	struct timespec ts;
	time_t start_time;
	size_t i;
	int final_score = 0;
	int opt, option_index;
	int verbose = 0;
	int inject_error_percent = 0;
	struct option long_options[] = {
		{"timeout", required_argument, 0, 't' },
		{"device",  required_argument, 0, 'd' },
		{"score",   required_argument, 0, 's' },
		{"inject-errors-percent",   required_argument, 0, 0 },
		{"verbose", no_argument, 0, 'v' },
		{"help",    no_argument, 0,       'h' },
		{0,         0,           0,        0  }
	};
	while ( (opt = getopt_long(argc, argv, "hvt:d:s:",
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
				break;
			case 'd':
				if (device_count < MAX_DEVICES) {
					devices[device_count++] = strdup(optarg);
				} else {
					fprintf(stderr, "too many devices, max is %d\n", MAX_DEVICES);
					return -1;
				}
				break;
			case 's':
				if (device_count < MAX_DEVICES) {
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

	memset(test_forks, 0, sizeof(test_forks));
	for (i=0; i<device_count; i++) {
		test_forks[i] = fork();
		if (test_forks[i] < 0) {
			fprintf(stderr, "Error spawning fork for %s: %s\n", devices[i], strerror(errno));
			syslog(LOG_ERR, "Error spawning fork for %s: %s\n", devices[i], strerror(errno));
			/* Just test the devices we have */
			break;
		}
		/* child */
		if (test_forks[i] == 0) {
			test_device(devices[i], verbose, inject_error_percent);
		}
	}

	/* See if they have finished */
	clock_gettime(CLOCK_REALTIME, &ts);
	start_time = ts.tv_sec;

	while ((finished_count < device_count) && ((start_time + timeout) > ts.tv_sec)) {
		for (i=0; i<device_count; i++) {
			int wstatus;
			pid_t w;

			if (test_forks[i] > 0) {
				w = waitpid(test_forks[i], &wstatus, WUNTRACED | WNOHANG | WCONTINUED);
				if (w < 0) {
					fprintf(stderr, "waitpid on %s failed: %s\n", devices[i], strerror(errno));
					return -1;
				}

				if (w == test_forks[i]) {
					if (WIFEXITED(wstatus)) {
						if (WEXITSTATUS(wstatus) == 0) {
							finished_count++;
							test_forks[i] = 0;
						} else {
							syslog(LOG_ERR, "Error reading from device %s", devices[i]);
							final_score += scores[i];
						}
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
			syslog(LOG_ERR, "Reading from device %s did not complete in %d seconds timeout", devices[i], timeout);
			fprintf(stderr, "Thread for device %s did not complete in time\n", devices[i]);
			final_score += scores[i];
		}
	}

	if (verbose) {
		printf("Final score is %d\n", final_score);
	}
	return final_score;
}
