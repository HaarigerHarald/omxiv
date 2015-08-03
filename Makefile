OBJS=omxiv.o OmxImage.o OmxRender.o SoftImage.o ./libnsbmp/libnsbmp.o
BIN=omxiv.bin
LDFLAGS+=-lilclient -ljpeg -lpng
INCLUDES+=-I./libnsbmp -I./libs/ilclient

# Optional http image displaying with libcurl-dev.
# If you want to compile without libcurl comment it out.
OBJS+= HttpImage.o
LDFLAGS+= -lcurl
CFLAGS+= -DUSE_LIBCURL

BUILDVERSION=$(shell git rev-parse --short=10 HEAD 2>/dev/null;test $$? -gt 0 && echo UNKNOWN)
CFLAGS+=-DVERSION=${BUILDVERSION}

include Makefile.include

$(BIN): $(OBJS)
	$(CC) -o $@ -s $(OBJS) $(LDFLAGS)

ilclient:
	mkdir -p libs
	cp -ru /opt/vc/src/hello_pi/libs/ilclient libs
	make -C libs/ilclient
	
install:
	install $(BIN) /usr/bin/omxiv
	
uninstall:
	rm -f /usr/bin/omxiv
