#include "list.h"

void hati_list_init(struct hati_list *list) {
	list->prev = list;
	list->next = list;
}

void hati_list_insert(struct hati_list *list, struct hati_list *elm) {
	elm->prev = list;
	elm->next = list->next;
	list->next = elm;
	elm->next->prev = elm;
}

void hati_list_remove(struct hati_list *elm) {
	elm->prev->next = elm->next;
	elm->next->prev = elm->prev;
	elm->next = NULL;
	elm->prev = NULL;
}

int hati_list_length(const struct hati_list *list) {
	struct hati_list *e;
	int count;
	count = 0;
	e = list->next;
	while (e != list) {
		e = e->next;
		count++;
	}
	return count;
}

int hati_list_empty(const struct hati_list *list) {
	return list->next == list;
}

void hati_list_insert_list(struct hati_list *list, struct hati_list *other) {
	if (hati_list_empty(other))
		return;
	other->next->prev = list;
	other->prev->next = list->next;
	list->next->prev = other->prev;
	list->next = other->next;
}
