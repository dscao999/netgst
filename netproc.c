#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <pthread.h>
#include <poll.h>
#include <stdlib.h>
#include <unistd.h>
#include "netproc.h"

static int prepare_net(const char *port)
{
	struct addrinfo hints, *adrlst;
	int sysret, retv = 0;
	int sock;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE|AI_NUMERICSERV;
	sysret = getaddrinfo(NULL, port, &hints, &adrlst);
	if (sysret != 0) {
		fprintf(stderr, "getaddrinfo failed: %s\n",
				gai_strerror(sysret));
		retv = -10;
		return retv;
	}
	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock == -1) {
		fprintf(stderr, "socket failed: %s\n", strerror(errno));
		retv = -11;
		goto err_exit_10;
	}
	sysret = bind(sock, adrlst->ai_addr, adrlst->ai_addrlen);
	if (sysret == -1) {
		fprintf(stderr, "bind failed: %s\n", strerror(errno));
		retv = -12;
		goto err_exit_20;
	}
	freeaddrinfo(adrlst);
	return sock;

err_exit_20:
	close(sock);
err_exit_10:
	freeaddrinfo(adrlst);
	return retv;
}

void net_processing(struct commarg *arg)
{
	int lsock, sock, sysret;
	unsigned long numpkts;
	char *buf;
	int curlen, maxlen, err;
	struct pollfd pfd, pfd1;

	lsock = prepare_net(arg->port);
	if (lsock < 0) {
		fprintf(stderr, "Cannot initialize socket for receiving\n");
		return;
	}
	sysret = listen(lsock, 5);
	if (sysret == -1) {
		fprintf(stderr, "Cannot listen to the socket: %s\n",
				strerror(errno));
		goto exit_10;
	}
	pfd.fd = lsock;
	pfd.events = POLLIN;
	do {
		sysret = poll(&pfd, 1, 500);
		if (sysret == 0)
			continue;
		else if (sysret == -1) {
			fprintf(stderr, "poll accept failed: %s\n",
					strerror(errno));
			break;
		}
		sock = accept(lsock, NULL, NULL);
	} while (sysret == 0 && *arg->g_exit == 0);
	if (*arg->g_exit != 0 || sock == -1 || sysret == -1) {
		if (sock == -1)
			fprintf(stderr, "accept failed: %s\n", strerror(errno));
		pthread_mutex_lock(arg->mutex);
		*arg->start = 1;
		pthread_cond_signal(arg->cond);
		pthread_mutex_unlock(arg->mutex);
		goto exit_15;
	}

	pfd.fd = sock;
	pfd.events = POLLIN;
	maxlen = 4096;
	buf = malloc(maxlen);
	do {
		sysret = poll(&pfd, 1, 500);
		if (sysret == 0)
			continue;
		else if (sysret == -1) {
			fprintf(stderr, "poll recv failed: %s\n",
					strerror(errno));
			break;
		}
		curlen = recv(sock, buf, maxlen, 0);
	} while (sysret == 0 && *arg->g_exit == 0);
	if (curlen == -1 || *arg->g_exit != 0) {
		if (curlen == -1)
			fprintf(stderr, "recv failed at begining: %s\n",
					strerror(errno));
		pthread_mutex_lock(arg->mutex);
		*arg->start = 1;
		pthread_cond_signal(arg->cond);
		pthread_mutex_unlock(arg->mutex);
		goto exit_20;
	}
	numpkts = curlen;
	sysret = write(arg->dstfd, buf, curlen);
	pthread_mutex_lock(arg->mutex);
	*arg->start = 1;
	pthread_cond_signal(arg->cond);
	pthread_mutex_unlock(arg->mutex);
	if (sysret == -1) {
		fprintf(stderr, "pipe write failed at start: %s\n",
				strerror(errno));
		goto exit_20;
	}

	err = 0;
	pfd1.fd = arg->dstfd;
	pfd1.events = POLLOUT;
	do {
		curlen = recv(sock, buf, maxlen, MSG_DONTWAIT);
		if (curlen == -1) {
			if (errno != EAGAIN && errno != EWOULDBLOCK) {
				fprintf(stderr, "recv failed at %lu: %s\n",
						numpkts, strerror(errno));
				break;
			}
			do
				sysret = poll(&pfd, 1, 500);
			while (sysret == 0 && *arg->g_exit == 0);
			if (sysret == -1) {
				err = 1;
				fprintf(stderr, "poll sock receive failed: %s\n",
						strerror(errno));
				break;
			}
			continue;
		} else if (curlen == 0 || *arg->g_exit != 0)
			break;

		numpkts += curlen;
		do {
			sysret = poll(&pfd1, 1, 500);
			if (sysret == 0)
				continue;
			else if (sysret == -1) {
				fprintf(stderr, "pipe poll failed: %s\n",
						strerror(errno));
				err = 1;
				break;
			}
			sysret = write(arg->dstfd, buf, curlen);
			if (sysret == -1) {
				if (errno == EINTR)
					continue;
				fprintf(stderr, "write pipe failed at %lu: %s\n",
						numpkts, strerror(errno));
				goto exit_30;
			}
			curlen -= sysret;
		} while(curlen > 0 && *arg->g_exit == 0);
	} while (*arg->g_exit == 0 && err == 0);

exit_30:
	printf("Total number of bytes received: %lu\n", numpkts);
exit_20:
	close(sock);
exit_15:
	close(arg->dstfd);
exit_10:
	close(lsock);
}
