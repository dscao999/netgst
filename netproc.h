#ifndef UDP_PROC_DSCAO__
#define UDP_PROC_DSCAO__

struct commarg {
	int dstfd;
	volatile int *start;
	volatile int *g_exit;
	pthread_mutex_t *mutex;
	pthread_cond_t *cond;
	const char *port;
};

void net_processing(struct commarg *arg);

#endif  /* UDP_PROC_DSCAO__ */
