/*
    SpotiFUSE: Spotify Filesystem in Userspace
    Copyright (C) 2012  Attila Sukosd <attila.sukosd@gmail.com>

    This program can be distributed under the terms of the GNU GPL.


    WARNING: So far this is pretty much a hack to try to see if things work...
	     so use at your own risk, no quality assurance what so ever.
             Probably will crash a couple of times ;)

  
*/

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <despotify.h>

#define MAX_NAME_LENGTH 255


/* 
Lets assume some stuff here:
TODO: make an intelligent way of determining these
*/
#define SAMPLING_RATE 44100
#define NUM_CHANNELS 2
#define SAMPLE_SIZE 2 // (16bit)

int global_argc;
char **global_argv;
struct playlist* rootlist = NULL;
struct playlist* searchlist = NULL;
struct playlist* lastlist = NULL;
struct search_result *search = NULL;
struct album_browse* playalbum = NULL;
struct despotify_session* ds = NULL;

// Ugly stuff here... this needs to be done per file descriptor
static int track_length;
static char tmp_buff[4096];
static int tmp_buff_size = 0;

#define SEARCH_DIR 0
#define PLAYLISTS_DIR 1
#define STARRED_DIR 2

static const char *root_structure[] = {"Search", "Playlists", "Starred"};

typedef struct {
  char name[MAX_NAME_LENGTH];
  struct track *track;
  struct file *next;
  char *tmp_buff;
  int size;
} file_t;

typedef struct {
  char name[MAX_NAME_LENGTH];
  file_t *files;
  struct folder *next;
} folder_t;

folder_t *folders = NULL;

int write_wav_header(char *buf)
{
  char header[] = { 
    0x52, 0x49, 0x46, 0x46, 0x24, 0x40, 0x00, 0x00, 0x57, 0x41, 0x56, 0x45, 0x66, 0x6d, 0x74, 0x20,
    0x10, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x44, 0xac, 0x00, 0x00, 0x10, 0xb1, 0x02, 0x00,
    0x04, 0x00, 0x10, 0x00, 0x64, 0x61, 0x74, 0x61, 0xff, 0xff, 0xff, 0xff
  };  

  memcpy(buf, header, 44);
  return 44;
}

void build_path(const char *path, folder_t *folder_tree) {
	char *ptr = path;
	folder_tree = malloc(sizeof(folder_tree));
	while (ptr = strchr(ptr+1, '/')) {
		*(folder_tree->name) = '\0';
		
	}
}

void clean_path(folder_t *head) {
	folder_t *curr = head;
	folder_t *next;
	while (curr) {
		next = curr->next;
		free(curr);
		curr = next;
	}
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


    memset(stbuf, 0, sizeof(struct stat));
    //else
    //    res = -ENOENT;

    if(strcmp(path, "/") == 0) {
    	stbuf->st_mode = S_IFDIR | 0755;
	stbuf->st_nlink = 2;
    } else {
        root_req = 0;
        for(i=0;i<3;i++) {
                if (strncmp(path+1, root_structure[i], strlen(root_structure[i])) == 0) {
                        method_type = i;
                        if (strlen(path) == strlen(root_structure[i])+1) // Exact folder request
                                root_req = 1;
			else {
			        *(folder_name) = '\0';
                                strcat(folder_name, path+strlen(root_structure[i])+2);
                                printf("F: %s\n", folder_name);
                                ptr = strchr(folder_name, '/');
                                *(track_name) = '\0';
                                if (ptr) strcat(track_name, ptr+1);
                                if (ptr) *ptr = '\0';

			}
                }
        }
	if (root_req) {
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
	} else {
		switch(method_type) {
			case SEARCH_DIR:
			case STARRED_DIR:
				stbuf->st_mode = S_IFDIR | 0755;
				stbuf->st_nlink = 2;
				break;
			case PLAYLISTS_DIR:
				cnt = 0;
				ptr = path;
				while (ptr = strchr(ptr+1, '/')) {
					printf("hi %d\n", cnt);
					if (ptr) cnt++;
				}
				
				if (cnt <= 1) { 
					stbuf->st_mode = S_IFDIR | 0755;
					stbuf->st_nlink = 2;
				} else {
	                                if (!rootlist) {
        	                                printf("no rootlist\n");
               	   	                        rootlist = despotify_get_stored_playlists(ds);
                           		}

	                                // Find the playlist from the root                      
        	                        for(cp = rootlist; cp; cp = cp->next) {
        	                                if (strcmp(cp->name, folder_name) == 0)
                	                                break;
                	                }
                	                // No playlist found
                	                if (!cp) return -ENOENT;
                	                // Find the tracks in the playlist
                	                for(ct = cp->tracks; ct; ct = ct->next) {
						if (strncmp(ct->title, track_name+6, strlen(ct->title)) == 0)
                                                        break;
					}
					stbuf->st_mode = S_IFREG | 0444;
					stbuf->st_nlink = 1;
					stbuf->st_size = (ct->length/1000)*SAMPLING_RATE*NUM_CHANNELS*SAMPLE_SIZE; // TODO: rounding to seconds right now...
				}
				break;
			default:
				res = -ENOENT;
		}
	}
    }
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
	int i, method_type, root_req;
    struct playlist *cp = rootlist;
    char track_name[250];
    char folder_name[250];
    char *ptr;


    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    if(strcmp(path, "/") == 0) {
	for(i=0;i<3;i++) {
		filler(buf, root_structure[i], NULL, 0);
	}
    } else {
	root_req = 0;
	for(i=0;i<3;i++) {
		if (strncmp(path+1, root_structure[i], strlen(root_structure[i])) == 0) {
			method_type = i;
			if (strlen(path) == strlen(root_structure[i])+1) // Exact folder request
				root_req = 1;
			else {
				*(folder_name) = '\0';
				strcat(folder_name, path+strlen(root_structure[i])+2);
				ptr = strchr(folder_name, '/');
				if (ptr) *ptr = '\0';
			}
		}
	}	

	printf("Path: %s\n", path);	
	printf("Folder name: %s\n", folder_name);

	switch(method_type) {
		case SEARCH_DIR:
			filler(buf, "search", NULL, 0);
			break;
		case PLAYLISTS_DIR:
			if (root_req) { // Get the list of playlists
				if (!rootlist) {
					printf("no rootlist\n");
					rootlist = despotify_get_stored_playlists(ds);
				}
				printf("Getting all the playlists: %s\n", despotify_get_error(ds));
				for(struct playlist *p = rootlist; p; p = p->next)
					filler(buf, p->name, NULL, 0);
			} else { // Otherwise the tracks inside the playlist
                                if (!rootlist) {
                                        printf("no rootlist\n");
                                        rootlist = despotify_get_stored_playlists(ds);
                                }
				
				// Find the playlist from the root			
				for(cp = rootlist; cp; cp = cp->next) {
					if (strcmp(cp->name, folder_name) == 0)
						break;
				}
				// No playlist found
				if (!cp) return -ENOENT;				
				// Find the tracks in the playlist
				int cnt = 0;
				for(struct track* t = cp->tracks; t; t = t->next) {
					cnt++;
					sprintf(track_name, "%03d - %s.wav", cnt, t->title);
					filler(buf, track_name, NULL, 0);
				}		
			}
			break;
		case STARRED_DIR:
			filler(buf, "starred", NULL, 0);
			break;
		default:
			return -ENOENT; 
	}
    }

    return 0;
}

static int spotifs_open(const char *path, struct fuse_file_info *fi)
{
   int i, method_type, root_req, cnt;
   char folder_name[250], track_name[250], ext[5];
   char *ptr;
   struct track *t;
   struct playlist *cp = rootlist;
   
 
   if(strcmp(path, "/") == 0) {
    	return -ENOENT;
   } else {
        root_req = 0;
        for(i=0;i<3;i++) {
                if (strncmp(path+1, root_structure[i], strlen(root_structure[i])) == 0) {
                        method_type = i;
                        if (strlen(path) == strlen(root_structure[i])+1) // Exact folder request
                                root_req = 1;
                        else {
                                *(folder_name) = '\0';
                                strcat(folder_name, path+strlen(root_structure[i])+2);
                                printf("F: %s\n", folder_name);
                                ptr = strchr(folder_name, '/');
				*(track_name) = '\0';
				strcat(track_name, ptr+1);
                                if (ptr) *ptr = '\0';
			}
                }
        }

	printf("Folder name: %s\nTrack name: %s\n", folder_name, track_name);

        if (root_req) {
        	return -ENOENT;
	} else {
                switch(method_type) {
                        case SEARCH_DIR:
                        case STARRED_DIR:
                                return -ENOENT;
				break;
                        case PLAYLISTS_DIR:
			        cnt = 0;
                                ptr = path;
                                while (ptr = strchr(ptr+1, '/')) {
                                        if (ptr) cnt++;
                                }

                                if (cnt <= 1) {
                                	return -ENOENT;
				} else {
	                                if (!rootlist) {
        	                                printf("no rootlist\n");
                	                        rootlist = despotify_get_stored_playlists(ds);
                        	        }

                                	for(cp = rootlist; cp; cp = cp->next) {
                                        	if (strcmp(cp->name, folder_name) == 0)
                                        	        break;
                                	}
                              		// No playlist found
                                	if (!cp) return -ENOENT;
                                	// Find the tracks in the playlist
                                	for(t = cp->tracks; t; t = t->next) {
						printf("%s\n", track_name+6);
						if (strncmp(t->title, track_name+6, strlen(t->title)) == 0)
							break;                                   
                                	}
	
					if (!t) return -ENOENT;

					ptr = strrchr(track_name, '.');
					strcpy(ext, ptr+1);
					if (strncmp(ext, "wav", 3)) return -ENOENT;

					track_length = t->length;
			/*		
					struct file_t *tmpf = malloc(sizeof(file_t));
					strcpy(tmpf->name, track_name);
					tmpf->track = t;
	
					fi->fh = (void *)tmpf;
			*/
					// Play track
					despotify_play(ds, t, true);
				}
                               	return 0;
			default:
				return -ENOENT;
		}
	}
    }

    return 0;
}

static int spotifs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    size_t len, tlen;
    (void) fi;
    struct pcm_data pcm;
    int output_buff_pos = 0;
    int attempt = 10;
    len = 0;

    // Write WAV headers
    if (offset == 0) {
	output_buff_pos += write_wav_header(buf);
    }
 
    if (tmp_buff_size) {
	memcpy(buf, tmp_buff, tmp_buff_size);
	output_buff_pos += tmp_buff_size;
	tmp_buff_size = 0;
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
		    memcpy(tmp_buff, pcm.buf+tlen, pcm.len-tlen);
		    tmp_buff_size = pcm.len-tlen;
	    }
    }
    printf("Ret with: %d from size: %d\n", output_buff_pos);

    return size;
}

static int spotifs_flush(const char *path, struct fuse_file_info *fi) {
    return 0;
}

static int spotifs_mkdir(const char *path, mode_t mode) {

    return 0;
}

static int spotifs_release(const char *path, struct fuse_file_info *fi) {
/*    struct file_t *tmpf = NULL;
    tmpf = (struct file_t *)fi->fh;

    if (tmpf && tmpf->tmp_buff)
	free(tmpf->tmp_buff);

    if (tmpf) free(tmpf);
*/
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

    if (!despotify_authenticate(ds, "", "")) {
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


