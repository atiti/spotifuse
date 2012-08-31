#include "item_mgr.h"

item_t*  add_item(item_t *head, char *name, char itype, char dtype, void *data) {
        item_t *curr = head;
        item_t *new = malloc(sizeof(item_t));
        strcpy(new->name, name);
	item_id++;
	new->id = item_id;
        new->itype = itype;
        new->dtype = dtype;
        new->data = data;
        new->next = NULL;
        new->prev = NULL;

        while (curr && curr->next) {
                curr = (item_t *)curr->next;
        }

	if (!curr) {
		new->prev = NULL;
		curr = (struct item_t *)new;
	} else {
	        new->prev = (struct item_t *)curr;
       		curr->next = (struct item_t *)new;
	}
	return new;
}

item_t* add_sub_item(item_t *head, char *name, char itype, char dtype, void *data) {
	item_t *curr = head;
	item_t *i;
	i = add_item(NULL, name, itype, dtype, data);
	i->prev = head;
	if (curr) {
		curr->dtype = DATA_TYPE_ITEM;
		curr->data = (void *)i;
	}
	return i;	
}

void print_item(item_t *i) {
	item_t *in, *ip;
	if (i) {
		printf("Item ID: %d (%p)\n", i->id, i);
		printf("Item name: %s\n", i->name);
		printf("Item type: %d\n", i->itype);
		printf("Data type: %d\n", i->dtype);
		printf("Data ptr: %p\n", i->data);
		if (i->next) {
			in = i->next;
			printf("Next ptr: %d (%p)\n", in->id, i->next);
		} else
			printf("Next ptr: %p\n", i->next);
		if (i->prev) {
			ip = i->prev;
			printf("Prev ptr: %d (%p)\n", ip->id, i->prev);
		} else
			printf("Prev ptr: %p\n", i->prev);
	} else {
		printf("Unalloced item\n");
	}
}

void print_items(item_t *head) {
	item_t *curr = head;
	while (curr) {
		printf("***************\n");
		print_item(curr);
		if (curr->dtype == DATA_TYPE_ITEM && curr->data)
			print_items((item_t *)curr->data);

		curr = (item_t*)curr->next;
	}
	printf("***************\n");
}

item_t* find_item_by_name(item_t *head, char *name) {
	item_t *curr = head;
	item_t *res = NULL;
        while (curr) {
                if (curr->dtype == DATA_TYPE_ITEM && curr->data) {
                        res = find_item_by_name((item_t *)curr->data, name);
                        if (res) break;
                }
                if (strcmp(curr->name, name) == 0) {
                        res = curr;
                        break;
                }
                curr = (item_t*)curr->next;
        }
        return res;
}

item_t* find_item_by_id(item_t *head, int id) {
	item_t *curr = head;
	item_t *res = NULL;
	while (curr) {
		if (curr->dtype == DATA_TYPE_ITEM && curr->data) {
			res = find_item_by_id((item_t *)curr->data, id);
			if (res) break;
		}
		if (curr->id == id) {
			res = curr;
			break;
		}
		curr = (item_t*)curr->next;
	}
	return res;
}

void free_items(item_t *head) {
	item_t *curr = head;
	item_t *tmp;
	while (curr) {
		if (curr->dtype == DATA_TYPE_ITEM && curr->data) // recursive free tree structure
			free_items((item_t *)curr->data);
		else if (curr->data) // just free the data
			free(curr->data);
			
		if (LINKED_LIST_DEBUG) printf("Freeing %p\n", curr);
		tmp = curr->next;
		free(curr);
		curr = tmp;
	}
}

#if 0
void main() {
	item_t *i, *sub, *root;
	root = add_item(root, "test", ITEM_TYPE_DIR, DATA_TYPE_ITEM, NULL);
	i = add_item(root, "test 2", ITEM_TYPE_DIR, DATA_TYPE_ITEM, NULL);
	i = add_sub_item(i, "test 2.1", ITEM_TYPE_DIR, DATA_TYPE_ITEM, NULL);
	add_item(i, "test 2.2", ITEM_TYPE_DIR, DATA_TYPE_ITEM, NULL);
	add_item(i, "test 2.3", ITEM_TYPE_DIR, DATA_TYPE_ITEM, NULL);
	add_item(root, "test 3", ITEM_TYPE_DIR, DATA_TYPE_ITEM, NULL);

	print_items(root);

	i = find_item_by_id(root, 4);
	print_item(i);

	i = find_item_by_name(root, "test 2.3");
	print_item(i);

	free_items(root);
}
#endif
