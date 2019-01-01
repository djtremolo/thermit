OBJS= main.o thermit.o crc.o streamFraming.o ioLinux.o msgBuf.o

THERMIT = makewhat
ALL = $(THERMIT)

all: $(ALL)

thermit: $(OBJS)
	$(CC) $(CFLAGS) -o thermit $(OBJS)

#Dependencies

main.o: main.c thermit.h thermitDebug.h

thermit.o: thermit.c thermit.h thermitDebug.h
streamFraming.o: streamFraming.c streamFraming.h thermitDebug.h
crc.o: crc.c crc.h
msgBuf.o: msgBuf.c msgBuf.h

ioLinux.o: ioLinux.c thermit.h streamFraming.h thermitDebug.h

#Targets

#Build with cc.
cc:
	make ek

#Build with gcc.
gcc:
	@UNAME=`uname` ; make "CC=gcc" "CC2=gcc" "CFLAGS=-D$$UNAME -O0 -ggdb -Wl,-Map,out.map" thermit

#Ditto but no debugging.
gccnd:
	make "CC=gcc" "CC2=gcc" "CFLAGS=-DNODEBUG -O2" thermit

#Build with gcc, Receive-Only, minimum size and features.
gccmin:
	make "CC=gcc" "CC2=gcc" \
	"CFLAGS=-DMINSIZE -DOBUFLEN=256 -DFN_MAX=16 -O2" thermit

#Ditto but Receive-Only:
gccminro:
	make "CC=gcc" "CC2=gcc" \
	"CFLAGS=-DMINSIZE -DOBUFLEN=256 -DFN_MAX=16 -DRECVONLY -O2" thermit

#Minimum size, receive-only, but with debugging:
gccminrod:
	make "CC=gcc" "CC2=gcc" \
	"CFLAGS=-DMINSIZE -DOBUFLEN=256 -DFN_MAX=16 -DRECVONLY -DDEBUG -O2" thermit

#HP-UX 9.0 or higher with ANSI C.
hp:
	make "SHELL=/usr/bin/sh" CC=/opt/ansic/bin/cc CC2=/opt/ansic/bin/cc \
	thermit "CFLAGS=-DHPUX -Aa"

#To get profile, build this target, run it, then "gprof ./thermit > file".
gprof:
	make "CC=gcc" "CC2=gcc" thermit "CFLAGS=-DNODEBUG -pg" "LNKFLAGS=-pg"

clean:
	rm -f $(OBJS) core

makewhat:
	@echo 'Defaulting to gcc...'
	make gcc

#End of Makefile
