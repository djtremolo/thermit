#Makefile for embedded Kermit.
#
# Copyright (C) 1995, 2011,
#  Trustees of Columbia University in the City of New York.
#  All Rights Reserved.  See kermit.c for license.

#OBJS= main.o kermit.o ioLinux.o
OBJS= main.o ioLinux.o

EK = makewhat
ALL = $(EK)

all: $(ALL)

ek: $(OBJS)
	$(CC) $(CFLAGS) -o thermit $(OBJS)

#Dependencies

main.o: main.c thermit.h ioAPI.h

#kermit.o: kermit.c cdefs.h debug.h kermit.h

ioLinux.o: ioLinux.c thermit.h ioAPI.h

#Targets

#Build with cc.
cc:
	make ek

#Build with gcc.
gcc:
	@UNAME=`uname` ; make "CC=gcc" "CC2=gcc" "CFLAGS=-D$$UNAME -O0 -ggdb" ek

#Ditto but no debugging.
gccnd:
	make "CC=gcc" "CC2=gcc" "CFLAGS=-DNODEBUG -O2" ek

#Build with gcc, Receive-Only, minimum size and features.
gccmin:
	make "CC=gcc" "CC2=gcc" \
	"CFLAGS=-DMINSIZE -DOBUFLEN=256 -DFN_MAX=16 -O2" ek

#Ditto but Receive-Only:
gccminro:
	make "CC=gcc" "CC2=gcc" \
	"CFLAGS=-DMINSIZE -DOBUFLEN=256 -DFN_MAX=16 -DRECVONLY -O2" ek

#Minimum size, receive-only, but with debugging:
gccminrod:
	make "CC=gcc" "CC2=gcc" \
	"CFLAGS=-DMINSIZE -DOBUFLEN=256 -DFN_MAX=16 -DRECVONLY -DDEBUG -O2" ek

#HP-UX 9.0 or higher with ANSI C.
hp:
	make "SHELL=/usr/bin/sh" CC=/opt/ansic/bin/cc CC2=/opt/ansic/bin/cc \
	ek "CFLAGS=-DHPUX -Aa"

#To get profile, build this target, run it, then "gprof ./ek > file".
gprof:
	make "CC=gcc" "CC2=gcc" ek "CFLAGS=-DNODEBUG -pg" "LNKFLAGS=-pg"

clean:
	rm -f $(OBJS) core

makewhat:
	@echo 'Defaulting to gcc...'
	make gcc

#End of Makefile
