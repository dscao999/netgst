
CFLAGS += -D_GNU_SOURCE -pthread
CFLAGS += $(shell pkg-config --cflags gstreamer-1.0)
LIBS += $(shell pkg-config --libs gstreamer-1.0)
LDFLAGS += -pthread

.PHONY: all clean

all: netfile netdisp netplay

netdisp: net-gst-display.o netproc.o
	$(LINK.o) $^ $(LIBS) -o $@

netfile: recv-file.o netproc.o
	$(LINK.o) $^ -o $@

netplay: send-file.o
	$(LINK.o) $^ -o $@


clean:
	-rm -f netdisp netfile netplay
	-rm -rf *.o
