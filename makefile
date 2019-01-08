#OBJS= main.o thermit.o crc.o streamFraming.o ioDummy.o msgBuf.o
OBJS= main.o thermit.o crc.o streamFraming.o ioLinux.o msgBuf.o

THERMIT = makewhat
ALL = $(THERMIT)

all: $(ALL)

thermit: $(OBJS)
	$(CC) $(CFLAGS) -o thermit $(OBJS)

#Dependencies
main.o: main.c
thermit.o: thermit.c
streamFraming.o: streamFraming.c
crc.o: crc.c
msgBuf.o: msgBuf.c
ioLinux.o: ioLinux.c
ioDummy.o: ioDummy.c

#Targets

#Build with gcc.
gcc:
	make "CC=gcc" "CC2=gcc" "CFLAGS=-O0 -ggdb -Wl,-Map,out.map" thermit

#Ditto but no debugging.
gccnd:
	make "CC=gcc" "CC2=gcc" "CFLAGS=-DTHERMIT_NO_DEBUG -Os -Wl,-Map,out.map" thermit

clean:
	rm -f $(OBJS) core

makewhat:
	@echo 'Defaulting to gcc...'
	make gcc

#End of Makefile
