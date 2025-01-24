#pragma once

#include <stddef.h>

#define typeof_member(T, m)	typeof(((T*)0)->m)

#define container_of(ptr, type, member)				\
	(__typeof__(type))((char *)(ptr) -				\
			     offsetof(__typeof__(*type), member))

#define container_of_const(ptr, type, member)				\
	_Generic(ptr,							\
		const typeof(*(ptr)) *: ((const type *)container_of(ptr, type, member)),\
		default: ((type *)container_of(ptr, type, member))	\
	)

#define list_for_each(pos, head, member)				\
	for (pos = container_of((head)->next, pos, member);	\
	     &pos->member != (head);					\
	     pos = container_of(pos->member.next, pos, member))

struct hati_list {
    struct hati_list *prev, *next;
};

void hati_list_init(struct hati_list *list);
void hati_list_insert(struct hati_list *list, struct hati_list *elm);
void hati_list_remove(struct hati_list *elm);
int hati_list_length(const struct hati_list *list);
int hati_list_empty(const struct hati_list *list);
void hati_list_insert_list(struct hati_list *list, struct hati_list *other);