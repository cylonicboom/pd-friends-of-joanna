# modsetcheck — check a set of mods before shipping it.
# Built the way tools/mkfiletable and tools/mkrom are: plain make, no cmake.
#
# pdft_read.c lives beside pdft_write.c in tools/mkfiletable, because the
# decoder and the encoder are a pair and the layout they agree on is one thing.

CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -std=c99
CFLAGS  += -I../mkfiletable -I../../port/include

OBJS    = modsetcheck.o pdft_read.o

all: modsetcheck

modsetcheck: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

pdft_read.o: ../mkfiletable/pdft_read.c ../mkfiletable/pdft_read.h ../mkfiletable/pdft_write.h
	$(CC) $(CFLAGS) -c -o $@ ../mkfiletable/pdft_read.c

clean:
	rm -f modsetcheck $(OBJS)

.PHONY: all clean
