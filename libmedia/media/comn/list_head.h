
#ifndef list_head_h
#define list_head_h

#include "mydef.h"

struct list_head {
    struct list_head *next, *prev;
};

#define LIST_HEAD_INIT(name) { &(name), &(name) }

#define LIST_HEAD(name) \
struct list_head name = LIST_HEAD_INIT(name)

#define INIT_LIST_HEAD(ptr) do { \
    (ptr)->next = (ptr); (ptr)->prev = (ptr); \
} while (0)

static void inline __list_add(struct list_head * new1,
                       struct list_head * prev,
                       struct list_head * next)
{
    next->prev = new1;
    new1->next = next;
    new1->prev = prev;
    prev->next = new1;
}

#define list_add(new1, head) \
do { \
    __list_add(new1, head, (head)->next); \
} while (0)

#define list_add_tail(new1, head) \
do { \
    __list_add(new1, (head)->prev, head); \
} while (0)

static void inline __list_del(struct list_head * prev,
                       struct list_head * next)
{
    next->prev = prev;
    prev->next = next;
}

#define list_del(entry) \
do { \
    __list_del((entry)->prev, (entry)->next); \
} while (0)

#define list_del_init(entry) \
do { \
    __list_del((entry)->prev, (entry)->next); \
    INIT_LIST_HEAD((entry)); \
} while (0)

static int inline list_empty(struct list_head *head)
{
    return head->next == head;
}

static int inline list_empty_no_optimalize(struct list_head *head)
{
    return ((volatile struct list_head*) head->next) == head;
}

static inline int list_is_singular(struct list_head *head)
{
	return !list_empty(head) && (head->next == head->prev);
}

static void inline list_join(struct list_head *one, struct list_head *two)
{
    one = one->prev;
    one->next->prev = two->prev;
    two->prev->next = one->next;
    one->next = two;
    two->prev = one;
}

static void inline list_split(struct list_head *one, struct list_head *two)
{
    struct list_head* entry = two->prev;
    one->prev->next = two;
    two->prev = one->prev;
    one->prev = entry;
    entry->next = one;
}

#define list_entry(ptr, type, member) container_of(ptr, type, member)

static void inline print_list(struct list_head* head, const char* pch){
    head = head->prev;
    struct list_head* ent = NULL;

    if (pch) {
        logmsg("%s", pch);
    }

    for (ent = head->next; ent != head; ent = ent->next) {
        if (ent->next->prev == ent) {
            logmsg("%p---->", ent);
        } else {
            my_assert2(0, "%p, but prev not match", ent);
        }
    }

    if (ent->next->prev == ent) {
        logmsg("%p---->", ent);
    } else {
        my_assert2(0, "%p, but prev not match", ent);
    }
    logmsg("%p\n", ent->next);
}

#endif
