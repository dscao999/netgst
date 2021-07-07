#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <poll.h>
#include <assert.h>
#include <time.h>
#include "netproc.h"

static volatile int global_exit = 0;
static void sig_handler(int sig)
{
	if (sig == SIGINT || sig == SIGTERM)
		global_exit = 1;
}

int main(int argc, char *argv[])
{
	struct sigaction mact;
	int sock, sysret, retv = 0;
	FILE *fin;
	ssize_t numb;
	int c, finish;
	unsigned long numpkts;
	const char *fname, *port, *svrip;
	char *buf;
	int buflen, len;
	extern char *optarg;
	extern int optind, opterr, optopt;

	svrip = NULL;
	port = NULL;
	opterr = 0;
	finish = 0;
	do {
		c = getopt(argc, argv, ":s:p:");
		switch(c) {
		case '?':
			fprintf(stderr, "Unknown option: %c\n", (char)optopt);
			break;
		case ':':
			fprintf(stderr, "Missing argument for %c\n",
					(char)optopt);
			break;
		case 's':
			svrip = optarg;
			break;
		case 'p':
			port = optarg;
			break;
		case -1:
			finish = 1;
			break;
		default:
			assert(0);
		}
	} while (finish == 0);
	if (optind == argc) {
		fprintf(stderr, "Missing file name.\n");
		fprintf(stderr, "Usage: %s filename\n", argv[0]);
		return 1;
	}
	if (!svrip)
		svrip = "localhost";
	if (!port)
		port = "7800";
	if (argc > optind)
		fname = argv[optind];
	else
		fname = "/etc/passwd";

	memset(&mact, 0, sizeof(mact));
	mact.sa_handler = sig_handler;
	if (sigaction(SIGINT, &mact, NULL) == -1 ||
			sigaction(SIGTERM, &mact, NULL) == -1)
		fprintf(stderr, "Cannot install signal handler: %s\n",
				strerror(errno));
	fin = fopen(fname, "rb");
	if (!fin) {
		fprintf(stderr, "Cannot open file %s: %s\n", fname,
				strerror(errno));
		return 2;
	}

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock == -1) {
		fprintf(stderr, "Cannot create a socket: %s\n", strerror(errno));
		retv = 3;
		goto exit_10;
	}

	struct addrinfo hints, *adrlst;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_NUMERICSERV;
	sysret = getaddrinfo(svrip, port, &hints, &adrlst);
	if (sysret != 0) {
		fprintf(stderr, "getaddrinfo failed: %s\n",
				gai_strerror(sysret));
		retv = 4;
		goto exit_20;
	}
	sysret = connect(sock, adrlst->ai_addr, adrlst->ai_addrlen);
	if (sysret == -1) {
		fprintf(stderr, "Connect failed: %s\n", strerror(errno));
		goto exit_30;
	}

	buflen = 4096;
	buf = malloc(buflen);
	numpkts = 0;
	numb = fread(buf, 1, buflen, fin);
	while (numb > 0 && global_exit == 0) {
		len = numb;
		do {
			sysret = send(sock, buf, numb, 0);
			if (sysret == -1) {
				if (errno == EINTR)
					continue;
				fprintf(stderr, "UDP send failed at offset " \
						"%lu: %s\n", numpkts,
						strerror(errno));
				goto exit_40;
			}
			numb -= sysret;
		} while (numb > 0 && global_exit == 0);
		numpkts += len;
		numb = fread(buf, 1, buflen, fin);
	}

exit_40:
	printf("Total bytes sent: %lu\n", numpkts);
	free(buf);
exit_30:
	freeaddrinfo(adrlst);
exit_20:
	close(sock);
exit_10:
	fclose(fin);
	return retv;
}
