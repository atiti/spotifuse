/*
    SpotiFUSE: Spotify Filesystem in Userspace
    Copyright (C) 2012  Attila Sukosd <attila.sukosd@gmail.com>

    This program can be distributed under the terms of the GNU GPL.


    WARNING: So far this is pretty much a hack to try to see if things work...
	     so use at your own risk, no quality assurance what so ever.
             Probably will crash a couple of times ;)

Changelog:
  - 0.1 : Initial hack
  - 0.1.1 : Implemented playlist browsing + track browsing + song streaming
  - 0.1.2 : File size estimation
  - 0.1.3 : Rewritten file structure to be using linked lists, fixed wave headers, login passed as option

  
*/

#include <fuse.h>
#include <fuse/fuse_opt.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <despotify.h>
#include "item_mgr.h"

#define PACKAGE_VERSION "0.1.2"

#define MAX_NAME_LENGTH 255


/* 
Lets assume some stuff here:
TODO: make an intelligent way of determining these
*/
#define SAMPLING_RATE 44100
#define NUM_CHANNELS 2
#define SAMPLE_SIZE 2 // (16bit)

struct spotifs_config {
	char *login;
	char *pass;
};

static struct spotifs_config spotifs_config;
static struct fuse_opt spotifs_opts[] = {
	{ "login=%s", offsetof(struct spotifs_config, login), 0 },
	{ "pass=%s", offsetof(struct spotifs_config, pass), 0 }
};

int global_argc;
char **global_argv;
static item_t *root = NULL;
struct playlist* rootlist = NULL;
struct playlist* searchlist = NULL;
struct playlist* lastlist = NULL;
struct search_result *search = NULL;
struct album_browse* playalbum = NULL;
struct despotify_session* ds = NULL;

// Ugly stuff here... this needs to be done per file descriptor
static char tmp_buff[4096];
static int tmp_buff_size = 0;


int write_wav_header(char *buf, int length)
{
  char header[] = { 
    0x52, 0x49, 0x46, 0x46,  // RIFF
    0x24, 0x40, 0x00, 0x00,  
    0x57, 0x41, 0x56, 0x45, // WAVE
    0x66, 0x6d, 0x74, 0x20, // chunk id: "fmt "
    0x10, 0x00, 0x00, 0x00, // chunk size: 16 bytes
    0x01, 0x00, // compression code: 1 - PCM/uncompressed
    0x02, 0x00, // number of channels: 2
    0x44, 0xac, 0x00, 0x00, // sample rate: 44100
    0x10, 0xb1, 0x02, 0x00, // average bytes per second: 176400 = numchan*samplerate*16bit
    0x04, 0x00, // block align	
    0x10, 0x00, // bits per sample: 16
    0x64, 0x61, 0x74, 0x61 // "data"
  };  

  memcpy(buf, header, 40);
  memcpy(buf+40, (char*)length, 4); // TODO: not endian safe!
  return 44;
}

void strip_slash(char *buff) {
    char *ptr = buff;
    while (*ptr) {
	if (*ptr == '/')
		*ptr = '-';
    	ptr++;
    }
}

item_t* extract_path_item(item_t *root, const char *path) {
    item_t *curr = root;
    item_t *res = NULL;
    char name[255];
    char *ptr = (char *)path, *ptr2;
    int num_folders = 0;
    do {
        ptr = strchr(ptr, '/');
        if (ptr) {
                ptr2 = strchr(ptr+1, '/');
                if (ptr2) {
                        name[0] = '\0';
                        strncat(name, ptr+1, (ptr2-ptr-1));
                } else
                        strcpy(name, ptr+1);
                num_folders++;
                printf("%d: %s\n", num_folders, name);
		curr = find_item_by_name_simple(curr, name);
		if (curr) {
			res = curr;
			curr = (item_t *)curr->data;
		}
                ptr++;
        }
    } while (ptr);
    return res;
}

static int spotifs_getattr(const char *path, struct stat *stbuf)
{
    int res = 0;
    int i,cnt, method_type, root_req;
    struct playlist *cp = rootlist;
    char track_name[250];
    char folder_name[250];
    char *ptr;
    struct track *ct;
    item_t *curr;

    curr = extract_path_item(root, path);
    memset(stbuf, 0, sizeof(struct stat));
 
    if (!curr && strcmp(path, "/") == 0) {
	stbuf->st_mode = S_IFDIR | 0755;
	stbuf->st_nlink = 2;	 
    	return 0;
    } else if (!curr)
	return -ENOENT; 

    if (curr->itype == ITEM_TYPE_DIR) {
	stbuf->st_mode = S_IFDIR | 0755;
	stbuf->st_nlink = 2;
    } else if (curr->itype == ITEM_TYPE_FILE) {
    	stbuf->st_mode = S_IFREG | 0444;
        stbuf->st_nlink = 1;
    	if (curr->udata) {
		ct = (struct track *)curr->udata;
		stbuf->st_size = (ct->length/1000)*SAMPLING_RATE*NUM_CHANNELS*SAMPLE_SIZE;
	} else
        	stbuf->st_size = 0; //(ct->length/1000)*SAMPLING_RATE*NUM_CHANNELS*SAMPLE_SIZE; // TODO: rounding to seconds right now...
    }
    return 0;
}

static int spotifs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
		off_t offset, struct fuse_file_info *fi)
{
    (void) offset;
    (void) fi;
    item_t *folder, *it;
    int i, method_type, root_req;
    struct playlist *cp = rootlist;
    char track_name[250];
    char folder_name[250];
    char *ptr;


    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);


    if (!rootlist) {
	folder = find_item_by_name(root, "Playlists");
    	printf("no rootlist\n");
    	rootlist = despotify_get_stored_playlists(ds);
                                
    	// Find the playlist from the root 
	i = 0;                     
        for(cp = rootlist; cp; cp = cp->next) {
		strip_slash(cp->name);
        	if (!i) {
			it = add_sub_item(folder, cp->name, ITEM_TYPE_DIR, DATA_TYPE_PLAYLIST, (void *)cp);
			i = 1;
		} else 
			add_item(it, cp->name, ITEM_TYPE_DIR, DATA_TYPE_PLAYLIST, (void *)cp);
	}
    }

    if (strcmp(path, "/") == 0) {
	folder = root;
    } else {
    	folder = extract_path_item(root, path);
 	if (folder && folder->data) {
		folder = (item_t *)folder->data;
	} else if (folder) {
		if (folder->dtype == DATA_TYPE_PLAYLIST) {
                	int cnt = 0;
			cp = (struct playlist *)folder->udata;
                        for(struct track* t = cp->tracks; t; t = t->next) {
                        	cnt++;
				strip_slash(t->title);
                        	sprintf(track_name, "%03d - %s.wav", cnt, t->title);
				if (cnt == 1)
					it = add_sub_item(folder, track_name, ITEM_TYPE_FILE, DATA_TYPE_TRACK, (void *)t);
				else
					add_item(it, track_name, ITEM_TYPE_FILE, DATA_TYPE_TRACK, (void *)t);
                	} 
			folder = it;			
		}
        } else {
		folder = NULL;
	}
    }
    while(folder) {
	filler(buf, folder->name, NULL, 0);
	folder = (item_t *)folder->next;
    }

    return 0;
}

static int spotifs_open(const char *path, struct fuse_file_info *fi)
{
   int i, method_type, root_req, cnt;
   char folder_name[250], track_name[250], ext[5];
   char *ptr;
   struct track *t = NULL;
   struct playlist *cp = rootlist;
   item_t *curr; 

   if((fi->flags & 3) != O_RDONLY)
	return -EACCES;

   curr = extract_path_item(root, path);
   print_item(curr);

   if (!curr)
	return -ENOENT;

   if (curr->udata)
	t = (struct track *)curr->udata;

   if (!t)
	return -ENOENT;

   curr->tmp_buff = malloc(4096);
   curr->tmp_buff_pos = 0;
   fi->fh = (unsigned int)curr;
   despotify_play(ds, t, false);

   return 0;
}

static int spotifs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    size_t len, tlen;
    (void) fi;
    struct pcm_data pcm; 
    struct track *t;
    item_t *curr;
    int output_buff_pos = 0;
    int attempt = 10;
    len = 0;

    curr = (item_t*)fi->fh;
    t = (struct track *)curr->udata;
    print_item(curr);

    // Write WAV headers
    if (offset == 0) {
	output_buff_pos += write_wav_header(buf, (t->length/1000)*SAMPLING_RATE*NUM_CHANNELS*SAMPLE_SIZE);
    }
 
    if (curr->tmp_buff_pos) {
	memcpy(buf, curr->tmp_buff, curr->tmp_buff_pos);
	output_buff_pos += curr->tmp_buff_pos;
	curr->tmp_buff_pos = 0;
    }

    while (output_buff_pos < size && attempt > 0) {
  	    despotify_get_pcm(ds, &pcm);
	    printf("Sample rate: %d\nChannels: %d\n", pcm.samplerate, pcm.channels);
	
	    if (pcm.len == 0)
		attempt--;
            else
		attempt = 10;

	    len += pcm.len;
	    printf("Got len: %d buflen: %d size: %d\n", output_buff_pos, pcm.len, size); 
	    if ((output_buff_pos+pcm.len) <= size) {
		    memcpy(buf+output_buff_pos, pcm.buf, pcm.len);
		    output_buff_pos += pcm.len;
	    } else {
		    tlen = ((size-output_buff_pos) <= pcm.len) ? (size-output_buff_pos) : pcm.len;
		    printf("Tlen: %d\n", tlen);
		    memcpy(buf+output_buff_pos, pcm.buf, tlen);
		    output_buff_pos += tlen;
		    // Overflow handling
		    memcpy(curr->tmp_buff, pcm.buf+tlen, pcm.len-tlen);
		    curr->tmp_buff_pos = pcm.len-tlen;
	    }
    }

    return size;
}

static int spotifs_flush(const char *path, struct fuse_file_info *fi) {
    return 0;
}

static int spotifs_mkdir(const char *path, mode_t mode) {

    return 0;
}

static int spotifs_release(const char *path, struct fuse_file_info *fi) {
    item_t *curr;
    curr = (item_t*)fi->fh;
    if (curr) {
	if (curr->tmp_buff)
		free(curr->tmp_buff);
    } 
    despotify_stop(ds); 
    return 0;
}


static struct fuse_operations spotifs_oper = {
    .getattr	= spotifs_getattr,
    .readdir	= spotifs_readdir,
    .open	= spotifs_open,
    .read	= spotifs_read,
    .flush	= spotifs_flush,
    .mkdir	= spotifs_mkdir
};


void callback(struct despotify_session* ds, int signal, void* data, void* callback_data) {
    static int seconds = -1;
    (void)ds; (void)callback_data; /* don't warn about unused parameters */

    switch (signal) {
        case DESPOTIFY_NEW_TRACK: {
            struct track* t = data;
            printf("New track: %s / %s (%d:%02d) %d kbit/s\n",
                            t->title, t->artist->name,
                            t->length / 60000, t->length % 60000 / 1000,
                            t->file_bitrate / 1000);
            break;
        }

        case DESPOTIFY_TIME_TELL:
            if ((int)(*((double*)data)) != seconds) {
                seconds = *((double*)data);
                printf("Time: %d:%02d\r", seconds / 60, seconds % 60);
            }
            break;

        case DESPOTIFY_END_OF_PLAYLIST:
            printf("End of playlist\n");
            break;
    }
}

void *spotifs_start(void *arg) {
    printf("SpotiFS: Started\n");
    if (!despotify_init()) {
	printf("Despotify_init failed!\n");
	exit(1);
	return NULL;
    }
    ds = despotify_init_client(callback, NULL, true, true);
    if (!ds) {
	printf("Despotify_init_client() failed!\n");
	exit(1);
	return NULL;
    }

    if (!spotifs_config.login || !spotifs_config.pass) {
	fprintf(stderr, "No user and/or password given!\n");
	exit(1);
    }
    if (!despotify_authenticate(ds, spotifs_config.login, spotifs_config.pass)) {
	printf("Despotify authentication failed: %s\n", despotify_get_error(ds));
	despotify_exit(ds);
	exit(1);
	return NULL;
    }

    return NULL;    
}

void *fuse_start(void *arg) {

    root = add_item(root, "Playlists", ITEM_TYPE_DIR, DATA_TYPE_ITEM, NULL);
    add_item(root, "Search", ITEM_TYPE_DIR, DATA_TYPE_ITEM, NULL);
    add_item(root, "Starred", ITEM_TYPE_DIR, DATA_TYPE_ITEM, NULL);

    fuse_main(global_argc, global_argv, &spotifs_oper);
    return NULL;
}

int main(int argc, char *argv[])
{
    pthread_t spotifs_thread, fuse_thread;
    global_argc = argc;
    global_argv = argv;
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

    memset(&spotifs_config, 0, sizeof(spotifs_config));
    fuse_opt_parse(&args, &spotifs_config, spotifs_opts, NULL);
    global_argc = args.argc;
    global_argv = args.argv;
    pthread_create(&spotifs_thread, NULL, spotifs_start, NULL);
    pthread_create(&fuse_thread, NULL, fuse_start, NULL);

    pthread_join(spotifs_thread, NULL);
    pthread_join(fuse_thread, NULL);

    return 0;
}


