# mkfiletable — build a mod's filetable.dat from its JSON manifest.
# Built the way tools/mkrom is: plain make, no cmake, no install step.

CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -std=c99
CFLAGS  += -Ivendor/parson

# zlib: the data segment of a ROM a mod sources bytes from is 1173-compressed,
# which is raw deflate behind a five byte header.
LDLIBS  ?= -lz

OBJS    = mkfiletable.o pdft_write.o vendor/parson/parson.o

all: mkfiletable slugmod

mkfiletable: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDLIBS)

# slugmod rewrites a manifest and renames files; mkfiletable never edits either.
# Two contracts that far apart do not belong behind one argv, so they are two
# binaries rather than a subcommand: nothing that runs in a build can reach the
# one that moves files by mistyping a flag.
slugmod: slugmod.o vendor/parson/parson.o
	$(CC) $(CFLAGS) -o $@ slugmod.o vendor/parson/parson.o

# The test driver is not part of the tool; see pdft_test.py.
pdft_reencode: pdft_reencode.o pdft_write.o
	$(CC) $(CFLAGS) -o $@ pdft_reencode.o pdft_write.o

test: mkfiletable pdft_reencode
	python3 pdft_test.py ./pdft_reencode

clean:
	rm -f mkfiletable slugmod pdft_reencode $(OBJS) slugmod.o pdft_reencode.o

.PHONY: all test clean
