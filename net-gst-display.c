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
#include <gst/gst.h>
#include "netproc.h"

/*void wait_udp_start(int port); */

struct CustomData {
	GstElement *pipeline;
	GstElement *source;
	GstElement *decoder;
	GstElement *a_convert, *v_convert;
	GstElement *resample;
	GstElement *a_sink, *v_sink;
	gboolean playing;
	gboolean seek_enabled;
	gboolean seek_done;
	gint64 duration;
	gint64 current;
	volatile int *terminate;
};

static volatile int global_exit = 0;
static volatile int print_current = 0;
static void sig_handler(int sig)
{
	if (sig == SIGUSR1 || sig == SIGUSR2)
		print_current = 1;
	else if (sig == SIGINT || sig == SIGTERM)
		global_exit = 1;
}

static void pad_added_handler(GstElement *src, GstPad *pad, struct CustomData *data);

static void query_seek_prop(struct CustomData *data)
{
	GstQuery *query;
	gint64 start, end;
	query = gst_query_new_seeking(GST_FORMAT_TIME);
	if (!gst_element_query(data->v_sink, query)) {
		g_printerr("Seeking query failed.");
		gst_query_unref(query);
		return;
	}

	gst_query_parse_seeking(query, NULL, &data->seek_enabled, &start, &end);
	if (data->seek_enabled) {
		g_print("Seeking is ENABLED from %" GST_TIME_FORMAT " to %" \
				GST_TIME_FORMAT "\n",
				GST_TIME_ARGS(start), GST_TIME_ARGS(end));
	} else
		g_print("Seeking is DISABLED for this stream.\n");
	gst_query_unref(query);
}

static void gst_mesg_check(GstMessage *msg, struct CustomData *data)
{
	GError *err;
	gchar *debug_info;
	GstState old_state, new_state, pending_state;

	switch(GST_MESSAGE_TYPE(msg)) {
	case GST_MESSAGE_ERROR:
		gst_message_parse_error(msg, &err, &debug_info);
		g_printerr("Error received from element %s: %s\n",
			       	GST_OBJECT_NAME (msg->src), err->message);
		g_printerr("Debugging information: %s\n", \
				debug_info ? debug_info : "none");
		g_clear_error (&err);
		g_free (debug_info);
		*data->terminate = 1;
		break;
	case GST_MESSAGE_EOS:
		g_print ("End-Of-Stream reached.\n");
		*data->terminate = 1;
		break;
	case GST_MESSAGE_STATE_CHANGED:
		if (GST_MESSAGE_SRC(msg) != GST_OBJECT(data->pipeline))
			break;
		gst_message_parse_state_changed(msg, &old_state, &new_state, &pending_state);
		g_print ("Pipeline state changed from %s to %s:\n",
				gst_element_state_get_name(old_state),
				gst_element_state_get_name(new_state));
		data->playing = (new_state == GST_STATE_PLAYING);
		if (data->playing)
			query_seek_prop(data);
		break;
	default:
		/* We should not reach here because we only asked for ERRORs and EOS */
		g_printerr ("Unexpected message received.\n");
		break;
	}
	gst_message_unref(msg);
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
	struct CustomData data;
	struct commarg tharg;
	GstBus *bus;
	GstMessage *msg;
	GstStateChangeReturn ret;
	GstMessageType mesg;
	struct sigaction mact;
	int pfd[2], sysret, retv = 0;
	pthread_t netsrc;
	volatile int play;
	pthread_mutex_t mutex;
	pthread_cond_t cond;

	pthread_mutex_init(&mutex, NULL);
	pthread_cond_init(&cond, NULL);
	memset(&data, 0, sizeof(data));
	data.duration = GST_CLOCK_TIME_NONE;

	data.terminate = &global_exit;
	gst_init(&argc, &argv);
	if (argc > 1)
		tharg.port = argv[1];
	else
		tharg.port = "7800";

	memset(&mact, 0, sizeof(mact));
	mact.sa_handler = sig_handler;
	if (sigaction(SIGUSR1, &mact, NULL) == -1 ||
			sigaction(SIGUSR2, &mact, NULL) == -1 ||
			sigaction(SIGINT, &mact, NULL) == -1 ||
			sigaction(SIGTERM, &mact, NULL) == -1)
		fprintf(stderr, "Cannot install signal handler: %s\n",
				strerror(errno));

	sysret = pipe(pfd);
	if (sysret == -1) {
		fprintf(stderr, "Cannot create pipe: %s\n", strerror(errno));
		retv = 2;
		goto exit_10;
	}
	tharg.dstfd = pfd[1];
	tharg.start = &play;
	tharg.g_exit = &global_exit;
	tharg.mutex = &mutex;
	tharg.cond = &cond;

	data.source = gst_element_factory_make("fdsrc", "source");
	data.decoder = gst_element_factory_make("decodebin", "decoder");
	data.a_convert = gst_element_factory_make("audioconvert", "a_convert");
	data.resample = gst_element_factory_make("audioresample", "resample");
	data.a_sink = gst_element_factory_make("autoaudiosink", "a_sink");
	data.v_convert = gst_element_factory_make("videoconvert", "v_convert");
	data.v_sink = gst_element_factory_make("autovideosink", "v_sink");

	data.pipeline = gst_pipeline_new("test-pipeline");
	if (!data.source || !data.a_sink || !data.a_convert || !data.v_convert ||
			!data.resample || !data.pipeline || !data.v_sink || !data.decoder) {
		g_printerr("Not all elements could be created.\n");
		retv = 4;
		goto exit_25;
	}

	gst_bin_add_many(GST_BIN(data.pipeline), data.source, data.decoder, data.a_convert,
			data.resample, data.a_sink, data.v_convert, data.v_sink, NULL);
	if (gst_element_link_many(data.a_convert, data.resample, data.a_sink, NULL) != TRUE) {
		g_printerr ("Elements could not be linked.\n");
		gst_object_unref (data.pipeline);
		retv = 4;
		goto exit_30;
	}
	if (gst_element_link_many(data.v_convert, data.v_sink, NULL) != TRUE) {
		g_printerr ("Elements could not be linked.\n");
		gst_object_unref (data.pipeline);
		retv = 4;
		goto exit_30;
	}
	if (gst_element_link_many(data.source, data.decoder, NULL) != TRUE) {
		g_printerr ("Elements could not be linked.\n");
		gst_object_unref (data.pipeline);
		retv = 4;
		goto exit_30;
	}

	g_object_set(data.source, "fd", (gint)pfd[0], NULL);
	g_signal_connect(data.decoder, "pad-added", G_CALLBACK(pad_added_handler), &data);

	play = 0;
	sysret = pthread_create(&netsrc, NULL, &net_receiver, &tharg);
	if (sysret) {
		fprintf(stderr, "Cannot create udp receiver: %s\n",
				strerror(errno));
		retv = 3;
	}
	pthread_mutex_lock(&mutex);
	while (play == 0)
		pthread_cond_wait(&cond, &mutex);
	pthread_mutex_unlock(&mutex);
	ret = gst_element_set_state(data.pipeline, GST_STATE_PLAYING);
	if (ret == GST_STATE_CHANGE_FAILURE) {
		g_printerr("Unable to set the pipeline to the playing state.\n");
		gst_object_unref (data.pipeline);
		retv = 5;
		goto exit_50;
	}

	bus = gst_element_get_bus(data.pipeline);
	do {
		mesg = GST_MESSAGE_STATE_CHANGED|GST_MESSAGE_ERROR|
			GST_MESSAGE_EOS|GST_MESSAGE_DURATION;
		msg = gst_bus_timed_pop_filtered(bus, 200 * GST_MSECOND, mesg);
		if (msg) {
			gst_mesg_check(msg, &data);
			continue;
		}
		if (GST_CLOCK_TIME_IS_VALID(data.duration)) {
			if (!print_current)
				continue;
			if (!gst_element_query_position (data.v_sink,
						GST_FORMAT_TIME, &data.current))
				g_printerr ("Could not query current position.\n");
			g_print("Current at: %" GST_TIME_FORMAT "\n",
					GST_TIME_ARGS(data.current));
			print_current = 0;
			continue;
		}

		if (!data.seek_enabled)
			continue;
		if (!gst_element_query_duration(data.v_sink,
					GST_FORMAT_TIME, &data.duration))
			g_printerr("Could not query current duration.\n");
		g_print("Duration: %" GST_TIME_FORMAT "\n",
				GST_TIME_ARGS(data.duration));
	} while(!(*data.terminate));

	gst_object_unref(bus);
	gst_element_set_state(data.pipeline, GST_STATE_NULL);

exit_50:
	pthread_join(netsrc, NULL);

exit_30:
	gst_object_unref(data.pipeline);
exit_25:
	close(pfd[0]);
	close(pfd[1]);

exit_10:
	pthread_mutex_destroy(&mutex);
	pthread_cond_destroy(&cond);
	return retv;
}

static void pad_added_handler(GstElement *src, GstPad *new_pad, struct CustomData *data)
{
	GstPad *sink_pad = NULL;
	GstPadLinkReturn ret;
	GstCaps *new_pad_caps = NULL;
	GstStructure *new_pad_struct = NULL;
	const gchar *new_pad_type = NULL;


	g_print("Received new pad '%s' from '%s':\n",
			GST_PAD_NAME(new_pad), GST_ELEMENT_NAME(src));

	new_pad_caps = gst_pad_get_current_caps(new_pad);
	new_pad_struct = gst_caps_get_structure(new_pad_caps, 0);
	new_pad_type = gst_structure_get_name(new_pad_struct);
	if (g_str_has_prefix(new_pad_type, "audio/x-raw")) {
		sink_pad = gst_element_get_static_pad(data->a_convert, "sink");
		if (gst_pad_is_linked(sink_pad)) {
			g_print("Audio already linked. Ignored.\n");
			goto exit_10;
		}
	} else if (g_str_has_prefix(new_pad_type, "video/x-raw")) {
	       sink_pad = gst_element_get_static_pad(data->v_convert, "sink");
	       if (gst_pad_is_linked(sink_pad)) {
		       g_print("Video already linked. Ignored.\n");
		       goto exit_10;
	       }
	} else {
		g_print("It has type '%s' which is not expected. Ignoring.\n", new_pad_type);
		goto exit_10;
	}

	ret = gst_pad_link(new_pad, sink_pad);
	if (GST_PAD_LINK_FAILED(ret))
		g_print("Type is '%s' but link failed.\n", new_pad_type);
	else
		g_print("Link succeeded (type '%s').\n", new_pad_type);

exit_10:
	if (new_pad_caps)
		gst_caps_unref (new_pad_caps);
	if (sink_pad)
		gst_object_unref(sink_pad);
}
