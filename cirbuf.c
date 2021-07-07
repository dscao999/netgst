struct cirbuf * cirbuf_init(void)
{
	struct cirbuf *cbuf;

	cbuf = malloc(sizeof(struct cirbuf));
	if (cbuf == NULL) {
		fprintf(stderr, "Out of Memory.\n");
		return NULL;
	}
	cbuf->head = 0;
	cbuf->tail = 0;
	pthread_mutex_init(&cbuf->mutex, NULL);
	pthread_cond_init(&cbuf->cond, NULL);
	return cbuf;
}

void cirbuf_exit(struct cirbuf *cbuf)
{
	pthread_cond_destroy(&cbuf->cond);
	pthread_mutex_destroy(&cbuf->mutex);
	free(cbuf);
}

void cirbuf_insert(struct cirbuf *cbuf, const struct record *c_rec)
{
	int empty;

	pthread_mutex_lock(&cbuf->mutex);
	while (cirbuf_full(cbuf))
		pthread_cond_wait(&cbuf->cond, &cbuf->mutex);
	cbuf->pool[cbuf->head] = c_rec;
	empty = cirbuf_empty(cbuf);
	cbuf->head = cirbuf_head_next(cbuf);
	pthread_mutex_unlock(&cbuf->mutex);
	if (empty)
		pthread_cond_signal(&cbuf->cond);
}

const struct record * cirbuf_consume(struct cirbuf *cbuf)
{
	const struct record *rec;
	int full;

	pthread_mutex_lock(&cbuf->mutex);
	while (cirbuf_empty(cbuf))
		pthread_cond_wait(&cbuf->cond, &cbuf->mutex);
	rec = cbuf->pool[cbuf->tail];
	full = cirbuf_full(cbuf);
	cbuf->tail = cirbuf_tail_next(cbuf);
	pthread_mutex_unlock(&cbuf->mutex);
	if (full)
		pthread_cond_signal(&cbuf->cond);
	return rec;
}

