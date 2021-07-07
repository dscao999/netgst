#define CIR_BUFLEN  8192
#define CIR_BUFMASK (CIR_BUFLEN - 1)

struct record {
	unsigned short maxlen, curlen;
	char buf[1500];
};

struct cirbuf {
	volatile int head, tail;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	const struct record *pool[CIR_BUFLEN];
};

static inline int cirbuf_tail_next(const struct cirbuf *cbuf)
{
	return ((cbuf->tail + 1) & CIR_BUFMASK);
}

static inline int cirbuf_head_next(const struct cirbuf *cbuf)
{
	return ((cbuf->head + 1) & CIR_BUFMASK);
}

static inline int cirbuf_full(const struct cirbuf *cbuf)
{
	return cirbuf_head_next(cbuf) == cbuf->tail;
}

static inline int cirbuf_empty(const struct cirbuf *cbuf)
{
	return (cbuf->head == cbuf->tail);
}

static inline int cirbuf_capacity(const struct cirbuf *cbuf)
{
	int dist = cbuf->tail - cbuf->head;
	if (dist <= 0)
		dist += CIR_BUFLEN;
	return dist;
}

struct cirbuf * cirbuf_init(void);
void cirbuf_exit(struct cirbuf *cbuf);
void cirbuf_insert(struct cirbuf *cbuf, const struct record *c_rec);
const struct record * cirbuf_consume(struct cirbuf *cbuf);

