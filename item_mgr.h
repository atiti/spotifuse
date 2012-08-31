#ifndef __ITEM_MGR_H__
#define __ITEM_MGR_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINKED_LIST_DEBUG 1

#define MAX_NAME_LENGTH 255

#define ITEM_TYPE_FILE 1
#define ITEM_TYPE_DIR 2
#define ITEM_TYPE_SYMLINK 3

#define DATA_TYPE_ITEM 0
#define DATA_TYPE_PLAYLIST 1
#define DATA_TYPE_TRACK 2
#define DATA_TYPE_ALBUM 3
#define DATA_TYPE_ALBUMART 4

static int item_id = 0;

typedef struct {
  int id;
  char name[MAX_NAME_LENGTH];
  char itype;
  char dtype;
  void *data;
  void *udata;
  char *tmp_buff;
  int tmp_buff_pos;
  struct item_t *next;
  struct item_t *prev;
} item_t;

item_t*  add_item(item_t *head, char *name, char itype, char dtype, void *udata);
item_t* add_sub_item(item_t *head, char *name, char itype, char dtype, void *udata);
item_t* find_item_by_name_simple(item_t *head, char *name);
item_t* find_item_by_name(item_t *head, char *name);
item_t* find_item_by_id_simple(item_t *head, int id);
item_t* find_item_by_id(item_t *head, int id);
void print_item(item_t *i);
void print_items(item_t *head);
void free_items(item_t *head);

#endif
