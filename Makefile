OBJS=omxiv.o omx_image.o omx_render.o soft_image.o ./libnsbmp/libnsbmp.o ./libnsgif/libnsgif.o
BIN=omxiv.bin
LDFLAGS+=-lilclient -ljpeg -lpng -lrt -ldl -Wl,--gc-sections -s
INCLUDES+=-I./libnsbmp -I./libnsgif -I./libs/ilclient

BUILDVERSION=\"$(shell git rev-parse --short=10 HEAD 2>/dev/null;test $$? -gt 0 && echo UNKNOWN)\"
LIBCURL_NAME=\"$(shell ldconfig -p | grep libcurl | head -n 1 | awk '{print $$1;}' 2>/dev/null)\"
CFLAGS+=-DVERSION=${BUILDVERSION} -DLCURL_NAME=$(LIBCURL_NAME)

include Makefile.include

$(BIN): help.h $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)
	
clean:: 
	@rm -f help.h
	
help.h: README.md
	echo -n "static void printUsage(){printf(\"" > help.h
	sed -n -e '/\#\# Synopsis/,/\#\# / p' README.md | sed -e '1d;$$d' | sed ':a;N;$$!ba;s/\n/\\n/g' | tr '\n' ' ' >> help.h
	echo "\\\\n\");}" >> help.h

ilclient:
	mkdir -p libs
	cp -ru /opt/vc/src/hello_pi/libs/ilclient libs
	make -C libs/ilclient
	
install:
	install $(BIN) /usr/bin/omxiv
	
uninstall:
	rm -f /usr/bin/omxiv

debug: CFLAGS:=$(filter-out -O3,$(CFLAGS)) -Og
debug: LDFLAGS:=$(filter-out -s,$(LDFLAGS))

debug: all
