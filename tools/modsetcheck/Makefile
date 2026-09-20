# modsetcheck — check a set of mods before shipping it.
# Built the way tools/mkfiletable and tools/mkrom are: plain make, no cmake.
#
# pdft_read.c lives beside pdft_write.c in tools/mkfiletable, because the
# decoder and the encoder are a pair and the layout they agree on is one thing.

CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -std=c99
CFLAGS  += -I../mkfiletable -I../../port/include

OBJS    = modsetcheck.o pdft_read.o

# The engine's own stage names and the vanilla file list, turned into a header
# at build time so this tool resolves a stage spec the way the loader does and
# cannot drift from the tables it is checking against. Not committed.
CONSTANTS  = ../../src/include/constants.h
STAGETABLE = ../../src/game/stagetable.c
VANILLA    = ../share/pd-ntsc-final/files.json

all: modsetcheck

modsetcheck.o: modsetcheck.c stagenames.h

stagenames.h: gen-stagenames.sh $(CONSTANTS) $(STAGETABLE) $(VANILLA)
	sh gen-stagenames.sh $(CONSTANTS) $(STAGETABLE) $(VANILLA) > $@.tmp && mv $@.tmp $@

modsetcheck: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

pdft_read.o: ../mkfiletable/pdft_read.c ../mkfiletable/pdft_read.h ../mkfiletable/pdft_write.h
	$(CC) $(CFLAGS) -c -o $@ ../mkfiletable/pdft_read.c

clean:
	rm -f modsetcheck $(OBJS) stagenames.h stagenames.h.tmp

.PHONY: all clean
