CC=gcc
CFLAGS=-ggdb -O0 -std=gnu99 -D_FILE_OFFSET_BITS=64 -DFUSE_USE_VERSION=22
LDFLAGS=-lfuse -ldespotify

all:
	$(CC) $(CFLAGS) $(LDFLAGS) spotifuse.c -o spotifuse

clean:
	rm -rf spotifuse
	
