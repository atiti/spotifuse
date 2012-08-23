/*
    SpotiFUSE: Spotify Filesystem in Userspace
    Copyright (C) 2012  Attila Sukosd <attila.sukosd@gmail.com>

    This program can be distributed under the terms of the GNU GPL.
*/

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <despotify.h>

#define MAX_NAME_LENGTH 255

static const char *hello_str = "Hello World!\n";
static const char *hello_path = "/hello";

int global_argc;
char **global_argv;
struct playlist* rootlist = NULL;
struct playlist* searchlist = NULL;
struct playlist* lastlist = NULL;
struct search_result *search = NULL;
struct album_browse* playalbum = NULL;
struct despotify_session* ds = NULL;

typedef struct {
  char name[MAX_NAME_LENGTH];
  struct track *track;
  struct file *next;
} file_t;

typedef struct {
  char name[MAX_NAME_LENGTH];
  file_t *files;
  struct folder *next;
} folder_t;

folder_t *folders = NULL;

void write_wav_header(char *buf)
{
  char header[] = { 
    0x52, 0x49, 0x46, 0x46, 0x24, 0x40, 0x00, 0x00, 0x57, 0x41, 0x56, 0x45, 0x66, 0x6d, 0x74, 0x20,
    0x10, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x44, 0xac, 0x00, 0x00, 0x10, 0xb1, 0x02, 0x00,
    0x04, 0x00, 0x10, 0x00, 0x64, 0x61, 0x74, 0x61, 0xff, 0xff, 0xff, 0xff
  };  

  memcpy(buf, header, 44);
}

static int spotifs_getattr(const char *path, struct stat *stbuf)
{
    int res = 0;

    memset(stbuf, 0, sizeof(struct stat));
    if(strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
    }
    else {
        stbuf->st_mode = S_IFDIR | 0444;
        stbuf->st_nlink = 2;
        //stbuf->st_size = strlen(hello_str);
    }
    //else
    //    res = -ENOENT;

    return res;
}

folder_t *find_folder(const char *name) {
    folder_t *folder;
    for(folder = folders; folder; folder = folder->next) {
	if (strcmp(name, folder->name) == 0)
		return folder;
    }
    return NULL;
}

static int spotifs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                         off_t offset, struct fuse_file_info *fi)
{
    (void) offset;
    (void) fi;
    folder_t *folder;

    if(strcmp(path, "/") == 0) {
    	filler(buf, ".", NULL, 0);
    	filler(buf, "..", NULL, 0);


    	if (!rootlist)
		rootlist = despotify_get_stored_playlists(ds);
	
    	for(struct playlist *p = rootlist; p; p = p->next)
		filler(buf, p->name, NULL, 0);    
    } else {
	folder = find_folder(path+1);
	if (folder) {
		filler(buf, ".", NULL, 0);
		filler(buf, "..", NULL, 0);
		file_t *file;
		for(file = folder->files; file; file = file->next)
			filler(buf, file->name, NULL, 0);
	} else
		return -ENOENT;
    }

    return 0;
}

static int spotifs_open(const char *path, struct fuse_file_info *fi)
{
    if(strcmp(path, hello_path) != 0)
        return -ENOENT;

    if((fi->flags & 3) != O_RDONLY)
        return -EACCES;

    return 0;
}

static int spotifs_read(const char *path, char *buf, size_t size, off_t offset,
                      struct fuse_file_info *fi)
{
    size_t len;
    (void) fi;
    if(strcmp(path, hello_path) != 0)
        return -ENOENT;

    len = strlen(hello_str);
    if (offset < len) {
        if (offset + size > len)
            size = len - offset;
        memcpy(buf, hello_str + offset, size);
    } else
        size = 0;

    return size;
}

static int spotifs_mkdir(const char *path, mode_t mode) {



    return 0;
}

static struct fuse_operations spotifs_oper = {
    .getattr	= spotifs_getattr,
    .readdir	= spotifs_readdir,
    .open	= spotifs_open,
    .read	= spotifs_read,
    .mkdir	= spotifs_mkdir
};


void callback(struct despotify_session* ds, int signal, void* data, void* callback_data) {
    static int seconds = -1;
    (void)ds; (void)callback_data; /* don't warn about unused parameters */

    switch (signal) {
        case DESPOTIFY_NEW_TRACK: {
            struct track* t = data;
            printf(L"New track: %s / %s (%d:%02d) %d kbit/s\n",
                            t->title, t->artist->name,
                            t->length / 60000, t->length % 60000 / 1000,
                            t->file_bitrate / 1000);
            break;
        }

        case DESPOTIFY_TIME_TELL:
            if ((int)(*((double*)data)) != seconds) {
                seconds = *((double*)data);
                printf(L"Time: %d:%02d\r", seconds / 60, seconds % 60);
            }
            break;

        case DESPOTIFY_END_OF_PLAYLIST:
            printf(L"End of playlist\n");
            break;
    }
}

void *spotifs_start(void *arg) {
    printf("SpotiFS: Started\n");
    if (!despotify_init()) {
	printf("Despotify_init failed!\n");
	return;
    }
    ds = despotify_init_client(callback, NULL, true, true);
    if (!ds) {
	printf("Despotify_init_client() failed!\n");
	return;
    }

    if (!despotify_authenticate(ds, "113204571", "elefant1234")) {
	printf("Despotify authentication failed: %s\n", despotify_get_error(ds));
	despotify_exit(ds);
	return;
    }



    return;    
}

void *fuse_start(void *arg) {
    printf("FUSE: Started SpotiFS\n");
    fuse_main(global_argc, global_argv, &spotifs_oper);
    return;
}

int main(int argc, char *argv[])
{
    pthread_t spotifs_thread, fuse_thread;
    global_argc = argc;
    global_argv = argv;

    pthread_create(&spotifs_thread, NULL, spotifs_start, NULL);
    pthread_create(&fuse_thread, NULL, fuse_start, NULL);

    pthread_join(spotifs_thread, NULL);
    pthread_join(fuse_thread, NULL);

    return 0;
}


