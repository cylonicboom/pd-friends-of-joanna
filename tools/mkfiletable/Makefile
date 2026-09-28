# mkfiletable — build a mod's filetable.dat from its JSON manifest.
# Built the way tools/mkrom is: plain make, no cmake, no install step.

CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -std=c99
CFLAGS  += -Ivendor/parson -I../../port/include -I../../include

# zlib: the data segment of a ROM a mod sources bytes from is 1173-compressed,
# which is raw deflate behind a five byte header.
LDLIBS  ?= -lz

# rompatch: a patched romSource is built here the same way the engine builds
# it, from the same decoder, so the offsets this tool writes are offsets into
# the image the engine will mount. ultratypes wants -D_LANGUAGE_C outside a
# port build.
OBJS    = mkfiletable.o pdft_write.o rompatch.o vendor/parson/parson.o

rompatch.o: ../../port/src/rompatch.c ../../port/include/rompatch.h
	$(CC) $(CFLAGS) -D_LANGUAGE_C -c -o $@ ../../port/src/rompatch.c

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

# The name rules are header-only, so this links nothing but the test itself.
pdftrules_test: pdftrules_test.c ../../port/include/pdftrules.h
	$(CC) $(CFLAGS) -o $@ pdftrules_test.c

test: mkfiletable pdft_reencode pdftrules_test
	./pdftrules_test
	python3 pdft_test.py ./pdft_reencode

clean:
	rm -f mkfiletable slugmod pdft_reencode pdftrules_test $(OBJS) slugmod.o pdft_reencode.o pdft_read.o

.PHONY: all test clean
