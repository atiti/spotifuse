APP_NAME=spotifuse
CC=gcc
CFLAGS=-ggdb -O2 -std=gnu99 -D_FILE_OFFSET_BITS=64 -DFUSE_USE_VERSION=22
LDFLAGS=-lfuse -ldespotify
CORE_OBJS=item_mgr.o spotifuse.o


%.o: %.c
	$(CC) -fvisibility=hidden $(CFLAGS) -c -o $@ $<
all: $(CORE_OBJS)
	$(CC) -o $(APP_NAME) $(LDFLAGS) $(CORE_OBJS)

clean:
	rm -rf $(CORE_OBJS) $(APP_NAME)
	
