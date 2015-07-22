OBJS=omxiv.o OmxImage.o OmxRender.o SoftImage.o
BIN=omxiv.bin
LDFLAGS+=-lilclient -ljpeg -lpng

BUILDVERSION=$(shell git rev-parse --short=10 HEAD 2>/dev/null;test $$? -gt 0 && echo UNKNOWN)
CFLAGS+=-DVERSION=${BUILDVERSION}

include Makefile.include

$(BIN): $(OBJS)
	$(CC) -o $@ -s -Wl,--no-whole-archive -Wl,--whole-archive $(OBJS) $(LDFLAGS) -Wl,--no-whole-archive -rdynamic

ilclient:
	mkdir libs
	cp -r /opt/vc/src/hello_pi/libs/ilclient libs
	cd libs/ilclient; make
	
install:
	cp $(BIN) /usr/bin/omxiv
	
uninstall:
	rm -f /usr/bin/omxiv
