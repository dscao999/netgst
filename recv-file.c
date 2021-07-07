#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>
#include <assert.h>
#include "netproc.h"

static volatile int global_exit = 0;
static void sig_handler(int sig)
{
	if (sig == SIGINT || sig == SIGTERM)
		global_exit = 1;
}

static void * net_receiver(void *dat)
{
	struct commarg *tharg = (struct commarg *)dat;

	net_processing(tharg);

	*tharg->start = -1;
	return NULL;
}

int main(int argc, char *argv[])
{
	struct commarg tharg;
	struct sigaction mact;
	int pfd[2], sysret, retv = 0;
	int pin, c, finish;
	ssize_t numb;
	pthread_t netsrc;
	volatile int play;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	FILE *fout;
	char *buf;
	const char *fname;
	extern char *optarg;
	extern int optind, opterr, optopt;
	static const struct timespec itv = {.tv_sec = 0, .tv_nsec = 40000000};

	tharg.port = NULL;
	fname = NULL;
	finish = 0;
	opterr = 0;
	do {
		c = getopt(argc, argv, ":p:");
		switch(c) {
		case '?':
			fprintf(stderr, "Unknown option: %c\n", (char)optopt);
			break;
		case ':':
			fprintf(stderr, "Missing argument for %c\n",
					(char)optopt);
			break;
		case 'p':
			tharg.port = optarg;
			break;
		case -1:
			finish = 1;
			break;
		default:
			assert(0);
		}
	} while (finish == 0);
	if (tharg.port == NULL)
		tharg.port = "7800";
	if (argc > optind)
		fname = argv[optind];
	else
		fname = "/tmp/play.dat";

	buf = malloc(2048);
	pthread_mutex_init(&mutex, NULL);
	pthread_cond_init(&cond, NULL);

	memset(&mact, 0, sizeof(mact));
	mact.sa_handler = sig_handler;
	if (sigaction(SIGINT, &mact, NULL) == -1 ||
			sigaction(SIGTERM, &mact, NULL) == -1)
		fprintf(stderr, "Cannot install signal handler: %s\n",
				strerror(errno));

	fout = fopen(fname, "wb");
	if (!fout) {
		fprintf(stderr, "Cannot open %s for writing: %s\n",
				fname, strerror(errno));
		retv = 1;
		goto exit_10;
	}
	sysret = pipe(pfd);
	if (sysret == -1) {
		fprintf(stderr, "Cannot create pipe: %s\n", strerror(errno));
		retv = 2;
		goto exit_15;
	}
	tharg.dstfd = pfd[1];
	tharg.start = &play;
	tharg.g_exit = &global_exit;
	tharg.mutex = &mutex;
	tharg.cond = &cond;

	play = 0;
	sysret = pthread_create(&netsrc, NULL, &net_receiver, &tharg);
	if (sysret) {
		fprintf(stderr, "Cannot create udp receiver: %s\n",
				strerror(errno));
		retv = 3;
		goto exit_40;
	}
	pthread_mutex_lock(&mutex);
	while (play == 0 && global_exit == 0)
		pthread_cond_wait(&cond, &mutex);
	pthread_mutex_unlock(&mutex);
	if (global_exit == 0)
		printf("Start playing...\n");
	else
		goto exit_50;

	pin = pfd[0];
	do {
		numb = read(pin, buf, 2048);
		if (numb == -1) {
			if (errno == EINTR)
				continue;
			fprintf(stderr, "pipe read failed: %s\n", strerror(errno));
			break;
		} else if (numb == 0) {
			printf("End of PIPE\n");
			break;
		}
		sysret = fwrite(buf, 1, numb, fout);
		if (sysret == -1) {
			fprintf(stderr, "fwrite failed: %s\n", strerror(errno));
			play = -1;
			break;
		}
		nanosleep(&itv, NULL);
	} while(global_exit == 0);
	close(pin);
	printf("global_exit: %d\n", global_exit);

exit_50:
	pthread_join(netsrc, NULL);
exit_40:
	close(pfd[0]);
	close(pfd[1]);
exit_15:
	fclose(fout);
exit_10:
	pthread_cond_destroy(&cond);
	pthread_mutex_destroy(&mutex);
	free(buf);
	return retv;
}
