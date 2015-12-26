OBJS=omxiv.o omx_image.o omx_render.o soft_image.o ./libnsbmp/libnsbmp.o ./libnsgif/libnsgif.o
BIN=omxiv.bin
LDFLAGS+=-lilclient -ljpeg -lpng -lrt -ldl -Wl,--gc-sections
INCLUDES+=-I./libnsbmp -I./libnsgif -I./libs/ilclient

BUILDVERSION=\"$(shell git rev-parse --short=10 HEAD 2>/dev/null;test $$? -gt 0 && echo UNKNOWN)\"
LIBCURL_NAME=\"$(shell ldconfig -p | grep libcurl | head -n 1 | awk '{print $$1;}' 2>/dev/null)\"
CFLAGS+=-DVERSION=${BUILDVERSION} -DLCURL_NAME=$(LIBCURL_NAME)

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
